/*
 * loop2.c
 *
 * Used as a helper to test AST delivery.  Loops in user mode for a few
 * seconds.  This one forks and runs the loop in two processes.
 *
 * $DragonFly: src/test/sysperf/loop2.c,v 1.2 2006/04/22 22:32:52 dillon Exp $
 */

#include "blib.h"

#define LOOP 1000000
#define INNER 100

int
main(int ac, char **av)
{
    int i;
    int j;
    int count = LOOP;
    pid_t pid;

    if (ac > 1)
	count = strtoul(av[1], NULL, 0);

    printf("SMP contention, userland-only loop, duel-forks.  Run just one\n");

    start_timing();
    if (fork() == 0) {
	for (i = count; i > 0; --i) {
	    for (j = INNER; j > 0; --j)
		nop();
	}
	_exit(1);
    } else {
	for (i = count; i > 0; --i) {
	    for (j = INNER; j > 0; --j)
		nop();
	}
	while (wait(NULL) > 0)
	    ;
	stop_timing(LOOP, "loop2/2xfork");
    }
    return(0);
}

