/*
 * Copyright (c) 2004-2006 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/kern/vfs_journal.c,v 1.33 2007/05/09 00:53:34 dillon Exp $
 */
/*
 * The journaling protocol is intended to evolve into a two-way stream
 * whereby transaction IDs can be acknowledged by the journaling target
 * when the data has been committed to hard storage.  Both implicit and
 * explicit acknowledgement schemes will be supported, depending on the
 * sophistication of the journaling stream, plus resynchronization and
 * restart when a journaling stream is interrupted.  This information will
 * also be made available to journaling-aware filesystems to allow better
 * management of their own physical storage synchronization mechanisms as
 * well as to allow such filesystems to take direct advantage of the kernel's
 * journaling layer so they don't have to roll their own.
 *
 * In addition, the worker thread will have access to much larger 
 * spooling areas then the memory buffer is able to provide by e.g. 
 * reserving swap space, in order to absorb potentially long interruptions
 * of off-site journaling streams, and to prevent 'slow' off-site linkages
 * from radically slowing down local filesystem operations.  
 *
 * Because of the non-trivial algorithms the journaling system will be
 * required to support, use of a worker thread is mandatory.  Efficiencies
 * are maintained by utilitizing the memory FIFO to batch transactions when
 * possible, reducing the number of gratuitous thread switches and taking
 * advantage of cpu caches through the use of shorter batched code paths
 * rather then trying to do everything in the context of the process
 * originating the filesystem op.  In the future the memory FIFO can be
 * made per-cpu to remove BGL or other locking requirements.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/poll.h>
#include <sys/mountctl.h>
#include <sys/journal.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/xio.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

#include <sys/file2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>
#include <sys/spinlock2.h>

static void journal_wthread(void *info);
static void journal_rthread(void *info);

static void *journal_reserve(struct journal *jo,
                        struct journal_rawrecbeg **rawpp,
                        int16_t streamid, int bytes);
static void *journal_extend(struct journal *jo,
                        struct journal_rawrecbeg **rawpp,
                        int truncbytes, int bytes, int *newstreamrecp);
static void journal_abort(struct journal *jo,
                        struct journal_rawrecbeg **rawpp);
static void journal_commit(struct journal *jo,
                        struct journal_rawrecbeg **rawpp,
                        int bytes, int closeout);
static void jrecord_data(struct jrecord *jrec,
			void *buf, int bytes, int dtype);


MALLOC_DEFINE(M_JOURNAL, "journal", "Journaling structures");
MALLOC_DEFINE(M_JFIFO, "journal-fifo", "Journal FIFO");

void
journal_create_threads(struct journal *jo)
{
	jo->flags &= ~(MC_JOURNAL_STOP_REQ | MC_JOURNAL_STOP_IMM);
	jo->flags |= MC_JOURNAL_WACTIVE;
	lwkt_create(journal_wthread, jo, NULL, &jo->wthread,
		    TDF_NOSTART, -1,
		    "journal w:%.*s", JIDMAX, jo->id);
	lwkt_setpri(&jo->wthread, TDPRI_KERN_DAEMON);
	lwkt_schedule(&jo->wthread);

	if (jo->flags & MC_JOURNAL_WANT_FULLDUPLEX) {
	    jo->flags |= MC_JOURNAL_RACTIVE;
	    lwkt_create(journal_rthread, jo, NULL, &jo->rthread,
			TDF_NOSTART, -1,
			"journal r:%.*s", JIDMAX, jo->id);
	    lwkt_setpri(&jo->rthread, TDPRI_KERN_DAEMON);
	    lwkt_schedule(&jo->rthread);
	}
}

void
journal_destroy_threads(struct journal *jo, int flags)
{
    int wcount;

    jo->flags |= MC_JOURNAL_STOP_REQ | (flags & MC_JOURNAL_STOP_IMM);
    wakeup(&jo->fifo);
    wcount = 0;
    while (jo->flags & (MC_JOURNAL_WACTIVE | MC_JOURNAL_RACTIVE)) {
	tsleep(jo, 0, "jwait", hz);
	if (++wcount % 10 == 0) {
	    kprintf("Warning: journal %s waiting for descriptors to close\n",
		jo->id);
	}
    }

    /*
     * XXX SMP - threads should move to cpu requesting the restart or
     * termination before finishing up to properly interlock.
     */
    tsleep(jo, 0, "jwait", hz);
    lwkt_free_thread(&jo->wthread);
    if (jo->flags & MC_JOURNAL_WANT_FULLDUPLEX)
	lwkt_free_thread(&jo->rthread);
}

/*
 * The per-journal worker thread is responsible for writing out the
 * journal's FIFO to the target stream.
 */
