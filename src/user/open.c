#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(2, "unvalid input!\n");
        exit(1);
    }

    open(argv[1], O_CREATE | O_RDWR, argv[2]);
    exit(0);
}