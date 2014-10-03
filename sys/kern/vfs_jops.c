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
 * $DragonFly: src/sys/kern/vfs_jops.c,v 1.36 2007/08/21 17:43:52 dillon Exp $
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

static int journal_attach(struct mount *mp);
static void journal_detach(struct mount *mp);
static int journal_install_vfs_journal(struct mount *mp, struct file *fp,
			    const struct mountctl_install_journal *info);
static int journal_restart_vfs_journal(struct mount *mp, struct file *fp,
			    const struct mountctl_restart_journal *info);
static int journal_remove_vfs_journal(struct mount *mp,
			    const struct mountctl_remove_journal *info);
static int journal_restart(struct mount *mp, struct file *fp,
			    struct journal *jo, int flags);
static int journal_destroy(struct mount *mp, struct journal *jo, int flags);
static int journal_resync_vfs_journal(struct mount *mp, const void *ctl);
static int journal_status_vfs_journal(struct mount *mp,
		       const struct mountctl_status_journal *info,
		       struct mountctl_journal_ret_status *rstat,
		       int buflen, int *res);

static void jrecord_undo_file(struct jrecord *jrec, struct vnode *vp,
			     int jrflags, off_t off, off_t bytes);

static int journal_setattr(struct vop_setattr_args *ap);
static int journal_write(struct vop_write_args *ap);
static int journal_fsync(struct vop_fsync_args *ap);
static int journal_putpages(struct vop_putpages_args *ap);
static int journal_setacl(struct vop_setacl_args *ap);
static int journal_setextattr(struct vop_setextattr_args *ap);
static int journal_ncreate(struct vop_ncreate_args *ap);
static int journal_nmknod(struct vop_nmknod_args *ap);
static int journal_nlink(struct vop_nlink_args *ap);
static int journal_nsymlink(struct vop_nsymlink_args *ap);
static int journal_nwhiteout(struct vop_nwhiteout_args *ap);
static int journal_nremove(struct vop_nremove_args *ap);
static int journal_nmkdir(struct vop_nmkdir_args *ap);
static int journal_nrmdir(struct vop_nrmdir_args *ap);
static int journal_nrename(struct vop_nrename_args *ap);

#define JRUNDO_SIZE	0x00000001
#define JRUNDO_UID	0x00000002
#define JRUNDO_GID	0x00000004
#define JRUNDO_FSID	0x00000008
#define JRUNDO_MODES	0x00000010
#define JRUNDO_INUM	0x00000020
#define JRUNDO_ATIME	0x00000040
#define JRUNDO_MTIME	0x00000080
#define JRUNDO_CTIME	0x00000100
#define JRUNDO_GEN	0x00000200
#define JRUNDO_FLAGS	0x00000400
#define JRUNDO_UDEV	0x00000800
#define JRUNDO_NLINK	0x00001000
#define JRUNDO_FILEDATA	0x00010000
#define JRUNDO_GETVP	0x00020000
#define JRUNDO_CONDLINK	0x00040000	/* write file data if link count 1 */
#define JRUNDO_VATTR	(JRUNDO_SIZE|JRUNDO_UID|JRUNDO_GID|JRUNDO_FSID|\
			 JRUNDO_MODES|JRUNDO_INUM|JRUNDO_ATIME|JRUNDO_MTIME|\
			 JRUNDO_CTIME|JRUNDO_GEN|JRUNDO_FLAGS|JRUNDO_UDEV|\
			 JRUNDO_NLINK)
#define JRUNDO_ALL	(JRUNDO_VATTR|JRUNDO_FILEDATA)

static struct vop_ops journal_vnode_vops = {
    .vop_default =	vop_journal_operate_ap,
    .vop_mountctl =	journal_mountctl,
    .vop_setattr =	journal_setattr,
    .vop_write =	journal_write,
    .vop_fsync =	journal_fsync,
    .vop_putpages =	journal_putpages,
    .vop_setacl =	journal_setacl,
    .vop_setextattr =	journal_setextattr,
    .vop_ncreate =	journal_ncreate,
    .vop_nmknod =	journal_nmknod,
    .vop_nlink =	journal_nlink,
    .vop_nsymlink =	journal_nsymlink,
    .vop_nwhiteout =	journal_nwhiteout,
    .vop_nremove =	journal_nremove,
    .vop_nmkdir =	journal_nmkdir,
    .vop_nrmdir =	journal_nrmdir,
    .vop_nrename =	journal_nrename
};

