/*
 * dremf() wrapper for remainderf().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 */
/* $FreeBSD: head/lib/msun/src/w_dremf.c 132760 2004-07-28 05:53:18Z kan $ */

#include "math.h"
#include "math_private.h"

float
dremf(float x, float y)
{
	return remainderf(x, y);
}
