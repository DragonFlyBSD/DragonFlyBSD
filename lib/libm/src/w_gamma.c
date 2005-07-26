/* @(#)w_gamma.c 5.1 93/09/24 */
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
 * $NetBSD: w_gamma.c,v 1.11 2002/05/26 22:02:00 wiz Exp $
 * $DragonFly: src/lib/libm/src/w_gamma.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

/* double gamma(double x)
 * Return the logarithm of the Gamma function of x.
 *
 * Method: call gamma_r
 */

#include <math.h>
#include "math_private.h"

double
gamma(double x)
{
	return lgamma_r(x,&signgam);
}
