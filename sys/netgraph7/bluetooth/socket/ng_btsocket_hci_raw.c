/*
 * ng_btsocket_hci_raw.c
 */

/*-
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_btsocket_hci_raw.c,v 1.14 2003/09/14 23:29:06 max Exp $
 * $FreeBSD: src/sys/netgraph/bluetooth/socket/ng_btsocket_hci_raw.c,v 1.23 2006/11/06 13:42:04 rwatson Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <bitstring.h>		/* XXX */
#include <sys/domain.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/msgport2.h>
#include <sys/refcount.h>
#include <netgraph7/ng_message.h>
#include <netgraph7/netgraph.h>
#include <netgraph7/netgraph2.h>
#include <netgraph7/bluetooth/include/ng_bluetooth.h>
#include <netgraph7/bluetooth/include/ng_hci.h>
#include <netgraph7/bluetooth/include/ng_l2cap.h>
#include <netgraph7/bluetooth/include/ng_btsocket.h>
#include <netgraph7/bluetooth/include/ng_btsocket_hci_raw.h>

/* MALLOC define */
#ifdef NG_SEPARATE_MALLOC
MALLOC_DEFINE(M_NETGRAPH_BTSOCKET_HCI_RAW, "netgraph_btsocks_hci_raw",
	"Netgraph Bluetooth raw HCI sockets");
#else
#define M_NETGRAPH_BTSOCKET_HCI_RAW M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Netgraph node methods */
static ng_constructor_t	ng_btsocket_hci_raw_node_constructor;
static ng_rcvmsg_t	ng_btsocket_hci_raw_node_rcvmsg;
static ng_shutdown_t	ng_btsocket_hci_raw_node_shutdown;
static ng_newhook_t	ng_btsocket_hci_raw_node_newhook;
static ng_connect_t	ng_btsocket_hci_raw_node_connect;
static ng_rcvdata_t	ng_btsocket_hci_raw_node_rcvdata;
static ng_disconnect_t	ng_btsocket_hci_raw_node_disconnect;

static void 		ng_btsocket_hci_raw_input (void *, int);
static void 		ng_btsocket_hci_raw_output(node_p, hook_p, void *, int);
static void		ng_btsocket_hci_raw_savctl(ng_btsocket_hci_raw_pcb_p, 
						   struct mbuf **,
						   struct mbuf *); 
static int		ng_btsocket_hci_raw_filter(ng_btsocket_hci_raw_pcb_p,
						   struct mbuf *, int);

#define ng_btsocket_hci_raw_wakeup_input_task() \
	taskqueue_enqueue(taskqueue_swi, &ng_btsocket_hci_raw_task)

/* Security filter */
struct ng_btsocket_hci_raw_sec_filter {
	bitstr_t	bit_decl(events, 0xff);
	bitstr_t	bit_decl(commands[0x3f], 0x3ff);
};

/* Netgraph type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_BTSOCKET_HCI_RAW_NODE_TYPE,
	.constructor =	ng_btsocket_hci_raw_node_constructor,
	.rcvmsg =	ng_btsocket_hci_raw_node_rcvmsg,
	.shutdown =	ng_btsocket_hci_raw_node_shutdown,
	.newhook =	ng_btsocket_hci_raw_node_newhook,
	.connect =	ng_btsocket_hci_raw_node_connect,
	.rcvdata =	ng_btsocket_hci_raw_node_rcvdata,
	.disconnect =	ng_btsocket_hci_raw_node_disconnect,
};

/* Globals */
extern int					ifqmaxlen;
static u_int32_t				ng_btsocket_hci_raw_debug_level;
static u_int32_t				ng_btsocket_hci_raw_ioctl_timeout;
static node_p					ng_btsocket_hci_raw_node;
static struct ng_bt_itemq			ng_btsocket_hci_raw_queue;
static struct lock				ng_btsocket_hci_raw_queue_lock;
static struct task				ng_btsocket_hci_raw_task;
static LIST_HEAD(, ng_btsocket_hci_raw_pcb)	ng_btsocket_hci_raw_sockets;
static struct lock				ng_btsocket_hci_raw_sockets_lock;
static u_int32_t				ng_btsocket_hci_raw_token;
static struct lock				ng_btsocket_hci_raw_token_lock;
static struct ng_btsocket_hci_raw_sec_filter	*ng_btsocket_hci_raw_sec_filter;
 
/* Sysctl tree */
SYSCTL_DECL(_net_bluetooth_hci_sockets);
SYSCTL_NODE(_net_bluetooth_hci_sockets, OID_AUTO, raw, CTLFLAG_RW,
        0, "Bluetooth raw HCI sockets family");
SYSCTL_INT(_net_bluetooth_hci_sockets_raw, OID_AUTO, debug_level, CTLFLAG_RW,
        &ng_btsocket_hci_raw_debug_level, NG_BTSOCKET_WARN_LEVEL,
	"Bluetooth raw HCI sockets debug level");
SYSCTL_INT(_net_bluetooth_hci_sockets_raw, OID_AUTO, ioctl_timeout, CTLFLAG_RW,
        &ng_btsocket_hci_raw_ioctl_timeout, 5,
	"Bluetooth raw HCI sockets ioctl timeout");
SYSCTL_INT(_net_bluetooth_hci_sockets_raw, OID_AUTO, queue_len, CTLFLAG_RD,
        &ng_btsocket_hci_raw_queue.len, 0,
        "Bluetooth raw HCI sockets input queue length");
SYSCTL_INT(_net_bluetooth_hci_sockets_raw, OID_AUTO, queue_maxlen, CTLFLAG_RD,
        &ng_btsocket_hci_raw_queue.maxlen, 0,
        "Bluetooth raw HCI sockets input queue max. length");
SYSCTL_INT(_net_bluetooth_hci_sockets_raw, OID_AUTO, queue_drops, CTLFLAG_RD,
        &ng_btsocket_hci_raw_queue.drops, 0,
        "Bluetooth raw HCI sockets input queue drops");

/* Debug */
#define NG_BTSOCKET_HCI_RAW_INFO \
	if (ng_btsocket_hci_raw_debug_level >= NG_BTSOCKET_INFO_LEVEL) \
		kprintf

#define NG_BTSOCKET_HCI_RAW_WARN \
	if (ng_btsocket_hci_raw_debug_level >= NG_BTSOCKET_WARN_LEVEL) \
		kprintf

#define NG_BTSOCKET_HCI_RAW_ERR \
	if (ng_btsocket_hci_raw_debug_level >= NG_BTSOCKET_ERR_LEVEL) \
		kprintf

#define NG_BTSOCKET_HCI_RAW_ALERT \
	if (ng_btsocket_hci_raw_debug_level >= NG_BTSOCKET_ALERT_LEVEL) \
		kprintf

/****************************************************************************
 ****************************************************************************
 **                          Netgraph specific
 ****************************************************************************
 ****************************************************************************/

/*
 * Netgraph node constructor. Do not allow to create node of this type.
 */