static void
journal_wthread(void *info)
{
    struct journal *jo = info;
    struct journal_rawrecbeg *rawp;
    int error;
    size_t avail;
    size_t bytes;
    size_t res;

    /* not MPSAFE yet */
    get_mplock();

    for (;;) {
	/*
	 * Calculate the number of bytes available to write.  This buffer
	 * area may contain reserved records so we can't just write it out
	 * without further checks.
	 */
	bytes = jo->fifo.windex - jo->fifo.rindex;

	/*
	 * sleep if no bytes are available or if an incomplete record is
	 * encountered (it needs to be filled in before we can write it
	 * out), and skip any pad records that we encounter.
	 */
	if (bytes == 0) {
	    if (jo->flags & MC_JOURNAL_STOP_REQ)
		break;
	    tsleep(&jo->fifo, 0, "jfifo", hz);
	    continue;
	}

	/*
	 * Sleep if we can not go any further due to hitting an incomplete
	 * record.  This case should occur rarely but may have to be better
	 * optimized XXX.
	 */
	rawp = (void *)(jo->fifo.membase + (jo->fifo.rindex & jo->fifo.mask));
	if (rawp->begmagic == JREC_INCOMPLETEMAGIC) {
	    tsleep(&jo->fifo, 0, "jpad", hz);
	    continue;
	}

	/*
	 * Skip any pad records.  We do not write out pad records if we can
	 * help it. 
	 */
	if (rawp->streamid == JREC_STREAMID_PAD) {
	    if ((jo->flags & MC_JOURNAL_WANT_FULLDUPLEX) == 0) {
		if (jo->fifo.rindex == jo->fifo.xindex) {
		    jo->fifo.xindex += (rawp->recsize + 15) & ~15;
		    jo->total_acked += (rawp->recsize + 15) & ~15;
		}
	    }
	    jo->fifo.rindex += (rawp->recsize + 15) & ~15;
	    jo->total_acked += bytes;
	    KKASSERT(jo->fifo.windex - jo->fifo.rindex >= 0);
	    continue;
	}

	/*
	 * 'bytes' is the amount of data that can potentially be written out.  
	 * Calculate 'res', the amount of data that can actually be written
	 * out.  res is bounded either by hitting the end of the physical
	 * memory buffer or by hitting an incomplete record.  Incomplete
	 * records often occur due to the way the space reservation model
	 * works.
	 */
	res = 0;
	avail = jo->fifo.size - (jo->fifo.rindex & jo->fifo.mask);
	while (res < bytes && rawp->begmagic == JREC_BEGMAGIC) {
	    res += (rawp->recsize + 15) & ~15;
	    if (res >= avail) {
		KKASSERT(res == avail);
		break;
	    }
	    rawp = (void *)((char *)rawp + ((rawp->recsize + 15) & ~15));
	}

	/*
	 * Issue the write and deal with any errors or other conditions.
	 * For now assume blocking I/O.  Since we are record-aware the
	 * code cannot yet handle partial writes.
	 *
	 * We bump rindex prior to issuing the write to avoid racing
	 * the acknowledgement coming back (which could prevent the ack
	 * from bumping xindex).  Restarts are always based on xindex so
	 * we do not try to undo the rindex if an error occurs.
	 *
	 * XXX EWOULDBLOCK/NBIO
	 * XXX notification on failure
	 * XXX permanent verses temporary failures
	 * XXX two-way acknowledgement stream in the return direction / xindex
	 */
	bytes = res;
	jo->fifo.rindex += bytes;
	error = fp_write(jo->fp, 
			jo->fifo.membase +
			 ((jo->fifo.rindex - bytes) & jo->fifo.mask),
			bytes, &res, UIO_SYSSPACE);
	if (error) {
	    kprintf("journal_thread(%s) write, error %d\n", jo->id, error);
	    /* XXX */
	} else {
	    KKASSERT(res == bytes);
	}

	/*
	 * Advance rindex.  If the journal stream is not full duplex we also
	 * advance xindex, otherwise the rjournal thread is responsible for
	 * advancing xindex.
	 */
	if ((jo->flags & MC_JOURNAL_WANT_FULLDUPLEX) == 0) {
	    jo->fifo.xindex += bytes;
	    jo->total_acked += bytes;
	}
	KKASSERT(jo->fifo.windex - jo->fifo.rindex >= 0);
	if ((jo->flags & MC_JOURNAL_WANT_FULLDUPLEX) == 0) {
	    if (jo->flags & MC_JOURNAL_WWAIT) {
		jo->flags &= ~MC_JOURNAL_WWAIT;	/* XXX hysteresis */
		wakeup(&jo->fifo.windex);
	    }
	}
    }
    fp_shutdown(jo->fp, SHUT_WR);
    jo->flags &= ~MC_JOURNAL_WACTIVE;
    wakeup(jo);
    wakeup(&jo->fifo.windex);
    rel_mplock();
}

/*
 * A second per-journal worker thread is created for two-way journaling
 * streams to deal with the return acknowledgement stream.
 */
static void
journal_rthread(void *info)
{
    struct journal_rawrecbeg *rawp;
    struct journal_ackrecord ack;
    struct journal *jo = info;
    int64_t transid;
    int error;
    size_t count;
    size_t bytes;

    transid = 0;
    error = 0;

    /* not MPSAFE yet */
    get_mplock();

    for (;;) {
	/*
	 * We have been asked to stop
	 */
	if (jo->flags & MC_JOURNAL_STOP_REQ)
		break;

	/*
	 * If we have no active transaction id, get one from the return
	 * stream.
	 */
	if (transid == 0) {
	    error = fp_read(jo->fp, &ack, sizeof(ack), &count, 
			    1, UIO_SYSSPACE);
#if 0
	    kprintf("fp_read ack error %d count %d\n", error, count);
#endif
	    if (error || count != sizeof(ack))
		break;
	    if (error) {
		kprintf("read error %d on receive stream\n", error);
		break;
	    }
	    if (ack.rbeg.begmagic != JREC_BEGMAGIC ||
		ack.rend.endmagic != JREC_ENDMAGIC
	    ) {
		kprintf("bad begmagic or endmagic on receive stream\n");
		break;
	    }
	    transid = ack.rbeg.transid;
	}

	/*
	 * Calculate the number of unacknowledged bytes.  If there are no
	 * unacknowledged bytes then unsent data was acknowledged, report,
	 * sleep a bit, and loop in that case.  This should not happen 
	 * normally.  The ack record is thrown away.
	 */
	bytes = jo->fifo.rindex - jo->fifo.xindex;

	if (bytes == 0) {
	    kprintf("warning: unsent data acknowledged transid %08llx\n",
		    (long long)transid);
	    tsleep(&jo->fifo.xindex, 0, "jrseq", hz);
	    transid = 0;
	    continue;
	}

	/*
	 * Since rindex has advanced, the record pointed to by xindex
	 * must be a valid record.
	 */
	rawp = (void *)(jo->fifo.membase + (jo->fifo.xindex & jo->fifo.mask));
	KKASSERT(rawp->begmagic == JREC_BEGMAGIC);
	KKASSERT(rawp->recsize <= bytes);

	/*
	 * The target can acknowledge several records at once.
	 */
	if (rawp->transid < transid) {
#if 1
	    kprintf("ackskip %08llx/%08llx\n",
		    (long long)rawp->transid,
		    (long long)transid);
#endif
	    jo->fifo.xindex += (rawp->recsize + 15) & ~15;
	    jo->total_acked += (rawp->recsize + 15) & ~15;
	    if (jo->flags & MC_JOURNAL_WWAIT) {
		jo->flags &= ~MC_JOURNAL_WWAIT;	/* XXX hysteresis */
		wakeup(&jo->fifo.windex);
	    }
	    continue;
	}
	if (rawp->transid == transid) {
#if 1
	    kprintf("ackskip %08llx/%08llx\n",
		    (long long)rawp->transid,
		    (long long)transid);
#endif
	    jo->fifo.xindex += (rawp->recsize + 15) & ~15;
	    jo->total_acked += (rawp->recsize + 15) & ~15;
	    if (jo->flags & MC_JOURNAL_WWAIT) {
		jo->flags &= ~MC_JOURNAL_WWAIT;	/* XXX hysteresis */
		wakeup(&jo->fifo.windex);
	    }
	    transid = 0;
	    continue;
	}
	kprintf("warning: unsent data(2) acknowledged transid %08llx\n",
		(long long)transid);
	transid = 0;
    }
    jo->flags &= ~MC_JOURNAL_RACTIVE;
    wakeup(jo);
    wakeup(&jo->fifo.windex);
    rel_mplock();
}

