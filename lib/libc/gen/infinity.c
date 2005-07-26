/*	$NetBSD: infinity_ieee754.c,v 1.3 2005/06/12 05:21:27 lukem Exp $	*/
/*	$DragonFly: src/lib/libc/gen/infinity.c,v 1.1 2005/07/26 21:15:19 joerg Exp $ */

/*
 * IEEE-compatible infinity.c -- public domain.
 */

#include <sys/endian.h>
#include <math.h>

const union __double_u __infinity =
#if BYTE_ORDER == BIG_ENDIAN
	{ { 0x7f, 0xf0, 0, 0, 0, 0,    0,    0 } };
#else
	{ {    0,    0, 0, 0, 0, 0, 0xf0, 0x7f } };
#endif
