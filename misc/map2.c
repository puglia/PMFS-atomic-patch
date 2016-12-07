#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAP_XIP_COW	0x04


#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

int increment = (100 << 12);

int main(int argc, char *argv[]){
           char *addr;
           int fd;
           struct stat sb;
           off_t offset, pa_offset;
           size_t length;
           ssize_t s,old_s;
	   int x, error;
	   printf("My process ID : %d\n", getpid());

           fd = open("/mnt/pmfs/test", O_RDWR);
           if (fd == -1)
               handle_error("open");
stats:
           if (fstat(fd, &sb) == -1)           /* To obtain file size */
               handle_error("fstat");
	  
	   printf("size 1 %d\n",sb.st_size);
	   old_s = sb.st_size;
	   
           offset = 0;
           pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
               /* offset for mmap() must be page aligned */

           if (offset >= sb.st_size) {
               fprintf(stderr, "offset is past end of file\n");
               //exit(EXIT_FAILURE);
		ftruncate(fd,increment);
		goto stats;
           }

	   length = sb.st_size - offset;

           addr = mmap(NULL, length+increment, PROT_READ | PROT_WRITE,
                       MAP_XIP_COW, fd, 0);
           if (addr == MAP_FAILED)
               handle_error("mmap");
	  //ftruncate(fd,sb.st_size+increment);
	  //posix_fallocate(fd,sb.st_size,increment);
	  printf("ftruncate size %d\n",sb.st_size);
	  if (fstat(fd, &sb) == -1)         
               handle_error("fstat");
	 printf("size 2 %d\n",sb.st_size);
	 for(x = 0; x<sb.st_size; x++)
		addr[x] = 'p';
	 
	   printf("X: %d\n",x);
	   msync(addr, length, MS_SYNC);
	   error = munmap(addr, length);
	   if(error)
		printf("\nerror: %d", error);
           exit(EXIT_SUCCESS);
       }
