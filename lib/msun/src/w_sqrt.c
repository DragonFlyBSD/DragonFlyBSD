/* @(#)w_sqrt.c 5.1 93/09/24 */
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
 * $FreeBSD: src/lib/msun/src/w_sqrt.c,v 1.5 1999/08/28 00:07:09 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/w_sqrt.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
 */

/*
 * wrapper sqrt(x)
 */

#include "math.h"
#include "math_private.h"

double
sqrt(double x)		/* wrapper sqrt */
{
#ifdef _IEEE_LIBM
	return __ieee754_sqrt(x);
#else
	double z;
	z = __ieee754_sqrt(x);
	if(_LIB_VERSION == _IEEE_ || isnan(x)) return z;
	if(x<0.0) {
	    return __kernel_standard(x,x,26); /* sqrt(negative) */
	} else
	    return z;
#endif
}