/*
 * This builds a pad record which the journaling thread will skip over.  Pad
 * records are required when we are unable to reserve sufficient stream space
 * due to insufficient space at the end of the physical memory fifo.
 *
 * Even though the record is not transmitted, a normal transid must be 
 * assigned to it so link recovery operations after a failure work properly.
 */
static
void
journal_build_pad(struct journal_rawrecbeg *rawp, int recsize, int64_t transid)
{
    struct journal_rawrecend *rendp;
    
    KKASSERT((recsize & 15) == 0 && recsize >= 16);

    rawp->streamid = JREC_STREAMID_PAD;
    rawp->recsize = recsize;	/* must be 16-byte aligned */
    rawp->transid = transid;
    /*
     * WARNING, rendp may overlap rawp->transid.  This is necessary to
     * allow PAD records to fit in 16 bytes.  Use cpu_ccfence() to
     * hopefully cause the compiler to not make any assumptions.
     */
    rendp = (void *)((char *)rawp + rawp->recsize - sizeof(*rendp));
    rendp->endmagic = JREC_ENDMAGIC;
    rendp->check = 0;
    rendp->recsize = rawp->recsize;

    /*
     * Set the begin magic last.  This is what will allow the journal
     * thread to write the record out.  Use a store fence to prevent
     * compiler and cpu reordering of the writes.
     */
    cpu_sfence();
    rawp->begmagic = JREC_BEGMAGIC;
}

/*
 * Wake up the worker thread if the FIFO is more then half full or if
 * someone is waiting for space to be freed up.  Otherwise let the 
 * heartbeat deal with it.  Being able to avoid waking up the worker
 * is the key to the journal's cpu performance.
 */
static __inline
void
journal_commit_wakeup(struct journal *jo)
{
    int avail;

    avail = jo->fifo.size - (jo->fifo.windex - jo->fifo.xindex);
    KKASSERT(avail >= 0);
    if ((avail < (jo->fifo.size >> 1)) || (jo->flags & MC_JOURNAL_WWAIT))
	wakeup(&jo->fifo);
}

/*
 * Create a new BEGIN stream record with the specified streamid and the
 * specified amount of payload space.  *rawpp will be set to point to the
 * base of the new stream record and a pointer to the base of the payload
 * space will be returned.  *rawpp does not need to be pre-NULLd prior to
 * making this call.  The raw record header will be partially initialized.
 *
 * A stream can be extended, aborted, or committed by other API calls
 * below.  This may result in a sequence of potentially disconnected
 * stream records to be output to the journaling target.  The first record
 * (the one created by this function) will be marked JREC_STREAMCTL_BEGIN,
 * while the last record on commit or abort will be marked JREC_STREAMCTL_END
 * (and possibly also JREC_STREAMCTL_ABORTED).  The last record could wind
 * up being the same as the first, in which case the bits are all set in
 * the first record.
 *
 * The stream record is created in an incomplete state by setting the begin
 * magic to JREC_INCOMPLETEMAGIC.  This prevents the worker thread from
 * flushing the fifo past our record until we have finished populating it.
 * Other threads can reserve and operate on their own space without stalling
 * but the stream output will stall until we have completed operations.  The
 * memory FIFO is intended to be large enough to absorb such situations
 * without stalling out other threads.
 */
static
void *
journal_reserve(struct journal *jo, struct journal_rawrecbeg **rawpp,
		int16_t streamid, int bytes)
{
    struct journal_rawrecbeg *rawp;
    int avail;
    int availtoend;
    int req;

    /*
     * Add header and trailer overheads to the passed payload.  Note that
     * the passed payload size need not be aligned in any way.
     */
    bytes += sizeof(struct journal_rawrecbeg);
    bytes += sizeof(struct journal_rawrecend);

    for (;;) {
	/*
	 * First, check boundary conditions.  If the request would wrap around
	 * we have to skip past the ending block and return to the beginning
	 * of the FIFO's buffer.  Calculate 'req' which is the actual number
	 * of bytes being reserved, including wrap-around dead space.
	 *
	 * Neither 'bytes' or 'req' are aligned.
	 *
	 * Note that availtoend is not truncated to avail and so cannot be
	 * used to determine whether the reservation is possible by itself.
	 * Also, since all fifo ops are 16-byte aligned, we can check
	 * the size before calculating the aligned size.
	 */
	availtoend = jo->fifo.size - (jo->fifo.windex & jo->fifo.mask);
	KKASSERT((availtoend & 15) == 0);
	if (bytes > availtoend) 
	    req = bytes + availtoend;	/* add pad to end */
	else
	    req = bytes;

	/*
	 * Next calculate the total available space and see if it is
	 * sufficient.  We cannot overwrite previously buffered data
	 * past xindex because otherwise we would not be able to restart
	 * a broken link at the target's last point of commit.
	 */
	avail = jo->fifo.size - (jo->fifo.windex - jo->fifo.xindex);
	KKASSERT(avail >= 0 && (avail & 15) == 0);

	if (avail < req) {
	    /* XXX MC_JOURNAL_STOP_IMM */
	    jo->flags |= MC_JOURNAL_WWAIT;
	    ++jo->fifostalls;
	    tsleep(&jo->fifo.windex, 0, "jwrite", 0);
	    continue;
	}

	/*
	 * Create a pad record for any dead space and create an incomplete
	 * record for the live space, then return a pointer to the
	 * contiguous buffer space that was requested.
	 *
	 * NOTE: The worker thread will not flush past an incomplete
	 * record, so the reserved space can be filled in at-will.  The
	 * journaling code must also be aware the reserved sections occuring
	 * after this one will also not be written out even if completed
	 * until this one is completed.
	 *
	 * The transaction id must accomodate real and potential pad creation.
	 */
	rawp = (void *)(jo->fifo.membase + (jo->fifo.windex & jo->fifo.mask));
	if (req != bytes) {
	    journal_build_pad(rawp, availtoend, jo->transid);
	    ++jo->transid;
	    rawp = (void *)jo->fifo.membase;
	}
	rawp->begmagic = JREC_INCOMPLETEMAGIC;	/* updated by abort/commit */
	rawp->recsize = bytes;			/* (unaligned size) */
	rawp->streamid = streamid | JREC_STREAMCTL_BEGIN;
	rawp->transid = jo->transid;
	jo->transid += 2;

	/*
	 * Issue a memory barrier to guarentee that the record data has been
	 * properly initialized before we advance the write index and return
	 * a pointer to the reserved record.  Otherwise the worker thread
	 * could accidently run past us.
	 *
	 * Note that stream records are always 16-byte aligned.
	 */
	cpu_sfence();
	jo->fifo.windex += (req + 15) & ~15;
	*rawpp = rawp;
	return(rawp + 1);
    }
    /* not reached */
    *rawpp = NULL;
    return(NULL);
}

