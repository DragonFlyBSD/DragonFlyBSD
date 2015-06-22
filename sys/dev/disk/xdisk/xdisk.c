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

static int xa_active;
SYSCTL_INT(_debug, OID_AUTO, xa_active, CTLFLAG_RW, &xa_active, 0,
	   "Number of active xdisk IOs");
static uint64_t xa_last;
SYSCTL_ULONG(_debug, OID_AUTO, xa_last, CTLFLAG_RW, &xa_last, 0,
	   "Offset of last xdisk IO");
static int xa_debug = 1;
SYSCTL_INT(_debug, OID_AUTO, xa_debug, CTLFLAG_RW, &xa_debug, 0,
	   "xdisk debugging");

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
	struct devstat	stats;
	struct disk_info info;
	struct disk	disk;
	uuid_t		peer_id;
	int		unit;
	int		opencnt;
	int		spancnt;
	uint64_t	keyid;
	int		serializing;
	int		last_error;
	int		terminating;
	char		peer_label[64];	/* from LNK_SPAN host/dev */
	char		pfs_label[64];	/* from LNK_SPAN serno */
	xa_tag_t	*open_tag;
	TAILQ_HEAD(, bio) bioq;		/* pending BIOs */
	TAILQ_HEAD(, xa_tag) tag_freeq;	/* available I/O tags */
	TAILQ_HEAD(, xa_tag) tag_pendq;	/* running I/O tags */
	struct lock	lk;
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

#define xa_printf(level, ctl, ...)	\
	if (xa_debug >= (level)) kprintf("xdisk: " ctl, __VA_ARGS__)

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

static int xdisk_opencount;
static cdev_t xdisk_dev;
struct lock xdisk_lk;
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
		lockinit(&xdisk_lk, "xdisk", 0, 0);
		xdisk_dev = make_dev(&xdisk_ops, 0,
				     UID_ROOT, GID_WHEEL, 0600, "xdisk");
		break;
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		if (!RB_EMPTY(&xa_device_tree))
			return (EBUSY);
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
	return(strcmp(sc1->pfs_label, sc2->pfs_label));
}

/*
 * Control device
 */
static int
xdisk_open(struct dev_open_args *ap)
{
	lockmgr(&xdisk_lk, LK_EXCLUSIVE);
	++xdisk_opencount;
	lockmgr(&xdisk_lk, LK_RELEASE);
	return(0);
}

