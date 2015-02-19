/*
 * ng_btsocket_l2cap.c
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
 * $Id: ng_btsocket_l2cap.c,v 1.16 2003/09/14 23:29:06 max Exp $
 * $FreeBSD: src/sys/netgraph/bluetooth/socket/ng_btsocket_l2cap.c,v 1.25 2007/10/31 16:17:20 emax Exp $
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
#include <netgraph7/bluetooth/include/ng_btsocket_l2cap.h>

/* MALLOC define */
#ifdef NG_SEPARATE_MALLOC
MALLOC_DEFINE(M_NETGRAPH_BTSOCKET_L2CAP, "netgraph_btsocks_l2cap",
		"Netgraph Bluetooth L2CAP sockets");
#else
#define M_NETGRAPH_BTSOCKET_L2CAP M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Netgraph node methods */
static ng_constructor_t	ng_btsocket_l2cap_node_constructor;
static ng_rcvmsg_t	ng_btsocket_l2cap_node_rcvmsg;
static ng_shutdown_t	ng_btsocket_l2cap_node_shutdown;
static ng_newhook_t	ng_btsocket_l2cap_node_newhook;
static ng_connect_t	ng_btsocket_l2cap_node_connect;
static ng_rcvdata_t	ng_btsocket_l2cap_node_rcvdata;
static ng_disconnect_t	ng_btsocket_l2cap_node_disconnect;

static void		ng_btsocket_l2cap_input   (void *, int);
static void		ng_btsocket_l2cap_rtclean (void *, int);

/* Netgraph type descriptor */
static struct ng_type	typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_BTSOCKET_L2CAP_NODE_TYPE,
	.constructor =	ng_btsocket_l2cap_node_constructor,
	.rcvmsg =	ng_btsocket_l2cap_node_rcvmsg,
	.shutdown =	ng_btsocket_l2cap_node_shutdown,
	.newhook =	ng_btsocket_l2cap_node_newhook,
	.connect =	ng_btsocket_l2cap_node_connect,
	.rcvdata =	ng_btsocket_l2cap_node_rcvdata,
	.disconnect =	ng_btsocket_l2cap_node_disconnect,
};

/* Globals */
extern int					ifqmaxlen;
static u_int32_t				ng_btsocket_l2cap_debug_level;
static node_p					ng_btsocket_l2cap_node;
static struct ng_bt_itemq			ng_btsocket_l2cap_queue;
static struct lock				ng_btsocket_l2cap_queue_lock;
static struct task				ng_btsocket_l2cap_queue_task;
static LIST_HEAD(, ng_btsocket_l2cap_pcb)	ng_btsocket_l2cap_sockets;
static struct lock				ng_btsocket_l2cap_sockets_lock;
static LIST_HEAD(, ng_btsocket_l2cap_rtentry)	ng_btsocket_l2cap_rt;
static struct lock				ng_btsocket_l2cap_rt_lock;
static struct task				ng_btsocket_l2cap_rt_task;

/* Sysctl tree */
SYSCTL_DECL(_net_bluetooth_l2cap_sockets);
SYSCTL_NODE(_net_bluetooth_l2cap_sockets, OID_AUTO, seq, CTLFLAG_RW,
	0, "Bluetooth SEQPACKET L2CAP sockets family");
SYSCTL_INT(_net_bluetooth_l2cap_sockets_seq, OID_AUTO, debug_level,
	CTLFLAG_RW,
	&ng_btsocket_l2cap_debug_level, NG_BTSOCKET_WARN_LEVEL,
	"Bluetooth SEQPACKET L2CAP sockets debug level");
SYSCTL_INT(_net_bluetooth_l2cap_sockets_seq, OID_AUTO, queue_len, 
	CTLFLAG_RD,
	&ng_btsocket_l2cap_queue.len, 0,
	"Bluetooth SEQPACKET L2CAP sockets input queue length");
SYSCTL_INT(_net_bluetooth_l2cap_sockets_seq, OID_AUTO, queue_maxlen, 
	CTLFLAG_RD,
	&ng_btsocket_l2cap_queue.maxlen, 0,
	"Bluetooth SEQPACKET L2CAP sockets input queue max. length");
SYSCTL_INT(_net_bluetooth_l2cap_sockets_seq, OID_AUTO, queue_drops, 
	CTLFLAG_RD,
	&ng_btsocket_l2cap_queue.drops, 0,
	"Bluetooth SEQPACKET L2CAP sockets input queue drops");

/* Debug */
#define NG_BTSOCKET_L2CAP_INFO \
	if (ng_btsocket_l2cap_debug_level >= NG_BTSOCKET_INFO_LEVEL) \
		kprintf

#define NG_BTSOCKET_L2CAP_WARN \
	if (ng_btsocket_l2cap_debug_level >= NG_BTSOCKET_WARN_LEVEL) \
		kprintf

#define NG_BTSOCKET_L2CAP_ERR \
	if (ng_btsocket_l2cap_debug_level >= NG_BTSOCKET_ERR_LEVEL) \
		kprintf

#define NG_BTSOCKET_L2CAP_ALERT \
	if (ng_btsocket_l2cap_debug_level >= NG_BTSOCKET_ALERT_LEVEL) \
		kprintf

/* 
 * Netgraph message processing routines
 */

static int ng_btsocket_l2cap_process_l2ca_con_req_rsp
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);
static int ng_btsocket_l2cap_process_l2ca_con_rsp_rsp
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);
static int ng_btsocket_l2cap_process_l2ca_con_ind
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);

static int ng_btsocket_l2cap_process_l2ca_cfg_req_rsp
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);
static int ng_btsocket_l2cap_process_l2ca_cfg_rsp_rsp
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);
static int ng_btsocket_l2cap_process_l2ca_cfg_ind
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);

static int ng_btsocket_l2cap_process_l2ca_discon_rsp
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);
static int ng_btsocket_l2cap_process_l2ca_discon_ind
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);

static int ng_btsocket_l2cap_process_l2ca_write_rsp
	(struct ng_mesg *, ng_btsocket_l2cap_rtentry_p);

/*
 * Send L2CA_xxx messages to the lower layer
 */

static int  ng_btsocket_l2cap_send_l2ca_con_req
	(ng_btsocket_l2cap_pcb_p);
static int  ng_btsocket_l2cap_send_l2ca_con_rsp_req
	(u_int32_t, ng_btsocket_l2cap_rtentry_p, bdaddr_p, int, int, int);
static int  ng_btsocket_l2cap_send_l2ca_cfg_req
	(ng_btsocket_l2cap_pcb_p);
static int  ng_btsocket_l2cap_send_l2ca_cfg_rsp
	(ng_btsocket_l2cap_pcb_p);
static int  ng_btsocket_l2cap_send_l2ca_discon_req
	(u_int32_t, ng_btsocket_l2cap_pcb_p);

static int ng_btsocket_l2cap_send2
	(ng_btsocket_l2cap_pcb_p);

/* 
 * Timeout processing routines
 */

static void ng_btsocket_l2cap_timeout         (ng_btsocket_l2cap_pcb_p);
static void ng_btsocket_l2cap_untimeout       (ng_btsocket_l2cap_pcb_p);
static void ng_btsocket_l2cap_process_timeout (void *);

/* 
 * Other stuff 
 */

static ng_btsocket_l2cap_pcb_p     ng_btsocket_l2cap_pcb_by_addr(bdaddr_p, int);
static ng_btsocket_l2cap_pcb_p     ng_btsocket_l2cap_pcb_by_token(u_int32_t);
static ng_btsocket_l2cap_pcb_p     ng_btsocket_l2cap_pcb_by_cid (bdaddr_p, int);
static int                         ng_btsocket_l2cap_result2errno(int);

#define ng_btsocket_l2cap_wakeup_input_task() \
	taskqueue_enqueue(taskqueue_swi, &ng_btsocket_l2cap_queue_task)

#define ng_btsocket_l2cap_wakeup_route_task() \
	taskqueue_enqueue(taskqueue_swi, &ng_btsocket_l2cap_rt_task)

/*****************************************************************************
 *****************************************************************************
 **                        Netgraph node interface
 *****************************************************************************
 *****************************************************************************/

/*
 * Netgraph node constructor. Do not allow to create node of this type.
 */

static int
ng_btsocket_l2cap_node_constructor(node_p node)
{
	return (EINVAL);
} /* ng_btsocket_l2cap_node_constructor */

/*
 * Do local shutdown processing. Let old node go and create new fresh one.
 */

static int
ng_btsocket_l2cap_node_shutdown(node_p node)
{
	int	error = 0;

	NG_NODE_UNREF(node);

	/* Create new node */
	error = ng_make_node_common(&typestruct, &ng_btsocket_l2cap_node);
	if (error != 0) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not create Netgraph node, error=%d\n", __func__, error);

		ng_btsocket_l2cap_node = NULL;

		return (error);
	}

	error = ng_name_node(ng_btsocket_l2cap_node,
				NG_BTSOCKET_L2CAP_NODE_TYPE);
	if (error != 0) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not name Netgraph node, error=%d\n", __func__, error);

		NG_NODE_UNREF(ng_btsocket_l2cap_node);
		ng_btsocket_l2cap_node = NULL;

		return (error);
	}
		
	return (0);
} /* ng_btsocket_l2cap_node_shutdown */

/*
 * We allow any hook to be connected to the node.
 */

static int
ng_btsocket_l2cap_node_newhook(node_p node, hook_p hook, char const *name)
{
	return (0);
} /* ng_btsocket_l2cap_node_newhook */

/* 
 * Just say "YEP, that's OK by me!"
 */

static int
ng_btsocket_l2cap_node_connect(hook_p hook)
{
	NG_HOOK_SET_PRIVATE(hook, NULL);
	NG_HOOK_REF(hook); /* Keep extra reference to the hook */

#if 0
	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));
	NG_HOOK_FORCE_QUEUE(hook);
#endif

	return (0);
} /* ng_btsocket_l2cap_node_connect */

/*
 * Hook disconnection. Schedule route cleanup task
 */

static int
ng_btsocket_l2cap_node_disconnect(hook_p hook)
{
	/*
	 * If hook has private information than we must have this hook in
	 * the routing table and must schedule cleaning for the routing table.
	 * Otherwise hook was connected but we never got "hook_info" message,
	 * so we have never added this hook to the routing table and it save
	 * to just delete it.
	 */

	if (NG_HOOK_PRIVATE(hook) != NULL)
		return (ng_btsocket_l2cap_wakeup_route_task());

	NG_HOOK_UNREF(hook); /* Remove extra reference */

	return (0);
} /* ng_btsocket_l2cap_node_disconnect */

/*
 * Process incoming messages 
 */