/*
 * Attempt to extend the stream record by <bytes> worth of payload space.
 *
 * If it is possible to extend the existing stream record no truncation
 * occurs and the record is extended as specified.  A pointer to the 
 * truncation offset within the payload space is returned.
 *
 * If it is not possible to do this the existing stream record is truncated
 * and committed, and a new stream record of size <bytes> is created.  A
 * pointer to the base of the new stream record's payload space is returned.
 *
 * *rawpp is set to the new reservation in the case of a new record but
 * the caller cannot depend on a comparison with the old rawp to determine if
 * this case occurs because we could end up using the same memory FIFO
 * offset for the new stream record.  Use *newstreamrecp instead.
 */
static void *
journal_extend(struct journal *jo, struct journal_rawrecbeg **rawpp, 
		int truncbytes, int bytes, int *newstreamrecp)
{
    struct journal_rawrecbeg *rawp;
    int16_t streamid;
    int availtoend;
    int avail;
    int osize;
    int nsize;
    int wbase;
    void *rptr;

    *newstreamrecp = 0;
    rawp = *rawpp;
    osize = (rawp->recsize + 15) & ~15;
    nsize = (rawp->recsize + bytes + 15) & ~15;
    wbase = (char *)rawp - jo->fifo.membase;

    /*
     * If the aligned record size does not change we can trivially adjust
     * the record size.
     */
    if (nsize == osize) {
	rawp->recsize += bytes;
	return((char *)(rawp + 1) + truncbytes);
    }

    /*
     * If the fifo's write index hasn't been modified since we made the
     * reservation and we do not hit any boundary conditions, we can 
     * trivially make the record smaller or larger.
     */
    if ((jo->fifo.windex & jo->fifo.mask) == wbase + osize) {
	availtoend = jo->fifo.size - wbase;
	avail = jo->fifo.size - (jo->fifo.windex - jo->fifo.xindex) + osize;
	KKASSERT((availtoend & 15) == 0);
	KKASSERT((avail & 15) == 0);
	if (nsize <= avail && nsize <= availtoend) {
	    jo->fifo.windex += nsize - osize;
	    rawp->recsize += bytes;
	    return((char *)(rawp + 1) + truncbytes);
	}
    }

    /*
     * It was not possible to extend the buffer.  Commit the current
     * buffer and create a new one.  We manually clear the BEGIN mark that
     * journal_reserve() creates (because this is a continuing record, not
     * the start of a new stream).
     */
    streamid = rawp->streamid & JREC_STREAMID_MASK;
    journal_commit(jo, rawpp, truncbytes, 0);
    rptr = journal_reserve(jo, rawpp, streamid, bytes);
    rawp = *rawpp;
    rawp->streamid &= ~JREC_STREAMCTL_BEGIN;
    *newstreamrecp = 1;
    return(rptr);
}

/*
 * Abort a journal record.  If the transaction record represents a stream
 * BEGIN and we can reverse the fifo's write index we can simply reverse
 * index the entire record, as if it were never reserved in the first place.
 *
 * Otherwise we set the JREC_STREAMCTL_ABORTED bit and commit the record
 * with the payload truncated to 0 bytes.
 */
static void
journal_abort(struct journal *jo, struct journal_rawrecbeg **rawpp)
{
    struct journal_rawrecbeg *rawp;
    int osize;

    rawp = *rawpp;
    osize = (rawp->recsize + 15) & ~15;

    if ((rawp->streamid & JREC_STREAMCTL_BEGIN) &&
	(jo->fifo.windex & jo->fifo.mask) == 
	 (char *)rawp - jo->fifo.membase + osize)
    {
	jo->fifo.windex -= osize;
	*rawpp = NULL;
    } else {
	rawp->streamid |= JREC_STREAMCTL_ABORTED;
	journal_commit(jo, rawpp, 0, 1);
    }
}

/*
 * Commit a journal record and potentially truncate it to the specified
 * number of payload bytes.  If you do not want to truncate the record,
 * simply pass -1 for the bytes parameter.  Do not pass rawp->recsize, that
 * field includes header and trailer and will not be correct.  Note that
 * passing 0 will truncate the entire data payload of the record.
 *
 * The logical stream is terminated by this function.
 *
 * If truncation occurs, and it is not possible to physically optimize the
 * memory FIFO due to other threads having reserved space after ours,
 * the remaining reserved space will be covered by a pad record.
 */
static void
journal_commit(struct journal *jo, struct journal_rawrecbeg **rawpp,
		int bytes, int closeout)
{
    struct journal_rawrecbeg *rawp;
    struct journal_rawrecend *rendp;
    int osize;
    int nsize;

    rawp = *rawpp;
    *rawpp = NULL;

    KKASSERT((char *)rawp >= jo->fifo.membase &&
	     (char *)rawp + rawp->recsize <= jo->fifo.membase + jo->fifo.size);
    KKASSERT(((intptr_t)rawp & 15) == 0);

    /*
     * Truncate the record if necessary.  If the FIFO write index as still
     * at the end of our record we can optimally backindex it.  Otherwise
     * we have to insert a pad record to cover the dead space.
     *
     * We calculate osize which is the 16-byte-aligned original recsize.
     * We calculate nsize which is the 16-byte-aligned new recsize.
     *
     * Due to alignment issues or in case the passed truncation bytes is
     * the same as the original payload, nsize may be equal to osize even
     * if the committed bytes is less then the originally reserved bytes.
     */
    if (bytes >= 0) {
	KKASSERT(bytes >= 0 && bytes <= rawp->recsize - sizeof(struct journal_rawrecbeg) - sizeof(struct journal_rawrecend));
	osize = (rawp->recsize + 15) & ~15;
	rawp->recsize = bytes + sizeof(struct journal_rawrecbeg) +
			sizeof(struct journal_rawrecend);
	nsize = (rawp->recsize + 15) & ~15;
	KKASSERT(nsize <= osize);
	if (osize == nsize) {
	    /* do nothing */
	} else if ((jo->fifo.windex & jo->fifo.mask) == (char *)rawp - jo->fifo.membase + osize) {
	    /* we are able to backindex the fifo */
	    jo->fifo.windex -= osize - nsize;
	} else {
	    /* we cannot backindex the fifo, emplace a pad in the dead space */
	    journal_build_pad((void *)((char *)rawp + nsize), osize - nsize,
				rawp->transid + 1);
	}
    }

    /*
     * Fill in the trailer.  Note that unlike pad records, the trailer will
     * never overlap the header.
     */
    rendp = (void *)((char *)rawp + 
	    ((rawp->recsize + 15) & ~15) - sizeof(*rendp));
    rendp->endmagic = JREC_ENDMAGIC;
    rendp->recsize = rawp->recsize;
    rendp->check = 0;		/* XXX check word, disabled for now */

    /*
     * Fill in begmagic last.  This will allow the worker thread to proceed.
     * Use a memory barrier to guarentee write ordering.  Mark the stream
     * as terminated if closeout is set.  This is the typical case.
     */
    if (closeout)
	rawp->streamid |= JREC_STREAMCTL_END;
    cpu_sfence();		/* memory and compiler barrier */
    rawp->begmagic = JREC_BEGMAGIC;

    journal_commit_wakeup(jo);
}