static int
xdisk_close(struct dev_close_args *ap)
{
	lockmgr(&xdisk_lk, LK_EXCLUSIVE);
	--xdisk_opencount;
	lockmgr(&xdisk_lk, LK_RELEASE);
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
	fp = holdfp(curproc->p_fd, xaioc->fd, -1);
	if (fp == NULL)
		return EINVAL;
	xa_printf(1, "xdisk_attach fp=%p\n", fp);

	/*
	 * See if the serial number is already present.  If we are
	 * racing a termination the disk subsystem may still have
	 * duplicate entries not yet removed so we wait a bit and
	 * retry.
	 */
	lockmgr(&xdisk_lk, LK_EXCLUSIVE);

	xaio = kmalloc(sizeof(*xaio), M_XDISK, M_WAITOK | M_ZERO);
	kdmsg_iocom_init(&xaio->iocom, xaio,
			 KDMSG_IOCOMF_AUTOCONN,
			 M_XDISK, xaio_rcvdmsg);
	xaio->iocom.exit_func = xaio_exit;

	kdmsg_iocom_reconnect(&xaio->iocom, fp, "xdisk");

	/*
	 * Setup our LNK_CONN advertisement for autoinitiate.
	 *
	 * Our filter is setup to only accept PEER_BLOCK advertisements.
	 * XXX no peer_id filter.
	 *
	 * We need a unique pfs_fsid to avoid confusion.
	 */
	xaio->iocom.auto_lnk_conn.peer_type = DMSG_PEER_CLIENT;
	xaio->iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	xaio->iocom.auto_lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;
	ksnprintf(xaio->iocom.auto_lnk_conn.peer_label,
		  sizeof(xaio->iocom.auto_lnk_conn.peer_label),
		  "%s/xdisk",
		  hostname);
	/* kern_uuidgen(&xaio->iocom.auto_lnk_conn.pfs_fsid, 1); */

	/*
	 * Setup our LNK_SPAN advertisement for autoinitiate
	 */
	TAILQ_INSERT_TAIL(&xaiocomq, xaio, entry);
	kdmsg_iocom_autoinitiate(&xaio->iocom, NULL);

	lockmgr(&xdisk_lk, LK_RELEASE);

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

	lockmgr(&xdisk_lk, LK_EXCLUSIVE);
	xa_printf(1, "%s", "xdisk_detach [xaio_exit()]\n");
	TAILQ_REMOVE(&xaiocomq, xaio, entry);
	lockmgr(&xdisk_lk, LK_RELEASE);

	kdmsg_iocom_uninit(&xaio->iocom);

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

	if (state) {
		xa_printf(4,
			"xdisk - rcvmsg state=%p rx=%08x tx=%08x msgcmd=%08x\n",
			state, state->rxcmd, state->txcmd,
			msg->any.head.cmd);
	}
	lockmgr(&xdisk_lk, LK_EXCLUSIVE);

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
		bcopy(msg->any.lnk_span.peer_label, xaio->dummysc.peer_label,
		      sizeof(xaio->dummysc.peer_label));
		xaio->dummysc.peer_label[
			sizeof(xaio->dummysc.peer_label) - 1] = 0;

		bcopy(msg->any.lnk_span.pfs_label, xaio->dummysc.pfs_label,
		      sizeof(xaio->dummysc.pfs_label));
		xaio->dummysc.pfs_label[
			sizeof(xaio->dummysc.pfs_label) - 1] = 0;

		xa_printf(3, "LINK_SPAN state %p create for %s\n",
			  msg->state, msg->any.lnk_span.pfs_label);

		sc = RB_FIND(xa_softc_tree, &xa_device_tree, &xaio->dummysc);
		if (sc == NULL) {
			xa_softc_t *sctmp;
			xa_tag_t *tag;
			cdev_t dev;
			int unit;
			int n;

			sc = kmalloc(sizeof(*sc), M_XDISK, M_WAITOK | M_ZERO);
			bcopy(msg->any.lnk_span.peer_label, sc->peer_label,
			      sizeof(sc->peer_label));
			sc->peer_label[sizeof(sc->peer_label) - 1] = 0;
			bcopy(msg->any.lnk_span.pfs_label, sc->pfs_label,
			      sizeof(sc->pfs_label));
			sc->pfs_label[sizeof(sc->pfs_label) - 1] = 0;

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
			lockinit(&sc->lk, "xalk", 0, 0);
			TAILQ_INIT(&sc->spanq);
			TAILQ_INIT(&sc->bioq);
			TAILQ_INIT(&sc->tag_freeq);
			TAILQ_INIT(&sc->tag_pendq);

			lockmgr(&sc->lk, LK_EXCLUSIVE);
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
				devstat_add_entry(&sc->stats, "xa", unit,
						  DEV_BSIZE,
						  DEVSTAT_NO_ORDERED_TAGS,
						  DEVSTAT_TYPE_DIRECT |
						  DEVSTAT_TYPE_IF_OTHER,
						  DEVSTAT_PRIORITY_OTHER);
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
			if (sc->pfs_label[0])
				sc->info.d_serialno = sc->pfs_label;
			/*
			 * WARNING! disk_setdiskinfo() must be asynchronous
			 *	    because we are in the rxmsg thread.  If
			 *	    it is synchronous and issues more disk
			 *	    I/Os, we will deadlock.
			 */
			disk_setdiskinfo(&sc->disk, &sc->info);
			xa_restart_deferred(sc);	/* eats serializing */
			lockmgr(&sc->lk, LK_RELEASE);
		} else {
			lockmgr(&sc->lk, LK_EXCLUSIVE);
			++sc->spancnt;
			TAILQ_INSERT_TAIL(&sc->spanq, msg->state, user_entry);
			msg->state->any.xa_sc = sc;
			if (sc->serializing == 0 && sc->open_tag == NULL) {
				sc->serializing = 1;
				xa_restart_deferred(sc); /* eats serializing */
			}
			lockmgr(&sc->lk, LK_RELEASE);
			if (sc->dev && sc->dev->si_disk) {
				xa_printf(1, "reprobe disk: %s\n",
					  sc->pfs_label);
				disk_msg_send(DISK_DISK_REPROBE,
					      sc->dev->si_disk,
					      NULL);
			}
		}
		xa_printf(2, "sc %p spancnt %d\n", sc, sc->spancnt);
		kdmsg_msg_result(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_DELETE:
		/*
		 * Manage the tracking node for the remote LNK_SPAN.
		 *
		 * Return a final result, closing our end of the transaction.
		 */
		sc = msg->state->any.xa_sc;
		xa_printf(3, "LINK_SPAN state %p delete for %s (sc=%p)\n",
			  msg->state, (sc ? sc->pfs_label : "(null)"), sc);
		lockmgr(&sc->lk, LK_EXCLUSIVE);
		msg->state->any.xa_sc = NULL;
		TAILQ_REMOVE(&sc->spanq, msg->state, user_entry);
		--sc->spancnt;

		xa_printf(2, "sc %p spancnt %d\n", sc, sc->spancnt);

		/*
		 * Spans can come and go as the graph stabilizes, so if
		 * we lose a span along with sc->open_tag we may be able
		 * to restart the I/Os on a different span.
		 */
		if (sc->spancnt &&
		    sc->serializing == 0 && sc->open_tag == NULL) {
			sc->serializing = 1;
			xa_restart_deferred(sc);
		}
		lockmgr(&sc->lk, LK_RELEASE);
		kdmsg_msg_reply(msg, 0);

#if 0
		/*
		 * Termination
		 */
		if (sc->spancnt == 0)
			xa_terminate_check(sc);
#endif
		break;
	case DMSG_LNK_SPAN | DMSGF_DELETE | DMSGF_REPLY:
		/*
		 * Ignore unimplemented streaming replies on our LNK_SPAN
		 * transaction.
		 */
		xa_printf(3, "LINK_SPAN state %p delete+reply\n",
			  msg->state);
		break;
	case DMSG_LNK_SPAN | DMSGF_REPLY:
		/*
		 * Ignore unimplemented streaming replies on our LNK_SPAN
		 * transaction.
		 */
		xa_printf(3, "LINK_SPAN state %p reply\n",
			  msg->state);
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
			xa_printf(0, "DEBUGMSG: %s\n", msg->aux_data);
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
	lockmgr(&xdisk_lk, LK_RELEASE);

	return 0;
}

/*
 * Determine if we can destroy the xa_softc.
 *
 * Called with xdisk_lk held.
 */
static
void
xa_terminate_check(struct xa_softc *sc)
{
	xa_tag_t *tag;

	/*
	 * Determine if we can destroy the softc.
	 */
	xa_printf(1, "Terminate check xa%d (%d,%d,%d) sc=%p ",
		sc->unit,
		sc->opencnt, sc->serializing, sc->spancnt,
		sc);

	if (sc->opencnt || sc->serializing || sc->spancnt ||
	    TAILQ_FIRST(&sc->bioq) || TAILQ_FIRST(&sc->tag_pendq)) {
		xa_printf(1, "%s", "(leave intact)\n");
		return;
	}

	/*
	 * Remove from device tree, a race with a new incoming span
	 * will create a new softc and disk.
	 */
	RB_REMOVE(xa_softc_tree, &xa_device_tree, sc);
	sc->terminating = 1;

	/*
	 * Device has to go first to prevent device ops races.
	 */
	if (sc->dev) {
		disk_destroy(&sc->disk);
		devstat_remove_entry(&sc->stats);
		sc->dev->si_drv1 = NULL;
		sc->dev = NULL;
	}

	xa_printf(1, "%s", "(remove from tree)\n");
	sc->serializing = 1;
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
	lockmgr(&xdisk_lk, LK_EXCLUSIVE);
again:
	sc = dev->si_drv1;
	if (sc == NULL) {
		lockmgr(&xdisk_lk, LK_RELEASE);
		return ENXIO;	/* raced destruction */
	}
	if (sc->serializing) {
		tsleep(sc, 0, "xarace", hz / 10);
		goto again;
	}
	if (sc->terminating) {
		lockmgr(&xdisk_lk, LK_RELEASE);
		return ENXIO;	/* raced destruction */
	}
	sc->serializing = 1;

	/*
	 * Serialize initial open
	 */
	if (sc->opencnt++ > 0) {
		sc->serializing = 0;
		wakeup(sc);
		lockmgr(&xdisk_lk, LK_RELEASE);
		return(0);
	}

	/*
	 * Issue BLK_OPEN if necessary.  ENXIO is returned if we have trouble.
	 */
	if (sc->open_tag == NULL) {
		lockmgr(&sc->lk, LK_EXCLUSIVE);
		xa_restart_deferred(sc); /* eats serializing */
		lockmgr(&sc->lk, LK_RELEASE);
	} else {
		sc->serializing = 0;
		wakeup(sc);
	}
	lockmgr(&xdisk_lk, LK_RELEASE);

	/*
	 * Wait for completion of the BLK_OPEN
	 */
	lockmgr(&xdisk_lk, LK_EXCLUSIVE);
	while (sc->serializing)
		lksleep(sc, &xdisk_lk, 0, "xaopen", hz);

	error = sc->last_error;
	if (error) {
		KKASSERT(sc->opencnt > 0);
		--sc->opencnt;
		xa_terminate_check(sc);
		sc = NULL;	/* sc may be invalid now */
	}
	lockmgr(&xdisk_lk, LK_RELEASE);

	return (error);
}

static int
xa_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	xa_softc_t *sc;
	xa_tag_t *tag;

	lockmgr(&xdisk_lk, LK_EXCLUSIVE);
	sc = dev->si_drv1;
	if (sc == NULL) {
		lockmgr(&sc->lk, LK_RELEASE);
		return ENXIO;	/* raced destruction */
	}
	if (sc->terminating) {
		lockmgr(&sc->lk, LK_RELEASE);
		return ENXIO;	/* raced destruction */
	}
	lockmgr(&sc->lk, LK_EXCLUSIVE);

	/*
	 * NOTE: Clearing open_tag allows a concurrent open to re-open
	 *	 the device and prevents autonomous completion of the tag.
	 */
	if (sc->opencnt == 1 && sc->open_tag) {
		tag = sc->open_tag;
		sc->open_tag = NULL;
		lockmgr(&sc->lk, LK_RELEASE);
		kdmsg_state_reply(tag->state, 0);	/* close our side */
		xa_wait(tag);				/* wait on remote */
	} else {
		lockmgr(&sc->lk, LK_RELEASE);
	}
	KKASSERT(sc->opencnt > 0);
	--sc->opencnt;
	xa_terminate_check(sc);
	lockmgr(&xdisk_lk, LK_RELEASE);

	return(0);
}

