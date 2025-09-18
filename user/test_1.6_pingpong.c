#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
	int start, end, pptimes = 1e4;
	
	char word[1];

    start = uptime();

    int p2c[2], c2p[2];
	pipe(p2c);
	pipe(c2p);
	int pid = fork();
	if (pid) {
		close(p2c[0]);
		close(c2p[1]);
		while (pptimes--) {
			write(p2c[1], "p", 1);
			read(c2p[0], word, 1);
		}
		close(p2c[1]);
		close(c2p[0]);
	} else {
		close(p2c[1]);
		close(c2p[0]);
		while (read(p2c[0], word, 1))
			write(c2p[1], "p", 1);
		close(p2c[0]);
		close(c2p[1]);
		exit(0);
	}

    end = uptime();

    int ticks = end - start;
    printf("elapsed: %d ticks, ~%d ms\n", ticks, ticks*10);
	wait(0);
	exit(0);
}