/************************************************************************
 *			TRANSACTION SUPPORT ROUTINES			*
 ************************************************************************
 *
 * JRECORD_*() - routines to create subrecord transactions and embed them
 *		 in the logical streams managed by the journal_*() routines.
 */

/*
 * Initialize the passed jrecord structure and start a new stream transaction
 * by reserving an initial build space in the journal's memory FIFO.
 */
void
jrecord_init(struct journal *jo, struct jrecord *jrec, int16_t streamid)
{
    bzero(jrec, sizeof(*jrec));
    jrec->jo = jo;
    jrec->streamid = streamid;
    jrec->stream_residual = JREC_DEFAULTSIZE;
    jrec->stream_reserved = jrec->stream_residual;
    jrec->stream_ptr = 
	journal_reserve(jo, &jrec->rawp, streamid, jrec->stream_reserved);
}

/*
 * Push a recursive record type.  All pushes should have matching pops.
 * The old parent is returned and the newly pushed record becomes the
 * new parent.  Note that the old parent's pointer may already be invalid
 * or may become invalid if jrecord_write() had to build a new stream
 * record, so the caller should not mess with the returned pointer in
 * any way other then to save it.
 */
struct journal_subrecord *
jrecord_push(struct jrecord *jrec, int16_t rectype)
{
    struct journal_subrecord *save;

    save = jrec->parent;
    jrec->parent = jrecord_write(jrec, rectype|JMASK_NESTED, 0);
    jrec->last = NULL;
    KKASSERT(jrec->parent != NULL);
    ++jrec->pushcount;
    ++jrec->pushptrgood;	/* cleared on flush */
    return(save);
}

/*
 * Pop a previously pushed sub-transaction.  We must set JMASK_LAST
 * on the last record written within the subtransaction.  If the last 
 * record written is not accessible or if the subtransaction is empty,
 * we must write out a pad record with JMASK_LAST set before popping.
 *
 * When popping a subtransaction the parent record's recsize field
 * will be properly set.  If the parent pointer is no longer valid
 * (which can occur if the data has already been flushed out to the
 * stream), the protocol spec allows us to leave it 0.
 *
 * The saved parent pointer which we restore may or may not be valid,
 * and if not valid may or may not be NULL, depending on the value
 * of pushptrgood.
 */
void
jrecord_pop(struct jrecord *jrec, struct journal_subrecord *save)
{
    struct journal_subrecord *last;

    KKASSERT(jrec->pushcount > 0);
    KKASSERT(jrec->residual == 0);

    /*
     * Set JMASK_LAST on the last record we wrote at the current
     * level.  If last is NULL we either no longer have access to the
     * record or the subtransaction was empty and we must write out a pad
     * record.
     */
    if ((last = jrec->last) == NULL) {
	jrecord_write(jrec, JLEAF_PAD|JMASK_LAST, 0);
	last = jrec->last;	/* reload after possible flush */
    } else {
	last->rectype |= JMASK_LAST;
    }

    /*
     * pushptrgood tells us how many levels of parent record pointers
     * are valid.  The jrec only stores the current parent record pointer
     * (and it is only valid if pushptrgood != 0).  The higher level parent
     * record pointers are saved by the routines calling jrecord_push() and
     * jrecord_pop().  These pointers may become stale and we determine
     * that fact by tracking the count of valid parent pointers with 
     * pushptrgood.  Pointers become invalid when their related stream
     * record gets pushed out.
     *
     * If no pointer is available (the data has already been pushed out),
     * then no fixup of e.g. the length field is possible for non-leaf
     * nodes.  The protocol allows for this situation by placing a larger
     * burden on the program scanning the stream on the other end.
     *
     * [parentA]
     *	  [node X]
     *    [parentB]
     *	     [node Y]
     *	     [node Z]
     *    (pop B)	see NOTE B
     * (pop A)		see NOTE A
     *
     * NOTE B:	This pop sets LAST in node Z if the node is still accessible,
     *		else a PAD record is appended and LAST is set in that.
     *
     *		This pop sets the record size in parentB if parentB is still
     *		accessible, else the record size is left 0 (the scanner must
     *		deal with that).
     *
     *		This pop sets the new 'last' record to parentB, the pointer
     *		to which may or may not still be accessible.
     *
     * NOTE A:	This pop sets LAST in parentB if the node is still accessible,
     *		else a PAD record is appended and LAST is set in that.
     *
     *		This pop sets the record size in parentA if parentA is still
     *		accessible, else the record size is left 0 (the scanner must
     *		deal with that).
     *
     *		This pop sets the new 'last' record to parentA, the pointer
     *		to which may or may not still be accessible.
     *
     * Also note that the last record in the stream transaction, which in
     * the above example is parentA, does not currently have the LAST bit
     * set.
     *
     * The current parent becomes the last record relative to the
     * saved parent passed into us.  It's validity is based on 
     * whether pushptrgood is non-zero prior to decrementing.  The saved
     * parent becomes the new parent, and its validity is based on whether
     * pushptrgood is non-zero after decrementing.
     *
     * The old jrec->parent may be NULL if it is no longer accessible.
     * If pushptrgood is non-zero, however, it is guarenteed to not
     * be NULL (since no flush occured).
     */
    jrec->last = jrec->parent;
    --jrec->pushcount;
    if (jrec->pushptrgood) {
	KKASSERT(jrec->last != NULL && last != NULL);
	if (--jrec->pushptrgood == 0) {
	    jrec->parent = NULL;	/* 'save' contains garbage or NULL */
	} else {
	    KKASSERT(save != NULL);
	    jrec->parent = save;	/* 'save' must not be NULL */
	}

	/*
	 * Set the record size in the old parent.  'last' still points to
	 * the original last record in the subtransaction being popped,
	 * jrec->last points to the old parent (which became the last
	 * record relative to the new parent being popped into).
	 */
	jrec->last->recsize = (char *)last + last->recsize - (char *)jrec->last;
    } else {
	jrec->parent = NULL;
	KKASSERT(jrec->last == NULL);
    }
}

