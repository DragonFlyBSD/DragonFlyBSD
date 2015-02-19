/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)uipc_socket2.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/uipc_socket2.c,v 1.55.2.17 2002/08/31 19:04:55 dwmalone Exp $
 */

#include "opt_param.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/domain.h>
#include <sys/file.h>	/* for maxfiles */
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

/*
 * Routines to add and remove data from an mbuf queue.
 *
 * The routines sbappend() or sbappendrecord() are normally called to
 * append new mbufs to a socket buffer.  sbappendrecord() differs from
 * sbappend() in that data supplied is treated as the beginning of a new
 * record.  sbappend() only begins a new record if the last mbuf in the
 * sockbuf is marked M_EOR.
 *
 * To place a sender's address, optional access rights, and data in a
 * socket receive buffer, sbappendaddr() or sbappendcontrol() should be
 * used.   These functions also begin a new record.
 *
 * Reliable protocols may use the socket send buffer to hold data
 * awaiting acknowledgement.  Data is normally copied from a socket
 * send buffer in a protocol with m_copy for output to a peer,
 * and then removing the data from the socket buffer with sbdrop()
 * or sbdroprecord() when the data is acknowledged by the peer.
 */

/*
 * Append mbuf chain m to the last record in the socket buffer sb.
 * The additional space associated the mbuf chain is recorded in sb.
 * Empty mbufs are discarded and mbufs are compacted where possible.
 *
 * If M_EOR is set in the first or last mbuf of the last record, the
 * mbuf chain is appended as a new record.  M_EOR is usually just set
 * in the last mbuf of the last record's mbuf chain (see sbcompress()),
 * but this may be changed in the future since there is no real need
 * to propogate the flag any more.
 */
void
sbappend(struct sockbuf *sb, struct mbuf *m)
{
	struct mbuf *n;

	mbuftrackid(m, 16);

	if (m) {
		n = sb->sb_lastrecord;
		if (n) {
			if (n->m_flags & M_EOR) {
				sbappendrecord(sb, m);
				return;
			}
		}
		n = sb->sb_lastmbuf;
		if (n) {
			if (n->m_flags & M_EOR) {
				sbappendrecord(sb, m);
				return;
			}
		}
		sbcompress(sb, m, n);
	}
}

/*
 * sbappendstream() is an optimized form of sbappend() for protocols
 * such as TCP that only have one record in the socket buffer, are
 * not PR_ATOMIC, nor allow MT_CONTROL data.  A protocol that uses
 * sbappendstream() must use sbappendstream() exclusively.
 */
void
sbappendstream(struct sockbuf *sb, struct mbuf *m)
{
	mbuftrackid(m, 17);
	KKASSERT(m->m_nextpkt == NULL);
	sbcompress(sb, m, sb->sb_lastmbuf);
}

#ifdef SOCKBUF_DEBUG

void
_sbcheck(struct sockbuf *sb)
{
	struct mbuf *m;
	struct mbuf *n = NULL;
	u_long len = 0, mbcnt = 0;

	for (m = sb->sb_mb; m; m = n) {
	    n = m->m_nextpkt;
	    if (n == NULL && sb->sb_lastrecord != m) {
		    kprintf("sockbuf %p mismatched lastrecord %p vs %p\n", sb, sb->sb_lastrecord, m);
		    panic("sbcheck1");
		
	    }
	    for (; m; m = m->m_next) {
		len += m->m_len;
		mbcnt += MSIZE;
		if (m->m_flags & M_EXT) /*XXX*/ /* pretty sure this is bogus */
			mbcnt += m->m_ext.ext_size;
		if (n == NULL && m->m_next == NULL) {
			if (sb->sb_lastmbuf != m) {
				kprintf("sockbuf %p mismatched lastmbuf %p vs %p\n", sb, sb->sb_lastmbuf, m);
				panic("sbcheck2");
			}
		}
	    }
	}
	if (sb->sb_mb == NULL) {
	    if (sb->sb_lastrecord != NULL) {
		kprintf("sockbuf %p is empty, lastrecord not NULL: %p\n",
			sb, sb->sb_lastrecord);
		panic("sbcheck3");
	    }
	    if (sb->sb_lastmbuf != NULL) {
		kprintf("sockbuf %p is empty, lastmbuf not NULL: %p\n",
			sb, sb->sb_lastmbuf);
		panic("sbcheck4");
	    }
	}
	if (len != sb->sb_cc || mbcnt != sb->sb_mbcnt) {
		kprintf("sockbuf %p cc %ld != %ld || mbcnt %ld != %ld\n",
		    sb, len, sb->sb_cc, mbcnt, sb->sb_mbcnt);
		panic("sbcheck5");
	}
}

