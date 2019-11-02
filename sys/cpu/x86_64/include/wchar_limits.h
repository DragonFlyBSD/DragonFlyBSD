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

#ifndef _CPU_WCHAR_LIMITS_H_
#define _CPU_WCHAR_LIMITS_H_

/*
 * 7.18.3 Limits of other integer types
 */

/*
 * The wchar_t type is a builtin type in c++, we assume that compiler
 * provides correct type and limits, otherwise use fallback values.
 * Allow compiler to override wchar_t limits with -fshort-wchar.
 */

/* Limits of wchar_t. */
#if defined(__cplusplus) && defined(__WCHAR_MAX__) && defined(__WCHAR_MIN__)
#define	__WCHAR_MIN	__WCHAR_MIN__	/* min value for c++ wchar_t */
#define	__WCHAR_MAX	__WCHAR_MAX__	/* max value for c++ wchar_t */
#elif defined(__SIZEOF_WCHAR_T__) && __SIZEOF_WCHAR_T__ == 2
#if defined(__WCHAR_MAX__) && defined(__WCHAR_MIN__)
#define	__WCHAR_MIN	__WCHAR_MIN__	/* min value for short wchar_t */
#define	__WCHAR_MAX	__WCHAR_MAX__	/* max value for short wchar_t */
#else
#define	__WCHAR_MIN	0	/* min value for short wchar_t (well, zero) */
#define	__WCHAR_MAX	0xffff	/* max value for short wchar_t (UINT16_MAX) */
#endif
#else
#define	__WCHAR_MIN	(-0x7fffffff-1)	/* min value for wchar_t (INT32_MIN) */
#define	__WCHAR_MAX	0x7fffffff	/* max value for wchar_t (INT32_MAX) */
#endif

/* Limits of wint_t. */
#define	__WINT_MIN	(-0x7fffffff-1)	/* min value for wint_t (INT32_MIN) */
#define	__WINT_MAX	0x7fffffff	/* max value for wint_t (INT32_MAX) */

#endif /* !_CPU_WCHAR_LIMITS_H_ */
