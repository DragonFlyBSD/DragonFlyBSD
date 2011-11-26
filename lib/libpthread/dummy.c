/* $DragonFly: src/lib/libpthread/dummy.c,v 1.1 2007/04/17 12:34:07 corecode Exp $ */

#include <stdio.h>
#include <stdlib.h>

static void __attribute__((__used__))
dummy_pthread_create(void)
{
	fprintf(stderr, "libpthread.so: dummy weak symbol executed\n");
	abort();
}

__weak_reference(dummy_pthread_create, pthread_create);
