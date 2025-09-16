#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

const int maxn = 35;
const int szi = sizeof(int);

int
main(int argc, char *argv[])
{
	int p2c[2];
	pipe(p2c);
	int pid = fork();
	if (pid) { // fa
		close(p2c[0]);
		for (int i = 2; i <= maxn; ++i) {
			write(p2c[1], &i, szi);
		}
		close(p2c[1]);
		wait(0);
		exit(0);
	} else { // son
		while (true) {
			int p, nxt;
			close(p2c[1]);
			if (read(p2c[0], &p, szi) == 0) {
				wait(0);
				exit(0);
			}
			int c2s[2];
			pipe(c2s);

			pid = fork();
			if (pid) {
				close(c2s[0]);
				fprintf(1, "prime %d\n", p);
				while (read(p2c[0], &nxt, szi) != 0)
					if (nxt % p)
						write(c2s[1], &nxt, szi);
				close(c2s[1]);
				close(p2c[0]);
				wait(0);
				exit(0);
			} else {
				p2c[0] = c2s[0];
				p2c[1] = c2s[1];
			}
		}
	}
}