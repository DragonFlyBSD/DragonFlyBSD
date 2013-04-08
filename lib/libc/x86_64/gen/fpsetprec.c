/* $FreeBSD: head/lib/libc/amd64/gen/fpsetprec.c 117864 2003-07-22 06:46:17Z peter $ */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_prec_t fpsetprec(fp_prec_t m)
{
	return (__fpsetprec(m));
}