static int
ng_btsocket_l2cap_node_rcvmsg(node_p node, item_p item, hook_p hook)
{
	struct ng_mesg	*msg = NGI_MSG(item); /* item still has message */
	int		 error = 0;

	if (msg != NULL && msg->header.typecookie == NGM_L2CAP_COOKIE) {
		lockmgr(&ng_btsocket_l2cap_queue_lock, LK_EXCLUSIVE);
		if (NG_BT_ITEMQ_FULL(&ng_btsocket_l2cap_queue)) {
			NG_BTSOCKET_L2CAP_ERR(
"%s: Input queue is full (msg)\n", __func__);

			NG_BT_ITEMQ_DROP(&ng_btsocket_l2cap_queue);
			NG_FREE_ITEM(item);
			error = ENOBUFS;
		} else {
			if (hook != NULL) {
				NG_HOOK_REF(hook);
				NGI_SET_HOOK(item, hook);
			}

			ng_ref_item(item);
			NG_BT_ITEMQ_ENQUEUE(&ng_btsocket_l2cap_queue, item);
			error = ng_btsocket_l2cap_wakeup_input_task();
		}
		lockmgr(&ng_btsocket_l2cap_queue_lock, LK_RELEASE);
	} else {
		NG_FREE_ITEM(item);
		error = EINVAL;
	}

	return (error);
} /* ng_btsocket_l2cap_node_rcvmsg */

/*
 * Receive data on a hook
 */

static int
ng_btsocket_l2cap_node_rcvdata(hook_p hook, item_p item)
{
	int	error = 0;

	lockmgr(&ng_btsocket_l2cap_queue_lock, LK_EXCLUSIVE);
	if (NG_BT_ITEMQ_FULL(&ng_btsocket_l2cap_queue)) {
		NG_BTSOCKET_L2CAP_ERR(
"%s: Input queue is full (data)\n", __func__);

		NG_BT_ITEMQ_DROP(&ng_btsocket_l2cap_queue);
		NG_FREE_ITEM(item);
		error = ENOBUFS;
	} else {
		NG_HOOK_REF(hook);
		NGI_SET_HOOK(item, hook);

		ng_ref_item(item);
		NG_BT_ITEMQ_ENQUEUE(&ng_btsocket_l2cap_queue, item);
		error = ng_btsocket_l2cap_wakeup_input_task();
	}
	lockmgr(&ng_btsocket_l2cap_queue_lock, LK_RELEASE);

	return (error);
} /* ng_btsocket_l2cap_node_rcvdata */

/*
 * Process L2CA_Connect respose. Socket layer must have initiated connection,
 * so we have to have a socket associated with message token.
 */

static int
ng_btsocket_l2cap_process_l2ca_con_req_rsp(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_con_op	*op = NULL;
	ng_btsocket_l2cap_pcb_t	*pcb = NULL;
	int			 error = 0;

	if (msg->header.arglen != sizeof(*op))
		return (EMSGSIZE);

	op = (ng_l2cap_l2ca_con_op *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* Look for the socket with the token */
	pcb = ng_btsocket_l2cap_pcb_by_token(msg->header.token);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (ENOENT);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Connect response, token=%d, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, result=%d, status=%d, " \
"state=%d\n",	__func__, msg->header.token,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->psm, op->lcid, op->result, op->status,
		pcb->state);

	if (pcb->state != NG_BTSOCKET_L2CAP_CONNECTING) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (ENOENT);
	}

	ng_btsocket_l2cap_untimeout(pcb);

	if (op->result == NG_L2CAP_PENDING) {
		ng_btsocket_l2cap_timeout(pcb);
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (0);
	}

	if (op->result == NG_L2CAP_SUCCESS) {
		/*
		 * Channel is now open, so update local channel ID and 
		 * start configuration process. Source and destination
		 * addresses as well as route must be already set.
		 */

		pcb->cid = op->lcid;

		error = ng_btsocket_l2cap_send_l2ca_cfg_req(pcb);
		if (error != 0) {
			/* Send disconnect request with "zero" token */
			ng_btsocket_l2cap_send_l2ca_discon_req(0, pcb);

			/* ... and close the socket */
			pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
			soisdisconnected(pcb->so);
		} else {
			pcb->cfg_state = NG_BTSOCKET_L2CAP_CFG_IN_SENT;
			pcb->state = NG_BTSOCKET_L2CAP_CONFIGURING;

			ng_btsocket_l2cap_timeout(pcb);
		}
	} else {
		/*
		 * We have failed to open connection, so convert result
		 * code to "errno" code and disconnect the socket. Channel
		 * already has been closed.
		 */

		pcb->so->so_error = ng_btsocket_l2cap_result2errno(op->result);
		pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
		soisdisconnected(pcb->so); 
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (error);
} /* ng_btsocket_l2cap_process_l2ca_con_req_rsp */

/*
 * Process L2CA_ConnectRsp response
 */

static int
ng_btsocket_l2cap_process_l2ca_con_rsp_rsp(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_con_rsp_op	*op = NULL;
	ng_btsocket_l2cap_pcb_t		*pcb = NULL;

	if (msg->header.arglen != sizeof(*op)) 
		return (EMSGSIZE);

	op = (ng_l2cap_l2ca_con_rsp_op *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* Look for the socket with the token */
	pcb = ng_btsocket_l2cap_pcb_by_token(msg->header.token);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (ENOENT);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_ConnectRsp response, token=%d, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, result=%d, state=%d\n",
		__func__, msg->header.token,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->psm, pcb->cid, op->result, pcb->state);

	if (pcb->state != NG_BTSOCKET_L2CAP_CONNECTING) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (ENOENT);
	}

	ng_btsocket_l2cap_untimeout(pcb);

	/* Check the result and disconnect the socket on failure */
	if (op->result != NG_L2CAP_SUCCESS) {
		/* Close the socket - channel already closed */
		pcb->so->so_error = ng_btsocket_l2cap_result2errno(op->result);
		pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
		soisdisconnected(pcb->so);
	} else {
		/* Move to CONFIGURING state and wait for CONFIG_IND */
		pcb->cfg_state = 0;
		pcb->state = NG_BTSOCKET_L2CAP_CONFIGURING;
		ng_btsocket_l2cap_timeout(pcb);
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (0);
} /* ng_btsocket_process_l2ca_con_rsp_rsp */

/*
 * Process L2CA_Connect indicator. Find socket that listens on address 
 * and PSM. Find exact or closest match. Create new socket and initiate 
 * connection.
 */

static int
ng_btsocket_l2cap_process_l2ca_con_ind(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_con_ind_ip	*ip = NULL;
	ng_btsocket_l2cap_pcb_t		*pcb = NULL, *pcb1 = NULL;
	int				 error = 0;
	u_int32_t			 token = 0;
	u_int16_t			 result = 0;

	if (msg->header.arglen != sizeof(*ip))
		return (EMSGSIZE);

	ip = (ng_l2cap_l2ca_con_ind_ip *)(msg->data);

	NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Connect indicator, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, ident=%d\n",
		__func__,
		rt->src.b[5], rt->src.b[4], rt->src.b[3],
		rt->src.b[2], rt->src.b[1], rt->src.b[0],
		ip->bdaddr.b[5], ip->bdaddr.b[4], ip->bdaddr.b[3],
		ip->bdaddr.b[2], ip->bdaddr.b[1], ip->bdaddr.b[0],
		ip->psm, ip->lcid, ip->ident);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);
	
	pcb = ng_btsocket_l2cap_pcb_by_addr(&rt->src, ip->psm);
	if (pcb != NULL) {
		struct socket	*so1 = NULL;

		lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

		/*
		 * First check the pending connections queue and if we have
		 * space then create new socket and set proper source address.
		 */

		if (pcb->so->so_qlen <= pcb->so->so_qlimit)
			so1 = sonewconn(pcb->so, 0);

		if (so1 == NULL) {
			result = NG_L2CAP_NO_RESOURCES;
			goto respond;
		}

		/*
		 * If we got here than we have created new socket. So complete 
		 * connection. If we we listening on specific address then copy 
		 * source address from listening socket, otherwise copy source 
		 * address from hook's routing information.
		 */

		pcb1 = so2l2cap_pcb(so1);
		KASSERT((pcb1 != NULL),
("%s: pcb1 == NULL\n", __func__));

 		lockmgr(&pcb1->pcb_lock, LK_EXCLUSIVE);

		if (bcmp(&pcb->src, NG_HCI_BDADDR_ANY, sizeof(pcb->src)) != 0)
			bcopy(&pcb->src, &pcb1->src, sizeof(pcb1->src));
		else
			bcopy(&rt->src, &pcb1->src, sizeof(pcb1->src));

		pcb1->flags &= ~NG_BTSOCKET_L2CAP_CLIENT;

		bcopy(&ip->bdaddr, &pcb1->dst, sizeof(pcb1->dst));
		pcb1->psm = ip->psm;
		pcb1->cid = ip->lcid;
		pcb1->rt = rt;

		/* Copy socket settings */
		pcb1->imtu = pcb->imtu;
		bcopy(&pcb->oflow, &pcb1->oflow, sizeof(pcb1->oflow));
		pcb1->flush_timo = pcb->flush_timo;

		token = pcb1->token;
	} else
		/* Nobody listens on requested BDADDR/PSM */
		result = NG_L2CAP_PSM_NOT_SUPPORTED;

respond:
	error = ng_btsocket_l2cap_send_l2ca_con_rsp_req(token, rt,
			&ip->bdaddr, ip->ident, ip->lcid, result);
	if (pcb1 != NULL) {
		if (error != 0) {
			pcb1->so->so_error = error;
			pcb1->state = NG_BTSOCKET_L2CAP_CLOSED;
			soisdisconnected(pcb1->so);
		} else {
			pcb1->state = NG_BTSOCKET_L2CAP_CONNECTING;
			soisconnecting(pcb1->so);

			ng_btsocket_l2cap_timeout(pcb1);
		}

		lockmgr(&pcb1->pcb_lock, LK_RELEASE);
	}

	if (pcb != NULL)
		lockmgr(&pcb->pcb_lock, LK_RELEASE);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (error);
} /* ng_btsocket_l2cap_process_l2ca_con_ind */

/*
 * Process L2CA_Config response
 */

