/* e_lgammaf.c -- float version of e_lgamma.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/e_lgammaf.c,v 1.5 1999/08/28 00:06:35 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/e_lgammaf.c,v 1.2 2003/06/17 04:26:52 dillon Exp $
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

/* __ieee754_lgammaf(x)
 * Return the logarithm of the Gamma function of x.
 *
 * Method: call __ieee754_lgammaf_r
 */

#include "math.h"
#include "math_private.h"

extern int signgam;

#ifdef __STDC__
	float __ieee754_lgammaf(float x)
#else
	float __ieee754_lgammaf(x)
	float x;
#endif
{
	return __ieee754_lgammaf_r(x,&signgam);
}
