/*-
 * Copyright (c) 2001, 2002 Mike Barcroft <mike@FreeBSD.org>
 * Copyright (c) 2001 The NetBSD Foundation, Inc.  All rights reserved.
 * Copyright (c) 1990, 1993 The Regents of the University of California. 
 *		All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/_stdint.h,v 1.1 2002/07/29 17:41:07 mike Exp $
 * $DragonFly: src/sys/cpu/i386/include/stdint.h,v 1.1 2003/11/09 02:22:35 dillon Exp $
 */

#ifndef _MACHINE_STDINT_H_
#define	_MACHINE_STDINT_H_

/*
 * Basic types upon which most other types are built.
 */
typedef	__signed char	__int8_t;
typedef	unsigned char	__uint8_t;
typedef	short		__int16_t;
typedef	unsigned short	__uint16_t;
typedef	int		__int32_t;
typedef	unsigned int	__uint32_t;

/*
 * This mess is to override compiler options that might restrict long long
 * and for lint which doesn't understand GNUC attributes.
 */
#if defined(lint)
typedef	long long		__int64_t;
typedef	unsigned long long	__uint64_t;
#elif defined(__GNUC__)
typedef	int __attribute__((__mode__(__DI__)))		__int64_t;
typedef	unsigned int __attribute__((__mode__(__DI__)))	__uint64_t;
#else
typedef	long long		__int64_t;
typedef	unsigned long long	__uint64_t;
#endif

/*
 * mbstate_t is an opaque object to keep conversion state, during multibyte
 * stream conversions.  The content must not be referenced by user programs.
 */
typedef union {
	char            __mbstate8[128];
	__int64_t       _mbstateL;              /* for alignment */
} __mbstate_t;

/*
 * Standard type definitions.
 */
typedef	__int64_t	__intmax_t;
typedef	__uint64_t	__uintmax_t;

typedef	__int32_t	__intptr_t;
typedef	__uint32_t	__uintptr_t;

typedef	__int32_t	__ptrdiff_t;		/* ptr1 - ptr2 */

typedef	__int32_t	__int_fast8_t;
typedef	__int32_t	__int_fast16_t;
typedef	__int32_t	__int_fast32_t;
typedef	__int64_t	__int_fast64_t;
typedef	__int8_t	__int_least8_t;
typedef	__int16_t	__int_least16_t;
typedef	__int32_t	__int_least32_t;
typedef	__int64_t	__int_least64_t;
typedef	__uint32_t	__uint_fast8_t;
typedef	__uint32_t	__uint_fast16_t;
typedef	__uint32_t	__uint_fast32_t;
typedef	__uint64_t	__uint_fast64_t;
typedef	__uint8_t	__uint_least8_t;
typedef	__uint16_t	__uint_least16_t;
typedef	__uint32_t	__uint_least32_t;
typedef	__uint64_t	__uint_least64_t;

/*
 * System types conveniently placed in this header file in order to put them
 * in proximity with the limit macros below and for convenient access by
 * other include files which need to pick and choose particular types but
 * do not wish to overly pollute their namespaces.
 */

typedef __uint32_t	__size_t;
typedef __int32_t	__ssize_t;
typedef long		__time_t;
typedef int		__timer_t;
typedef __int32_t	__register_t;
typedef __uint32_t	__u_register_t;
typedef __int32_t	__sig_atomic_t;
typedef unsigned long	__clock_t;
typedef unsigned long	__clockid_t;
typedef __uint32_t	__socklen_t;

/*
 * Its convenient to put these here rather then create another header file.
 */
#define __offsetof(type, field) ((size_t)(&((type *)0)->field))
#define __arysize(ary)		(sizeof(ary)/sizeof((ary)[0]))

#endif /* _MACHINE_STDINT_H_ */

/*
 * OpenGroup stdint.h extensions.  Since these are protected by a define we
 * do not have to generate __ versions of them.
 */
#if !defined(__cplusplus) || defined(__STDC_CONSTANT_MACROS)
#ifndef _MACHINE_STDINT_H_STDC_CONSTANT_MACROS_
#define _MACHINE_STDINT_H_STDC_CONSTANT_MACROS_

#define	INT8_C(c)	(c)
#define	INT16_C(c)	(c)
#define	INT32_C(c)	(c)
#define	INT64_C(c)	(c ## LL)

#define	UINT8_C(c)	(c)
#define	UINT16_C(c)	(c)
#define	UINT32_C(c)	(c ## U)
#define	UINT64_C(c)	(c ## ULL)