static int
ng_btsocket_hci_raw_node_constructor(node_p node)
{
	return (EINVAL);
} /* ng_btsocket_hci_raw_node_constructor */

/*
 * Netgraph node destructor. Just let old node go and create new fresh one.
 */

static int
ng_btsocket_hci_raw_node_shutdown(node_p node)
{
	int	error = 0;

	NG_NODE_UNREF(node);

	error = ng_make_node_common(&typestruct, &ng_btsocket_hci_raw_node);
	if (error  != 0) {
		NG_BTSOCKET_HCI_RAW_ALERT(
"%s: Could not create Netgraph node, error=%d\n", __func__, error);

		ng_btsocket_hci_raw_node = NULL;

		return (ENOMEM);
        }

	error = ng_name_node(ng_btsocket_hci_raw_node,
				NG_BTSOCKET_HCI_RAW_NODE_TYPE);
	if (error != 0) {
		NG_BTSOCKET_HCI_RAW_ALERT(
"%s: Could not name Netgraph node, error=%d\n", __func__, error);

		NG_NODE_UNREF(ng_btsocket_hci_raw_node);
		ng_btsocket_hci_raw_node = NULL;

		return (EINVAL);
	}

	return (0);
} /* ng_btsocket_hci_raw_node_shutdown */

/*
 * Create new hook. Just say "yes"
 */

static int
ng_btsocket_hci_raw_node_newhook(node_p node, hook_p hook, char const *name)
{
	return (0);
} /* ng_btsocket_hci_raw_node_newhook */

/*
 * Connect hook. Just say "yes"
 */

static int
ng_btsocket_hci_raw_node_connect(hook_p hook)
{
	return (0);
} /* ng_btsocket_hci_raw_node_connect */

/*
 * Disconnect hook
 */

static int
ng_btsocket_hci_raw_node_disconnect(hook_p hook)
{
	return (0);
} /* ng_btsocket_hci_raw_node_disconnect */

/*
 * Receive control message.
 * Make sure it is a message from HCI node and it is a response.
 * Enqueue item and schedule input task.
 */

static int
ng_btsocket_hci_raw_node_rcvmsg(node_p node, item_p item, hook_p lasthook) 
{
	struct ng_mesg	*msg = NGI_MSG(item); /* item still has message */
	int		 error = 0;

	/*
	 * Check for empty sockets list creates LOR when both sender and
	 * receiver device are connected to the same host, so remove it
	 * for now
	 */

	if (msg != NULL &&
	    (msg->header.typecookie == NGM_HCI_COOKIE ||
	     msg->header.typecookie == NGM_GENERIC_COOKIE) &&
	    msg->header.flags & NGF_RESP) {
		if (msg->header.token == 0) {
			NG_FREE_ITEM(item);
			return (0);
		}

		lockmgr(&ng_btsocket_hci_raw_queue_lock, LK_EXCLUSIVE);
		if (NG_BT_ITEMQ_FULL(&ng_btsocket_hci_raw_queue)) {
			NG_BTSOCKET_HCI_RAW_ERR(
"%s: Input queue is full\n", __func__);

			NG_BT_ITEMQ_DROP(&ng_btsocket_hci_raw_queue);
			NG_FREE_ITEM(item);
			error = ENOBUFS;
		} else {
			ng_ref_item(item);
			NG_BT_ITEMQ_ENQUEUE(&ng_btsocket_hci_raw_queue, item);
			error = ng_btsocket_hci_raw_wakeup_input_task();
		}
		lockmgr(&ng_btsocket_hci_raw_queue_lock, LK_RELEASE);
	} else {
		NG_FREE_ITEM(item);
		error = EINVAL;
	}

	return (error);
} /* ng_btsocket_hci_raw_node_rcvmsg */

/*
 * Receive packet from the one of our hook.
 * Prepend every packet with sockaddr_hci and record sender's node name.
 * Enqueue item and schedule input task.
 */

static int
ng_btsocket_hci_raw_node_rcvdata(hook_p hook, item_p item)
{
	struct mbuf	*nam = NULL;
	int		 error;

	/*
	 * Check for empty sockets list creates LOR when both sender and
	 * receiver device are connected to the same host, so remove it
	 * for now
	 */

	MGET(nam, M_NOWAIT, MT_SONAME);
	if (nam != NULL) {
		struct sockaddr_hci	*sa = mtod(nam, struct sockaddr_hci *);

		nam->m_len = sizeof(struct sockaddr_hci);

		sa->hci_len = sizeof(*sa);
		sa->hci_family = AF_BLUETOOTH;
		strlcpy(sa->hci_node, NG_PEER_NODE_NAME(hook),
			sizeof(sa->hci_node));

		NGI_GET_M(item, nam->m_next);
		NGI_M(item) = nam;

		lockmgr(&ng_btsocket_hci_raw_queue_lock, LK_EXCLUSIVE);
		if (NG_BT_ITEMQ_FULL(&ng_btsocket_hci_raw_queue)) {
			NG_BTSOCKET_HCI_RAW_ERR(
"%s: Input queue is full\n", __func__);

			NG_BT_ITEMQ_DROP(&ng_btsocket_hci_raw_queue);
			NG_FREE_ITEM(item);
			error = ENOBUFS;
		} else {
			ng_ref_item(item);
			NG_BT_ITEMQ_ENQUEUE(&ng_btsocket_hci_raw_queue, item);
			error = ng_btsocket_hci_raw_wakeup_input_task();
		}
		lockmgr(&ng_btsocket_hci_raw_queue_lock, LK_RELEASE);
	} else {
		NG_BTSOCKET_HCI_RAW_ERR(
"%s: Failed to allocate address mbuf\n", __func__);

		NG_FREE_ITEM(item);
		error = ENOBUFS;
	}

	return (error);
} /* ng_btsocket_hci_raw_node_rcvdata */

/****************************************************************************
 ****************************************************************************
 **                              Sockets specific
 ****************************************************************************
 ****************************************************************************/

/*
 * Get next token. We need token to avoid theoretical race where process
 * submits ioctl() message then interrupts ioctl() and re-submits another
 * ioctl() on the same socket *before* first ioctl() complete.
 */
 
static void
ng_btsocket_hci_raw_get_token(u_int32_t *token)
{
	lockmgr(&ng_btsocket_hci_raw_token_lock, LK_EXCLUSIVE);
  
	if (++ ng_btsocket_hci_raw_token == 0)
		ng_btsocket_hci_raw_token = 1;
 
	*token = ng_btsocket_hci_raw_token;
 
	lockmgr(&ng_btsocket_hci_raw_token_lock, LK_RELEASE);
} /* ng_btsocket_hci_raw_get_token */

/*
 * Send Netgraph message to the node - do not expect reply
 */

static int
ng_btsocket_hci_raw_send_ngmsg(char *path, int cmd, void *arg, int arglen)
{
	struct ng_mesg	*msg = NULL;
	int		 error = 0;

	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, cmd, arglen, M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	if (arg != NULL && arglen > 0)
		bcopy(arg, msg->data, arglen);

	NG_SEND_MSG_PATH(error, ng_btsocket_hci_raw_node, msg, path, 0);

	return (error);
} /* ng_btsocket_hci_raw_send_ngmsg */

