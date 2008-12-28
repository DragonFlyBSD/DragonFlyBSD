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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/sys/kern/uipc_sockbuf.c,v 1.3 2007/08/09 01:10:04 dillon Exp $
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
 * Cupholder memory
 */
MALLOC_DEFINE(M_CUPHOLDER, "cupholder", "sockbuf's ring structure");
struct objcache *cupholder_oc;

static void
cupholderinit(void *data __unused)
{
	int limit;

	limit = nmbufs;
	cupholder_oc = objcache_create_mbacked(
				M_CUPHOLDER, sizeof(struct cupholder),
				&limit, 64, NULL, NULL, NULL);
}
SYSINIT(cupholder, SI_BOOT2_MACHDEP, SI_ORDER_SECOND, cupholderinit, NULL);

/*
 * Initialize a sockbuf
 */
void
sb_init(struct sockbuf *sb)
{
	bzero(sb, sizeof(*sb));
	/* sb->note is initially NULL */
	sb->head = sb->tail = sb_allocate_ch(sb);
}

/*
 * Uninitialize a sockbuf.  The sockbuf must be empty.
 */
void
sb_uninit(struct sockbuf *sb)
{
	struct cupholder *ch;
	struct mbuf *m;

	/*
	 * Clean out any cupholders.  If we have any, they should be empty.
	 */
	while ((ch = sb->head) != NULL) {
		if (ch == sb->tail)
			sb->tail = NULL;
		if (sb->note == ch)
			sb->note = NULL;
		sb->head = ch->next;
		m = ch->m;
		sb_dispose_ch(sb, ch);
		KKASSERT(m == NULL);
	}

#if 0
	/*
	 * Clean out any cache of free cupholders
	 */
	while ((ch = sb->free) != NULL) {
		sb->free = ch->next;
		objcache_put(cupholder_oc, ch);
	}
#endif
	bzero(sb, sizeof(*sb));
}

/*
 * Append a new cupholder and mbuf.  If the existing tail cupholder is empty
 * we can just reuse it.  The mbuf may be chained via m_next but represents
 * a record-bounded entity only if M_EOR is set in the first mbuf.
 *
 * The caller may be breaking up records separated by m_nextpkt.  Once
 * in a sockbuf m_nextpkt must be NULL.  Set m_nextpkt to NULL here.
 */
static inline
int
_sb_enq(struct sockbuf *sb, struct mbuf *m)
{
	struct cupholder *ch;
	int len;

	KKASSERT((m->m_flags & M_SOCKBUF) == 0);
	m_set_flagsm(m, M_SOCKBUF);
	len = m_lengthm(m, NULL);
	m->m_nextpkt = NULL;	/* obsolete? */

	/*
	 * Note race in new-ch case.  The linkage is created before the
	 * tail is moved.  Consumers should consume based on sb_cc_est(),
	 * not based on ch->next, though I think the worst that happens
	 * is you end up with an empty cupholder in the list, which we
	 * allow.
	 *
	 * When optimizing re-use of the last cupholder we have to determine
	 * if the other side temporarily chained some mbufs.  Checking whether
	 * the sockbuf has a 0 count is sufficient.
	 */
	if (sb->tail->m == NULL && sb->rbytes == sb->wbytes) {
		sb->tail->m = m;
	} else {
		ch = sb_allocate_ch(sb);
		ch->m = m;
		sb->tail->next = ch;
		sb->tail = ch;
	}

	/*
	 * Currently the code checks for the existence of
	 * data by testing the byte count, so make sure
	 * that before we update the byte counters, the
	 * data has already been added.  Changing the
	 * network code to test for mbuf availability
	 * should be doable but is not straightforward.
	 */
	_sb_produce(sb, len, 1);

	return 0;
}

/*
 * Remove the head and return the mbuf.  The head must exist.
 *
 * The returned mbuf may be chained via m_next but represents a whole
 * record only if M_EOR is set in the first mbuf.
 */
