/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 */
/*
 * This module allows disk devices to be created and associated with a
 * communications pipe or socket.  You open the device and issue an
 * ioctl() to install a new disk along with its communications descriptor.
 *
 * All further communication occurs via the descriptor using the DMSG
 * LNK_CONN, LNK_SPAN, and BLOCK protocols.  The descriptor can be a
 * direct connection to a remote machine's disk (in-kernenl), to a remote
 * cluster controller, to the local cluster controller, etc.
 *
 * /dev/xdisk is the control device, issue ioctl()s to create the /dev/xa%d
 * devices.  These devices look like raw disks to the system.
 *
 * TODO:
 *	Handle circuit disconnects, leave bio's pending
 *	Restart bio's on circuit reconnect.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/udev.h>
#include <sys/uuid.h>
#include <sys/kern_syscall.h>

#include <sys/dmsg.h>
#include <sys/xdiskioctl.h>

#include <sys/buf2.h>
#include <sys/thread2.h>

struct xa_softc;

struct xa_tag {
	TAILQ_ENTRY(xa_tag) entry;
	struct xa_softc	*xa;
	dmsg_blk_error_t status;
	kdmsg_state_t	*state;
	kdmsg_circuit_t	*circ;
	struct bio	*bio;
	int		running;	/* transaction running */
	int		waitseq;	/* streaming reply */
	int		done;		/* final (transaction closed) */
};

typedef struct xa_tag	xa_tag_t;

struct xa_softc {
	TAILQ_ENTRY(xa_softc) entry;
	cdev_t		dev;
	kdmsg_iocom_t	iocom;
	struct xdisk_attach_ioctl xaioc;
	struct disk_info info;
	struct disk	disk;
	uuid_t		pfs_fsid;
	int		unit;
	int		serializing;
	int		attached;
	int		opencnt;
	uint64_t	keyid;
	xa_tag_t	*opentag;
	TAILQ_HEAD(, bio) bioq;
	TAILQ_HEAD(, xa_tag) tag_freeq;
	TAILQ_HEAD(, xa_tag) tag_pendq;
	TAILQ_HEAD(, kdmsg_circuit) circq;
	struct lwkt_token tok;
};

typedef struct xa_softc	xa_softc_t;

#define MAXTAGS		64	/* no real limit */

static int xdisk_attach(struct xdisk_attach_ioctl *xaioc);
static int xdisk_detach(struct xdisk_attach_ioctl *xaioc);
static void xa_exit(kdmsg_iocom_t *iocom);
static void xa_terminate_check(struct xa_softc *xa);
static int xa_rcvdmsg(kdmsg_msg_t *msg);
static void xa_autodmsg(kdmsg_msg_t *msg);

static xa_tag_t *xa_setup_cmd(xa_softc_t *xa, struct bio *bio);
static void xa_start(xa_tag_t *tag, kdmsg_msg_t *msg);
static uint32_t xa_wait(xa_tag_t *tag, int seq);
static void xa_done(xa_tag_t *tag, int wasbio);
static int xa_sync_completion(kdmsg_state_t *state, kdmsg_msg_t *msg);
static int xa_bio_completion(kdmsg_state_t *state, kdmsg_msg_t *msg);
static void xa_restart_deferred(xa_softc_t *xa);

MALLOC_DEFINE(M_XDISK, "Networked disk client", "Network Disks");

/*
 * Control device, issue ioctls to create xa devices.
 */
static d_open_t xdisk_open;
static d_close_t xdisk_close;
static d_ioctl_t xdisk_ioctl;

static struct dev_ops xdisk_ops = {
	{ "xdisk", 0, D_MPSAFE | D_TRACKCLOSE },
        .d_open =	xdisk_open,
        .d_close =	xdisk_close,
        .d_ioctl =	xdisk_ioctl
};

/*
 * XA disk devices
 */
static d_open_t xa_open;
static d_close_t xa_close;
static d_ioctl_t xa_ioctl;
static d_strategy_t xa_strategy;
static d_psize_t xa_size;

static struct dev_ops xa_ops = {
	{ "xa", 0, D_DISK | D_CANFREE | D_MPSAFE | D_TRACKCLOSE },
        .d_open =	xa_open,
        .d_close =	xa_close,
        .d_ioctl =	xa_ioctl,
        .d_read =	physread,
        .d_write =	physwrite,
        .d_strategy =	xa_strategy,
	.d_psize =	xa_size
};

