/*	$NetBSD: efun.c,v 1.6 2008/04/28 20:23:02 martin Exp $	*/

/*-
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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

#ifndef _BMAKE_UTIL_H_
#define	_BMAKE_UTIL_H_

#include <sys/cdefs.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

static void (*efunc)(int, const char *, ...) = err;

static __always_inline char *
estrdup(const char *s)
{
	char *d = strdup(s);
	if (d == NULL)
		(*efunc)(1, "Cannot copy string");
	return d;
}

static __always_inline char *
estrndup(const char *s, size_t len)
{
	char *d = strndup(s, len);
	if (d == NULL)
		(*efunc)(1, "Cannot copy string");
	return d;
}

static __always_inline void *
emalloc(size_t n)
{
	void *p = malloc(n);
	if (p == NULL)
		(*efunc)(1, "Cannot allocate %zu bytes", n);
	return p;
}

static __always_inline void *
ecalloc(size_t n, size_t s)
{
	void *p = calloc(n, s);
	if (p == NULL)
		(*efunc)(1, "Cannot allocate %zu bytes", n);
	return p;
}

static __always_inline void *
erealloc(void *p, size_t n)
{
	void *q = realloc(p, n);
	if (q == NULL)
		(*efunc)(1, "Cannot re-allocate %zu bytes", n);
	return q;
}

#endif /* _BMAKE_UTIL_H_ */
