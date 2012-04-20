/*
 * IEEE-compatible infinityl.c for little-endian 80-bit format -- public domain.
 * Note that the representation includes 48 bits of tail padding per x86_64 ABI.
 *
 * $NetBSD: infinityl.c,v 1.2 2005/06/12 05:21:27 lukem Exp $
 */

#include <math.h>

const union __long_double_u __infinityl =
	{ { 0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7f, 0, 0, 0, 0, 0, 0 } };
