/*
 * $FreeBSD: src/lib/libc/amd64/gen/fpgetround.c,v 1.1 2003/07/22 06:46:17 peter Exp $
 * $DragonFly: src/lib/libcr/amd64/gen/Attic/fpgetround.c,v 1.1 2004/03/13 19:46:55 eirikn Exp $
 */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_rnd_t fpgetround(void)
{
	return __fpgetround();
}
