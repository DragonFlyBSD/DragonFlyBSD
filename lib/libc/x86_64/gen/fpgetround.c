/* $FreeBSD: head/lib/libc/amd64/gen/fpgetround.c 117864 2003-07-22 06:46:17Z peter $ */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_rnd_t fpgetround(void)
{
	return __fpgetround();
}
