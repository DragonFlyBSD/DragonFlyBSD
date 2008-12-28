/*-
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2008 Jeffrey Hsu
 * Copyright (c) 2008 Matthew Dillon <dillon@backplane.com>
 * Copyright (c) 2008 Aggelos Economopoulos <aoiko@cc.ece.ntua.gr>
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
 * @(#)socketvar.h	8.3 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/socketvar.h,v 1.46.2.10 2003/08/24 08:24:39 hsu Exp $
 * $DragonFly: src/sys/sys/sockbuf.h,v 1.1 2007/04/22 01:13:17 dillon Exp $
 */

#ifndef _SYS_SOCKBUF_H_
#define _SYS_SOCKBUF_H_

#ifndef _SYS_OBJCACHE_H_
#include <sys/objcache.h>
#endif

#if defined(_KERNEL)
#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>	/* KKASSERT */
#endif
#endif

#include <sys/types.h>

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Receptacle for a packet, used as part of the lockless mbuf
 * chaining algorithm.
 */
struct cupholder {
	struct cupholder *next;
	struct mbuf *m;
};

typedef struct cupholder *cupholder_t;

/*
 * This is a lockless socket buffer for keeping track of mbuf chains in
 * sockets, implementing a single-consumer/single-producer MP-safe buffer.
 * It is a coupled with a lock on the user side only to interlock multiple
 * thread access.  The kernel side is 100% lockless.  The structure is
 * loosely laid out to place the consumer and producer on different cache
 * lines.
 *
 * The producer and consumer modify distinct fields.  Each packet is
 * stored in a cupholder and there is always at least one cupholder
 * in the sockbuf.  The cupholder may be empty (ch->m == NULL).  Due to
 * the lockless nature of its operation it is possible for any cupholder in
 * the list to be empty, and the consumer can simply remove such except
 * for the one terminating the list.
 *
 * The number of bytes available in the sockbuf is (int)(wbytes - rbytes).
 * Note that the fields can roll-over.  The producer updates wbytes after
 * adding a new cupholder and the consumer updates rbytes after removing
 * one (or just its associated mbuf if the last cupholder).
 *
 * The consumer may optionally track newly queued packets using the 'note'
 * (for notify) pointer, differentiating between packets already processed
 * but still being held in the sockbuf and newly queued packets that may
 * need fresh attention.  The note field points to the last cupholder
 * returned by sb_next_note(), or NULL to indicate that the head of the
 * sockbuf is the next note.  It is automatically managed by the SB calls.
 * sb->note is initially NULL and will remain so if the consumer does not
 * use the mechanic, causing the cpu branch cache to always succeed and
 * cost 0 cpu cycles to execute.
 */
struct sockbuf {
	struct cupholder *head;	/* consumer - never NULL */
	struct cupholder *note;	/* consumer - track new appends. Can be NULL */
	int	rbytes;		/* consumer - field can roll-over */
	int	rmbufs;		/* consumer - field can roll-over */
#ifdef __amd64__
	int	dummy[2];	/* cache line alignment */
#endif
	struct cupholder *tail;	/* producer - never NULL */
	int	wbytes;		/* producer - field can roll-over */
	int	wmbufs;		/* producer - field can roll-over */
};

#if !defined(_KERNEL)

/* For userspace, may be implemented differently in the future */
static __inline
int
sb_cc_est(struct sockbuf *sb)
{
	int len;

	len = (int)(sb->wbytes - sb->rbytes);
	return (len);
}

#else	/* _KERNEL */

#define SB_MAX	(256*1024)

/*
 * Consume the specified number of bytes, only modifies the byte
 * count, not the mbuf chains.  Typically run prior to the actual
 * cupholder being processed.
 */