static inline
struct mbuf *
_sb_deq(struct sockbuf *sb)
{
        struct cupholder *ch;
	struct mbuf *m;
	int len;

	/*
	 * Locate the head, dispose of any empty cupholders along
	 * the way.  The tail cupholder is never disposed of.
	 *
	 * If note is pointing at the current head we must set it to NULL,
	 * which will cause sb_next_note() to return the new head.
	 */
        for (;;) {
                ch = sb->head;
                if (ch->m || ch->next == NULL)
			break;
		cpu_lfence();		/* SMP race against tail append */
		if (ch->m)
			break;
		if (sb->note == ch)
			sb->note = NULL;
                sb->head = ch->next;
                sb_dispose_ch(sb, ch);
        }

	/*
	 * We must have found a mbuf
	 */
	m = ch->m;
	KKASSERT(m != NULL);
	KKASSERT((m->m_flags & M_SOCKBUF) != 0);
	len = m_lengthm(m, NULL);
	_sb_consume(sb, len, 1);

	/*
	 * We can dispose of ch if it is not the tail.  Note the SMP race
	 * here.  Once we set ch->m to NULL the producer can instantly
	 * load it with a new mbuf and possibly also chain another mbuf,
	 * so we can't clear the mbuf until after we've checked the tail.
	 */
	if (sb->note == ch)
		sb->note = NULL;
	if (ch->next) {
		cpu_ccfence();
		ch->m = NULL;
		sb->head = ch->next;
		sb_dispose_ch(sb, ch);
	} else {
		cpu_ccfence();
		ch->m = NULL;
	}
	m_clear_flagsm(m, M_SOCKBUF);
	return (m);
}

/*
 * This is for code that knows that all accesses to this
 * sockbuf are synchronised, so that we can give a stable
 * character count
 *
 * XXX: actually this is just a marker for code that hasn't
 * been audited yet.  Convert calls to sb_cc_est() when they
 * get fixed.
 */
int
sb_cc_mplocked(struct sockbuf *sb)
{
	ASSERT_MP_LOCK_HELD(curthread);
	return (sb->wbytes - sb->rbytes);
}

/*
 * Break apart a chain of mbufs delineated by m_next and insert them
 * individually into a sockbuf.  This is used only by stream protocols.
 */
static void
sb_insert_chain(struct sockbuf *sb, struct mbuf *m)
{
	struct mbuf *o, *free_chain;

	free_chain = NULL;
	for (; m; m = o) {
		KKASSERT((m->m_flags & M_SOCKBUF) == 0);
		if (m->m_len == 0) {
			/* XXX: can we even get here? */
			o = m->m_next;
			m->m_next = free_chain;
			free_chain = m;
			continue;
		}
		/*
		 * we expect that M_EOR can only be set on the last
		 * mbuf in a chain
		 */
		if (m->m_flags & M_EOR) {
			KKASSERT(m->m_next == NULL);
		}
		/*
		 * XXX: I don't think we want to coalesce with previous
		 * mbuf like in the original -- agg
		 */
		o = m->m_next;
		m->m_next = NULL;
		_sb_enq(sb, m);
	}
        if (free_chain) {
		m_freem(free_chain);
	}
}

/*
 * Append a chain of mbufs delineated by m_next to a sockbuf representing
 * a stream.  The mbufs must represent data only and may not contain any
 * control or address prefixes, and probably should not have M_EOR set
 * either.  Each mbuf is treated as an individual entity.
 */
void
sb_append_stream(struct sockbuf *sb, struct mbuf *m)
{
	KKASSERT(m->m_nextpkt == NULL);
	sb_insert_chain(sb, m);
}

/*
 * Make a chain out of the stream protocol mbufs (i.e. ->m_next == NULL).
 * The chain is stored in a single cupholder at the head and also returned.
 *
 * sb_chain_inplace() - leaves the chain intact in the sockbuf
 * sb_chain_remove() - removes the mbuf chain from the sockbuf
 *
 * These are hopefully temporary routines to collapse several
 * packets worth of mbufs into a single chain in the first cupholder,
 * leaving other cupholders empty.
 *
 * Chains created in-place should be unchained as soon as possible
 * (restoring the empty cupholders).  sb_drop_head() may break
 * in non-obvious ways otherwise.
 */
