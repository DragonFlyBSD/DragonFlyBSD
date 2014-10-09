/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)libkern.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/sys/libkern.h,v 1.20.2.2 2001/09/30 21:12:54 luigi Exp $
 */

#ifndef _SYS_LIBKERN_H_
#define	_SYS_LIBKERN_H_

#if !defined(_KERNEL)
#error "This file should not be included by userland programs."
#else

/*
 * Hacks to prevent conflicts with standard header files
 */

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif
#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif

/* BCD conversions. */
extern u_char const	bcd2bin_data[];
extern u_char const	bin2bcd_data[];
extern char const	hex2ascii_data[];

#define	bcd2bin(bcd)	(bcd2bin_data[bcd])
#define	bin2bcd(bin)	(bin2bcd_data[bin])
#define	hex2ascii(hex)	(hex2ascii_data[hex])

/*
 * Number of bytes needed for hexadecimal representation
 * plus trailing \0
 */
#define HEX_NCPYLEN(s) (s * 3)

static __inline int imax(int a, int b) { return (a > b ? a : b); }
static __inline int imin(int a, int b) { return (a < b ? a : b); }
static __inline long lmax(long a, long b) { return (a > b ? a : b); }
static __inline long lmin(long a, long b) { return (a < b ? a : b); }
static __inline u_int umax(u_int a, u_int b) { return (a > b ? a : b); }
static __inline u_int umin(u_int a, u_int b) { return (a < b ? a : b); }
static __inline u_int max(u_int a, u_int b) { return (a > b ? a : b); }
static __inline u_int min(u_int a, u_int b) { return (a < b ? a : b); }
static __inline quad_t qmax(quad_t a, quad_t b) { return (a > b ? a : b); }
static __inline quad_t qmin(quad_t a, quad_t b) { return (a < b ? a : b); }
static __inline u_long ulmax(u_long a, u_long b) { return (a > b ? a : b); }
static __inline u_long ulmin(u_long a, u_long b) { return (a < b ? a : b); }
static __inline u_int64_t u64max(u_int64_t a, u_int64_t b) { return (a > b ? a : b); }
static __inline u_int64_t u64min(u_int64_t a, u_int64_t b) { return (a < b ? a : b); }
static __inline int64_t i64max(int64_t a, int64_t b) { return (a > b ? a : b); }
static __inline int64_t i64min(int64_t a, int64_t b) { return (a < b ? a : b); }
static __inline size_t szmax(size_t a, size_t b) { return (a > b ? a : b); }
static __inline size_t szmin(size_t a, size_t b) { return (a < b ? a : b); }

static __inline int abs(int a) { return (a < 0 ? -a : a); }
static __inline long labs(long a) { return (a < 0 ? -a : a); }
static __inline quad_t qabs(quad_t a) { return (a < 0 ? -a : a); }

/* Prototypes for non-quad routines. */
u_int32_t karc4random (void);
void	karc4rand (void *, size_t);
int	 bcmp (const void *, const void *, size_t);
void	*kbsearch (const void *, const void *, size_t,
		     size_t, int (*)(const void *, const void *));
#ifndef HAVE_INLINE_FFS
int	 ffs (int);
#endif
#ifndef HAVE_INLINE_FFSL
int	 ffsl (long);
#endif
#ifndef	HAVE_INLINE_FLS
int	 fls (int);
#endif
#ifndef HAVE_INLINE_FLSL
int	 flsl(long);
#endif
int	 locc (int, char *, u_int);
void	 kqsort (void *base, size_t nmemb, size_t size,
		    int (*compar)(const void *, const void *));
void	 kqsort_r(void *base, size_t nmemb, size_t size, void *thunk,
		    int (*compar)(void *, const void *, const void *));
u_long	 krandom (void);
char	*index (const char *, int);
char	*rindex (const char *, int);
int	 scanc (u_int, const u_char *, const u_char *, int);
int	 skpc (int, int, char *);
void	 skrandom (u_long);
char	*strcat (char * __restrict, const char * __restrict);
char	*strncat(char * __restrict, const char * __restrict, size_t);
int	 strcmp (const char *, const char *);
int	 strcasecmp (const char *, const char *);
char	*strcpy (char * __restrict, const char * __restrict);
size_t   strlcat (char *, const char *, size_t);
size_t   strlcpy (char *, const char *, size_t);
size_t	 strlen (const char *);
int	 strncmp (const char *, const char *, size_t);
int	 strncasecmp (const char *, const char *, size_t);
char	*strncpy (char * __restrict, const char * __restrict, size_t);
char	*strsep(char **, const char *);
char	*strstr(const char *, const char *);
int	_kfnmatch(const char *, const char *, int, int);
void *	memcchr(const void *, int, size_t);
char *	hexncpy(const u_char *, int, char *, int, const char *);

/*
 * memset can't be an inline, it is used by gcc-4.x directly.
 */
void	*memset(void *b, int c, size_t len);
void	*memmove(void *s1, const void *s2, size_t n);


static __inline int
kfnmatch(const char *pattern, const char *string, int flags)
{
	return(_kfnmatch(pattern, string, flags, 0));
}

static __inline int
memcmp(const void *b1, const void *b2, size_t len)
{
	return (bcmp(b1, b2, len));
}

static __inline char *
strchr(const char *p, int ch)
{
	return (index(p, ch));
}

static __inline char *
strrchr(const char *p, int ch)
{
	return (rindex(p, ch));
}

/* kfnmatch() return values. */
#define	FNM_NOMATCH	1	/* Match failed. */

/* kfnmatch() flags. */
#define	FNM_NOESCAPE	0x01	/* Disable backslash escaping. */
#define	FNM_PATHNAME	0x02	/* Slash must be matched by slash. */
#define	FNM_PERIOD	0x04	/* Period must be matched by period. */
#define	FNM_LEADING_DIR	0x08	/* Ignore /<tail> after Imatch. */
#define	FNM_CASEFOLD	0x10	/* Case insensitive search. */
#define	FNM_IGNORECASE	FNM_CASEFOLD
#define	FNM_FILE_NAME	FNM_PATHNAME

#endif	/* _KERNEL */
#endif	/* !_SYS_LIBKERN_H_ */
