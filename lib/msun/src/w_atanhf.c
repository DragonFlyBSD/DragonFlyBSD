/* w_atanhf.c -- float version of w_atanh.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/w_atanhf.c,v 1.5 1999/08/28 00:06:59 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/w_atanhf.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
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
 * wrapper atanhf(x)
 */

#include "math.h"
#include "math_private.h"


float
atanhf(float x)		/* wrapper atanhf */
{
#ifdef _IEEE_LIBM
	return __ieee754_atanhf(x);
#else
	float z,y;
	z = __ieee754_atanhf(x);
	if(_LIB_VERSION == _IEEE_ || isnanf(x)) return z;
	y = fabsf(x);
	if(y>=(float)1.0) {
	    if(y>(float)1.0)
	        /* atanhf(|x|>1) */
	        return (float)__kernel_standard((double)x,(double)x,130);
	    else
	        /* atanhf(|x|==1) */
	        return (float)__kernel_standard((double)x,(double)x,131);
	} else
	    return z;
#endif
}