static int
ng_btsocket_l2cap_process_l2ca_cfg_req_rsp(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_cfg_op	*op = NULL;
	ng_btsocket_l2cap_pcb_p	 pcb = NULL;

	if (msg->header.arglen != sizeof(*op))
		return (EMSGSIZE);

	op = (ng_l2cap_l2ca_cfg_op *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* 
	 * Socket must have issued a Configure request, so we must have a 
	 * socket that wants to be configured. Use Netgraph message token 
	 * to find it
	 */

	pcb = ng_btsocket_l2cap_pcb_by_token(msg->header.token);
	if (pcb == NULL) {
		/*
		 * XXX FIXME what to do here? We could not find a
		 * socket with requested token. We even can not send
		 * Disconnect, because we do not know channel ID
		 */

		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (ENOENT);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

        NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Config response, token=%d, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, result=%d, state=%d, " \
"cfg_state=%x\n",
		__func__, msg->header.token,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->psm, pcb->cid, op->result, pcb->state, pcb->cfg_state);

	if (pcb->state != NG_BTSOCKET_L2CAP_CONFIGURING) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (ENOENT);
	}

	if (op->result == NG_L2CAP_SUCCESS) {
		/*
		 * XXX FIXME Actually set flush and link timeout.
		 * Set QoS here if required. Resolve conficts (flush_timo). 
		 * Save incoming MTU (peer's outgoing MTU) and outgoing flow 
		 * spec.
		 */

		pcb->imtu = op->imtu;
		bcopy(&op->oflow, &pcb->oflow, sizeof(pcb->oflow));
		pcb->flush_timo = op->flush_timo;

		/*
		 * We have configured incoming side, so record it and check 
		 * if configuration is complete. If complete then mark socket
		 * as connected, otherwise wait for the peer.
		 */

		pcb->cfg_state &= ~NG_BTSOCKET_L2CAP_CFG_IN_SENT;
		pcb->cfg_state |= NG_BTSOCKET_L2CAP_CFG_IN;

		if (pcb->cfg_state == NG_BTSOCKET_L2CAP_CFG_BOTH) {
			/* Configuration complete - mark socket as open */
			ng_btsocket_l2cap_untimeout(pcb);
			pcb->state = NG_BTSOCKET_L2CAP_OPEN;
			soisconnected(pcb->so); 
		} 
	} else {
		/*
		 * Something went wrong. Could be unacceptable parameters,
		 * reject or unknown option. That's too bad, but we will
		 * not negotiate. Send Disconnect and close the channel.
		 */

		ng_btsocket_l2cap_untimeout(pcb);

		switch (op->result) {
		case NG_L2CAP_UNACCEPTABLE_PARAMS:
		case NG_L2CAP_UNKNOWN_OPTION:
			pcb->so->so_error = EINVAL;
			break;

		default:
			pcb->so->so_error = ECONNRESET;
			break;
		}

		/* Send disconnect with "zero" token */
		ng_btsocket_l2cap_send_l2ca_discon_req(0, pcb);

		/* ... and close the socket */
		pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
		soisdisconnected(pcb->so);
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (0);
} /* ng_btsocket_l2cap_process_l2ca_cfg_req_rsp */

/*
 * Process L2CA_ConfigRsp response
 */

static int
ng_btsocket_l2cap_process_l2ca_cfg_rsp_rsp(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_cfg_rsp_op	*op = NULL;
	ng_btsocket_l2cap_pcb_t		*pcb = NULL;
	int				 error = 0;

	if (msg->header.arglen != sizeof(*op))
		return (EMSGSIZE);

	op = (ng_l2cap_l2ca_cfg_rsp_op *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* Look for the socket with the token */
	pcb = ng_btsocket_l2cap_pcb_by_token(msg->header.token);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (ENOENT);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

        NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_ConfigRsp response, token=%d, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, result=%d, state=%d, " \
"cfg_state=%x\n",
		__func__, msg->header.token,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->psm, pcb->cid, op->result, pcb->state, pcb->cfg_state);

	if (pcb->state != NG_BTSOCKET_L2CAP_CONFIGURING) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (ENOENT);
	}

	/* Check the result and disconnect socket of failure */
	if (op->result != NG_L2CAP_SUCCESS)
		goto disconnect;

	/*
	 * Now we done with remote side configuration. Configure local 
	 * side if we have not done it yet.
	 */

	pcb->cfg_state &= ~NG_BTSOCKET_L2CAP_CFG_OUT_SENT;
	pcb->cfg_state |= NG_BTSOCKET_L2CAP_CFG_OUT;

	if (pcb->cfg_state == NG_BTSOCKET_L2CAP_CFG_BOTH) {
		/* Configuration complete - mask socket as open */
		ng_btsocket_l2cap_untimeout(pcb);
		pcb->state = NG_BTSOCKET_L2CAP_OPEN;
		soisconnected(pcb->so);
	} else {
		if (!(pcb->cfg_state & NG_BTSOCKET_L2CAP_CFG_IN_SENT)) {
			/* Send L2CA_Config request - incoming path */
			error = ng_btsocket_l2cap_send_l2ca_cfg_req(pcb);
			if (error != 0)
				goto disconnect;

			pcb->cfg_state |= NG_BTSOCKET_L2CAP_CFG_IN_SENT;
		}
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (error);

disconnect:
	ng_btsocket_l2cap_untimeout(pcb);

	/* Send disconnect with "zero" token */
	ng_btsocket_l2cap_send_l2ca_discon_req(0, pcb);

	/* ... and close the socket */
	pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
	soisdisconnected(pcb->so);

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (error);
} /* ng_btsocket_l2cap_process_l2ca_cfg_rsp_rsp */

/*
 * Process L2CA_Config indicator
 */

static int
ng_btsocket_l2cap_process_l2ca_cfg_ind(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_cfg_ind_ip	*ip = NULL;
	ng_btsocket_l2cap_pcb_t		*pcb = NULL;
	int				 error = 0;

	if (msg->header.arglen != sizeof(*ip))
		return (EMSGSIZE);

	ip = (ng_l2cap_l2ca_cfg_ind_ip *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* Check for the open socket that has given channel ID */
	pcb = ng_btsocket_l2cap_pcb_by_cid(&rt->src, ip->lcid);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (ENOENT);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

        NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Config indicator, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, state=%d, cfg_state=%x\n",
		__func__,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->psm, pcb->cid, pcb->state, pcb->cfg_state);

	/* XXX FIXME re-configuration on open socket */
 	if (pcb->state != NG_BTSOCKET_L2CAP_CONFIGURING) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (ENOENT);
	}

	/*
	 * XXX FIXME Actually set flush and link timeout. Set QoS here if
	 * required. Resolve conficts (flush_timo). Note outgoing MTU (peer's 
	 * incoming MTU) and incoming flow spec.
	 */

	pcb->omtu = ip->omtu;
	bcopy(&ip->iflow, &pcb->iflow, sizeof(pcb->iflow));
	pcb->flush_timo = ip->flush_timo;

	/*
	 * Send L2CA_Config response to our peer and check for the errors, 
	 * if any send disconnect to close the channel. 
	 */

	if (!(pcb->cfg_state & NG_BTSOCKET_L2CAP_CFG_OUT_SENT)) {
		error = ng_btsocket_l2cap_send_l2ca_cfg_rsp(pcb);
		if (error != 0) {
			ng_btsocket_l2cap_untimeout(pcb);

			pcb->so->so_error = error;

			/* Send disconnect with "zero" token */
			ng_btsocket_l2cap_send_l2ca_discon_req(0, pcb);

			/* ... and close the socket */
			pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
			soisdisconnected(pcb->so);
		} else
			pcb->cfg_state |= NG_BTSOCKET_L2CAP_CFG_OUT_SENT;
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (error);
} /* ng_btsocket_l2cap_process_l2cap_cfg_ind */

/*
 * Process L2CA_Disconnect response
 */

static int
ng_btsocket_l2cap_process_l2ca_discon_rsp(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_discon_op	*op = NULL;
	ng_btsocket_l2cap_pcb_t	*pcb = NULL;

	/* Check message */
	if (msg->header.arglen != sizeof(*op))
		return (EMSGSIZE);

	op = (ng_l2cap_l2ca_discon_op *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/*
	 * Socket layer must have issued L2CA_Disconnect request, so there 
	 * must be a socket that wants to be disconnected. Use Netgraph 
	 * message token to find it.
	 */

	pcb = ng_btsocket_l2cap_pcb_by_token(msg->header.token);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (0);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	/* XXX Close socket no matter what op->result says */
	if (pcb->state != NG_BTSOCKET_L2CAP_CLOSED) {
       		NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Disconnect response, token=%d, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, result=%d, state=%d\n",
			__func__, msg->header.token,
			pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
			pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
			pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
			pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
			pcb->psm, pcb->cid, op->result, pcb->state);

		ng_btsocket_l2cap_untimeout(pcb);

		pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
		soisdisconnected(pcb->so);
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (0);
} /* ng_btsocket_l2cap_process_l2ca_discon_rsp */

/*
 * Process L2CA_Disconnect indicator
 */

static int
ng_btsocket_l2cap_process_l2ca_discon_ind(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_discon_ind_ip	*ip = NULL;
	ng_btsocket_l2cap_pcb_t		*pcb = NULL;

	/* Check message */
	if (msg->header.arglen != sizeof(*ip))
		return (EMSGSIZE);

	ip = (ng_l2cap_l2ca_discon_ind_ip *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* Look for the socket with given channel ID */
	pcb = ng_btsocket_l2cap_pcb_by_cid(&rt->src, ip->lcid);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (0);
	}

	/*
	 * Channel has already been destroyed, so disconnect the socket 
	 * and be done with it. If there was any pending request we can
	 * not do anything here anyway.
	 */

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

       	NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Disconnect indicator, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, state=%d\n",
		__func__,
		pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
		pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
		pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
		pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
		pcb->psm, pcb->cid, pcb->state);

	if (pcb->flags & NG_BTSOCKET_L2CAP_TIMO)
		ng_btsocket_l2cap_untimeout(pcb);

	pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
	soisdisconnected(pcb->so);

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (0);
} /* ng_btsocket_l2cap_process_l2ca_discon_ind */

/*
 * Process L2CA_Write response
 */

static int 
ng_btsocket_l2cap_process_l2ca_write_rsp(struct ng_mesg *msg,
		ng_btsocket_l2cap_rtentry_p rt)
{
	ng_l2cap_l2ca_write_op	*op = NULL;
	ng_btsocket_l2cap_pcb_t	*pcb = NULL;

	/* Check message */
	if (msg->header.arglen != sizeof(*op))
		return (EMSGSIZE);

	op = (ng_l2cap_l2ca_write_op *)(msg->data);

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/* Look for the socket with given token */
	pcb = ng_btsocket_l2cap_pcb_by_token(msg->header.token);
	if (pcb == NULL) {
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
		return (ENOENT);
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

       	NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CA_Write response, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dst bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, lcid=%d, result=%d, length=%d, " \
"state=%d\n",		__func__,
			pcb->src.b[5], pcb->src.b[4], pcb->src.b[3],
			pcb->src.b[2], pcb->src.b[1], pcb->src.b[0],
			pcb->dst.b[5], pcb->dst.b[4], pcb->dst.b[3],
			pcb->dst.b[2], pcb->dst.b[1], pcb->dst.b[0],
			pcb->psm, pcb->cid, op->result, op->length,
			pcb->state);

	if (pcb->state != NG_BTSOCKET_L2CAP_OPEN) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

		return (ENOENT);
	}
	
	ng_btsocket_l2cap_untimeout(pcb);

	/*
 	 * Check if we have more data to send
 	 */

	sbdroprecord(&pcb->so->so_snd.sb);
	if (pcb->so->so_snd.sb.sb_cc > 0) {
		if (ng_btsocket_l2cap_send2(pcb) == 0)
			ng_btsocket_l2cap_timeout(pcb);
		else
			sbdroprecord(&pcb->so->so_snd.sb); /* XXX */
	}

	/*
	 * Now set the result, drop packet from the socket send queue and 
	 * ask for more (wakeup sender)
	 */

	pcb->so->so_error = ng_btsocket_l2cap_result2errno(op->result);
	sowwakeup(pcb->so);

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	return (0);
} /* ng_btsocket_l2cap_process_l2ca_write_rsp */