struct mbuf *
sb_chain_inplace(struct sockbuf *sb, int len)
{
	struct cupholder *ch;
	struct mbuf *m;		/* scan mbuf */
	struct mbuf *p;		/* link mbuf */
	struct mbuf *f;		/* head mbuf - to be returned */

	KKASSERT(len > 0);

	/*
	 * Extract the head mbuf
	 */
	f = NULL;
	p = NULL;
	ch = sb->head;

	/*
	 * Must loop based on len to avoid SMP race on tail.
	 */
	while (len > 0) {
		KKASSERT(ch != NULL);
		m = ch->m;
		if (m) {
			KKASSERT(m->m_next == NULL); /* stream proto only */
			/* XXX embedded controls */
			KKASSERT(m->m_len <= len);
			len -= m->m_len;
			if (f == NULL) {
				f = p = m;
				/* leave ch->m pointing at f */
			} else {
				p->m_next = m;
				p = m;
				ch->m = NULL;
			}
		}
		ch = ch->next;
	}
	KKASSERT(len == 0);
	return (f);
}

/*
 * sb_chain_remove works like sb_chain_inplace but physically removes
 * the mbufs and cleans up the cupholders.  You cannot unchain a removed
 * chain.
 */
struct mbuf *
sb_chain_remove(struct sockbuf *sb, int len)
{
	struct cupholder *ch;
	struct mbuf *m;		/* scan mbuf */
	struct mbuf *p;		/* link mbuf */
	struct mbuf *f;		/* head mbuf - to be returned */
	int istail;

	KKASSERT(len > 0);

	/*
	 * Extract the head mbuf
	 */
	f = NULL;
	p = NULL;

	while (len > 0) {
		ch = sb->head;
		KKASSERT(ch != NULL);

		/*
		 * Must check tail status before potentially NULLing out
		 * ch->m to avoid SMP race with producer re-using ch->m.
		 */
		istail = (ch->next == NULL);
		cpu_ccfence();
		m = ch->m;
		if (m) {
			KKASSERT(m->m_next == NULL); /* stream proto only */
			KKASSERT(m->m_len <= len);
			len -= m->m_len;
			if (f == NULL) {
				f = p = m;
			} else {
				p->m_next = m;
				p = m;
			}
			ch->m = NULL;
			m_clear_flagsm(m, M_SOCKBUF);
		}

		/*
		 * Remove the head as we build the chain.  The last cupholder
		 * is not removed (but it's ch->m can still be NULL'd out).
		 */
		if (sb->note == ch)
			sb->note = NULL;
		if (istail)
			break;
		sb->head = ch->next;
		sb_dispose_ch(sb, ch);
	}
	KKASSERT(len == 0);
	return (f);
}

/*
 * Restore a chain created by sb_chain_inplace().  We expect there to be
 * a sufficient number of empty cupholders to restore the sequence (since
 * they were what we constructed the chain out of in the first place).
 */
void
sb_unchain(struct sockbuf *sb, struct mbuf *f)
{
	struct cupholder *ch;
	struct mbuf *m;
	struct mbuf *n;

	for (ch = sb->head; ch; ch = ch->next) {
		if (ch->m == f)
			break;
	}
	KKASSERT(ch);
	m = ch->m;
	while ((n = m->m_next) != NULL) {
		ch = ch->next;
		KKASSERT(ch != NULL);
		KKASSERT(ch->m == NULL);
		KKASSERT(n->m_flags & M_SOCKBUF);
		m->m_next = NULL;
		ch->m = n;
		m = n;
	}
}

/*
 * Enqueue one or more packets as discrete records.  Packets are delineated
 * by m_nextpkt.  Each packet may contain a chain of mbufs delineated by
 * m_next.  M_EOR is set on the mbuf heading up each packet.
 */
void
sb_append_record(struct sockbuf *sb, struct mbuf *m)
{
	struct mbuf *n;

	for (; m; m = n) {
		n = m->m_nextpkt;
		m->m_flags |= M_EOR;
		_sb_enq(sb, m);
	}
}

/*
 * Generic mbuf append.  The mbuf may represent a list of discrete records
 * separated by m_nextpkt, with multiple mbufs making up each record
 * delineated by m_next, or the mbuf may represent a simple chain of mbufs.
 *
 * When representing a simple chain of mbufs m_nextpkt must be NULL.
 *
 * M_EOR is used to determine which method we use.
 */
void
sb_append(struct sockbuf *sb, struct mbuf *m)
{
	if (m->m_flags & M_EOR) {
		sb_append_record(sb, m);
	} else {
		KKASSERT(m->m_nextpkt == NULL);
		sb_append_stream(sb, m);
	}
}

/*
 * Append with address.  Return zero if we could not allocate a mbuf header,
 * return non-zero on success.
 *
 * Mbufs with address or control prefixes are always appended as records.
 */
