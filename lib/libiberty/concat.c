/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
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
 * $DragonFly: src/lib/libiberty/concat.c,v 1.1 2004/10/23 12:15:21 joerg Exp $
 */

#include <err.h>
#include <libiberty.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

static size_t
concat_strlen(const char *str1, va_list ap)
{
	size_t len, new_len;

	len = strlen(str1);

	while ((str1 = va_arg(ap, const char *)) != NULL) {
		new_len = len + strlen(str1);
		if (new_len < len)
			errx(EXIT_FAILURE, "overflow in %s", __func__);
		len = new_len;
	}
	return(len);
}

static void
concat_copy(char *buf, const char *str1, va_list ap)
{
	size_t len;

	do {
	    len = strlen(str1);
	    memcpy(buf, str1, len);
	    buf += len;
	} while ((str1 = va_arg(ap, const char *)) != NULL);

	*buf = '\0';
}

char *
concat(const char *str1, ...)
{
	va_list ap;
	size_t len;
	char *str;

	if (str1 == NULL)
		return(xstrdup(""));

	va_start(ap, str1);
	len = concat_strlen(str1, ap);
	va_end(ap);

	str = xmalloc(len + 1);
	va_start(ap, str1);
	concat_copy(str, str1, ap);
	va_end(ap);	

	return(str);
}

char *
reconcat(char *str1, ...)
{
	va_list ap;
	size_t len;
	char *str;

	va_start(ap, str1);
	len = concat_strlen("", ap);
	va_end(ap);

	str = xmalloc(len + 1);
	va_start(ap, str1);
	concat_copy(str, "", ap);
	va_end(ap);	

	free(str1);

	return(str);
}
