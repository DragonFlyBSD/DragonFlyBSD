/*
 * cabs() wrapper for hypot().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 *
 * $NetBSD: w_cabs.c,v 1.4 2001/01/06 00:15:00 christos Exp $
 * $DragonFly: src/lib/libm/src/w_cabs.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

#include <complex.h>
#include <math.h>

double
cabs(double complex z)
{
	return hypot(creal(z), cimag(z));
}
