#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(2, "unvalid input!\n");
        exit(1);
    }

    chspmode(argv[1], argv[2], atoi(argv[3]));

    exit(0);
}