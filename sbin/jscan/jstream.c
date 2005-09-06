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
 * $DragonFly: src/sbin/jscan/jstream.c,v 1.6 2005/09/06 18:43:52 dillon Exp $
 */

#include "jscan.h"

static struct jhash	*JHashAry[JHASH_SIZE];

static void jnormalize(struct jstream *js);

/*
 * Integrate a raw record.  Deal with the transaction begin and end flags
 * to create a forward-referenced collection of jstream records.  If we are
 * able to complete a transaction, the first js associated with that
 * transaction is returned.
 *
 * XXX we need to store the data for very large multi-record transactions
 * separately since it might not fit into memory.
 */
struct jstream *
jaddrecord(struct jsession *ss, struct jdata *jd)
{
    struct journal_rawrecbeg *head;
    struct jstream *js;
    struct jhash *jh;
    struct jhash **jhp;

    js = malloc(sizeof(struct jstream));
    bzero(js, sizeof(struct jstream));
    js->js_jdata = jref(jd);
    js->js_head = (void *)jd->jd_data;
    js->js_session = ss;
    head = js->js_head;

    /*
     * Check for a completely self-contained transaction, just return the
     * js if possible.
     */
    if ((head->streamid & (JREC_STREAMCTL_BEGIN|JREC_STREAMCTL_END)) ==
	(JREC_STREAMCTL_BEGIN|JREC_STREAMCTL_END)
    ) {
	jnormalize(js);
	return (js);
    }

    /*
     * Check for an open transaction in the hash table, create a new one
     * if necessary.
     */
    jhp = &JHashAry[head->streamid & JHASH_MASK];
    while ((jh = *jhp) != NULL) {
	if (jh->jh_session == ss &&
	   ((jh->jh_transid ^ head->streamid) & JREC_STREAMID_MASK) == 0
	) {
	    break;
	}
	jhp = &jh->jh_hash;
    }
    if (jh == NULL) {
	jh = malloc(sizeof(*jh));
	bzero(jh, sizeof(*jh));
	*jhp = jh;
	jh->jh_first = js;
	jh->jh_last = js;
	jh->jh_transid = head->streamid;
	jh->jh_session = ss;
	return (NULL);
    }

    /*
     * Emplace the stream segment
     */
    jh->jh_transid |= head->streamid & JREC_STREAMCTL_MASK;
    if (js->js_session->ss_jfin->jf_direction == JD_FORWARDS) {
	jh->jh_last->js_next = js;
	jh->jh_last = js;
    } else {
	js->js_next = jh->jh_first;
	jh->jh_first = js;
    }

    /*
     * If the transaction is complete, remove the hash entry and return the
     * js representing the beginning of the transaction.
     */
    if ((jh->jh_transid & (JREC_STREAMCTL_BEGIN|JREC_STREAMCTL_END)) ==
	(JREC_STREAMCTL_BEGIN|JREC_STREAMCTL_END)
    ) {
	*jhp = jh->jh_hash;
	js = jh->jh_first;
	free(jh);

	jnormalize(js);
    } else {
	js = NULL;
    }
    return (js);
}

/*
 * Renormalize the jscan list to remove all the meta record headers
 * and trailers except for the very first one.
 */
static
void
jnormalize(struct jstream *js)
{
    struct jstream *jscan;
    off_t off;

    js->js_normalized_off = 0;
    js->js_normalized_base = (void *)js->js_head;
    js->js_normalized_size = js->js_head->recsize - sizeof(struct journal_rawrecend);
    js->js_normalized_total = js->js_normalized_size;
    off = js->js_normalized_size;
    for (jscan = js->js_next; jscan; jscan = jscan->js_next) {
	jscan->js_normalized_off = off;
	jscan->js_normalized_base = (char *)jscan->js_head + 
		sizeof(struct journal_rawrecbeg);
	jscan->js_normalized_size = jscan->js_head->recsize -
	       sizeof(struct journal_rawrecbeg) -
	       sizeof(struct journal_rawrecend);
	off += jscan->js_normalized_size;
	js->js_normalized_total += jscan->js_normalized_size;
    }
}

void
jscan_dispose(struct jstream *js)
{
    struct jstream *jnext;

    if (js->js_alloc_buf) {
	free(js->js_alloc_buf);
	js->js_alloc_buf = NULL;
	js->js_alloc_size = 0;
    }

    while (js) {
	jnext = js->js_next;
	jfree(js->js_session->ss_jfin, js->js_jdata);
	js->js_jdata = NULL;
	free(js);
	js = jnext;
    }
}

/*
 * Read the specified block of data out of a linked set of jstream
 * structures.  Returns 0 on success or an error code on error.
 */
int
jsread(struct jstream *js, off_t off, void *buf, int bytes)
{
    const void *ptr;
    int n;

    while (bytes) {
	n = jsreadany(js, off, &ptr);
	if (n == 0)
	    return (ENOENT);
	if (n > bytes)
	    n = bytes;
	bcopy(ptr, buf, n);
	buf = (char *)buf + n;
	off += n;
	bytes -= n;
    }
    return(0);
}

/*
 * Read the specified block of data out of a linked set of jstream
 * structures.  Attempt to return a pointer into the data set but
 * allocate and copy if that is not possible.  Returns 0 on success
 * or an error code on error.
 */
int
jsreadp(struct jstream *js, off_t off, const void **bufp,
	int bytes)
{
    int error = 0;
    int n;

    n = jsreadany(js, off, bufp);
    if (n < bytes) {
	if (js->js_alloc_size < bytes) {
	    if (js->js_alloc_buf)
		free(js->js_alloc_buf);
	    js->js_alloc_buf = malloc(bytes);
	    js->js_alloc_size = bytes;
	    assert(js->js_alloc_buf != NULL);
	}
	error = jsread(js, off, js->js_alloc_buf, bytes);
	if (error) {
	    *bufp = NULL;
	} else {
	    *bufp = js->js_alloc_buf;
	}
    }
    return(error);
}

int
jsreadcallback(struct jstream *js, ssize_t (*func)(int, const void *, size_t),
		int fd, off_t off, int bytes)
{
    const void *bufp;
    int res;
    int n;
    int r;

    res = 0;
    while (bytes && (n = jsreadany(js, off, &bufp)) > 0) {
	if (n > bytes)
	    n = bytes;
	r = func(fd, bufp, n);
	if (r != n) {
	    if (res == 0)
		res = -1;
	}
	res += n;
	bytes -= n;
	off += n;
    }
    return(res);
}

/*
 * Return the largest contiguous buffer starting at the specified offset,
 * or 0.
 */
int
jsreadany(struct jstream *js, off_t off, const void **bufp)
{
    struct jstream *scan;
    int n;

    if ((scan = js->js_cache) == NULL || scan->js_normalized_off > off)
	scan = js;
    while (scan && scan->js_normalized_off <= off) {
	js->js_cache = scan;
	if (scan->js_normalized_off + scan->js_normalized_size > off) {
	    n = (int)(off - scan->js_normalized_off);
	    *bufp = scan->js_normalized_base + n;
	    return(scan->js_normalized_size - n);
	}
	scan = scan->js_next;
    }
    return(0);
}

