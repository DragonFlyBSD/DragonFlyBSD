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
 * $DragonFly: src/sys/kern/vfs_jops.c,v 1.3 2004/12/29 02:40:02 dillon Exp $
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
 * In addition, the journaling thread will have access to much larger 
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
 * originating the filesystem op.  
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/kernel.h>
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
static void journal_write_record(struct journal *jo, const char *ctl,
			    const char *buf, int bytes);
static void journal_write(struct journal *jo, const char *buf, int bytes);

static int journal_nmkdir(struct vop_nmkdir_args *ap);

static struct vnodeopv_entry_desc journal_vnodeop_entries[] = {
    { &vop_default_desc,		vop_journal_operate_ap },
    { &vop_mountctl_desc,		(void *)journal_mountctl },
    { &vop_nmkdir_desc,			(void *)journal_nmkdir },
    { NULL, NULL }
};

static MALLOC_DEFINE(M_JOURNAL, "journal", "Journaling structure");
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
 * Install a journal on a mount point
 */
static int
journal_install_vfs_journal(struct mount *mp, struct file *fp, 
			    const struct mountctl_install_journal *info)
{
    struct journal *jo;
    int error = 0;
    int size;

    jo = malloc(sizeof(struct journal), M_JOURNAL, M_WAITOK|M_ZERO);
    bcopy(info->id, jo->id, sizeof(jo->id));
    jo->flags = info->flags & ~(MC_JOURNAL_ACTIVE | MC_JOURNAL_STOP_REQ);

    /*
     * Memory FIFO size, round to nearest power of 2
     */
    if (info->flags & MC_JOURNAL_MBSIZE_PROVIDED) {
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
    if (info->flags & MC_JOURNAL_TRANSID_PROVIDED) {
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

    if (error) {
	free(jo, M_JOURNAL);
    } else {
	fhold(fp);
	jo->flags |= MC_JOURNAL_ACTIVE;
	lwkt_create(journal_thread, jo, NULL, &jo->thread,
			TDF_STOPREQ, -1, "journal %.*s", JIDMAX, jo->id);
	lwkt_setpri(&jo->thread, TDPRI_KERN_DAEMON);
	lwkt_schedule(&jo->thread);
	journal_write_record(jo, "INSTALL", NULL, 0);

	TAILQ_INSERT_TAIL(&mp->mnt_jlist, jo, jentry);
    }
    return(error);
}

static int
journal_remove_vfs_journal(struct mount *mp, const struct mountctl_remove_journal *info)
{
    struct journal *jo;
    int error;

    TAILQ_FOREACH(jo, &mp->mnt_jlist, jentry) {
	if (bcmp(jo->id, info->id, sizeof(jo->id)) == 0)
	    break;
    }
    if (jo) {
	error = 0;
	TAILQ_REMOVE(&mp->mnt_jlist, jo, jentry);
	journal_write_record(jo, "REMOVE", NULL, 0); /* XXX sequencing */
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

static void
journal_thread(void *info)
{
    struct journal *jo = info;
    int bytes;
    int error;
    int res;

    for (;;) {
	bytes = (jo->fifo.windex - jo->fifo.rindex) & jo->fifo.mask;
	if (bytes == 0 || (jo->flags & MC_JOURNAL_STOP_IMM)) {
	    if (jo->flags & MC_JOURNAL_STOP_REQ)
		break;
	    tsleep(&jo->fifo, 0, "jfifo", 0); /* XXX add heartbeat */
	}
	if (bytes > jo->fifo.size - jo->fifo.windex)
	    bytes = jo->fifo.size - jo->fifo.windex;
	error = fp_write(jo->fp, jo->fifo.membase + jo->fifo.rindex, bytes, &res);
	if (error) {
	    printf("journal_thread(%s) write, error %d\n", jo->id, error);
	    jo->fifo.rindex = jo->fifo.windex; /* XXX flag out-of-sync */
	} else {
	    printf("journal_thread(%s) write %d\n", jo->id, res);
	    jo->fifo.rindex = (jo->fifo.rindex + res) & jo->fifo.mask;
	    if (jo->flags & MC_JOURNAL_WWAIT) {
		jo->flags &= ~MC_JOURNAL_WWAIT;	/* XXX hysteresis */
		wakeup(&jo->fifo.windex);
	    }
	}
    }
    jo->flags &= ~MC_JOURNAL_ACTIVE;
    wakeup(jo);
    wakeup(&jo->fifo.windex);
}

static 
void 
journal_write_record(struct journal *jo, const char *ctl, 
		     const char *buf, int bytes)
{
    char head[64];

    if (jo->flags & MC_JOURNAL_STOP_REQ)
	return;
    snprintf(head, sizeof(head), "%016llx %s\n", jo->transid, ctl);
    /* XXX locking (token) or cmpexgl space reservation */
    ++jo->transid;	/* XXX embed nanotime, force monotonic */
    journal_write(jo, head, strlen(head));
    if (bytes)
	journal_write(jo, buf, bytes);
}

static
void
journal_write(struct journal *jo, const char *buf, int bytes)
{
    int avail;

    while (bytes && (jo->flags & MC_JOURNAL_ACTIVE)) {
	avail = (jo->fifo.windex - jo->fifo.rindex) & jo->fifo.mask;
	avail = jo->fifo.size - avail - 1;
	if (avail == 0) {
	    if (jo->flags & MC_JOURNAL_STOP_IMM)
		break;
	    jo->flags |= MC_JOURNAL_WWAIT;
	    tsleep(&jo->fifo.windex, 0, "jwrite", 0);
	    continue;
	}
	if (avail > jo->fifo.size - jo->fifo.windex)
	    avail = jo->fifo.size - jo->fifo.windex;
	if (avail > bytes)
	    avail = bytes;
	bcopy(buf, jo->fifo.membase + jo->fifo.windex, avail);
	bytes -= avail;
	jo->fifo.windex = (jo->fifo.windex + avail) & jo->fifo.mask;
	tsleep(&jo->fifo, 0, "jfifo", 0);	/* XXX hysteresis */
    }
}

/************************************************************************
 *			JOURNAL VNOPS					*
 ************************************************************************/

static
int
journal_nmkdir(struct vop_nmkdir_args *ap)
{
    int error;

    printf("JMKDIR %s\n", ap->a_ncp->nc_name);
    error = vop_journal_operate_ap(&ap->a_head);
    return (error);
}

