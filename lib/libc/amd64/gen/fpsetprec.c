/*
 * $FreeBSD: src/lib/libc/amd64/gen/fpsetprec.c,v 1.1 2003/07/22 06:46:17 peter Exp $
 * $DragonFly: src/lib/libc/amd64/gen/Attic/fpsetprec.c,v 1.2 2005/11/12 21:56:11 swildner Exp $
 */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_prec_t
fpsetprec(fp_prec_t m)
{
	return (__fpsetprec(m));
}