int
journal_mountctl(struct vop_mountctl_args *ap)
{
    struct mount *mp;
    int error = 0;

    mp = ap->a_head.a_ops->head.vv_mount;
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
	case MOUNTCTL_RESTART_VFS_JOURNAL:
	case MOUNTCTL_REMOVE_VFS_JOURNAL:
	case MOUNTCTL_RESYNC_VFS_JOURNAL:
	case MOUNTCTL_STATUS_VFS_JOURNAL:
	    error = ENOENT;
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
	case MOUNTCTL_RESTART_VFS_JOURNAL:
	    if (ap->a_ctllen != sizeof(struct mountctl_restart_journal))
		error = EINVAL;
	    if (error == 0 && ap->a_fp == NULL)
		error = EBADF;
	    if (error == 0)
		error = journal_restart_vfs_journal(mp, ap->a_fp, ap->a_ctl);
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
	case MOUNTCTL_STATUS_VFS_JOURNAL:
	    if (ap->a_ctllen != sizeof(struct mountctl_status_journal))
		error = EINVAL;
	    if (error == 0) {
		error = journal_status_vfs_journal(mp, ap->a_ctl, 
					ap->a_buf, ap->a_buflen, ap->a_res);
	    }
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
    KKASSERT(mp->mnt_jbitmap == NULL);
    vfs_add_vnodeops(mp, &journal_vnode_vops, &mp->mnt_vn_journal_ops);
    mp->mnt_jbitmap = kmalloc(JREC_STREAMID_JMAX/8, M_JOURNAL, M_WAITOK|M_ZERO);
    mp->mnt_streamid = JREC_STREAMID_JMIN;
    return(0);
}

static void
journal_detach(struct mount *mp)
{
    KKASSERT(mp->mnt_jbitmap != NULL);
    if (mp->mnt_vn_journal_ops)
	vfs_rm_vnodeops(mp, &journal_vnode_vops, &mp->mnt_vn_journal_ops);
    kfree(mp->mnt_jbitmap, M_JOURNAL);
    mp->mnt_jbitmap = NULL;
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

    jo = kmalloc(sizeof(struct journal), M_JOURNAL, M_WAITOK|M_ZERO);
    bcopy(info->id, jo->id, sizeof(jo->id));
    jo->flags = info->flags & ~(MC_JOURNAL_WACTIVE | MC_JOURNAL_RACTIVE |
				MC_JOURNAL_STOP_REQ);

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
    jo->fifo.membase = kmalloc(jo->fifo.size, M_JFIFO, M_WAITOK|M_ZERO|M_NULLOK);
    if (jo->fifo.membase == NULL)
	error = ENOMEM;

    /*
     * Create the worker threads and generate the association record.
     */
    if (error) {
	kfree(jo, M_JOURNAL);
    } else {
	fhold(fp);
	journal_create_threads(jo);
	jrecord_init(jo, &jrec, JREC_STREAMID_DISCONT);
	jrecord_write(&jrec, JTYPE_ASSOCIATE, 0);
	jrecord_done(&jrec, 0);
	TAILQ_INSERT_TAIL(&mp->mnt_jlist, jo, jentry);
    }
    return(error);
}

/*
 * Restart a journal with a new descriptor.   The existing reader and writer
 * threads are terminated and a new descriptor is associated with the
 * journal.  The FIFO rindex is reset to xindex and the threads are then
 * restarted.
 */
static int
journal_restart_vfs_journal(struct mount *mp, struct file *fp,
			   const struct mountctl_restart_journal *info)
{
    struct journal *jo;
    int error;

    TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	if (bcmp(jo->id, info->id, sizeof(jo->id)) == 0)
	    break;
    }
    if (jo)
	error = journal_restart(mp, fp, jo, info->flags);
    else
	error = EINVAL;
    return (error);
}

static int
journal_restart(struct mount *mp, struct file *fp, 
		struct journal *jo, int flags)
{
    /*
     * XXX lock the jo
     */

#if 0
    /*
     * Record the fact that we are doing a restart in the journal.
     * XXX it isn't safe to do this if the journal is being restarted
     * because it was locked up and the writer thread has already exited.
     */
    jrecord_init(jo, &jrec, JREC_STREAMID_RESTART);
    jrecord_write(&jrec, JTYPE_DISASSOCIATE, 0);
    jrecord_done(&jrec, 0);
#endif

