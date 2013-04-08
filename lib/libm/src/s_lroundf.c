/* $FreeBSD: head/lib/msun/src/s_lroundf.c 144771 2005-04-08 00:52:27Z das $ */

#define type		float
#define	roundit		roundf
#define dtype		long
#define	DTYPE_MIN	LONG_MIN
#define	DTYPE_MAX	LONG_MAX
#define	fn		lroundf

#include "s_lround.c"
