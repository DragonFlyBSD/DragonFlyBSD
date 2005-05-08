/*	$NetBSD: src/lib/libc/locale/rune_local.h,v 1.7 2003/03/02 22:18:15 tshiozak Exp $	*/
/*	$DragonFly: src/lib/libc/locale/rune_local.h,v 1.2 2005/05/08 15:55:15 joerg Exp $ */

/*-
 * Copyright (c) 2000 Citrus Project,
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

#ifndef _RUNE_LOCAL_H_
#define _RUNE_LOCAL_H_

/* rune.c */
_RuneLocale *_Read_RuneMagi(FILE *fp);
void	_NukeRune(_RuneLocale *);

/* setrunelocale.c */
int	_xpg4_setrunelocale(const char *);
_RuneLocale *_findrunelocale(char *);
int	_newrunelocale(char *);

/* runeglue.c */
int	__runetable_to_netbsd_ctype(const char *);

/* ___runetype_mb.c */
_RuneType ___runetype_mb(wint_t);

/* ___tolower_mb.c */
wint_t	___tolower_mb(wint_t);

/* ___toupper_mb.c */
wint_t	___toupper_mb(wint_t);

#endif