    /*
     * Stop the reader and writer threads and clean up the current 
     * descriptor.
     */
    kprintf("RESTART WITH FP %p KILLING %p\n", fp, jo->fp);
    journal_destroy_threads(jo, flags);

    if (jo->fp)
	fdrop(jo->fp);

    /*
     * Associate the new descriptor, reset the FIFO index, and recreate
     * the threads.
     */
    fhold(fp);
    jo->fp = fp;
    jo->fifo.rindex = jo->fifo.xindex;
    journal_create_threads(jo);

    return(0);
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
    int error;

    TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	if (bcmp(jo->id, info->id, sizeof(jo->id)) == 0)
	    break;
    }
    if (jo)
	error = journal_destroy(mp, jo, info->flags);
    else
	error = EINVAL;
    return (error);
}

/*
 * Remove all journals associated with a mount point.  Usually called
 * by the umount code.
 */
void
journal_remove_all_journals(struct mount *mp, int flags)
{
    struct journal *jo;

    while ((jo = TAILQ_FIRST(&mp->mnt_jlist)) != NULL) {
	journal_destroy(mp, jo, flags);
    }
}

static int
journal_destroy(struct mount *mp, struct journal *jo, int flags)
{
    struct jrecord jrec;

    TAILQ_REMOVE(&mp->mnt_jlist, jo, jentry);

    jrecord_init(jo, &jrec, JREC_STREAMID_DISCONT);
    jrecord_write(&jrec, JTYPE_DISASSOCIATE, 0);
    jrecord_done(&jrec, 0);

    journal_destroy_threads(jo, flags);

    if (jo->fp)
	fdrop(jo->fp);
    if (jo->fifo.membase)
	kfree(jo->fifo.membase, M_JFIFO);
    kfree(jo, M_JOURNAL);

    return(0);
}

static int
journal_resync_vfs_journal(struct mount *mp, const void *ctl)
{
    return(EINVAL);
}

static int
journal_status_vfs_journal(struct mount *mp, 
		       const struct mountctl_status_journal *info,
		       struct mountctl_journal_ret_status *rstat,
		       int buflen, int *res)
{
    struct journal *jo;
    int error = 0;
    int index;

    index = 0;
    *res = 0;
    TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	if (info->index == MC_JOURNAL_INDEX_ID) {
	    if (bcmp(jo->id, info->id, sizeof(jo->id)) != 0)
		continue;
	} else if (info->index >= 0) {
	    if (info->index < index)
		continue;
	} else if (info->index != MC_JOURNAL_INDEX_ALL) {
	    continue;
	}
	if (buflen < sizeof(*rstat)) {
	    if (*res)
		rstat[-1].flags |= MC_JOURNAL_STATUS_MORETOCOME;
	    else
		error = EINVAL;
	    break;
	}
	bzero(rstat, sizeof(*rstat));
	rstat->recsize = sizeof(*rstat);
	bcopy(jo->id, rstat->id, sizeof(jo->id));
	rstat->index = index;
	rstat->membufsize = jo->fifo.size;
	rstat->membufused = jo->fifo.windex - jo->fifo.xindex;
	rstat->membufunacked = jo->fifo.rindex - jo->fifo.xindex;
	rstat->bytessent = jo->total_acked;
	rstat->fifostalls = jo->fifostalls;
	++rstat;
	++index;
	*res += sizeof(*rstat);
	buflen -= sizeof(*rstat);
    }
    return(error);
}

/************************************************************************
 *			PARALLEL TRANSACTION SUPPORT ROUTINES		*
 ************************************************************************
 *
 * JRECLIST_*() - routines which create and iterate over jrecord structures,
 *		  because a mount point may have multiple attached journals.
 */

/*
 * Initialize the passed jrecord_list and create a jrecord for each 
 * journal we need to write to.  Unnecessary mallocs are avoided by
 * using the passed jrecord structure as the first jrecord in the list.
 * A starting transaction is pushed for each jrecord.
 *
 * Returns non-zero if any of the journals require undo records.
 */