static struct lwkt_token xdisk_token = LWKT_TOKEN_INITIALIZER(xdisk_token);
static int xdisk_opencount;
static cdev_t xdisk_dev;
static TAILQ_HEAD(, xa_softc) xa_queue;

/*
 * Module initialization
 */
static int
xdisk_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		TAILQ_INIT(&xa_queue);
		xdisk_dev = make_dev(&xdisk_ops, 0,
				     UID_ROOT, GID_WHEEL, 0600, "xdisk");
		break;
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		if (xdisk_opencount || TAILQ_FIRST(&xa_queue))
			return (EBUSY);
		if (xdisk_dev) {
			destroy_dev(xdisk_dev);
			xdisk_dev = NULL;
		}
		dev_ops_remove_all(&xdisk_ops);
		dev_ops_remove_all(&xa_ops);
		break;
	default:
		break;
	}
	return 0;
}

DEV_MODULE(xdisk, xdisk_modevent, 0);

/*
 * Control device
 */
static int
xdisk_open(struct dev_open_args *ap)
{
	lwkt_gettoken(&xdisk_token);
	++xdisk_opencount;
	lwkt_reltoken(&xdisk_token);
	return(0);
}

static int
xdisk_close(struct dev_close_args *ap)
{
	lwkt_gettoken(&xdisk_token);
	--xdisk_opencount;
	lwkt_reltoken(&xdisk_token);
	return(0);
}

static int
xdisk_ioctl(struct dev_ioctl_args *ap)
{
	int error;

	switch(ap->a_cmd) {
	case XDISKIOCATTACH:
		error = xdisk_attach((void *)ap->a_data);
		break;
	case XDISKIOCDETACH:
		error = xdisk_detach((void *)ap->a_data);
		break;
	default:
		error = ENOTTY;
		break;
	}
	return error;
}

/************************************************************************
 *				DMSG INTERFACE				*
 ************************************************************************/

