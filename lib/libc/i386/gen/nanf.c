/*	$NetBSD: nanf.c,v 1.3 2005/04/15 22:39:11 kleink Exp $	*/
/*	$DragonFly: src/lib/libc/i386/gen/nanf.c,v 1.1 2005/07/26 21:15:19 joerg Exp $ */

#include <math.h>
#include <machine/endian.h>

/* bytes for quiet NaN (IEEE single precision) */
const union __float_u __nanf =
		{ {    0,    0, 0xc0, 0x7f } };
