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
 *	@(#)stddef.h	8.1 (Berkeley) 6/2/93
 *
 * $FreeBSD: src/include/stddef.h,v 1.2.8.4 2002/08/07 15:49:32 imp Exp $
 * $DragonFly: src/include/stddef.h,v 1.3 2003/11/09 02:22:28 dillon Exp $
 */

#ifndef _STDDEF_H_
#define _STDDEF_H_

#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>			/* __rune_t and friends */
#endif

#ifndef _SIZE_T_DECLARED_
#define _SIZE_T_DECLARED_
typedef	__size_t	size_t;		/* open group */
#endif

#ifndef _PTRDIFF_T_DECLARED_
#define _PTRDIFF_T_DECLARED_
typedef	__ptrdiff_t	ptrdiff_t;	/* open group */
#endif

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)
#ifndef _RUNE_T_DECLARED_
#define _RUNE_T_DECLARED_
typedef	__rune_t	rune_t;
#endif
#endif

#ifndef	__cplusplus
#ifndef _WCHAR_T_DECLARED_
#define _WCHAR_T_DECLARED_
typedef __wchar_t	wchar_t;	/* open group */
#endif
#endif

#ifndef	NULL
#define	NULL	0
#endif

#define	offsetof(type, member)	__offsetof(type, member)

#endif /* _STDDEF_H_ */
