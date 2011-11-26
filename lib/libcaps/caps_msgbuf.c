/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/lib/libcaps/caps_msgbuf.c,v 1.1 2004/03/07 23:36:44 dillon Exp $
 */

#include "defs.h"

void
caps_init_msgbuf(caps_msgbuf_t msg, void *data, int size)
{
    msg->base = data;
    msg->index = 0;
    msg->bufsize = size;
    msg->error = 0;
}

void
caps_msgbuf_error(caps_msgbuf_t msg, int eno, int undo)
{
    if (eno)
	msg->error = eno;
    if (msg->index < undo)
	msg->index = 0;
    else
	msg->index -= undo;
}

/*
 * Extract the next class specification.  A class specification is an upper
 * case letter followed by quoted data or non-quoted data.  Non-quoted data
 * may only contain the characters 0-9 and LOWER CASE a-z, '.', or '_'.
 * The returned (ptr,len) tuple includes the quotes in quoted data.
 */
u_int8_t
caps_msgbuf_getclass(caps_msgbuf_t msg, u_int8_t **pptr, int *plen)
{
    u_int8_t c;
    u_int8_t cc;
    int i;

    while (msg->index < msg->bufsize) {
	c = msg->base[msg->index++];
	if (c >= 'A' && c <= 'Z') {
	    i = msg->index;
	    *pptr = msg->base + i;
	    if (i < msg->bufsize && msg->base[i] == '\"') {
		for (++i; i < msg->bufsize; ++i) {
		    cc = msg->base[i];
		    if ((cc >= 'a' && cc <= 'z') ||
			(cc >= 'A' && cc <= 'Z') ||
			(cc >= '0' && cc <= '9') ||
			cc == '_' || cc == '.' || cc == '/' || cc == '+' || 
			cc == '-' || cc == '%'
		    ) {
			continue;
		    }
		    if (cc == '"')	/* quote end string, else error */
			++i;
		    else
			msg->error = EINVAL;
		    break;
		}
	    } else {
		for (; i < msg->bufsize; ++i) {
		    cc = msg->base[i];
		    if (cc >= 'a' && cc <= 'z')
			continue;
		    if (cc >= '0' && cc <= '9')
			continue;
		    if (cc == '.' || cc == '_')
			continue;
		    break;
		}
	    }
	    *plen = i - msg->index;
	    msg->index = i;
	    return(c);
	}
	if (c == ',' || c == '{' || c == '}')
	    return(c);
	if (c != '\t' && c != ' ' && c != '\r' && c != '\n') {
	    msg->error = EINVAL;
	    break;
	}
	/* loop on whitespce */
    }
    return(0);
}

/*
 * Support routines for encoding/decoding
 */
void
caps_msgbuf_printf(caps_msgbuf_t msg, const char *ctl, ...)
{
    va_list va;
    int i;

    va_start(va, ctl);
    i = msg->index;
    if (i <= msg->bufsize)
	i += vsnprintf(msg->base + i, msg->bufsize - i, ctl, va);
    else
	i += vsnprintf(NULL, 0, ctl, va);
    msg->index = i;
    va_end(va);
}