static int
xdisk_attach(struct xdisk_attach_ioctl *xaioc)
{
	xa_softc_t *xa;
	xa_tag_t *tag;
	struct file *fp;
	int unit;
	int n;
	char devname[64];
	cdev_t dev;

	/*
	 * Normalize ioctl params
	 */
	fp = holdfp(curproc->p_fd, xaioc->fd, -1);
	if (fp == NULL)
		return EINVAL;
	if (xaioc->cl_label[sizeof(xaioc->cl_label) - 1] != 0)
		return EINVAL;
	if (xaioc->fs_label[sizeof(xaioc->fs_label) - 1] != 0)
		return EINVAL;
	if (xaioc->blksize < DEV_BSIZE || xaioc->blksize > MAXBSIZE)
		return EINVAL;

	/*
	 * See if the serial number is already present.  If we are
	 * racing a termination the disk subsystem may still have
	 * duplicate entries not yet removed so we wait a bit and
	 * retry.
	 */
	lwkt_gettoken(&xdisk_token);
again:
	TAILQ_FOREACH(xa, &xa_queue, entry) {
		if (strcmp(xa->iocom.auto_lnk_conn.fs_label,
			   xaioc->fs_label) == 0) {
			if (xa->serializing) {
				tsleep(xa, 0, "xadelay", hz / 10);
				goto again;
			}
			xa->serializing = 1;
			kdmsg_iocom_uninit(&xa->iocom);
			break;
		}
	}

	/*
	 * Create a new xa if not already present
	 */
	if (xa == NULL) {
		unit = 0;
		for (;;) {
			TAILQ_FOREACH(xa, &xa_queue, entry) {
				if (xa->unit == unit)
					break;
			}
			if (xa == NULL)
				break;
			++unit;
		}
		xa = kmalloc(sizeof(*xa), M_XDISK, M_WAITOK|M_ZERO);
		xa->unit = unit;
		xa->serializing = 1;
		lwkt_token_init(&xa->tok, "xa");
		TAILQ_INIT(&xa->circq);
		TAILQ_INIT(&xa->bioq);
		TAILQ_INIT(&xa->tag_freeq);
		TAILQ_INIT(&xa->tag_pendq);
		for (n = 0; n < MAXTAGS; ++n) {
			tag = kmalloc(sizeof(*tag), M_XDISK, M_WAITOK|M_ZERO);
			tag->xa = xa;
			TAILQ_INSERT_TAIL(&xa->tag_freeq, tag, entry);
		}
		TAILQ_INSERT_TAIL(&xa_queue, xa, entry);
	} else {
		unit = xa->unit;
	}

	/*
	 * (xa) is now serializing.
	 */
	xa->xaioc = *xaioc;
	xa->attached = 1;
	lwkt_reltoken(&xdisk_token);

	/*
	 * Create device
	 */
	if (xa->dev == NULL) {
		dev = disk_create(unit, &xa->disk, &xa_ops);
		dev->si_drv1 = xa;
		xa->dev = dev;
	}

	xa->info.d_media_blksize = xaioc->blksize;
	xa->info.d_media_blocks = xaioc->bytes / xaioc->blksize;
	xa->info.d_dsflags = DSO_MBRQUIET | DSO_RAWPSIZE;
	xa->info.d_secpertrack = 32;
	xa->info.d_nheads = 64;
	xa->info.d_secpercyl = xa->info.d_secpertrack * xa->info.d_nheads;
	xa->info.d_ncylinders = 0;
	if (xa->xaioc.fs_label[0])
		xa->info.d_serialno = xa->xaioc.fs_label;

	/*
	 * Set up messaging connection
	 */
	ksnprintf(devname, sizeof(devname), "xa%d", unit);
	kdmsg_iocom_init(&xa->iocom, xa,
			 KDMSG_IOCOMF_AUTOCONN |
			 KDMSG_IOCOMF_AUTOSPAN |
			 KDMSG_IOCOMF_AUTOCIRC |
			 KDMSG_IOCOMF_AUTOFORGE,
			 M_XDISK, xa_rcvdmsg);
	xa->iocom.exit_func = xa_exit;

	kdmsg_iocom_reconnect(&xa->iocom, fp, devname);

	/*
	 * Setup our LNK_CONN advertisement for autoinitiate.
	 *
	 * Our filter is setup to only accept PEER_BLOCK/SERVER
	 * advertisements.
	 */
	xa->iocom.auto_lnk_conn.pfs_type = DMSG_PFSTYPE_CLIENT;
	xa->iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	xa->iocom.auto_lnk_conn.peer_type = DMSG_PEER_BLOCK;
	xa->iocom.auto_lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;
	xa->iocom.auto_lnk_conn.pfs_mask = 1LLU << DMSG_PFSTYPE_SERVER;
	ksnprintf(xa->iocom.auto_lnk_conn.cl_label,
		  sizeof(xa->iocom.auto_lnk_conn.cl_label),
		  "%s", xaioc->cl_label);

	/*
	 * We need a unique pfs_fsid to avoid confusion.
	 * We supply a rendezvous fs_label using the serial number.
	 */
	kern_uuidgen(&xa->pfs_fsid, 1);
	xa->iocom.auto_lnk_conn.pfs_fsid = xa->pfs_fsid;
	ksnprintf(xa->iocom.auto_lnk_conn.fs_label,
		  sizeof(xa->iocom.auto_lnk_conn.fs_label),
		  "%s", xaioc->fs_label);

	/*
	 * Setup our LNK_SPAN advertisement for autoinitiate
	 */
	xa->iocom.auto_lnk_span.pfs_type = DMSG_PFSTYPE_CLIENT;
	xa->iocom.auto_lnk_span.proto_version = DMSG_SPAN_PROTO_1;
	xa->iocom.auto_lnk_span.peer_type = DMSG_PEER_BLOCK;
	ksnprintf(xa->iocom.auto_lnk_span.cl_label,
		  sizeof(xa->iocom.auto_lnk_span.cl_label),
		  "%s", xa->xaioc.cl_label);

	kdmsg_iocom_autoinitiate(&xa->iocom, xa_autodmsg);
	disk_setdiskinfo_sync(&xa->disk, &xa->info);

	lwkt_gettoken(&xdisk_token);
	xa->serializing = 0;
	xa_terminate_check(xa);
	lwkt_reltoken(&xdisk_token);

	return(0);
}

