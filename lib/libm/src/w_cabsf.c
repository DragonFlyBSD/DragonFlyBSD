/*
 * cabsf() wrapper for hypotf().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 *
 * $NetBSD: w_cabsf.c,v 1.4 2001/01/06 00:15:00 christos Exp $
 * $DragonFly: src/lib/libm/src/w_cabsf.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

#include <complex.h>
#include <math.h>

float
cabsf(float complex z)
{
	return hypot(crealf(z), cimagf(z));
}