#endif

/*
 * Same as sbappend(), except the mbuf chain begins a new record.
 */
void
sbappendrecord(struct sockbuf *sb, struct mbuf *m0)
{
	struct mbuf *firstmbuf;
	struct mbuf *secondmbuf;

	if (m0 == NULL)
		return;
	mbuftrackid(m0, 18);

	sbcheck(sb);

	/*
	 * Break the first mbuf off from the rest of the mbuf chain.
	 */
	firstmbuf = m0;
	secondmbuf = m0->m_next;
	m0->m_next = NULL;

	/*
	 * Insert the first mbuf of the m0 mbuf chain as the last record of
	 * the sockbuf.  Note this permits zero length records!  Keep the
	 * sockbuf state consistent.
	 */
	if (sb->sb_mb == NULL)
		sb->sb_mb = firstmbuf;
	else
		sb->sb_lastrecord->m_nextpkt = firstmbuf;
	sb->sb_lastrecord = firstmbuf;	/* update hint for new last record */
	sb->sb_lastmbuf = firstmbuf;	/* update hint for new last mbuf */

	/*
	 * propagate the EOR flag so sbcompress() can pick it up
	 */
	if ((firstmbuf->m_flags & M_EOR) && (secondmbuf != NULL)) {
		firstmbuf->m_flags &= ~M_EOR;
		secondmbuf->m_flags |= M_EOR;
	}

	/*
	 * The succeeding call to sbcompress() omits accounting for
	 * the first mbuf, so do it here.
	 */
	sballoc(sb, firstmbuf);

	/* Compact the rest of the mbuf chain in after the first mbuf. */
	sbcompress(sb, secondmbuf, firstmbuf);
}

/*
 * Append address and data, and optionally, control (ancillary) data
 * to the receive queue of a socket.  If present,
 * m0 must include a packet header with total length.
 * Returns 0 if insufficient mbufs.
 */
int
sbappendaddr(struct sockbuf *sb, const struct sockaddr *asa, struct mbuf *m0,
	     struct mbuf *control)
{
	struct mbuf *m, *n;
	int eor;

	mbuftrackid(m0, 19);
	mbuftrackid(control, 20);
	if (m0 && (m0->m_flags & M_PKTHDR) == 0)
		panic("sbappendaddr");
	sbcheck(sb);

	for (n = control; n; n = n->m_next) {
		if (n->m_next == NULL)	/* keep pointer to last control buf */
			break;
	}
	if (asa->sa_len > MLEN)
		return (0);
	MGET(m, M_NOWAIT, MT_SONAME);
	if (m == NULL)
		return (0);
	KKASSERT(m->m_nextpkt == NULL);
	m->m_len = asa->sa_len;
	bcopy(asa, mtod(m, caddr_t), asa->sa_len);
	if (n)
		n->m_next = m0;		/* concatenate data to control */
	else
		control = m0;
	m->m_next = control;
	for (n = m; n; n = n->m_next)
		sballoc(sb, n);

	if (sb->sb_mb == NULL)
		sb->sb_mb = m;
	else
		sb->sb_lastrecord->m_nextpkt = m;
	sb->sb_lastrecord = m;

	/*
	 * Propogate M_EOR to the last mbuf and calculate sb_lastmbuf
	 * so sbappend() can find it.
	 */
	eor = m->m_flags;
	while (m->m_next) {
		m->m_flags &= ~M_EOR;
		m = m->m_next;
		eor |= m->m_flags;
	}
	m->m_flags |= eor & M_EOR;
	sb->sb_lastmbuf = m;

	return (1);
}

/*
 * Append control information followed by data. Both the control and data
 * must be non-null.
 */
int
sbappendcontrol(struct sockbuf *sb, struct mbuf *m0, struct mbuf *control)
{
	struct mbuf *n;
	u_int length, cmbcnt, m0mbcnt;
	int eor;

	KASSERT(control != NULL, ("sbappendcontrol"));
	KKASSERT(control->m_nextpkt == NULL);
	sbcheck(sb);

	mbuftrackid(m0, 21);
	mbuftrackid(control, 22);

	length = m_countm(control, &n, &cmbcnt) + m_countm(m0, NULL, &m0mbcnt);

	KKASSERT(m0 != NULL);

	n->m_next = m0;			/* concatenate data to control */

	if (sb->sb_mb == NULL)
		sb->sb_mb = control;
	else
		sb->sb_lastrecord->m_nextpkt = control;
	sb->sb_lastrecord = control;

	/*
	 * Propogate M_EOR to the last mbuf and calculate sb_lastmbuf
	 * so sbappend() can find it.
	 */
	eor = m0->m_flags;
	while (m0->m_next) {
		m0->m_flags &= ~M_EOR;
		m0 = m0->m_next;
		eor |= m0->m_flags;
	}
	m0->m_flags |= eor & M_EOR;
	sb->sb_lastmbuf = m0;

	sb->sb_cc += length;
	sb->sb_mbcnt += cmbcnt + m0mbcnt;

	return (1);
}