static
int
jreclist_init(struct mount *mp, struct jrecord_list *jreclist, 
	      struct jrecord *jreccache, int16_t rectype)
{
    struct journal *jo;
    struct jrecord *jrec;
    int wantrev;
    int count;
    int16_t streamid;

    TAILQ_INIT(&jreclist->list);

    /*
     * Select the stream ID to use for the transaction.  We must select
     * a stream ID that is not currently in use by some other parallel
     * transaction.  
     *
     * Don't bother calculating the next streamid when reassigning
     * mnt_streamid, since parallel transactions are fairly rare.  This
     * also allows someone observing the raw records to clearly see
     * when parallel transactions occur.
     */
    streamid = mp->mnt_streamid;
    count = 0;
    while (mp->mnt_jbitmap[streamid >> 3] & (1 << (streamid & 7))) {
	if (++streamid == JREC_STREAMID_JMAX)
		streamid = JREC_STREAMID_JMIN;
	if (++count == JREC_STREAMID_JMAX - JREC_STREAMID_JMIN) {
		kprintf("jreclist_init: all streamid's in use! sleeping\n");
		tsleep(jreclist, 0, "jsidfl", hz * 10);
		count = 0;
	}
    }
    mp->mnt_jbitmap[streamid >> 3] |= 1 << (streamid & 7);
    mp->mnt_streamid = streamid;
    jreclist->streamid = streamid;

    /*
     * Now initialize a stream on each journal.
     */
    count = 0;
    wantrev = 0;
    TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	if (count == 0)
	    jrec = jreccache;
	else
	    jrec = kmalloc(sizeof(*jrec), M_JOURNAL, M_WAITOK);
	jrecord_init(jo, jrec, streamid);
	jrec->user_save = jrecord_push(jrec, rectype);
	TAILQ_INSERT_TAIL(&jreclist->list, jrec, user_entry);
	if (jo->flags & MC_JOURNAL_WANT_REVERSABLE)
	    wantrev = 1;
	++count;
    }
    return(wantrev);
}

/*
 * Terminate the journaled transactions started by jreclist_init().  If
 * an error occured, the transaction records will be aborted.
 */
static
void
jreclist_done(struct mount *mp, struct jrecord_list *jreclist, int error)
{
    struct jrecord *jrec;
    int count;

    /*
     * Cleanup the jrecord state on each journal.
     */
    TAILQ_FOREACH(jrec, &jreclist->list, user_entry) {
	jrecord_pop(jrec, jrec->user_save);
	jrecord_done(jrec, error);
    }

    /*
     * Free allocated jrec's (the first is always supplied)
     */
    count = 0;
    while ((jrec = TAILQ_FIRST(&jreclist->list)) != NULL) {
	TAILQ_REMOVE(&jreclist->list, jrec, user_entry);
	if (count)
	    kfree(jrec, M_JOURNAL);
	++count;
    }

    /*
     * Clear the streamid so it can be reused.
     */
    mp->mnt_jbitmap[jreclist->streamid >> 3] &= ~(1 << (jreclist->streamid & 7));
}

/*
 * This procedure writes out UNDO records for available reversable
 * journals.
 *
 * XXX could use improvement.  There is no need to re-read the file
 * for each journal.
 */
static
void
jreclist_undo_file(struct jrecord_list *jreclist, struct vnode *vp, 
		   int jrflags, off_t off, off_t bytes)
{
    struct jrecord *jrec;
    int error;

    error = 0;
    if (jrflags & JRUNDO_GETVP)
	error = vget(vp, LK_SHARED);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist->list, user_entry) {
	    if (jrec->jo->flags & MC_JOURNAL_WANT_REVERSABLE) {
		jrecord_undo_file(jrec, vp, jrflags, off, bytes);
	    }
	}
    }
    if (error == 0 && jrflags & JRUNDO_GETVP)
	vput(vp);
}

/************************************************************************
 *			LOW LEVEL UNDO SUPPORT ROUTINE			*
 ************************************************************************
 *
 * This function is used to support UNDO records.  It will generate an
 * appropriate record with the requested portion of the file data.  Note
 * that file data is only recorded if JRUNDO_FILEDATA is passed.  If bytes
 * is -1, it will be set to the size of the file.
 */
