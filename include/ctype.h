/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ctype.h	5.3 (Berkeley) 4/3/91
 *	$NetBSD: src/include/ctype.h,v 1.25 2003/10/22 15:51:18 kleink Exp $
 *	$DragonFly: src/include/ctype.h,v 1.17 2006/03/06 02:54:12 swildner Exp $
 */

#ifndef _CTYPE_H_
#define _CTYPE_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>

#define	_CTYPEMASK_U	0x0001
#define	_CTYPEMASK_L	0x0002
#define	_CTYPEMASK_D	0x0004
#define	_CTYPEMASK_S	0x0008
#define	_CTYPEMASK_P	0x0010
#define	_CTYPEMASK_C	0x0020
#define	_CTYPEMASK_X	0x0040
#define	_CTYPEMASK_B	0x0080
#define	_CTYPEMASK_A	0x0100
#define	_CTYPEMASK_G	0x0200
#define	_CTYPEMASK_R	0x0400

extern const __uint16_t	*__libc_ctype_;
extern const __int16_t	*__libc_tolower_tab_;
extern const __int16_t	*__libc_toupper_tab_;

__BEGIN_DECLS
int	isalnum(int);
int	isalpha(int);
int	iscntrl(int);
int	isdigit(int);
int	isgraph(int);
int	islower(int);
int	isprint(int);
int	ispunct(int);
int	isspace(int);
int	isupper(int);
int	isxdigit(int);
int	tolower(int);
int	toupper(int);

#if defined(__XSI_VISIBLE)
int	isascii(int);
int	toascii(int);
int	_tolower(int);
int	_toupper(int);
#endif

#if __ISO_C_VISIBLE >= 1999 || __POSIX_VISIBLE >= 200112L || \
    __XSI_VISIBLE >= 600
int	isblank(int);
#endif
__END_DECLS

#ifdef _CTYPE_PRIVATE
extern const __uint16_t	__libc_C_ctype_[];
extern const __int16_t	__libc_C_toupper_[];
extern const __int16_t	__libc_C_tolower_[];
#endif

/*
 * don't get all wishy washy and try to support architectures where
 * char isn't 8 bits.   It's 8 bits, period.
 */
#if !defined(_CTYPE_H_DISABLE_MACROS_)

#define _CTYPE_NUM_CHARS	(1 << (8*sizeof(char)))

static __inline int
__libc_ctype_index(__uint16_t mask, int c)
{
	if (c < -1 || c >= _CTYPE_NUM_CHARS)
		return(0);	/* XXX maybe assert instead? */
	return(__libc_ctype_[c + 1] & mask);
}

static __inline int
__libc_ctype_convert(const __int16_t *array, int c)
{
	if (c < -1 || c >= _CTYPE_NUM_CHARS)
		return(c);	/* XXX maybe assert instead? */
	return(array[c + 1]);
}

#ifndef _CTYPE_PRIVATE
#undef _CTYPE_NUM_CHARS
#endif

#define	isdigit(c)	__libc_ctype_index(_CTYPEMASK_D, c)
#define	islower(c)	__libc_ctype_index(_CTYPEMASK_L, c)
#define	isspace(c)	__libc_ctype_index(_CTYPEMASK_S, c)
#define	ispunct(c)	__libc_ctype_index(_CTYPEMASK_P, c)
#define	isupper(c)	__libc_ctype_index(_CTYPEMASK_U, c)
#define	isalpha(c)	__libc_ctype_index(_CTYPEMASK_A, c)
#define	isxdigit(c)	__libc_ctype_index(_CTYPEMASK_X, c)
#define	isalnum(c)	__libc_ctype_index(_CTYPEMASK_A|_CTYPEMASK_D, c)
#define	isprint(c)	__libc_ctype_index(_CTYPEMASK_R, c)
#define	isgraph(c)	__libc_ctype_index(_CTYPEMASK_G, c)
#define	iscntrl(c)	__libc_ctype_index(_CTYPEMASK_C, c)
#define	tolower(c)	__libc_ctype_convert(__libc_tolower_tab_, c)
#define	toupper(c)	__libc_ctype_convert(__libc_toupper_tab_, c)

#if defined(__XSI_VISIBLE)
#define	isascii(c)	((unsigned)(c) <= 0177)
#define	toascii(c)	((c) & 0177)
#define _tolower(c)	((c) - 'A' + 'a')
#define _toupper(c)	((c) - 'a' + 'A')
#endif

#if __ISO_C_VISIBLE >= 1999 || __POSIX_VISIBLE >= 200112L || \
    __XSI_VISIBLE >= 600
#define isblank(c)	__libc_ctype_index(_CTYPEMASK_B, c)
#endif

#endif /* !_CTYPE_H_DISABLE_MACROS_ */

#endif /* !_CTYPE_H_ */