/*
 * Send Netgraph message to the node (no data) and wait for reply 
 */

static int
ng_btsocket_hci_raw_send_sync_ngmsg(ng_btsocket_hci_raw_pcb_p pcb, char *path,
		int cmd, void *rsp, int rsplen)
{
	struct ng_mesg	*msg = NULL;
	int		 error = 0;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	NG_MKMESSAGE(msg, NGM_HCI_COOKIE, cmd, 0, M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	ng_btsocket_hci_raw_get_token(&msg->header.token);
	pcb->token = msg->header.token;
	pcb->msg = NULL;

	NG_SEND_MSG_PATH(error, ng_btsocket_hci_raw_node, msg, path, 0);
	if (error != 0) {
		pcb->token = 0;
		return (error);
	}

	error = lksleep(&pcb->msg, &pcb->pcb_lock, PCATCH, "hcictl", 
			ng_btsocket_hci_raw_ioctl_timeout * hz);
	pcb->token = 0;

	if (error != 0)
		return (error);

	if (pcb->msg != NULL && pcb->msg->header.cmd == cmd)
		bcopy(pcb->msg->data, rsp, rsplen);
	else
		error = EINVAL;

	NG_FREE_MSG(pcb->msg); /* checks for != NULL */

	return (0);
} /* ng_btsocket_hci_raw_send_sync_ngmsg */

/*
 * Create control information for the packet
 */

static void
ng_btsocket_hci_raw_savctl(ng_btsocket_hci_raw_pcb_p pcb, struct mbuf **ctl,
		struct mbuf *m) 
{
	int		dir;
	struct timeval	tv;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->flags & NG_BTSOCKET_HCI_RAW_DIRECTION) {
		dir = (m->m_flags & M_PROTO1)? 1 : 0;
		*ctl = sbcreatecontrol((caddr_t) &dir, sizeof(dir),
					SCM_HCI_RAW_DIRECTION, SOL_HCI_RAW);
		if (*ctl != NULL)
			ctl = &((*ctl)->m_next);
	}

	if (pcb->so->so_options & SO_TIMESTAMP) {
		microtime(&tv);
		*ctl = sbcreatecontrol((caddr_t) &tv, sizeof(tv),
					SCM_TIMESTAMP, SOL_SOCKET);
		if (*ctl != NULL)
			ctl = &((*ctl)->m_next);
	}
} /* ng_btsocket_hci_raw_savctl */

/*
 * Raw HCI sockets data input routine
 */

static void
ng_btsocket_hci_raw_data_input(struct mbuf *nam)
{
	ng_btsocket_hci_raw_pcb_p	 pcb = NULL;
	struct mbuf			*m0 = NULL, *m = NULL;
	struct sockaddr_hci		*sa = NULL;

	m0 = nam->m_next;
	nam->m_next = NULL;

	KASSERT((nam->m_type == MT_SONAME),
		("%s: m_type=%d\n", __func__, nam->m_type));
	KASSERT((m0->m_flags & M_PKTHDR),
		("%s: m_flags=%#x\n", __func__, m0->m_flags));

	sa = mtod(nam, struct sockaddr_hci *);

	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_EXCLUSIVE);

	LIST_FOREACH(pcb, &ng_btsocket_hci_raw_sockets, next) {

		lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

		/*
		 * If socket was bound then check address and
		 *  make sure it matches.
		 */

		if (pcb->addr.hci_node[0] != 0 &&
		    strcmp(sa->hci_node, pcb->addr.hci_node) != 0)
			goto next;

		/*
		 * Check packet against filters
		 * XXX do we have to call m_pullup() here?
		 */

		if (ng_btsocket_hci_raw_filter(pcb, m0, 1) != 0)
			goto next;

		/*
		 * Make a copy of the packet, append to the socket's
		 * receive queue and wakeup socket. sbappendaddr()
		 * will check if socket has enough buffer space.
		 */

		m = m_dup(m0, M_NOWAIT);
		if (m != NULL) {
			struct mbuf	*ctl = NULL;

			ng_btsocket_hci_raw_savctl(pcb, &ctl, m);

			if (sbappendaddr(&pcb->so->so_rcv.sb, 
					(struct sockaddr *) sa, m, ctl))
				sorwakeup(pcb->so);
			else {
				NG_BTSOCKET_HCI_RAW_INFO(
"%s: sbappendaddr() failed\n", __func__);

				NG_FREE_M(m);
				NG_FREE_M(ctl);
			}
		}
next:
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
	}

	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_RELEASE);

	NG_FREE_M(nam);
	NG_FREE_M(m0);
} /* ng_btsocket_hci_raw_data_input */ 

/*
 * Raw HCI sockets message input routine
 */

static void
ng_btsocket_hci_raw_msg_input(struct ng_mesg *msg)
{
	ng_btsocket_hci_raw_pcb_p	pcb = NULL;

	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_EXCLUSIVE);

	LIST_FOREACH(pcb, &ng_btsocket_hci_raw_sockets, next) {
		lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

		if (msg->header.token == pcb->token) {
			pcb->msg = msg;
			wakeup(&pcb->msg);

			lockmgr(&pcb->pcb_lock, LK_RELEASE);
			lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_RELEASE);

			return;
		}

		lockmgr(&pcb->pcb_lock, LK_RELEASE);
	}

	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_RELEASE);

	NG_FREE_MSG(msg); /* checks for != NULL */
} /* ng_btsocket_hci_raw_msg_input */

/*
 * Raw HCI sockets input routines
 */

static void
ng_btsocket_hci_raw_input(void *context, int pending)
{
	item_p	item = NULL;

	for (;;) {
		lockmgr(&ng_btsocket_hci_raw_queue_lock, LK_EXCLUSIVE);
		NG_BT_ITEMQ_DEQUEUE(&ng_btsocket_hci_raw_queue, item);
		lockmgr(&ng_btsocket_hci_raw_queue_lock, LK_RELEASE);

		if (item == NULL)
			break;

		switch(item->el_flags & NGQF_TYPE) {
		case NGQF_DATA: {
			struct mbuf	*m = NULL;

			NGI_GET_M(item, m);
			ng_btsocket_hci_raw_data_input(m);
			} break;

		case NGQF_MESG: {
			struct ng_mesg	*msg = NULL;

			NGI_GET_MSG(item, msg);
			ng_btsocket_hci_raw_msg_input(msg);
			} break;

		default:
			KASSERT(0, 
("%s: invalid item type=%ld\n", __func__, (item->el_flags & NGQF_TYPE)));
			break;
		}

		NG_FREE_ITEM(item);
		ng_unref_item(item, 0);
	}
} /* ng_btsocket_hci_raw_input */

/*
 * Raw HCI sockets output routine
 */

