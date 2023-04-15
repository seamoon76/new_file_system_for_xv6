#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main() {
    //两个管道，1->父写 2->子写
    int p1[2];
    int p2[2];
    //缓冲区 一个字符
    char buf[1] = {'p'};
    pipe(p1);
    pipe(p2);
    if (fork() == 0) {
        close(p1[1]);
        close(p2[0]);

        if (write(p2[1], buf, sizeof(buf)) == sizeof(buf)) {
            printf("w");
            printf("%d", sizeof(buf));
        } else {
            printf("child wrote error");
        }
        if (read(p1[0], buf, sizeof(buf)) == sizeof(buf)) {
            printf("r");
        } else {
            printf("child received error");
        }
        exit(0);
    } else {
        close(p2[1]);
        close(p1[0]);

        if (write(p1[1], buf, sizeof(buf)) == sizeof(buf)) {
            printf("W");
            printf("%d", sizeof(buf));
        } else {
            printf("parent wrote error");
        }
        if (read(p2[0], buf, sizeof(buf)) == sizeof(buf)) {
            printf("R");
        } else {
            printf("parent received error");
        }
        exit(0);
    }
    exit(0);
}