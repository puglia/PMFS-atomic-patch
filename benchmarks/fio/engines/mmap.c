/*
 * mmap engine
 *
 * IO engine that reads/writes from files by doing memcpy to/from
 * a memory mapped region of the file.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/timeb.h>

#include "pcm_i.h"

#include "../fio.h"
#include "../verify.h"

/*
 * Limits us to 1GB of mapped files in total
 */
#define MMAP_TOTAL_SZ	(1 * 1024 * 1024 * 1024UL)

#define MAP_XIP_COW 0x04

#define XIP_COW 1
#define TIME_CONT 0
#define WRITES_BEFORE_MSYNC 6
#define MILLIS_BEFORE_MSYNC 5000

static unsigned long mmap_map_size;
static unsigned long mmap_map_mask;

static int fio_mmap_file(struct thread_data *td, struct fio_file *f,
			 size_t length, off_t off)
{
	int flags = 0;

	if (td_rw(td))
		flags = PROT_READ | PROT_WRITE;
	else if (td_write(td)) {
		flags = PROT_WRITE;

		if (td->o.verify != VERIFY_NONE)
			flags |= PROT_READ;
	} else
		flags = PROT_READ;
	if(f->mmap_ptr == NULL)
		f->mmap_ptr = mmap(NULL, length, flags, XIP_COW?MAP_XIP_COW:MAP_SHARED, f->fd, off);

	if (f->mmap_ptr == MAP_FAILED) {
		f->mmap_ptr = NULL;
		td_verror(td, errno, "mmap");
		goto err;
	}

	if (!td_random(td)) {
		if (posix_madvise(f->mmap_ptr, length, POSIX_MADV_SEQUENTIAL) < 0) {
			td_verror(td, errno, "madvise");
			goto err;
		}
	} else {
		if (posix_madvise(f->mmap_ptr, length, POSIX_MADV_RANDOM) < 0) {
			td_verror(td, errno, "madvise");
			goto err;
		}
	}

err:
	if (td->error && f->mmap_ptr)
		munmap(f->mmap_ptr, length);

	return td->error;
}

/*
 * Just mmap an appropriate portion, we cannot mmap the full extent
 */
static int fio_mmapio_prep_limited(struct thread_data *td, struct io_u *io_u)
{
	struct fio_file *f = io_u->file;

	if (io_u->buflen > mmap_map_size) {
		log_err("fio: bs too big for mmap engine\n");
		return EIO;
	}

	f->mmap_sz = mmap_map_size;
	if (f->mmap_sz  > f->io_size)
		f->mmap_sz = f->io_size;

	f->mmap_off = io_u->offset;

	return fio_mmap_file(td, f, f->mmap_sz, f->mmap_off);
}

/*
 * Attempt to mmap the entire file
 */
static int fio_mmapio_prep_full(struct thread_data *td, struct io_u *io_u)
{
	struct fio_file *f = io_u->file;
	int ret;

	if (fio_file_partial_mmap(f))
		return EINVAL;

	f->mmap_sz = f->io_size;
	f->mmap_off = 0;

	ret = fio_mmap_file(td, f, f->mmap_sz, f->mmap_off);
	if (ret)
		fio_file_set_partial_mmap(f);

	return ret;
}

int auxcont = 0;
int stinit = 0;
struct timeb start;
struct timeb stop;
struct timeb start_wr,stop_wr,start_ms,stop_ms;
unsigned long total_wr,total_ms;

static int should_msync(){
	if(!TIME_CONT){
		auxcont++;
		if(auxcont <= WRITES_BEFORE_MSYNC)
			return 0;
		auxcont = 0;
		return 1;
	}

	if(!stinit){		
		ftime(&start);
		stinit++;
	}
	auxcont++;
	ftime(&stop);
	int diff = (int) ((stop.time * 1000) + stop.millitm) - ((start.time*1000) + start.millitm);
	//printf("time:%d\n",diff);
	if(diff < MILLIS_BEFORE_MSYNC)
		return 0;
	printf("Cont:%d  ###\n\n",auxcont);
	auxcont = 0;
	ftime(&start);
	return 1;
}

