/*
 * call1.c
 *
 * Test a standard function call to a function which does nothing much.
 *
 * $DragonFly: src/test/sysperf/call3.c,v 1.2 2004/03/20 02:04:28 dillon Exp $
 */

#include "blib.h"

#define LOOP 500000000

__asm(".p2align 6");
static void nop1(void) { }
__asm(".p2align 6");
static void nop2(void) { }
__asm(".p2align 6");
static void nop(void (*func)(void)) { func(); }

int
main(int ac, char **av)
{
    int i;

    printf("call nop() function through function pointer in loop\n");
    start_timing();
    for (i = 0; i < LOOP; ++i) {
	nop(nop1);
	nop(nop1);
    }
    stop_timing(LOOP * 2, "call3/nop1-1");
    start_timing();
    for (i = 0; i < LOOP; ++i) {
	nop(nop1);
	nop(nop2);
    }
    stop_timing(LOOP * 2, "call3/nop1-2");
    start_timing();
    for (i = 0; i < LOOP; ++i) {
	nop(nop2);
	nop(nop2);
    }
    stop_timing(LOOP * 2, "call3/nop2-2");
    return(0);
}

