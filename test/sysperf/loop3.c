/*
 * loop3.c
 *
 * used as a helper to test AST delivery.  This is an endless loop.
 *
 * $DragonFly: src/test/sysperf/loop3.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

static void nop() { }

int
main(int ac, char **av)
{
    int i;

    printf("(non timing) one process, endless loop in userland\n");
    for (;;)
	nop();
    return(0);
}

