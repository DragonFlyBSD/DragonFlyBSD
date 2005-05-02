/*	$NetBSD: src/include/ctype.h,v 1.25 2003/10/22 15:51:18 kleink Exp $	*/
/*	$DragonFly: src/include/ctype.h,v 1.7 2005/05/02 17:41:05 joerg Exp $ */

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
 */

#ifndef _CTYPE_H_
#define _CTYPE_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>

#define	_U	0x01
#define	_L	0x02
#define	_N	0x04
#define	_S	0x08
#define	_P	0x10
#define	_C	0x20
#define	_X	0x40
#define	_B	0x80

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

#if _ISO_C_VISIBLE >= 1999 || _POSIX_VISIBLE >= 200112L || \
    __XSI_VISIBLE >= 600
int	isblank(int);
#endif
__END_DECLS

#define	isdigit(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _N))
#define	islower(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _L))
#define	isspace(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _S))
#define	ispunct(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _P))
#define	isupper(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _U))
#define	isalpha(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & (_U|_L)))
#define	isxdigit(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & (_N|_X)))
#define	isalnum(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & (_U|_L|_N)))
#define	isprint(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & (_P|_U|_L|_N|_S)))
#define	isgraph(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & (_P|_U|_L|_N)))
#define	iscntrl(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _C))
#define	tolower(c)	((int)((__libc_tolower_tab_ + 1)[(int)(c)]))
#define	toupper(c)	((int)((__libc_toupper_tab_ + 1)[(int)(c)]))

#if defined(__XSI_VISIBLE)
#define	isascii(c)	((unsigned)(c) <= 0177)
#define	toascii(c)	((c) & 0177)
#define _tolower(c)	((c) - 'A' + 'a')
#define _toupper(c)	((c) - 'a' + 'A')
#endif

#if __ISO_C_VISIBLE >= 1999 || __POSIX_VISIBLE >= 200112L || \
    __XSI_VISIBLE >= 600
#define isblank(c)	((int)((__libc_ctype_ + 1)[(int)(c)] & _B))
#endif

#ifdef _CTYPE_PRIVATE
#include <machine/limits.h>	/* for CHAR_BIT */

#define _CTYPE_NUM_CHARS	(1 << CHAR_BIT)

#define _CTYPE_ID	 	"DFCTYPE"
#define _CTYPE_REV		3

extern const __uint16_t	__libc_C_ctype_[];
extern const __int16_t	__libc_C_toupper_[];
extern const __int16_t	__libc_C_tolower_[];
#endif

#endif /* !_CTYPE_H_ */
