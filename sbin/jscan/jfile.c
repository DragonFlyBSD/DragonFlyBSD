/*
 * Copyright (c) 2004,2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sbin/jscan/jfile.c,v 1.4 2005/07/05 04:08:07 dillon Exp $
 */

#include "jscan.h"

/*
 * Open a journal for directional scanning
 */
struct jfile *
jopen_stream(const char *path, enum jdirection jdir)
{
    FILE *fp;
    struct jfile *jf;

    if ((fp = fopen(path, "r")) == NULL)
	return (NULL);
    if ((jf = jopen_fp(fp, jdir)) == NULL)
	fclose (fp);
    return(jf);
}

struct jfile *
jopen_fp(FILE *fp, enum jdirection jdir)
{
    struct jfile *jf;

    jf = malloc(sizeof(struct jfile));
    bzero(jf, sizeof(struct jfile));
    jf->jf_fp = fp;
    jf->jf_direction = jdir;
    jf->jf_setpt = -1;
    if (jdir == JF_BACKWARDS) {
	fseeko(jf->jf_fp, 0L, SEEK_END);
	jf->jf_pos = ftello(jf->jf_fp);
    }
    return(jf);
}

/*
 * Close a previously opened journal, clean up any side allocations.
 */
void
jclose_stream(struct jfile *jf)
{
    struct jdata *jd;

    fclose(jf->jf_fp);
    jf->jf_fp = NULL;
    while ((jd = jf->jf_saved) != NULL) {
	jf->jf_saved = jd->jd_next;
	free(jd);
    }
    free(jf);
}

/*
 * Align us to the next 16 byte boundary.  If scanning forwards we align
 * forwards if not already aligned.  If scanning backwards we align
 * backwards if not already aligned.
 */
void
jalign(struct jfile *jf)
{
    if (jf->jf_direction == JF_FORWARDS) {
	jf->jf_pos = (jf->jf_pos + 15) & ~15;
	fseeko(jf->jf_fp, jf->jf_pos, SEEK_SET);
    } else {
	jf->jf_pos = jf->jf_pos & ~15;
    }
}

/*
 * Read data from a journal forwards or backwards.  Note that the file
 * pointer's actual seek position does not match jf_pos in the reverse
 * scan case.  Callers should never access jf_fp directly.
 */
int
jread(struct jfile *jf, void *buf, int bytes)
{
    int n;
    
    if (jf->jf_direction == JF_FORWARDS) {
	while (bytes) {
	    n = fread(buf, 1, bytes, jf->jf_fp);
	    if (n <= 0)
		break;
	    assert(n <= bytes);
	    jf->jf_pos += n;
	    buf = (char *)buf + n;
	    bytes -= n;
	}
	if (bytes == 0) {
		return (0);
	} else {
		fseeko(jf->jf_fp, jf->jf_pos, SEEK_SET);
		return (errno ? errno : ENOENT);
	}
    } else {
	if (bytes > jf->jf_pos)
		return (ENOENT);
	jf->jf_pos -= bytes;
	fseeko(jf->jf_fp, jf->jf_pos, SEEK_SET);
	if (fread(buf, bytes, 1, jf->jf_fp) == 1) {
		return (0);
	} else {
		jf->jf_pos += bytes;
		return (errno);
	}
    }
}

void
jset(struct jfile *jf)
{
    jf->jf_setpt = jf->jf_pos;
}

void
jreturn(struct jfile *jf)
{
    jf->jf_pos = jf->jf_setpt;
    jf->jf_setpt = -1;
    fseeko(jf->jf_fp, jf->jf_pos, SEEK_SET);
}

void
jflush(struct jfile *jf)
{
    jf->jf_setpt = -1;
}

