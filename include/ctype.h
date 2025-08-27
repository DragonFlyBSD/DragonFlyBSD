/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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
 *	@(#)ctype.h	8.4 (Berkeley) 1/21/94
 *      $FreeBSD: head/include/ctype.h 233600 2012-03-28 12:11:54Z theraven $
 */

#ifndef _CTYPE_H_
#define _CTYPE_H_

#include <sys/cdefs.h>
#include <machine/stdint.h>

#define	_CTYPE_A	0x00000100L		/* Alpha */
#define	_CTYPE_C	0x00000200L		/* Control */
#define	_CTYPE_D	0x00000400L		/* Digit */
#define	_CTYPE_G	0x00000800L		/* Graph */
#define	_CTYPE_L	0x00001000L		/* Lower */
#define	_CTYPE_P	0x00002000L		/* Punct */
#define	_CTYPE_S	0x00004000L		/* Space */
#define	_CTYPE_U	0x00008000L		/* Upper */
#define	_CTYPE_X	0x00010000L		/* X digit */
#define	_CTYPE_B	0x00020000L		/* Blank */
#define	_CTYPE_R	0x00040000L		/* Print */
#define	_CTYPE_I	0x00080000L		/* Ideogram */
#define	_CTYPE_T	0x00100000L		/* Special */
#define	_CTYPE_Q	0x00200000L		/* Phonogram */
#define	_CTYPE_N	0x00400000L		/* Number (superset of digit) */
#define	_CTYPE_SW0	0x20000000L		/* 0 width character */
#define	_CTYPE_SW1	0x40000000L		/* 1 width character */
#define	_CTYPE_SW2	0x80000000L		/* 2 width character */
#define	_CTYPE_SW3	0xc0000000L		/* 3 width character */
#define	_CTYPE_SWM	0xe0000000L		/* Mask for screen width data */
#define	_CTYPE_SWS	30			/* Bits to shift to get width */

/*
 * rune_t is declared to be an ``int'' instead of the more natural
 * ``unsigned long'' or ``long''.  Two things are happening here.  It is not
 * unsigned so that EOF (-1) can be naturally assigned to it and used.  Also,
 * it looks like 10646 will be a 31 bit standard.  This means that if your
 * ints cannot hold 32 bits, you will be in trouble.  The reason an int was
 * chosen over a long is that the is*() and to*() routines take ints (says
 * ANSI C), but they use __ct_rune_t instead of int.
 *
 * NOTE: rune_t is not covered by ANSI nor other standards, and should not
 * be instantiated outside of lib/libc/locale.  Use wchar_t.
 */
#ifndef ___CT_RUNE_T_DECLARED
typedef	int	__ct_rune_t;			/* Arg type for ctype funcs */
#define	___CT_RUNE_T_DECLARED
#endif

__BEGIN_DECLS
unsigned long	___runetype(__ct_rune_t) __pure;
__ct_rune_t	___tolower(__ct_rune_t) __pure;
__ct_rune_t	___toupper(__ct_rune_t) __pure;
__END_DECLS

/*
 * _EXTERNALIZE_CTYPE_INLINES_ is defined in locale/nomacros.c to tell us
 * to generate code for extern versions of all our inline functions.
 */
#ifdef _EXTERNALIZE_CTYPE_INLINES_
#define	_USE_CTYPE_INLINE_
#define	static
#undef	__always_inline
#define	__always_inline
#endif

extern int __mb_sb_limit;

/*
 * Use inline functions if we are allowed to and the compiler supports them.
 */
#if !defined(_DONT_USE_CTYPE_INLINE_) && \
    (defined(_USE_CTYPE_INLINE_) || defined(__GNUC__) || defined(__cplusplus))

#include <runetype.h>

static __always_inline int
__maskrune(__ct_rune_t _c, unsigned long _f)
{
	return ((_c < 0 || _c >= _CACHED_RUNES) ? ___runetype(_c) :
		_CurrentRuneLocale->__runetype[_c]) & _f;
}

static __always_inline int
__sbmaskrune(__ct_rune_t _c, unsigned long _f)
{
	return (_c < 0 || _c >= __mb_sb_limit) ? 0 :
	       _CurrentRuneLocale->__runetype[_c] & _f;
}

static __always_inline int
__istype(__ct_rune_t _c, unsigned long _f)
{
	return (!!__maskrune(_c, _f));
}

static __always_inline int
__sbistype(__ct_rune_t _c, unsigned long _f)
{
	return (!!__sbmaskrune(_c, _f));
}

static __always_inline int
__isctype(__ct_rune_t _c, unsigned long _f)
{
	return (_c < 0 || _c >= 128) ? 0 :
	       !!(_DefaultRuneLocale.__runetype[_c] & _f);
}

static __always_inline __ct_rune_t
__toupper(__ct_rune_t _c)
{
	return (_c < 0 || _c >= _CACHED_RUNES) ? ___toupper(_c) :
	       _CurrentRuneLocale->__mapupper[_c];
}

static __always_inline __ct_rune_t
__sbtoupper(__ct_rune_t _c)
{
	return (_c < 0 || _c >= __mb_sb_limit) ? _c :
	       _CurrentRuneLocale->__mapupper[_c];
}

static __always_inline __ct_rune_t
__tolower(__ct_rune_t _c)
{
	return (_c < 0 || _c >= _CACHED_RUNES) ? ___tolower(_c) :
	       _CurrentRuneLocale->__maplower[_c];
}

static __always_inline __ct_rune_t
__sbtolower(__ct_rune_t _c)
{
	return (_c < 0 || _c >= __mb_sb_limit) ? _c :
	       _CurrentRuneLocale->__maplower[_c];
}

static __always_inline int
__wcwidth(__ct_rune_t _c)
{
	unsigned int _x;

	if (_c == 0)
		return (0);
	_x = (unsigned int)__maskrune(_c, _CTYPE_SWM|_CTYPE_R);
	if ((_x & _CTYPE_SWM) != 0)
		return ((_x & _CTYPE_SWM) >> _CTYPE_SWS);
	return ((_x & _CTYPE_R) != 0 ? 1 : -1);
}

#else /* not using inlines */

__BEGIN_DECLS
int		__maskrune(__ct_rune_t, unsigned long);
int		__sbmaskrune(__ct_rune_t, unsigned long);
int		__istype(__ct_rune_t, unsigned long);
int		__sbistype(__ct_rune_t, unsigned long);
int		__isctype(__ct_rune_t, unsigned long);
__ct_rune_t	__toupper(__ct_rune_t);
__ct_rune_t	__sbtoupper(__ct_rune_t);
__ct_rune_t	__tolower(__ct_rune_t);
__ct_rune_t	__sbtolower(__ct_rune_t);
int		__wcwidth(__ct_rune_t);
__END_DECLS
#endif /* using inlines */


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

#if __XSI_VISIBLE
int	isascii(int);
int	toascii(int);
#endif

#if __ISO_C_VISIBLE >= 1999
int	isblank(int);
#endif

#if __BSD_VISIBLE
int	digittoint(int);
int	ishexnumber(int);
int	isideogram(int);
int	isnumber(int);
int	isphonogram(int);
int	isrune(int);
int	isspecial(int);
#endif

#if __POSIX_VISIBLE >= 200809 || defined(_XLOCALE_H_)
#include <xlocale/_ctype.h>
#endif
__END_DECLS

#ifndef __cplusplus
#define	isalnum(c)	__sbistype((c), _CTYPE_A|_CTYPE_D|_CTYPE_N)
#define	isalpha(c)	__sbistype((c), _CTYPE_A)
#define	iscntrl(c)	__sbistype((c), _CTYPE_C)
#define	isdigit(c)	__sbistype((c), _CTYPE_D)
#define	isgraph(c)	__sbistype((c), _CTYPE_G)
#define	islower(c)	__sbistype((c), _CTYPE_L)
#define	isprint(c)	__sbistype((c), _CTYPE_R)
#define	ispunct(c)	__sbistype((c), _CTYPE_P)
#define	isspace(c)	__sbistype((c), _CTYPE_S)
#define	isupper(c)	__sbistype((c), _CTYPE_U)
#define	isxdigit(c)	__sbistype((c), _CTYPE_X)
#define	tolower(c)	__sbtolower(c)
#define	toupper(c)	__sbtoupper(c)
#endif /* !__cplusplus */

#if __XSI_VISIBLE
/*
 * POSIX.1-2001 specifies _tolower() and _toupper() to be macros equivalent to
 * tolower() and toupper() respectively, minus extra checking to ensure that
 * the argument is a lower or uppercase letter respectively.  We've chosen to
 * implement these macros with the same error checking as tolower() and
 * toupper() since this doesn't violate the specification itself, only its
 * intent.  We purposely leave _tolower() and _toupper() undocumented to
 * discourage their use.
 *
 * XXX isascii() and toascii() should similarly be undocumented.
 */
#define	_tolower(c)	__sbtolower(c)
#define	_toupper(c)	__sbtoupper(c)
#define	isascii(c)	(((c) & ~0x7F) == 0)
#define	toascii(c)	((c) & 0x7F)
#endif

#if __ISO_C_VISIBLE >= 1999 && !defined(__cplusplus)
#define	isblank(c)	__sbistype((c), _CTYPE_B)
#endif

#if __BSD_VISIBLE
#define	digittoint(c)	__sbmaskrune((c), 0xFF)
#define	ishexnumber(c)	__sbistype((c), _CTYPE_X)
#define	isideogram(c)	__sbistype((c), _CTYPE_I)
#define	isnumber(c)	__sbistype((c), _CTYPE_D|_CTYPE_N)
#define	isphonogram(c)	__sbistype((c), _CTYPE_Q)
#define	isrune(c)	__sbistype((c), 0xFFFFFF00L)
#define	isspecial(c)	__sbistype((c), _CTYPE_T)
#endif

#endif /* !_CTYPE_H_ */
