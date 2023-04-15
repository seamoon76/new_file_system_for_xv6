#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

int main() {
    open("./a", O_CREATE | O_RDWR,"iam@admin9876");
    symlink("./a", "./b");
    int fd1 = open("./a", O_RDWR,"iam@admin9876");
    write(fd1, "a", 1);
    // printf("%d", r);
    exit(0);
}
