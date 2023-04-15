#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(2, "unvalid input!\n");
        exit(1);
    }
    for (int i = 1; i < argc; i++)
    {
        if (delete (argv[i]) < 0)
        {
            printf("failed to delete %s\n", argv[i]);
        }
    }
    exit(0);
}