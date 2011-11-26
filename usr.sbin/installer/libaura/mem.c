/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Chris Pressey <cpressey@catseye.mine.nu>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * mem.c
 * $Id: mem.c,v 1.2 2005/02/06 06:57:30 cpressey Exp $
 * Aura memory management functions.
 */

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#ifdef TRACK_ALLOCATION
#include <stdio.h>
#endif

#include "mem.h"

void *
aura_malloc(size_t size, const char *what)
{
	void *ptr;

	if ((ptr = malloc(size)) == NULL) {
		errx(EX_UNAVAILABLE, "malloc(%s) failed", what);
	}
#ifdef TRACK_ALLOCATION
	fprintf(stderr, "***** malloc(%s) = %08lx\n", what, ptr);
#endif
	bzero(ptr, size);

	return(ptr);
}

char *
aura_strdup(const char *string)
{
	char *ptr;

	if ((ptr = strdup(string)) == NULL) {
		errx(EX_UNAVAILABLE, "strdup(\"%s\") failed", string);
	}
#ifdef TRACK_ALLOCATION
	fprintf(stderr, "***** strdup(\"%s\") = %08lx\n", string, ptr);
#endif

	return(ptr);
}

void
#ifdef TRACK_ALLOCATION
aura_free(void *ptr, const char *what)
#else
aura_free(void *ptr, const char *what __unused)
#endif
{
#ifdef TRACK_ALLOCATION
	fprintf(stderr, "***** free(%s) = %08lx\n", what, ptr);
#endif
	free(ptr);
}