static int
xdisk_detach(struct xdisk_attach_ioctl *xaioc)
{
	struct xa_softc *xa;

	lwkt_gettoken(&xdisk_token);
	for (;;) {
		TAILQ_FOREACH(xa, &xa_queue, entry) {
			if (strcmp(xa->iocom.auto_lnk_conn.fs_label,
				   xaioc->fs_label) == 0) {
				break;
			}
		}
		if (xa == NULL || xa->serializing == 0) {
			xa->serializing = 1;
			break;
		}
		tsleep(xa, 0, "xadet", hz / 10);
	}
	if (xa) {
		kdmsg_iocom_uninit(&xa->iocom);
		xa->serializing = 0;
	}
	lwkt_reltoken(&xdisk_token);
	return(0);
}

/*
 * Called from iocom core transmit thread upon disconnect.
 */
static
void
xa_exit(kdmsg_iocom_t *iocom)
{
	struct xa_softc *xa = iocom->handle;

	lwkt_gettoken(&xa->tok);
	lwkt_gettoken(&xdisk_token);

	/*
	 * We must wait for any I/O's to complete to ensure that all
	 * state structure references are cleaned up before returning.
	 */
	xa->attached = -1;	/* force deferral or failure */
	while (TAILQ_FIRST(&xa->tag_pendq)) {
		tsleep(xa, 0, "xabiow", hz / 10);
	}

	/*
	 * All serializing code checks for de-initialization so only
	 * do it if we aren't already serializing.
	 */
	if (xa->serializing == 0) {
		xa->serializing = 1;
		kdmsg_iocom_uninit(iocom);
		xa->serializing = 0;
	}

	/*
	 * If the drive is not in use and no longer attach it can be
	 * destroyed.
	 */
	xa->attached = 0;
	xa_terminate_check(xa);
	lwkt_reltoken(&xdisk_token);
	lwkt_reltoken(&xa->tok);
}

/*
 * Determine if we can destroy the xa_softc.
 *
 * Called with xdisk_token held.
 */
static
void
xa_terminate_check(struct xa_softc *xa)
{
	xa_tag_t *tag;
	struct bio *bio;

	if (xa->opencnt || xa->attached || xa->serializing)
		return;
	xa->serializing = 1;
	kdmsg_iocom_uninit(&xa->iocom);

	/*
	 * When destroying an xa make sure all pending I/O (typically
	 * from the disk probe) is done.
	 *
	 * XXX what about new I/O initiated prior to disk_destroy().
	 */
	while ((tag = TAILQ_FIRST(&xa->tag_pendq)) != NULL) {
		TAILQ_REMOVE(&xa->tag_pendq, tag, entry);
		if ((bio = tag->bio) != NULL) {
			tag->bio = NULL;
			bio->bio_buf->b_error = ENXIO;
			bio->bio_buf->b_flags |= B_ERROR;
			biodone(bio);
		}
		TAILQ_INSERT_TAIL(&xa->tag_freeq, tag, entry);
	}
	if (xa->dev) {
		disk_destroy(&xa->disk);
		xa->dev->si_drv1 = NULL;
		xa->dev = NULL;
	}
	KKASSERT(xa->opencnt == 0 && xa->attached == 0);
	while ((tag = TAILQ_FIRST(&xa->tag_freeq)) != NULL) {
		TAILQ_REMOVE(&xa->tag_freeq, tag, entry);
		tag->xa = NULL;
		kfree(tag, M_XDISK);
	}
	KKASSERT(TAILQ_EMPTY(&xa->tag_pendq));
	TAILQ_REMOVE(&xa_queue, xa, entry); /* XXX */
	kfree(xa, M_XDISK);
}

/*
 * Shim to catch and record virtual circuit events.
 */
