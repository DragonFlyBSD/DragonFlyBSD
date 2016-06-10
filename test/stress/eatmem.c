/*
 * EATMEM.C
 *
 * (c)Copyright 2003 Matthew Dillon <dillon@backplane.com>.  This code is
 *    hereby placed in the public domain.
 *
 * This program 'eats' memory by allocating and touching it.  Make sure your
 * datasize resource limit is sufficient for the amount of memory you specify.
 *
 * $DragonFly: src/test/stress/eatmem.c,v 1.1 2003/08/25 19:41:00 dillon Exp $
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int ac, char **av)
{
    size_t bytes;
    size_t i;
    char *ptr;

    if (ac == 1) {
	printf("eatmem MB [msec/page]\n");
	printf("specifying msec/page will cause eatmem to loop forever\n");
	exit(1);
    }
    bytes = strtoul(av[1], NULL, 0) * 1024 * 1024;
    ptr = malloc(bytes);

    for (;;) {
	for (i = 0; i < bytes; i += 4096) {
	    ++ptr[i];
	    if (av[2] && strtol(av[2], NULL, 0))
		usleep(1000 * strtol(av[2], NULL, 0));
	    if (i == 0)
		puts("loop0");
	}
	if (ac != 3)
		break;
	puts("loop");
    }
    puts("ok");
    sleep(10);
    puts("done");
    exit(0);
}