static void
ng_btsocket_hci_raw_output(node_p node, hook_p hook, void *arg1, int arg2)
{
	struct mbuf		*nam = (struct mbuf *) arg1, *m = NULL;
	struct sockaddr_hci	*sa = NULL;
	int			 error;

	m = nam->m_next;
	nam->m_next = NULL;

	KASSERT((nam->m_type == MT_SONAME),
		("%s: m_type=%d\n", __func__, nam->m_type));
	KASSERT((m->m_flags & M_PKTHDR),
		("%s: m_flags=%#x\n", __func__, m->m_flags));

	sa = mtod(nam, struct sockaddr_hci *);

	/*
	 * Find downstream hook
	 * XXX For now access node hook list directly. Should be safe because
	 * we used ng_send_fn() and we should have exclusive lock on the node.
	 */

	LIST_FOREACH(hook, &node->nd_hooks, hk_hooks) {
		if (hook == NULL || NG_HOOK_NOT_VALID(hook) || 
		    NG_NODE_NOT_VALID(NG_PEER_NODE(hook)))
			continue;

		if (strcmp(sa->hci_node, NG_PEER_NODE_NAME(hook)) == 0) {
			NG_SEND_DATA_ONLY(error, hook, m); /* sets m to NULL */
			break;
		}
	}

	NG_FREE_M(nam); /* check for != NULL */
	NG_FREE_M(m);
} /* ng_btsocket_hci_raw_output */

/*
 * Check frame against security and socket filters. 
 * d (direction bit) == 1 means incoming frame.
 */

static int
ng_btsocket_hci_raw_filter(ng_btsocket_hci_raw_pcb_p pcb, struct mbuf *m, int d)
{
	int	type, event, opcode;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	switch ((type = *mtod(m, u_int8_t *))) {
	case NG_HCI_CMD_PKT:
		if (!(pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)) {
			opcode = le16toh(mtod(m, ng_hci_cmd_pkt_t *)->opcode);
		
			if (!bit_test(
ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF(opcode) - 1],
NG_HCI_OCF(opcode) - 1))
				return (EPERM);
		}

		if (d && !bit_test(pcb->filter.packet_mask, NG_HCI_CMD_PKT - 1))
			return (EPERM);
		break;

	case NG_HCI_ACL_DATA_PKT:
	case NG_HCI_SCO_DATA_PKT:
		if (!(pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED) ||
		    !bit_test(pcb->filter.packet_mask, type - 1) ||
		    !d)
			return (EPERM);
		break;

	case NG_HCI_EVENT_PKT:
		if (!d)
			return (EINVAL);

		event = mtod(m, ng_hci_event_pkt_t *)->event - 1;

		if (!(pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED))
			if (!bit_test(ng_btsocket_hci_raw_sec_filter->events, event))
				return (EPERM);

		if (!bit_test(pcb->filter.event_mask, event))
			return (EPERM);
		break;

	default:
		return (EINVAL);
	}

	return (0);
} /* ng_btsocket_hci_raw_filter */

/*
 * Initialize everything
 */

void
ng_btsocket_hci_raw_init(void)
{
	bitstr_t	*f = NULL;
	int		 error = 0;

	ng_btsocket_hci_raw_node = NULL;
	ng_btsocket_hci_raw_debug_level = NG_BTSOCKET_WARN_LEVEL;
	ng_btsocket_hci_raw_ioctl_timeout = 5;

	/* Register Netgraph node type */
	error = ng_newtype(&typestruct);
	if (error != 0) {
		NG_BTSOCKET_HCI_RAW_ALERT(
"%s: Could not register Netgraph node type, error=%d\n", __func__, error);

		return;
	}

	/* Create Netgrapg node */
	error = ng_make_node_common(&typestruct, &ng_btsocket_hci_raw_node);
	if (error != 0) {
		NG_BTSOCKET_HCI_RAW_ALERT(
"%s: Could not create Netgraph node, error=%d\n", __func__, error);

		ng_btsocket_hci_raw_node = NULL;

		return;
        }

	error = ng_name_node(ng_btsocket_hci_raw_node,
				NG_BTSOCKET_HCI_RAW_NODE_TYPE);
	if (error != 0) {
		NG_BTSOCKET_HCI_RAW_ALERT(
"%s: Could not name Netgraph node, error=%d\n", __func__, error);

		NG_NODE_UNREF(ng_btsocket_hci_raw_node);
		ng_btsocket_hci_raw_node = NULL;

		return;
	}

	/* Create input queue */
	NG_BT_ITEMQ_INIT(&ng_btsocket_hci_raw_queue, ifqmaxlen);
	lockinit(&ng_btsocket_hci_raw_queue_lock,
		"btsocks_hci_raw_queue_lock", 0, 0);
	TASK_INIT(&ng_btsocket_hci_raw_task, 0,
		ng_btsocket_hci_raw_input, NULL);

	/* Create list of sockets */
	LIST_INIT(&ng_btsocket_hci_raw_sockets);
	lockinit(&ng_btsocket_hci_raw_sockets_lock,
		"btsocks_hci_raw_sockets_lock", 0, 0);

	/* Tokens */
	ng_btsocket_hci_raw_token = 0;
	lockinit(&ng_btsocket_hci_raw_token_lock,
		"btsocks_hci_raw_token_lock", 0, 0);

	/* 
	 * Security filter
	 * XXX never FREE()ed
	 */

	ng_btsocket_hci_raw_sec_filter = NULL;

	ng_btsocket_hci_raw_sec_filter = kmalloc(sizeof(struct ng_btsocket_hci_raw_sec_filter),
						 M_NETGRAPH_BTSOCKET_HCI_RAW,
						 M_WAITOK | M_NULLOK | M_ZERO);
	if (ng_btsocket_hci_raw_sec_filter == NULL) {
		kprintf("%s: Could not allocate security filter!\n", __func__);
		return;
	}

	/*
	 * XXX How paranoid can we get? 
	 *
	 * Initialize security filter. If bit is set in the mask then
	 * unprivileged socket is allowed to send (receive) this command
	 * (event).
	 */

	/* Enable all events */
	memset(&ng_btsocket_hci_raw_sec_filter->events, 0xff,
		sizeof(ng_btsocket_hci_raw_sec_filter->events)/
			sizeof(ng_btsocket_hci_raw_sec_filter->events[0]));

	/* Disable some critical events */
	f = ng_btsocket_hci_raw_sec_filter->events;
	bit_clear(f, NG_HCI_EVENT_RETURN_LINK_KEYS - 1);
	bit_clear(f, NG_HCI_EVENT_LINK_KEY_NOTIFICATION - 1);
	bit_clear(f, NG_HCI_EVENT_VENDOR - 1);

	/* Commands - Link control */
	f = ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF_LINK_CONTROL-1];
	bit_set(f, NG_HCI_OCF_INQUIRY - 1);
	bit_set(f, NG_HCI_OCF_INQUIRY_CANCEL - 1);
	bit_set(f, NG_HCI_OCF_PERIODIC_INQUIRY - 1);
	bit_set(f, NG_HCI_OCF_EXIT_PERIODIC_INQUIRY - 1);
	bit_set(f, NG_HCI_OCF_REMOTE_NAME_REQ - 1);
	bit_set(f, NG_HCI_OCF_READ_REMOTE_FEATURES - 1);
	bit_set(f, NG_HCI_OCF_READ_REMOTE_VER_INFO - 1);
	bit_set(f, NG_HCI_OCF_READ_CLOCK_OFFSET - 1);

	/* Commands - Link policy */
	f = ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF_LINK_POLICY-1];
	bit_set(f, NG_HCI_OCF_ROLE_DISCOVERY - 1);
	bit_set(f, NG_HCI_OCF_READ_LINK_POLICY_SETTINGS - 1);

	/* Commands - Host controller and baseband */
	f = ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF_HC_BASEBAND-1];
	bit_set(f, NG_HCI_OCF_READ_PIN_TYPE - 1);
	bit_set(f, NG_HCI_OCF_READ_LOCAL_NAME - 1);
	bit_set(f, NG_HCI_OCF_READ_CON_ACCEPT_TIMO - 1);
	bit_set(f, NG_HCI_OCF_READ_PAGE_TIMO - 1);
	bit_set(f, NG_HCI_OCF_READ_SCAN_ENABLE - 1);
	bit_set(f, NG_HCI_OCF_READ_PAGE_SCAN_ACTIVITY - 1);
	bit_set(f, NG_HCI_OCF_READ_INQUIRY_SCAN_ACTIVITY - 1);
	bit_set(f, NG_HCI_OCF_READ_AUTH_ENABLE - 1);
	bit_set(f, NG_HCI_OCF_READ_ENCRYPTION_MODE - 1);
	bit_set(f, NG_HCI_OCF_READ_UNIT_CLASS - 1);
	bit_set(f, NG_HCI_OCF_READ_VOICE_SETTINGS - 1);
	bit_set(f, NG_HCI_OCF_READ_AUTO_FLUSH_TIMO - 1);
	bit_set(f, NG_HCI_OCF_READ_NUM_BROADCAST_RETRANS - 1);
	bit_set(f, NG_HCI_OCF_READ_HOLD_MODE_ACTIVITY - 1);
	bit_set(f, NG_HCI_OCF_READ_XMIT_LEVEL - 1);
	bit_set(f, NG_HCI_OCF_READ_SCO_FLOW_CONTROL - 1);
	bit_set(f, NG_HCI_OCF_READ_LINK_SUPERVISION_TIMO - 1);
	bit_set(f, NG_HCI_OCF_READ_SUPPORTED_IAC_NUM - 1);
	bit_set(f, NG_HCI_OCF_READ_IAC_LAP - 1);
	bit_set(f, NG_HCI_OCF_READ_PAGE_SCAN_PERIOD - 1);
	bit_set(f, NG_HCI_OCF_READ_PAGE_SCAN - 1);

	/* Commands - Informational */
	f = ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF_INFO - 1];
	bit_set(f, NG_HCI_OCF_READ_LOCAL_VER - 1);
	bit_set(f, NG_HCI_OCF_READ_LOCAL_FEATURES - 1);
	bit_set(f, NG_HCI_OCF_READ_BUFFER_SIZE - 1);
	bit_set(f, NG_HCI_OCF_READ_COUNTRY_CODE - 1);
	bit_set(f, NG_HCI_OCF_READ_BDADDR - 1);

	/* Commands - Status */
	f = ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF_STATUS - 1];
	bit_set(f, NG_HCI_OCF_READ_FAILED_CONTACT_CNTR - 1);
	bit_set(f, NG_HCI_OCF_GET_LINK_QUALITY - 1);
	bit_set(f, NG_HCI_OCF_READ_RSSI - 1);

	/* Commands - Testing */
	f = ng_btsocket_hci_raw_sec_filter->commands[NG_HCI_OGF_TESTING - 1];
	bit_set(f, NG_HCI_OCF_READ_LOOPBACK_MODE - 1);
} /* ng_btsocket_hci_raw_init */