/*
 * Compress mbuf chain m into the socket buffer sb following mbuf tailm.
 * If tailm is null, the buffer is presumed empty.  Also, as a side-effect,
 * increment the sockbuf counts for each mbuf in the chain.
 */
void
sbcompress(struct sockbuf *sb, struct mbuf *m, struct mbuf *tailm)
{
	int eor = 0;
	struct mbuf *free_chain = NULL;

	mbuftrackid(m, 23);

	sbcheck(sb);
	while (m) {
		struct mbuf *o;

		eor |= m->m_flags & M_EOR;
		/*
		 * Disregard empty mbufs as long as we don't encounter
		 * an end-of-record or there is a trailing mbuf of
		 * the same type to propagate the EOR flag to.
		 *
		 * Defer the m_free() call because it can block and break
		 * the atomicy of the sockbuf.
		 */
		if (m->m_len == 0 &&
		    (eor == 0 ||
		     (((o = m->m_next) || (o = tailm)) &&
		      o->m_type == m->m_type))) {
			o = m->m_next;
			m->m_next = free_chain;
			free_chain = m;
			m = o;
			continue;
		}

		/*
		 * See if we can coalesce with preceding mbuf.  Never try
		 * to coalesce a mbuf representing an end-of-record or
		 * a mbuf locked by userland for reading.
		 */
		if (tailm && !(tailm->m_flags & (M_EOR | M_SOLOCKED)) &&
		    M_WRITABLE(tailm) &&
		    m->m_len <= MCLBYTES / 4 && /* XXX: Don't copy too much */
		    m->m_len <= M_TRAILINGSPACE(tailm) &&
		    tailm->m_type == m->m_type) {
			u_long mbcnt_sz;

			bcopy(mtod(m, caddr_t),
			      mtod(tailm, caddr_t) + tailm->m_len,
			      (unsigned)m->m_len);
			tailm->m_len += m->m_len;

			sb->sb_cc += m->m_len;		/* update sb counter */

			/*
			 * Fix the wrongly updated mbcnt_prealloc
			 */
			mbcnt_sz = MSIZE;
			if (m->m_flags & M_EXT)
				mbcnt_sz += m->m_ext.ext_size;
			atomic_subtract_long(&sb->sb_mbcnt_prealloc, mbcnt_sz);

			o = m->m_next;
			m->m_next = free_chain;
			free_chain = m;
			m = o;
			continue;
		}

		/* Insert whole mbuf. */
		if (tailm == NULL) {
			KASSERT(sb->sb_mb == NULL,
				("sbcompress: sb_mb not NULL"));
			sb->sb_mb = m;		/* only mbuf in sockbuf */
			sb->sb_lastrecord = m;	/* new last record */
		} else {
			tailm->m_next = m;	/* tack m on following tailm */
		}
		sb->sb_lastmbuf = m;	/* update last mbuf hint */

		tailm = m;	/* just inserted mbuf becomes the new tail */
		m = m->m_next;		/* advance to next mbuf */
		tailm->m_next = NULL;	/* split inserted mbuf off from chain */

		/* update sb counters for just added mbuf */
		sballoc(sb, tailm);

		/* clear EOR on intermediate mbufs */
		tailm->m_flags &= ~M_EOR;
	}

	/*
	 * Propogate EOR to the last mbuf
	 */
	if (eor) {
		if (tailm)
			tailm->m_flags |= eor;
		else
			kprintf("semi-panic: sbcompress");
	}

	/*
	 * Clean up any defered frees.
	 */
	while (free_chain)
		free_chain = m_free(free_chain);

	sbcheck(sb);
}

/*
 * Free all mbufs in a sockbuf.
 * Check that all resources are reclaimed.
 */
