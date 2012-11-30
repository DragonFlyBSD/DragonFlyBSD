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
static int disk_rcvdmsg(kdmsg_msg_t *msg);

void
disk_iocom_init(struct disk *dp)
{
	kdmsg_iocom_init(&dp->d_iocom, dp,
			 KDMSG_IOCOMF_AUTOCONN |
			 KDMSG_IOCOMF_AUTOSPAN |
			 KDMSG_IOCOMF_AUTOCIRC,
			 M_DMSG_DISK, disk_rcvdmsg);
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
	char devname[64];

	ksnprintf(devname, sizeof(devname), "%s%d",
		  dev_dname(dp->d_rawdev), dkunit(dp->d_rawdev));

	kdmsg_iocom_reconnect(&dp->d_iocom, fp, devname);

	dp->d_iocom.auto_lnk_conn.pfs_type = DMSG_PFSTYPE_SERVER;
	dp->d_iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
	dp->d_iocom.auto_lnk_conn.peer_type = DMSG_PEER_BLOCK;
	dp->d_iocom.auto_lnk_conn.peer_mask = 1LLU << DMSG_PEER_BLOCK;
	dp->d_iocom.auto_lnk_conn.pfs_mask = (uint64_t)-1;
	ksnprintf(dp->d_iocom.auto_lnk_conn.cl_label,
		  sizeof(dp->d_iocom.auto_lnk_conn.cl_label),
		  "%s/%s", hostname, devname);
	if (dp->d_info.d_serialno) {
		ksnprintf(dp->d_iocom.auto_lnk_conn.fs_label,
			  sizeof(dp->d_iocom.auto_lnk_conn.fs_label),
			  "%s", dp->d_info.d_serialno);
	}

	dp->d_iocom.auto_lnk_span.pfs_type = DMSG_PFSTYPE_SERVER;
	dp->d_iocom.auto_lnk_span.proto_version = DMSG_SPAN_PROTO_1;
	dp->d_iocom.auto_lnk_span.peer_type = DMSG_PEER_BLOCK;
	dp->d_iocom.auto_lnk_span.media.block.bytes =
						dp->d_info.d_media_size;
	dp->d_iocom.auto_lnk_span.media.block.blksize =
						dp->d_info.d_media_blksize;
	ksnprintf(dp->d_iocom.auto_lnk_span.cl_label,
		  sizeof(dp->d_iocom.auto_lnk_span.cl_label),
		  "%s/%s", hostname, devname);
	if (dp->d_info.d_serialno) {
		ksnprintf(dp->d_iocom.auto_lnk_span.fs_label,
			  sizeof(dp->d_iocom.auto_lnk_span.fs_label),
			  "%s", dp->d_info.d_serialno);
	}

	kdmsg_iocom_autoinitiate(&dp->d_iocom, NULL);

	return (0);
}

int
disk_rcvdmsg(kdmsg_msg_t *msg)
{
	struct disk *dp = msg->iocom->handle;

	switch(msg->any.head.cmd & DMSGF_TRANSMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * Execute shell command (not supported atm)
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("diskiocom: DEBUGMSG: %s\n", msg->aux_data);
		}
		break;
	case DMSG_BLK_OPEN | DMSGF_CREATE:
	case DMSG_BLK_READ | DMSGF_CREATE:
	case DMSG_BLK_WRITE | DMSGF_CREATE:
	case DMSG_BLK_FLUSH | DMSGF_CREATE:
	case DMSG_BLK_FREEBLKS | DMSGF_CREATE:
	default:
		kprintf("diskiocom: DISK ADHOC INPUT %s%d cmd %08x\n",
			dev_dname(dp->d_rawdev), dkunit(dp->d_rawdev),
			msg->any.head.cmd);
		if (msg->any.head.cmd & DMSGF_CREATE)
			kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
	return (0);
}
