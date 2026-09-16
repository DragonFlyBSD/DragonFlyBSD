/*
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Leding Li <lileding@gmail.com>
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

#include <sys/param.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdint.h>

int __getcwd(char *, size_t);

char *
getcwd(char *buffer, size_t size)
{
	int allocated;
	int error;

	allocated = 0;
	if (buffer == NULL) {
		size = MAXPATHLEN;
		buffer = malloc(size);
		if (buffer == NULL)
			return (NULL);
		allocated = 1;
	}
	error = __getcwd(buffer, size);
	if (error == 0)
		return (buffer);
	if (allocated)
		free(buffer);
	return (NULL);
}

int
getpagesize(void)
{
	return (PAGE_SIZE);
}

void
abort(void)
{
	__builtin_trap();
}

void
exit(int status)
{
	_exit(status);
	__builtin_unreachable();
}

/* Use the locale-independent Citrus parser with unsigned long limits. */
#include "../libc/citrus/citrus_bcs.h"

#define _FUNCNAME strtoul
#define __UINT unsigned long
#undef UINT_MAX
#define UINT_MAX ULONG_MAX
#define isspace(c) _citrus_bcs_isspace(c)
#define isxdigit(c) _citrus_bcs_isxdigit(c)
#define isdigit(c) _citrus_bcs_isdigit(c)
#define isalpha(c) _citrus_bcs_isalpha(c)
#define isupper(c) _citrus_bcs_isupper(c)
#include "../libc/citrus/_strtoul.h"
