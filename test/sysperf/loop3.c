/*
 * loop3.c
 *
 * used as a helper to test AST delivery.  This is an endless loop.
 *
 * $DragonFly: src/test/sysperf/loop3.c,v 1.2 2006/04/22 22:32:52 dillon Exp $
 */

#include "blib.h"

int
main(int ac, char **av)
{
    int i;

    printf("(non timing) one process, endless loop in userland\n");
    for (;;)
	nop();
    return(0);
}

