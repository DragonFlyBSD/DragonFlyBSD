/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression)
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
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/vfsops.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/objcache.h>

#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>

#include <sys/mutex.h>
#include <sys/mutex2.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"

static int hammer2_rcvdmsg(kdmsg_msg_t *msg);
static void hammer2_autodmsg(kdmsg_msg_t *msg);
static int hammer2_lnk_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);

void
hammer2_iocom_init(hammer2_dev_t *hmp)
{
	/*
	 * Automatic LNK_CONN
	 * Automatic LNK_SPAN handling
	 * No automatic LNK_SPAN generation (we generate multiple spans
	 *				     ourselves).
	 */
	kdmsg_iocom_init(&hmp->iocom, hmp,
			 KDMSG_IOCOMF_AUTOCONN |
			 KDMSG_IOCOMF_AUTORXSPAN,
			 hmp->mchain, hammer2_rcvdmsg);
}

void
hammer2_iocom_uninit(hammer2_dev_t *hmp)
{
	/* XXX chain depend deadlck? */
	if (hmp->iocom.mmsg)
		kdmsg_iocom_uninit(&hmp->iocom);
}

/*
 * Reconnect using the passed file pointer.  The caller must ref the
 * fp for us.
 */
void
hammer2_cluster_reconnect(hammer2_dev_t *hmp, struct file *fp)
{
	/*
	 * Closes old comm descriptor, kills threads, cleans up
	 * states, then installs the new descriptor and creates
	 * new threads.
	 */
	kdmsg_iocom_reconnect(&hmp->iocom, fp, "hammer2");

	/*
	 * Setup LNK_CONN fields for autoinitiated state machine.  LNK_CONN
	 * does not have to be unique.  peer_id can be used to filter incoming
	 * LNK_SPANs automatically if desired (though we still need to check).
	 * peer_label typically identifies who we are and is not a filter.
	 *
	 * Since we will be initiating multiple LNK_SPANs we cannot use
	 * AUTOTXSPAN, but we do use AUTORXSPAN so kdmsg tracks received
	 * LNK_SPANs, and we simply monitor those messages.
	 */
	bzero(&hmp->iocom.auto_lnk_conn.peer_id,
	      sizeof(hmp->iocom.auto_lnk_conn.peer_id));
	/* hmp->iocom.auto_lnk_conn.peer_id = hmp->voldata.fsid; */
	hmp->iocom.auto_lnk_conn.proto_version = DMSG_SPAN_PROTO_1;
#if 0
	hmp->iocom.auto_lnk_conn.peer_type = hmp->voldata.peer_type;
#endif
	hmp->iocom.auto_lnk_conn.peer_type = DMSG_PEER_HAMMER2;

	/*
	 * We just want to receive LNK_SPANs related to HAMMER2 matching
	 * peer_id.
	 */
	hmp->iocom.auto_lnk_conn.peer_mask = 1LLU << DMSG_PEER_HAMMER2;

#if 0
	switch (ipdata->pfs_type) {
	case DMSG_PFSTYPE_CLIENT:
		hmp->iocom.auto_lnk_conn.peer_mask &=
				~(1LLU << DMSG_PFSTYPE_CLIENT);
		break;
	default:
		break;
	}
#endif

	bzero(&hmp->iocom.auto_lnk_conn.peer_label,
	      sizeof(hmp->iocom.auto_lnk_conn.peer_label));
	ksnprintf(hmp->iocom.auto_lnk_conn.peer_label,
		  sizeof(hmp->iocom.auto_lnk_conn.peer_label),
		  "%s/%s",
		  hostname, "hammer2-mount");
	kdmsg_iocom_autoinitiate(&hmp->iocom, hammer2_autodmsg);
}

