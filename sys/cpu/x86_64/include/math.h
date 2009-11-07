/* 
 * $NetBSD: math.h,v 1.2 2003/10/28 00:55:28 kleink Exp $
 */

#ifndef _CPU_MATH_H_
#define _CPU_MATH_H_

#define	__HAVE_LONG_DOUBLE
#define	__HAVE_NANF

#if __ISO_C_VISIBLE >= 1999
/* 7.12#2 float_t, double_t */
typedef float	float_t;
typedef double	double_t;
#endif

#endif
