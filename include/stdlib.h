/*-
 * Copyright (c) 1990, 1993
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
 *	@(#)stdlib.h	8.5 (Berkeley) 5/19/95
 * $FreeBSD: src/include/stdlib.h,v 1.16.2.5 2002/12/13 01:34:00 tjr Exp $
 * $DragonFly: src/include/stdlib.h,v 1.8 2004/08/15 16:01:11 joerg Exp $
 */

#ifndef _STDLIB_H_
#define	_STDLIB_H_

#include <sys/cdefs.h>

#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>
#endif

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)
#ifndef _RUNE_T_DECLARED
#define _RUNE_T_DECLARED
typedef __rune_t	rune_t;
#endif
#endif

#ifndef __cplusplus
#ifndef _WCHAR_T_DECLARED
#define _WCHAR_T_DECLARED
typedef __wchar_t	wchar_t;
#endif
#endif

#ifndef _SIZE_T_DECLARED
#define _SIZE_T_DECLARED
typedef __size_t	size_t;
#endif

typedef struct {
	int quot;		/* quotient */
	int rem;		/* remainder */
} div_t;

typedef struct {
	long quot;		/* quotient */
	long rem;		/* remainder */
} ldiv_t;

#ifndef NULL
#define	NULL	0
#endif

#define	EXIT_FAILURE	1
#define	EXIT_SUCCESS	0

#define	RAND_MAX	0x7fffffff

extern int __mb_cur_max;
#define	MB_CUR_MAX	__mb_cur_max

__BEGIN_DECLS
void	 abort(void) __dead2;
int	 abs(int) __pure2;
int	 atexit(void (*)(void));
double	 atof(const char *);
int	 atoi(const char *);
long	 atol(const char *);
void	*bsearch(const void *, const void *, size_t,
		 size_t, int (*)(const void *, const void *));
void	*calloc(size_t, size_t);
div_t	 div(int, int) __pure2;
void	 exit(int) __dead2;
void	 free(void *);
char	*getenv(const char *);
long	 labs(long) __pure2;
ldiv_t	 ldiv(long, long) __pure2;
void	*malloc(size_t);
void	 qsort(void *, size_t, size_t, int(*)(const void *, const void *));
int	 rand(void);
void	*realloc(void *, size_t);
void	 srand(unsigned);
double	 strtod(const char *, char **);
long	 strtol(const char *, char **, int);
unsigned long	strtoul(const char *, char **, int);
#ifdef __LONG_LONG_SUPPORTED
long long	strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
#endif
int	 system(const char *);

int	 mblen(const char *, size_t);
size_t	 mbstowcs(wchar_t *, const char *, size_t);
int	 wctomb(char *, wchar_t);
int	 mbtowc(wchar_t *, const char *, size_t);
size_t	 wcstombs(char *, const wchar_t *, size_t);

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)
int	 putenv(const char *);
int	 setenv(const char *, const char *, int);

double	 drand48(void);
double	 erand48(unsigned short[3]);
long	 jrand48(unsigned short[3]);
void	 lcong48(unsigned short[7]);
long	 lrand48(void);
long	 mrand48(void);
long	 nrand48(unsigned short[3]);
unsigned short *seed48(unsigned short[3]);
void	 srand48(long);

void	*alloca(size_t);		/* built-in for gcc */
					/* getcap(3) functions */
__uint32_t arc4random(void);
void	 arc4random_addrandom(unsigned char *, int);
void	 arc4random_stir(void);
char	*getbsize(int *, long *);
char	*cgetcap(char *, char *, int);
int	 cgetclose(void);
int	 cgetent(char **, char **, char *);
int	 cgetfirst(char **, char **);
int	 cgetmatch(char *, char *);
int	 cgetnext(char **, char **);
int	 cgetnum(char *, char *, long *);
int	 cgetset(char *);
int	 cgetstr(char *, char *, char **);
int	 cgetustr(char *, char *, char **);

int	 daemon(int, int);
char	*devname(int, int);
char	*devname_r(int, int, char *, size_t);
int	 getloadavg(double [], int);
const char *getprogname(void);

char	*group_from_gid(unsigned long, int);
int	 heapsort(void *, size_t, size_t, int (*)(const void *, const void *));
char	*initstate(unsigned long, char *, long);
int	 mergesort(void *, size_t, size_t, int (*)(const void *, const void *));
int	 radixsort(const unsigned char **, int, const unsigned char *,
		   unsigned int);
int	 sradixsort(const unsigned char **, int, const unsigned char *,
		    unsigned int);
int	 rand_r(unsigned *);
long	 random(void);
void    *reallocf(void *, size_t);
char	*realpath(const char *, char []);
void	 setprogname(const char *);
char	*setstate(char *);
void	 srandom(unsigned long);
void	 srandomdev(void);
char	*user_from_uid(unsigned long, int);
#ifndef __STRICT_ANSI__
__int64_t	strtoq(const char *, char **, int);
__uint64_t	strtouq(const char *, char **, int);

#ifdef __LONG_LONG_SUPPORTED
unsigned long long strtonum(const char *, long long, unsigned long long,
			    const char **);
#endif
#endif
void	 unsetenv(const char *);
#endif /* !_ANSI_SOURCE && !_POSIX_SOURCE */
__END_DECLS

#endif /* !_STDLIB_H_ */