/*
 * Abort connection on socket
 */

void
ng_btsocket_hci_raw_abort(netmsg_t msg)
{
} /* ng_btsocket_hci_raw_abort */

#if 0 /* XXX */
void
ng_btsocket_hci_raw_close(struct socket *so)
{
} /* ng_btsocket_hci_raw_close */
#endif

/*
 * Create new raw HCI socket
 */

void
ng_btsocket_hci_raw_attach(netmsg_t msg)
{
	struct socket			*so = msg->attach.base.nm_so;
	int				 proto = msg->attach.nm_proto;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	int				 error = 0;

	if (pcb != NULL) {
		error = EISCONN;
		goto out;
	}

	if (ng_btsocket_hci_raw_node == NULL) {
		error = EPROTONOSUPPORT;
		goto out;
	}
	if (proto != BLUETOOTH_PROTO_HCI) {
		error = EPROTONOSUPPORT;
		goto out;
	}
	if (so->so_type != SOCK_RAW) {
		error = ESOCKTNOSUPPORT;
		goto out;
	}

	error = soreserve(so, NG_BTSOCKET_HCI_RAW_SENDSPACE,
				NG_BTSOCKET_HCI_RAW_RECVSPACE, NULL);
	if (error != 0)
		goto out;

	pcb = kmalloc(sizeof(*pcb), M_NETGRAPH_BTSOCKET_HCI_RAW,
		      M_WAITOK | M_NULLOK | M_ZERO);
	if (pcb == NULL) {
		error = ENOMEM;
		goto out;
	}

	so->so_pcb = (caddr_t) pcb;
	pcb->so = so;

	if (curproc == NULL ||
	    priv_check(curthread, PRIV_NETBLUETOOTH_RAW) == 0)
		pcb->flags |= NG_BTSOCKET_HCI_RAW_PRIVILEGED;

	/*
	 * Set default socket filter. By default socket only accepts HCI
	 * Command_Complete and Command_Status event packets.
	 */

	bit_set(pcb->filter.event_mask, NG_HCI_EVENT_COMMAND_COMPL - 1);
	bit_set(pcb->filter.event_mask, NG_HCI_EVENT_COMMAND_STATUS - 1);

	lockinit(&pcb->pcb_lock, "btsocks_hci_raw_pcb_lock", 0, 0);

	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_EXCLUSIVE);
	LIST_INSERT_HEAD(&ng_btsocket_hci_raw_sockets, pcb, next);
	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
} /* ng_btsocket_hci_raw_attach */

/*
 * Bind raw HCI socket
 */

void
ng_btsocket_hci_raw_bind(netmsg_t msg)
{
	struct socket			*so = msg->bind.base.nm_so;
	struct sockaddr			*nam = msg->bind.nm_nam;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	struct sockaddr_hci		*sa = (struct sockaddr_hci *) nam;
	int				 error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_hci_raw_node == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sa == NULL) {
		error = EINVAL;
		goto out;
	}
	if (sa->hci_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
		goto out;
	}
	if (sa->hci_len != sizeof(*sa)) {
		error = EINVAL;
		goto out;
	}
	if (sa->hci_node[0] == 0) {
		error = EINVAL;
		goto out;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);
	bcopy(sa, &pcb->addr, sizeof(pcb->addr));
	lockmgr(&pcb->pcb_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->bind.base.lmsg, error);
} /* ng_btsocket_hci_raw_bind */