/*
 * Write out a leaf record, including associated data.
 */
void
jrecord_leaf(struct jrecord *jrec, int16_t rectype, void *ptr, int bytes)
{
    jrecord_write(jrec, rectype, bytes);
    jrecord_data(jrec, ptr, bytes, JDATA_KERN);
}

void
jrecord_leaf_uio(struct jrecord *jrec, int16_t rectype,
		 struct uio *uio)
{
    struct iovec *iov;
    int i;

    for (i = 0; i < uio->uio_iovcnt; ++i) {
	iov = &uio->uio_iov[i];
	if (iov->iov_len == 0)
	    continue;
	if (uio->uio_segflg == UIO_SYSSPACE) {
	    jrecord_write(jrec, rectype, iov->iov_len);
	    jrecord_data(jrec, iov->iov_base, iov->iov_len, JDATA_KERN);
	} else { /* UIO_USERSPACE */
	    jrecord_write(jrec, rectype, iov->iov_len);
	    jrecord_data(jrec, iov->iov_base, iov->iov_len, JDATA_USER);
	}
    }
}

void
jrecord_leaf_xio(struct jrecord *jrec, int16_t rectype, xio_t xio)
{
    int bytes = xio->xio_npages * PAGE_SIZE;

    jrecord_write(jrec, rectype, bytes);
    jrecord_data(jrec, xio, bytes, JDATA_XIO);
}

/*
 * Write a leaf record out and return a pointer to its base.  The leaf
 * record may contain potentially megabytes of data which is supplied
 * in jrecord_data() calls.  The exact amount must be specified in this
 * call.
 *
 * THE RETURNED SUBRECORD POINTER IS ONLY VALID IMMEDIATELY AFTER THE
 * CALL AND MAY BECOME INVALID AT ANY TIME.  ONLY THE PUSH/POP CODE SHOULD
 * USE THE RETURN VALUE.
 */
struct journal_subrecord *
jrecord_write(struct jrecord *jrec, int16_t rectype, int bytes)
{
    struct journal_subrecord *last;
    int pusheditout;

    /*
     * Try to catch some obvious errors.  Nesting records must specify a
     * size of 0, and there should be no left-overs from previous operations
     * (such as incomplete data writeouts).
     */
    KKASSERT(bytes == 0 || (rectype & JMASK_NESTED) == 0);
    KKASSERT(jrec->residual == 0);

    /*
     * Check to see if the current stream record has enough room for
     * the new subrecord header.  If it doesn't we extend the current
     * stream record.
     *
     * This may have the side effect of pushing out the current stream record
     * and creating a new one.  We must adjust our stream tracking fields
     * accordingly.
     */
    if (jrec->stream_residual < sizeof(struct journal_subrecord)) {
	jrec->stream_ptr = journal_extend(jrec->jo, &jrec->rawp,
				jrec->stream_reserved - jrec->stream_residual,
				JREC_DEFAULTSIZE, &pusheditout);
	if (pusheditout) {
	    /*
	     * If a pushout occured, the pushed out stream record was
	     * truncated as specified and the new record is exactly the
	     * extension size specified.
	     */
	    jrec->stream_reserved = JREC_DEFAULTSIZE;
	    jrec->stream_residual = JREC_DEFAULTSIZE;
	    jrec->parent = NULL;	/* no longer accessible */
	    jrec->pushptrgood = 0;	/* restored parents in pops no good */
	} else {
	    /*
	     * If no pushout occured the stream record is NOT truncated and
	     * IS extended.
	     */
	    jrec->stream_reserved += JREC_DEFAULTSIZE;
	    jrec->stream_residual += JREC_DEFAULTSIZE;
	}
    }
    last = (void *)jrec->stream_ptr;
    last->rectype = rectype;
    last->reserved = 0;

    /*
     * We may not know the record size for recursive records and the 
     * header may become unavailable due to limited FIFO space.  Write
     * -1 to indicate this special case.
     */
    if ((rectype & JMASK_NESTED) && bytes == 0)
	last->recsize = -1;
    else
	last->recsize = sizeof(struct journal_subrecord) + bytes;
    jrec->last = last;
    jrec->residual = bytes;		/* remaining data to be posted */
    jrec->residual_align = -bytes & 7;	/* post-data alignment required */
    jrec->stream_ptr += sizeof(*last);	/* current write pointer */
    jrec->stream_residual -= sizeof(*last); /* space remaining in stream */
    return(last);
}

/*
 * Write out the data associated with a leaf record.  Any number of calls
 * to this routine may be made as long as the byte count adds up to the
 * amount originally specified in jrecord_write().
 *
 * The act of writing out the leaf data may result in numerous stream records
 * being pushed out.   Callers should be aware that even the associated
 * subrecord header may become inaccessible due to stream record pushouts.
 */
static void
jrecord_data(struct jrecord *jrec, void *buf, int bytes, int dtype)
{
    int pusheditout;
    int extsize;
    int xio_offset = 0;

    KKASSERT(bytes >= 0 && bytes <= jrec->residual);

    /*
     * Push out stream records as long as there is insufficient room to hold
     * the remaining data.
     */
    while (jrec->stream_residual < bytes) {
	/*
	 * Fill in any remaining space in the current stream record.
	 */
	switch (dtype) {
	case JDATA_KERN:
	    bcopy(buf, jrec->stream_ptr, jrec->stream_residual);
	    break;
	case JDATA_USER:
	    copyin(buf, jrec->stream_ptr, jrec->stream_residual);
	    break;
	case JDATA_XIO:
	    xio_copy_xtok((xio_t)buf, xio_offset, jrec->stream_ptr,
			  jrec->stream_residual);
	    xio_offset += jrec->stream_residual;
	    break;
	}
	if (dtype != JDATA_XIO)
	    buf = (char *)buf + jrec->stream_residual;
	bytes -= jrec->stream_residual;
	/*jrec->stream_ptr += jrec->stream_residual;*/
	jrec->residual -= jrec->stream_residual;
	jrec->stream_residual = 0;

	/*
	 * Try to extend the current stream record, but no more then 1/4
	 * the size of the FIFO.
	 */
	extsize = jrec->jo->fifo.size >> 2;
	if (extsize > bytes)
	    extsize = (bytes + 15) & ~15;

	jrec->stream_ptr = journal_extend(jrec->jo, &jrec->rawp,
				jrec->stream_reserved - jrec->stream_residual,
				extsize, &pusheditout);
	if (pusheditout) {
	    jrec->stream_reserved = extsize;
	    jrec->stream_residual = extsize;
	    jrec->parent = NULL;	/* no longer accessible */
	    jrec->last = NULL;		/* no longer accessible */
	    jrec->pushptrgood = 0;	/* restored parents in pops no good */
	} else {
	    jrec->stream_reserved += extsize;
	    jrec->stream_residual += extsize;
	}
    }

    /*
     * Push out any remaining bytes into the current stream record.
     */
    if (bytes) {
	switch (dtype) {
	case JDATA_KERN:
	    bcopy(buf, jrec->stream_ptr, bytes);
	    break;
	case JDATA_USER:
	    copyin(buf, jrec->stream_ptr, bytes);
	    break;
	case JDATA_XIO:
	    xio_copy_xtok((xio_t)buf, xio_offset, jrec->stream_ptr, bytes);
	    break;
	}
	jrec->stream_ptr += bytes;
	jrec->stream_residual -= bytes;
	jrec->residual -= bytes;
    }

    /*
     * Handle data alignment requirements for the subrecord.  Because the
     * stream record's data space is more strictly aligned, it must already
     * have sufficient space to hold any subrecord alignment slop.
     */
    if (jrec->residual == 0 && jrec->residual_align) {
	KKASSERT(jrec->residual_align <= jrec->stream_residual);
	bzero(jrec->stream_ptr, jrec->residual_align);
	jrec->stream_ptr += jrec->residual_align;
	jrec->stream_residual -= jrec->residual_align;
	jrec->residual_align = 0;
    }
}

