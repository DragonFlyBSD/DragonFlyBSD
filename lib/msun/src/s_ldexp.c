/* @(#)s_ldexp.c 5.1 93/09/24 */
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
 * $FreeBSD: src/lib/msun/src/s_ldexp.c,v 1.5 1999/08/28 00:06:51 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_ldexp.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
 */

#include "math.h"
#include "math_private.h"
#include <errno.h>

double
ldexp(double value, int exp)
{
	if(!finite(value)||value==0.0) return value;
	value = scalbn(value,exp);
	if(!finite(value)||value==0.0) errno = ERANGE;
	return value;
}
