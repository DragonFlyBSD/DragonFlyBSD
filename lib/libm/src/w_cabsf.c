/*
 * cabsf() wrapper for hypotf().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 *
 * $FreeBSD: head/lib/msun/src/w_cabsf.c 78172 2001-06-13 15:16:30Z ru $
 */

#include <complex.h>
#include <math.h>
#include "math_private.h"

float
cabsf(float complex z)
{

	return hypotf(crealf(z), cimagf(z));
}
