/*
 * Copyright (c) 2012-2014 The DragonFly Project.  All rights reserved.
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
#include <sys/tree.h>
#include <sys/udev.h>
#include <sys/uuid.h>
#include <sys/kern_syscall.h>

#include <sys/dmsg.h>
#include <sys/xdiskioctl.h>

#include <sys/buf2.h>
#include <sys/thread2.h>

struct xa_softc;
struct xa_softc_tree;
RB_HEAD(xa_softc_tree, xa_softc);
RB_PROTOTYPE(xa_softc_tree, xa_softc, rbnode, xa_softc_cmp);

/*
 * Track a BIO tag
 */
struct xa_tag {
	TAILQ_ENTRY(xa_tag) entry;
	struct xa_softc	*sc;
	dmsg_blk_error_t status;
	kdmsg_state_t	*state;
	struct bio	*bio;
	int		waiting;
	int		async;
	int		done;
};

typedef struct xa_tag	xa_tag_t;

/*
 * Track devices.
 */
struct xa_softc {
	struct kdmsg_state_list spanq;
	RB_ENTRY(xa_softc) rbnode;
	cdev_t		dev;
	struct disk_info info;
	struct disk	disk;
	uuid_t		pfs_fsid;
	int		unit;
	int		opencnt;
	int		spancnt;
	uint64_t	keyid;
	int		serializing;
	int		last_error;
	char		cl_label[64];   /* from LNK_SPAN cl_label (host/dev) */
	char		fs_label[64];   /* from LNK_SPAN fs_label (serno str) */
	xa_tag_t	*open_tag;
	TAILQ_HEAD(, bio) bioq;		/* pending BIOs */
	TAILQ_HEAD(, xa_tag) tag_freeq;	/* available I/O tags */
	TAILQ_HEAD(, xa_tag) tag_pendq;	/* running I/O tags */
	struct lwkt_token tok;
};

typedef struct xa_softc	xa_softc_t;

struct xa_iocom {
	TAILQ_ENTRY(xa_iocom) entry;
	kdmsg_iocom_t	iocom;
	xa_softc_t	dummysc;
};

typedef struct xa_iocom xa_iocom_t;

static int xa_softc_cmp(xa_softc_t *sc1, xa_softc_t *sc2);
RB_GENERATE(xa_softc_tree, xa_softc, rbnode, xa_softc_cmp);
static struct xa_softc_tree xa_device_tree;

#define MAXTAGS		64	/* no real limit */

static int xdisk_attach(struct xdisk_attach_ioctl *xaioc);
static int xdisk_detach(struct xdisk_attach_ioctl *xaioc);
static void xaio_exit(kdmsg_iocom_t *iocom);
static int xaio_rcvdmsg(kdmsg_msg_t *msg);

static void xa_terminate_check(struct xa_softc *sc);

static xa_tag_t *xa_setup_cmd(xa_softc_t *sc, struct bio *bio);
static void xa_start(xa_tag_t *tag, kdmsg_msg_t *msg, int async);
static void xa_done(xa_tag_t *tag, int wasbio);
static void xa_release(xa_tag_t *tag, int wasbio);
static uint32_t xa_wait(xa_tag_t *tag);
static int xa_sync_completion(kdmsg_state_t *state, kdmsg_msg_t *msg);
static int xa_bio_completion(kdmsg_state_t *state, kdmsg_msg_t *msg);
static void xa_restart_deferred(xa_softc_t *sc);

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
static TAILQ_HEAD(, xa_iocom) xaiocomq;

/*
 * Module initialization
 */
static int
xdisk_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		TAILQ_INIT(&xaiocomq);
		RB_INIT(&xa_device_tree);
		xdisk_dev = make_dev(&xdisk_ops, 0,
				     UID_ROOT, GID_WHEEL, 0600, "xdisk");
		break;
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		if (xdisk_opencount || TAILQ_FIRST(&xaiocomq))
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

