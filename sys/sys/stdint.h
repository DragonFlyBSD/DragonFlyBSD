/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_STDINT_H_
#define	_SYS_STDINT_H_

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif
#include <machine/stdint.h>

/*
 * Proxy header for kernel compilation.  Do not include outside kernel
 * headers. Userland should use <stdint.h> instead.
 *
 * This header is not a convenient placeholder for non integer types.
 */

#ifdef _KERNEL
#include <machine/int_limits.h>

typedef	__boolean_t	boolean_t;		/* kernel only */

#if !defined(__bool_true_false_are_defined) && !defined(__cplusplus)
#define	__bool_true_false_are_defined	1
#define	false	0
#define	true	1
#if __STDC_VERSION__ < 199901L && !__GNUC_PREREQ__(3, 0)
typedef	int	_Bool;
#endif
typedef	_Bool	bool;
#endif /* !__bool_true_false_are_defined && !__cplusplus */

#define	offsetof(type, field)	__offsetof(type, field)

#ifndef _PTRDIFF_T_DECLARED
typedef	__ptrdiff_t	ptrdiff_t;	/* ptr1 - ptr2 for kernel */
#define	_PTRDIFF_T_DECLARED
#endif

typedef	__int8_t	int8_t;
typedef	__int16_t	int16_t;
typedef	__int32_t	int32_t;
typedef	__int64_t	int64_t;

typedef	__uint8_t	uint8_t;
typedef	__uint16_t	uint16_t;
typedef	__uint32_t	uint32_t;
typedef	__uint64_t	uint64_t;

#ifndef _INTPTR_T_DECLARED
typedef	__intptr_t	intptr_t;	/* VKERNEL uses <unistd.h> */
#define	_INTPTR_T_DECLARED
#endif
typedef	__uintptr_t	uintptr_t;

typedef	__intmax_t	intmax_t;
typedef	__uintmax_t	uintmax_t;
#endif /* _KERNEL */

#ifndef _KERNEL
#ifndef _STDINT_H_
#include <stdint.h>			/* in case we still need it */
#endif
#endif /* !_KERNEL */

#endif /* !_SYS_STDINT_H_ */
