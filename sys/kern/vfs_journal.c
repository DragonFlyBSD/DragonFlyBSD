/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/kern/vfs_journal.c,v 1.4 2004/12/30 21:41:04 dillon Exp $
 */
/*
 * Each mount point may have zero or more independantly configured journals
 * attached to it.  Each journal is represented by a memory FIFO and worker
 * thread.  Journal events are streamed through the FIFO to the thread,
 * batched up (typically on one-second intervals), and written out by the
 * thread. 
 *
 * Journal vnode ops are executed instead of mnt_vn_norm_ops when one or
 * more journals have been installed on a mount point.  It becomes the
 * responsibility of the journal op to call the underlying normal op as
 * appropriate.
 *
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
#include <sys/file.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

#include <sys/file2.h>
#include <sys/thread2.h>

static int journal_attach(struct mount *mp);
static void journal_detach(struct mount *mp);
static int journal_install_vfs_journal(struct mount *mp, struct file *fp,
			    const struct mountctl_install_journal *info);
static int journal_remove_vfs_journal(struct mount *mp,
			    const struct mountctl_remove_journal *info);
static int journal_resync_vfs_journal(struct mount *mp, const void *ctl);
static void journal_thread(void *info);

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

static void jrecord_init(struct journal *jo, 
			    struct jrecord *jrec, int16_t streamid);
static struct journal_subrecord *jrecord_push(
			    struct jrecord *jrec, int16_t rectype);
static void jrecord_pop(struct jrecord *jrec, struct journal_subrecord *parent);
static struct journal_subrecord *jrecord_write(struct jrecord *jrec,
			    int16_t rectype, int bytes);
static void jrecord_data(struct jrecord *jrec, const void *buf, int bytes);
static void jrecord_done(struct jrecord *jrec, int abortit);

static void jrecord_write_path(struct jrecord *jrec, 
			    int16_t rectype, struct namecache *ncp);
static void jrecord_write_vattr(struct jrecord *jrec, struct vattr *vat);


static int journal_nmkdir(struct vop_nmkdir_args *ap);

static struct vnodeopv_entry_desc journal_vnodeop_entries[] = {
    { &vop_default_desc,		vop_journal_operate_ap },
    { &vop_mountctl_desc,		(void *)journal_mountctl },
    { &vop_nmkdir_desc,			(void *)journal_nmkdir },
    { NULL, NULL }
};

static MALLOC_DEFINE(M_JOURNAL, "journal", "Journaling structures");
static MALLOC_DEFINE(M_JFIFO, "journal-fifo", "Journal FIFO");

int
journal_mountctl(struct vop_mountctl_args *ap)
{
    struct mount *mp;
    int error = 0;

    mp = ap->a_head.a_ops->vv_mount;
    KKASSERT(mp);

    if (mp->mnt_vn_journal_ops == NULL) {
	switch(ap->a_op) {
	case MOUNTCTL_INSTALL_VFS_JOURNAL:
	    error = journal_attach(mp);
	    if (error == 0 && ap->a_ctllen != sizeof(struct mountctl_install_journal))
		error = EINVAL;
	    if (error == 0 && ap->a_fp == NULL)
		error = EBADF;
	    if (error == 0)
		error = journal_install_vfs_journal(mp, ap->a_fp, ap->a_ctl);
	    if (TAILQ_EMPTY(&mp->mnt_jlist))
		journal_detach(mp);
	    break;
	case MOUNTCTL_REMOVE_VFS_JOURNAL:
	case MOUNTCTL_RESYNC_VFS_JOURNAL:
	    error = EINVAL;
	    break;
	default:
	    error = EOPNOTSUPP;
	    break;
	}
    } else {
	switch(ap->a_op) {
	case MOUNTCTL_INSTALL_VFS_JOURNAL:
	    if (ap->a_ctllen != sizeof(struct mountctl_install_journal))
		error = EINVAL;
	    if (error == 0 && ap->a_fp == NULL)
		error = EBADF;
	    if (error == 0)
		error = journal_install_vfs_journal(mp, ap->a_fp, ap->a_ctl);
	    break;
	case MOUNTCTL_REMOVE_VFS_JOURNAL:
	    if (ap->a_ctllen != sizeof(struct mountctl_remove_journal))
		error = EINVAL;
	    if (error == 0)
		error = journal_remove_vfs_journal(mp, ap->a_ctl);
	    if (TAILQ_EMPTY(&mp->mnt_jlist))
		journal_detach(mp);
	    break;
	case MOUNTCTL_RESYNC_VFS_JOURNAL:
	    if (ap->a_ctllen != 0)
		error = EINVAL;
	    error = journal_resync_vfs_journal(mp, ap->a_ctl);
	    break;
	default:
	    error = EOPNOTSUPP;
	    break;
	}
    }
    return (error);
}

/*
 * High level mount point setup.  When a 
 */
