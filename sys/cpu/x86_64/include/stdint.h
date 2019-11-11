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
 */

#ifndef _CPU_STDINT_H_
#define	_CPU_STDINT_H_

#include <sys/cdefs.h>

/*
 * Basic types upon which most other types are built.
 */
typedef	__signed char	__int8_t;
typedef	unsigned char	__uint8_t;
typedef	short		__int16_t;
typedef	unsigned short	__uint16_t;
typedef	int		__int32_t;
typedef	unsigned int	__uint32_t;
#if defined(__cplusplus) || __STDC_VERSION__ < 199901L && !__GNUC_PREREQ__(3, 0)
typedef	int		__boolean_t;
#else
typedef	_Bool		__boolean_t;
#endif

#ifdef __LP64__
typedef	long		__int64_t;
typedef	unsigned long	__uint64_t;
#else
__extension__
typedef	long long	__int64_t;
__extension__
typedef	unsigned long long __uint64_t;
#endif

/*
 * Basic signed and unsigned LP types.  We support LP64 and ILP32 models only.
 */
#ifdef __LP64__
typedef	long		__intlp_t;
typedef	unsigned long	__uintlp_t;
#else
typedef	int		__intlp_t;
typedef	unsigned int	__uintlp_t;
#endif

/*
 * Standard type definitions.
 */
typedef	__int64_t	__intmax_t;
typedef	__uint64_t	__uintmax_t;

typedef	__intlp_t	__intptr_t;
typedef	__uintlp_t	__uintptr_t;
typedef	__intlp_t	__ptrdiff_t;	/* ptr1 - ptr2 */

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

/* <sys/types.h> */
typedef	unsigned long	__clock_t;	/* ticks in CLOCKS_PER_SEC */
typedef	unsigned long	__clockid_t;	/* CLOCK_* identifiers */
typedef	__int64_t	__off_t;	/* file offset or size */
typedef	__int32_t	__pid_t;	/* process [group] id */
typedef	__uintlp_t	__size_t;	/* sizes of objects */
typedef	__intlp_t	__ssize_t;	/* byte counts or error status */
typedef	long		__suseconds_t;	/* microseconds (signed) */
typedef	__intlp_t	__time_t;	/* epoch time */
typedef	int		__timer_t;	/* POSIX timer identifiers */

/* misc types */
#if __GNUC_PREREQ__(2, 7)
typedef	int		__register_t __attribute__((__mode__(__word__)));
#else
typedef	__intlp_t	__register_t;
#endif

typedef __int32_t	__sig_atomic_t;	/* XXX */
typedef __uint32_t	__socklen_t;
typedef volatile int	__atomic_intr_t;
typedef __int64_t	__rlim_t;

#endif /* _CPU_STDINT_H_ */