/*
 * Send L2CA_Connect request
 */

static int
ng_btsocket_l2cap_send_l2ca_con_req(ng_btsocket_l2cap_pcb_p pcb)
{
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_con_ip	*ip = NULL;
	int			 error = 0;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->rt == NULL || 
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CON,
		sizeof(*ip), M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	msg->header.token = pcb->token;

	ip = (ng_l2cap_l2ca_con_ip *)(msg->data);
	bcopy(&pcb->dst, &ip->bdaddr, sizeof(ip->bdaddr));
	ip->psm = pcb->psm;

	NG_SEND_MSG_HOOK(error, ng_btsocket_l2cap_node, msg,pcb->rt->hook, 0);

	return (error);
} /* ng_btsocket_l2cap_send_l2ca_con_req */

/*
 * Send L2CA_Connect response
 */

static int
ng_btsocket_l2cap_send_l2ca_con_rsp_req(u_int32_t token,
		ng_btsocket_l2cap_rtentry_p rt, bdaddr_p dst, int ident, 
		int lcid, int result)
{
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_con_rsp_ip	*ip = NULL;
	int				 error = 0;

	if (rt == NULL || rt->hook == NULL || NG_HOOK_NOT_VALID(rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CON_RSP,
		sizeof(*ip), M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	msg->header.token = token;

	ip = (ng_l2cap_l2ca_con_rsp_ip *)(msg->data);
	bcopy(dst, &ip->bdaddr, sizeof(ip->bdaddr));
	ip->ident = ident;
	ip->lcid = lcid;
	ip->result = result;
	ip->status = 0;

	NG_SEND_MSG_HOOK(error, ng_btsocket_l2cap_node, msg, rt->hook, 0);

	return (error);
} /* ng_btsocket_l2cap_send_l2ca_con_rsp_req */

/*
 * Send L2CA_Config request
 */

static int
ng_btsocket_l2cap_send_l2ca_cfg_req(ng_btsocket_l2cap_pcb_p pcb)
{
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_cfg_ip	*ip = NULL;
	int			 error = 0;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->rt == NULL || 
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CFG,
		sizeof(*ip), M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	msg->header.token = pcb->token;

	ip = (ng_l2cap_l2ca_cfg_ip *)(msg->data);
	ip->lcid = pcb->cid;
	ip->imtu = pcb->imtu;
	bcopy(&pcb->oflow, &ip->oflow, sizeof(ip->oflow));
	ip->flush_timo = pcb->flush_timo;
	ip->link_timo = pcb->link_timo;

	NG_SEND_MSG_HOOK(error, ng_btsocket_l2cap_node, msg,pcb->rt->hook, 0);

	return (error);
} /* ng_btsocket_l2cap_send_l2ca_cfg_req */

/*
 * Send L2CA_Config response
 */

static int
ng_btsocket_l2cap_send_l2ca_cfg_rsp(ng_btsocket_l2cap_pcb_p pcb)
{
	struct ng_mesg			*msg = NULL;
	ng_l2cap_l2ca_cfg_rsp_ip	*ip = NULL;
	int				 error = 0;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->rt == NULL || 
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_CFG_RSP,
		sizeof(*ip), M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	msg->header.token = pcb->token;

	ip = (ng_l2cap_l2ca_cfg_rsp_ip *)(msg->data);
	ip->lcid = pcb->cid;
	ip->omtu = pcb->omtu;
	bcopy(&pcb->iflow, &ip->iflow, sizeof(ip->iflow));

	NG_SEND_MSG_HOOK(error, ng_btsocket_l2cap_node, msg, pcb->rt->hook, 0);

	return (error);
} /* ng_btsocket_l2cap_send_l2ca_cfg_rsp */

/*
 * Send L2CA_Disconnect request
 */

static int
ng_btsocket_l2cap_send_l2ca_discon_req(u_int32_t token,
		ng_btsocket_l2cap_pcb_p pcb)
{
	struct ng_mesg		*msg = NULL;
	ng_l2cap_l2ca_discon_ip	*ip = NULL;
	int			 error = 0;

	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->rt == NULL || 
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook))
		return (ENETDOWN); 

	NG_MKMESSAGE(msg, NGM_L2CAP_COOKIE, NGM_L2CAP_L2CA_DISCON,
		sizeof(*ip), M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);

	msg->header.token = token;

	ip = (ng_l2cap_l2ca_discon_ip *)(msg->data);
	ip->lcid = pcb->cid;

	NG_SEND_MSG_HOOK(error, ng_btsocket_l2cap_node, msg,pcb->rt->hook, 0);

	return (error);
} /* ng_btsocket_l2cap_send_l2ca_discon_req */

/*****************************************************************************
 *****************************************************************************
 **                              Socket interface
 *****************************************************************************
 *****************************************************************************/

/*
 * L2CAP sockets data input routine
 */

static void
ng_btsocket_l2cap_data_input(struct mbuf *m, hook_p hook)
{
	ng_l2cap_hdr_t			*hdr = NULL;
	ng_l2cap_clt_hdr_t		*clt_hdr = NULL;
	ng_btsocket_l2cap_pcb_t		*pcb = NULL;
	ng_btsocket_l2cap_rtentry_t	*rt = NULL;

	if (hook == NULL) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Invalid source hook for L2CAP data packet\n", __func__);
		goto drop;
	}

	rt = (ng_btsocket_l2cap_rtentry_t *) NG_HOOK_PRIVATE(hook);
	if (rt == NULL) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not find out source bdaddr for L2CAP data packet\n", __func__);
		goto drop;
	}

	/* Make sure we can access header */
	if (m->m_pkthdr.len < sizeof(*hdr)) {
		NG_BTSOCKET_L2CAP_ERR(
"%s: L2CAP data packet too small, len=%d\n", __func__, m->m_pkthdr.len);
		goto drop;
	}

	if (m->m_len < sizeof(*hdr)) { 
		m = m_pullup(m, sizeof(*hdr));
		if (m == NULL)
			goto drop;
	}

	/* Strip L2CAP packet header and verify packet length */
	hdr = mtod(m, ng_l2cap_hdr_t *);
	m_adj(m, sizeof(*hdr));

	if (hdr->length != m->m_pkthdr.len) {
		NG_BTSOCKET_L2CAP_ERR(
"%s: Bad L2CAP data packet length, len=%d, length=%d\n",
			__func__, m->m_pkthdr.len, hdr->length);
		goto drop;
	}

	/*
	 * Now process packet. Two cases:
	 *
	 * 1) Normal packet (cid != 2) then find connected socket and append
	 *    mbuf to the socket queue. Wakeup socket.
	 *
	 * 2) Broadcast packet (cid == 2) then find all sockets that connected
	 *    to the given PSM and have SO_BROADCAST bit set and append mbuf
	 *    to the socket queue. Wakeup socket.
	 */

	NG_BTSOCKET_L2CAP_INFO(
"%s: Received L2CAP data packet: src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dcid=%d, length=%d\n",
		__func__, 
		rt->src.b[5], rt->src.b[4], rt->src.b[3],
		rt->src.b[2], rt->src.b[1], rt->src.b[0],
		hdr->dcid, hdr->length);

	if (hdr->dcid >= NG_L2CAP_FIRST_CID) {

		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

		/* Normal packet: find connected socket */
		pcb = ng_btsocket_l2cap_pcb_by_cid(&rt->src, hdr->dcid);
		if (pcb == NULL) {
			lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
			goto drop;
		}

		lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

		if (pcb->state != NG_BTSOCKET_L2CAP_OPEN) {
			NG_BTSOCKET_L2CAP_ERR(
"%s: No connected socket found, src bdaddr=%x:%x:%x:%x:%x:%x, dcid=%d, " \
"state=%d\n",			__func__,
				rt->src.b[5], rt->src.b[4], rt->src.b[3],
				rt->src.b[2], rt->src.b[1], rt->src.b[0],
				hdr->dcid, pcb->state);

			lockmgr(&pcb->pcb_lock, LK_RELEASE);
			lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
			goto drop;
		}

		/* Check packet size against socket's incoming MTU */
		if (hdr->length > pcb->imtu) {
			NG_BTSOCKET_L2CAP_ERR(
"%s: L2CAP data packet too big, src bdaddr=%x:%x:%x:%x:%x:%x, " \
"dcid=%d, length=%d, imtu=%d\n",
				__func__, 
				rt->src.b[5], rt->src.b[4], rt->src.b[3],
				rt->src.b[2], rt->src.b[1], rt->src.b[0],
				hdr->dcid, hdr->length, pcb->imtu);

			lockmgr(&pcb->pcb_lock, LK_RELEASE);
			lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
			goto drop;
		}

		/* Check if we have enough space in socket receive queue */
		if (m->m_pkthdr.len > ssb_space(&pcb->so->so_rcv)) {

			/* 
			 * This is really bad. Receive queue on socket does
			 * not have enough space for the packet. We do not 
			 * have any other choice but drop the packet. L2CAP 
			 * does not provide any flow control.
			 */

			NG_BTSOCKET_L2CAP_ERR(
"%s: Not enough space in socket receive queue. Dropping L2CAP data packet, " \
"src bdaddr=%x:%x:%x:%x:%x:%x, dcid=%d, len=%d, space=%ld\n",
				__func__,
				rt->src.b[5], rt->src.b[4], rt->src.b[3],
				rt->src.b[2], rt->src.b[1], rt->src.b[0],
				hdr->dcid, m->m_pkthdr.len,
				ssb_space(&pcb->so->so_rcv));

			lockmgr(&pcb->pcb_lock, LK_RELEASE);
			lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
			goto drop;
		}

		/* Append packet to the socket receive queue and wakeup */
		sbappendrecord(&pcb->so->so_rcv.sb, m);
		m = NULL;

		sorwakeup(pcb->so);

		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
	} else if (hdr->dcid == NG_L2CAP_CLT_CID) {
		/* Broadcast packet: give packet to all sockets  */

		/* Check packet size against connectionless MTU */
		if (hdr->length > NG_L2CAP_MTU_DEFAULT) {
			NG_BTSOCKET_L2CAP_ERR(
"%s: Connectionless L2CAP data packet too big, " \
"src bdaddr=%x:%x:%x:%x:%x:%x, length=%d\n",
				__func__,
				rt->src.b[5], rt->src.b[4], rt->src.b[3],
				rt->src.b[2], rt->src.b[1], rt->src.b[0],
				hdr->length);
			goto drop;
		}

		/* Make sure we can access connectionless header */
		if (m->m_pkthdr.len < sizeof(*clt_hdr)) {
			NG_BTSOCKET_L2CAP_ERR(
"%s: Can not get L2CAP connectionless packet header, " \
"src bdaddr=%x:%x:%x:%x:%x:%x, length=%d\n",
				__func__,
				rt->src.b[5], rt->src.b[4], rt->src.b[3],
				rt->src.b[2], rt->src.b[1], rt->src.b[0],
				hdr->length);
			goto drop;
		}

		if (m->m_len < sizeof(*clt_hdr)) {
			m = m_pullup(m, sizeof(*clt_hdr));
			if (m == NULL)
				goto drop;
		}

		/* Strip connectionless header and deliver packet */
		clt_hdr = mtod(m, ng_l2cap_clt_hdr_t *);
		m_adj(m, sizeof(*clt_hdr));

		NG_BTSOCKET_L2CAP_INFO(
"%s: Got L2CAP connectionless data packet, " \
"src bdaddr=%x:%x:%x:%x:%x:%x, psm=%d, length=%d\n",
			__func__,
			rt->src.b[5], rt->src.b[4], rt->src.b[3],
			rt->src.b[2], rt->src.b[1], rt->src.b[0],
			clt_hdr->psm, hdr->length);

		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

		LIST_FOREACH(pcb, &ng_btsocket_l2cap_sockets, next) {
			struct mbuf	*copy = NULL;

			lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

			if (bcmp(&rt->src, &pcb->src, sizeof(pcb->src)) != 0 ||
			    pcb->psm != clt_hdr->psm || 
			    pcb->state != NG_BTSOCKET_L2CAP_OPEN || 
			    (pcb->so->so_options & SO_BROADCAST) == 0 || 
			    m->m_pkthdr.len > ssb_space(&pcb->so->so_rcv))
				goto next;

			/*
			 * Create a copy of the packet and append it to the 
			 * socket's queue. If m_dup() failed - no big deal
			 * it is a broadcast traffic after all
			 */

			copy = m_dup(m, M_NOWAIT);
			if (copy != NULL) {
				sbappendrecord(&pcb->so->so_rcv.sb, copy);
				sorwakeup(pcb->so);
			}
next:
			lockmgr(&pcb->pcb_lock, LK_RELEASE);
		}

		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
	}
