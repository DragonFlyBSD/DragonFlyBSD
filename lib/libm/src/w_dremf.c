/*
 * dremf() wrapper for remainderf().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 *
 * $NetBSD: w_dremf.c,v 1.4 2004/06/25 15:57:38 drochner Exp $
 * $DragonFly: src/lib/libm/src/w_dremf.c,v 1.1 2005/07/26 21:15:20 joerg Exp $
 */

#include <math.h>
#include "math_private.h"

float
dremf(float x, float y)
{
	return remainderf(x, y);
}