/*
 * We are finished with the transaction.  This closes the transaction created
 * by jrecord_init().
 *
 * NOTE: If abortit is not set then we must be at the top level with no
 *	 residual subrecord data left to output.
 *
 *	 If abortit is set then we can be in any state, all pushes will be 
 *	 popped and it is ok for there to be residual data.  This works 
 *	 because the virtual stream itself is truncated.  Scanners must deal
 *	 with this situation.
 *
 * The stream record will be committed or aborted as specified and jrecord
 * resources will be cleaned up.
 */
void
jrecord_done(struct jrecord *jrec, int abortit)
{
    KKASSERT(jrec->rawp != NULL);

    if (abortit) {
	journal_abort(jrec->jo, &jrec->rawp);
    } else {
	KKASSERT(jrec->pushcount == 0 && jrec->residual == 0);
	journal_commit(jrec->jo, &jrec->rawp, 
			jrec->stream_reserved - jrec->stream_residual, 1);
    }

    /*
     * jrec should not be used beyond this point without another init,
     * but clean up some fields to ensure that we panic if it is.
     *
     * Note that jrec->rawp is NULLd out by journal_abort/journal_commit.
     */
    jrec->jo = NULL;
    jrec->stream_ptr = NULL;
}

/************************************************************************
 *			LOW LEVEL RECORD SUPPORT ROUTINES		*
 ************************************************************************
 *
 * These routine create low level recursive and leaf subrecords representing
 * common filesystem structures.
 */

/*
 * Write out a filename path relative to the base of the mount point.
 * rectype is typically JLEAF_PATH{1,2,3,4}.
 */
void
jrecord_write_path(struct jrecord *jrec, int16_t rectype, struct namecache *ncp)
{
    char buf[64];	/* local buffer if it fits, else malloced */
    char *base;
    int pathlen;
    int index;
    struct namecache *scan;

    /*
     * Pass 1 - figure out the number of bytes required.  Include terminating
     * 	       \0 on last element and '/' separator on other elements.
     *
     * The namecache topology terminates at the root of the filesystem
     * (the normal lookup code would then continue by using the mount
     * structure to figure out what it was mounted on).
     */
again:
    pathlen = 0;
    for (scan = ncp; scan; scan = scan->nc_parent) {
	if (scan->nc_nlen > 0)
	    pathlen += scan->nc_nlen + 1;
    }

    if (pathlen <= sizeof(buf))
	base = buf;
    else
	base = kmalloc(pathlen, M_TEMP, M_INTWAIT);

    /*
     * Pass 2 - generate the path buffer
     */
    index = pathlen;
    for (scan = ncp; scan; scan = scan->nc_parent) {
	if (scan->nc_nlen == 0)
	    continue;
	if (scan->nc_nlen >= index) {
	    if (base != buf)
		kfree(base, M_TEMP);
	    goto again;
	}
	if (index == pathlen)
	    base[--index] = 0;
	else
	    base[--index] = '/';
	index -= scan->nc_nlen;
	bcopy(scan->nc_name, base + index, scan->nc_nlen);
    }
    jrecord_leaf(jrec, rectype, base + index, pathlen - index);
    if (base != buf)
	kfree(base, M_TEMP);
}

/*
 * Write out a file attribute structure.  While somewhat inefficient, using
 * a recursive data structure is the most portable and extensible way.
 */
void
jrecord_write_vattr(struct jrecord *jrec, struct vattr *vat)
{
    void *save;

    save = jrecord_push(jrec, JTYPE_VATTR);
    if (vat->va_type != VNON)
	jrecord_leaf(jrec, JLEAF_VTYPE, &vat->va_type, sizeof(vat->va_type));
    if (vat->va_mode != (mode_t)VNOVAL)
	jrecord_leaf(jrec, JLEAF_MODES, &vat->va_mode, sizeof(vat->va_mode));
    if (vat->va_nlink != VNOVAL)
	jrecord_leaf(jrec, JLEAF_NLINK, &vat->va_nlink, sizeof(vat->va_nlink));
    if (vat->va_uid != VNOVAL)
	jrecord_leaf(jrec, JLEAF_UID, &vat->va_uid, sizeof(vat->va_uid));
    if (vat->va_gid != VNOVAL)
	jrecord_leaf(jrec, JLEAF_GID, &vat->va_gid, sizeof(vat->va_gid));
    if (vat->va_fsid != VNOVAL)
	jrecord_leaf(jrec, JLEAF_FSID, &vat->va_fsid, sizeof(vat->va_fsid));
    if (vat->va_fileid != VNOVAL)
	jrecord_leaf(jrec, JLEAF_INUM, &vat->va_fileid, sizeof(vat->va_fileid));
    if (vat->va_size != VNOVAL)
	jrecord_leaf(jrec, JLEAF_SIZE, &vat->va_size, sizeof(vat->va_size));
    if (vat->va_atime.tv_sec != VNOVAL)
	jrecord_leaf(jrec, JLEAF_ATIME, &vat->va_atime, sizeof(vat->va_atime));
    if (vat->va_mtime.tv_sec != VNOVAL)
	jrecord_leaf(jrec, JLEAF_MTIME, &vat->va_mtime, sizeof(vat->va_mtime));
    if (vat->va_ctime.tv_sec != VNOVAL)
	jrecord_leaf(jrec, JLEAF_CTIME, &vat->va_ctime, sizeof(vat->va_ctime));
    if (vat->va_gen != VNOVAL)
	jrecord_leaf(jrec, JLEAF_GEN, &vat->va_gen, sizeof(vat->va_gen));
    if (vat->va_flags != VNOVAL)
	jrecord_leaf(jrec, JLEAF_FLAGS, &vat->va_flags, sizeof(vat->va_flags));
    if (vat->va_rmajor != VNOVAL) {
	udev_t rdev = makeudev(vat->va_rmajor, vat->va_rminor);
	jrecord_leaf(jrec, JLEAF_UDEV, &rdev, sizeof(rdev));
	jrecord_leaf(jrec, JLEAF_UMAJOR, &vat->va_rmajor, sizeof(vat->va_rmajor));
	jrecord_leaf(jrec, JLEAF_UMINOR, &vat->va_rminor, sizeof(vat->va_rminor));
    }
#if 0
    if (vat->va_filerev != VNOVAL)
	jrecord_leaf(jrec, JLEAF_FILEREV, &vat->va_filerev, sizeof(vat->va_filerev));
#endif
    jrecord_pop(jrec, save);
}

