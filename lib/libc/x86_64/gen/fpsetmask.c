/* $FreeBSD: head/lib/libc/amd64/gen/fpsetmask.c 117864 2003-07-22 06:46:17Z peter $ */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_except_t fpsetmask(fp_except_t m)
{
	return (__fpsetmask(m));
}
