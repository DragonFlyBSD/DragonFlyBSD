/*
 * loop2.c
 *
 * Used as a helper to test AST delivery.  Loops in user mode for a few
 * seconds.  This one forks and runs the loop in two processes.
 *
 * $DragonFly: src/test/sysperf/loop2.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

#define LOOP 100000000

static void nop() { }

int
main(int ac, char **av)
{
    int i;
    pid_t pid;

    printf("SMP contention, userland-only loop, duel-forks.  Run just one\n");

    start_timing();
    if (fork() == 0) {
	for (i = 0; i < LOOP; ++i)
	    nop();
	_exit(1);
    } else {
	for (i = 0; i < LOOP; ++i)
	    nop();
	while (wait(NULL) > 0);
	stop_timing(LOOP, "loop2/2xfork");
    }
    return(0);
}