static int fio_mmapio_prep(struct thread_data *td, struct io_u *io_u)
{
	struct fio_file *f = io_u->file;
	int ret;
	/*
	 * It fits within existing mapping, use it
	 */
	if (io_u->offset >= f->mmap_off &&
	    io_u->offset + io_u->buflen < f->mmap_off + f->mmap_sz)
		goto done;

	/*
	 * unmap any existing mapping
	 */
	if (f->mmap_ptr && should_msync() && !(io_u->ddir == DDIR_READ)) {
		ftime(&start_ms);
		if (msync(f->mmap_ptr, f->mmap_sz, MS_SYNC | 0x008 )) {
			io_u->error = errno;
			td_verror(td, io_u->error, "msync");
			printf("msync error\n");
		}

		ftime(&stop_ms);
		total_ms += ((stop_ms.time * 1000) + stop_ms.millitm) - ((start_ms.time*1000) + start_ms.millitm);
		int total_ovr = total_wr + total_ms;
		
		//printf("Time spent writing %lf Time spent syncing %lf   #####\n\n",(double)total_wr/total_ovr,(double)total_ms/total_ovr);
		//total_wr=0;
		if (munmap(f->mmap_ptr, f->mmap_sz) < 0)
			return errno;
		f->mmap_ptr = NULL;
	}

	if (fio_mmapio_prep_full(td, io_u)) {
		td_clear_error(td);
		ret = fio_mmapio_prep_limited(td, io_u);
		if (ret)
			return ret;
	}

done:
	io_u->mmap_data = f->mmap_ptr + io_u->offset - f->mmap_off -
				f->file_offset;
	return 0;
}

static int fio_mmapio_queue(struct thread_data *td, struct io_u *io_u)
{
	struct fio_file *f = io_u->file;

	fio_ro_check(td, io_u);

	if (io_u->ddir == DDIR_READ)
		memcpy(io_u->xfer_buf, io_u->mmap_data, io_u->xfer_buflen);
	else if (io_u->ddir == DDIR_WRITE){
		ftime(&start_wr);
		memcpy(io_u->mmap_data, io_u->xfer_buf, io_u->xfer_buflen);
		//printf("Address: %llx\n",io_u->mmap_data);
		if(!XIP_COW)
			emulate_latency(io_u->xfer_buflen);

		ftime(&stop_wr);
		total_wr += ((stop_wr.time * 1000) + stop_wr.millitm) - ((start_wr.time*1000) + start_wr.millitm);
	}
	else if (ddir_sync(io_u->ddir)){	
		/*if (msync(f->mmap_ptr, f->mmap_sz, MS_SYNC | 0x008)) {
			io_u->error = errno;
			td_verror(td, io_u->error, "msync");
			printf("msync error\n");
		}*/
	} else if (io_u->ddir == DDIR_TRIM) {
		int ret = do_io_u_trim(td, io_u);

		if (!ret)
			td_verror(td, io_u->error, "trim");
	}

	/*
	 * not really direct, but should drop the pages from the cache
	 */
	if (td->o.odirect && ddir_rw(io_u->ddir)) {
		if (msync(io_u->mmap_data, io_u->xfer_buflen, MS_SYNC) < 0) {
			io_u->error = errno;
			td_verror(td, io_u->error, "msync");
		}
		if (posix_madvise(io_u->mmap_data, io_u->xfer_buflen, POSIX_MADV_DONTNEED) < 0) {
			io_u->error = errno;
			td_verror(td, io_u->error, "madvise");
		}
	}

	return FIO_Q_COMPLETED;
}

static int fio_mmapio_init(struct thread_data *td)
{
	struct thread_options *o = &td->o;
	unsigned long shift, mask;

	if ((td->o.rw_min_bs & page_mask) &&
	    (o->odirect || o->fsync_blocks || o->fdatasync_blocks)) {
		log_err("fio: mmap options dictate a minimum block size of "
			"%llu bytes\n", (unsigned long long) page_size);
		return 1;
	}

	mmap_map_size = MMAP_TOTAL_SZ / td->o.nr_files;
	mask = mmap_map_size;
	shift = 0;
	do {
		mask >>= 1;
		if (!mask)
			break;
		shift++;
	} while (1);

	mmap_map_mask = 1UL << shift;
	return 0;
}

static struct ioengine_ops ioengine = {
	.name		= "mmap",
	.version	= FIO_IOOPS_VERSION,
	.init		= fio_mmapio_init,
	.prep		= fio_mmapio_prep,
	.queue		= fio_mmapio_queue,
	.open_file	= generic_open_file,
	.close_file	= generic_close_file,
	.get_file_size	= generic_get_file_size,
	.flags		= FIO_SYNCIO | FIO_NOEXTEND,
};

static void fio_init fio_mmapio_register(void)
{
	register_ioengine(&ioengine);
}

static void fio_exit fio_mmapio_unregister(void)
{
	unregister_ioengine(&ioengine);
}
