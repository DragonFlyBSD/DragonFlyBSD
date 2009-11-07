/*
 * $DragonFly: src/lib/libc/amd64/gen/nanf.c,v 1.1 2006/07/27 00:46:57 corecode Exp $
 */

#include <sys/cdefs.h>

#include <math.h>
#include <machine/endian.h>

/* bytes for quiet NaN (IEEE single precision) */
const union __float_u __nanf =
		{ {    0,    0, 0xc0, 0x7f } };