drop:
	NG_FREE_M(m); /* checks for m != NULL */
} /* ng_btsocket_l2cap_data_input */

/*
 * L2CAP sockets default message input routine
 */

static void
ng_btsocket_l2cap_default_msg_input(struct ng_mesg *msg, hook_p hook)
{
	switch (msg->header.cmd) {
	case NGM_L2CAP_NODE_HOOK_INFO: {
		ng_btsocket_l2cap_rtentry_t	*rt = NULL;

		if (hook == NULL || msg->header.arglen != sizeof(bdaddr_t))
			break;

		if (bcmp(msg->data, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0)
			break;

		lockmgr(&ng_btsocket_l2cap_rt_lock, LK_EXCLUSIVE);

		rt = (ng_btsocket_l2cap_rtentry_t *) NG_HOOK_PRIVATE(hook);
		if (rt == NULL) {
			rt = kmalloc(sizeof(*rt), M_NETGRAPH_BTSOCKET_L2CAP,
				     M_WAITOK | M_NULLOK | M_ZERO);
			if (rt == NULL) {
				lockmgr(&ng_btsocket_l2cap_rt_lock, LK_RELEASE);
				break;
			}

			LIST_INSERT_HEAD(&ng_btsocket_l2cap_rt, rt, next);

			NG_HOOK_SET_PRIVATE(hook, rt);
		}

		bcopy(msg->data, &rt->src, sizeof(rt->src));
		rt->hook = hook;

		lockmgr(&ng_btsocket_l2cap_rt_lock, LK_RELEASE);

		NG_BTSOCKET_L2CAP_INFO(
"%s: Updating hook \"%s\", src bdaddr=%x:%x:%x:%x:%x:%x\n",
			__func__, NG_HOOK_NAME(hook), 
			rt->src.b[5], rt->src.b[4], rt->src.b[3], 
			rt->src.b[2], rt->src.b[1], rt->src.b[0]);
		} break;

	default:
		NG_BTSOCKET_L2CAP_WARN(
"%s: Unknown message, cmd=%d\n", __func__, msg->header.cmd);
		break;
	}

	NG_FREE_MSG(msg); /* Checks for msg != NULL */
} /* ng_btsocket_l2cap_default_msg_input */

/*
 * L2CAP sockets L2CA message input routine
 */

static void
ng_btsocket_l2cap_l2ca_msg_input(struct ng_mesg *msg, hook_p hook)
{
	ng_btsocket_l2cap_rtentry_p	rt = NULL;

	if (hook == NULL) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Invalid source hook for L2CA message\n", __func__);
		goto drop;
	}

	rt = (ng_btsocket_l2cap_rtentry_p) NG_HOOK_PRIVATE(hook);
	if (rt == NULL) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not find out source bdaddr for L2CA message\n", __func__);
		goto drop;
	}

	switch (msg->header.cmd) {
	case NGM_L2CAP_L2CA_CON: /* L2CA_Connect response */
		ng_btsocket_l2cap_process_l2ca_con_req_rsp(msg, rt);
		break;

	case NGM_L2CAP_L2CA_CON_RSP: /* L2CA_ConnectRsp response */
		ng_btsocket_l2cap_process_l2ca_con_rsp_rsp(msg, rt);
		break;

	case NGM_L2CAP_L2CA_CON_IND: /* L2CA_Connect indicator */
		ng_btsocket_l2cap_process_l2ca_con_ind(msg, rt);
		break;

	case NGM_L2CAP_L2CA_CFG: /* L2CA_Config response */
		ng_btsocket_l2cap_process_l2ca_cfg_req_rsp(msg, rt);
		break;

	case NGM_L2CAP_L2CA_CFG_RSP: /* L2CA_ConfigRsp response */
		ng_btsocket_l2cap_process_l2ca_cfg_rsp_rsp(msg, rt);
		break;

	case NGM_L2CAP_L2CA_CFG_IND: /* L2CA_Config indicator */
		ng_btsocket_l2cap_process_l2ca_cfg_ind(msg, rt);
		break;

	case NGM_L2CAP_L2CA_DISCON: /* L2CA_Disconnect response */
		ng_btsocket_l2cap_process_l2ca_discon_rsp(msg, rt);
		break;

	case NGM_L2CAP_L2CA_DISCON_IND: /* L2CA_Disconnect indicator */
		ng_btsocket_l2cap_process_l2ca_discon_ind(msg, rt);
		break;

	case NGM_L2CAP_L2CA_WRITE: /* L2CA_Write response */
		ng_btsocket_l2cap_process_l2ca_write_rsp(msg, rt);
		break;

	/* XXX FIXME add other L2CA messages */

	default:
		NG_BTSOCKET_L2CAP_WARN(
"%s: Unknown L2CA message, cmd=%d\n", __func__, msg->header.cmd);
		break;
	}
drop:
	NG_FREE_MSG(msg);
} /* ng_btsocket_l2cap_l2ca_msg_input */

/*
 * L2CAP sockets input routine
 */

static void
ng_btsocket_l2cap_input(void *context, int pending)
{
	item_p	item = NULL;
	hook_p	hook = NULL;

	for (;;) {
		lockmgr(&ng_btsocket_l2cap_queue_lock, LK_EXCLUSIVE);
		NG_BT_ITEMQ_DEQUEUE(&ng_btsocket_l2cap_queue, item);
		lockmgr(&ng_btsocket_l2cap_queue_lock, LK_RELEASE);

		if (item == NULL)
			break;

		NGI_GET_HOOK(item, hook);
		if (hook != NULL && NG_HOOK_NOT_VALID(hook))
			goto drop;

		switch(item->el_flags & NGQF_TYPE) {
		case NGQF_DATA: {
			struct mbuf     *m = NULL;

			NGI_GET_M(item, m);
			ng_btsocket_l2cap_data_input(m, hook);
			} break;

		case NGQF_MESG: {
			struct ng_mesg  *msg = NULL;

			NGI_GET_MSG(item, msg);

			switch (msg->header.cmd) {
			case NGM_L2CAP_L2CA_CON:
			case NGM_L2CAP_L2CA_CON_RSP:
			case NGM_L2CAP_L2CA_CON_IND:
			case NGM_L2CAP_L2CA_CFG:
			case NGM_L2CAP_L2CA_CFG_RSP:
			case NGM_L2CAP_L2CA_CFG_IND: 
			case NGM_L2CAP_L2CA_DISCON:
			case NGM_L2CAP_L2CA_DISCON_IND:
			case NGM_L2CAP_L2CA_WRITE:
			/* XXX FIXME add other L2CA messages */
				ng_btsocket_l2cap_l2ca_msg_input(msg, hook);
				break;

			default:
				ng_btsocket_l2cap_default_msg_input(msg, hook);
				break;
			}
			} break;

		default:
			KASSERT(0,
("%s: invalid item type=%ld\n", __func__, (item->el_flags & NGQF_TYPE)));
			break;
		}
drop:
		if (hook != NULL)
			NG_HOOK_UNREF(hook);

		NG_FREE_ITEM(item);
		ng_unref_item(item, 0);
	}
} /* ng_btsocket_l2cap_input */

/*
 * Route cleanup task. Gets scheduled when hook is disconnected. Here we 
 * will find all sockets that use "invalid" hook and disconnect them.
 */

static void
ng_btsocket_l2cap_rtclean(void *context, int pending)
{
	ng_btsocket_l2cap_pcb_p		pcb = NULL, pcb_next = NULL;
	ng_btsocket_l2cap_rtentry_p	rt = NULL;

	lockmgr(&ng_btsocket_l2cap_rt_lock, LK_EXCLUSIVE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	/*
	 * First disconnect all sockets that use "invalid" hook
	 */

	for (pcb = LIST_FIRST(&ng_btsocket_l2cap_sockets); pcb != NULL; ) {
		lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);
		pcb_next = LIST_NEXT(pcb, next);

		if (pcb->rt != NULL &&
		    pcb->rt->hook != NULL && NG_HOOK_NOT_VALID(pcb->rt->hook)) {
			if (pcb->flags & NG_BTSOCKET_L2CAP_TIMO)
				ng_btsocket_l2cap_untimeout(pcb);

			pcb->so->so_error = ENETDOWN;
			pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
			soisdisconnected(pcb->so);

			pcb->token = 0;
			pcb->cid = 0;
			pcb->rt = NULL;
		}

		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		pcb = pcb_next;
	}

	/*
	 * Now cleanup routing table
	 */

	for (rt = LIST_FIRST(&ng_btsocket_l2cap_rt); rt != NULL; ) {
		ng_btsocket_l2cap_rtentry_p	rt_next = LIST_NEXT(rt, next);

		if (rt->hook != NULL && NG_HOOK_NOT_VALID(rt->hook)) {
			LIST_REMOVE(rt, next);

			NG_HOOK_SET_PRIVATE(rt->hook, NULL);
			NG_HOOK_UNREF(rt->hook); /* Remove extra reference */

			bzero(rt, sizeof(*rt));
			kfree(rt, M_NETGRAPH_BTSOCKET_L2CAP);
		}

		rt = rt_next;
	}

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_rt_lock, LK_RELEASE);
} /* ng_btsocket_l2cap_rtclean */

/*
 * Initialize everything
 */