static int
xa_softc_cmp(xa_softc_t *sc1, xa_softc_t *sc2)
{
	return(bcmp(sc1->fs_label, sc2->fs_label, sizeof(sc1->fs_label)));
}

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
	xa_iocom_t *xaio;
	struct file *fp;

	/*
	 * Normalize ioctl params
	 */
	kprintf("xdisk_attach1\n");
	fp = holdfp(curproc->p_fd, xaioc->fd, -1);
	if (fp == NULL)
		return EINVAL;
	kprintf("xdisk_attach2\n");

	/*
	 * See if the serial number is already present.  If we are
	 * racing a termination the disk subsystem may still have
	 * duplicate entries not yet removed so we wait a bit and
	 * retry.
	 */
	lwkt_gettoken(&xdisk_token);

	xaio = kmalloc(sizeof(*xaio), M_XDISK, M_WAITOK | M_ZERO);
	kprintf("xdisk_attach3\n");
	kdmsg_iocom_init(&xaio->iocom, xaio,
			 KDMSG_IOCOMF_AUTOCONN,
			 M_XDISK, xaio_rcvdmsg);
	xaio->iocom.exit_func = xaio_exit;

	kdmsg_iocom_reconnect(&xaio->iocom, fp, "xdisk");

	/*
	 * Setup our LNK_CONN advertisement for autoinitiate.
	 *
	 * Our filter is setup to only accept PEER_BLOCK/SERVER
	 * advertisements.
	 *
	 * We need a unique pfs_fsid to avoid confusion.
	 */
	xaio->iocom.auto_lnk_conn.pfs_type = DMSG_PFSTYPE_CLIENT;
	xaio->iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	xaio->iocom.auto_lnk_conn.peer_type = DMSG_PEER_BLOCK;
	xaio->iocom.auto_lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;
	xaio->iocom.auto_lnk_conn.pfs_mask = 1LLU << DMSG_PFSTYPE_SERVER;
	ksnprintf(xaio->iocom.auto_lnk_conn.fs_label,
		  sizeof(xaio->iocom.auto_lnk_conn.fs_label),
		  "xdisk");
	kern_uuidgen(&xaio->iocom.auto_lnk_conn.pfs_fsid, 1);

	/*
	 * Setup our LNK_SPAN advertisement for autoinitiate
	 */
	TAILQ_INSERT_TAIL(&xaiocomq, xaio, entry);
	kdmsg_iocom_autoinitiate(&xaio->iocom, NULL);
	lwkt_reltoken(&xdisk_token);

	return 0;
}

static int
xdisk_detach(struct xdisk_attach_ioctl *xaioc)
{
	return EINVAL;
}

/*
 * Called from iocom core transmit thread upon disconnect.
 */
static
void
xaio_exit(kdmsg_iocom_t *iocom)
{
	xa_iocom_t *xaio = iocom->handle;

	kprintf("xdisk_detach -xaio_exit\n");
	lwkt_gettoken(&xdisk_token);
	TAILQ_REMOVE(&xaiocomq, xaio, entry);
	lwkt_reltoken(&xdisk_token);

	kfree(xaio, M_XDISK);
}

/*
 * Called from iocom core to handle messages that the iocom core does not
 * handle itself and for which a state function callback has not yet been
 * established.
 *
 * We primarily care about LNK_SPAN transactions here.
 */
