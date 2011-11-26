/*
 * syscall3.c
 *
 * $DragonFly: src/test/sysperf/syscall3.c,v 1.2 2004/10/31 20:19:24 eirikn Exp $
 */

#include "blib.h"

extern int getuid_msg(void);

int
main(int ac, char **av)
{
    printf("(non timing) one process, endless loop calling getuid_msg()\n");
    for (;;) 
	getuid_msg();
    /* not reached */
    return(0);
}

