/*
 * loop1.c
 *
 * used as a helper to test AST delivery.  Loops in user mode for 5 seconds.
 * $DragonFly: src/test/sysperf/loop1.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

#define LOOP 100000000

static void nop() { }

int
main(int ac, char **av)
{
    int i;

    printf("SMP contention, userland-only loop (run one, then run ncpu copies in parallel\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
	nop();
    stop_timing(LOOP, "loop1/user");
    return(0);
}

