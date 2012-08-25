/*
 * syscall1.c
 *
 * $DragonFly: src/test/sysperf/syscall5.c,v 1.1 2005/03/28 03:13:24 dillon Exp $
 */

#include "blib.h"

int
main(int ac, char **av)
{
    long long count = 0;
    long long max;
    struct timespec ts;
    int j;

    printf("timing standard clock_gettime() syscall\n");

    start_timing();
    while (stop_timing(0, NULL) == 0) {
	for (j = 0; j < 100; ++j)
	    clock_gettime(CLOCK_REALTIME_FAST, &ts);
	count += 100;
    }
    max = count;
    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j)
	    clock_gettime(CLOCK_REALTIME_FAST, &ts);
    }
    stop_timing(count, "getuid()");
    return(0);
}
