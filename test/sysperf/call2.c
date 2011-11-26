/*
 * call2.c
 *
 * Test a standard function call to a function which does nothing much.
 *
 * $DragonFly: src/test/sysperf/call2.c,v 1.2 2005/08/02 17:11:04 hmp Exp $
 */

#include "blib.h"

#define LOOP 1000000000

static void xnop(void) { }

static void (*xnop_ptr)(void) = xnop;

int
main(int ac, char **av)
{
    int i;

    printf("call nop() function through function pointer in loop\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
	xnop_ptr();
    stop_timing(LOOP, "loop1/user");
    return(0);
}