static __inline
void
_sb_consume(struct sockbuf *sb, int c, int mbs)
{
#ifdef INVARIANTS
	int len;
#endif
	KKASSERT(c >= 0);

	sb->rbytes += c;	/* may wrap */
	sb->rmbufs += mbs;	/* may wrap */
#ifdef INVARIANTS
	len = sb->wbytes - sb->rbytes;	/* workaround gcc-4 bug */
	KKASSERT(len >= 0);
#endif
}

/*
 * Produce the specified number of bytes, only modifies the byte
 * count, not the mbuf chains.  Must be run after the new cupholder
 * has been added.
 */
static __inline
void
_sb_produce(struct sockbuf *sb, int c, int mbs)
{
#ifdef INVARIANTS
	int len;
#endif
	KKASSERT(c >= 0);

	sb->wbytes += c;	/* may wrap */
	sb->wmbufs += mbs;	/* may wrap */
#ifdef INVARIANTS
	len = sb->wbytes - sb->rbytes;	/* workaround gcc-4 bug */
	KKASSERT(len >= 0);
#endif
}

/*
 * Returns true if the socket buffer is not empty.
 */
static __inline
int
sb_notempty(struct sockbuf *sb)
{
	return(sb->rbytes != sb->wbytes);
}

/*
 * Returns true if the socket buffer is empty.
 */
static __inline
int
sb_isempty(struct sockbuf *sb)
{
	return(sb->rbytes == sb->wbytes);
}

/*
 * Estimate the number of bytes available in the sockbuf.  When
 * called by the consumer the returned value is a lower bound.  When
 * called by the producer the returned value is an upper bound.
 */
static __inline
int
sb_cc_est(struct sockbuf *sb)
{
	int len;

	len = (int)(sb->wbytes - sb->rbytes);
	KKASSERT(len >= 0);
	return (len);
}

static __inline
int
sb_mbcnt_est(struct sockbuf *sb)
{
	int count;

	count = (int)(sb->wmbufs - sb->rmbufs);
	KKASSERT(count >= 0);
	return (count);
}

extern struct objcache *cupholder_oc;

/*
 * Allocate a cupholder.
 */
static __inline
struct cupholder *
sb_allocate_ch(struct sockbuf *sb)
{
	struct cupholder *ch;

	ch = objcache_get(cupholder_oc, M_WAITOK);
	ch->next = NULL;
	ch->m = NULL;
	return(ch);
}

/*
 * Dispose of a cupholder.
 */
static __inline
void
sb_dispose_ch(struct sockbuf *sb, struct cupholder *ch)
{
	KKASSERT(ch->m == NULL);
	objcache_put(cupholder_oc, ch);
}

/*
 * Return the next available mbuf or NULL.  This function cleans
 * out any empty cupholders encountered (if not the last).
 */
static __inline
struct mbuf *
sb_head(struct sockbuf *sb)
{
	struct cupholder *ch;
	struct mbuf *m;

	/*
	 * NOTE: SMP race.  If ch->next is NULL it is possible to race
	 * another cpu populating ch->m (ch->m going from NULL to non-NULL).
	 *
	 * If ch->next is not NULL and ch->m is NULL we need a load fence
	 * to recheck m as its read may have been reordered.
	 */
	for (;;) {
		ch = sb->head;
		m = ch->m;
		if (m || ch->next == NULL)
			return(m);
		cpu_lfence();
		if (ch->m)		/* must recheck m */
			return(ch->m);
		if (sb->note == ch)
			sb->note = NULL;
		sb->head = ch->next;
		sb_dispose_ch(sb, ch);
	}
	/* NOT REACHED */
}

/*
 * The note mechanism allows consumers (mainly protocol stacks) to
 * differentiate between mbufs left in the sockbuf from a prior operation
 * and mbufs newly appended by the producer.
 *
 * The note pointer is a bit weird.  If NULL then the next cupholder to note
 * is the head.  If not NULL it points to the previously returned cupholder
 * and we advance to the next one.
 *
 * A pointer to the sockbuf's mbuf pointer is returned.  NULL is returned
 * when the list is exhausted.  If NULL is not returned then *returned_mpp
 * will be non-NULL.  The caller is NOT alllowed to completely dispose
 * of the returned mbuf chain.  It is allowed to weed out control and
 * addr messages, and update *returned_mpp, but may not update it to
 * NULL.
 */
