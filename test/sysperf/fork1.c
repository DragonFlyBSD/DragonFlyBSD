/*
 * fork1.c
 *
 * $DragonFly: src/test/sysperf/fork1.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

int
main(int ac, char **av)
{
    int j;

    start_timing();
    for (j = 0; j < 10000; ++j) {
	if (fork() == 0) {
	    _exit(1);
	} else {
	    while (wait(NULL) > 0);
	}
    }
    stop_timing(j, "fork/exit/wait:");
    return(0);
}

