/* @(#)s_matherr.c 5.1 93/09/24 */
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
 * $FreeBSD: src/lib/msun/src/s_matherr.c,v 1.5 1999/08/28 00:06:53 peter Exp $
 * $DragonFly: src/lib/msun/src/Attic/s_matherr.c,v 1.3 2004/12/29 15:22:57 asmodai Exp $
 */

#include "math.h"
#include "math_private.h"

int
matherr(struct exception *x)
{
	int n=0;
	if(x->arg1!=x->arg1) return 0;
	return n;
}
