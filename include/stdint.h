/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _STDINT_H_
#define _STDINT_H_

#include <machine/stdint.h>

typedef __int8_t	int8_t;
typedef __int16_t	int16_t;
typedef __int32_t	int32_t;
typedef __int64_t	int64_t;

typedef __uint8_t	uint8_t;
typedef __uint16_t	uint16_t;
typedef __uint32_t	uint32_t;
typedef __uint64_t	uint64_t;

#ifndef _INTPTR_T_DECLARED
#define _INTPTR_T_DECLARED
typedef __intptr_t	intptr_t;
#endif
typedef __uintptr_t	uintptr_t;

typedef __intmax_t	intmax_t;
typedef __uintmax_t	uintmax_t;

typedef __int_fast8_t		int_fast8_t;
typedef __int_fast16_t		int_fast16_t;
typedef __int_fast32_t		int_fast32_t;
typedef __int_fast64_t		int_fast64_t;
typedef __int_least8_t		int_least8_t;
typedef __int_least16_t		int_least16_t;
typedef __int_least32_t		int_least32_t;
typedef __int_least64_t		int_least64_t;
typedef __uint_fast8_t		uint_fast8_t;
typedef __uint_fast16_t		uint_fast16_t;
typedef __uint_fast32_t		uint_fast32_t;
typedef __uint_fast64_t		uint_fast64_t;
typedef __uint_least8_t		uint_least8_t;
typedef __uint_least16_t	uint_least16_t;
typedef __uint_least32_t	uint_least32_t;
typedef __uint_least64_t	uint_least64_t;

#include <machine/int_const.h>
#include <machine/int_limits.h>
#include <machine/wchar_limits.h>

/* Also possibly defined in <wchar.h> */
/* Limits of wchar_t. */
#ifndef WCHAR_MIN
#define	WCHAR_MIN	__WCHAR_MIN
#endif
#ifndef WCHAR_MAX
#define	WCHAR_MAX	__WCHAR_MAX
#endif

/* Limits of wint_t. */
#ifndef WINT_MIN
#define	WINT_MIN	__WINT_MIN
#endif
#ifndef WINT_MAX
#define	WINT_MAX	__WINT_MAX
#endif

#if __EXT1_VISIBLE
#ifndef RSIZE_MAX
#define	RSIZE_MAX (SIZE_MAX >> 1)
#endif
#endif /* __EXT1_VISIBLE */

#endif /* !_STDINT_H_ */