static void
xa_autodmsg(kdmsg_msg_t *msg)
{
	xa_softc_t *xa = msg->iocom->handle;

	kdmsg_circuit_t *circ;
	kdmsg_circuit_t *cscan;
	uint32_t xcmd;

	/*
	 * Because this is just a shim we don't have a state callback for
	 * the transactions we are sniffing, so make things easier by
	 * calculating the original command along with the current message's
	 * flags.  This is because transactions are made up of numerous
	 * messages and only the first typically specifies the actual command.
	 */
	if (msg->state) {
		xcmd = msg->state->icmd |
		       (msg->any.head.cmd & (DMSGF_CREATE |
					     DMSGF_DELETE |
					     DMSGF_REPLY));
	} else {
		xcmd = msg->any.head.cmd;
	}

	/*
	 * Add or remove a circuit, sorted by weight (lower numbers are
	 * better).
	 */
	switch(xcmd) {
	case DMSG_LNK_CIRC | DMSGF_CREATE | DMSGF_REPLY:
		/*
		 * Track established circuits
		 */
		circ = msg->state->any.circ;
		lwkt_gettoken(&xa->tok);
		if (circ->recorded == 0) {
			TAILQ_FOREACH(cscan, &xa->circq, entry) {
				if (circ->weight < cscan->weight)
					break;
			}
			if (cscan)
				TAILQ_INSERT_BEFORE(cscan, circ, entry);
			else
				TAILQ_INSERT_TAIL(&xa->circq, circ, entry);
			circ->recorded = 1;
		}

		/*
		 * Restart any deferred I/O.
		 */
		xa_restart_deferred(xa);
		lwkt_reltoken(&xa->tok);
		break;
	case DMSG_LNK_CIRC | DMSGF_DELETE | DMSGF_REPLY:
		/*
		 * Losing virtual circuit.  Remove the circ from contention.
		 */
		circ = msg->state->any.circ;
		lwkt_gettoken(&xa->tok);
		if (circ->recorded) {
			TAILQ_REMOVE(&xa->circq, circ, entry);
			circ->recorded = 0;
		}
		xa_restart_deferred(xa);
		lwkt_reltoken(&xa->tok);
		break;
	default:
		break;
	}
}

static int
xa_rcvdmsg(kdmsg_msg_t *msg)
{
	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * Execute shell command (not supported atm).
		 *
		 * This is a one-way packet but if not (e.g. if part of
		 * a streaming transaction), we will have already closed
		 * our end.
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		/*
		 * Receive one or more replies to a shell command that we
		 * sent.
		 *
		 * This is a one-way packet but if not (e.g. if part of
		 * a streaming transaction), we will have already closed
		 * our end.
		 */
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("xdisk: DEBUGMSG: %s\n", msg->aux_data);
		}
		break;
	default:
		/*
		 * Unsupported LNK message received.  We only need to
		 * reply if it's a transaction in order to close our end.
		 * Ignore any one-way messages are any further messages
		 * associated with the transaction.
		 *
		 * NOTE: This case also includes DMSG_LNK_ERROR messages
		 *	 which might be one-way, replying to those would
		 *	 cause an infinite ping-pong.
		 */
		if (msg->any.head.cmd & DMSGF_CREATE)
			kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
	return(0);
}


/************************************************************************
 *			   XA DEVICE INTERFACE				*
 ************************************************************************/

static int
xa_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	xa_softc_t *xa;
	xa_tag_t *tag;
	kdmsg_msg_t *msg;
	int error;

	dev->si_bsize_phys = 512;
	dev->si_bsize_best = 32768;

	/*
	 * Interlock open with opencnt, wait for attachment operations
	 * to finish.
	 */
	lwkt_gettoken(&xdisk_token);
again:
	xa = dev->si_drv1;
	if (xa == NULL) {
		lwkt_reltoken(&xdisk_token);
		return ENXIO;	/* raced destruction */
	}
	if (xa->serializing) {
		tsleep(xa, 0, "xarace", hz / 10);
		goto again;
	}
	if (xa->attached == 0) {
		lwkt_reltoken(&xdisk_token);
		return ENXIO;	/* raced destruction */
	}

	/*
	 * Serialize initial open
	 */
	if (xa->opencnt++ > 0) {
		lwkt_reltoken(&xdisk_token);
		return(0);
	}
	xa->serializing = 1;
	lwkt_reltoken(&xdisk_token);

	tag = xa_setup_cmd(xa, NULL);
	if (tag == NULL) {
		lwkt_gettoken(&xdisk_token);
		KKASSERT(xa->opencnt > 0);
		--xa->opencnt;
		xa->serializing = 0;
		xa_terminate_check(xa);
		lwkt_reltoken(&xdisk_token);
		return(ENXIO);
	}
	msg = kdmsg_msg_alloc(&xa->iocom, tag->circ,
			      DMSG_BLK_OPEN | DMSGF_CREATE,
			      xa_sync_completion, tag);
	msg->any.blk_open.modes = DMSG_BLKOPEN_RD | DMSG_BLKOPEN_WR;
	xa_start(tag, msg);
	if (xa_wait(tag, 0) == 0) {
		xa->keyid = tag->status.keyid;
		xa->opentag = tag;	/* leave tag open */
		xa->serializing = 0;
		error = 0;
	} else {
		xa_done(tag, 0);
		lwkt_gettoken(&xdisk_token);
		KKASSERT(xa->opencnt > 0);
		--xa->opencnt;
		xa->serializing = 0;
		xa_terminate_check(xa);
		lwkt_reltoken(&xdisk_token);
		error = ENXIO;
	}
	return (error);
}

