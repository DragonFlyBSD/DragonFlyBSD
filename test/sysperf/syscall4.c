/*
 * syscall1.c
 *
 * $DragonFly: src/test/sysperf/syscall4.c,v 1.1 2003/11/19 00:59:19 dillon Exp $
 */

#include "blib.h"

int
main(int ac, char **av)
{
    const char *path;
    long long count = 0;
    long long max;
    struct stat st;
    int j;

    printf("timing standard stat() syscall\n");
    if (ac > 1)
	path = av[1];
    else
	path = ".";

    start_timing();
    while (stop_timing(0, NULL) == 0) {
	for (j = 0; j < 100; ++j)
	    stat(path, &st);
	count += 100;
    }
    max = count;
    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j)
	    stat(path, &st);
    }
    stop_timing(count, "stat()");
    return(0);
}