static int
journal_attach(struct mount *mp)
{
    vfs_add_vnodeops(mp, &mp->mnt_vn_journal_ops, journal_vnodeop_entries);
    return(0);
}

static void
journal_detach(struct mount *mp)
{
    if (mp->mnt_vn_journal_ops)
	vfs_rm_vnodeops(&mp->mnt_vn_journal_ops);
}

/*
 * Install a journal on a mount point.  Each journal has an associated worker
 * thread which is responsible for buffering and spooling the data to the
 * target.  A mount point may have multiple journals attached to it.  An
 * initial start record is generated when the journal is associated.
 */
static int
journal_install_vfs_journal(struct mount *mp, struct file *fp, 
			    const struct mountctl_install_journal *info)
{
    struct journal *jo;
    struct jrecord jrec;
    int error = 0;
    int size;

    jo = malloc(sizeof(struct journal), M_JOURNAL, M_WAITOK|M_ZERO);
    bcopy(info->id, jo->id, sizeof(jo->id));
    jo->flags = info->flags & ~(MC_JOURNAL_ACTIVE | MC_JOURNAL_STOP_REQ);

    /*
     * Memory FIFO size, round to nearest power of 2
     */
    if (info->membufsize) {
	if (info->membufsize < 65536)
	    size = 65536;
	else if (info->membufsize > 128 * 1024 * 1024)
	    size = 128 * 1024 * 1024;
	else
	    size = (int)info->membufsize;
    } else {
	size = 1024 * 1024;
    }
    jo->fifo.size = 1;
    while (jo->fifo.size < size)
	jo->fifo.size <<= 1;

    /*
     * Other parameters.  If not specified the starting transaction id
     * will be the current date.
     */
    if (info->transid) {
	jo->transid = info->transid;
    } else {
	struct timespec ts;
	getnanotime(&ts);
	jo->transid = ((int64_t)ts.tv_sec << 30) | ts.tv_nsec;
    }

    jo->fp = fp;

    /*
     * Allocate the memory FIFO
     */
    jo->fifo.mask = jo->fifo.size - 1;
    jo->fifo.membase = malloc(jo->fifo.size, M_JFIFO, M_WAITOK|M_ZERO|M_NULLOK);
    if (jo->fifo.membase == NULL)
	error = ENOMEM;

    /*
     * Create the worker thread and generate the association record.
     */
    if (error) {
	free(jo, M_JOURNAL);
    } else {
	fhold(fp);
	jo->flags |= MC_JOURNAL_ACTIVE;
	lwkt_create(journal_thread, jo, NULL, &jo->thread,
			TDF_STOPREQ, -1, "journal %.*s", JIDMAX, jo->id);
	lwkt_setpri(&jo->thread, TDPRI_KERN_DAEMON);
	lwkt_schedule(&jo->thread);

	jrecord_init(jo, &jrec, JREC_STREAMID_DISCONT);
	jrecord_write(&jrec, JTYPE_ASSOCIATE, 0);
	jrecord_done(&jrec, 0);
	TAILQ_INSERT_TAIL(&mp->mnt_jlist, jo, jentry);
    }
    return(error);
}

/*
 * Disassociate a journal from a mount point and terminate its worker thread.
 * A final termination record is written out before the file pointer is
 * dropped.
 */
static int
journal_remove_vfs_journal(struct mount *mp, 
			   const struct mountctl_remove_journal *info)
{
    struct journal *jo;
    struct jrecord jrec;
    int error;

    TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	if (bcmp(jo->id, info->id, sizeof(jo->id)) == 0)
	    break;
    }
    if (jo) {
	error = 0;
	TAILQ_REMOVE(&mp->mnt_jlist, jo, jentry);

	jrecord_init(jo, &jrec, JREC_STREAMID_DISCONT);
	jrecord_write(&jrec, JTYPE_DISASSOCIATE, 0);
	jrecord_done(&jrec, 0);

	jo->flags |= MC_JOURNAL_STOP_REQ | (info->flags & MC_JOURNAL_STOP_IMM);
	wakeup(&jo->fifo);
	while (jo->flags & MC_JOURNAL_ACTIVE) {
	    tsleep(jo, 0, "jwait", 0);
	}
	lwkt_free_thread(&jo->thread); /* XXX SMP */
	if (jo->fp)
	    fdrop(jo->fp, curthread);
	if (jo->fifo.membase)
	    free(jo->fifo.membase, M_JFIFO);
	free(jo, M_JOURNAL);
    } else {
	error = EINVAL;
    }
    return (error);
}

