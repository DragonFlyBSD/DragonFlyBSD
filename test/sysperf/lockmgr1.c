/*
 * varsym..c
 *
 * varsym [threads]
 *
 * tests shared lock using varsym_get()
 */

#include "blib.h"
#include <sys/varsym.h>

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
	char buf[256];

	printf("timing standard varsym_get() syscall, VARSYM_SYS\n");

	start_timing();
	while (stop_timing(0, NULL) == 0) {
		varsym_get(VARSYM_SYS_MASK, "fubar", buf, sizeof(buf));
		++count;
	}
	max = count * 4;

	if (ac > 1)
		n = strtol(av[1], NULL, 0);
	else
		n = 1;

	start_timing();
	for (i = 0; i < n; ++i) {
		if (fork() == 0) {
			for (count = 0; count < max; ++count) {
				varsym_get(VARSYM_SYS_MASK, "fubar",
					   buf, sizeof(buf));
			}
			_exit(0);
		}
	}
	while (wait3(&status, 0, NULL) >= 0 || errno == EINTR)
		;
	stop_timing(max * n, "varsym1");

	return(0);
}
