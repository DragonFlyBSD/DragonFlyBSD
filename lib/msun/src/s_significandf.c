/* s_significandf.c -- float version of s_significand.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/s_significandf.c,v 1.5 1999/08/28 00:06:55 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_significandf.c,v 1.2 2003/06/17 04:26:53 dillon Exp $
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

#include "math.h"
#include "math_private.h"

#ifdef __STDC__
	float significandf(float x)
#else
	float significandf(x)
	float x;
#endif
{
	return __ieee754_scalbf(x,(float) -ilogbf(x));
}
