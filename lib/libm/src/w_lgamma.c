/* @(#)w_lgamma.c 5.1 93/09/24 */
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
 * $NetBSD: w_lgamma.c,v 1.10 2002/05/26 22:02:02 wiz Exp $
 * $DragonFly: src/lib/libm/src/w_lgamma.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

/*
 * double lgamma(double x)
 * Return the logarithm of the Gamma function of x.
 */

#include <math.h>
#include "math_private.h"

double
lgamma(double x)
{
	return lgamma_r(x,&signgam);
}