void
ng_btsocket_l2cap_init(void)
{
	int	error = 0;

	ng_btsocket_l2cap_node = NULL;
	ng_btsocket_l2cap_debug_level = NG_BTSOCKET_WARN_LEVEL;

	/* Register Netgraph node type */
	error = ng_newtype(&typestruct);
	if (error != 0) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not register Netgraph node type, error=%d\n", __func__, error);

                return;
	}

	/* Create Netgrapg node */
	error = ng_make_node_common(&typestruct, &ng_btsocket_l2cap_node);
	if (error != 0) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not create Netgraph node, error=%d\n", __func__, error);

		ng_btsocket_l2cap_node = NULL;

		return;
	}

	error = ng_name_node(ng_btsocket_l2cap_node,
				NG_BTSOCKET_L2CAP_NODE_TYPE);
	if (error != 0) {
		NG_BTSOCKET_L2CAP_ALERT(
"%s: Could not name Netgraph node, error=%d\n", __func__, error);

		NG_NODE_UNREF(ng_btsocket_l2cap_node);
		ng_btsocket_l2cap_node = NULL;

		return;
	}

	/* Create input queue */
	NG_BT_ITEMQ_INIT(&ng_btsocket_l2cap_queue, ifqmaxlen);
	lockinit(&ng_btsocket_l2cap_queue_lock,
		"btsocks_l2cap_queue_lock", 0, 0);
	TASK_INIT(&ng_btsocket_l2cap_queue_task, 0,
		ng_btsocket_l2cap_input, NULL);

	/* Create list of sockets */
	LIST_INIT(&ng_btsocket_l2cap_sockets);
	lockinit(&ng_btsocket_l2cap_sockets_lock,
		"btsocks_l2cap_sockets_lock", 0, 0);

	/* Routing table */
	LIST_INIT(&ng_btsocket_l2cap_rt);
	lockinit(&ng_btsocket_l2cap_rt_lock,
		"btsocks_l2cap_rt_lock", 0, 0);
	TASK_INIT(&ng_btsocket_l2cap_rt_task, 0,
		ng_btsocket_l2cap_rtclean, NULL);
} /* ng_btsocket_l2cap_init */

/*
 * Abort connection on socket
 */

void
ng_btsocket_l2cap_abort(netmsg_t msg)
{
	struct socket		*so = msg->abort.base.nm_so;

	so->so_error = ECONNABORTED;

	ng_btsocket_l2cap_disconnect(msg);
} /* ng_btsocket_l2cap_abort */

#if 0 /* XXX */
void
ng_btsocket_l2cap_close(struct socket *so)
{

	(void)ng_btsocket_l2cap_disconnect(so);
} /* ng_btsocket_l2cap_close */
#endif

/*
 * Accept connection on socket. Nothing to do here, socket must be connected
 * and ready, so just return peer address and be done with it.
 */

void
ng_btsocket_l2cap_accept(netmsg_t msg)
{
	int			error = 0;

	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}

	ng_btsocket_l2cap_peeraddr(msg);
	return;

out:
	lwkt_replymsg(&msg->accept.base.lmsg, error);
} /* ng_btsocket_l2cap_accept */

/*
 * Create and attach new socket
 */

void
ng_btsocket_l2cap_attach(netmsg_t msg)
{
	struct socket		*so = msg->attach.base.nm_so;
	int			 proto = msg->attach.nm_proto;
	static u_int32_t	 token = 0;
	ng_btsocket_l2cap_pcb_p	 pcb = so2l2cap_pcb(so);
	int			 error = 0;

	/* Check socket and protocol */
	if (ng_btsocket_l2cap_node == NULL) {
		error = EPROTONOSUPPORT;
		goto out;
	}
	if (so->so_type != SOCK_SEQPACKET) {
		error = ESOCKTNOSUPPORT;
		goto out;
	}

#if 0 /* XXX sonewconn() calls "pru_attach" with proto == 0 */
	if (proto != 0) 
		if (proto != BLUETOOTH_PROTO_L2CAP) {
			error = EPROTONOSUPPORT;
			goto out;
		}
#endif /* XXX */

	if (pcb != NULL) {
		error = EISCONN;
		goto out;
	}

	/* Reserve send and receive space if it is not reserved yet */
	if ((so->so_snd.ssb_hiwat == 0) || (so->so_rcv.ssb_hiwat == 0)) {
		error = soreserve(so, NG_BTSOCKET_L2CAP_SENDSPACE,
					NG_BTSOCKET_L2CAP_RECVSPACE, NULL);
		if (error != 0)
			goto out;
	}

	/* Allocate the PCB */
        pcb = kmalloc(sizeof(*pcb), M_NETGRAPH_BTSOCKET_L2CAP,
		      M_WAITOK | M_NULLOK | M_ZERO);
        if (pcb == NULL) {
		error = ENOMEM;
		goto out;
	}

	/* Link the PCB and the socket */
	so->so_pcb = (caddr_t) pcb;
	pcb->so = so;
	pcb->state = NG_BTSOCKET_L2CAP_CLOSED;

	/* Initialize PCB */
	pcb->imtu = pcb->omtu = NG_L2CAP_MTU_DEFAULT;

	/* Default flow */
	pcb->iflow.flags = 0x0;
	pcb->iflow.service_type = NG_HCI_SERVICE_TYPE_BEST_EFFORT;
	pcb->iflow.token_rate = 0xffffffff; /* maximum */
	pcb->iflow.token_bucket_size = 0xffffffff; /* maximum */
	pcb->iflow.peak_bandwidth = 0x00000000; /* maximum */
	pcb->iflow.latency = 0xffffffff; /* don't care */
	pcb->iflow.delay_variation = 0xffffffff; /* don't care */

	bcopy(&pcb->iflow, &pcb->oflow, sizeof(pcb->oflow));

	pcb->flush_timo = NG_L2CAP_FLUSH_TIMO_DEFAULT;
	pcb->link_timo = NG_L2CAP_LINK_TIMO_DEFAULT;

	callout_init_mp(&pcb->timo);

	/*
	 * XXX Mark PCB mutex as DUPOK to prevent "duplicated lock of
	 * the same type" message. When accepting new L2CAP connection 
	 * ng_btsocket_l2cap_process_l2ca_con_ind() holds both PCB mutexes 
	 * for "old" (accepting) PCB and "new" (created) PCB.
	 */
		
	lockinit(&pcb->pcb_lock, "btsocks_l2cap_pcb_lock", 0, LK_CANRECURSE);

        /*
	 * Add the PCB to the list
	 * 
	 * XXX FIXME VERY IMPORTANT!
	 *
	 * This is totally FUBAR. We could get here in two cases:
	 *
	 * 1) When user calls socket()
	 * 2) When we need to accept new incomming connection and call 
	 *    sonewconn()
	 *
	 * In the first case we must acquire ng_btsocket_l2cap_sockets_mtx.
	 * In the second case we hold ng_btsocket_l2cap_sockets_mtx already.
	 * So we now need to distinguish between these cases. From reading
	 * /sys/kern/uipc_socket.c we can find out that sonewconn() calls
	 * pru_attach with proto == 0 and td == NULL. For now use this fact
	 * to figure out if we were called from socket() or from sonewconn().
	 */

	if (proto != 0)
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);
	else
		KKASSERT(lockowned(&ng_btsocket_l2cap_sockets_lock) != 0);
	
	/* Set PCB token. Use ng_btsocket_l2cap_sockets_mtx for protection */
	if (++ token == 0)
		token ++;

	pcb->token = token;

	LIST_INSERT_HEAD(&ng_btsocket_l2cap_sockets, pcb, next);

	if (proto != 0)
		lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
} /* ng_btsocket_l2cap_attach */

/*
 * Bind socket
 */

void
ng_btsocket_l2cap_bind(netmsg_t msg)
{
	struct socket		*so = msg->bind.base.nm_so;
	struct sockaddr		*nam = msg->bind.nm_nam;
	ng_btsocket_l2cap_pcb_t	*pcb = NULL;
	struct sockaddr_l2cap	*sa = (struct sockaddr_l2cap *) nam;
	int			 psm, error = 0;

	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}

	/* Verify address */
	if (sa == NULL) {
		error = EINVAL;
		goto out;
	}
	if (sa->l2cap_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
		goto out;
	}
	if (sa->l2cap_len != sizeof(*sa)) {
		error = EINVAL;
		goto out;
	}

	psm = le16toh(sa->l2cap_psm);

	/* 
	 * Check if other socket has this address already (look for exact
	 * match PSM and bdaddr) and assign socket address if it's available.
	 *
	 * Note: socket can be bound to ANY PSM (zero) thus allowing several
	 * channels with the same PSM between the same pair of BD_ADDR'es.
	 */

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);

	LIST_FOREACH(pcb, &ng_btsocket_l2cap_sockets, next)
		if (psm != 0 && psm == pcb->psm &&
		    bcmp(&pcb->src, &sa->l2cap_bdaddr, sizeof(bdaddr_t)) == 0)
			break;

	if (pcb == NULL) {
		/* Set socket address */
		pcb = so2l2cap_pcb(so);
		if (pcb != NULL) {
			bcopy(&sa->l2cap_bdaddr, &pcb->src, sizeof(pcb->src));
			pcb->psm = psm;
		} else
			error = EINVAL;
	} else
		error = EADDRINUSE;

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->bind.base.lmsg, error);
} /* ng_btsocket_l2cap_bind */

/*
 * Connect socket
 */

void
ng_btsocket_l2cap_connect(netmsg_t msg)
{
	struct socket			*so = msg->connect.base.nm_so;
	struct sockaddr			*nam = msg->connect.nm_nam;
	ng_btsocket_l2cap_pcb_t		*pcb = so2l2cap_pcb(so);
	struct sockaddr_l2cap		*sa = (struct sockaddr_l2cap *) nam;
	ng_btsocket_l2cap_rtentry_t	*rt = NULL;
	int				 have_src, error = 0;

	/* Check socket */
	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}
	if (pcb->state == NG_BTSOCKET_L2CAP_CONNECTING) {
		error = EINPROGRESS;
		goto out;
	}

	/* Verify address */
	if (sa == NULL) {
		error = EINVAL;
		goto out;
	}
	if (sa->l2cap_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
		goto out;
	}
	if (sa->l2cap_len != sizeof(*sa)) {
		error = EINVAL;
		goto out;
	}
	if (sa->l2cap_psm == 0 ||
	    bcmp(&sa->l2cap_bdaddr, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0) {
		error = EDESTADDRREQ;
		goto out;
	}
	if (pcb->psm != 0 && pcb->psm != le16toh(sa->l2cap_psm)) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Routing. Socket should be bound to some source address. The source
	 * address can be ANY. Destination address must be set and it must not
	 * be ANY. If source address is ANY then find first rtentry that has
	 * src != dst.
	 */

	lockmgr(&ng_btsocket_l2cap_rt_lock, LK_EXCLUSIVE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);
	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	/* Send destination address and PSM */
	bcopy(&sa->l2cap_bdaddr, &pcb->dst, sizeof(pcb->dst));
	pcb->psm = le16toh(sa->l2cap_psm);

	pcb->rt = NULL;
	have_src = bcmp(&pcb->src, NG_HCI_BDADDR_ANY, sizeof(pcb->src));

	LIST_FOREACH(rt, &ng_btsocket_l2cap_rt, next) {
		if (rt->hook == NULL || NG_HOOK_NOT_VALID(rt->hook))
			continue;

		/* Match src and dst */
		if (have_src) {
			if (bcmp(&pcb->src, &rt->src, sizeof(rt->src)) == 0)
				break;
		} else {
			if (bcmp(&pcb->dst, &rt->src, sizeof(rt->src)) != 0)
				break;
		}
	}

	if (rt != NULL) {
		pcb->rt = rt;

		if (!have_src)
			bcopy(&rt->src, &pcb->src, sizeof(pcb->src));
	} else
		error = EHOSTUNREACH;

	/*
	 * Send L2CA_Connect request 
	 */

	if (error == 0) {	
		error = ng_btsocket_l2cap_send_l2ca_con_req(pcb);
		if (error == 0) {
			pcb->flags |= NG_BTSOCKET_L2CAP_CLIENT;
			pcb->state = NG_BTSOCKET_L2CAP_CONNECTING;
			soisconnecting(pcb->so);

			ng_btsocket_l2cap_timeout(pcb);
		}
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_rt_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->connect.base.lmsg, error);
} /* ng_btsocket_l2cap_connect */

