/*
 * call3.c
 *
 * Test a standard function call to a function which does nothing much.
 *
 * $DragonFly: src/test/sysperf/call3.c,v 1.3 2005/08/02 17:11:04 hmp Exp $
 */

#include "blib.h"

#define LOOP 500000000

__asm(".p2align 6");
static void xnop1(void) { }
__asm(".p2align 6");
static void xnop2(void) { }
__asm(".p2align 6");
static void xnop(void (*func)(void)) { func(); }

int
main(int ac, char **av)
{
    int i;

    printf("call nop() function through function pointer in loop\n");
    start_timing();
    for (i = 0; i < LOOP; ++i) {
	xnop(xnop1);
	xnop(xnop1);
    }
    stop_timing(LOOP * 2, "call3/nop1-1");
    start_timing();
    for (i = 0; i < LOOP; ++i) {
	xnop(xnop1);
	xnop(xnop2);
    }
    stop_timing(LOOP * 2, "call3/nop1-2");
    start_timing();
    for (i = 0; i < LOOP; ++i) {
	xnop(xnop2);
	xnop(xnop2);
    }
    stop_timing(LOOP * 2, "call3/nop2-2");
    return(0);
}

