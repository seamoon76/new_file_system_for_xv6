#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/sysinfo.h"

int main(int argc, char* argv[]) {
    if (argc != 1) {
        fprintf(2, "error");
        exit(1);
    }
    struct sysinfo info;
    sysinfo(&info);
    printf("free space: %d", info.freemem);
    exit(0);
}