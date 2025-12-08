#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"


int
main(int argc, char *argv[])
{
	char str[100] = {}, c = 0;
	char *argvs[MAXARG];
	int argcs = 0;
	for (; argcs + 1 < argc; ++argcs) {
		int lens = strlen(argv[argcs + 1]);
		argvs[argcs] = malloc(lens + 1);
		memmove(argvs[argcs], argv[argcs + 1], lens);
		argvs[argcs][lens] = '\0';
	}
	while (c != 255) {
		int argcsA = 0;
		for (int i = 0; i < 100; ++i) {
			if (read(0, &c, 1) == 0)
				c = 255;
			if (c == '\n' || c == 255) {
				str[i] = '\0';
				break;
			}
			else {
				str[i] = c;
			}
		}
		int lst = 0;
		for (int i = 0; str[i]; ++i) {
			if (str[i + 1] == ' ' || str[i + 1] == '\0') {
				int lens = i + 1 - lst;
				argvs[argcs + argcsA] = malloc(lens + 1);
				memmove(argvs[argcs + argcsA], str + lst, lens);
				argvs[argcs + argcsA][lens] = '\0';
				lst = i + 2;
				++argcsA;
			}
		}
		if (fork() == 0) {
			argvs[argcs + argcsA] = 0;
			exec(argvs[0], argvs);
			exit(0);
		}
		for (int i = 0; i < argcsA; ++i) {
			free(argvs[argcs + i]);
			argvs[argcs + i] = 0;
		}
	}
	for (int i = 0; i < argcs; ++i) {
		free(argvs[i]);
		argvs[i] = 0;
	}
	exit(0);
}