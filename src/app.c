#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>

static volatile int stop = 0;

void *thread_handler(void *data)
{
    pthread_t tid = syscall(SYS_gettid);
    int num = *((int *) data);
    while(stop == 0) {
        printf("C_APP: Running thread tid: %u, app id: %d\n", (unsigned int) tid, num);
        sleep(3 * num);
    }

    printf("C_APP: Stopping thread tid: %u, app id: %d\n", (unsigned int) tid, num);
    pthread_exit(NULL);
}

void signal_handler(int signum)
{
    switch(signum) {
        case SIGTERM:
        {
            printf("Received SIGTERM in APP1 --> Received in thread id: %u\n", (unsigned int) syscall(SYS_gettid));
            stop = 1;
            break;
        }
        case SIGINT:
        {
            printf("APP1: Received SIGINT, Ignoring for now\n");
            break;
        }
    }
}

int main()
{
    pthread_t tid1, tid2;
    int app_id = 1;
    sigset_t sigset;

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // block all signals for the process
    sigfillset(&sigset);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    // launch some threads
    pthread_create(&tid1, NULL, thread_handler, (void *) &app_id);
    sleep(1);
    app_id = 2;
    pthread_create(&tid2, NULL, thread_handler, (void *) &app_id);

    // Enable only SIGTERM and SIGINT
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);

    // block till both threads terminate
    pthread_join(tid1, NULL); // will block till tid1 terminates
    pthread_join(tid2, NULL); // will block till tid2 terminates
}

