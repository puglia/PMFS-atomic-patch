#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BUF_SIZE 1 << 20

const char *file_name= "/mnt/pmfs/test";

int main(int argc,char *aa[]){
    int fd,i;
    char *buffer = malloc(BUF_SIZE);

   for(i = 0; i<BUF_SIZE; i++)
	buffer[i] = 'k';
    

    fd=open(file_name,O_RDWR);
    if(fd==-1){
        printf("file not found.\n");
        return -1;
    }

    write(fd,buffer,BUF_SIZE);
    close(fd);
}