/*
 * Connect raw HCI socket
 */

void
ng_btsocket_hci_raw_connect(netmsg_t msg)
{
	struct socket			*so = msg->connect.base.nm_so;
	struct sockaddr			*nam = msg->connect.nm_nam;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	struct sockaddr_hci		*sa = (struct sockaddr_hci *) nam;
	int				 error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_hci_raw_node == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sa == NULL) {
		error = EINVAL;
		goto out;
	}
	if (sa->hci_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
		goto out;
	}
	if (sa->hci_len != sizeof(*sa)) {
		error = EINVAL;
		goto out;
	}
	if (sa->hci_node[0] == 0) {
		error = EDESTADDRREQ;
		goto out;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	if (bcmp(sa, &pcb->addr, sizeof(pcb->addr)) != 0) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = EADDRNOTAVAIL;
		goto out;
	}

	soisconnected(so);

	lockmgr(&pcb->pcb_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->connect.base.lmsg, error);
} /* ng_btsocket_hci_raw_connect */

/*
 * Process ioctl on socket
 */

void
ng_btsocket_hci_raw_control(netmsg_t msg)
{
	struct socket			*so = msg->control.base.nm_so;
	u_long				 cmd = msg->control.nm_cmd;
	caddr_t				 data = msg->control.nm_data;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	char				 path[NG_NODESIZ + 1];
	struct ng_mesg			*ngmsg = NULL;
	int				 error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_hci_raw_node == NULL) {
		error = EINVAL;
		goto out;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	/* Check if we have device name */
	if (pcb->addr.hci_node[0] == 0) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = EHOSTUNREACH;
		goto out;
	}

	/* Check if we have pending ioctl() */
	if (pcb->token != 0) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = EBUSY;
		goto out;
	}

	ksnprintf(path, sizeof(path), "%s:", pcb->addr.hci_node);

	switch (cmd) {
	case SIOC_HCI_RAW_NODE_GET_STATE: {
		struct ng_btsocket_hci_raw_node_state	*p =
			(struct ng_btsocket_hci_raw_node_state *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path, 
				NGM_HCI_NODE_GET_STATE,
				&p->state, sizeof(p->state));
		} break;

	case SIOC_HCI_RAW_NODE_INIT:
		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_INIT, NULL, 0);
		else
			error = EPERM;
		break;

	case SIOC_HCI_RAW_NODE_GET_DEBUG: {
		struct ng_btsocket_hci_raw_node_debug	*p = 
			(struct ng_btsocket_hci_raw_node_debug *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_DEBUG,
				&p->debug, sizeof(p->debug));
		} break;

	case SIOC_HCI_RAW_NODE_SET_DEBUG: {
		struct ng_btsocket_hci_raw_node_debug	*p = 
			(struct ng_btsocket_hci_raw_node_debug *) data;

		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_SET_DEBUG, &p->debug,
					sizeof(p->debug));
		else
			error = EPERM;
		} break;

	case SIOC_HCI_RAW_NODE_GET_BUFFER: {
		struct ng_btsocket_hci_raw_node_buffer	*p = 
			(struct ng_btsocket_hci_raw_node_buffer *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_BUFFER,
				&p->buffer, sizeof(p->buffer));
		} break;

	case SIOC_HCI_RAW_NODE_GET_BDADDR: {
		struct ng_btsocket_hci_raw_node_bdaddr	*p = 
			(struct ng_btsocket_hci_raw_node_bdaddr *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_BDADDR,
				&p->bdaddr, sizeof(p->bdaddr));
		} break;

	case SIOC_HCI_RAW_NODE_GET_FEATURES: {
		struct ng_btsocket_hci_raw_node_features	*p = 
			(struct ng_btsocket_hci_raw_node_features *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_FEATURES,
				&p->features, sizeof(p->features));
		} break;

	case SIOC_HCI_RAW_NODE_GET_STAT: {
		struct ng_btsocket_hci_raw_node_stat	*p = 
			(struct ng_btsocket_hci_raw_node_stat *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_STAT,
				&p->stat, sizeof(p->stat));
		} break;

	case SIOC_HCI_RAW_NODE_RESET_STAT:
		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_RESET_STAT, NULL, 0);
		else
			error = EPERM;
		break;

	case SIOC_HCI_RAW_NODE_FLUSH_NEIGHBOR_CACHE:
		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_FLUSH_NEIGHBOR_CACHE,
					NULL, 0);
		else
			error = EPERM;
		break;

	case SIOC_HCI_RAW_NODE_GET_NEIGHBOR_CACHE:  {
		struct ng_btsocket_hci_raw_node_neighbor_cache	*p = 
			(struct ng_btsocket_hci_raw_node_neighbor_cache *) data;
		ng_hci_node_get_neighbor_cache_ep		*p1 = NULL;
		ng_hci_node_neighbor_cache_entry_ep		*p2 = NULL;

		if (p->num_entries <= 0 || 
		    p->num_entries > NG_HCI_MAX_NEIGHBOR_NUM ||
		    p->entries == NULL) {
			error = EINVAL;
			break;
		}

		NG_MKMESSAGE(ngmsg, NGM_HCI_COOKIE,
			NGM_HCI_NODE_GET_NEIGHBOR_CACHE, 0, M_WAITOK | M_NULLOK);
		if (ngmsg == NULL) {
			error = ENOMEM;
			break;
		}
		ng_btsocket_hci_raw_get_token(&ngmsg->header.token);
		pcb->token = ngmsg->header.token;
		pcb->msg = NULL;

		NG_SEND_MSG_PATH(error, ng_btsocket_hci_raw_node, ngmsg, path, 0);
		if (error != 0) {
			pcb->token = 0;
			break;
		}

		error = lksleep(&pcb->msg, &pcb->pcb_lock,
				PCATCH, "hcictl", 
				ng_btsocket_hci_raw_ioctl_timeout * hz);
		pcb->token = 0;

		if (error != 0)
			break;

		if (pcb->msg != NULL &&
		    pcb->msg->header.cmd == NGM_HCI_NODE_GET_NEIGHBOR_CACHE) {
			/* Return data back to user space */
			p1 = (ng_hci_node_get_neighbor_cache_ep *)
				(pcb->msg->data);
			p2 = (ng_hci_node_neighbor_cache_entry_ep *)
				(p1 + 1);

			p->num_entries = min(p->num_entries, p1->num_entries);
			if (p->num_entries > 0)
				error = copyout((caddr_t) p2, 
						(caddr_t) p->entries,
						p->num_entries * sizeof(*p2));
		} else
			error = EINVAL;

		NG_FREE_MSG(pcb->msg); /* checks for != NULL */
		}break;

	case SIOC_HCI_RAW_NODE_GET_CON_LIST: {
		struct ng_btsocket_hci_raw_con_list	*p = 
			(struct ng_btsocket_hci_raw_con_list *) data;
		ng_hci_node_con_list_ep			*p1 = NULL;
		ng_hci_node_con_ep			*p2 = NULL;

		if (p->num_connections == 0 ||
		    p->num_connections > NG_HCI_MAX_CON_NUM ||
		    p->connections == NULL) {
			error = EINVAL;
			break;
		}

		NG_MKMESSAGE(ngmsg, NGM_HCI_COOKIE, NGM_HCI_NODE_GET_CON_LIST,
			0, M_WAITOK | M_NULLOK);
		if (ngmsg == NULL) {
			error = ENOMEM;
			break;
		}
		ng_btsocket_hci_raw_get_token(&ngmsg->header.token);
		pcb->token = ngmsg->header.token;
		pcb->msg = NULL;

		NG_SEND_MSG_PATH(error, ng_btsocket_hci_raw_node, ngmsg, path, 0);
		if (error != 0) {
			pcb->token = 0;
			break;
		}

		error = lksleep(&pcb->msg, &pcb->pcb_lock,
				PCATCH, "hcictl", 
				ng_btsocket_hci_raw_ioctl_timeout * hz);
		pcb->token = 0;

		if (error != 0)
			break;

		if (pcb->msg != NULL &&
		    pcb->msg->header.cmd == NGM_HCI_NODE_GET_CON_LIST) {
			/* Return data back to user space */
			p1 = (ng_hci_node_con_list_ep *)(pcb->msg->data);
			p2 = (ng_hci_node_con_ep *)(p1 + 1);

			p->num_connections = min(p->num_connections,
						p1->num_connections);
			if (p->num_connections > 0)
				error = copyout((caddr_t) p2, 
					(caddr_t) p->connections,
					p->num_connections * sizeof(*p2));
		} else
			error = EINVAL;

		NG_FREE_MSG(pcb->msg); /* checks for != NULL */
		} break;

	case SIOC_HCI_RAW_NODE_GET_LINK_POLICY_MASK: {
		struct ng_btsocket_hci_raw_node_link_policy_mask	*p = 
			(struct ng_btsocket_hci_raw_node_link_policy_mask *) 
				data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_LINK_POLICY_SETTINGS_MASK,
				&p->policy_mask, sizeof(p->policy_mask));
		} break;

	case SIOC_HCI_RAW_NODE_SET_LINK_POLICY_MASK: {
		struct ng_btsocket_hci_raw_node_link_policy_mask	*p = 
			(struct ng_btsocket_hci_raw_node_link_policy_mask *) 
				data;

		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_SET_LINK_POLICY_SETTINGS_MASK,
					&p->policy_mask,
					sizeof(p->policy_mask));
		else
			error = EPERM;
		} break;

	case SIOC_HCI_RAW_NODE_GET_PACKET_MASK: {
		struct ng_btsocket_hci_raw_node_packet_mask	*p = 
			(struct ng_btsocket_hci_raw_node_packet_mask *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_PACKET_MASK,
				&p->packet_mask, sizeof(p->packet_mask));
		} break;

	case SIOC_HCI_RAW_NODE_SET_PACKET_MASK: {
		struct ng_btsocket_hci_raw_node_packet_mask	*p = 
			(struct ng_btsocket_hci_raw_node_packet_mask *) data;

		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_SET_PACKET_MASK,
					&p->packet_mask,
					sizeof(p->packet_mask));
		else
			error = EPERM;
		} break;

	case SIOC_HCI_RAW_NODE_GET_ROLE_SWITCH: {
		struct ng_btsocket_hci_raw_node_role_switch	*p = 
			(struct ng_btsocket_hci_raw_node_role_switch *) data;

		error = ng_btsocket_hci_raw_send_sync_ngmsg(pcb, path,
				NGM_HCI_NODE_GET_ROLE_SWITCH,
				&p->role_switch, sizeof(p->role_switch));
		} break;

	case SIOC_HCI_RAW_NODE_SET_ROLE_SWITCH: {
		struct ng_btsocket_hci_raw_node_role_switch	*p = 
			(struct ng_btsocket_hci_raw_node_role_switch *) data;

		if (pcb->flags & NG_BTSOCKET_HCI_RAW_PRIVILEGED)
			error = ng_btsocket_hci_raw_send_ngmsg(path,
					NGM_HCI_NODE_SET_ROLE_SWITCH,
					&p->role_switch,
					sizeof(p->role_switch));
		else
			error = EPERM;
		} break;

	case SIOC_HCI_RAW_NODE_LIST_NAMES: {
		struct ng_btsocket_hci_raw_node_list_names	*nl =
			(struct ng_btsocket_hci_raw_node_list_names *) data;
		struct nodeinfo					*ni = nl->names;

		if (nl->num_names == 0) {
			error = EINVAL;
			break;
		}

		NG_MKMESSAGE(ngmsg, NGM_GENERIC_COOKIE, NGM_LISTNAMES,
			0, M_WAITOK | M_NULLOK);
		if (ngmsg == NULL) {
			error = ENOMEM;
			break;
		}
		ng_btsocket_hci_raw_get_token(&ngmsg->header.token);
		pcb->token = ngmsg->header.token;
		pcb->msg = NULL;

		NG_SEND_MSG_PATH(error, ng_btsocket_hci_raw_node, ngmsg, ".:", 0);
		if (error != 0) {
			pcb->token = 0;
			break;
		}

		error = lksleep(&pcb->msg, &pcb->pcb_lock,
				PCATCH, "hcictl",
				ng_btsocket_hci_raw_ioctl_timeout * hz);
		pcb->token = 0;

		if (error != 0)
			break;

		if (pcb->msg != NULL && pcb->msg->header.cmd == NGM_LISTNAMES) {
			/* Return data back to user space */
			struct namelist	*nl1 = (struct namelist *) pcb->msg->data;
			struct nodeinfo	*ni1 = &nl1->nodeinfo[0];

			while (nl->num_names > 0 && nl1->numnames > 0) {
				if (strcmp(ni1->type, NG_HCI_NODE_TYPE) == 0) {
					error = copyout((caddr_t) ni1,
							(caddr_t) ni,
							sizeof(*ni));
					if (error != 0)
						break;

					nl->num_names --;
					ni ++;
				}

				nl1->numnames --;
				ni1 ++;
			}

			nl->num_names = ni - nl->names;
		} else
			error = EINVAL;

		NG_FREE_MSG(pcb->msg); /* checks for != NULL */
		} break;

	default:
		error = EINVAL;
		break;
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->control.base.lmsg, error);
} /* ng_btsocket_hci_raw_control */

