/* s_finitef.c -- float version of s_finite.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */

/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * $NetBSD: s_finitef.c,v 1.7 2002/05/26 22:01:55 wiz Exp $
 * $DragonFly: src/lib/libm/src/s_finitef.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

/*
 * finitef(x) returns 1 is x is finite, else 0;
 * no branching!
 */

#include <math.h>
#include "math_private.h"

int
finitef(float x)
{
	int32_t ix;
	GET_FLOAT_WORD(ix,x);
	return (int)((u_int32_t)((ix&0x7fffffff)-0x7f800000)>>31);
}
