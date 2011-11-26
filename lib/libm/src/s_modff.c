/* s_modff.c -- float version of s_modf.c.
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
 * $NetBSD: s_modff.c,v 1.7 2002/05/26 22:01:57 wiz Exp $
 * $DragonFly: src/lib/libm/src/s_modff.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

#include <math.h>
#include "math_private.h"

static const float one = 1.0;

float
modff(float x, float *iptr)
{
	int32_t i0,j0_;
	u_int32_t i;
	GET_FLOAT_WORD(i0,x);
	j0_ = ((i0>>23)&0xff)-0x7f;	/* exponent of x */
	if(j0_<23) {			/* integer part in x */
	    if(j0_<0) {			/* |x|<1 */
	        SET_FLOAT_WORD(*iptr,i0&0x80000000);	/* *iptr = +-0 */
		return x;
	    } else {
		i = (0x007fffff)>>j0_;
		if((i0&i)==0) {			/* x is integral */
		    u_int32_t ix;
		    *iptr = x;
		    GET_FLOAT_WORD(ix,x);
		    SET_FLOAT_WORD(x,ix&0x80000000);	/* return +-0 */
		    return x;
		} else {
		    SET_FLOAT_WORD(*iptr,i0&(~i));
		    return x - *iptr;
		}
	    }
	} else {			/* no fraction part */
	    u_int32_t ix;
	    *iptr = x*one;
	    GET_FLOAT_WORD(ix,x);
	    SET_FLOAT_WORD(x,ix&0x80000000);	/* return +-0 */
	    return x;
	}
}
