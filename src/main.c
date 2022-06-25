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

#define UNUSED(expr) do { (void)(expr); } while (0)
#define QUICK_START_TIME	10 // seconds
#define QUICK_RESTART_COUNT	5

typedef struct program_t
{
	char name[64];
	char command[256];
	char *args[64];
	int shutdown_promise;
	int type;
	int restart;
	pid_t pid;
	int status;
	int restart_cnt;
	time_t last_restart;
	int quick_restart_cnt;
} program_t;

typedef enum {
	STATUS_PROGRAM_LAUNCH_ACTIVE = 0,
	STATUS_PROGRAM_STOPPED = -1,
	STATUS_PROGRAM_LAUNCH_FAIL = -2,
	STATUS_PROGRAM_LAUNCH_PENDING = -3,
	STATUS_PROGRAM_STOPPING = -4,
} program_staus_t;

typedef enum {
	PROGRAM_TYPE_SIMPLE = 1,
	PROGRAM_TYPE_DAEMON = 2,
	PROGRAM_TYPE_SCRIPT = 3,
} program_type_t;

typedef enum {
	ALLOW_PROGRAM_RUN_ONETIME = 1,
	ALLOW_PROGRAM_RUN_RESTART = 2,
} program_restart_t;

typedef enum {
	RUNLEVEL_STATE_STARTING = 1,
	RUNLEVEL_STATE_PROGRESS = 2,
	RUNLEVEL_STATE_STABLE = 3,
} runlevel_state_t;

program_t programs[] = {
	{
		"start p1", 
		"/home/shokumar/sandbox/procmon-clone/start_p1.sh",
		{ "/home/shokumar/sandbox/procmon-clone/start_p1.sh", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		10,
		PROGRAM_TYPE_SIMPLE,
		ALLOW_PROGRAM_RUN_RESTART,
		-1,
		STATUS_PROGRAM_LAUNCH_PENDING,
		-1,
		0,
		0,
	},
	{
		"script 2", 
		"/home/shokumar/sandbox/procmon-clone/script2.sh",
		{ "/home/shokumar/sandbox/procmon-clone/scipt2.sh", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		10,
		PROGRAM_TYPE_SIMPLE,
		ALLOW_PROGRAM_RUN_RESTART,
		-1,
		STATUS_PROGRAM_LAUNCH_PENDING,
		-1,
		0,
		0,
	},
};

static volatile sig_atomic_t running = 0;
static volatile sig_atomic_t procmon_abort = 0;
static volatile sig_atomic_t runlevel_state = RUNLEVEL_STATE_STARTING;

char *get_program_status(int status)
{
	switch(status) {
		case STATUS_PROGRAM_LAUNCH_ACTIVE:
			return "ACTIVE";
		case STATUS_PROGRAM_STOPPED:
			return "STOPPED";
		case STATUS_PROGRAM_LAUNCH_FAIL:
			return "FAILED";
		case STATUS_PROGRAM_LAUNCH_PENDING:
			return "PENDING";
		case STATUS_PROGRAM_STOPPING:
			return "STOPPING";
		default:
			return "UNKNOWN";
	}
}

void dump_programs()
{
	for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
		printf("Name: %s --> status: %s (%d), restart cnt: %d, pid: %d\n", programs[i].name, get_program_status(programs[i].status), programs[i].status, programs[i].restart_cnt, programs[i].pid);
	}
}

int launch_program(program_t *program)
{
	int retVal = 0;
	program->pid = vfork();
	if (program->pid == 0) {
		// child, overlay the program image on this pid
		if ((retVal = execv(program->command, program->args)) < 0) {
			program->status = STATUS_PROGRAM_LAUNCH_FAIL;
			exit(-1);
		}
	} else if (program->pid > 0 && retVal == 0) {
		// parent, print child details and return
		printf("Succesfully launched: %s, pid: %d\n", program->name, program->pid);
		program->status = STATUS_PROGRAM_LAUNCH_ACTIVE;
		program->last_restart = time(NULL);
		running++;
		return 0;
	} else {
		if (retVal != 0) {
			printf("Failed to launch program: %s, error: %s\n", program->name, strerror(errno));

			// wait for vforked thread which exits when it fails to start a new program using execv
			wait(NULL);
		} else
			printf("vfork() failed for program: %s, error: %s\n", program->name, strerror(errno));
		
		program->status = STATUS_PROGRAM_LAUNCH_FAIL;
		return -1;
	}

	return -1;
}

void kill_runlevel()
{
	program_t *program;
	for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
		program = &programs[i];
		if (program->status == STATUS_PROGRAM_LAUNCH_ACTIVE) {
			printf("Sending SIGTERM to program: %s, pid: %d\n", program->name, program->pid);
			program->status = STATUS_PROGRAM_STOPPING;
			kill(program->pid, SIGTERM);
		}
	}
}

