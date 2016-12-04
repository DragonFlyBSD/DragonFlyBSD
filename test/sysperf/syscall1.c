/*
 * syscall1.c
 *
 * N thread getuid timing test (default 1)
 */

#include "blib.h"

int
main(int ac, char **av)
{
	long long count = 0;
	long long max;
	char c;
	int n;
	int i;
	int j;
	int status;

	printf("timing standard getuid() syscall, single thread\n");
	printf("if using powerd, run several times\n");

	start_timing();
	while (stop_timing(0, NULL) == 0) {
		for (j = 0; j < 100; ++j)
			getuid();
		count += 100;
	}
	max = count;

	if (ac > 1)
		n = strtol(av[1], NULL, 0);
	else
		n = 1;

	start_timing();
	for (i = 0; i < n; ++i) {
		if (fork() == 0) {
			for (count = 0; count < max; count += 100) {
				for (j = 0; j < 100; ++j)
					getuid();
			}
			_exit(0);
		}
	}
	while (wait3(&status, 0, NULL) >= 0 || errno == EINTR)
		;
	stop_timing(count * n, "getuid()");

	return(0);
}