static void
jrecord_undo_file(struct jrecord *jrec, struct vnode *vp, int jrflags, 
		  off_t off, off_t bytes)
{
    struct vattr attr;
    void *save1; /* warning, save pointers do not always remain valid */
    void *save2;
    int error;

    /*
     * Setup.  Start the UNDO record, obtain a shared lock on the vnode,
     * and retrieve attribute info.
     */
    save1 = jrecord_push(jrec, JTYPE_UNDO);
    error = VOP_GETATTR(vp, &attr);
    if (error)
	goto done;

    /*
     * Generate UNDO records as requested.
     */
    if (jrflags & JRUNDO_VATTR) {
	save2 = jrecord_push(jrec, JTYPE_VATTR);
	jrecord_leaf(jrec, JLEAF_VTYPE, &attr.va_type, sizeof(attr.va_type));
	if ((jrflags & JRUNDO_NLINK) && attr.va_nlink != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_NLINK, &attr.va_nlink, sizeof(attr.va_nlink));
	if ((jrflags & JRUNDO_SIZE) && attr.va_size != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_SIZE, &attr.va_size, sizeof(attr.va_size));
	if ((jrflags & JRUNDO_UID) && attr.va_uid != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_UID, &attr.va_uid, sizeof(attr.va_uid));
	if ((jrflags & JRUNDO_GID) && attr.va_gid != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_GID, &attr.va_gid, sizeof(attr.va_gid));
	if ((jrflags & JRUNDO_FSID) && attr.va_fsid != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_FSID, &attr.va_fsid, sizeof(attr.va_fsid));
	if ((jrflags & JRUNDO_MODES) && attr.va_mode != (mode_t)VNOVAL)
	    jrecord_leaf(jrec, JLEAF_MODES, &attr.va_mode, sizeof(attr.va_mode));
	if ((jrflags & JRUNDO_INUM) && attr.va_fileid != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_INUM, &attr.va_fileid, sizeof(attr.va_fileid));
	if ((jrflags & JRUNDO_ATIME) && attr.va_atime.tv_sec != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_ATIME, &attr.va_atime, sizeof(attr.va_atime));
	if ((jrflags & JRUNDO_MTIME) && attr.va_mtime.tv_sec != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_MTIME, &attr.va_mtime, sizeof(attr.va_mtime));
	if ((jrflags & JRUNDO_CTIME) && attr.va_ctime.tv_sec != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_CTIME, &attr.va_ctime, sizeof(attr.va_ctime));
	if ((jrflags & JRUNDO_GEN) && attr.va_gen != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_GEN, &attr.va_gen, sizeof(attr.va_gen));
	if ((jrflags & JRUNDO_FLAGS) && attr.va_flags != VNOVAL)
	    jrecord_leaf(jrec, JLEAF_FLAGS, &attr.va_flags, sizeof(attr.va_flags));
	if ((jrflags & JRUNDO_UDEV) && attr.va_rmajor != VNOVAL) {
	    udev_t rdev = makeudev(attr.va_rmajor, attr.va_rminor);
	    jrecord_leaf(jrec, JLEAF_UDEV, &rdev, sizeof(rdev));
	    jrecord_leaf(jrec, JLEAF_UMAJOR, &attr.va_rmajor, sizeof(attr.va_rmajor));
	    jrecord_leaf(jrec, JLEAF_UMINOR, &attr.va_rminor, sizeof(attr.va_rminor));
	}
	jrecord_pop(jrec, save2);
    }

    /*
     * Output the file data being overwritten by reading the file and
     * writing it out to the journal prior to the write operation.  We
     * do not need to write out data past the current file EOF.
     *
     * XXX support JRUNDO_CONDLINK - do not write out file data for files
     * with a link count > 1.  The undo code needs to locate the inode and
     * regenerate the hardlink.
     */
    if ((jrflags & JRUNDO_FILEDATA) && attr.va_type == VREG) {
	if (attr.va_size != VNOVAL) {
	    if (bytes == -1)
		bytes = attr.va_size - off;
	    if (off + bytes > attr.va_size)
		bytes = attr.va_size - off;
	    if (bytes > 0)
		jrecord_file_data(jrec, vp, off, bytes);
	} else {
	    error = EINVAL;
	}
    }
    if ((jrflags & JRUNDO_FILEDATA) && attr.va_type == VLNK) {
	struct iovec aiov;
	struct uio auio;
	char *buf;

	buf = kmalloc(PATH_MAX, M_JOURNAL, M_WAITOK);
	aiov.iov_base = buf;
	aiov.iov_len = PATH_MAX;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = curthread;
	auio.uio_resid = PATH_MAX;
	error = VOP_READLINK(vp, &auio, proc0.p_ucred);
	if (error == 0) {
		jrecord_leaf(jrec, JLEAF_SYMLINKDATA, buf, 
				PATH_MAX - auio.uio_resid);
	}
	kfree(buf, M_JOURNAL);
    }
done:
    if (error)
	jrecord_leaf(jrec, JLEAF_ERROR, &error, sizeof(error));
    jrecord_pop(jrec, save1);
}