void *launch_runlevel_programs(void *data)
{
	int cnt = 0;
	program_t *program;

	UNUSED(data);

	runlevel_state = RUNLEVEL_STATE_PROGRESS;

	// launch all programs
	for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
		program = &programs[i];

		if (program->restart || program->restart_cnt == -1) {
			if (program->last_restart != 0) {
				// check if we are doing a quick restart for persistent failures
				time_t curr = time(NULL);
				if ((program->last_restart - curr) < QUICK_START_TIME) {
					// quick restarts
					program->quick_restart_cnt++;
					if (program->quick_restart_cnt > QUICK_RESTART_COUNT) {
						// we need to terminate all and quit
						kill_runlevel();
						procmon_abort = 2;
						if (running == 0) {
							// no more SIGCHLD will come, simply abort here
							dump_programs();
							printf("Programs restarting too frequently. Aborting for good.\n");
							exit(0);
						}
						pthread_exit(NULL);
					}
				}
			}
			if (launch_program(program) < 0) {
				cnt++;
			}

			// increment the restart count either way as a succefull or failed attampt
			program->restart_cnt++;
		}		
	}

	if (cnt) {
		printf("Some program failed to launch. Failed count: %d\n", cnt);
	}

	runlevel_state = RUNLEVEL_STATE_STABLE;

	dump_programs();
	pthread_exit(NULL);
}

void launch_runlevel()
{
	pthread_t tid;
	pthread_create(&tid, NULL, launch_runlevel_programs, NULL);
	pthread_detach(tid);
}

// handle program termination using SIGINT
void signal_handler(int signum)
{
	switch(signum) {
		case SIGUSR1:
		case SIGUSR2:
			{
				dump_programs();
				break;
			}
		case SIGCHLD:
		{
			program_t *program;

			// loop through all active programs and wait for them as SIGCHLD can get
			// colaced and we might miss some of them
			for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
				program = &programs[i];
				if (program->status != STATUS_PROGRAM_LAUNCH_ACTIVE && program->status != STATUS_PROGRAM_STOPPING)
					continue;

				pid_t wpid = waitpid(program->pid, NULL, WNOHANG);
				if (wpid == 0) {
					// this pid did not exit
					continue;
				} else if (wpid == program->pid) {
					// program exited
					printf("Terminated Program: %s\n", program->name);
					program->status = STATUS_PROGRAM_STOPPED;
					program->pid = -1;
					running--;
				} else {
					// error in waitpid system call; should not happem
					printf("This should not happen but, waitpid() failed for program: %s, pid: %d\n", program->name, program->pid);
					continue;
				}
			}

			dump_programs();
			if (running == 0 && procmon_abort) {
				if (procmon_abort == 1)
					printf("All programs terminated. Quitting\n");
				else if (procmon_abort == 2) {
					dump_programs();
					printf("Programs restarting too frequently. Quitting for good.\n");
				}

				exit(0);
			}

			if (running == 0 && procmon_abort == 0) {
				printf("All programs terminated. Restarting runlevel\n");
				launch_runlevel();
				return;
			}

			// some prorams are still running. We are simulating procmon runlevel actually
			// so send SIGTERM to terminate all other programs
			// this can happen when we kill the program using pkill -15
			kill_runlevel();

			break;
		}
		case SIGSEGV:
			{
				kill_runlevel();
				// log or dump anything you ould want

				// if I fell through the switchcase with signum as SIGSEGV, I just can
				// try to send the SIGTERM to any launched program and then assume
				// they terminate well. I don't care for SIGCHLD neither can I handle that
				// when it comes later as don't know what is my program state. So just
				// generate core dump by raising SIGSEGV signal again and get terminated.
				printf("Segfaulting myself to generate core dump\n");
				signal(SIGSEGV, SIG_DFL);
				kill(getpid(), SIGSEGV);
				break;
			}
		case SIGTERM:
			{
				printf("Received SIGTERM. Terminating all launched programs\n");
				procmon_abort = 1;
				kill_runlevel();
				break;
			}
		case SIGINT:
		{
			printf("Received SIGINT, terminating all programs and aborting.\n");

			if (running == 0) {
				printf("All programs terminated. quitting\n");
				exit(0);
			}

			procmon_abort = 1;
			kill_runlevel();
			break;
		}
		default:
		{
			printf("Unhandled signal number: %d\n", signum);
			break;
		}
	}
}

int main()
{
	// register signal handlers
	signal(SIGCHLD, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGUSR1, signal_handler);
	signal(SIGUSR2, signal_handler);

	launch_runlevel();

	while (runlevel_state != RUNLEVEL_STATE_STABLE) {
		sleep(1);
	}

	while (1)
		sleep(10);

	return (0);
}