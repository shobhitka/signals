#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>

typedef struct program_t
{
	char name[64];
	char command[256];
	char *args[64];
	int shutdown_promise;
	int daemon;
	pid_t pid;
	int status;
} program_t;

typedef enum {
	STATUS_PROGRAM_LAUNCH_ACTIVE = 0,
	STATUS_PROGRAM_STOPPED = -1,
	STATUS_PROGRAM_LAUNCH_FAIL = -2,
	STATUS_PROGRAM_LAUNCH_PENDING = -3,
} program_staus_t;

typedef enum {
	PROGRAM_TYPE_SIMPLE = 1,
	PROGRAM_TYPE_DAEMON = 2,
	PROGRAM_TYPE_SCRIPT = 3,
} program_type_t;

program_t programs[] = {
	{
		"script 1", 
		"/home/shokumar/sandbox/procmon-clone/script1.sh",
		{ "/home/shokumar/sandbox/procmon-clone/scipt1.sh", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		10,
		PROGRAM_TYPE_SIMPLE,
		-1,
		STATUS_PROGRAM_LAUNCH_PENDING,
	},
	{
		"script 2", 
		"/home/shokumar/sandbox/procmon-clone/script2.sh",
		{ "/home/shokumar/sandbox/procmon-clone/scipt2.sh", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		10,
		PROGRAM_TYPE_SIMPLE,
		-1,
		STATUS_PROGRAM_LAUNCH_PENDING,
	},
};

static int running = 0;

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
		default:
			return "FAILED";
	}
}

void dump_programs()
{
	for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
		printf("Name: %s --> status: %s, pid: %d\n", programs[i].name, get_program_status(programs[i].status), programs[i].pid);
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

// handle program termination using SIGINT
void signal_handler(int signum)
{
	switch(signum) {
		case SIGCHLD:
		{
			program_t *program;

			// loop through all active programs and wait for them as SIGCHLD can get
			// colaced and we might miss some of them
			for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
				program = &programs[i];
				if (program->status != STATUS_PROGRAM_LAUNCH_ACTIVE)
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
					printf("This hsould not happen but, waitpid() failed for program: %s, pid: %d\n", program->name, program->pid);
					continue;
				}
			}

			if (running == 0) {
				printf("All programs terminated. quitting\n");
				exit(0);
			}
			break;
		}
		case SIGINT:
		{
			if (running == 0) {
				printf("All programs terminated. quitting\n");
				exit(0);
			}

			// send SIGTERM to all running programs
			for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
				if (programs[i].status == STATUS_PROGRAM_LAUNCH_ACTIVE) {
					printf("Sending SIGTERM to program: %s, pid: %d\n", programs[i].name, programs[i].pid);
					kill(programs[i].pid, SIGTERM);

					// TBD: shutdown promise implementation
				}
			}
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
	int cnt = 0;

	// launch all programs
	for (unsigned int i = 0; i < sizeof(programs)/sizeof(program_t); i++) {
		if (launch_program(&programs[i]) < 0) {
			cnt++;
		}
	}

	if (cnt) {
		printf("Some program failed to launch. Failed count: %d\n", cnt);
		dump_programs();
	}

	// register signal handlers
	signal(SIGCHLD, signal_handler);
	signal(SIGINT, signal_handler);

	while (1)
		sleep(10);

	return (0);
}