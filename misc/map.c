#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAP_XIP_COW	0x04

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

int main(int argc, char *argv[]){
           char *addr,strg;
           int fd;
           struct stat sb;
           off_t offset, pa_offset;
           size_t length;
           ssize_t s;
	   int x, error;
	   printf("My process ID : %d\n", getpid());

           fd = open("/mnt/pmfs/test", O_RDWR);
           if (fd == -1)
               handle_error("open");

           if (fstat(fd, &sb) == -1)           /* To obtain file size */
               handle_error("fstat");

           offset = 0;
           pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
               /* offset for mmap() must be page aligned */

           if (offset >= sb.st_size) {
               fprintf(stderr, "offset is past end of file\n");
               exit(EXIT_FAILURE);
           }
	   printf("sizes - %lld \n",sb.st_size);
	   length = sb.st_size - offset;

           addr = mmap(NULL, length + offset - pa_offset, PROT_READ | PROT_WRITE,
                       MAP_XIP_COW, fd, pa_offset);

           if (addr == MAP_FAILED)
               handle_error("mmap");
	  // printf("%s",addr);
	   printf("ok\n");

	   
stuff:		
	   scanf("%d", &x);
	   if(x)
		offset = x;
           else goto out;
  	   addr[offset++] = 'b';
	   addr[offset++] = 'y';
	   
	   printf("ok\n");
	   //printf("%s",addr);
	   
	   
	   goto stuff;
out:	
	   msync(addr, length, MS_SYNC);
	   error = munmap(addr, length);
	   if(error)
		printf("\nerror: %d", error);
           exit(EXIT_SUCCESS);
       }
