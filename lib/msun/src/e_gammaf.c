/* e_gammaf.c -- float version of e_gamma.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/e_gammaf.c,v 1.5 1999/08/28 00:06:31 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/e_gammaf.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
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

/* __ieee754_gammaf(x)
 * Return the logarithm of the Gamma function of x.
 *
 * Method: call __ieee754_gammaf_r
 */

#include "math.h"
#include "math_private.h"

extern int signgam;

float
__ieee754_gammaf(float x)
{
	return __ieee754_gammaf_r(x,&signgam);
}
