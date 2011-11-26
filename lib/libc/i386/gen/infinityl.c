/*	$NetBSD: infinityl.c,v 1.2 2005/06/12 05:21:26 lukem Exp $	*/
/*	$DragonFly: src/lib/libc/i386/gen/infinityl.c,v 1.1 2005/07/26 21:15:19 joerg Exp $ */

/*
 * IEEE-compatible infinityl.c for little-endian 80-bit format -- public domain.
 * Note that the representation includes 16 bits of tail padding per i386 ABI.
 */

#include <math.h>

const union __long_double_u __infinityl =
	{ { 0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7f, 0, 0 } };
