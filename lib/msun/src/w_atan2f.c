/* w_atan2f.c -- float version of w_atan2.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/w_atan2f.c,v 1.5 1999/08/28 00:06:58 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/w_atan2f.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
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
 * wrapper atan2f(y,x)
 */

#include "math.h"
#include "math_private.h"


float
atan2f(float y, float x)		/* wrapper atan2f */
{
#ifdef _IEEE_LIBM
	return __ieee754_atan2f(y,x);
#else
	float z;
	z = __ieee754_atan2f(y,x);
	if(_LIB_VERSION == _IEEE_||isnanf(x)||isnanf(y)) return z;
	if(x==(float)0.0&&y==(float)0.0) {
		/* atan2f(+-0,+-0) */
	        return (float)__kernel_standard((double)y,(double)x,103);
	} else
	    return z;
#endif
}