static int
journal_resync_vfs_journal(struct mount *mp, const void *ctl)
{
    return(EINVAL);
}

/*
 * The per-journal worker thread is responsible for writing out the
 * journal's FIFO to the target stream.
 */
static void
journal_thread(void *info)
{
    struct journal *jo = info;
    struct journal_rawrecbeg *rawp;
    int bytes;
    int error;
    int avail;
    int res;

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
	rawp = (void *)(jo->fifo.membase + (jo->fifo.rindex & jo->fifo.mask));
	if (rawp->begmagic == JREC_INCOMPLETEMAGIC) {
	    tsleep(&jo->fifo, 0, "jpad", hz);
	    continue;
	}
	if (rawp->streamid == JREC_STREAMID_PAD) {
	    jo->fifo.rindex += (rawp->recsize + 15) & ~15;
	    KKASSERT(jo->fifo.windex - jo->fifo.rindex > 0);
	    continue;
	}

	/*
	 * Figure out how much we can write out, beware the buffer wrap
	 * case.
	 */
	res = 0;
	avail = jo->fifo.size - (jo->fifo.rindex & jo->fifo.mask);
	while (res < bytes && rawp->begmagic == JREC_BEGMAGIC) {
	    res += (rawp->recsize + 15) & ~15;
	    if (res >= avail) {
		KKASSERT(res == avail);
		break;
	    }
	}

	/*
	 * Issue the write and deal with any errors or other conditions.
	 * For now assume blocking I/O.  Since we are record-aware the
	 * code cannot yet handle partial writes.
	 *
	 * XXX EWOULDBLOCK/NBIO
	 * XXX notification on failure
	 * XXX two-way acknowledgement stream in the return direction / xindex
	 */
	bytes = res;
	error = fp_write(jo->fp, 
			jo->fifo.membase + (jo->fifo.rindex & jo->fifo.mask),
			bytes, &res);
	if (error) {
	    printf("journal_thread(%s) write, error %d\n", jo->id, error);
	    /* XXX */
	} else {
	    KKASSERT(res == bytes);
	    printf("journal_thread(%s) write %d\n", jo->id, res);
	}

	/*
	 * Advance rindex.  XXX for now also advance xindex, which will
	 * eventually be advanced when the target acknowledges the sequence
	 * space.
	 */
	jo->fifo.rindex += bytes;
	jo->fifo.xindex += bytes;
	if (jo->flags & MC_JOURNAL_WWAIT) {
	    jo->flags &= ~MC_JOURNAL_WWAIT;	/* XXX hysteresis */
	    wakeup(&jo->fifo.windex);
	}
    }
    jo->flags &= ~MC_JOURNAL_ACTIVE;
    wakeup(jo);
    wakeup(&jo->fifo.windex);
}

static __inline
void
journal_build_pad(struct journal_rawrecbeg *rawp, int recsize)
{
    struct journal_rawrecend *rendp;
    
    KKASSERT((recsize & 15) == 0 && recsize >= 16);

    rawp->begmagic = JREC_BEGMAGIC;
    rawp->streamid = JREC_STREAMID_PAD;
    rawp->recsize = recsize;	/* must be 16-byte aligned */
    rawp->seqno = 0;
    /*
     * WARNING, rendp may overlap rawp->seqno.  This is necessary to
     * allow PAD records to fit in 16 bytes.  Use cpu_mb1() to
     * hopefully cause the compiler to not make any assumptions.
     */
    cpu_mb1();
    rendp = (void *)((char *)rawp + rawp->recsize - sizeof(*rendp));
    rendp->endmagic = JREC_ENDMAGIC;
    rendp->check = 0;
    rendp->recsize = rawp->recsize;
}

