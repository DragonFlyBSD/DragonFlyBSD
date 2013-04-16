/*-
 * Copyright (c) 2011 Venkatesh Srinivas, 
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

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>

struct memstream_cookie {
	char **pub_buf;
	size_t *pub_size;

	char *head;
	size_t pos;
	size_t tail;
};

static void
sync_pub_cookie(struct memstream_cookie *cp)
{
	*cp->pub_buf = cp->head;
	*cp->pub_size = cp->tail; 
}

static int
memstream_writefn(void *cookie, const char *buf, int len)
{
	struct memstream_cookie *c;
	size_t reqsize;

	c = cookie;

	/* Write is contained within valid region */
	if (c->pos + len < c->tail) {
		bcopy(buf, &c->head[c->pos], len);
		c->pos += len;
		return (len);
	}

	/* Write results in resizing buffer */
	reqsize = c->pos + len + 1;
	c->head = reallocf(c->head, reqsize);
	if (c->head == NULL) {
		errno = ENOMEM;
		return (0);
	}

	bcopy(buf, &c->head[c->pos], len);
	
	c->tail = c->pos + len;
	c->pos = c->tail;
	c->head[c->tail] = '\0';

	sync_pub_cookie(c);

	return (len);
}

static fpos_t
memstream_seekfn(void *cookie, fpos_t pos, int whence)
{
	struct memstream_cookie *c;
	
	c = cookie;

	/* XXX: Should validate SEEK_SET and SEEK_CUR positions */
	/* XXX: What to do wrt SEEK_END? Is it relative to tail? to pos? */

	switch(whence) {
	case (SEEK_SET):
		c->pos = pos;
		return (c->pos);
		break;
	case (SEEK_CUR):
		c->pos += pos;
		return (c->pos);
		break;
	case (SEEK_END):
	default:
		errno = EINVAL;
		return (fpos_t) -1;
	}
}

static int
memstream_closefn(void *cookie)
{
	struct memstream_cookie *c;

	c = cookie;

	sync_pub_cookie(c);

	free(c);
	return (0);
}

FILE *
open_memstream(char **bufp, size_t *sizep)
{
	FILE *fp;
	struct memstream_cookie *c;

	fp = NULL;
	if (bufp == NULL || sizep == NULL) {
		errno = EINVAL;
		goto out;
	}

	c = malloc(sizeof(struct memstream_cookie));
	if (c == NULL) {
		errno = EINVAL;
		goto out;
	}

	fp = funopen(c,
		     NULL,
		     memstream_writefn,
		     memstream_seekfn,
		     memstream_closefn
		    );

	if (fp == NULL) {
		free(c);
		errno = ENOMEM;
		goto out;
	}

	c->pub_buf = bufp;
	c->pub_size = sizep;
	c->head = NULL;
	c->tail = 0;
	c->pos = 0;

out:
	return (fp);
}