void
sbflush(struct sockbuf *sb)
{
	while (sb->sb_mbcnt) {
		/*
		 * Don't call sbdrop(sb, 0) if the leading mbuf is non-empty:
		 * we would loop forever. Panic instead.
		 */
		if (!sb->sb_cc && (sb->sb_mb == NULL || sb->sb_mb->m_len))
			break;
		sbdrop(sb, (int)sb->sb_cc);
	}
	KASSERT(!(sb->sb_cc || sb->sb_mb || sb->sb_mbcnt || sb->sb_lastmbuf),
	    ("sbflush: cc %ld || mb %p || mbcnt %ld || lastmbuf %p",
	    sb->sb_cc, sb->sb_mb, sb->sb_mbcnt, sb->sb_lastmbuf));
}

/*
 * Drop data from (the front of) a sockbuf.  If the current record is
 * exhausted this routine will move onto the next one and continue dropping
 * data.
 */
void
sbdrop(struct sockbuf *sb, int len)
{
	struct mbuf *m;
	struct mbuf *free_chain = NULL;

	sbcheck(sb);
	crit_enter();

	m = sb->sb_mb;
	while (m && len > 0) {
		if (m->m_len > len) {
			m->m_len -= len;
			m->m_data += len;
			sb->sb_cc -= len;
			atomic_subtract_long(&sb->sb_cc_prealloc, len);
			break;
		}
		len -= m->m_len;
		m = sbunlinkmbuf(sb, m, &free_chain);
		if (m == NULL && len)
			m = sb->sb_mb;
	}

	/*
	 * Remove any trailing 0-length mbufs in the current record.  If
	 * the last record for which data was removed is now empty, m will be
	 * NULL.
	 */
	while (m && m->m_len == 0) {
		m = sbunlinkmbuf(sb, m, &free_chain);
	}
	crit_exit();
	if (free_chain)
		m_freem(free_chain);
	sbcheck(sb);
}

/*
 * Drop a record off the front of a sockbuf and move the next record
 * to the front.
 *
 * Must be called while holding a critical section.
 */
void
sbdroprecord(struct sockbuf *sb)
{
	struct mbuf *m;
	struct mbuf *n;

	sbcheck(sb);
	m = sb->sb_mb;
	if (m) {
		if ((sb->sb_mb = m->m_nextpkt) == NULL) {
			sb->sb_lastrecord = NULL;
			sb->sb_lastmbuf = NULL;
		}
		m->m_nextpkt = NULL;
		for (n = m; n; n = n->m_next)
			sbfree(sb, n);
		m_freem(m);
		sbcheck(sb);
	}
}

/*
 * Drop the first mbuf off the sockbuf and move the next mbuf to the front.
 * Currently only the head mbuf of the sockbuf may be dropped this way.
 *
 * The next mbuf in the same record as the mbuf being removed is returned
 * or NULL if the record is exhausted.  Note that other records may remain
 * in the sockbuf when NULL is returned.
 *
 * Must be called while holding a critical section.
 */
struct mbuf *
sbunlinkmbuf(struct sockbuf *sb, struct mbuf *m, struct mbuf **free_chain)
{
	struct mbuf *n;

	KKASSERT(sb->sb_mb == m);
	sbfree(sb, m);
	n = m->m_next;
	if (n) {
		sb->sb_mb = n;
		if (sb->sb_lastrecord == m)
			sb->sb_lastrecord = n;
		KKASSERT(sb->sb_lastmbuf != m);
		n->m_nextpkt = m->m_nextpkt;
	} else {
		sb->sb_mb = m->m_nextpkt;
		if (sb->sb_lastrecord == m) {
			KKASSERT(sb->sb_mb == NULL);
			sb->sb_lastrecord = NULL;
		}
		if (sb->sb_mb == NULL)
			sb->sb_lastmbuf = NULL;
	}
	m->m_nextpkt = NULL;
	if (free_chain) {
		m->m_next = *free_chain;
		*free_chain = m;
	} else {
		m->m_next = NULL;
	}
	return(n);
}

/*
 * Create a "control" mbuf containing the specified data
 * with the specified type for presentation on a socket buffer.
 */
struct mbuf *
sbcreatecontrol(caddr_t p, int size, int type, int level)
{
	struct cmsghdr *cp;
	struct mbuf *m;

	if (CMSG_SPACE((u_int)size) > MCLBYTES)
		return (NULL);
	m = m_getl(CMSG_SPACE((u_int)size), M_NOWAIT, MT_CONTROL, 0, NULL);
	if (m == NULL)
		return (NULL);
	m->m_len = CMSG_SPACE(size);
	cp = mtod(m, struct cmsghdr *);
	if (p != NULL)
		memcpy(CMSG_DATA(cp), p, size);
	cp->cmsg_len = CMSG_LEN(size);
	cp->cmsg_level = level;
	cp->cmsg_type = type;
	mbuftrackid(m, 24);
	return (m);
}