/*
 * Write out the creds used to issue a file operation.  If a process is
 * available write out additional tracking information related to the 
 * process.
 *
 * XXX additional tracking info
 * XXX tty line info
 */
void
jrecord_write_cred(struct jrecord *jrec, struct thread *td, struct ucred *cred)
{
    void *save;
    struct proc *p;

    save = jrecord_push(jrec, JTYPE_CRED);
    jrecord_leaf(jrec, JLEAF_UID, &cred->cr_uid, sizeof(cred->cr_uid));
    jrecord_leaf(jrec, JLEAF_GID, &cred->cr_gid, sizeof(cred->cr_gid));
    if (td && (p = td->td_proc) != NULL) {
	jrecord_leaf(jrec, JLEAF_PID, &p->p_pid, sizeof(p->p_pid));
	jrecord_leaf(jrec, JLEAF_COMM, p->p_comm, sizeof(p->p_comm));
    }
    jrecord_pop(jrec, save);
}

/*
 * Write out information required to identify a vnode
 *
 * XXX this needs work.  We should write out the inode number as well,
 * and in fact avoid writing out the file path for seqential writes
 * occuring within e.g. a certain period of time.
 */
void
jrecord_write_vnode_ref(struct jrecord *jrec, struct vnode *vp)
{
    struct nchandle nch;

    nch.mount = vp->v_mount;
    spin_lock(&vp->v_spin);
    TAILQ_FOREACH(nch.ncp, &vp->v_namecache, nc_vnode) {
	if ((nch.ncp->nc_flag & (NCF_UNRESOLVED|NCF_DESTROYED)) == 0)
	    break;
    }
    if (nch.ncp) {
	cache_hold(&nch);
	spin_unlock(&vp->v_spin);
	jrecord_write_path(jrec, JLEAF_PATH_REF, nch.ncp);
	cache_drop(&nch);
    } else {
	spin_unlock(&vp->v_spin);
    }
}

void
jrecord_write_vnode_link(struct jrecord *jrec, struct vnode *vp, 
			 struct namecache *notncp)
{
    struct nchandle nch;

    nch.mount = vp->v_mount;
    spin_lock(&vp->v_spin);
    TAILQ_FOREACH(nch.ncp, &vp->v_namecache, nc_vnode) {
	if (nch.ncp == notncp)
	    continue;
	if ((nch.ncp->nc_flag & (NCF_UNRESOLVED|NCF_DESTROYED)) == 0)
	    break;
    }
    if (nch.ncp) {
	cache_hold(&nch);
	spin_unlock(&vp->v_spin);
	jrecord_write_path(jrec, JLEAF_PATH_REF, nch.ncp);
	cache_drop(&nch);
    } else {
	spin_unlock(&vp->v_spin);
    }
}

/*
 * Write out the data represented by a pagelist
 */
void
jrecord_write_pagelist(struct jrecord *jrec, int16_t rectype,
			struct vm_page **pglist, int *rtvals, int pgcount,
			off_t offset)
{
    struct xio xio;
    int error;
    int b;
    int i;

    i = 0;
    xio_init(&xio);
    while (i < pgcount) {
	/*
	 * Find the next valid section.  Skip any invalid elements
	 */
	if (rtvals[i] != VM_PAGER_OK) {
	    ++i;
	    offset += PAGE_SIZE;
	    continue;
	}

	/*
	 * Figure out how big the valid section is, capping I/O at what the
	 * MSFBUF can represent.
	 */
	b = i;
	while (i < pgcount && i - b != XIO_INTERNAL_PAGES && 
	       rtvals[i] == VM_PAGER_OK
	) {
	    ++i;
	}

	/*
	 * And write it out.
	 */
	if (i - b) {
	    error = xio_init_pages(&xio, pglist + b, i - b, XIOF_READ);
	    if (error == 0) {
		jrecord_leaf(jrec, JLEAF_SEEKPOS, &offset, sizeof(offset));
		jrecord_leaf_xio(jrec, rectype, &xio);
	    } else {
		kprintf("jrecord_write_pagelist: xio init failure\n");
	    }
	    xio_release(&xio);
	    offset += (off_t)(i - b) << PAGE_SHIFT;
	}
    }
}

/*
 * Write out the data represented by a UIO.
 */
void
jrecord_write_uio(struct jrecord *jrec, int16_t rectype, struct uio *uio)
{
    if (uio->uio_segflg != UIO_NOCOPY) {
	jrecord_leaf(jrec, JLEAF_SEEKPOS, &uio->uio_offset, 
		     sizeof(uio->uio_offset));
	jrecord_leaf_uio(jrec, rectype, uio);
    }
}

void
jrecord_file_data(struct jrecord *jrec, struct vnode *vp, 
		  off_t off, off_t bytes)
{
    const int bufsize = 8192;
    char *buf;
    int error;
    int n;

    buf = kmalloc(bufsize, M_JOURNAL, M_WAITOK);
    jrecord_leaf(jrec, JLEAF_SEEKPOS, &off, sizeof(off));
    while (bytes) {
	n = (bytes > bufsize) ? bufsize : (int)bytes;
	error = vn_rdwr(UIO_READ, vp, buf, n, off, UIO_SYSSPACE, IO_NODELOCKED,
			proc0.p_ucred, NULL);
	if (error) {
	    jrecord_leaf(jrec, JLEAF_ERROR, &error, sizeof(error));
	    break;
	}
	jrecord_leaf(jrec, JLEAF_FILEDATA, buf, n);
	bytes -= n;
	off += n;
    }
    kfree(buf, M_JOURNAL);
}
