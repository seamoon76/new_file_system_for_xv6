
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main()
{
    char buf[1024];
    int fd, i, blocks;

    fd = open("big.file", O_CREATE | O_WRONLY, "iam@admin9876");
    if (fd < 0)
    {
        printf("big: cannot open big.file for writing\n");
        exit(0);
    }

    blocks = 0;
    while (1)
    {
        *(int *)buf = blocks;
        int cc = write(fd, buf, sizeof(buf));
        if (cc <= 0)
            break;
        blocks++;
        if (blocks % 100 == 0)
            printf("blocks:%d\n",blocks);
        if (blocks >= 100000) {
             printf("more than 10w! success!");
             break;
         }
    }

    printf("\nwrote %d blocks\n", blocks);

    close(fd);
    fd = open("big.file", O_RDONLY, "iam@admin9876");
    if (fd < 0)
    {
        printf("big: cannot re-open big.file for reading\n");
        exit(0);
    }
    for (i = 0; i < blocks; i++)
    {
        int cc = read(fd, buf, sizeof(buf));
        if (cc <= 0)
        {
            printf("big: read error at block %d\n", i);
            exit(0);
        }
        if (*(int *)buf != i)
        {
            printf("big: read the wrong data (%d) for block %d\n",
                   *(int *)buf, i);
        }
    }

    printf("done; ok\n");

    exit(0);
}