/*
 * Wake up the worker thread if the FIFO is more then half full or if
 * someone is waiting for space to be freed up.  Otherwise let the 
 * heartbeat deal with it.  Being able to avoid waking up the worker
 * is the key to the journal's cpu efficiency.
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
 * making this call.
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
	 * Note that availtoend is not truncated to avail and so cannot be
	 * used to determine whether the reservation is possible by itself.
	 * Also, since all fifo ops are 16-byte aligned, we can check
	 * the size before calculating the aligned size.
	 */
	availtoend = jo->fifo.size - (jo->fifo.windex & jo->fifo.mask);
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
	 */
	rawp = (void *)(jo->fifo.membase + (jo->fifo.windex & jo->fifo.mask));
	if (req != bytes) {
	    journal_build_pad(rawp, req - bytes);
	    rawp = (void *)jo->fifo.membase;
	}
	rawp->begmagic = JREC_INCOMPLETEMAGIC;	/* updated by abort/commit */
	rawp->recsize = bytes;			/* (unaligned size) */
	rawp->streamid = streamid | JREC_STREAMCTL_BEGIN;
	rawp->seqno = 0;			/* set by caller */

	/*
	 * Issue a memory barrier to guarentee that the record data has been
	 * properly initialized before we advance the write index and return
	 * a pointer to the reserved record.  Otherwise the worker thread
	 * could accidently run past us.
	 *
	 * Note that stream records are always 16-byte aligned.
	 */
	cpu_mb1();
	jo->fifo.windex += (req + 15) & ~15;
	*rawpp = rawp;
	return(rawp + 1);
    }
    /* not reached */
    *rawpp = NULL;
    return(NULL);
}