static int
hammer2_rcvdmsg(kdmsg_msg_t *msg)
{
	kprintf("RCVMSG %08x\n", msg->tcmd);

	switch(msg->tcmd) {
	case DMSG_DBG_SHELL:
		/*
		 * (non-transaction)
		 * Execute shell command (not supported atm)
		 */
		kdmsg_msg_result(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		/*
		 * (non-transaction)
		 */
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("HAMMER2 DBG: %s\n", msg->aux_data);
		}
		break;
	default:
		/*
		 * Unsupported message received.  We only need to
		 * reply if it's a transaction in order to close our end.
		 * Ignore any one-way messages or any further messages
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

/*
 * This function is called after KDMSG has automatically handled processing
 * of a LNK layer message (typically CONN or SPAN).
 *
 * We tag off the LNK_CONN to trigger our LNK_VOLCONF messages which
 * advertises all available hammer2 super-root volumes.
 *
 * We collect span state
 */
static void hammer2_update_spans(hammer2_dev_t *hmp, kdmsg_state_t *state);

static void
hammer2_autodmsg(kdmsg_msg_t *msg)
{
	hammer2_dev_t *hmp = msg->state->iocom->handle;
	int copyid;

	switch(msg->tcmd) {
	case DMSG_LNK_CONN | DMSGF_CREATE:
	case DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_DELETE:
	case DMSG_LNK_CONN | DMSGF_DELETE:
		/*
		 * NOTE: kern_dmsg will automatically issue a result,
		 *       leaving the transaction open, for CREATEs,
		 *	 and will automatically issue a terminating reply
		 *	 for DELETEs.
		 */
		break;
	case DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_REPLY:
	case DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_DELETE | DMSGF_REPLY:
		/*
		 * Do a volume configuration dump when we receive a reply
		 * to our auto-CONN (typically leaving the transaction open).
		 */
		if (msg->any.head.cmd & DMSGF_CREATE) {
			kprintf("HAMMER2: VOLDATA DUMP\n");

			/*
			 * Dump the configuration stored in the volume header.
			 * This will typically be import/export access rights,
			 * master encryption keys (encrypted), etc.
			 */
			hammer2_voldata_lock(hmp);
			copyid = 0;
			while (copyid < HAMMER2_COPYID_COUNT) {
				if (hmp->voldata.copyinfo[copyid].copyid)
					hammer2_volconf_update(hmp, copyid);
				++copyid;
			}
			hammer2_voldata_unlock(hmp);

			kprintf("HAMMER2: INITIATE SPANs\n");
			hammer2_update_spans(hmp, msg->state);
		}
		if ((msg->any.head.cmd & DMSGF_DELETE) &&
		    msg->state && (msg->state->txcmd & DMSGF_DELETE) == 0) {
			kprintf("HAMMER2: CONN WAS TERMINATED\n");
		}
		break;
	case DMSG_LNK_SPAN | DMSGF_CREATE:
		/*
		 * Monitor SPANs and issue a result, leaving the SPAN open
		 * if it is something we can use now or in the future.
		 */
		if (msg->any.lnk_span.peer_type != DMSG_PEER_HAMMER2) {
			kdmsg_msg_reply(msg, 0);
			break;
		}
		if (msg->any.lnk_span.proto_version != DMSG_SPAN_PROTO_1) {
			kdmsg_msg_reply(msg, 0);
			break;
		}
		DMSG_TERMINATE_STRING(msg->any.lnk_span.peer_label);
		kprintf("H2 +RXSPAN cmd=%08x (%-20s) cl=",
			msg->any.head.cmd, msg->any.lnk_span.peer_label);
		printf_uuid(&msg->any.lnk_span.peer_id);
		kprintf(" fs=");
		printf_uuid(&msg->any.lnk_span.pfs_id);
		kprintf(" type=%d\n", msg->any.lnk_span.pfs_type);
		kdmsg_msg_result(msg, 0);
		break;
	case DMSG_LNK_SPAN | DMSGF_DELETE:
		/*
		 * NOTE: kern_dmsg will automatically reply to DELETEs.
		 */
		kprintf("H2 -RXSPAN\n");
		break;
	default:
		break;
	}
}

/*
 * Update LNK_SPAN state
 */
static void
hammer2_update_spans(hammer2_dev_t *hmp, kdmsg_state_t *state)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	hammer2_pfs_t *spmp;
	hammer2_key_t key_next;
	kdmsg_msg_t *rmsg;
	size_t name_len;
	int ddflag;

	/*
	 * Lookup mount point under the media-localized super-root.
	 *
	 * cluster->pmp will incorrectly point to spmp and must be fixed
	 * up later on.
	 */
	spmp = hmp->spmp;
	cparent = hammer2_inode_lock_ex(spmp->iroot);
	cluster = hammer2_cluster_lookup(cparent, &key_next,
					 HAMMER2_KEY_MIN,
					 HAMMER2_KEY_MAX,
					 0, &ddflag);
	while (cluster) {
		if (hammer2_cluster_type(cluster) != HAMMER2_BREF_TYPE_INODE)
			continue;
		ripdata = &hammer2_cluster_rdata(cluster)->ipdata;
		kprintf("UPDATE SPANS: %s\n", ripdata->filename);

		rmsg = kdmsg_msg_alloc(&hmp->iocom.state0,
				       DMSG_LNK_SPAN | DMSGF_CREATE,
				       hammer2_lnk_span_reply, NULL);
		rmsg->any.lnk_span.peer_id = ripdata->pfs_clid;
		rmsg->any.lnk_span.pfs_id = ripdata->pfs_fsid;
		rmsg->any.lnk_span.pfs_type = ripdata->pfs_type;
		rmsg->any.lnk_span.peer_type = DMSG_PEER_HAMMER2;
		rmsg->any.lnk_span.proto_version = DMSG_SPAN_PROTO_1;
		name_len = ripdata->name_len;
		if (name_len >= sizeof(rmsg->any.lnk_span.peer_label))
			name_len = sizeof(rmsg->any.lnk_span.peer_label) - 1;
		bcopy(ripdata->filename,
		      rmsg->any.lnk_span.peer_label,
		      name_len);

		kdmsg_msg_write(rmsg);

		cluster = hammer2_cluster_next(cparent, cluster,
					       &key_next,
					       key_next,
					       HAMMER2_KEY_MAX,
					       0);
	}
	hammer2_inode_unlock_ex(spmp->iroot, cparent);
}

static
int
hammer2_lnk_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		kdmsg_msg_reply(msg, 0);
	}
	return 0;
}

/*
 * Volume configuration updates are passed onto the userland service
 * daemon via the open LNK_CONN transaction.
 */
void
hammer2_volconf_update(hammer2_dev_t *hmp, int index)
{
	kdmsg_msg_t *msg;

	/* XXX interlock against connection state termination */
	kprintf("volconf update %p\n", hmp->iocom.conn_state);
	if (hmp->iocom.conn_state) {
		kprintf("TRANSMIT VOLCONF VIA OPEN CONN TRANSACTION\n");
		msg = kdmsg_msg_alloc(hmp->iocom.conn_state,
				      DMSG_LNK_HAMMER2_VOLCONF,
				      NULL, NULL);
		H2_LNK_VOLCONF(msg)->copy = hmp->voldata.copyinfo[index];
		H2_LNK_VOLCONF(msg)->mediaid = hmp->voldata.fsid;
		H2_LNK_VOLCONF(msg)->index = index;
		kdmsg_msg_write(msg);
	}
}
