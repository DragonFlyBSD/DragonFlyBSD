/*	$NetBSD: infinityf_ieee754.c,v 1.2 2005/06/12 05:21:27 lukem Exp $	*/
/*	$DragonFly: src/lib/libc/gen/infinityf.c,v 1.1 2005/07/26 21:15:19 joerg Exp $ */

/*
 * IEEE-compatible infinityf.c -- public domain.
 */

#include <sys/endian.h>
#include <math.h>

const union __float_u __infinityf =
#if BYTE_ORDER == BIG_ENDIAN
	{ { 0x7f, 0x80,     0,    0 } };
#else
	{ {    0,    0,  0x80, 0x7f } };
#endif
