/*-
 * Copyright (c) 2002 Mike Barcroft <mike@FreeBSD.org>
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
 *
 * $FreeBSD: src/include/strings.h,v 1.7 2008/11/03 10:22:19 kib Exp $
 */

#ifndef _STRINGS_H_
#define	_STRINGS_H_

#include <sys/cdefs.h>
#include <sys/types.h>

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

__BEGIN_DECLS
#if !defined(_KERNEL_VIRTUAL)
#if __BSD_VISIBLE || (__XSI_VISIBLE && __XSI_VISIBLE <= 600)
int	 bcmp(const void *, const void *, size_t) __pure;	/* LEGACY */
void	 bcopy(const void *, void *, size_t);			/* LEGACY */
void	 bzero(void *, size_t);					/* LEGACY */
#endif
int	 ffs(int) __pure2;
#endif
#ifdef __BSD_VISIBLE
int	 ffsll(long long) __pure2;
#if !defined(_KERNEL_VIRTUAL)
int	 ffsl(long) __pure2;
int	 fls(int) __pure2;
int	 flsl(long) __pure2;
#endif
int	 flsll(long long) __pure2;
#endif
#if !defined(_KERNEL_VIRTUAL)
#if __BSD_VISIBLE || (__XSI_VISIBLE && __XSI_VISIBLE <= 600)
char	*index(const char *, int) __pure;			/* LEGACY */
char	*rindex(const char *, int) __pure;			/* LEGACY */
#endif
int	 strcasecmp(const char *, const char *) __pure;
int	 strncasecmp(const char *, const char *, size_t) __pure;
#endif

#if __POSIX_VISIBLE >= 200809 || defined(_XLOCALE_H_)
#include <xlocale/_strings.h>
#endif
__END_DECLS

#endif /* _STRINGS_H_ */
