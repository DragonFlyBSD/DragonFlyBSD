/*
 * call1.c
 *
 * Test a standard function call to a function which does nothing much.
 *
 * $DragonFly: src/test/sysperf/call1.c,v 1.1 2004/03/20 01:51:01 dillon Exp $
 */

#include "blib.h"

#define LOOP 1000000000

static void nop() { }

int
main(int ac, char **av)
{
    int i;

    printf("call nop() function in loop\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
	nop();
    stop_timing(LOOP, "loop1/user");
    return(0);
}

