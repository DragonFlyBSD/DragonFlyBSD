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

static int xdisk_attach(struct xdisk_attach_ioctl *xaioc);
static void xa_exit(kdmsg_iocom_t *iocom);
static int xa_msg_conn_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);
static int xa_msg_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);
static int xa_lnk_rcvmsg(kdmsg_msg_t *msg);
static int xa_lnk_dbgmsg(kdmsg_msg_t *msg);
static int xa_adhoc_input(kdmsg_msg_t *msg);

MALLOC_DEFINE(M_XDISK, "Networked disk client", "Network Disks");

/*
 * Control device, issue ioctls to create xa devices.
 */
static d_open_t xdisk_open;
static d_close_t xdisk_close;
static d_ioctl_t xdisk_ioctl;

static struct dev_ops xdisk_ops = {
	{ "xdisk", 0, D_MPSAFE },
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
	{ "xa", 0, D_DISK | D_CANFREE | D_MPSAFE },
        .d_open =	xa_open,
        .d_close =	xa_close,
        .d_ioctl =	xa_ioctl,
        .d_read =	physread,
        .d_write =	physwrite,
        .d_strategy =	xa_strategy,
	.d_psize =	xa_size
};

struct xa_softc {
	TAILQ_ENTRY(xa_softc) entry;
	cdev_t		dev;
	kdmsg_iocom_t	iocom;
	struct xdisk_attach_ioctl xaioc;
	struct disk_info info;
	struct disk	disk;
	uuid_t		pfs_fsid;
	int		unit;
	int		inprog;
	int		connected;
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
	struct xa_softc *scan;
	struct xa_softc *xa;
	struct file *fp;
	kdmsg_msg_t *msg;
	int unit;
	char devname[64];
	cdev_t dev;

	fp = holdfp(curproc->p_fd, xaioc->fd, -1);
	if (fp == NULL)
		return EINVAL;

	xa = kmalloc(sizeof(*xa), M_XDISK, M_WAITOK|M_ZERO);

	/*
	 * Find unit
	 */
	lwkt_gettoken(&xdisk_token);
	unit = 0;
	do {
		TAILQ_FOREACH(scan, &xa_queue, entry) {
			if (scan->unit == unit)
				break;
		}
	} while (scan != NULL);
	xa->unit = unit;
	xa->xaioc = *xaioc;
	TAILQ_INSERT_TAIL(&xa_queue, xa, entry);
	lwkt_reltoken(&xdisk_token);

	/*
	 * Create device
	 */
	dev = disk_create(unit, &xa->disk, &xa_ops);
	dev->si_drv1 = xa;
	xa->dev = dev;

	xa->info.d_media_blksize = 512;
	xa->info.d_media_blocks = xaioc->size / 512;
	xa->info.d_dsflags = DSO_MBRQUIET | DSO_RAWPSIZE;
	xa->info.d_secpertrack = 32;
	xa->info.d_nheads = 64;
	xa->info.d_secpercyl = xa->info.d_secpertrack * xa->info.d_nheads;
	xa->info.d_ncylinders = 0;
	disk_setdiskinfo_sync(&xa->disk, &xa->info);

	/*
	 * Set up messaging connection
	 */
	ksnprintf(devname, sizeof(devname), "xa%d", unit);
	kdmsg_iocom_init(&xa->iocom, xa, M_XDISK,
			 xa_lnk_rcvmsg,
			 xa_lnk_dbgmsg,
			 xa_adhoc_input);
	xa->iocom.exit_func = xa_exit;
	xa->inprog = 1;
	kern_uuidgen(&xa->pfs_fsid, 1);
	kdmsg_iocom_reconnect(&xa->iocom, fp, devname);

	/*
	 * Issue DMSG_LNK_CONN for device.  This sets up filters so hopefully
	 * the only SPANs we receive are from servers providing the label
	 * being configured.  Hopefully that's just a single server(!)(!).
	 * (HAMMER peers might have multiple servers but block device peers
	 * currently only allow one).  There could still be multiple spans
	 * due to there being multiple paths available, however.
	 */

	msg = kdmsg_msg_alloc(&xa->iocom.router, DMSG_LNK_CONN | DMSGF_CREATE,
			      xa_msg_conn_reply, xa);
	msg->any.lnk_conn.pfs_type = 0;
	msg->any.lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	msg->any.lnk_conn.peer_type = DMSG_PEER_BLOCK;
	msg->any.lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;
	ksnprintf(msg->any.lnk_conn.cl_label,
		  sizeof(msg->any.lnk_conn.cl_label),
		  "%s", xaioc->cl_label);
	msg->any.lnk_conn.pfs_fsid = xa->pfs_fsid;
	xa->iocom.conn_state = msg->state;
	kdmsg_msg_write(msg);

