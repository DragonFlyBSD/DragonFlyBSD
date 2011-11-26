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
 * $DragonFly: src/sbin/jscan/jstream.c,v 1.8 2005/09/07 07:20:23 dillon Exp $
 */

#include "jscan.h"

static struct jhash	*JHashAry[JHASH_SIZE];

static void jnormalize(struct jstream *js);
static int jaddrecord_backtrack(struct jsession *ss, struct jdata *jd);

/*
 * Integrate a raw record.  Deal with the transaction begin and end flags
 * to create a forward-referenced collection of jstream records.  If we are
 * able to complete a transaction, the first js associated with that
 * transaction is returned.
 *
 * XXX we need to store the data for very large multi-record transactions
 * separately since it might not fit into memory.
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

retry:
    /*
     * Check for an open transaction in the hash table.
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

    /*
     * We might have picked up a transaction in the middle, which we
     * detect by not finding a hash record coupled with a raw record
     * whos JREC_STREAMCTL_BEGIN bit is not set (or JREC_STREAMCTL_END
     * bit if we are scanning backwards).
     *
     * When this case occurs we have to backtrack to locate the
     * BEGIN (END if scanning backwards) and collect those records
     * in order to obtain a complete transaction.
     *
     * This case most often occurs when a batch operation runs in 
     * prefix set whos starting raw-record transaction id is in
     * the middle of one or more meta-transactions.  It's a bit of
     * a tricky situation, but easily resolvable by scanning the
     * prefix set backwards (forwards if originally scanning backwards)
     * to locate the raw record representing the start (end) of the
     * transaction.
     */
    if (jh == NULL) {
	if (ss->ss_direction == JD_FORWARDS &&
	    (head->streamid & JREC_STREAMCTL_BEGIN) == 0
	) {
	    if (verbose_opt > 1)
		fprintf(stderr, "mid-transaction detected transid %016jx "
				"streamid %04x\n",
			(uintmax_t)jd->jd_transid,
			head->streamid & JREC_STREAMID_MASK);
	    if (jaddrecord_backtrack(ss, jd) == 0) {
		if (verbose_opt)
		    fprintf(stderr, "mid-transaction streamid %04x collection "
				    "succeeded\n",
			    head->streamid & JREC_STREAMID_MASK);
		goto retry;
	    }
	    fprintf(stderr, "mid-transaction streamid %04x collection failed\n",
		    head->streamid & JREC_STREAMID_MASK);
	    jscan_dispose(js);
	    return(NULL);
	} else if (ss->ss_direction == JD_BACKWARDS &&
	    (head->streamid & JREC_STREAMCTL_END) == 0
	) {
	    if (verbose_opt > 1)
		fprintf(stderr, "mid-transaction detected transid %016jx "
				"streamid %04x\n",
				(uintmax_t)jd->jd_transid,
				head->streamid & JREC_STREAMID_MASK);
	    if (jaddrecord_backtrack(ss, jd) == 0) {
		if (verbose_opt)
		    fprintf(stderr, "mid-transaction streamid %04x "
				    "collection succeeded\n",
			    head->streamid & JREC_STREAMID_MASK);
		goto retry;
	    }
	    fprintf(stderr, "mid-transaction streamid %04x collection failed\n",
		    head->streamid & JREC_STREAMID_MASK);
	    jscan_dispose(js);
	    return(NULL);
	}
    }

    /*
     * If we've made it to here and we still don't have a hash record
     * to track the transaction, create one.
     */
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
    if (ss->ss_direction == JD_FORWARDS) {
	jh->jh_last->js_next = js;
	jh->jh_last = js;
    } else {
	js->js_next = jh->jh_first;
	jh->jh_first = js;
    }

    /*
     * If the transaction is complete, remove the hash entry and return the
     * js representing the beginning of the transaction.  Otherwise leave
     * the hash entry intact and return NULL.
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

/*
 * For sanity's sake I will describe the normal backtracking that must occur,
 * but this routine must also operate on reverse-scanned (undo) records
 * by forward tracking.
 *
 * A record has been found that represents the middle or end of a transaction
 * when we were expecting the beginning of a transaction.  We must backtrack
 * to locate the start of the transaction, then process raw records relating
 * to the transaction until we reach our current point (jd) again.  If
 * we find a matching streamid representing the end of a transaction instead
 * of the expected start-of-transaction that record belongs to a wholely
 * different meta-transaction and the record we seek is known to not be
 * available.
 *
 * jd is the current record, directon is the normal scan direction (we have
 * to scan in the reverse direction). 
 */