/*
 * Extend a previous reservation by the specified number of payload bytes.
 * If it is not possible to extend the existing reservation due to either
 * another thread having reserved space after us or due to a boundary
 * condition, the current reservation will be committed and possibly
 * truncated and a new reservation with the specified payload size will
 * be created. *rawpp is set to the new reservation in this case but the
 * caller cannot depend on a comparison with the old rawp to determine if
 * this case occurs because we could end up using the same memory FIFO
 * offset for the new stream record.
 *
 * In either case this function will return a pointer to the base of the
 * extended payload space.
 *
 * If a new stream block is created the caller needs to recalculate payload
 * byte counts, if the same stream block is used the caller needs to extend
 * its current notion of the payload byte count.
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
     * If the aligned record size does not change we can trivially extend
     * the record.
     */
    if (nsize == osize) {
	rawp->recsize += bytes;
	return((char *)rawp + rawp->recsize - bytes);
    }

    /*
     * If the fifo's write index hasn't been modified since we made the
     * reservation and we do not hit any boundary conditions, we can 
     * trivially extend the record.
     */
    if ((jo->fifo.windex & jo->fifo.mask) == wbase + osize) {
	availtoend = jo->fifo.size - wbase;
	avail = jo->fifo.size - (jo->fifo.windex - jo->fifo.xindex) + osize;
	KKASSERT((availtoend & 15) == 0);
	KKASSERT((avail & 15) == 0);
	if (nsize <= avail && nsize <= availtoend) {
	    jo->fifo.windex += nsize - osize;
	    rawp->recsize += bytes;
	    return((char *)rawp + rawp->recsize - bytes);
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
     * Truncate the record if requested.  If the FIFO write index as still
     * at the end of our record we can optimally backindex it.  Otherwise
     * we have to insert a pad record.
     *
     * We calculate osize which is the 16-byte-aligned original recsize.
     * We calculate nsize which is the 16-byte-aligned new recsize.
     *
     * Due to alignment issues or in case the passed truncation bytes is
     * the same as the original payload, windex will be equal to nindex.
     */
    if (bytes >= 0) {
	KKASSERT(bytes >= 0 && bytes <= rawp->recsize - sizeof(struct journal_rawrecbeg) - sizeof(struct journal_rawrecend));
	osize = (rawp->recsize + 15) & ~15;
	rawp->recsize = bytes + sizeof(struct journal_rawrecbeg) +
			sizeof(struct journal_rawrecend);
	nsize = (rawp->recsize + 15) & ~15;
	if (osize == nsize) {
	    /* do nothing */
	} else if ((jo->fifo.windex & jo->fifo.mask) == (char *)rawp - jo->fifo.membase + osize) {
	    /* we are able to backindex the fifo */
	    jo->fifo.windex -= osize - nsize;
	} else {
	    /* we cannot backindex the fifo, emplace a pad in the dead space */
	    journal_build_pad((void *)((char *)rawp + osize), osize - nsize);
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
    cpu_mb1();			/* memory barrier */
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

static int16_t sid = JREC_STREAMID_JMIN;

/*
 * Initialize the passed jrecord structure and start a new stream transaction
 * by reserving an initial build space in the journal's memory FIFO.
 */
static void
jrecord_init(struct journal *jo, struct jrecord *jrec, int16_t streamid)
{
    bzero(jrec, sizeof(*jrec));
    jrec->jo = jo;
    if (streamid < 0) {
	streamid = sid++;	/* XXX need to track stream ids! */
	if (sid == JREC_STREAMID_JMAX)
	    sid = JREC_STREAMID_JMIN;
    }
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
static 
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
static void
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
 * Write a leaf record out and return a pointer to its base.  The leaf
 * record may contain potentially megabytes of data which is supplied
 * in jrecord_data() calls.  The exact amount must be specified in this
 * call.
 */
static
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
	    jrec->stream_reserved = JREC_DEFAULTSIZE;
	    jrec->stream_residual = JREC_DEFAULTSIZE;
	    jrec->parent = NULL;	/* no longer accessible */
	    jrec->pushptrgood = 0;	/* restored parents in pops no good */
	} else {
	    jrec->stream_reserved += JREC_DEFAULTSIZE;
	    jrec->stream_residual += JREC_DEFAULTSIZE;
	}
    }
    last = (void *)jrec->stream_ptr;
    last->rectype = rectype;
    last->reserved = 0;
    last->recsize = sizeof(struct journal_subrecord) + bytes;
    jrec->last = last;
    jrec->residual = bytes;		/* remaining data to be posted */
    jrec->residual_align = -bytes & 7;	/* post-data alignment required */
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
jrecord_data(struct jrecord *jrec, const void *buf, int bytes)
{
    int pusheditout;
    int extsize;

    KKASSERT(bytes >= 0 && bytes <= jrec->residual);

    /*
     * Push out stream records as long as there is insufficient room to hold
     * the remaining data.
     */
    while (jrec->stream_residual < bytes) {
	/*
	 * Fill in any remaining space in the current stream record.
	 */
	bcopy(buf, jrec->stream_ptr, jrec->stream_residual);
	buf = (const char *)buf + jrec->stream_residual;
	bytes -= jrec->stream_residual;
	/*jrec->stream_ptr += jrec->stream_residual;*/
	jrec->stream_residual = 0;
	jrec->residual -= jrec->stream_residual;

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
	bcopy(buf, jrec->stream_ptr, bytes);
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
 * We are finished with a transaction.  If abortit is not set then we must
 * be at the top level with no residual subrecord data left to output.
 * If abortit is set then we can be in any state.
 *
 * The stream record will be committed or aborted as specified and jrecord
 * resources will be cleaned up.
 */
static void
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
 *			LEAF RECORD SUPPORT ROUTINES			*
 ************************************************************************
 *
 * These routine create leaf subrecords representing common filesystem
 * structures.
 */

static void
jrecord_write_path(struct jrecord *jrec, int16_t rectype, struct namecache *ncp)
{
}

static void
jrecord_write_vattr(struct jrecord *jrec, struct vattr *vat)
{
}

/************************************************************************
 *			JOURNAL VNOPS					*
 ************************************************************************/

static
int
journal_nmkdir(struct vop_nmkdir_args *ap)
{
    struct mount *mp;
    struct journal *jo;
    struct jrecord jrec;
    void *save;		/* warning, save pointers do not always remain valid */
    int error;

    error = vop_journal_operate_ap(&ap->a_head);
    mp = ap->a_head.a_ops->vv_mount;
    if (error == 0) {
	TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	    jrecord_init(jo, &jrec, -1);
	    if (jo->flags & MC_JOURNAL_WANT_REVERSABLE) {
		save = jrecord_push(&jrec, JTYPE_UNDO);
		/* XXX undo operations */
		jrecord_pop(&jrec, save);
	    }
#if 0
	    if (jo->flags & MC_JOURNAL_WANT_AUDIT) {
		jrecord_write_audit(&jrec);
	    }
#endif
	    save = jrecord_push(&jrec, JTYPE_MKDIR);
	    jrecord_write_path(&jrec, JLEAF_PATH1, ap->a_ncp);
	    jrecord_write_vattr(&jrec, ap->a_vap);
	    jrecord_pop(&jrec, save);
	    jrecord_done(&jrec, 0);
	}
    }
    return (error);
}