	xa->inprog = 0;		/* unstall msg thread exit (if racing) */

	return(0);
}

/*
 * Handle reply to our LNK_CONN transaction (transaction remains open)
 */
static
int
xa_msg_conn_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	struct xa_softc *xa = state->any.any;
        kdmsg_msg_t *rmsg;

	if (msg->any.head.cmd & DMSGF_CREATE) {
		kprintf("XA LNK_CONN received reply\n");
		rmsg = kdmsg_msg_alloc(&xa->iocom.router,
				       DMSG_LNK_SPAN | DMSGF_CREATE,
				       xa_msg_span_reply, xa);
		rmsg->any.lnk_span.pfs_type = 0;
		rmsg->any.lnk_span.proto_version = DMSG_SPAN_PROTO_1;
		rmsg->any.lnk_span.peer_type = DMSG_PEER_BLOCK;

		ksnprintf(rmsg->any.lnk_span.cl_label,
			  sizeof(rmsg->any.lnk_span.cl_label),
			  "%s", xa->xaioc.cl_label);
		kdmsg_msg_write(rmsg);
	}
	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		kprintf("DISK LNK_CONN terminated by remote\n");
		xa->iocom.conn_state = NULL;
		kdmsg_msg_reply(msg, 0);
	}
	return(0);
}

static int
xa_msg_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	kprintf("SPAN REPLY - Our sent span was terminated by the "
		"remote %08x state %p\n", msg->any.head.cmd, state);
	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		kdmsg_msg_reply(msg, 0);
	}
	return (0);
}

/*
 * Called from iocom core transmit thread upon disconnect.
 */
static
void
xa_exit(kdmsg_iocom_t *iocom)
{
	struct xa_softc *xa = iocom->handle;

	kprintf("XA_EXIT UNIT %d\n", xa->unit);

	kdmsg_iocom_uninit(iocom);

	while (xa->inprog) {
		tsleep(xa, 0, "xarace", hz);
	}

	/*
	 * XXX allow reconnection, wait for users to terminate?
	 */

	disk_destroy(&xa->disk);

	lwkt_gettoken(&xdisk_token);
	TAILQ_REMOVE(&xa_queue, xa, entry);
	lwkt_reltoken(&xdisk_token);

	kfree(xa, M_XDISK);
}

static int
xa_lnk_rcvmsg(kdmsg_msg_t *msg)
{
	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_LNK_CONN | DMSGF_CREATE:
		/*
		 * reply & leave trans open
		 */
		kprintf("XA CONN RECEIVE - (just ignore it)\n");
		kdmsg_msg_result(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_CREATE:
		kprintf("XA SPAN RECEIVE - ADDED FROM CLUSTER\n");
		break;
	case DMSG_LNK_SPAN | DMSGF_DELETE:
		kprintf("SPAN RECEIVE - DELETED FROM CLUSTER\n");
		break;
	default:
		break;
	}
	return(0);
}

static int
xa_lnk_dbgmsg(kdmsg_msg_t *msg)
{
	switch(msg->any.head.cmd & DMSGF_CMDSWMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * Execute shell command (not supported atm)
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("DEBUGMSG: %s\n", msg->aux_data);
		}
		break;
	default:
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
	return(0);
}

static int
xa_adhoc_input(kdmsg_msg_t *msg)
{
        kprintf("XA ADHOC INPUT MSG %08x\n", msg->any.head.cmd);
        return(0);
}

/************************************************************************
 *			   XA DEVICE INTERFACE				*
 ************************************************************************/

static int
xa_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct xa_softc *xa;

	xa = dev->si_drv1;

	dev->si_bsize_phys = 512;
	dev->si_bsize_best = 32768;

	/*
	 * Issue streaming open and wait for reply.
	 */

	/* XXX check ap->a_oflags & FWRITE, EACCES if read-only */

	return(0);
}

static int
xa_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
}

static int
xa_strategy(struct dev_strategy_args *ap)
{
}

static int
xa_ioctl(struct dev_ioctl_args *ap)
{
	return (ENOTTY);
}

static int
xa_size(struct dev_psize_args *ap)
{
	struct xa_softc *xa;

	if ((xa = ap->a_head.a_dev->si_drv1) == NULL)
		return (ENXIO);
	if (xa->inprog)
		return (ENXIO);
	ap->a_result = xa->info.d_media_blocks;
	return (0);
}