int
sb_append_addr(struct sockbuf *sb, const struct sockaddr *asa, struct mbuf *m,
	     struct mbuf *ctrl)
{
	struct mbuf *addrmb, *n;

	if (!m) {
		KKASSERT(m->m_flags & M_PKTHDR);
	}
	/* make n point to last control mbuf */
	for (n = ctrl; n; n = n->m_next)
		;
	if (asa->sa_len > MLEN)
		return (0);
	MGET(addrmb, MB_DONTWAIT, MT_SONAME);
	if (addrmb == NULL)
		return (0);
	addrmb->m_len = asa->sa_len;
	addrmb->m_flags |= M_EOR;
	bcopy(asa, mtod(addrmb, caddr_t), asa->sa_len);

	/*
	 * ok, now we need to build a chain where
	 * addr -> ctrl -> data
	 */
	if (n) {
		n->m_next = m;
	} else {	/* don't have ctrl */
		ctrl = m;
	}
	addrmb->m_next = ctrl;
	_sb_enq(sb, addrmb);

	return 1;
}

/*
 * Append a mbuf with a control.
 *
 * Mbufs with address or control prefixes are always appended as records.
 */
void
sb_append_control(struct sockbuf *sb, struct mbuf *m, struct mbuf *ctrl)
{
	struct mbuf *n;
	u_int ccnt;

	KKASSERT(ctrl != NULL);
	KKASSERT(ctrl->m_nextpkt == NULL);

	KKASSERT(m != NULL);	/* XXX: do we care? -- agg */

	m_countm(ctrl, &n, &ccnt);
	ctrl->m_flags |= M_EOR;
	n->m_next = m;
	_sb_enq(sb, ctrl);
}

/*
 * Drop first mbuf of first mbuf chain, return next mbuf.
 *
 * When dropping the first mbuf of a m_next chain the next mbuf in
 * the chain will inherit the M_EOR status of the chain.
 *
 * If not NULL *ceor will be set to 0 or 1, indicating that the mbuf being
 * dropped completes a record.  *ceor is only set when removing the last
 * mbuf in a chain if that mbuf has M_EOR or the returned mbuf has M_EOR.
 */
struct mbuf *
sb_drop_head(struct sockbuf *sb, int *ceor, struct mbuf **free_chain)
{
	struct mbuf *m, *n;
	int eor;

	/*
	 * We do not want to race the producer while it is appending
	 * a new cupholder.
	 */
	if (sb_isempty(sb)) {
		if (ceor)
			*ceor = 0;
		return(NULL);
	}

	m = sb_head(sb);
	n = m->m_next;
	if (n) {
		/*
		 * More mbufs in packet.
		 */
		_sb_consume(sb, m->m_len, 0);
		KKASSERT(sb->head->m == m);
		sb->head->m = n;
		n->m_flags |= m->m_flags & (M_SOCKBUF|M_EOR);
		m->m_flags &= ~M_SOCKBUF;
		eor = 0;
	} else {
		/*
		 * Last mbuf in packet.
		 */
		_sb_deq(sb);
		n = sb_head(sb);
		if (m->m_flags & M_EOR)
			eor = 1;
		else if (n && (n->m_flags & M_EOR))
			eor = 1;
		else
			eor = 0;
	}
	if (ceor)
		*ceor = eor;

	/*
	 * If free_chain is NULL the caller already has a handle on this
	 * mbuf from a prior sb_head() and we need only unlink m_next.
	 */
	if (free_chain) {
		m->m_next = *free_chain;
		*free_chain = m;
	} else {
		m->m_next = NULL;
	}
	return n;
}

/*
 * Drop len bytes starting from the beginning
 */
void
sb_drop(struct sockbuf *sb, int len)
{
	struct mbuf *m;
	struct mbuf *free_chain = NULL;

	KKASSERT(len >= 0);
	m = sb_head(sb);
	while (m && (len > 0)) {
		if (m->m_len > len) {
			m->m_len -= len;
			m->m_data += len;
			_sb_consume(sb, len, 0);
			len = 0;
			break;
		}
		len -= m->m_len;
		m = sb_drop_head(sb, NULL, &free_chain);
#if 0
		if ((m == NULL) && (len > 0)) {
			m = sb_head(sb);
		}
#endif
	}
	KKASSERT(len == 0);

	/*
	 * Remove any trailing 0-length mbufs in the current record.  If
	 * the last record for which data was removed is now empty, m will be
	 * NULL.
	 */
	while (m && m->m_len == 0) {
		m = sb_drop_head(sb, NULL, &free_chain);
	}
	if (free_chain)
		m_freem(free_chain);
}

