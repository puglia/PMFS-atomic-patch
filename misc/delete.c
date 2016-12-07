#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BUF_SIZE 50 << 20

const char *file_name= "/mnt/pmfs/anotherstuff";

int main(int argc,char *aa[]){
    unlink(file_name);
}
