/* s_significandf.c -- float version of s_significand.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
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
 *
 * $NetBSD: s_significandf.c,v 1.6 2002/05/26 22:01:58 wiz Exp $
 * $DragonFly: src/lib/libm/src/s_significandf.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

#include <math.h>
#include "math_private.h"

float
significandf(float x)
{
	return scalbf(x,(float) -ilogbf(x));
}
