#include <kernel/types.h>

#include <user/user.h>

int main(void) {
    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);
    int pid = fork();
    if (pid) {
        close(p1[0]);
        close(p2[1]);
        write(p1[1], "ping", 5);
        char rcv[5];
        read(p2[0], rcv, 5);
        printf("<%d>: received %s\n", getpid(), rcv);
        int status;
        wait(&status);
    } else {
        close(p1[1]);
        close(p2[0]);
        char rcv[5];
        read(p1[0], rcv, 5);
        printf("<%d>: received %s\n", getpid(), rcv);
        write(p2[1], "pong", 5);
    }
    exit(0);
}
