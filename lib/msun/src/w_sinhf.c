/* w_sinhf.c -- float version of w_sinh.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/w_sinhf.c,v 1.5 1999/08/28 00:07:09 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/w_sinhf.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
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
 * wrapper sinhf(x)
 */

#include "math.h"
#include "math_private.h"

float
sinhf(float x)		/* wrapper sinhf */
{
#ifdef _IEEE_LIBM
	return __ieee754_sinhf(x);
#else
	float z;
	z = __ieee754_sinhf(x);
	if(_LIB_VERSION == _IEEE_) return z;
	if(!finitef(z)&&finitef(x)) {
	    /* sinhf overflow */
	    return (float)__kernel_standard((double)x,(double)x,125);
	} else
	    return z;
#endif
}
