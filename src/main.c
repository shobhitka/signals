#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

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
	STATUS_PROGRAM_LAUNCH_SUCCESS = 0,
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
		"test1", 
		"./test1",
		{ "./test1", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		10,
		PROGRAM_TYPE_SIMPLE,
		-1,
		STATUS_PROGRAM_LAUNCH_PENDING,
	},
	{
		"test2", 
		"./test2",
		{ "./test1", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		10,
		PROGRAM_TYPE_SIMPLE,
		-1,
		STATUS_PROGRAM_LAUNCH_PENDING,
	},
};

char *get_program_status(int status)
{
	switch(status) {
		case 0:
			return "ACTIVE";
		case -1:
			return "STOPPED";
		case -2:
			return "FAILED";
		case -3:
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
			printf("Failed to launch program: %s, error: %s\n", program->name, strerror(errno));
			return -1;
		}
	} else if (program->pid > 0 && retVal == 0) {
		// parent, print child details and return
		printf("Succesfully launched: %s, pid: %d\n", program->name, program->pid);
		program->status = STATUS_PROGRAM_LAUNCH_SUCCESS;
		return 0;
	}

	printf("vfork() failed for program: %s, error: %s\n", program->name, strerror(errno));
	program->status = STATUS_PROGRAM_LAUNCH_FAIL;
	return -1;
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

	if (cnt)
		printf("Some program failed to launch. Failed count: %d\n", cnt);

	dump_programs();

	while (1)
		sleep(10);

	return (0);
}