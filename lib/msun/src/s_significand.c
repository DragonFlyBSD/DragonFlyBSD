/* @(#)s_signif.c 5.1 93/09/24 */
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
 * $FreeBSD: src/lib/msun/src/s_significand.c,v 1.6 1999/08/28 00:06:55 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_significand.c,v 1.2 2003/06/17 04:26:53 dillon Exp $
 */

/*
 * significand(x) computes just
 * 	scalb(x, (double) -ilogb(x)),
 * for exercising the fraction-part(F) IEEE 754-1985 test vector.
 */

#include "math.h"
#include "math_private.h"

#ifdef __STDC__
	double __generic_significand(double x)
#else
	double __generic_significand(x)
	double x;
#endif
{
	return __ieee754_scalb(x,(double) -ilogb(x));
}
