/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * $NetBSD: math.h,v 1.46 2007/02/22 22:08:19 drochner Exp $
 */

/*
 * @(#)fdlibm.h 5.1 93/09/24
 */

#ifndef _MATH_H_
#define _MATH_H_

#include <sys/cdefs.h>
#include <machine/limits.h>

#if __GNUC_PREREQ__(3, 3) || (defined(__INTEL_COMPILER) && __INTEL_COMPILER >= 800)
#define	__MATH_BUILTIN_CONSTANTS
#endif

#if __GNUC_PREREQ__(3, 0) && !defined(__INTEL_COMPILER)
#define	__MATH_BUILTIN_RELOPS
#endif

union __float_u {
	unsigned char __dummy[sizeof(float)];
	float __val;
};

union __double_u {
	unsigned char __dummy[sizeof(double)];
	double __val;
};

union __long_double_u {
	unsigned char __dummy[sizeof(long double)];
	long double __val;
};

#include <machine/math.h>

#ifdef __HAVE_LONG_DOUBLE
#define	__fpmacro_unary_floating(__name, __arg0)			\
	/* LINTED */							\
	((sizeof (__arg0) == sizeof (float))				\
	?	__ ## __name ## f (__arg0)				\
	: (sizeof (__arg0) == sizeof (double))				\
	?	__ ## __name ## d (__arg0)				\
	:	__ ## __name ## l (__arg0))
#else
#define	__fpmacro_unary_floating(__name, __arg0)			\
	/* LINTED */							\
	((sizeof (__arg0) == sizeof (float))				\
	?	__ ## __name ## f (__arg0)				\
	:	__ ## __name ## d (__arg0))
#endif /* __HAVE_LONG_DOUBLE */

/*
 * ANSI/POSIX
 */
/* 7.12#3 HUGE_VAL, HUGELF, HUGE_VALL */
extern const union __double_u __infinity;
#ifdef __MATH_BUILTIN_CONSTANTS
#define	HUGE_VAL	__builtin_huge_val()
#else
#define	HUGE_VAL	__infinity.__val
#endif

/*
 * ISO C99
 */
#if __ISO_C_VISIBLE >= 1999
/* 7.12#3 HUGE_VAL, HUGELF, HUGE_VALL */
extern const union __float_u __infinityf;
#ifdef __MATH_BUILTIN_CONSTANTS
#define	HUGE_VALF	__builtin_huge_valf()
#else
#define	HUGE_VALF	__infinityf.__val
#endif

extern const union __long_double_u __infinityl;
#ifdef __MATH_BUILTIN_CONSTANTS
#define	HUGE_VALL	__builtin_huge_vall()
#else
#define	HUGE_VALL	__infinityl.__val
#endif

/* 7.12#4 INFINITY */
#ifdef __MATH_BUILTIN_CONSTANTS
#define	INFINITY	__builtin_inf()
#elif defined(__INFINITY)
#define	INFINITY	__INFINITY	/* float constant which overflows */
#else
#define	INFINITY	HUGE_VALF	/* positive infinity */
#endif /* __INFINITY */

/* 7.12#5 NAN: a quiet NaN, if supported */
#ifdef __MATH_BUILTIN_CONSTANTS
#define	NAN		__builtin_nan("")
#elif defined(__HAVE_NANF)
extern const union __float_u __nanf;
#define	NAN		__nanf.__val
#endif /* __HAVE_NANF */

/* 7.12#6 number classification macros */
#define	FP_INFINITE	0x00
#define	FP_NAN		0x01
#define	FP_NORMAL	0x02
#define	FP_SUBNORMAL	0x03
#define	FP_ZERO		0x04
/* NetBSD extensions */
#define	_FP_LOMD	0x80		/* range for machine-specific classes */
#define	_FP_HIMD	0xff

/* 7.12#8 values returned by ilogb(0) or ilogb(NAN), respectively */
#define	FP_ILOGB0	INT_MIN
#define	FP_ILOGBNAN	INT_MIN

#endif /* ISO C99 */

/*
 * XOPEN/SVID
 */
#if __XSI_VISIBLE > 0
#define	M_E		2.7182818284590452354	/* e */
#define	M_LOG2E		1.4426950408889634074	/* log 2e */
#define	M_LOG10E	0.43429448190325182765	/* log 10e */
#define	M_LN2		0.69314718055994530942	/* log e2 */
#define	M_LN10		2.30258509299404568402	/* log e10 */
#define	M_PI		3.14159265358979323846	/* pi */
#define	M_PI_2		1.57079632679489661923	/* pi/2 */
#define	M_PI_4		0.78539816339744830962	/* pi/4 */
#define	M_1_PI		0.31830988618379067154	/* 1/pi */
#define	M_2_PI		0.63661977236758134308	/* 2/pi */
#define	M_2_SQRTPI	1.12837916709551257390	/* 2/sqrt(pi) */
#define	M_SQRT2		1.41421356237309504880	/* sqrt(2) */
#define	M_SQRT1_2	0.70710678118654752440	/* 1/sqrt(2) */

#define	MAXFLOAT	((float)3.40282346638528860e+38)
extern int signgam;
#endif /* _XSI_VISIBLE */

#if __DF_VISIBLE
#define	HUGE		MAXFLOAT

/*
 * set X_TLOSS = pi*2**52, which is possibly defined in <values.h>
 * (one may replace the following line by "#include <values.h>")
 */

#define X_TLOSS		1.41484755040568800000e+16

#define	DOMAIN		1
#define	SING		2
#define	OVERFLOW	3
#define	UNDERFLOW	4
#define	TLOSS		5
#define	PLOSS		6

#endif /* __DF_VISIBLE */

__BEGIN_DECLS
/*
 * ANSI/POSIX
 */
double	acos(double);
double	asin(double);
double	atan(double);
double	atan2(double, double);
double	cos(double);
double	sin(double);
double	tan(double);

double	cosh(double);
double	sinh(double);
double	tanh(double);

double	exp(double);
double  exp2(double);
double	frexp(double, int *);
double	ldexp(double, int);
double	log(double);
double	log2(double);
double	log10(double);
double	modf(double, double *);

double	pow(double, double);
double	sqrt(double);

double	ceil(double);
double	fabs(double);
double	floor(double);
double	fmod(double, double);

#if __XSI_VISIBLE > 0
double	erf(double);
double	erfc(double);
double	gamma(double);
double	hypot(double, double);
int	finite(double);
double	j0(double);
double	j1(double);
double	jn(int, double);
double	lgamma(double);
double	tgamma(double);
double	y0(double);
double	y1(double);
double	yn(int, double);
#endif /* __XSI_VISIBLE */

#if __XSI_VISIBLE >= 500
double	acosh(double);
double	asinh(double);
double	atanh(double);
double	cbrt(double);
double	expm1(double);
int	ilogb(double);
double	log1p(double);
double	logb(double);
double	nextafter(double, double);
double	remainder(double, double);
double	rint(double);
double	scalb(double, double);
#endif /* __XSI_VISIBLE >= 500 */

/*
 * ISO C99
 */
#if __ISO_C_VISIBLE >= 1999
/* 7.12.3.1 int fpclassify(real-floating x) */
#define	fpclassify(__x)	__fpmacro_unary_floating(fpclassify, __x)

/* 7.12.3.2 int isfinite(real-floating x) */
#define	isfinite(__x)	__fpmacro_unary_floating(isfinite, __x)

/* 7.12.3.5 int isnormal(real-floating x) */
#define	isnormal(__x)	(fpclassify(__x) == FP_NORMAL)

/* 7.12.3.6 int signbit(real-floating x) */
#define	signbit(__x)	__fpmacro_unary_floating(signbit, __x)

/* 7.12.4 trigonometric */

float	acosf(float);
float	asinf(float);
float	atanf(float);
float	atan2f(float, float);
float	cosf(float);
float	sinf(float);
float	tanf(float);

/* 7.12.5 hyperbolic */

float	acoshf(float);
float	asinhf(float);
float	atanhf(float);
float	coshf(float);
float	sinhf(float);
float	tanhf(float);

/* 7.12.6 exp / log */

float	expf(float);
float   exp2f(float);
float	expm1f(float);
float	frexpf(float, int *);
int	ilogbf(float);
float	ldexpf(float, int);
float	logf(float);
float	log2f(float);
float	log10f(float);
float	log1pf(float);
float	logbf(float);
float	modff(float, float *);
float	scalblnf(float, long);
float	scalbnf(float, int);

/* 7.12.7 power / absolute */

float	cbrtf(float);
float	fabsf(float);
float	hypotf(float, float);
float	powf(float, float);
float	sqrtf(float);

/* 7.12.8 error / gamma */

float	erff(float);
float	erfcf(float);
float	lgammaf(float);
float	tgammaf(float);

/* 7.12.9 nearest integer */

float	ceilf(float);
float	floorf(float);
float	nearbyintf(float);
double	nearbyint(double);
float	rintf(float);
double	round(double);
float	roundf(float);
double	trunc(double);
float	truncf(float);
long int	lrint(double);
long int	lrintf(float);
/* LONGLONG */
long long int	llrint(double);
/* LONGLONG */
long long int	llrintf(float);
long int	lround(double);
long int	lroundf(float);
/* LONGLONG */
long long int	llround(double);
/* LONGLONG */
long long int	llroundf(float);

/* 7.12.10 remainder */

float	fmodf(float, float);
float	remainderf(float, float);

/* 7.12.10.3 The remquo functions */
double remquo(double, double, int *);
float  remquof(float, float, int *);

/* 7.12.11 manipulation */

float	copysignf(float, float);
double	nan(const char *);
float	nanf(const char *);
float	nextafterf(float, float);

/* 7.12.12 maximum, minimum, positive difference */
double	fdim(double, double);
float	fdimf(float, float);

double	fmax(double, double);
float	fmaxf(float, float);

double  fmin(double, double);
float   fminf(float, float);


/* isoC99 */
double fma  (double, double, double);
float  fmaf (float,  float,  float);




#endif /* __ISO_C_VISIBLE >= 1999 */

#if __ISO_C_VISIBLE >= 1999
/* 7.12.3.3 int isinf(real-floating x) */
#ifdef __isinf
#define	isinf(__x)	__isinf(__x)
#else
#define	isinf(__x)	__fpmacro_unary_floating(isinf, __x)
#endif

/* 7.12.3.4 int isnan(real-floating x) */
#ifdef __isnan
#define	isnan(__x)	__isnan(__x)
#else
#define	isnan(__x)	__fpmacro_unary_floating(isnan, __x)
#endif

/* 7.12.14 Comparision macros */
#ifdef __MATH_BUILTIN_RELOPS
#define	isgreater(x, y)		__builtin_isgreater((x), (y))
#define	isgreaterequal(x, y)	__builtin_isgreaterequal((x), (y))
#define	isless(x, y)		__builtin_isless((x), (y))
#define	islessequal(x, y)	__builtin_islessequal((x), (y))
#define	islessgreater(x, y)	__builtin_islessgreater((x), (y))
#define	isunordered(x, y)	__builtin_isunordered((x), (y))
#else
#define	isgreater(x, y)		(!isunordered((x), (y)) && (x) > (y))
#define	isgreaterequal(x, y)	(!isunordered((x), (y)) && (x) >= (y))
#define	isless(x, y)		(!isunordered((x), (y)) && (x) < (y))
#define	islessequal(x, y)	(!isunordered((x), (y)) && (x) <= (y))
#define	islessgreater(x, y)	(!isunordered((x), (y)) && \
					((x) > (y) || (y) > (x)))
#define	isunordered(x, y)	(isnan(x) || isnan(y))
#endif /* __MATH_BUILTIN_RELOPS */

#endif /* __ISO_C_VISIBLE >= 1999 */

#if __DF_VISIBLE
/*
 * IEEE Test Vector
 */
double	significand(double);

/*
 * Functions callable from C, intended to support IEEE arithmetic.
 */
double	copysign(double, double);
double	scalbln(double, long);
double	scalbn(double, int);

/*
 * BSD math library entry points
 */
double	drem(double, double);

/*
 * Reentrant version of gamma & lgamma; passes signgam back by reference
 * as the second argument; user must allocate space for signgam.
 */
double	gamma_r(double, int *);
double	lgamma_r(double, int *);
#endif /* __DF_VISIBLE */


#if __DF_VISIBLE

/* float versions of ANSI/POSIX functions */

float	gammaf(float);
int	finitef(float);
float	j0f(float);
float	j1f(float);
float	jnf(int, float);
float	y0f(float);
float	y1f(float);
float	ynf(int, float);

float	scalbf(float, float);

/*
 * float version of IEEE Test Vector
 */
float	significandf(float);

/*
 * float versions of BSD math library entry points
 */
float	dremf(float, float);

/*
 * Float versions of reentrant version of gamma & lgamma; passes
 * signgam back by reference as the second argument; user must
 * allocate space for signgam.
 */
float	gammaf_r(float, int *);
float	lgammaf_r(float, int *);
#endif /* __DF_VISIBLE */

/*
 * Library implementation
 */
int	__fpclassifyf(float);
int	__fpclassifyd(double);
int	__isfinitef(float);
int	__isfinited(double);
int	__isinff(float);
int	__isinfd(double);
int	__isnanf(float);
int	__isnand(double);
int	__signbitf(float);
int	__signbitd(double);

#ifdef __HAVE_LONG_DOUBLE
int	__fpclassifyl       (long double);
int	__isfinitel         (long double);
int	__isinfl            (long double);
int	__isnanl            (long double);
int	__signbitl          (long double);

long double	acosl       (long double);
long double	asinl       (long double);
long double	atan2l      (long double, long double);
long double	atanl       (long double);
long double	ceill       (long double);
long double	cosl        (long double);
long double	cbrtl       (long double);
long double	copysignl   (long double, long double);
long double	fdiml       (long double, long double);
long double	exp2l       (long double);
long double	fabsl       (long double);
long double	floorl      (long double);
long double	fmal        (long double, long double, long double);
long double	frexpl      (long double, int *);
long double	fmaxl       (long double, long double);
long double	fminl       (long double, long double);
long double	fmodl       (long double, long double);
long double	hypotl      (long double, long double);
int        	ilogbl      (long double);
long double	ldexpl      (long double, int);
long long	llrintl     (long double);
long long      	llroundl    (long double);
long double	logbl       (long double);
long       	lrintl      (long double);
long		lroundl     (long double);
long double	modfl       (long double, long double *);
long double	nanl        (const char *);
long double	nearbyintl  (long double);
long double	nextafterl  (long double, long double);
double     	nexttoward  (double,      long double);
float      	nexttowardf (float,       long double);
long double	remainderl  (long double, long double);
long double	remquol     (long double, long double, int *);
long double	rintl       (long double);
long double	roundl      (long double);
long double	scalblnl    (long double, long);
long double	scalbnl     (long double, int);
long double	sinl        (long double);
long double	sqrtl       (long double);
long double	tanl        (long double);
long double	truncl      (long double);
#endif
__END_DECLS

#endif /* _MATH_H_ */
