/* s_ldexpf.c -- float version of s_ldexp.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 *
 * $FreeBSD: src/lib/msun/src/s_ldexpf.c,v 1.5 1999/08/28 00:06:51 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_ldexpf.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
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
#include <errno.h>

float
ldexpf(float value, int exp)
{
	if(!finitef(value)||value==(float)0.0) return value;
	value = scalbnf(value,exp);
	if(!finitef(value)||value==(float)0.0) errno = ERANGE;
	return value;
}
