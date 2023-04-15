#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

int extentfile(int filenum){
    if(filenum>(int)MAXFILE)
    filenum=(int)MAXFILE;
    int fd, i;
    char fname[] = "extentfile";
    char data[1024];

    printf("\nExtentTest starting\n");
    memset(data, 'a', sizeof(data));
    int start_tick = uptime();
    fd = open(fname, O_CREATE | O_RDWR | O_EXTENT,"iam@admin9876");
    for(i = 0; i < filenum; i++){
//    printf(fd, "%d\n", i);
    // if (i % 400 == 0)
    //         printf(".");
    write(fd, data, sizeof(data));}
    close(fd);
    int end_tick = uptime();
    int write_time_cost=end_tick-start_tick;
    printf("write %d blocks to a file use%d ticks.\n",filenum,write_time_cost);
    //printf("read\n");
    start_tick=uptime();

    fd = open(fname, O_RDONLY|O_EXTENT,"iam@admin9876");
    //struct stat st2;
	//fstat(fd,&st2);
    //showstat(&st2);
    for (i = 0; i < filenum; i++)
    read(fd, data, sizeof(data));
    close(fd);
    end_tick=uptime();
    int read_time_cost=end_tick-start_tick;
    printf("read %d blocks to a file use%d ticks.\n",filenum,read_time_cost);
    
	
    if(unlink(fname)<0){
        printf("remove file failed.\n");
    }
    return 0;
}

int blockmapfile(int filenum){
    if(filenum>(int)MAXFILE)
    filenum=(int)MAXFILE;
    int fd, i;
    char fname[] = "blockmapfile";
    char data[1024];

    printf("\nBlock map test starting\n");
    memset(data, 'a', sizeof(data));
    int start_tick = uptime();
    int cc=0;
    fd = open(fname, O_CREATE | O_RDWR,"iam@admin9876");
    for(i = 0; i < filenum; i++){
    cc=write(fd, data, sizeof(data));
    if(cc<0)
    {
        break;
    }
    // if (i % 400 == 0)
    //     printf(".");
    }
    close(fd);
    int end_tick = uptime();
    int write_time_cost=end_tick-start_tick;
    printf("write %d blocks to a file use%d ticks.\n",filenum,write_time_cost);
    //printf("read\n");
    start_tick=uptime();

    fd = open(fname, O_RDONLY,"iam@admin9876");
    for (i = 0; i < filenum; i++)
        read(fd, data, sizeof(data));
    close(fd);
    end_tick=uptime();
    int read_time_cost=end_tick-start_tick;
    printf("read %d blocks to a file use%d ticks.\n",filenum,read_time_cost);
    if(unlink(fname)<0){
        printf("remove file failed.\n");
    }
    return 0;
}


int main(int argc, char *argv[]){
   
        int filenum=atoi(argv[1]);
        printf("test w/r block num=%d",filenum);
        blockmapfile(filenum);
        extentfile(filenum);
        

    exit(0);
}