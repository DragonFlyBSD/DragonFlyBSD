/*
 * cabsf() wrapper for hypotf().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 *
 * $FreeBSD: src/lib/msun/src/w_cabsf.c,v 1.2.12.1 2001/11/23 16:16:18 dd Exp $
 * $DragonFly: src/lib/msun/src/Attic/w_cabsf.c,v 1.2 2003/06/17 04:26:53 dillon Exp $
 */

#include <complex.h>
#include <math.h>
#include "math_private.h"

float
cabsf(z)
	float complex z;
{

	return hypotf(crealf(z), cimagf(z));
}
