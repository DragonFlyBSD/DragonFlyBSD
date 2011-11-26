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
 * extbuf.c
 * $Id: buffer.c,v 1.2 2005/02/06 06:57:30 cpressey Exp $
 * Routines to manipulate extensible buffers.
 *
 * Aura buffers are buffers that attempt to automatically expand
 * when more data is written to them than they can initially hold.
 * In addition, each extensible buffer contains a cursor from which
 * its contents may be incrementally scanned.
 */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "buffer.h"

/*
 * Create a new extensible buffer with the given initial size.
 */
struct aura_buffer *
aura_buffer_new(size_t size)
{
	struct aura_buffer *e;

	e = malloc(sizeof(struct aura_buffer));

	e->len = 0;
	e->size = size;
	e->pos = 0;

	e->buf = malloc(size);
	e->buf[0] = '\0';

	return(e);
}

/*
 * Deallocate the memory used for an extensible buffer.
 */
void
aura_buffer_free(struct aura_buffer *e)
{
	if (e != NULL) {
		if (e->buf != NULL)
			free(e->buf);
		free(e);
	}
}

/*
 * Return the underlying (static) buffer of an extensible buffer.
 *
 * NOTE that you should NEVER cache the returned pointer anywhere,
 * as any further manipulation of the extensible buffer may cause
 * it to be invalidated.
 *
 * ALSO NOTE that the buffer may contain embedded NULs, but will
 * also be guaranteed to be NUL-terminated.
 */
char *
aura_buffer_buf(struct aura_buffer *e)
{
	return(e->buf);
}

/*
 * Return the current length of the extensible buffer.
 */
size_t
aura_buffer_len(struct aura_buffer *e)
{
	return(e->len);
}

/*
 * Return the current size of the extensible buffer.  This is how
 * big it's length may grow to before expanded.
 */
size_t
aura_buffer_size(struct aura_buffer *e)
{
	return(e->size);
}

/*
 * Ensure that an extensible buffer's size is at least the given
 * size.  If it is not, it will be internally grown to that size.
 * This does not affect the contents of the buffer in any way.
 */
void
aura_buffer_ensure_size(struct aura_buffer *e, size_t size)
{
	if (e->size >= size) return;
	e->size = size;
	if ((e->buf = realloc(e->buf, e->size)) == NULL) {
		err(EX_UNAVAILABLE, "realloc()");
	}
}

/*
 * Set the contents of an extensible buffer from a regular (char *)
 * buffer.  The extensible buffer will grow if needed.  Any existing
 * contents of the extensible buffer are destroyed in this operation.
 * Note that, because this requires that the length of the
 * regular buffer be specified, it may safely contain NUL bytes.
 */
void
aura_buffer_set(struct aura_buffer *e, const char *buf, size_t length)
{
	while ((length + 1) > e->size) {
		e->size *= 2;
	}
	if ((e->buf = realloc(e->buf, e->size)) == NULL) {
		err(EX_UNAVAILABLE, "realloc()");
	}
	memcpy(e->buf, buf, length);
	e->len = length;
	e->buf[e->len] = '\0';
}

/*
 * Append the contents of a regular buffer to the end of the existing
 * contents of an extensible buffer.  The extensible buffer will grow
 * if needed.  Note that, because this requires that the length of the
 * regular buffer be specified, it may safely contain NUL bytes.
 */
void
aura_buffer_append(struct aura_buffer *e, const char *buf, size_t length)
{
	while (e->len + (length + 1) > e->size) {
		e->size *= 2;
	}
	if ((e->buf = realloc(e->buf, e->size)) == NULL) {
		err(EX_UNAVAILABLE, "realloc()");
	}
	memcpy(e->buf + e->len, buf, length);
	e->len += length;
	e->buf[e->len] = '\0';
}

/*
 * Set the contents of an extensible buffer from an ASCIIZ string.
 * This is identical to aura_buffer_set except that the length need not
 * be specified, and the ASCIIZ string may not contain embedded NUL's.
 */
void
aura_buffer_cpy(struct aura_buffer *e, const char *s)
{
	aura_buffer_set(e, s, strlen(s));
}

/*
 * Append the contents of an ASCIIZ string to an extensible buffer.
 * This is identical to aura_buffer_append except that the length need not
 * be specified, and the ASCIIZ string may not contain embedded NUL's.
 */
void
aura_buffer_cat(struct aura_buffer *e, const char *s)
{
	aura_buffer_append(e, s, strlen(s));
}

/*
 * Append the entire contents of a text file to an extensible buffer.
 */
int
aura_buffer_cat_file(struct aura_buffer *e, const char *fmt, ...)
{
	va_list args;
	char *filename, line[1024];
	FILE *f;

	va_start(args, fmt);
	vasprintf(&filename, fmt, args);
	va_end(args);

	if ((f = fopen(filename, "r")) == NULL)
		return(0);

	free(filename);

	while (fgets(line, 1023, f) != NULL) {
		aura_buffer_cat(e, line);
	}

	fclose(f);

	return(1);
}

/*
 * Append the entire output of a shell command to an extensible buffer.
 */
int
aura_buffer_cat_pipe(struct aura_buffer *e, const char *fmt, ...)
{
	va_list args;
	char *command, line[1024];
	FILE *p;

	va_start(args, fmt);
	vasprintf(&command, fmt, args);
	va_end(args);

	if ((p = popen(command, "r")) == NULL)
		return(0);

	free(command);

	while (fgets(line, 1023, p) != NULL) {
		aura_buffer_cat(e, line);
	}

	pclose(p);

	return(1);
}

/*** CURSORED FUNCTIONS ***/

/*
 * Note that the cursor can be anywhere from the first character to
 * one position _beyond_ the last character in the buffer.
 */

int
aura_buffer_seek(struct aura_buffer *e, size_t pos)
{
	if (pos <= e->size) {
		e->pos = pos;
		return(1);
	} else {
		return(0);
	}
}

size_t
aura_buffer_tell(struct aura_buffer *e)
{
	return(e->pos);
}

int
aura_buffer_eof(struct aura_buffer *e)
{
	return(e->pos >= e->size);
}

char
aura_buffer_peek_char(struct aura_buffer *e)
{
	return(e->buf[e->pos]);
}

char
aura_buffer_scan_char(struct aura_buffer *e)
{
	return(e->buf[e->pos++]);
}

int
aura_buffer_compare(struct aura_buffer *e, const char *s)
{
	size_t i, pos;

	for (i = 0, pos = e->pos; s[i] != '\0' && pos < e->size; i++, pos++) {
		if (e->buf[pos] != s[i])
			return(0);
	}

	if (pos <= e->size) {
		return(pos);
	} else {
		return(0);
	}
}

int
aura_buffer_expect(struct aura_buffer *e, const char *s)
{
	int pos;

	if ((pos = aura_buffer_compare(e, s)) > 0) {
		e->pos = pos;
		return(1);
	} else {
		return(0);
	}
}

void
aura_buffer_push(struct aura_buffer *e, const void *src, size_t len)
{
	aura_buffer_ensure_size(e, e->pos + len);
	memcpy(e->buf + e->pos, src, len);
	e->pos += len;
}

int
aura_buffer_pop(struct aura_buffer *e, void *dest, size_t len)
{
	if (e->pos - len > 0) {
		e->pos -= len;
		memcpy(dest, e->buf + e->pos, len);
		return(1);
	} else {
		return(0);
	}
}
