/*
 * syscall3.c
 */

#include "blib.h"

int
main(int ac, char **av)
{
    printf("(non timing) one process, endless loop calling getuid()\n");
    for (;;) 
	getuid();
    /* not reached */
    return(0);
}