/*
 * Process getsockopt/setsockopt system calls
 */

void
ng_btsocket_hci_raw_ctloutput(netmsg_t msg)
{
	struct socket				*so = msg->ctloutput.base.nm_so;
	struct sockopt				*sopt = msg->ctloutput.nm_sopt;
	ng_btsocket_hci_raw_pcb_p		 pcb = so2hci_raw_pcb(so);
	struct ng_btsocket_hci_raw_filter	 filter;
	int					 error = 0, dir;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_hci_raw_node == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sopt->sopt_level != SOL_HCI_RAW)
		goto out;

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case SO_HCI_RAW_FILTER:
			error = sooptcopyout(sopt, &pcb->filter,
						sizeof(pcb->filter));
			break;

		case SO_HCI_RAW_DIRECTION:
			dir = (pcb->flags & NG_BTSOCKET_HCI_RAW_DIRECTION)?1:0;
			error = sooptcopyout(sopt, &dir, sizeof(dir));
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case SOPT_SET:
		switch (sopt->sopt_name) {
		case SO_HCI_RAW_FILTER:
			error = sooptcopyin(sopt, &filter, sizeof(filter),
						sizeof(filter));
			if (error == 0)
				bcopy(&filter, &pcb->filter,
						sizeof(pcb->filter));
			break;

		case SO_HCI_RAW_DIRECTION:
			error = sooptcopyin(sopt, &dir, sizeof(dir),
						sizeof(dir));
			if (error != 0)
				break;

			if (dir)
				pcb->flags |= NG_BTSOCKET_HCI_RAW_DIRECTION;
			else
				pcb->flags &= ~NG_BTSOCKET_HCI_RAW_DIRECTION;
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		error = EINVAL;
		break;
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->ctloutput.base.lmsg, error);
} /* ng_btsocket_hci_raw_ctloutput */