#define	INTMAX_C(c)	(c ## LL)
#define	UINTMAX_C(c)	(c ## ULL)

#endif
#endif /* !defined(__cplusplus) || defined(__STDC_CONSTANT_MACROS) */

#if !defined(__cplusplus) || defined(__STDC_LIMIT_MACROS)
#ifndef _MACHINE_STDINT_H_STDC_LIMIT_MAVROS_
#define _MACHINE_STDINT_H_STDC_LIMIT_MAVROS_

/*
 * ISO/IEC 9899:1999
 * 7.18.2.1 Limits of exact-width integer types
 */
/* Minimum values of exact-width signed integer types. */
#define	INT8_MIN	(-0x7f-1)
#define	INT16_MIN	(-0x7fff-1)
#define	INT32_MIN	(-0x7fffffff-1)
#define	INT64_MIN	(-0x7fffffffffffffffLL-1)

/* Maximum values of exact-width signed integer types. */
#define	INT8_MAX	0x7f
#define	INT16_MAX	0x7fff
#define	INT32_MAX	0x7fffffff
#define	INT64_MAX	0x7fffffffffffffffLL

/* Maximum values of exact-width unsigned integer types. */
#define	UINT8_MAX	0xff
#define	UINT16_MAX	0xffff
#define	UINT32_MAX	0xffffffffU
#define	UINT64_MAX	0xffffffffffffffffULL

/*
 * ISO/IEC 9899:1999
 * 7.18.2.4  Limits of integer types capable of holding object pointers
 */
#define	INTPTR_MIN	INT32_MIN
#define	INTPTR_MAX	INT32_MAX
#define	UINTPTR_MAX	UINT32_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.2.5  Limits of greatest-width integer types
 */
#define	INTMAX_MIN	INT64_MIN
#define	INTMAX_MAX	INT64_MAX
#define	UINTMAX_MAX	UINT64_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.3  Limits of other integer types
 */
/* Limits of ptrdiff_t. */
#define	PTRDIFF_MIN	INT32_MIN	
#define	PTRDIFF_MAX	INT32_MAX

/* Limits of sig_atomic_t. */
#define	SIG_ATOMIC_MIN	INT32_MIN
#define	SIG_ATOMIC_MAX	INT32_MAX

/* Limit of size_t. */
#define	SIZE_MAX	UINT32_MAX

/* NOTE: wchar and wint macros in sys/stdint.h */

/*
 * ISO/IEC 9899:1999
 * 7.18.2.2  Limits of minimum-width integer types
 */
/* Minimum values of minimum-width signed integer types. */
#define	INT_LEAST8_MIN		INT8_MIN
#define	INT_LEAST16_MIN		INT16_MIN
#define	INT_LEAST32_MIN		INT32_MIN
#define	INT_LEAST64_MIN		INT64_MIN

/* Maximum values of minimum-width signed integer types. */
#define	INT_LEAST8_MAX		INT8_MAX
#define	INT_LEAST16_MAX		INT16_MAX
#define	INT_LEAST32_MAX		INT32_MAX
#define	INT_LEAST64_MAX		INT64_MAX

/* Maximum values of minimum-width unsigned integer types. */
#define	UINT_LEAST8_MAX		UINT8_MAX
#define	UINT_LEAST16_MAX	UINT16_MAX
#define	UINT_LEAST32_MAX	UINT32_MAX
#define	UINT_LEAST64_MAX	UINT64_MAX

/*
 * ISO/IEC 9899:1999
 * 7.18.2.3  Limits of fastest minimum-width integer types
 */
/* Minimum values of fastest minimum-width signed integer types. */
#define	INT_FAST8_MIN		INT32_MIN
#define	INT_FAST16_MIN		INT32_MIN
#define	INT_FAST32_MIN		INT32_MIN
#define	INT_FAST64_MIN		INT64_MIN

/* Maximum values of fastest minimum-width signed integer types. */
#define	INT_FAST8_MAX		INT32_MAX
#define	INT_FAST16_MAX		INT32_MAX
#define	INT_FAST32_MAX		INT32_MAX
#define	INT_FAST64_MAX		INT64_MAX

/* Maximum values of fastest minimum-width unsigned integer types. */
#define	UINT_FAST8_MAX		UINT32_MAX
#define	UINT_FAST16_MAX		UINT32_MAX
#define	UINT_FAST32_MAX		UINT32_MAX
#define	UINT_FAST64_MAX		UINT64_MAX

#endif
#endif /* !defined(__cplusplus) || defined(__STDC_LIMIT_MACROS) */

