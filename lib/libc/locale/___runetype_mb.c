/*	$NetBSD: src/lib/libc/locale/___runetype_mb.c,v 1.9 2003/08/07 16:43:03 agc Exp $	*/
/*	$DragonFly: src/lib/libc/locale/___runetype_mb.c,v 1.1 2005/03/16 06:54:41 joerg Exp $ */

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <wctype.h>
#include "rune.h"
#include "rune_local.h"

_RuneType
___runetype_mb(wint_t c)
{
	uint32_t x;
	_RuneRange *rr = &_CurrentRuneLocale->rl_runetype_ext;
	_RuneEntry *re = rr->rr_rune_ranges;

	if (c == WEOF)
		return(0U);

	for (x = 0; x < rr->rr_nranges; ++x, ++re) {
		/* XXX assumes wchar_t = int */
		if ((__nbrune_t)c < re->re_min)
			return(0U);
		if ((__nbrune_t)c <= re->re_max) {
			if (re->re_rune_types)
				return(re->re_rune_types[(__nbrune_t)c - re->re_min]);
			else
				return(re->re_map);
		}
	}
	return(0U);
}
