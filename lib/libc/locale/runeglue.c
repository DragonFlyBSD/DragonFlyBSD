/*	$NetBSD: src/lib/libc/locale/runeglue.c,v 1.10 2003/03/10 21:18:49 tshiozak Exp $	*/
/*	$DragonFly: src/lib/libc/locale/runeglue.c,v 1.3 2005/09/17 14:39:44 joerg Exp $ */

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
 *
 *	Id: runeglue.c,v 1.7 2000/12/22 22:52:29 itojun Exp
 */

/*
 * Glue code to hide "rune" facility from user programs.
 * This is important to keep backward/future compatibility.
 */

#define _CTYPE_PRIVATE

#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "rune.h"
#include "rune_local.h"

#if EOF != -1
#error "EOF != -1"
#endif
#if _CACHED_RUNES != 256
#error "_CACHED_RUNES != 256"
#endif

int
__runetable_to_netbsd_ctype(const char *locale)
{
	int i;
	uint16_t *new_ctype;
	int16_t *new_toupper, *new_tolower;

	_DIAGASSERT(locale != NULL);

	/* set to C locale, to ease failure case handling */
	if (__libc_ctype_ != __libc_C_ctype_) {
		/* LINTED const castaway */
		free(__DECONST(void *, __libc_ctype_));
		__libc_ctype_ = __libc_C_ctype_;
	}
	if (__libc_toupper_tab_ != __libc_C_toupper_) {
		/* LINTED const castaway */
		free(__DECONST(void *, __libc_toupper_tab_));
		__libc_toupper_tab_ = __libc_C_toupper_;
	}
	if (__libc_tolower_tab_ != __libc_C_tolower_) {
		/* LINTED const castaway */
		free(__DECONST(void *, __libc_tolower_tab_));
		__libc_tolower_tab_ = __libc_C_tolower_;
	}

	if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0)
		return(0);

	new_ctype = malloc(sizeof(*new_ctype) * (1 + _CTYPE_NUM_CHARS));
	if (new_ctype == NULL)
		return(-1);
	new_toupper = malloc(sizeof(*new_toupper) * (1 + 256));
	if (new_toupper == NULL) {
		free(new_ctype);
		return(-1);
	}
	new_tolower = malloc(sizeof(*new_tolower) * (1 + 256));
	if (new_tolower == NULL) {
		free(new_ctype);
		free(new_toupper);
		return(-1);
	}

	memset(new_ctype, 0, sizeof(*new_ctype) * (1 + _CTYPE_NUM_CHARS));
	memset(new_toupper, 0, sizeof(*new_toupper) * (1 + 256));
	memset(new_tolower, 0, sizeof(*new_tolower) * (1 + 256));

	new_ctype[0] = 0;
	new_toupper[0] = EOF;
	new_tolower[0] = EOF;
	for (i = 0; i < _CTYPE_NUM_CHARS; i++) {
		new_ctype[i + 1] = 0;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_U)
			new_ctype[i + 1] |= _CTYPEMASK_U;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_L)
			new_ctype[i + 1] |= _CTYPEMASK_L;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_D)
			new_ctype[i + 1] |= _CTYPEMASK_D;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_S)
			new_ctype[i + 1] |= _CTYPEMASK_S;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_P)
			new_ctype[i + 1] |= _CTYPEMASK_P;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_C)
			new_ctype[i + 1] |= _CTYPEMASK_C;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_X)
			new_ctype[i + 1] |= _CTYPEMASK_X;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_B)
			new_ctype[i + 1] |= _CTYPEMASK_B;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_A)
			new_ctype[i + 1] |= _CTYPEMASK_A;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_G)
			new_ctype[i + 1] |= _CTYPEMASK_G;
		if (_CurrentRuneLocale->rl_runetype[i] & _CTYPE_R)
			new_ctype[i + 1] |= _CTYPEMASK_R;

		new_toupper[i + 1] = (int16_t)_CurrentRuneLocale->rl_mapupper[i];
		new_tolower[i + 1] = (int16_t)_CurrentRuneLocale->rl_maplower[i];
	}

	__libc_ctype_ = new_ctype;
	__libc_toupper_tab_ = new_toupper;
	__libc_tolower_tab_ = new_tolower;

	return(0);
}