/************************************************************************
 *			JOURNAL VNOPS					*
 ************************************************************************
 *
 * These are function shims replacing the normal filesystem ops.  We become
 * responsible for calling the underlying filesystem ops.  We have the choice
 * of executing the underlying op first and then generating the journal entry,
 * or starting the journal entry, executing the underlying op, and then
 * either completing or aborting it.  
 *
 * The journal is supposed to be a high-level entity, which generally means
 * identifying files by name rather then by inode.  Supplying both allows
 * the journal to be used both for inode-number-compatible 'mirrors' and
 * for simple filesystem replication.
 *
 * Writes are particularly difficult to deal with because a single write may
 * represent a hundred megabyte buffer or more, and both writes and truncations
 * require the 'old' data to be written out as well as the new data if the
 * log is reversable.  Other issues:
 *
 * - How to deal with operations on unlinked files (no path available),
 *   but which may still be filesystem visible due to hard links.
 *
 * - How to deal with modifications made via a memory map.
 *
 * - Future cache coherency support will require cache coherency API calls
 *   both prior to and after the call to the underlying VFS.
 *
 * ALSO NOTE: We do not have to shim compatibility VOPs like MKDIR which have
 * new VFS equivalents (NMKDIR).
 */

/*
 * Journal vop_setattr { a_vp, a_vap, a_cred }
 */
static
int
journal_setattr(struct vop_setattr_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    if (jreclist_init(mp, &jreclist, &jreccache, JTYPE_SETATTR)) {
	jreclist_undo_file(&jreclist, ap->a_vp, JRUNDO_VATTR, 0, 0);
    }
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, curthread, ap->a_cred);
	    jrecord_write_vnode_ref(jrec, ap->a_vp);
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_vattr(jrec, ap->a_vap);
	    jrecord_pop(jrec, save);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_write { a_vp, a_uio, a_ioflag, a_cred }
 */
static
int
journal_write(struct vop_write_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    struct uio uio_copy;
    struct iovec uio_one_iovec;
    void *save;
    int error;

    /*
     * Special synchronizing writes for VM backing store do not supply any
     * real data
     */
    if (ap->a_uio->uio_segflg == UIO_NOCOPY) {
	    error = vop_journal_operate_ap(&ap->a_head);
	    return (error);
    }

    /*
     * This is really nasty.  UIO's don't retain sufficient information to
     * be reusable once they've gone through the VOP chain.  The iovecs get
     * cleared, so we have to copy the UIO.
     *
     * XXX fix the UIO code to not destroy iov's during a scan so we can
     *     reuse the uio over and over again.
     *
     * XXX UNDO code needs to journal the old data prior to the write.
     */
    uio_copy = *ap->a_uio;
    if (uio_copy.uio_iovcnt == 1) {
	uio_one_iovec = ap->a_uio->uio_iov[0];
	uio_copy.uio_iov = &uio_one_iovec;
    } else {
	uio_copy.uio_iov = kmalloc(uio_copy.uio_iovcnt * sizeof(struct iovec),
				    M_JOURNAL, M_WAITOK);
	bcopy(ap->a_uio->uio_iov, uio_copy.uio_iov, 
		uio_copy.uio_iovcnt * sizeof(struct iovec));
    }

    /*
     * Write out undo data.  Note that uio_offset is incorrect if
     * IO_APPEND is set, but fortunately we have no undo file data to
     * write out in that case.
     */
    mp = ap->a_head.a_ops->head.vv_mount;
    if (jreclist_init(mp, &jreclist, &jreccache, JTYPE_WRITE)) {
	if (ap->a_ioflag & IO_APPEND) {
	    jreclist_undo_file(&jreclist, ap->a_vp, JRUNDO_SIZE|JRUNDO_MTIME, 0, 0);
	} else {
	    jreclist_undo_file(&jreclist, ap->a_vp, 
			       JRUNDO_FILEDATA|JRUNDO_SIZE|JRUNDO_MTIME, 
			       uio_copy.uio_offset, uio_copy.uio_resid);
	}
    }
    error = vop_journal_operate_ap(&ap->a_head);

    /*
     * XXX bad hack to figure out the offset for O_APPEND writes (note: 
     * uio field state after the VFS operation).
     */
    uio_copy.uio_offset = ap->a_uio->uio_offset - 
			  (uio_copy.uio_resid - ap->a_uio->uio_resid);

    /*
     * Output the write data to the journal.
     */
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_vnode_ref(jrec, ap->a_vp);
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_uio(jrec, JLEAF_FILEDATA, &uio_copy);
	    jrecord_pop(jrec, save);
	}
    }
    jreclist_done(mp, &jreclist, error);

    if (uio_copy.uio_iov != &uio_one_iovec)
	kfree(uio_copy.uio_iov, M_JOURNAL);
    return (error);
}

