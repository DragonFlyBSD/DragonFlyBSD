/* s_fabsf.c -- float version of s_fabs.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/s_fabsf.c,v 1.5 1999/08/28 00:06:47 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_fabsf.c,v 1.2 2003/06/17 04:26:53 dillon Exp $
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
 */

/*
 * fabsf(x) returns the absolute value of x.
 */

#include "math.h"
#include "math_private.h"

#ifdef __STDC__
	float fabsf(float x)
#else
	float fabsf(x)
	float x;
#endif
{
	u_int32_t ix;
	GET_FLOAT_WORD(ix,x);
	SET_FLOAT_WORD(x,ix&0x7fffffff);
        return x;
}