/*
 * Drop a record off the front of a sockbuf and move the next record
 * to the front.  This call does not apply to stream sockbufs.
 *
 * The sockbuf must contain at least one record.
 */
void
sb_drop_record(struct sockbuf *sb)
{
	struct mbuf *m;

	if ((m = _sb_deq(sb)) != NULL) {
		m_freem(m);
	}
}

/*
 * Dequeue the next record starting at the head, return the mbuf chain
 * or NULL if no records remain.
 */
struct mbuf *
sb_deq_record(struct sockbuf *sb)
{
	if (sb_mbcnt_est(sb) > 0)
		return(_sb_deq(sb));
	return(NULL);
}

/*
 * XXX: caller must make sure sb is not reachable
 * by any proto thread first -- agg
 */
void
sb_flush(struct sockbuf *sb)
{
	while (sb_head(sb) != NULL) {
		sb_drop_record(sb);
	}
}

/*
 * Take a snapshot of a sockbuf.  Races are handled as follows:
 *
 * 1) sr->max_len may have a value smaller then the contents of the sockbuf
 *
 * 2) sr->ch can race sr->m, but if this occurs sr->max_len will be 0.
 */
void
sb_reader_init(struct sb_reader *sr, struct sockbuf *sb)
{
	sr->sb = sb;
	sr->max_len = sb_cc_est(sb); /* XXX use mbcnt_est */
	cpu_lfence();
	sr->m = sb_head(sb);	/* note: sb_head() may adjust sb->head */
	sr->ch = sb->head;
	sr->len = 0;
	if (sr->m)
		sr->mflags = sr->m->m_flags & M_EOR;
	else
		sr->mflags = 0;
}

void
sb_reader_rewind(struct sb_reader *sr)
{
	KKASSERT(sr->sb != NULL);
	sr->m = sb_head(sr->sb);
	sr->ch = sr->sb->head;
	sr->len = 0;
	if (sr->m)
		sr->mflags = sr->m->m_flags & M_EOR;
	else
		sr->mflags = 0;
}

/*
 * Return the next mbuf.  Set *ceor to 0 or 1 indicating that the
 * returned packet is the last packet in a record.
 *
 * *ceor is left unmodified if NULL is returned.
 *
 * Note that the last packet in a m_next-delineated mbuf chain is not
 * necessarily the last packet in a record.
 */
struct mbuf *
sb_reader_next(struct sb_reader *sr, int *ceor)
{
	struct mbuf *ret;
	int eor;

	KKASSERT(sr->sb != NULL);

	/*
	 * We pretend there are only sr->max_len bytes in the sockbuf.
	 * This is always less then the actual number of bytes in the sockbuf
	 * so we should never run out of mbufs.
	 */
	if (sr->len >= sr->max_len)
		return(NULL);

	/*
	 * The next packet to return is already in sr->m.
	 */
	ret = sr->m;
	eor = sr->mflags & M_EOR;
	if (ret == NULL)
		return(NULL);
	sr->len += ret->m_len;

	/*
	 * Calculate the next packet.  If the first mbuf of the next
	 * packet has M_EOR set then the current mbuf being returned
	 * is also considered to be the last mbuf in a record, even if
	 * it isn't recordized.
	 */
	if (ret->m_next == NULL) {
		sr->mflags &= ~M_EOR;
		while ((sr->ch = sr->ch->next) != NULL) {
			if (sr->ch->m != NULL) {
				sr->m = sr->ch->m;
				if (sr->m->m_flags & M_EOR) {
					sr->mflags |= M_EOR;
					eor |= M_EOR;
				}
				goto done;
			}
		}
		sr->m = NULL;
	}
done:
	if (ceor)
		*ceor = eor;
	return(ret);
}

/*
 * XXX: duplicates m_copym()
 */
