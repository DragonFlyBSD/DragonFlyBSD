/*
 * $FreeBSD: src/lib/libc/amd64/gen/fpgetsticky.c,v 1.1 2003/07/22 06:46:17 peter Exp $
 * $DragonFly: src/lib/libc/amd64/gen/Attic/fpgetsticky.c,v 1.1 2004/02/02 05:43:14 dillon Exp $
 */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_except_t fpgetsticky(void)
{
	return __fpgetsticky();
}
