/*
 * Written by J.T. Conklin, Apr 10, 1995
 * Public domain.
 *
 * $FreeBSD: head/lib/libc/amd64/gen/flt_rounds.c 132383 2004-07-19 08:17:25Z das $
 */

#include <float.h>

static const int map[] = {
	1,	/* round to nearest */
	3,	/* round to zero */
	2,	/* round to negative infinity */
	0	/* round to positive infinity */
};

int
__flt_rounds(void)
{
	int x;

	/* Assume that the x87 and the SSE unit agree on the rounding mode. */
	__asm("fnstcw %0" : "=m" (x));
	return (map[(x >> 10) & 0x03]);
}