/*
 * Process ioctl's calls on socket
 */

void
ng_btsocket_l2cap_control(netmsg_t msg)
{
	lwkt_replymsg(&msg->control.base.lmsg, EINVAL);
} /* ng_btsocket_l2cap_control */

/*
 * Process getsockopt/setsockopt system calls
 */

void
ng_btsocket_l2cap_ctloutput(netmsg_t msg)
{
	struct socket		*so = msg->ctloutput.base.nm_so;
	struct sockopt		*sopt = msg->ctloutput.nm_sopt;
	ng_btsocket_l2cap_pcb_p	 pcb = so2l2cap_pcb(so);
	int			 error = 0;
	ng_l2cap_cfg_opt_val_t	 v;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sopt->sopt_level != SOL_L2CAP)
		goto out;

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case SO_L2CAP_IMTU: /* get incoming MTU */
			error = sooptcopyout(sopt, &pcb->imtu,
						sizeof(pcb->imtu));
			break;

		case SO_L2CAP_OMTU: /* get outgoing (peer incoming) MTU */
			error = sooptcopyout(sopt, &pcb->omtu,
						sizeof(pcb->omtu));
			break;

		case SO_L2CAP_IFLOW: /* get incoming flow spec. */
			error = sooptcopyout(sopt, &pcb->iflow,
						sizeof(pcb->iflow));
			break;

		case SO_L2CAP_OFLOW: /* get outgoing flow spec. */
			error = sooptcopyout(sopt, &pcb->oflow,
						sizeof(pcb->oflow));
			break;

		case SO_L2CAP_FLUSH: /* get flush timeout */
			error = sooptcopyout(sopt, &pcb->flush_timo,
						sizeof(pcb->flush_timo));
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case SOPT_SET:
		/*
		 * XXX
		 * We do not allow to change these parameters while socket is 
		 * connected or we are in the process of creating a connection.
		 * May be this should indicate re-configuration of the open 
		 * channel?
		 */

		if (pcb->state != NG_BTSOCKET_L2CAP_CLOSED) {
			error = EACCES;
			break;
		}

		switch (sopt->sopt_name) {
		case SO_L2CAP_IMTU: /* set incoming MTU */
			error = sooptcopyin(sopt, &v, sizeof(v), sizeof(v.mtu));
			if (error == 0)
				pcb->imtu = v.mtu;
			break;

		case SO_L2CAP_OFLOW: /* set outgoing flow spec. */
			error = sooptcopyin(sopt, &v, sizeof(v),sizeof(v.flow));
			if (error == 0)
				bcopy(&v.flow, &pcb->oflow, sizeof(pcb->oflow));
			break;

		case SO_L2CAP_FLUSH: /* set flush timeout */
			error = sooptcopyin(sopt, &v, sizeof(v),
						sizeof(v.flush_timo));
			if (error == 0)
				pcb->flush_timo = v.flush_timo;
			break;

		default:
			error = ENOPROTOOPT;
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
} /* ng_btsocket_l2cap_ctloutput */

/*
 * Detach and destroy socket
 */

void
ng_btsocket_l2cap_detach(netmsg_t msg)
{
	struct socket		*so = msg->detach.base.nm_so;
	ng_btsocket_l2cap_pcb_p	 pcb = so2l2cap_pcb(so);
	int			 error = 0;

	KASSERT(pcb != NULL, ("ng_btsocket_l2cap_detach: pcb == NULL"));

	if (ng_btsocket_l2cap_node == NULL) 
		goto out;

	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_EXCLUSIVE);
	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	/* XXX what to do with pending request? */
	if (pcb->flags & NG_BTSOCKET_L2CAP_TIMO)
		ng_btsocket_l2cap_untimeout(pcb);

	if (pcb->state != NG_BTSOCKET_L2CAP_CLOSED &&
	    pcb->state != NG_BTSOCKET_L2CAP_DISCONNECTING)
		/* Send disconnect request with "zero" token */
		ng_btsocket_l2cap_send_l2ca_discon_req(0, pcb);

	pcb->state = NG_BTSOCKET_L2CAP_CLOSED;

	LIST_REMOVE(pcb, next);

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
	lockmgr(&ng_btsocket_l2cap_sockets_lock, LK_RELEASE);

	lockuninit(&pcb->pcb_lock);
	bzero(pcb, sizeof(*pcb));
	kfree(pcb, M_NETGRAPH_BTSOCKET_L2CAP);

	soisdisconnected(so);
	so->so_pcb = NULL;

out:
	lwkt_replymsg(&msg->detach.base.lmsg, error);
} /* ng_btsocket_l2cap_detach */

/*
 * Disconnect socket
 */

void
ng_btsocket_l2cap_disconnect(netmsg_t msg)
{
	struct socket		*so = msg->disconnect.base.nm_so;
	ng_btsocket_l2cap_pcb_p	 pcb = so2l2cap_pcb(so);
	int			 error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	if (pcb->state == NG_BTSOCKET_L2CAP_DISCONNECTING) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = EINPROGRESS;
		goto out;
	}

	if (pcb->state != NG_BTSOCKET_L2CAP_CLOSED) {
		/* XXX FIXME what to do with pending request? */
		if (pcb->flags & NG_BTSOCKET_L2CAP_TIMO)
			ng_btsocket_l2cap_untimeout(pcb);

		error = ng_btsocket_l2cap_send_l2ca_discon_req(pcb->token, pcb);
		if (error == 0) {
			pcb->state = NG_BTSOCKET_L2CAP_DISCONNECTING;
			soisdisconnecting(so);

			ng_btsocket_l2cap_timeout(pcb);
		}

		/* XXX FIXME what to do if error != 0 */
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);

out:
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
} /* ng_btsocket_l2cap_disconnect */

/*
 * Listen on socket
 */

void
ng_btsocket_l2cap_listen(netmsg_t msg)
{
	struct socket		*so = msg->listen.base.nm_so;
	struct thread		*td = msg->listen.nm_td;
	int			 backlog = msg->listen.nm_flags; /* XXX */
	ng_btsocket_l2cap_pcb_p	 pcb = so2l2cap_pcb(so);
	int			 error = 0;

	if (so->so_state &
	    (SS_ISCONNECTED | SS_ISCONNECTING | SS_ISDISCONNECTING)) {
		error = EINVAL;
		goto out;
	}
	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}
	if (pcb->psm == 0) {
		error = EADDRNOTAVAIL;
		goto out;
	}
	solisten(so, backlog, td);

out:
	lwkt_replymsg(&msg->listen.base.lmsg, error);
} /* ng_btsocket_listen */

/*
 * Get peer address
 */

void
ng_btsocket_l2cap_peeraddr(netmsg_t msg)
{
	struct socket		 *so = msg->peeraddr.base.nm_so;
	struct sockaddr		**nam = msg->peeraddr.nm_nam;
	ng_btsocket_l2cap_pcb_p	  pcb = so2l2cap_pcb(so);
	struct sockaddr_l2cap	  sa;
	int			  error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}

	bcopy(&pcb->dst, &sa.l2cap_bdaddr, sizeof(sa.l2cap_bdaddr));
	sa.l2cap_psm = htole16(pcb->psm);
	sa.l2cap_len = sizeof(sa);
	sa.l2cap_family = AF_BLUETOOTH;

	*nam = dup_sockaddr((struct sockaddr *) &sa);

	if (*nam == NULL)
		error = ENOMEM;

out:
	lwkt_replymsg(&msg->peeraddr.base.lmsg, error);
} /* ng_btsocket_l2cap_peeraddr */

/*
 * Send data to socket
 */

void
ng_btsocket_l2cap_send(netmsg_t msg)
{
	struct socket		*so = msg->send.base.nm_so;
	struct mbuf		*m = msg->send.nm_m;
	struct mbuf		*control = msg->send.nm_control;
	ng_btsocket_l2cap_pcb_t	*pcb = so2l2cap_pcb(so);
	int			 error = 0;

	if (ng_btsocket_l2cap_node == NULL) {
		error = ENETDOWN;
		goto drop;
	}

	/* Check socket and input */
	if (pcb == NULL || m == NULL || control != NULL) {
		error = EINVAL;
		goto drop;
	}

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	/* Make sure socket is connected */
	if (pcb->state != NG_BTSOCKET_L2CAP_OPEN) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = ENOTCONN;
		goto drop;
	}

	/* Check route */
	if (pcb->rt == NULL ||
	    pcb->rt->hook == NULL || NG_HOOK_NOT_VALID(pcb->rt->hook)) {
		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = ENETDOWN;
		goto drop;
	}

	/* Check packet size agains outgoing (peer's incoming) MTU) */
	if (m->m_pkthdr.len > pcb->omtu) {
		NG_BTSOCKET_L2CAP_ERR(
"%s: Packet too big, len=%d, omtu=%d\n", __func__, m->m_pkthdr.len, pcb->omtu);

		lockmgr(&pcb->pcb_lock, LK_RELEASE);
		error = EMSGSIZE;
		goto drop;
	}

	/*
	 * First put packet on socket send queue. Then check if we have
	 * pending timeout. If we do not have timeout then we must send
	 * packet and schedule timeout. Otherwise do nothing and wait for
	 * L2CA_WRITE_RSP.
	 */

	sbappendrecord(&pcb->so->so_snd.sb, m);
	m = NULL;

	if (!(pcb->flags & NG_BTSOCKET_L2CAP_TIMO)) {
		error = ng_btsocket_l2cap_send2(pcb);
		if (error == 0)
			ng_btsocket_l2cap_timeout(pcb);
		else
			sbdroprecord(&pcb->so->so_snd.sb); /* XXX */
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
drop:
	NG_FREE_M(m); /* checks for != NULL */
	NG_FREE_M(control);

	lwkt_replymsg(&msg->send.base.lmsg, error);
} /* ng_btsocket_l2cap_send */

/*
 * Send first packet in the socket queue to the L2CAP layer
 */

