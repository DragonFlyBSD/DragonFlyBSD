/* @(#)s_fabs.c 5.1 93/09/24 */
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
 * $FreeBSD: src/lib/msun/src/s_fabs.c,v 1.5 1999/08/28 00:06:47 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_fabs.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
 */

/*
 * fabs(x) returns the absolute value of x.
 */

#include "math.h"
#include "math_private.h"

double
fabs(double x)
{
	u_int32_t high;
	GET_HIGH_WORD(high,x);
	SET_HIGH_WORD(x,high&0x7fffffff);
        return x;
}