/*
 * Detach raw HCI socket
 */

void
ng_btsocket_hci_raw_detach(netmsg_t msg)
{
	struct socket			*so = msg->detach.base.nm_so;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	int				 error = 0;

	KASSERT(pcb != NULL, ("ng_btsocket_hci_raw_detach: pcb == NULL"));

	if (ng_btsocket_hci_raw_node == NULL)
		goto out;

	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_EXCLUSIVE);
	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	LIST_REMOVE(pcb, next);

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_hci_raw_sockets_lock, LK_RELEASE);

	lockuninit(&pcb->pcb_lock);

	bzero(pcb, sizeof(*pcb));
	kfree(pcb, M_NETGRAPH_BTSOCKET_HCI_RAW);

	so->so_pcb = NULL;

out:
	lwkt_replymsg(&msg->detach.base.lmsg, error);
} /* ng_btsocket_hci_raw_detach */

/*
 * Disconnect raw HCI socket
 */

void
ng_btsocket_hci_raw_disconnect(netmsg_t msg)
{
	struct socket			*so = msg->disconnect.base.nm_so;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	int				 error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_hci_raw_node == NULL) {
		error = EINVAL;
		goto out;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);
	soisdisconnected(so);
	lockmgr(&pcb->pcb_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
} /* ng_btsocket_hci_raw_disconnect */

/*
 * Get socket peer's address
 */

void
ng_btsocket_hci_raw_peeraddr(netmsg_t msg)
{
	return (ng_btsocket_hci_raw_sockaddr(msg));
} /* ng_btsocket_hci_raw_peeraddr */

/*
 * Send data
 */

void
ng_btsocket_hci_raw_send(netmsg_t msg)
{
	struct socket			*so = msg->send.base.nm_so;
	struct mbuf			*control = msg->send.nm_control;
	struct mbuf			*m = msg->send.nm_m;
	struct sockaddr			*sa = msg->send.nm_addr;
	ng_btsocket_hci_raw_pcb_p	 pcb = so2hci_raw_pcb(so);
	struct mbuf			*nam = NULL;
	int				 error = 0;

	if (ng_btsocket_hci_raw_node == NULL) {
		error = ENETDOWN;
		goto drop;
	}
	if (pcb == NULL) {
		error = EINVAL;
		goto drop;
	}
	if (control != NULL) {
		error = EINVAL;
		goto drop;
	}

	if (m->m_pkthdr.len < sizeof(ng_hci_cmd_pkt_t) ||
	    m->m_pkthdr.len > sizeof(ng_hci_cmd_pkt_t) + NG_HCI_CMD_PKT_SIZE) {
		error = EMSGSIZE;
		goto drop;
	}

	if (m->m_len < sizeof(ng_hci_cmd_pkt_t)) {
		if ((m = m_pullup(m, sizeof(ng_hci_cmd_pkt_t))) == NULL) {
			error = ENOBUFS;
			goto drop;
		}
	}
	if (*mtod(m, u_int8_t *) != NG_HCI_CMD_PKT) {
		error = ENOTSUP;
		goto drop;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	error = ng_btsocket_hci_raw_filter(pcb, m, 0);
	if (error != 0) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		goto drop;
	}

	if (sa == NULL) {
		if (pcb->addr.hci_node[0] == 0) {
			lockmgr(&pcb->pcb_lock, LK_RELEASE);
			error = EDESTADDRREQ;
			goto drop;
		}

		sa = (struct sockaddr *) &pcb->addr;
	}

	MGET(nam, M_NOWAIT, MT_SONAME);
	if (nam == NULL) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = ENOBUFS;
		goto drop;
	}

	nam->m_len = sizeof(struct sockaddr_hci);
	bcopy(sa,mtod(nam, struct sockaddr_hci *),sizeof(struct sockaddr_hci));

	nam->m_next = m;
	m = NULL;

	lockmgr(&pcb->pcb_lock, LK_RELEASE);

	error = ng_send_fn(ng_btsocket_hci_raw_node, NULL,
				ng_btsocket_hci_raw_output, nam, 0);
	goto out;

drop:
	NG_FREE_M(control); /* NG_FREE_M checks for != NULL */
	NG_FREE_M(nam);
	NG_FREE_M(m);

out:	
	lwkt_replymsg(&msg->send.base.lmsg, error);
} /* ng_btsocket_hci_raw_send */

/*
 * Get socket address
 */

void
ng_btsocket_hci_raw_sockaddr(netmsg_t msg)
{
	struct socket			 *so = msg->sockaddr.base.nm_so;
	struct sockaddr			**nam = msg->sockaddr.nm_nam;
	ng_btsocket_hci_raw_pcb_p	  pcb = so2hci_raw_pcb(so);
	struct sockaddr_hci		  sa;
	int				  error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_hci_raw_node == NULL) {
		error = EINVAL;
		goto out;
	}

	bzero(&sa, sizeof(sa));
	sa.hci_len = sizeof(sa);
	sa.hci_family = AF_BLUETOOTH;

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);
	strlcpy(sa.hci_node, pcb->addr.hci_node, sizeof(sa.hci_node));
	lockmgr(&pcb->pcb_lock, LK_RELEASE);

	*nam = dup_sockaddr((struct sockaddr *) &sa);

	if (*nam == NULL)
		error = ENOMEM;

out:
	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
} /* ng_btsocket_hci_raw_sockaddr */

