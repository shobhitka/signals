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
        printf("APP2: Running thread tid: %u, app id: %d\n", (unsigned int) tid, num);
        sleep(3 * num);
    }

    printf("APP2: Stopping thread tid: %u, app id: %d\n", (unsigned int) tid, num);
    pthread_exit(NULL);
}

void signal_handler(int signum)
{
    switch(signum) {
        case SIGTERM:
        {
            printf("APP2: Received SIGTERM\n");
            stop = 1;
            // putting sleep so that we can see the threads terminating.
            sleep(10);
            exit(0);
        }
        case SIGINT:
        {
            printf("APP2: Received SIGINT, Ignoring for now\n");
            break;           
        }
    }
}

int main()
{
    pthread_t tid1, tid2;
    int app_id = 3;
    pid_t pid = fork();

    // Creating simple background daemon. This is not perfect but will do as
    // a test daemon
    if (pid > 0) {
        // exit parent
        exit(0);
    }

    // make child as session leader
    setsid();

    // ideally we should close STDOUT and STDIN as they are no longer valid as parent 
    // exitted, and we should log in a file, like syslog.
    // but letting logs to come on console even though process is in background for 
    // just checking all others logs intermixed with these

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // launch some threads 
    pthread_create(&tid1, NULL, thread_handler, (void *) &app_id);
    pthread_detach(tid1);
    sleep(1);
    app_id = 4;
    pthread_create(&tid2, NULL, thread_handler, (void *) &app_id);
    pthread_detach(tid2);

    while (1) {
        sleep(10);
    }
}