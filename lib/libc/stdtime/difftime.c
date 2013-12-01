/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
**
** $FreeBSD: head/contrib/tzcode/stdtime/difftime.c 192625 2009-05-23 06:31:50Z edwin $
*/
/*LINTLIBRARY*/

#include "namespace.h"
#include "private.h"	/* for time_t and TYPE_SIGNED */
#include "un-namespace.h"

double
difftime(time_t time1, time_t time0)
{
	/*
	** If (sizeof (double) > sizeof (time_t)) simply convert and subtract
	** (assuming that the larger type has more precision).
	*/
	if (sizeof (double) > sizeof (time_t))
		return (double) time1 - (double) time0;
	if (!TYPE_SIGNED(time_t)) {
		/*
		** The difference of two unsigned values can't overflow
		** if the minuend is greater than or equal to the subtrahend.
		*/
		if (time1 >= time0)
			return            time1 - time0;
		else	return -(double) (time0 - time1);
	}
	/*
	** Handle cases where both time1 and time0 have the same sign
	** (meaning that their difference cannot overflow).
	*/
	if ((time1 < 0) == (time0 < 0))
		return time1 - time0;
	/*
	** time1 and time0 have opposite signs.
	** Punt if uintmax_t is too narrow.
	** This suffers from double rounding; attempt to lessen that
	** by using long double temporaries.
	*/
	if (sizeof (uintmax_t) < sizeof (time_t))
		return (long double) time1 - (long double) time0;
	/*
	** Stay calm...decent optimizers will eliminate the complexity below.
	*/
	if (time1 >= 0 /* && time0 < 0 */)
		return    (uintmax_t) time1 + (uintmax_t) (-1 - time0) + 1;
	return -(double) ((uintmax_t) time0 + (uintmax_t) (-1 - time1) + 1);
}
