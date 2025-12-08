#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{	
	printf("%d\n", getpid());
	printf("a\n");
	int pid = fork();
	if (pid == 0)
		printf("%d\n", getpid());
	fork();
	printf("%d\n", getpid());
	printf("c\n");
	wait(0);
	exit(0);
}