static int
ng_btsocket_l2cap_send2(ng_btsocket_l2cap_pcb_p pcb)
{
	struct	mbuf		*m = NULL;
	ng_l2cap_l2ca_hdr_t	*hdr = NULL;
	int			 error = 0;
	
	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->so->so_snd.sb.sb_cc == 0)
		return (EINVAL); /* XXX */

	m = m_dup(pcb->so->so_snd.sb.sb_mb, M_NOWAIT);
	if (m == NULL)
		return (ENOBUFS);

	/* Create L2CA packet header */
	M_PREPEND(m, sizeof(*hdr), M_NOWAIT);
	if (m != NULL)
		if (m->m_len < sizeof(*hdr))
			m = m_pullup(m, sizeof(*hdr));

	if (m == NULL) {
		NG_BTSOCKET_L2CAP_ERR(
"%s: Failed to create L2CA packet header\n", __func__);

		return (ENOBUFS);
	}

	hdr = mtod(m, ng_l2cap_l2ca_hdr_t *);
	hdr->token = pcb->token;
	hdr->length = m->m_pkthdr.len - sizeof(*hdr);
	hdr->lcid = pcb->cid;

	NG_BTSOCKET_L2CAP_INFO(
"%s: Sending packet: len=%d, length=%d, lcid=%d, token=%d, state=%d\n",
		__func__, m->m_pkthdr.len, hdr->length, hdr->lcid, 
		hdr->token, pcb->state);

	/*
	 * If we got here than we have successfuly creates new L2CAP 
	 * data packet and now we can send it to the L2CAP layer
	 */

	NG_SEND_DATA_ONLY(error, pcb->rt->hook, m);

	return (error);
} /* ng_btsocket_l2cap_send2 */

/*
 * Get socket address
 */

void
ng_btsocket_l2cap_sockaddr(netmsg_t msg)
{
	struct socket		 *so = msg->sockaddr.base.nm_so;
	struct sockaddr		**nam = msg->sockaddr.nm_nam;
	ng_btsocket_l2cap_pcb_p	  pcb = so2l2cap_pcb(so);
	struct sockaddr_l2cap	  sa;
	int			  error = 0;

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}
	if (ng_btsocket_l2cap_node == NULL) {
		error = EINVAL;
		goto out;
	}

	bcopy(&pcb->src, &sa.l2cap_bdaddr, sizeof(sa.l2cap_bdaddr));
	sa.l2cap_psm = htole16(pcb->psm);
	sa.l2cap_len = sizeof(sa);
	sa.l2cap_family = AF_BLUETOOTH;

	*nam = dup_sockaddr((struct sockaddr *) &sa);

	if (*nam == NULL)
		error = ENOMEM;

out:
	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
} /* ng_btsocket_l2cap_sockaddr */

/*****************************************************************************
 *****************************************************************************
 **                              Misc. functions
 *****************************************************************************
 *****************************************************************************/

/*
 * Look for the socket that listens on given PSM and bdaddr. Returns exact or
 * close match (if any). Caller must hold ng_btsocket_l2cap_sockets_mtx.
 */

static ng_btsocket_l2cap_pcb_p
ng_btsocket_l2cap_pcb_by_addr(bdaddr_p bdaddr, int psm)
{
	ng_btsocket_l2cap_pcb_p	p = NULL, p1 = NULL;

	KKASSERT(lockowned(&ng_btsocket_l2cap_sockets_lock) != 0);

	LIST_FOREACH(p, &ng_btsocket_l2cap_sockets, next) {
		if (p->so == NULL || !(p->so->so_options & SO_ACCEPTCONN) || 
		    p->psm != psm) 
			continue;

		if (bcmp(&p->src, bdaddr, sizeof(p->src)) == 0)
			break;

		if (bcmp(&p->src, NG_HCI_BDADDR_ANY, sizeof(p->src)) == 0)
			p1 = p;
	}

	return ((p != NULL)? p : p1);
} /* ng_btsocket_l2cap_pcb_by_addr */

/*
 * Look for the socket that has given token.
 * Caller must hold ng_btsocket_l2cap_sockets_mtx.
 */

static ng_btsocket_l2cap_pcb_p
ng_btsocket_l2cap_pcb_by_token(u_int32_t token)
{
	ng_btsocket_l2cap_pcb_p	p = NULL;

	if (token == 0)
		return (NULL);

	KKASSERT(lockowned(&ng_btsocket_l2cap_sockets_lock) != 0);

	LIST_FOREACH(p, &ng_btsocket_l2cap_sockets, next)
		if (p->token == token)
			break;

	return (p);
} /* ng_btsocket_l2cap_pcb_by_token */

/*
 * Look for the socket that assigned to given source address and channel ID.
 * Caller must hold ng_btsocket_l2cap_sockets_mtx
 */

static ng_btsocket_l2cap_pcb_p
ng_btsocket_l2cap_pcb_by_cid(bdaddr_p src, int cid)
{
	ng_btsocket_l2cap_pcb_p	p = NULL;

	KKASSERT(lockowned(&ng_btsocket_l2cap_sockets_lock) != 0);

	LIST_FOREACH(p, &ng_btsocket_l2cap_sockets, next)
		if (p->cid == cid && bcmp(src, &p->src, sizeof(p->src)) == 0)
			break;

	return (p);
} /* ng_btsocket_l2cap_pcb_by_cid */

/*
 * Set timeout on socket
 */

static void
ng_btsocket_l2cap_timeout(ng_btsocket_l2cap_pcb_p pcb)
{
	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (!(pcb->flags & NG_BTSOCKET_L2CAP_TIMO)) {
		pcb->flags |= NG_BTSOCKET_L2CAP_TIMO;
		callout_reset(&pcb->timo, bluetooth_l2cap_ertx_timeout(),
		    ng_btsocket_l2cap_process_timeout, pcb);
	} else
		KASSERT(0,
("%s: Duplicated socket timeout?!\n", __func__));
} /* ng_btsocket_l2cap_timeout */

/*
 * Unset timeout on socket
 */

static void
ng_btsocket_l2cap_untimeout(ng_btsocket_l2cap_pcb_p pcb)
{
	KKASSERT(lockowned(&pcb->pcb_lock) != 0);

	if (pcb->flags & NG_BTSOCKET_L2CAP_TIMO) {
		callout_stop(&pcb->timo);
		pcb->flags &= ~NG_BTSOCKET_L2CAP_TIMO;
	} else
		KASSERT(0,
("%s: No socket timeout?!\n", __func__));
} /* ng_btsocket_l2cap_untimeout */

/*
 * Process timeout on socket
 */

static void
ng_btsocket_l2cap_process_timeout(void *xpcb)
{
	ng_btsocket_l2cap_pcb_p	pcb = (ng_btsocket_l2cap_pcb_p) xpcb;

	lockmgr(&pcb->pcb_lock, LK_EXCLUSIVE);

	pcb->flags &= ~NG_BTSOCKET_L2CAP_TIMO;
	pcb->so->so_error = ETIMEDOUT;

	switch (pcb->state) {
	case NG_BTSOCKET_L2CAP_CONNECTING:
	case NG_BTSOCKET_L2CAP_CONFIGURING:
		/* Send disconnect request with "zero" token */
		if (pcb->cid != 0)
			ng_btsocket_l2cap_send_l2ca_discon_req(0, pcb);

		/* ... and close the socket */
		pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
		soisdisconnected(pcb->so);
		break;

	case NG_BTSOCKET_L2CAP_OPEN:
		/* Send timeout - drop packet and wakeup sender */
		sbdroprecord(&pcb->so->so_snd.sb);
		sowwakeup(pcb->so);
		break;

	case NG_BTSOCKET_L2CAP_DISCONNECTING:
		/* Disconnect timeout - disconnect the socket anyway */
		pcb->state = NG_BTSOCKET_L2CAP_CLOSED;
		soisdisconnected(pcb->so);
		break;

	default:
		NG_BTSOCKET_L2CAP_ERR(
"%s: Invalid socket state=%d\n", __func__, pcb->state);
		break;
	}

	lockmgr(&pcb->pcb_lock, LK_RELEASE);
} /* ng_btsocket_l2cap_process_timeout */

/*
 * Translate HCI/L2CAP error code into "errno" code
 * XXX Note: Some L2CAP and HCI error codes have the same value, but 
 *     different meaning
 */

static int
ng_btsocket_l2cap_result2errno(int result)
{
	switch (result) {
	case 0x00: /* No error */ 
		return (0);

	case 0x01: /* Unknown HCI command */
		return (ENODEV);

	case 0x02: /* No connection */
		return (ENOTCONN);

	case 0x03: /* Hardware failure */
		return (EIO);

	case 0x04: /* Page timeout */
		return (EHOSTDOWN);

	case 0x05: /* Authentication failure */
	case 0x06: /* Key missing */
	case 0x18: /* Pairing not allowed */
	case 0x21: /* Role change not allowed */
	case 0x24: /* LMP PSU not allowed */
	case 0x25: /* Encryption mode not acceptable */
	case 0x26: /* Unit key used */
		return (EACCES);

	case 0x07: /* Memory full */
		return (ENOMEM);

	case 0x08:   /* Connection timeout */
	case 0x10:   /* Host timeout */
	case 0x22:   /* LMP response timeout */
	case 0xee:   /* HCI timeout */
	case 0xeeee: /* L2CAP timeout */
		return (ETIMEDOUT);

	case 0x09: /* Max number of connections */
	case 0x0a: /* Max number of SCO connections to a unit */
		return (EMLINK);

	case 0x0b: /* ACL connection already exists */
		return (EEXIST);

	case 0x0c: /* Command disallowed */
		return (EBUSY);

	case 0x0d: /* Host rejected due to limited resources */
	case 0x0e: /* Host rejected due to securiity reasons */
	case 0x0f: /* Host rejected due to remote unit is a personal unit */
	case 0x1b: /* SCO offset rejected */
	case 0x1c: /* SCO interval rejected */
	case 0x1d: /* SCO air mode rejected */
		return (ECONNREFUSED);

	case 0x11: /* Unsupported feature or parameter value */
	case 0x19: /* Unknown LMP PDU */
	case 0x1a: /* Unsupported remote feature */
	case 0x20: /* Unsupported LMP parameter value */
	case 0x27: /* QoS is not supported */
	case 0x29: /* Paring with unit key not supported */
		return (EOPNOTSUPP);

	case 0x12: /* Invalid HCI command parameter */
	case 0x1e: /* Invalid LMP parameters */
		return (EINVAL);

	case 0x13: /* Other end terminated connection: User ended connection */
	case 0x14: /* Other end terminated connection: Low resources */
	case 0x15: /* Other end terminated connection: About to power off */
		return (ECONNRESET);

	case 0x16: /* Connection terminated by local host */
		return (ECONNABORTED);

#if 0 /* XXX not yet */
	case 0x17: /* Repeated attempts */
	case 0x1f: /* Unspecified error */
	case 0x23: /* LMP error transaction collision */
	case 0x28: /* Instant passed */
#endif
	}

	return (ENOSYS);
} /* ng_btsocket_l2cap_result2errno */