static int
xa_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	xa_softc_t *xa;
	xa_tag_t *tag;

	xa = dev->si_drv1;
	if (xa == NULL)
		return ENXIO;	/* raced destruction */

	lwkt_gettoken(&xa->tok);
	if ((tag = xa->opentag) != NULL) {
		xa->opentag = NULL;
		kdmsg_state_reply(tag->state, 0);
		while (tag->done == 0)
			xa_wait(tag, tag->waitseq);
		xa_done(tag, 0);
	}
	lwkt_reltoken(&xa->tok);

	lwkt_gettoken(&xdisk_token);
	KKASSERT(xa->opencnt > 0);
	--xa->opencnt;
	xa_terminate_check(xa);
	lwkt_reltoken(&xdisk_token);

	return(0);
}

static int
xa_strategy(struct dev_strategy_args *ap)
{
	xa_softc_t *xa = ap->a_head.a_dev->si_drv1;
	xa_tag_t *tag;
	struct bio *bio = ap->a_bio;

	/*
	 * Allow potentially temporary link failures to fail the I/Os
	 * only if the device is not open.  That is, we allow the disk
	 * probe code prior to mount to fail.
	 */
	if (xa->attached == 0 && xa->opencnt == 0) {
		bio->bio_buf->b_error = ENXIO;
		bio->bio_buf->b_flags |= B_ERROR;
		biodone(bio);
		return(0);
	}

	tag = xa_setup_cmd(xa, bio);
	if (tag)
		xa_start(tag, NULL);
	return(0);
}

static int
xa_ioctl(struct dev_ioctl_args *ap)
{
	return(ENOTTY);
}

static int
xa_size(struct dev_psize_args *ap)
{
	struct xa_softc *xa;

	if ((xa = ap->a_head.a_dev->si_drv1) == NULL)
		return (ENXIO);
	ap->a_result = xa->info.d_media_blocks;
	return (0);
}

/************************************************************************
 *		    XA BLOCK PROTOCOL STATE MACHINE			*
 ************************************************************************
 *
 * Implement tag/msg setup and related functions.
 */
static xa_tag_t *
xa_setup_cmd(xa_softc_t *xa, struct bio *bio)
{
	kdmsg_circuit_t *circ;
	xa_tag_t *tag;

	/*
	 * Only get a tag if we have a valid virtual circuit to the server.
	 */
	lwkt_gettoken(&xa->tok);
	TAILQ_FOREACH(circ, &xa->circq, entry) {
		if (circ->lost == 0)
			break;
	}
	if (circ == NULL || xa->attached <= 0) {
		tag = NULL;
	} else if ((tag = TAILQ_FIRST(&xa->tag_freeq)) != NULL) {
		TAILQ_REMOVE(&xa->tag_freeq, tag, entry);
		tag->bio = bio;
		tag->circ = circ;
		kdmsg_circ_hold(circ);
		TAILQ_INSERT_TAIL(&xa->tag_pendq, tag, entry);
	}

	/*
	 * If we can't dispatch now and this is a bio, queue it for later.
	 */
	if (tag == NULL && bio) {
		TAILQ_INSERT_TAIL(&xa->bioq, bio, bio_act);
	}
	lwkt_reltoken(&xa->tok);

	return (tag);
}

