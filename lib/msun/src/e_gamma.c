/* @(#)e_gamma.c 5.1 93/09/24 */
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
 * $FreeBSD: src/lib/msun/src/e_gamma.c,v 1.5 1999/08/28 00:06:31 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/e_gamma.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
 */

/* __ieee754_gamma(x)
 * Return the logarithm of the Gamma function of x.
 *
 * Method: call __ieee754_gamma_r
 */

#include "math.h"
#include "math_private.h"

extern int signgam;

double
__ieee754_gamma(double x)
{
	return __ieee754_gamma_r(x,&signgam);
}