/*
 * Journal vop_fsync { a_vp, a_waitfor }
 */
static
int
journal_fsync(struct vop_fsync_args *ap)
{
#if 0
    struct mount *mp;
    struct journal *jo;
#endif
    int error;

    error = vop_journal_operate_ap(&ap->a_head);
#if 0
    mp = ap->a_head.a_ops->head.vv_mount;
    if (error == 0) {
	TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	    /* XXX synchronize pending journal records */
	}
    }
#endif
    return (error);
}

/*
 * Journal vop_putpages { a_vp, a_m, a_count, a_sync, a_rtvals, a_offset }
 *
 * note: a_count is in bytes.
 */
static
int
journal_putpages(struct vop_putpages_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    if (jreclist_init(mp, &jreclist, &jreccache, JTYPE_PUTPAGES) && 
	ap->a_count > 0
    ) {
	jreclist_undo_file(&jreclist, ap->a_vp, 
			   JRUNDO_FILEDATA|JRUNDO_SIZE|JRUNDO_MTIME, 
			   ap->a_offset, btoc(ap->a_count));
    }
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0 && ap->a_count > 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_vnode_ref(jrec, ap->a_vp);
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_pagelist(jrec, JLEAF_FILEDATA, ap->a_m, ap->a_rtvals, 
				   btoc(ap->a_count), ap->a_offset);
	    jrecord_pop(jrec, save);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_setacl { a_vp, a_type, a_aclp, a_cred }
 */
static
int
journal_setacl(struct vop_setacl_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_SETACL);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
#if 0
	    if ((jo->flags & MC_JOURNAL_WANT_REVERSABLE))
		jrecord_undo_file(jrec, ap->a_vp, JRUNDO_XXX, 0, 0);
#endif
	    jrecord_write_cred(jrec, curthread, ap->a_cred);
	    jrecord_write_vnode_ref(jrec, ap->a_vp);
#if 0
	    save = jrecord_push(jrec, JTYPE_REDO);
	    /* XXX type, aclp */
	    jrecord_pop(jrec, save);
#endif
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_setextattr { a_vp, a_name, a_uio, a_cred }
 */
static
int
journal_setextattr(struct vop_setextattr_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_SETEXTATTR);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
#if 0
	    if ((jo->flags & MC_JOURNAL_WANT_REVERSABLE))
		jrecord_undo_file(jrec, ap->a_vp, JRUNDO_XXX, 0, 0);
#endif
	    jrecord_write_cred(jrec, curthread, ap->a_cred);
	    jrecord_write_vnode_ref(jrec, ap->a_vp);
	    jrecord_leaf(jrec, JLEAF_ATTRNAME, ap->a_attrname,
			strlen(ap->a_attrname));
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_uio(jrec, JLEAF_FILEDATA, ap->a_uio);
	    jrecord_pop(jrec, save);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_ncreate { a_nch, a_vpp, a_cred, a_vap }
 */
static
int
journal_ncreate(struct vop_ncreate_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_CREATE);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	    if (*ap->a_vpp)
		jrecord_write_vnode_ref(jrec, *ap->a_vpp);
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_vattr(jrec, ap->a_vap);
	    jrecord_pop(jrec, save);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nmknod { a_nch, a_vpp, a_cred, a_vap }
 */
static
int
journal_nmknod(struct vop_nmknod_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_MKNOD);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_vattr(jrec, ap->a_vap);
	    jrecord_pop(jrec, save);
	    if (*ap->a_vpp)
		jrecord_write_vnode_ref(jrec, *ap->a_vpp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nlink { a_nch, a_vp, a_cred }
 */
static
int
journal_nlink(struct vop_nlink_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_LINK);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	    /* XXX PATH to VP and inode number */
	    /* XXX this call may not record the correct path when
	     * multiple paths are available */
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_write_vnode_link(jrec, ap->a_vp, ap->a_nch->ncp);
	    jrecord_pop(jrec, save);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_symlink { a_nch, a_vpp, a_cred, a_vap, a_target }
 */
static
int
journal_nsymlink(struct vop_nsymlink_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    void *save;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_SYMLINK);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	    save = jrecord_push(jrec, JTYPE_REDO);
	    jrecord_leaf(jrec, JLEAF_SYMLINKDATA,
			ap->a_target, strlen(ap->a_target));
	    jrecord_pop(jrec, save);
	    if (*ap->a_vpp)
		jrecord_write_vnode_ref(jrec, *ap->a_vpp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nwhiteout { a_nch, a_cred, a_flags }
 */
static
int
journal_nwhiteout(struct vop_nwhiteout_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_WHITEOUT);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nremove { a_nch, a_cred }
 */
static
int
journal_nremove(struct vop_nremove_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    if (jreclist_init(mp, &jreclist, &jreccache, JTYPE_REMOVE) &&
	ap->a_nch->ncp->nc_vp
    ) {
	jreclist_undo_file(&jreclist, ap->a_nch->ncp->nc_vp, 
			   JRUNDO_ALL|JRUNDO_GETVP|JRUNDO_CONDLINK, 0, -1);
    }
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nmkdir { a_nch, a_vpp, a_cred, a_vap }
 */
static
int
journal_nmkdir(struct vop_nmkdir_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    jreclist_init(mp, &jreclist, &jreccache, JTYPE_MKDIR);
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
#if 0
	    if (jo->flags & MC_JOURNAL_WANT_AUDIT) {
		jrecord_write_audit(jrec);
	    }
#endif
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_vattr(jrec, ap->a_vap);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	    if (*ap->a_vpp)
		jrecord_write_vnode_ref(jrec, *ap->a_vpp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nrmdir { a_nch, a_cred }
 */
static
int
journal_nrmdir(struct vop_nrmdir_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    if (jreclist_init(mp, &jreclist, &jreccache, JTYPE_RMDIR)) {
	jreclist_undo_file(&jreclist, ap->a_nch->ncp->nc_vp,
			   JRUNDO_VATTR|JRUNDO_GETVP, 0, 0);
    }
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_nch->ncp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}

/*
 * Journal vop_nrename { a_fnch, a_tnch, a_cred }
 */
static
int
journal_nrename(struct vop_nrename_args *ap)
{
    struct jrecord_list jreclist;
    struct jrecord jreccache;
    struct jrecord *jrec;
    struct mount *mp;
    int error;

    mp = ap->a_head.a_ops->head.vv_mount;
    if (jreclist_init(mp, &jreclist, &jreccache, JTYPE_RENAME) &&
	ap->a_tnch->ncp->nc_vp
    ) {
	jreclist_undo_file(&jreclist, ap->a_tnch->ncp->nc_vp, 
			   JRUNDO_ALL|JRUNDO_GETVP|JRUNDO_CONDLINK, 0, -1);
    }
    error = vop_journal_operate_ap(&ap->a_head);
    if (error == 0) {
	TAILQ_FOREACH(jrec, &jreclist.list, user_entry) {
	    jrecord_write_cred(jrec, NULL, ap->a_cred);
	    jrecord_write_path(jrec, JLEAF_PATH1, ap->a_fnch->ncp);
	    jrecord_write_path(jrec, JLEAF_PATH2, ap->a_tnch->ncp);
	}
    }
    jreclist_done(mp, &jreclist, error);
    return (error);
}
