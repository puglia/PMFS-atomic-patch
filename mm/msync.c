/*
 *	linux/mm/msync.c
 *
 * Copyright (C) 1994-1999  Linus Torvalds
 */

/*
 * The msync() system call.
 */
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/syscalls.h>
#include <linux/sched.h>

/*
 * MS_SYNC syncs the entire file - including mappings.
 *
 * MS_ASYNC does not start I/O (it used to, up to 2.5.67).
 * Nor does it marks the relevant pages dirty (it used to up to 2.6.17).
 * Now it doesn't do anything, since dirty pages are properly tracked.
 *
 * The application may now run fsync() to
 * write out the dirty pages and wait on the writeout and check the result.
 * Or the application may run fadvise(FADV_DONTNEED) against the fd to start
 * async writeout immediately.
 * So by _not_ starting I/O in MS_ASYNC we provide complete flexibility to
 * applications.
 */
SYSCALL_DEFINE3(msync, unsigned long, start, size_t, len, int, flags)
{
	unsigned long end, fsync_start, fsync_end;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	size_t file_offset;
	int unmapped_error = 0;
	int error = -EINVAL;

	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC | (0x008 | 0x010 | 0x020 | 0x040 | 0x080 | 0x100)))
		goto out;
	if (start & ~PAGE_MASK)
		goto out;
	if ((flags & MS_ASYNC) && (flags & MS_SYNC))
		goto out;
	error = -ENOMEM;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;
	error = 0;
	if (end == start)
		goto out;
	/*
	 * If the interval [start,end) covers some unmapped address ranges,
	 * just ignore them, but return -ENOMEM at the end.
	 */
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, start);
	for (;;) {
		struct file *file;

		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out_unlock;
		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			start = vma->vm_start;
			if (start >= end)
				goto out_unlock;
			unmapped_error = -ENOMEM;
		}
		/* Here vma->vm_start <= start < vma->vm_end. */
		if ((flags & MS_INVALIDATE) &&
				(vma->vm_flags & VM_LOCKED)) {
			error = -EBUSY;
			goto out_unlock;
		}
		file = vma->vm_file;
		fsync_start = start;
		fsync_end = min(end, vma->vm_end);
		start = vma->vm_end;
		if ((flags & MS_SYNC) && file &&
				(vma->vm_flags & VM_SHARED)) {
			get_file(file);
			up_read(&mm->mmap_sem);
			if(vma->vm_flags & VM_XIP_COW){		
			error = vfs_fsync_range(file,fsync_start, fsync_end - 1, flags & 0x008?13:flags & 0x010?14:flags & 0x020?15:flags & 0x040?16:flags & 0x080?17:flags & 0x100?18:12 );
			if(flags & 0x100){printk("flushy\n");
				goto flushy;}

			}
			else{
flushy:
				file_offset = vma->vm_pgoff * PAGE_SIZE;
				error = vfs_fsync_range(file, 
						file_offset + fsync_start - vma->vm_start,
						file_offset + fsync_end - vma->vm_start - 1, (vma->vm_flags & VM_ATOMIC)?11:0);
			}
			fput(file);
			if (error || start >= end)
				goto out;
			down_read(&mm->mmap_sem);
			vma = find_vma(mm, start);
		} else {
			if (start >= end) {
				error = 0;
				goto out_unlock;
			}
			vma = vma->vm_next;
		}
	}
out_unlock:
	up_read(&mm->mmap_sem);
out:
	return error ? : unmapped_error;
}