static __inline
struct mbuf **
sb_next_note(struct sockbuf *sb)
{
	struct cupholder *note;

	/*
	 * Locate the next note.  If sb->note is NULL the next note is
	 * the head of the list.  If sb->head->m is NULL fall through as
	 * if we started at sb->head and need to return the note after.
	 */
	note = sb->note;
	if (note == NULL) {
		note = sb->head;	/* never NULL */
		if (note->m) {
			sb->note = note;
			return(&note->m);
		}
	}

	/*
	 * We want to return the next note.  Skip any empty placeholders.
	 */
	while (note->next) {
		note = note->next;
		cpu_lfence();
		if (note->m) {
			sb->note = note;
			return(&note->m);
		}
	}

	/*
	 * We have exhausted the list.  Save our current note (at the
	 * end of the list), so the next call will get the next note,
	 * and return NULL.
	 *
	 * If the current note represents the end of the list the next
	 * call will get the next node.  If this is the tail of the list
	 * and the mbuf is disposed of (by a dequeue), the dequeue
	 * operation will set sb->note to NULL.  Otherwise we won't have
	 * visibility to the next append (which reuses the empty tail).
	 */
	sb->note = note;
	return(NULL);
}

enum {
	SB_READER_VALID = 0x1,
};

struct sb_reader {	/* sockbuf iterator (ro) */
	struct sockbuf *sb;
	struct cupholder *ch;
	struct mbuf *m;
	int flags;
	int mflags;
	int max_len;
	int len;
};

static __inline int
sb_reader_len(struct sb_reader *sr)
{
	KKASSERT(sr->sb != NULL);
	return (sr->max_len);
}

static __inline void
sb_reader_inv(struct sb_reader *sr)
{
	sr->sb = NULL;
}

void	sb_init(struct sockbuf *);
void	sb_uninit(struct sockbuf *);
int	sb_cc_mplocked(struct sockbuf *);
void	sb_append (struct sockbuf *sb, struct mbuf *m);
int	sb_append_addr (struct sockbuf *sb, const struct sockaddr *asa,
	    struct mbuf *m0, struct mbuf *control);
void	sb_append_control (struct sockbuf *sb, struct mbuf *m0,
	    struct mbuf *control);
void	sb_append_record (struct sockbuf *sb, struct mbuf *m0);
void	sb_append_stream (struct sockbuf *sb, struct mbuf *m);
void	sb_drop (struct sockbuf *sb, int len);
void	sb_drop_record (struct sockbuf *sb);
void	sb_flush (struct sockbuf *sb);
void	sb_unchain(struct sockbuf *, struct mbuf *);
void	sb_dump(struct sockbuf *);

struct mbuf *sbcreatecontrol (caddr_t p, int size, int type, int level);
struct mbuf *sb_drop_head (struct sockbuf *, int *, struct mbuf **);
struct mbuf *sb_deq_record (struct sockbuf *);
struct mbuf *sb_chain_inplace(struct sockbuf *, int);
struct mbuf *sb_chain_remove(struct sockbuf *, int);

void	sb_reader_init(struct sb_reader *, struct sockbuf *);
void	sb_reader_rewind(struct sb_reader *);
struct mbuf *sb_reader_next(struct sb_reader *, int *);
struct mbuf *sb_reader_copym(struct sb_reader *, int, int);
void	sb_reader_copy_to_buf(struct sb_reader *, int, int, caddr_t);
void	sb_reader_copy(struct sb_reader *, struct sb_reader *);
void	sb_reader_done(struct sb_reader *);

#endif /* _KERNEL */
#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#endif /* !_SYS_SOCKBUF_H_ */
