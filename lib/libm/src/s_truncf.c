/* @(#)s_floor.c 5.1 93/09/24 */
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
 * $NetBSD: s_truncf.c,v 1.3 2007/01/17 23:24:22 hubertf Exp $
 * $DragonFly: src/lib/libm/src/s_truncf.c,v 1.1 2007/06/17 06:26:18 pavalos Exp $
 */

/*
 * truncf(x)
 * Return x rounded toward 0 to integral value
 * Method:
 *	Bit twiddling.
 * Exception:
 *	Inexact flag raised if x not equal to truncf(x).
 */

#include <math.h>
#include "math_private.h"

static const float huge = 1.0e30F;

float
truncf(float x)
{
	int32_t i0,j00;
	uint32_t i;
	GET_FLOAT_WORD(i0,x);
	j00 = ((i0>>23)&0xff)-0x7f;
	if(j00<23) {
	    if(j00<0) { 	/* raise inexact if x != 0 */
		if(huge+x>0.0F)		/* |x|<1, so return 0*sign(x) */
		    i0 &= 0x80000000;
	    } else {
		i = (0x007fffff)>>j00;
		if((i0&i)==0) return x; /* x is integral */
		if(huge+x>0.0F)		/* raise inexact flag */
		    i0 &= (~i);
	    }
	} else {
	    if(j00==0x80) return x+x;	/* inf or NaN */
	    else return x;		/* x is integral */
	}
	SET_FLOAT_WORD(x,i0);
	return x;
}
