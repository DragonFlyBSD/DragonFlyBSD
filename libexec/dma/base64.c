/*
 * Copyright (c) 1995-2001 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/libexec/dma/base64.c,v 1.1 2008/02/02 18:20:51 matthias Exp $
 */

#include <stdlib.h>
#include <string.h>

int base64_encode(const void *data, int size, char **str);

static char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int
base64_encode(const void *data, int size, char **str)
{
    char *s, *p;
    int i;
    int c;
    const unsigned char *q;

    p = s = (char *) malloc(size * 4 / 3 + 4);
    if (p == NULL)
	return -1;
    q = (const unsigned char *) data;
    i = 0;
    for (i = 0; i < size;) {
	c = q[i++];
	c *= 256;
	if (i < size)
	    c += q[i];
	i++;
	c *= 256;
	if (i < size)
	    c += q[i];
	i++;
	p[0] = base64_chars[(c & 0x00fc0000) >> 18];
	p[1] = base64_chars[(c & 0x0003f000) >> 12];
	p[2] = base64_chars[(c & 0x00000fc0) >> 6];
	p[3] = base64_chars[(c & 0x0000003f) >> 0];
	if (i > size)
	    p[3] = '=';
	if (i > size + 1)
	    p[2] = '=';
	p += 4;
    }
    *p = 0;
    *str = s;
    return strlen(s);
}

