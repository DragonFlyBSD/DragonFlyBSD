#include <stdio.h>
#include <stdlib.h>

static void __attribute__((__used__))
dummy_pthread_create(void)
{
	fprintf(stderr, "libpthread.so: dummy weak symbol executed\n");
	abort();
}

__weak_reference(dummy_pthread_create, pthread_create);

static void __attribute__((__used__))
dummy_pthread_cancel(void)
{
	fprintf(stderr, "libpthread.so: dummy weak symbol executed\n");
	abort();
}

__weak_reference(dummy_pthread_cancel, pthread_cancel);