static int
xaio_rcvdmsg(kdmsg_msg_t *msg)
{
	kdmsg_state_t	*state = msg->state;
	xa_iocom_t	*xaio = state->iocom->handle;
	xa_softc_t	*sc;

	kprintf("xdisk_rcvdmsg %08x\n", msg->any.head.cmd);
	lwkt_gettoken(&xdisk_token);

	switch(msg->tcmd) {
	case DMSG_LNK_SPAN | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * A LNK_SPAN transaction which is opened and closed
		 * degenerately is not useful to us, just ignore it.
		 */
		kdmsg_msg_reply(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_CREATE:
		/*
		 * Manage the tracking node for the remote LNK_SPAN.
		 *
		 * Return a streaming result, leaving the transaction open
		 * in both directions to allow sub-transactions.
		 */
		bcopy(msg->any.lnk_span.cl_label, xaio->dummysc.cl_label,
		      sizeof(xaio->dummysc.cl_label));
		xaio->dummysc.cl_label[sizeof(xaio->dummysc.cl_label) - 1] = 0;

		bcopy(msg->any.lnk_span.fs_label, xaio->dummysc.fs_label,
		      sizeof(xaio->dummysc.fs_label));
		xaio->dummysc.fs_label[sizeof(xaio->dummysc.fs_label) - 1] = 0;

		kprintf("xdisk: %s LNK_SPAN create\n",
			msg->any.lnk_span.fs_label);

		sc = RB_FIND(xa_softc_tree, &xa_device_tree, &xaio->dummysc);
		if (sc == NULL) {
			xa_softc_t *sctmp;
			xa_tag_t *tag;
			cdev_t dev;
			int unit;
			int n;

			sc = kmalloc(sizeof(*sc), M_XDISK, M_WAITOK | M_ZERO);
			bcopy(msg->any.lnk_span.cl_label, sc->cl_label,
			      sizeof(sc->cl_label));
			sc->cl_label[sizeof(sc->cl_label) - 1] = 0;
			bcopy(msg->any.lnk_span.fs_label, sc->fs_label,
			      sizeof(sc->fs_label));
			sc->fs_label[sizeof(sc->fs_label) - 1] = 0;

			/* XXX FIXME O(N^2) */
			unit = -1;
			do {
				++unit;
				RB_FOREACH(sctmp, xa_softc_tree,
					   &xa_device_tree) {
					if (sctmp->unit == unit)
						break;
				}
			} while (sctmp);

			sc->unit = unit;
			sc->serializing = 1;
			sc->spancnt = 1;
			lwkt_token_init(&sc->tok, "xa");
			TAILQ_INIT(&sc->spanq);
			TAILQ_INIT(&sc->bioq);
			TAILQ_INIT(&sc->tag_freeq);
			TAILQ_INIT(&sc->tag_pendq);
			RB_INSERT(xa_softc_tree, &xa_device_tree, sc);
			TAILQ_INSERT_TAIL(&sc->spanq, msg->state, user_entry);
			msg->state->any.xa_sc = sc;

			/*
			 * Setup block device
			 */
			for (n = 0; n < MAXTAGS; ++n) {
				tag = kmalloc(sizeof(*tag),
					      M_XDISK, M_WAITOK|M_ZERO);
				tag->sc = sc;
				TAILQ_INSERT_TAIL(&sc->tag_freeq, tag, entry);
			}

			if (sc->dev == NULL) {
				dev = disk_create(unit, &sc->disk, &xa_ops);
				dev->si_drv1 = sc;
				sc->dev = dev;
			}

			sc->info.d_media_blksize =
				msg->any.lnk_span.media.block.blksize;
			if (sc->info.d_media_blksize <= 0)
				sc->info.d_media_blksize = 1;
			sc->info.d_media_blocks =
				msg->any.lnk_span.media.block.bytes /
				sc->info.d_media_blksize;
			sc->info.d_dsflags = DSO_MBRQUIET | DSO_RAWPSIZE;
			sc->info.d_secpertrack = 32;
			sc->info.d_nheads = 64;
			sc->info.d_secpercyl = sc->info.d_secpertrack *
					       sc->info.d_nheads;
			sc->info.d_ncylinders = 0;
			if (sc->fs_label[0])
				sc->info.d_serialno = sc->fs_label;
			disk_setdiskinfo_sync(&sc->disk, &sc->info);
			xa_restart_deferred(sc);	/* eats serializing */
		} else {
			++sc->spancnt;
			TAILQ_INSERT_TAIL(&sc->spanq, msg->state, user_entry);
			msg->state->any.xa_sc = sc;
			if (sc->serializing == 0 && sc->open_tag == NULL) {
				sc->serializing = 1;
				xa_restart_deferred(sc); /* eats serializing */
			}
		}
		kdmsg_msg_result(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_DELETE:
	case DMSG_LNK_SPAN | DMSGF_DELETE | DMSGF_REPLY:
		/*
		 * Manage the tracking node for the remote LNK_SPAN.
		 *
		 * Return a final result, closing our end of the transaction.
		 */
		sc = msg->state->any.xa_sc;
		kprintf("xdisk: %s LNK_SPAN terminate\n", sc->fs_label);
		msg->state->any.xa_sc = NULL;
		TAILQ_REMOVE(&sc->spanq, msg->state, user_entry);
		--sc->spancnt;
		xa_terminate_check(sc);
		kdmsg_msg_reply(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_REPLY:
		/*
		 * Ignore unimplemented streaming replies on our LNK_SPAN
		 * transaction.
		 */
		break;
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
		 * Receive one or more replies to a shell command
		 * that we sent.  Just dump it to the console.
		 *
		 * This is a one-way packet but if not (e.g. if
		 * part of a streaming transaction), we will have
		 * already closed our end.
		 */
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("xdisk: DEBUGMSG: %s\n",
				msg->aux_data);
		}
		break;
	default:
		/*
		 * Unsupported one-way message, streaming message, or
		 * transaction.
		 *
		 * Terminate any unsupported transactions with an error
		 * and ignore any unsupported streaming messages.
		 *
		 * NOTE: This case also includes DMSG_LNK_ERROR messages
		 *	 which might be one-way, replying to those would
		 *	 cause an infinite ping-pong.
		 */
		if (msg->any.head.cmd & DMSGF_CREATE)
			kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
	lwkt_reltoken(&xdisk_token);

	return 0;
}

/*
 * Determine if we can destroy the xa_softc.
 *
 * Called with xdisk_token held.
 */
static
void
xa_terminate_check(struct xa_softc *sc)
{
	xa_tag_t *tag;

	/*
	 * Determine if we can destroy the softc.
	 */
	kprintf("xdisk: terminate check xa%d (%d,%d,%d)\n",
		sc->unit,
		sc->opencnt, sc->serializing, sc->spancnt);

	if (sc->opencnt || sc->serializing || sc->spancnt)
		return;
	sc->serializing = 1;
	KKASSERT(TAILQ_EMPTY(&sc->tag_pendq));

	RB_REMOVE(xa_softc_tree, &xa_device_tree, sc);

	if (sc->dev) {
		disk_destroy(&sc->disk);
		sc->dev->si_drv1 = NULL;
		sc->dev = NULL;
	}
	KKASSERT(sc->opencnt == 0);
	KKASSERT(TAILQ_EMPTY(&sc->tag_pendq));

	while ((tag = TAILQ_FIRST(&sc->tag_freeq)) != NULL) {
		TAILQ_REMOVE(&sc->tag_freeq, tag, entry);
		tag->sc = NULL;
		kfree(tag, M_XDISK);
	}
	kfree(sc, M_XDISK);
}

/************************************************************************
 *			   XA DEVICE INTERFACE				*
 ************************************************************************/

static int
xa_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	xa_softc_t *sc;
	int error;

	dev->si_bsize_phys = 512;
	dev->si_bsize_best = 32768;

	/*
	 * Interlock open with opencnt, wait for attachment operations
	 * to finish.
	 */
	lwkt_gettoken(&xdisk_token);
again:
	sc = dev->si_drv1;
	if (sc == NULL) {
		lwkt_reltoken(&xdisk_token);
		return ENXIO;	/* raced destruction */
	}
	if (sc->serializing) {
		tsleep(sc, 0, "xarace", hz / 10);
		goto again;
	}
	sc->serializing = 1;

	/*
	 * Serialize initial open
	 */
	if (sc->opencnt++ > 0) {
		lwkt_reltoken(&xdisk_token);
		return(0);
	}
	lwkt_reltoken(&xdisk_token);

	/*
	 * Issue BLK_OPEN if necessary.  ENXIO is returned if we have trouble.
	 */
	if (sc->open_tag == NULL) {
		xa_restart_deferred(sc); /* eats serializing */
	} else {
		sc->serializing = 0;
		wakeup(sc);
	}

	/*
	 * Wait for completion of the BLK_OPEN
	 */
	lwkt_gettoken(&xdisk_token);
	while (sc->serializing)
		tsleep(sc, 0, "xaopen", hz);

	error = sc->last_error;
	if (error) {
		KKASSERT(sc->opencnt > 0);
		--sc->opencnt;
		xa_terminate_check(sc);
		sc = NULL;	/* sc may be invalid now */
	}
	lwkt_reltoken(&xdisk_token);

	return (error);
}

static int
xa_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	xa_softc_t *sc;
	xa_tag_t *tag;

	sc = dev->si_drv1;
	if (sc == NULL)
		return ENXIO;	/* raced destruction */
	lwkt_gettoken(&xdisk_token);
	lwkt_gettoken(&sc->tok);

	/*
	 * NOTE: Clearing open_tag allows a concurrent open to re-open
	 *	 the device and prevents autonomous completion of the tag.
	 */
	if (sc->opencnt == 1 && sc->open_tag) {
		tag = sc->open_tag;
		sc->open_tag = NULL;
		kdmsg_state_reply(tag->state, 0);	/* close our side */
		xa_wait(tag);				/* wait on remote */
	}
	lwkt_reltoken(&sc->tok);
	KKASSERT(sc->opencnt > 0);
	--sc->opencnt;
	xa_terminate_check(sc);
	lwkt_reltoken(&xdisk_token);

	return(0);
}

static int
xa_strategy(struct dev_strategy_args *ap)
{
	xa_softc_t *sc = ap->a_head.a_dev->si_drv1;
	xa_tag_t *tag;
	struct bio *bio = ap->a_bio;

	/*
	 * Allow potentially temporary link failures to fail the I/Os
	 * only if the device is not open.  That is, we allow the disk
	 * probe code prior to mount to fail.
	 */
	if (sc->opencnt == 0) {
		bio->bio_buf->b_error = ENXIO;
		bio->bio_buf->b_flags |= B_ERROR;
		biodone(bio);
		return(0);
	}

	tag = xa_setup_cmd(sc, bio);
	if (tag)
		xa_start(tag, NULL, 1);
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
	struct xa_softc *sc;

	if ((sc = ap->a_head.a_dev->si_drv1) == NULL)
		return (ENXIO);
	ap->a_result = sc->info.d_media_blocks;
	return (0);
}

/************************************************************************
 *		    XA BLOCK PROTOCOL STATE MACHINE			*
 ************************************************************************
 *
 * Implement tag/msg setup and related functions.
 */
static xa_tag_t *
xa_setup_cmd(xa_softc_t *sc, struct bio *bio)
{
	xa_tag_t *tag;

	/*
	 * Only get a tag if we have a valid virtual circuit to the server.
	 */
	lwkt_gettoken(&sc->tok);
	if ((tag = TAILQ_FIRST(&sc->tag_freeq)) != NULL) {
		TAILQ_REMOVE(&sc->tag_freeq, tag, entry);
		tag->bio = bio;
		TAILQ_INSERT_TAIL(&sc->tag_pendq, tag, entry);
	}

	/*
	 * If we can't dispatch now and this is a bio, queue it for later.
	 */
	if (tag == NULL && bio) {
		TAILQ_INSERT_TAIL(&sc->bioq, bio, bio_act);
	}
	lwkt_reltoken(&sc->tok);

	return (tag);
}

static void
xa_start(xa_tag_t *tag, kdmsg_msg_t *msg, int async)
{
	xa_softc_t *sc = tag->sc;

	tag->done = 0;
	tag->async = async;

	if (msg == NULL) {
		struct bio *bio;
		struct buf *bp;

		KKASSERT(tag->bio);
		bio = tag->bio;
		bp = bio->bio_buf;

		switch(bp->b_cmd) {
		case BUF_CMD_READ:
			msg = kdmsg_msg_alloc(sc->open_tag->state,
					      DMSG_BLK_READ |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_read.keyid = sc->keyid;
			msg->any.blk_read.offset = bio->bio_offset;
			msg->any.blk_read.bytes = bp->b_bcount;
			break;
		case BUF_CMD_WRITE:
			msg = kdmsg_msg_alloc(sc->open_tag->state,
					      DMSG_BLK_WRITE |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_write.keyid = sc->keyid;
			msg->any.blk_write.offset = bio->bio_offset;
			msg->any.blk_write.bytes = bp->b_bcount;
			msg->aux_data = bp->b_data;
			msg->aux_size = bp->b_bcount;
			break;
		case BUF_CMD_FLUSH:
			msg = kdmsg_msg_alloc(sc->open_tag->state,
					      DMSG_BLK_FLUSH |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_flush.keyid = sc->keyid;
			msg->any.blk_flush.offset = bio->bio_offset;
			msg->any.blk_flush.bytes = bp->b_bcount;
			break;
		case BUF_CMD_FREEBLKS:
			msg = kdmsg_msg_alloc(sc->open_tag->state,
					      DMSG_BLK_FREEBLKS |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_freeblks.keyid = sc->keyid;
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

	if (msg) {
		tag->state = msg->state;
		kdmsg_msg_write(msg);
	} else {
		tag->status.head.error = DMSG_ERR_IO;
		xa_done(tag, 1);
	}
}

static uint32_t
xa_wait(xa_tag_t *tag)
{
	xa_softc_t *sc = tag->sc;
	uint32_t error;

	kprintf("xdisk: xa_wait  %p\n", tag);

	lwkt_gettoken(&sc->tok);
	tag->waiting = 1;
	while (tag->done == 0)
		tsleep(tag, 0, "xawait", 0);
	lwkt_reltoken(&sc->tok);
	error = tag->status.head.error;
	tag->waiting = 0;
	xa_release(tag, 0);

	return error;
}

static void
xa_done(xa_tag_t *tag, int wasbio)
{
	KKASSERT(tag->bio == NULL);

	tag->state = NULL;
	tag->done = 1;
	if (tag->waiting)
		wakeup(tag);
	if (tag->async)
		xa_release(tag, wasbio);
}

static
void
xa_release(xa_tag_t *tag, int wasbio)
{
	xa_softc_t *sc = tag->sc;
	struct bio *bio;

	lwkt_gettoken(&sc->tok);
	if (wasbio && (bio = TAILQ_FIRST(&sc->bioq)) != NULL) {
		TAILQ_REMOVE(&sc->bioq, bio, bio_act);
		tag->bio = bio;
		lwkt_reltoken(&sc->tok);
		xa_start(tag, NULL, 1);
	} else {
		TAILQ_REMOVE(&sc->tag_pendq, tag, entry);
		TAILQ_INSERT_TAIL(&sc->tag_freeq, tag, entry);
		lwkt_reltoken(&sc->tok);
	}
}

/*
 * Handle messages under the BLKOPEN transaction.
 */
static int
xa_sync_completion(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	xa_tag_t *tag = state->any.any;
	xa_softc_t *sc = tag->sc;
	struct bio *bio;

	/*
	 * If the tag has been cleaned out we already closed our side
	 * of the transaction and we are waiting for the other side to
	 * close.
	 */
	if (tag == NULL) {
		if (msg->any.head.cmd & DMSGF_CREATE)
			kdmsg_state_reply(state, DMSG_ERR_LOSTLINK);
		return 0;
	}

	/*
	 * Validate the tag
	 */
	lwkt_gettoken(&sc->tok);

	/*
	 * Handle initial response to our open and restart any deferred
	 * BIOs on success.
	 *
	 * NOTE: DELETE may also be set.
	 */
	if (msg->any.head.cmd & DMSGF_CREATE) {
		switch(msg->any.head.cmd & DMSGF_CMDSWMASK) {
		case DMSG_LNK_ERROR | DMSGF_REPLY:
			bzero(&tag->status, sizeof(tag->status));
			tag->status.head = msg->any.head;
			break;
		case DMSG_BLK_ERROR | DMSGF_REPLY:
			tag->status = msg->any.blk_error;
			break;
		}
		sc->last_error = tag->status.head.error;
		kprintf("xdisk: blk_open completion status %d\n",
			sc->last_error);
		if (sc->last_error == 0) {
			while ((bio = TAILQ_FIRST(&sc->bioq)) != NULL) {
				tag = xa_setup_cmd(sc, NULL);
				if (tag == NULL)
					break;
				TAILQ_REMOVE(&sc->bioq, bio, bio_act);
				tag->bio = bio;
				xa_start(tag, NULL, 1);
			}
		}
		sc->serializing = 0;
		wakeup(sc);
	}

	/*
	 * Handle unexpected termination (or lost comm channel) from other
	 * side.  Autonomous completion only if open_tag matches,
	 * otherwise another thread is probably waiting on the tag.
	 *
	 * (see xa_close() for other interactions)
	 */
	if (msg->any.head.cmd & DMSGF_DELETE) {
		kdmsg_state_reply(tag->state, 0);
		if (sc->open_tag == tag) {
			sc->open_tag = NULL;
			xa_done(tag, 0);
		} else {
			tag->async = 0;
			xa_done(tag, 0);
		}
	}
	lwkt_reltoken(&sc->tok);
	return (0);
}

static int
xa_bio_completion(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	xa_tag_t *tag = state->any.any;
	xa_softc_t *sc = tag->sc;
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
	    (msg->any.head.cmd & DMSGF_DELETE) && sc->opencnt) {
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
	 * and we put the BIO back onto the bioq for a later restart.
	 */
handle_repend:
	lwkt_gettoken(&sc->tok);
	kprintf("BIO CIRC FAILURE, REPEND BIO %p\n", bio);
	tag->bio = NULL;
	xa_done(tag, 0);
	if ((state->txcmd & DMSGF_DELETE) == 0)
		kdmsg_msg_reply(msg, 0);

	/*
	 * Requeue the bio
	 */
	TAILQ_INSERT_TAIL(&sc->bioq, bio, bio_act);

	lwkt_reltoken(&sc->tok);
	return (0);
}

/*
 * Restart as much deferred I/O as we can.  The serializer is set and we
 * eat it (clear it) when done.
 *
 * Called with sc->tok held
 */
static
void
xa_restart_deferred(xa_softc_t *sc)
{
	kdmsg_state_t *span;
	kdmsg_msg_t *msg;
	xa_tag_t *tag;
	int error;

	KKASSERT(sc->serializing);

	/*
	 * Determine if a restart is needed.
	 */
	if (sc->opencnt == 0) {
		/*
		 * Device is not open, nothing to do, eat serializing.
		 */
		sc->serializing = 0;
		wakeup(sc);
	} else if (sc->open_tag == NULL) {
		/*
		 * BLK_OPEN required before we can restart any BIOs.
		 * Select the best LNK_SPAN to issue the BLK_OPEN under.
		 *
		 * serializing interlocks waiting open()s.
		 */
		error = 0;
		TAILQ_FOREACH(span, &sc->spanq, user_entry) {
			if ((span->rxcmd & DMSGF_DELETE) == 0)
				break;
		}
		if (span == NULL)
			error = ENXIO;

		if (error == 0) {
			tag = xa_setup_cmd(sc, NULL);
			if (tag == NULL)
				error = ENXIO;
		}
		if (error == 0) {
			kprintf("xdisk: BLK_OPEN\n");
			sc->open_tag = tag;
			msg = kdmsg_msg_alloc(span,
					      DMSG_BLK_OPEN |
					      DMSGF_CREATE,
					      xa_sync_completion, tag);
			msg->any.blk_open.modes = DMSG_BLKOPEN_RD;
			xa_start(tag, msg, 0);
		}
		if (error) {
			sc->serializing = 0;
			wakeup(sc);
		}
		/* else leave serializing set until BLK_OPEN response */
	} else {
		/* nothing to do */
		sc->serializing = 0;
		wakeup(sc);
	}
}