static int
xa_strategy(struct dev_strategy_args *ap)
{
	xa_softc_t *sc = ap->a_head.a_dev->si_drv1;
	xa_tag_t *tag;
	struct bio *bio = ap->a_bio;

	devstat_start_transaction(&sc->stats);
	atomic_add_int(&xa_active, 1);
	xa_last = bio->bio_offset;

	/*
	 * If no tags are available NULL is returned and the bio is
	 * placed on sc->bioq.
	 */
	lockmgr(&sc->lk, LK_EXCLUSIVE);
	tag = xa_setup_cmd(sc, bio);
	if (tag)
		xa_start(tag, NULL, 1);
	lockmgr(&sc->lk, LK_RELEASE);

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
 * Called with sc->lk held.
 */
static xa_tag_t *
xa_setup_cmd(xa_softc_t *sc, struct bio *bio)
{
	xa_tag_t *tag;

	/*
	 * Only get a tag if we have a valid virtual circuit to the server.
	 */
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

	return (tag);
}

/*
 * Called with sc->lk held
 */
static void
xa_start(xa_tag_t *tag, kdmsg_msg_t *msg, int async)
{
	xa_softc_t *sc = tag->sc;

	tag->done = 0;
	tag->async = async;
	tag->status.head.error = DMSG_ERR_IO;	/* fallback error */

	if (msg == NULL) {
		struct bio *bio;
		struct buf *bp;
		kdmsg_state_t *trans;

		if (sc->opencnt == 0 || sc->open_tag == NULL) {
			TAILQ_FOREACH(trans, &sc->spanq, user_entry) {
				if ((trans->rxcmd & DMSGF_DELETE) == 0)
					break;
			}
		} else {
			trans = sc->open_tag->state;
		}
		if (trans == NULL)
			goto skip;

		KKASSERT(tag->bio);
		bio = tag->bio;
		bp = bio->bio_buf;

		switch(bp->b_cmd) {
		case BUF_CMD_READ:
			msg = kdmsg_msg_alloc(trans,
					      DMSG_BLK_READ |
					      DMSGF_CREATE |
					      DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_read.keyid = sc->keyid;
			msg->any.blk_read.offset = bio->bio_offset;
			msg->any.blk_read.bytes = bp->b_bcount;
			break;
		case BUF_CMD_WRITE:
			msg = kdmsg_msg_alloc(trans,
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
			msg = kdmsg_msg_alloc(trans,
					      DMSG_BLK_FLUSH |
					      DMSGF_CREATE | DMSGF_DELETE,
					      xa_bio_completion, tag);
			msg->any.blk_flush.keyid = sc->keyid;
			msg->any.blk_flush.offset = bio->bio_offset;
			msg->any.blk_flush.bytes = bp->b_bcount;
			break;
		case BUF_CMD_FREEBLKS:
			msg = kdmsg_msg_alloc(trans,
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
			devstat_end_transaction_buf(&sc->stats, bp);
			atomic_add_int(&xa_active, -1);
			biodone(bio);
			tag->bio = NULL;
			break;
		}
	}

	/*
	 * If no msg was allocated we likely could not find a good span.
	 */
skip:
	if (msg) {
		/*
		 * Message was passed in or constructed.
		 */
		tag->state = msg->state;
		lockmgr(&sc->lk, LK_RELEASE);
		kdmsg_msg_write(msg);
		lockmgr(&sc->lk, LK_EXCLUSIVE);
	} else if (tag->bio &&
		   (tag->bio->bio_buf->b_flags & B_FAILONDIS) == 0) {
		/*
		 * No spans available but BIO is not allowed to fail
		 * on connectivity problems.  Requeue the BIO.
		 */
		TAILQ_INSERT_TAIL(&sc->bioq, tag->bio, bio_act);
		tag->bio = NULL;
		lockmgr(&sc->lk, LK_RELEASE);
		xa_done(tag, 1);
		lockmgr(&sc->lk, LK_EXCLUSIVE);
	} else {
		/*
		 * No spans available, bio is allowed to fail.
		 */
		lockmgr(&sc->lk, LK_RELEASE);
		tag->status.head.error = DMSG_ERR_IO;
		xa_done(tag, 1);
		lockmgr(&sc->lk, LK_EXCLUSIVE);
	}
}

static uint32_t
xa_wait(xa_tag_t *tag)
{
	xa_softc_t *sc = tag->sc;
	uint32_t error;

	lockmgr(&sc->lk, LK_EXCLUSIVE);
	tag->waiting = 1;
	while (tag->done == 0)
		lksleep(tag, &sc->lk, 0, "xawait", 0);
	lockmgr(&sc->lk, LK_RELEASE);

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

/*
 * Release a tag.  If everything looks ok and there are pending BIOs
 * (due to all tags in-use), we can use the tag to start the next BIO.
 * Do not try to restart if the connection is currently failed.
 */
static
void
xa_release(xa_tag_t *tag, int wasbio)
{
	xa_softc_t *sc = tag->sc;
	struct bio *bio;

	if ((bio = tag->bio) != NULL) {
		struct buf *bp = bio->bio_buf;

		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		devstat_end_transaction_buf(&sc->stats, bp);
		atomic_add_int(&xa_active, -1);
		biodone(bio);
		tag->bio = NULL;
	}

	lockmgr(&sc->lk, LK_EXCLUSIVE);

	if (wasbio && sc->open_tag &&
	    (bio = TAILQ_FIRST(&sc->bioq)) != NULL) {
		TAILQ_REMOVE(&sc->bioq, bio, bio_act);
		tag->bio = bio;
		xa_start(tag, NULL, 1);
	} else {
		TAILQ_REMOVE(&sc->tag_pendq, tag, entry);
		TAILQ_INSERT_TAIL(&sc->tag_freeq, tag, entry);
	}
	lockmgr(&sc->lk, LK_RELEASE);
}

/*
 * Handle messages under the BLKOPEN transaction.
 */
static int
xa_sync_completion(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	xa_tag_t *tag = state->any.any;
	xa_softc_t *sc;
	struct bio *bio;

	/*
	 * If the tag has been cleaned out we already closed our side
	 * of the transaction and we are waiting for the other side to
	 * close.
	 */
	xa_printf(1, "xa_sync_completion: tag %p msg %08x state %p\n",
		  tag, msg->any.head.cmd, msg->state);

	if (tag == NULL) {
		if (msg->any.head.cmd & DMSGF_CREATE)
			kdmsg_state_reply(state, DMSG_ERR_LOSTLINK);
		return 0;
	}
	sc = tag->sc;

	/*
	 * Validate the tag
	 */
	lockmgr(&sc->lk, LK_EXCLUSIVE);

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
		xa_printf(1, "blk_open completion status %d\n",
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
	lockmgr(&sc->lk, LK_RELEASE);

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
	 * If the device is open stall the bio on DMSG errors.  If an
	 * actual I/O error occured on the remote device, DMSG_ERR_IO
	 * will be returned.
	 */
	if (tag->status.head.error &&
	    (msg->any.head.cmd & DMSGF_DELETE) && sc->opencnt) {
		if (tag->status.head.error != DMSG_ERR_IO)
			goto handle_repend;
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
		if (tag->status.head.error != 0) {
			bp->b_error = EIO;
			bp->b_flags |= B_ERROR;
		} else {
			bp->b_resid = 0;
		}
		devstat_end_transaction_buf(&sc->stats, bp);
		atomic_add_int(&xa_active, -1);
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
	 *
	 * probe I/Os (where the device is not open) will be failed
	 * instead of requeued.
	 */
handle_repend:
	tag->bio = NULL;
	if (bio->bio_buf->b_flags & B_FAILONDIS) {
		xa_printf(1, "xa_strategy: lost link, fail probe bp %p\n",
			  bio->bio_buf);
		bio->bio_buf->b_error = ENXIO;
		bio->bio_buf->b_flags |= B_ERROR;
		biodone(bio);
		bio = NULL;
	} else {
		xa_printf(1, "xa_strategy: lost link, requeue bp %p\n",
			  bio->bio_buf);
	}
	xa_done(tag, 0);
	if ((state->txcmd & DMSGF_DELETE) == 0)
		kdmsg_msg_reply(msg, 0);

	/*
	 * Requeue the bio
	 */
	if (bio) {
		lockmgr(&sc->lk, LK_EXCLUSIVE);
		TAILQ_INSERT_TAIL(&sc->bioq, bio, bio_act);
		lockmgr(&sc->lk, LK_RELEASE);
	}
	return (0);
}

/*
 * Restart as much deferred I/O as we can.  The serializer is set and we
 * eat it (clear it) when done.
 *
 * Called with sc->lk held
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
			sc->open_tag = tag;
			msg = kdmsg_msg_alloc(span,
					      DMSG_BLK_OPEN |
					      DMSGF_CREATE,
					      xa_sync_completion, tag);
			msg->any.blk_open.modes = DMSG_BLKOPEN_RD;
			xa_printf(1,
				  "BLK_OPEN tag %p state %p "
				  "span-state %p\n",
				  tag, msg->state, span);
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
