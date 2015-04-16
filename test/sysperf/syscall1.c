/*
 * syscall1.c
 *
 * Single thread getuid timing test.
 */

#include "blib.h"

int
main(int ac, char **av)
{
	long long count = 0;
	long long max;
	char c;
	int j;

	printf("timing standard getuid() syscall, single thread\n");
	printf("if using powerd, run several times\n");

	start_timing();
	while (stop_timing(0, NULL) == 0) {
		for (j = 0; j < 100; ++j)
			getuid();
		count += 100;
	}
	max = count;
	start_timing();
	for (count = 0; count < max; count += 100) {
		for (j = 0; j < 100; ++j)
			getuid();
	}
	stop_timing(count, "getuid()");
	return(0);
}

