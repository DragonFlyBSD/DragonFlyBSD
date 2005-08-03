/*
 * cmp.c
 *
 * Test for comparing pointers vs bit-testing.
 *
 * $DragonFly: src/test/sysperf/cmp.c,v 1.1 2005/08/03 13:37:27 hmp Exp $
 */

#include "blib.h"

#define LOOP 1000000000

static void nop(void) { }

struct f {
	void (*nop_ptr)(void);
	int mask;
} foo;
#define	SOME_MASK 	0x00800

struct f *fp = &foo;

int
main(int ac, char **av)
{
    int i;
	struct f *fp = &foo;
	foo.nop_ptr = nop;
	foo.mask = SOME_MASK;

    printf("compare nop() function pointer against NULL (struct not pointer)\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
		if (foo.nop_ptr == NULL)
			;
    stop_timing(LOOP, "loop1/cmp-pointer");

	printf("compare nop() function pointer against NULL (struct pointer)\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
		if (fp->nop_ptr == NULL)
			;
    stop_timing(LOOP, "loop2/cmp-pointer");

	printf("compare bitmask checking\n");
    start_timing();
    for (i = 0; i < LOOP; ++i)
		if (foo.mask & SOME_MASK)
			;
    stop_timing(LOOP, "loop3/cmp-bitmask");


    return(0);
}

