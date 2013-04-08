/* $FreeBSD: head/lib/msun/src/s_lroundl.c 144772 2005-04-08 01:24:08Z das $ */

#define type		long double
#define	roundit		roundl
#define dtype		long
#define	DTYPE_MIN	LONG_MIN
#define	DTYPE_MAX	LONG_MAX
#define	fn		lroundl

#include "s_lround.c"
