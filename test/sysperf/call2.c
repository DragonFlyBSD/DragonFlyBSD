/*
 * call1.c
 *
 * Test a standard function call to a function which does nothing much.
 *
 * $DragonFly: src/test/sysperf/call2.c,v 1.1 2004/03/20 01:51:01 dillon Exp $
 */

#include "blib.h"

#define LOOP 1000000000

static void nop(void) { }

static void (*nop_ptr)(void) = nop;

int
main(int ac, char **av)
{
    int i;

    printf("call nop() function through function pointer in loop\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
	nop_ptr();
    stop_timing(LOOP, "loop1/user");
    return(0);
}

