/*
 * cabs() wrapper for hypot().
 *
 * Written by J.T. Conklin, <jtc@wimsey.com>
 * Placed into the Public Domain, 1994.
 *
 * Modified by Steven G. Kargl for the long double type.
 *
 * FreeBSD SVN: 177757 (2008-03-30)
 */

#include <complex.h>
#include <math.h>

long double
cabsl(long double complex z)
{
	return hypotl(creall(z), cimagl(z));
}
