/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)limits.h	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/i386/include/limits.h,v 1.14.2.2 2000/11/05 09:21:42 obrien Exp $
 */

#ifndef _CPU_LIMITS_H_
#define	_CPU_LIMITS_H_

#include <sys/cdefs.h>

#define	CHAR_BIT	8		/* number of bits in a char */
#define	MB_LEN_MAX	6		/* Allow 31 bit UTF2 */

/*
 * According to ANSI (section 2.2.4.2), the values below must be usable by
 * #if preprocessing directives.  Additionally, the expression must have the
 * same type as would an expression that is an object of the corresponding
 * type converted according to the integral promotions.  The subtraction for
 * INT_MIN, etc., is so the value is not unsigned; e.g., 0x80000000 is an
 * unsigned int for 32-bit two's complement ANSI compilers (section 3.1.3.2).
 * These numbers are for the default configuration of gcc.  They work for
 * some other compilers as well, but this should not be depended on.
 */
#define	SCHAR_MAX	0x7f		/* max value for a signed char */
#define	SCHAR_MIN	(-0x7f - 1)	/* min value for a signed char */

#define	UCHAR_MAX	0xff		/* max value for an unsigned char */

#ifdef __CHAR_UNSIGNED__
#define	CHAR_MAX	UCHAR_MAX	/* max value for a char */
#define	CHAR_MIN	0		/* min value for a char */
#else
#define	CHAR_MAX	SCHAR_MAX	/* max value for a char */
#define	CHAR_MIN	SCHAR_MIN	/* min value for a char */
#endif

#define	USHRT_MAX	0xffff		/* max value for an unsigned short */
#define	SHRT_MAX	0x7fff		/* max value for a short */
#define	SHRT_MIN	(-0x7fff - 1)	/* min value for a short */

#define	UINT_MAX	0xffffffffU	/* max value for an unsigned int */
#define	INT_MAX		0x7fffffff	/* max value for an int */
#define	INT_MIN		(-0x7fffffff - 1)	/* min value for an int */

#if defined(__x86_64__)
#define	ULONG_MAX	0xffffffffffffffffUL	/* max for an unsigned long */
#define	LONG_MAX	0x7fffffffffffffffL	/* max for a long */
#define	LONG_MIN	(-0x7fffffffffffffffL - 1) /* min for a long */

#ifdef __LONG_LONG_SUPPORTED
#define	ULLONG_MAX	0xffffffffffffffffULL	/* max value for an unsigned long long */
#define	LLONG_MAX	0x7fffffffffffffffLL	/* max value for a long long */
#define	LLONG_MIN	(-0x7fffffffffffffffLL - 1)  /* min for a long long */
#endif
#elif defined(__i386__)
#define	ULONG_MAX	0xffffffffUL		/* max for an unsigned long */
#define	LONG_MAX	0x7fffffffUL		/* max for a long */
#define	LONG_MIN	(-0x7fffffffL - 1)	/* min for a long */

#ifdef __LONG_LONG_SUPPORTED
#define	ULLONG_MAX	0xffffffffULL		/* max value for an unsigned long long */
#define	LLONG_MAX	0x7fffffffLL		/* max value for a long long */
#define	LLONG_MIN	(-0x7fffffffLL - 1)	/* min for a long long */
#endif
#endif

#if __POSIX_VISIBLE || __XSI_VISIBLE
#define	SSIZE_MAX	LONG_MAX	/* max value for a ssize_t */
#endif

#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
#define	SIZE_T_MAX	ULONG_MAX	/* max value for a size_t */

#define	OFF_MAX		LONG_MAX	/* max value for an off_t */
#define	OFF_MIN		LONG_MIN	/* min value for an off_t */
#endif

#if __BSD_VISIBLE
#define	GID_MAX		UINT_MAX        /* max value for a gid_t */
#define	UID_MAX		UINT_MAX        /* max value for a uid_t */

/* Quads and long longs are the same size.  Ensure they stay in sync. */
#define	UQUAD_MAX	ULLONG_MAX	/* max value for a uquad_t */
#define	QUAD_MAX	LLONG_MAX	/* max value for a quad_t */
#define	QUAD_MIN	LLONG_MIN	/* min value for a quad_t */
#endif

#if __XSI_VISIBLE || __POSIX_VISIBLE >= 200809
#if defined(__x86_64__)
#define	LONG_BIT	64
#elif defined(__i386__)
#define	LONG_BIT	32
#endif
#define	WORD_BIT	32
#endif

#endif /* !_CPU_LIMITS_H_ */
