/*
 * $DragonFly: src/lib/msun/src/Attic/w_dremf.c,v 1.2 2004/06/19 17:19:50 joerg Exp $
 *
 * dremf() wrapper for remainderf().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 */

#include "math.h"
#include "math_private.h"

float
dremf(float x, float y)
{
	return remainderf(x, y);
}
