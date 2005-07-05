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
 * $DragonFly: src/sbin/jscan/jstream.c,v 1.3 2005/07/05 02:38:34 dillon Exp $
 */

#include "jscan.h"

static struct jhash	*JHashAry[JHASH_SIZE];

static struct jstream *jaddrecord(struct jfile *jf, struct jstream *js);
static void jnormalize(struct jstream *js);

/*
 * Locate the next (or previous) complete virtual stream transaction given a
 * file descriptor and direction.  Keep track of partial stream records as
 * a side effect.
 *
 * Note that a transaction might represent a huge I/O operation, resulting
 * in an overall node structure that spans gigabytes, but individual
 * subrecord leaf nodes are limited in size and we depend on this to simplify
 * the handling of leaf records. 
 *
 * A transaction may cover several raw records.  The jstream collection for
 * a transaction is only returned when the entire transaction has been
 * successfully scanned.  Due to the interleaving of transactions the ordering
 * of returned JS's may be different (not exactly reversed) when scanning a
 * journal backwards verses forwards.  Since parallel operations are 
 * theoretically non-conflicting, this should not present a problem.
 */
struct jstream *
jscan_stream(struct jfile *jf)
{
    struct journal_rawrecbeg head;
    struct journal_rawrecend tail;
    int recsize;
    int search;
    int error;
    struct jstream *js;

    /*
     * Get the current offset and make sure it is 16-byte aligned.  If it
     * isn't, align it and enter search mode.
     */
    if (jf->jf_pos & 15) {
	jf_warn(jf, "realigning bad offset and entering search mode");
	jalign(jf);
	search = 1;
    } else {
	search = 0;
    }

    error = 0;
    js = NULL;

    if (jf->jf_direction == JF_FORWARDS) {
	/*
	 * Scan the journal forwards.  Note that the file pointer might not
	 * be seekable.
	 */
	while ((error = jread(jf, &head, sizeof(head))) == 0) {
	    if (head.begmagic != JREC_BEGMAGIC) {
		if (search == 0)
		    jf_warn(jf, "bad beginmagic, searching for new record");
		search = 1;
		jalign(jf);
		continue;
	    }
	    recsize = (head.recsize + 15) & ~15;
	    if (recsize <= 0) {
		jf_warn(jf, "bad recordsize: %d\n", recsize);
		search = 1;
		jalign(jf);
		continue;
	    }
	    jset(jf);
	    js = malloc(offsetof(struct jstream, js_data[recsize]));
	    bzero(js, sizeof(struct jstream));
	    bcopy(&head, js->js_data, sizeof(head));
	    error = jread(jf, js->js_data + sizeof(head), recsize - sizeof(head));
	    if (error) {
		jf_warn(jf, "Incomplete stream record\n");
		jreturn(jf);
		free(js);
		js = NULL;
		break;
	    }

	    /*
	     * note: recsize is aligned (the actual record size),
	     * head.recsize is unaligned (the actual payload size).
	     */
	    js->js_size = head.recsize;
	    bcopy(js->js_data + recsize - sizeof(tail), &tail, sizeof(tail));
	    if (tail.endmagic != JREC_ENDMAGIC) {
		jf_warn(jf, "bad endmagic, searching for new record");
		search = 1;
		jreturn(jf);
		free(js);
		js = NULL;
		continue;
	    }
	    jflush(jf);
	    if ((js = jaddrecord(jf, js)) != NULL)
		break;
	}
    } else {
	/*
	 * Scan the journal backwards.  Note that jread()'s reverse-seek and
	 * read.  The data read will be forward ordered, however.
	 */
	while ((error = jread(jf, &tail, sizeof(tail))) == 0) {
	    if (tail.endmagic != JREC_ENDMAGIC) {
		if (search == 0)
		    jf_warn(jf, "bad endmagic, searching for new record");
		search = 1;
		jalign(jf);
		continue;
	    }
	    recsize = (tail.recsize + 15) & ~15;
	    if (recsize <= 0) {
		jf_warn(jf, "bad recordsize: %d\n", recsize);
		search = 1;
		jalign(jf);
		continue;
	    }
	    jset(jf);
	    js = malloc(offsetof(struct jstream, js_data[recsize]));
	    bzero(js, sizeof(struct jstream));
	    bcopy(&tail, js->js_data + recsize - sizeof(tail), sizeof(tail));
	    error = jread(jf, js->js_data, recsize - sizeof(tail));

	    if (error) {
		jf_warn(jf, "Incomplete stream record\n");
		jreturn(jf);
		free(js);
		js = NULL;
		break;
	    }
	    js->js_size = tail.recsize;
	    bcopy(js->js_data + recsize - sizeof(tail), &tail, sizeof(tail));
	    bcopy(js->js_data, &head, sizeof(head));
	    if (head.begmagic != JREC_BEGMAGIC) {
		jf_warn(jf, "bad begmagic, searching for new record");
		search = 1;
		jreturn(jf);
		free(js);
		continue;
	    }
	    jflush(jf);
	    if ((js = jaddrecord(jf, js)) != NULL)
		break;
	}
    }
    jf->jf_error = error;
    return(js);
}

/*
 * Integrate a jstream record.  Deal with the transaction begin and end flags
 * to create a forward-referenced collection of jstream records.  If we are
 * able to complete a transaction, the first js associated with that
 * transaction is returned.
 *
 * XXX we need to store the data for very large multi-record transactions
 * separately since it might not fit into memory.
 */
static struct jstream *
jaddrecord(struct jfile *jf, struct jstream *js)
{
    struct journal_rawrecbeg *head = (void *)js->js_data;
    struct jhash *jh;
    struct jhash **jhp;

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
	if (((jh->jh_transid ^ head->streamid) & JREC_STREAMID_MASK) == 0)
	    break;
	jhp = &jh->jh_hash;
    }
    if (jh == NULL) {
	jh = malloc(sizeof(*jh));
	bzero(jh, sizeof(*jh));
	*jhp = jh;
	jh->jh_first = js;
	jh->jh_last = js;
	jh->jh_transid = head->streamid;
	return (NULL);
    }

    /*
     * Emplace the stream segment
     */
    jh->jh_transid |= head->streamid & JREC_STREAMCTL_MASK;
    if (jf->jf_direction == JF_FORWARDS) {
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
    js->js_normalized_base = js->js_data;
    js->js_normalized_size = ((struct journal_rawrecbeg *)js->js_data)->recsize - sizeof(struct journal_rawrecend);
    js->js_normalized_total = js->js_normalized_size;
    off = js->js_normalized_size;
    for (jscan = js->js_next; jscan; jscan = jscan->js_next) {
	jscan->js_normalized_off = off;
	jscan->js_normalized_base = jscan->js_data + 
		sizeof(struct journal_rawrecbeg);
	jscan->js_normalized_size = jscan->js_size -
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

