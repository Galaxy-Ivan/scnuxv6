#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
	int p2c[2], c2p[2];
	char word[1];
	pipe(p2c);
	pipe(c2p);
	int pid = fork(); // get son pid at father, and 0 at son
	if (pid) {
		close(p2c[0]);
		close(c2p[1]);
		write(p2c[1], "x", 1);
		read(c2p[0], word, 1);
		fprintf(1, "%d: received pong\n", getpid());
		close(p2c[1]);
		close(c2p[0]);
		wait(0);
		exit(0);
	} else {
		close(p2c[1]);
		close(c2p[0]);
		read(p2c[0], word, 1);
		fprintf(1, "%d: received ping\n", getpid());
		close(p2c[0]);
		close(c2p[1]);
		exit(0);
	}
}