/*
 * syscall3.c
 *
 * $DragonFly: src/test/sysperf/syscall3.c,v 1.1 2003/08/12 02:29:44 dillon Exp $
 */

#include "blib.h"

int
main(int ac, char **av)
{
    printf("(non timing) one process, endless loop calling getuid_msg()\n");
    for (;;) 
	getuid();
    /* not reached */
    return(0);
}

