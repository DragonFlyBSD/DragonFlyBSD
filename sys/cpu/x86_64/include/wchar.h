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

#ifndef _CPU_WCHAR_H_
#define _CPU_WCHAR_H_

#include <machine/stdint.h>

/*
 * wchar_t, wint_t and rune_t are signed so that EOF (-1) can be naturally
 * assigned to it and used.  The rune_t is meant for internal use only
 * (see <ctype.h>).
 */

/*
 * wchar_t and rune_t have to be of the same type.  However there are some
 * issues with language binding (c++ specifically where it is a keyword).
 * Also "clang -fms-extensions" has a reserved keyword __wchar_t.  Use
 * ___wchar_t type only to declare wchar_t to avoid conflicts in headers.
 *
 * ANSI specifies ``int'' as argument for the is*() and to*() routines.
 * Keeping wchar_t and rune_t as ``int'' instead of the more natural
 * ``long'' helps ANSI conformance. ISO 10646 will most likely end up as
 * 31 bit standard and all supported architectures have sizeof(int) >= 4.
 *
 * Allow compiler to override wchar_t with -fshort-wchar.
 */
#ifndef __cplusplus
#if defined(__SIZEOF_WCHAR_T__) && __SIZEOF_WCHAR_T__ == 2
#if defined(__WCHAR_TYPE__)
typedef	__WCHAR_TYPE__	___wchar_t;	/* compiler short wchar type */
#else
typedef	unsigned short	___wchar_t;
#endif
#else
typedef	int		___wchar_t;	/* same as __ct_rune_t */
#endif
#endif

/*
 * wint_t and rune_t must be the same type.  Also, wint_t should be able to
 * hold all members of the largest character set plus one extra value (WEOF),
 * and must be at least 16 bits.
 */
typedef	int		__wint_t;

/*
 * mbstate_t is an opaque object to keep conversion state, during multibyte
 * stream conversions.  The content must not be referenced by user programs.
 */
typedef union {
	__uint8_t	__mbstate8[128];
	__int64_t	__mbstateL;	/* for alignment */
} __mbstate_t;

#endif /* !_CPU_WCHAR_H_ */
