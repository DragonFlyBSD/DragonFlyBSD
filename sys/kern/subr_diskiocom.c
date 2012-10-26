/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/disklabel32.h>
#include <sys/disklabel64.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/devfs.h>
#include <sys/thread.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include <sys/dmsg.h>

#include <sys/buf2.h>
#include <sys/mplock2.h>
#include <sys/msgport2.h>
#include <sys/thread2.h>

static MALLOC_DEFINE(M_DMSG_DISK, "dmsg_disk", "disk dmsg");

static int disk_iocom_reconnect(struct disk *dp, struct file *fp);
static int disk_msg_conn_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);
static int disk_msg_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);

void
disk_iocom_init(struct disk *dp)
{
	kdmsg_iocom_init(&dp->d_iocom, dp, M_DMSG_DISK,
			 disk_lnk_rcvmsg,
			 disk_dbg_rcvmsg,
			 disk_adhoc_input);
}

void
disk_iocom_update(struct disk *dp)
{
}

void
disk_iocom_uninit(struct disk *dp)
{
	kdmsg_iocom_uninit(&dp->d_iocom);
}

int
disk_iocom_ioctl(struct disk *dp, int cmd, void *data)
{
	struct file *fp;
	struct disk_ioc_recluster *recl;
	int error;

	switch(cmd) {
	case DIOCRECLUSTER:
		recl = data;
		fp = holdfp(curproc->p_fd, recl->fd, -1);
		if (fp) {
			error = disk_iocom_reconnect(dp, fp);
		} else {
			error = EINVAL;
		}
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return error;
}

static
int
disk_iocom_reconnect(struct disk *dp, struct file *fp)
{
	kdmsg_msg_t *msg;
	char devname[64];

	ksnprintf(devname, sizeof(devname), "%s%d",
		  dev_dname(dp->d_rawdev), dkunit(dp->d_rawdev));

	kdmsg_iocom_reconnect(&dp->d_iocom, fp, devname);

	msg = kdmsg_msg_alloc(&dp->d_iocom.router, DMSG_LNK_CONN | DMSGF_CREATE,
			      disk_msg_conn_reply, dp);
	msg->any.lnk_conn.pfs_type = 0;
	msg->any.lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	msg->any.lnk_conn.peer_type = DMSG_PEER_BLOCK;
	msg->any.lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;

	ksnprintf(msg->any.lnk_conn.cl_label,
		  sizeof(msg->any.lnk_conn.cl_label),
		  "%s/%s", hostname, devname);
	dp->d_iocom.conn_state = msg->state;
	kdmsg_msg_write(msg);

	return (0);
}

/*
 * Received reply to our LNK_CONN transaction, indicating LNK_SPAN support.
 * Issue LNK_SPAN.
 */
static
int
disk_msg_conn_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	struct disk *dp = state->any.any;
	kdmsg_msg_t *rmsg;

	if (msg->any.head.cmd & DMSGF_CREATE) {
		kprintf("DISK LNK_CONN received reply\n");
		rmsg = kdmsg_msg_alloc(&dp->d_iocom.router,
				       DMSG_LNK_SPAN | DMSGF_CREATE,
				       disk_msg_span_reply, dp);
		rmsg->any.lnk_span.pfs_type = 0;
		rmsg->any.lnk_span.proto_version = DMSG_SPAN_PROTO_1;
		rmsg->any.lnk_span.peer_type = DMSG_PEER_BLOCK;

		ksnprintf(rmsg->any.lnk_span.cl_label,
			  sizeof(rmsg->any.lnk_span.cl_label),
			  "%s/%s%d",
			  hostname,
			  dev_dname(dp->d_rawdev),
			  dkunit(dp->d_rawdev));
		kdmsg_msg_write(rmsg);
	}
	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		kprintf("DISK LNK_CONN terminated by remote\n");
		dp->d_iocom.conn_state = NULL;
		kdmsg_msg_reply(msg, 0);
	}
	return(0);
}

/*
 * Reply to our LNK_SPAN.  The transaction is left open.
 */
static
int
disk_msg_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	/*struct disk *dp = state->any.any;*/

	kprintf("DISK LNK_SPAN reply received\n");
	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		kdmsg_msg_reply(msg, 0);
	}
	return (0);
}

int
disk_lnk_rcvmsg(kdmsg_msg_t *msg)
{
	/*struct disk *dp = msg->router->iocom->handle;*/

	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_LNK_CONN | DMSGF_CREATE:
		/*
		 * reply & leave trans open
		 */
		kprintf("DISK CONN RECEIVE - (just ignore it)\n");
		kdmsg_msg_result(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_CREATE:
		kprintf("DISK SPAN RECEIVE - ADDED FROM CLUSTER\n");
		break;
	case DMSG_LNK_SPAN | DMSGF_DELETE:
		kprintf("DISK SPAN RECEIVE - DELETED FROM CLUSTER\n");
		break;
	default:
		break;
	}
	return (0);
}

int
disk_dbg_rcvmsg(kdmsg_msg_t *msg)
{
	/*struct disk *dp = msg->router->iocom->handle;*/

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
	return (0);
}

int
disk_adhoc_input(kdmsg_msg_t *msg)
{
	struct disk *dp = msg->router->iocom->handle;

	kprintf("DISK ADHOC INPUT %s%d\n",
		dev_dname(dp->d_rawdev), dkunit(dp->d_rawdev));

	return (0);
}
