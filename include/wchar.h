/*	$NetBSD: src/include/wchar.h,v 1.20 2004/05/08 21:57:05 kleink Exp $	*/

/*-
 * Copyright (c)1999 Citrus Project,
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

/*-
 * Copyright (c) 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julian Coleman.
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
 */

#ifndef _WCHAR_H_
#define _WCHAR_H_

#include <sys/stdint.h>
/* XXX namespace pollution */
#include <machine/limits.h>

#include <stdio.h> /* for FILE* */
#include <sys/_null.h>

#ifndef __cplusplus
#ifndef _WCHAR_T_DECLARED
#define	_WCHAR_T_DECLARED
typedef	__wchar_t	wchar_t;
#endif
#endif

#ifndef WCHAR_MIN
#define	WCHAR_MIN	INT_MIN
#endif

#ifndef WCHAR_MAX
#define	WCHAR_MAX	INT_MAX
#endif

#ifndef _WINT_T_DECLARED
#define	_WINT_T_DECLARED
typedef __wint_t	wint_t;
#endif

#ifndef WINT_MIN
#define	WINT_MIN	INT_MIN
#endif

#ifndef WINT_MAX
#define	WINT_MAX	INT_MAX
#endif

#ifndef _MBSTATE_T_DECLARED
#define	_MBSTATE_T_DECLARED
typedef __mbstate_t	mbstate_t;
#endif

#ifndef _SIZE_T_DECLARED
#define _SIZE_T_DECLARED
typedef __size_t	size_t;
#endif

#ifndef WEOF
#define	WEOF 	((wint_t)(-1))
#endif

struct tm;

__BEGIN_DECLS
wint_t	btowc(int);
size_t	mbrlen(const char * __restrict, size_t, mbstate_t * __restrict);
size_t	mbrtowc(wchar_t * __restrict, const char * __restrict, size_t,
	    mbstate_t * __restrict);
int	mbsinit(const mbstate_t *);
size_t	mbsrtowcs(wchar_t * __restrict, const char ** __restrict, size_t,
	    mbstate_t * __restrict);
size_t	wcrtomb(char * __restrict, wchar_t, mbstate_t * __restrict);
wchar_t	*wcscat(wchar_t * __restrict, const wchar_t * __restrict);
wchar_t	*wcschr(const wchar_t *, wchar_t);
int	wcscmp(const wchar_t *, const wchar_t *);
int	wcscoll(const wchar_t *, const wchar_t *);
wchar_t	*wcscpy(wchar_t * __restrict, const wchar_t * __restrict);
size_t	wcscspn(const wchar_t *, const wchar_t *);
size_t	wcsftime(wchar_t * __restrict, size_t, const wchar_t * __restrict,
		 const struct tm * __restrict);
int	wcscasecmp(const wchar_t *, const wchar_t *);
int	wcsncasecmp(const wchar_t *, const wchar_t *, size_t n);
size_t	wcslen(const wchar_t *);
wchar_t	*wcsncat(wchar_t * __restrict, const wchar_t * __restrict, size_t);
int	wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t	*wcsncpy(wchar_t * __restrict , const wchar_t * __restrict, size_t);
size_t	 wcsnlen(const wchar_t *, size_t) __pure;
wchar_t	*wcspbrk(const wchar_t *, const wchar_t *);
wchar_t	*wcsrchr(const wchar_t *, wchar_t);
size_t	wcsrtombs(char * __restrict, const wchar_t ** __restrict, size_t,
		  mbstate_t * __restrict);
size_t	wcsspn(const wchar_t *, const wchar_t *);
wchar_t	*wcsstr(const wchar_t *, const wchar_t *);
wchar_t *wcstok(wchar_t * __restrict, const wchar_t * __restrict,
		wchar_t ** __restrict);
size_t	wcsxfrm(wchar_t *, const wchar_t *, size_t);
wchar_t	*wmemchr(const wchar_t *, wchar_t, size_t);
int	wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t	*wmemcpy(wchar_t * __restrict, const wchar_t * __restrict, size_t);
wchar_t	*wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t	*wmemset(wchar_t *, wchar_t, size_t);

size_t	wcslcat(wchar_t *, const wchar_t *, size_t);
size_t	wcslcpy(wchar_t *, const wchar_t *, size_t);
int	wcswidth(const wchar_t *, size_t);
int	wctob(wint_t);
int	wcwidth(wchar_t);

unsigned long wcstoul(const wchar_t * __restrict, wchar_t ** __restrict, int);
long 	wcstol(const wchar_t * __restrict, wchar_t ** __restrict, int);
double	wcstod(const wchar_t * __restrict, wchar_t ** __restrict);

#if __ISO_C_VISIBLE >= 1999 || __DF_VISIBLE
float	wcstof(const wchar_t * __restrict, wchar_t ** __restrict);
long double wcstold(const wchar_t * __restrict, wchar_t ** __restrict);

/* LONGLONG */
long long wcstoll(const wchar_t * __restrict, wchar_t ** __restrict, int);
/* LONGLONG */
unsigned long long wcstoull(const wchar_t * __restrict,
			    wchar_t ** __restrict, int);
#endif

wint_t	ungetwc(wint_t, FILE *);
wint_t	fgetwc(FILE *);
wchar_t	*fgetws(wchar_t * __restrict, int, FILE * __restrict);
wint_t	getwc(FILE *);
wint_t	getwchar(void);
wint_t	fputwc(wchar_t, FILE *);
int	fputws(const wchar_t * __restrict, FILE * __restrict);
wint_t	putwc(wchar_t, FILE *);
wint_t	putwchar(wchar_t);

int	fwide(FILE *, int);

wchar_t	*fgetwln(FILE * __restrict, size_t * __restrict);
int	fwprintf(FILE * __restrict, const wchar_t * __restrict, ...);
int	fwscanf(FILE * __restrict, const wchar_t * __restrict, ...);
int	swprintf(wchar_t * __restrict, size_t n,
		 const wchar_t * __restrict, ...);
int	swscanf(const wchar_t * __restrict, const wchar_t * __restrict, ...);
int	vfwprintf(FILE * __restrict, const wchar_t * __restrict, __va_list);
int	vswprintf(wchar_t * __restrict, size_t, const wchar_t * __restrict,
	      __va_list);
int	vwprintf(const wchar_t * __restrict, __va_list);
int	wprintf(const wchar_t * __restrict, ...);
int	wscanf(const wchar_t * __restrict, ...);
#if __ISO_C_VISIBLE >= 1999 || __DF_VISIBLE
int	vfwscanf(FILE * __restrict, const wchar_t * __restrict, __va_list);
int	vswscanf(const wchar_t * __restrict, const wchar_t * __restrict,
		 __va_list);
int	vwscanf(const wchar_t * __restrict, __va_list);
#endif

#if __POSIX_VISIBLE >= 200809 || __BSD_VISIBLE
wchar_t	*wcsdup(const wchar_t *);
#endif
__END_DECLS

#define getwc(f) fgetwc(f)
#define getwchar() getwc(stdin)
#define putwc(wc, f) fputwc((wc), (f))
#define putwchar(wc) putwc((wc), stdout)

#endif /* !_WCHAR_H_ */