struct mbuf *
sb_reader_copym(struct sb_reader *sr, int off0, int len)
{
	struct mbuf *m, *n, **np;
	int off = off0;
	struct mbuf *top;
	int copyhdr = 0;

	KKASSERT(sr->sb != NULL);
	m = sb_reader_next(sr, NULL);
	KASSERT(off >= 0, ("negative off %d", off));
	KASSERT(len >= 0, ("negative len %d", len));
	if (off == 0 && m->m_flags & M_PKTHDR)
		copyhdr = 1;
	while (off > 0) {
		KASSERT(m != NULL, ("m_copym, offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = sb_reader_next(sr, NULL);
	}
	np = &top;
	top = 0;
	while (len > 0) {
		if (m == NULL) {
			KASSERT(len == M_COPYALL,
			    ("m_copym, length > size of mbuf chain"));
			break;
		}
		/*
		 * Because we are sharing any cluster attachment below,
		 * be sure to get an mbuf that does not have a cluster
		 * associated with it.
		 */
		if (copyhdr)
			n = m_gethdr(MB_DONTWAIT, m->m_type);
		else
			n = m_get(MB_DONTWAIT, m->m_type);
		*np = n;
		if (n == NULL)
			goto nospace;
		if (copyhdr) {
			if (!m_dup_pkthdr(n, m, MB_DONTWAIT))
				goto nospace;
			if (len == M_COPYALL)
				n->m_pkthdr.len -= off0;
			else
				n->m_pkthdr.len = len;
			copyhdr = 0;
		}
		n->m_len = min(len, m->m_len - off);
		if (m->m_flags & M_EXT) {
			KKASSERT((n->m_flags & M_EXT) == 0);
			n->m_data = m->m_data + off;
			m->m_ext.ext_ref(m->m_ext.ext_arg);
			n->m_ext = m->m_ext;
			n->m_flags |= m->m_flags & (M_EXT | M_EXT_CLUSTER);
		} else {
			bcopy(mtod(m, caddr_t)+off, mtod(n, caddr_t),
			    (unsigned)n->m_len);
		}
		if (len != M_COPYALL)
			len -= n->m_len;
		off = 0;
		m = sb_reader_next(sr, NULL);
		np = &n->m_next;
	}
	return (top);
nospace:
	m_freem(top);
	return (NULL);
}

/*
 * XXX: This is basically m_copydata()
 */
void
sb_reader_copy_to_buf(struct sb_reader *sr, int off, int len, caddr_t cp)
{
	struct mbuf *m;
	unsigned count;

	KKASSERT(sr->sb != NULL);
	KASSERT(off >= 0, ("negative off %d", off));
	KASSERT(len >= 0, ("negative len %d", len));
	m = sb_reader_next(sr, NULL);
	while (off > 0) {
		KASSERT(m != NULL, ("offset > size of mbuf chain"));
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = sb_reader_next(sr, NULL);
	}
	while (len > 0) {
		KASSERT(m != NULL, ("length > size of mbuf chain"));
		count = min(m->m_len - off, len);
		bcopy(mtod(m, caddr_t) + off, cp, count);
		len -= count;
		cp += count;
		off = 0;
		m = sb_reader_next(sr, NULL);
	}
}

void
sb_reader_copy(struct sb_reader *srs, struct sb_reader *srd)
{
	KKASSERT(srs->sb != NULL);
	*srd = *srs;
}

void
sb_reader_done(struct sb_reader *sr)
{
	KKASSERT(sr->sb != NULL);
	sr->sb = NULL;
}

/*
 * For debugging
 */
void
sb_dump(struct sockbuf *sb)
{
	struct cupholder *ch;
	struct mbuf *m;
	int lim = 12;

	kprintf("sb %p {\n", sb);
	kprintf("rb = %d, wb=%d\n", sb->rbytes, sb->wbytes);

	for (ch = sb->head; ch; ch = ch->next) {
		kprintf(" %p(", ch);
		for (m = ch->m; m; m = m->m_next) {
			if (ch->m != m)
				kprintf(", ");
			kprintf("%p:%d ", m, m->m_len);
			if (--lim == 0) {
				kprintf("...");
				break;
			}
		}
		if (--lim <= 0) {
			if (lim == 0)
				kprintf("...");
			break;
		}
		kprintf(")");
	}
	kprintf("}\n");
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
	m = m_getl(CMSG_SPACE((u_int)size), MB_DONTWAIT, MT_CONTROL, 0, NULL);
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