static void
xa_start(xa_tag_t *tag, kdmsg_msg_t *msg)
{
	xa_softc_t *xa = tag->xa;

	if (msg == NULL) {
		struct bio *bio;
		struct buf *bp;

		KKASSERT(tag->bio);
		bio = tag->bio;
		bp = bio->bio_buf;

		switch(bp->b_cmd) {
		case BUF_CMD_READ:
			msg = kdmsg_msg_alloc(&xa->iocom, tag->circ,
					      DMSG_BLK_READ |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_read.keyid = xa->keyid;
			msg->any.blk_read.offset = bio->bio_offset;
			msg->any.blk_read.bytes = bp->b_bcount;
			break;
		case BUF_CMD_WRITE:
			msg = kdmsg_msg_alloc(&xa->iocom, tag->circ,
					      DMSG_BLK_WRITE |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_write.keyid = xa->keyid;
			msg->any.blk_write.offset = bio->bio_offset;
			msg->any.blk_write.bytes = bp->b_bcount;
			msg->aux_data = bp->b_data;
			msg->aux_size = bp->b_bcount;
			break;
		case BUF_CMD_FLUSH:
			msg = kdmsg_msg_alloc(&xa->iocom, tag->circ,
					      DMSG_BLK_FLUSH |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_flush.keyid = xa->keyid;
			msg->any.blk_flush.offset = bio->bio_offset;
			msg->any.blk_flush.bytes = bp->b_bcount;
			break;
		case BUF_CMD_FREEBLKS:
			msg = kdmsg_msg_alloc(&xa->iocom, tag->circ,
					      DMSG_BLK_FREEBLKS |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_freeblks.keyid = xa->keyid;
			msg->any.blk_freeblks.offset = bio->bio_offset;
			msg->any.blk_freeblks.bytes = bp->b_bcount;
			break;
		default:
			bp->b_flags |= B_ERROR;
			bp->b_error = EIO;
			biodone(bio);
			tag->bio = NULL;
			break;
		}
	}

	tag->done = 0;
	tag->waitseq = 0;
	if (msg) {
		tag->state = msg->state;
		kdmsg_msg_write(msg);
	} else {
		xa_done(tag, 1);
	}
}

static uint32_t
xa_wait(xa_tag_t *tag, int seq)
{
	xa_softc_t *xa = tag->xa;

	lwkt_gettoken(&xa->tok);
	while (tag->waitseq == seq)
		tsleep(tag, 0, "xawait", 0);
	lwkt_reltoken(&xa->tok);
	return (tag->status.head.error);
}

static void
xa_done(xa_tag_t *tag, int wasbio)
{
	xa_softc_t *xa = tag->xa;
	struct bio *bio;

	KKASSERT(tag->bio == NULL);
	tag->done = 1;
	tag->state = NULL;

	lwkt_gettoken(&xa->tok);
	if (wasbio && (bio = TAILQ_FIRST(&xa->bioq)) != NULL) {
		TAILQ_REMOVE(&xa->bioq, bio, bio_act);
		tag->bio = bio;
		lwkt_reltoken(&xa->tok);
		xa_start(tag, NULL);
	} else {
		if (tag->circ) {
			kdmsg_circ_drop(tag->circ);
			tag->circ = NULL;
		}
		TAILQ_REMOVE(&xa->tag_pendq, tag, entry);
		TAILQ_INSERT_TAIL(&xa->tag_freeq, tag, entry);
		lwkt_reltoken(&xa->tok);
	}
}

static int
xa_sync_completion(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	xa_tag_t *tag = state->any.any;
	xa_softc_t *xa = tag->xa;

	switch(msg->any.head.cmd & DMSGF_CMDSWMASK) {
	case DMSG_LNK_ERROR | DMSGF_REPLY:
		bzero(&tag->status, sizeof(tag->status));
		tag->status.head = msg->any.head;
		break;
	case DMSG_BLK_ERROR | DMSGF_REPLY:
		tag->status = msg->any.blk_error;
		break;
	}
	lwkt_gettoken(&xa->tok);
	if (msg->any.head.cmd & DMSGF_DELETE) {	/* receive termination */
		if (xa->opentag == tag) {
			xa->opentag = NULL;	/* XXX */
			kdmsg_state_reply(tag->state, 0);
			xa_done(tag, 0);
			lwkt_reltoken(&xa->tok);
			return(0);
		} else {
			tag->done = 1;
		}
	}
	++tag->waitseq;
	lwkt_reltoken(&xa->tok);

	wakeup(tag);

	return (0);
}

static int
xa_bio_completion(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	xa_tag_t *tag = state->any.any;
	xa_softc_t *xa = tag->xa;
	struct bio *bio;
	struct buf *bp;

	/*
	 * Get the bio from the tag.  If no bio is present we just do
	 * 'done' handling.
	 */
	if ((bio = tag->bio) == NULL)
		goto handle_done;
	bp = bio->bio_buf;

	/*
	 * Process return status
	 */
	switch(msg->any.head.cmd & DMSGF_CMDSWMASK) {
	case DMSG_LNK_ERROR | DMSGF_REPLY:
		bzero(&tag->status, sizeof(tag->status));
		tag->status.head = msg->any.head;
		if (tag->status.head.error)
			tag->status.resid = bp->b_bcount;
		else
			tag->status.resid = 0;
		break;
	case DMSG_BLK_ERROR | DMSGF_REPLY:
		tag->status = msg->any.blk_error;
		break;
	}

	/*
	 * Potentially move the bio back onto the pending queue if the
	 * device is open and the error is related to losing the virtual
	 * circuit.
	 */
	if (tag->status.head.error &&
	    (msg->any.head.cmd & DMSGF_DELETE) && xa->opencnt) {
		if (tag->status.head.error == DMSG_ERR_LOSTLINK ||
		    tag->status.head.error == DMSG_ERR_CANTCIRC) {
			goto handle_repend;
		}
	}

	/*
	 * Process bio completion
	 *
	 * For reads any returned data is zero-extended if necessary, so
	 * the server can short-cut any all-zeros reads if it desires.
	 */
	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		if (msg->aux_data && msg->aux_size) {
			if (msg->aux_size < bp->b_bcount) {
				bcopy(msg->aux_data, bp->b_data, msg->aux_size);
				bzero(bp->b_data + msg->aux_size,
				      bp->b_bcount - msg->aux_size);
			} else {
				bcopy(msg->aux_data, bp->b_data, bp->b_bcount);
			}
		} else {
			bzero(bp->b_data, bp->b_bcount);
		}
		/* fall through */
	case BUF_CMD_WRITE:
	case BUF_CMD_FLUSH:
	case BUF_CMD_FREEBLKS:
	default:
		if (tag->status.resid > bp->b_bcount)
			tag->status.resid = bp->b_bcount;
		bp->b_resid = tag->status.resid;
		if ((bp->b_error = tag->status.head.error) != 0) {
			bp->b_flags |= B_ERROR;
		} else {
			bp->b_resid = 0;
		}
		biodone(bio);
		tag->bio = NULL;
		break;
	}

	/*
	 * Handle completion of the transaction.  If the bioq is not empty
	 * we can initiate another bio on the same tag.
	 *
	 * NOTE: Most of our transactions will be single-message
	 *	 CREATE+DELETEs, so we won't have to terminate the
	 *	 transaction separately, here.  But just in case they
	 *	 aren't be sure to terminate the transaction.
	 */
handle_done:
	if (msg->any.head.cmd & DMSGF_DELETE) {
		xa_done(tag, 1);
		if ((state->txcmd & DMSGF_DELETE) == 0)
			kdmsg_msg_reply(msg, 0);
	}
	return (0);

	/*
	 * Handle the case where the transaction failed due to a
	 * connectivity issue.  The tag is put away with wasbio=0
	 * and we restart the bio.
	 *
	 * Setting circ->lost causes xa_setup_cmd() to skip the circuit.
	 * Other circuits might still be live.  Once a circuit gets messed
	 * up it will (eventually) be deleted so we can simply leave (lost)
	 * set forever after.
	 */
handle_repend:
	lwkt_gettoken(&xa->tok);
	kprintf("BIO CIRC FAILURE, REPEND BIO %p\n", bio);
	tag->circ->lost = 1;
	tag->bio = NULL;
	xa_done(tag, 0);
	if ((state->txcmd & DMSGF_DELETE) == 0)
		kdmsg_msg_reply(msg, 0);

	/*
	 * Restart or requeue the bio
	 */
	tag = xa_setup_cmd(xa, bio);
	if (tag)
		xa_start(tag, NULL);
	lwkt_reltoken(&xa->tok);
	return (0);
}

/*
 * Restart as much deferred I/O as we can.
 *
 * Called with xa->tok held
 */
static
void
xa_restart_deferred(xa_softc_t *xa)
{
	struct bio *bio;
	xa_tag_t *tag;

	while ((bio = TAILQ_FIRST(&xa->bioq)) != NULL) {
		tag = xa_setup_cmd(xa, NULL);
		if (tag == NULL)
			break;
		TAILQ_REMOVE(&xa->bioq, bio, bio_act);
		tag->bio = bio;
		xa_start(tag, NULL);
	}
}