static
int
jaddrecord_backtrack(struct jsession *ss, struct jdata *jd)
{
    struct jfile *jf = ss->ss_jfin;
    struct jdata *scan;
    struct jstream *js;
    u_int16_t streamid;
    u_int16_t scanid;

    assert(ss->ss_direction == JD_FORWARDS || ss->ss_direction == JD_BACKWARDS);
    if (jmodes & JMODEF_INPUT_PIPE)
	return(-1);

    streamid = ((struct journal_rawrecbeg *)jd->jd_data)->streamid & JREC_STREAMID_MASK;

    if (ss->ss_direction == JD_FORWARDS) {
	/*
	 * Backtrack in the reverse direction looking for the transaction 
	 * start bit.  If we find an end bit instead it belongs to an
	 * unrelated transaction using the same streamid and there is no
	 * point continuing.
	 */
	scan = jref(jd);
	while ((scan = jread(jf, scan, JD_BACKWARDS)) != NULL) {
	    scanid = ((struct journal_rawrecbeg *)scan->jd_data)->streamid;
	    if ((scanid & JREC_STREAMID_MASK) != streamid)
		continue;
	    if (scanid & JREC_STREAMCTL_END) {
		jfree(jf, scan);
		return(-1);
	    }
	    if (scanid & JREC_STREAMCTL_BEGIN)
		break;
	}

	/*
	 * Now jaddrecord the related records.
	 */
	while (scan != NULL && scan->jd_transid < jd->jd_transid) {
	    scanid = ((struct journal_rawrecbeg *)scan->jd_data)->streamid;
	    if ((scanid & JREC_STREAMID_MASK) == streamid) {
		js = jaddrecord(ss, scan);
		assert(js == NULL);
	    }
	    scan = jread(jf, scan, JD_FORWARDS);
	}
	if (scan == NULL)
	    return(-1);
	jfree(jf, scan);
    } else {
	/*
	 * Backtrack in the forwards direction looking for the transaction
	 * end bit.  If we find a start bit instead if belongs to an
	 * unrelated transaction using the same streamid and there is no
	 * point continuing.
	 */
	scan = jref(jd);
	while ((scan = jread(jf, scan, JD_FORWARDS)) != NULL) {
	    scanid = ((struct journal_rawrecbeg *)scan->jd_data)->streamid;
	    if ((scanid & JREC_STREAMID_MASK) != streamid)
		continue;
	    if (scanid & JREC_STREAMCTL_BEGIN) {
		jfree(jf, scan);
		return(-1);
	    }
	    if (scanid & JREC_STREAMCTL_END)
		break;
	}

	/*
	 * Now jaddrecord the related records.
	 */
	while (scan != NULL && scan->jd_transid > jd->jd_transid) {
	    scanid = ((struct journal_rawrecbeg *)scan->jd_data)->streamid;
	    if ((scanid & JREC_STREAMID_MASK) == streamid) {
		js = jaddrecord(ss, scan);
		assert(js == NULL);
	    }
	    scan = jread(jf, scan, JD_BACKWARDS);
	}
	if (scan == NULL)
	    return(-1);
	jfree(jf, scan);
    }
    return(0);
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
	    if (js->js_alloc_buf == NULL)
		fprintf(stderr, "attempt to allocate %d bytes failed\n", bytes);
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

