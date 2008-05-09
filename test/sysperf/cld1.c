/*
 * cld1.c
 *
 * $DragonFly: src/test/sysperf/cld1.c,v 1.1 2008/05/09 15:49:42 dillon Exp $
 */

#include "blib.h"

static __attribute__ ((noinline)) void dummy_with_cld(void);
static __attribute__ ((noinline)) void dummy_without_cld(void);

int
main(int ac, char **av)
{
    long long count = 0;
    long long max;
    char c;
    int j;

    printf("timing the cld instruction\n");

    start_timing();
    while (stop_timing(0, NULL) == 0) {
	for (j = 0; j < 100; ++j)
	    getuid();
	count += 100;
    }
    max = count;
    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j)
	    dummy_without_cld();
    }
    stop_timing(count, "dummy() - without cld");

    start_timing();
    for (count = 0; count < max; count += 100) {
	for (j = 0; j < 100; ++j)
	    dummy_with_cld();
    }
    stop_timing(count, "dummy() - with cld");
    return(0);
}

static
__attribute__ ((noinline))
void
dummy_with_cld(void)
{
	volatile int i, j, k;

	i = 0;
	j = 0;
	k = 0;
	__asm __volatile("cld"::);
}

static
__attribute__ ((noinline))
void
dummy_without_cld(void)
{
	volatile int i, j, k;

	i = 0;
	j = 0;
	k = 0;
}
