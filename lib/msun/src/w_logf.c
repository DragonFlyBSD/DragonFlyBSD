/* w_logf.c -- float version of w_log.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/w_logf.c,v 1.5 1999/08/28 00:07:06 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/w_logf.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
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
 * wrapper logf(x)
 */

#include "math.h"
#include "math_private.h"


float
logf(float x)		/* wrapper logf */
{
#ifdef _IEEE_LIBM
	return __ieee754_logf(x);
#else
	float z;
	z = __ieee754_logf(x);
	if(_LIB_VERSION == _IEEE_ || isnanf(x) || x > (float)0.0) return z;
	if(x==(float)0.0)
	    /* logf(0) */
	    return (float)__kernel_standard((double)x,(double)x,116);
	else
	    /* logf(x<0) */
	    return (float)__kernel_standard((double)x,(double)x,117);
#endif
}
