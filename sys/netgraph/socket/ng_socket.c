
/*
 * ng_socket.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD: src/sys/netgraph/ng_socket.c,v 1.11.2.6 2002/07/02 22:17:18 archie Exp $
 * $Whistle: ng_socket.c,v 1.28 1999/11/01 09:24:52 julian Exp $
 */

/*
 * Netgraph socket nodes
 *
 * There are two types of netgraph sockets, control and data.
 * Control sockets have a netgraph node, but data sockets are
 * parasitic on control sockets, and have no node of their own.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/domain.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/filedesc.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#ifdef NOTYET
#include <sys/vnode.h>
#endif

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/msgport2.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include "ng_socketvar.h"
#include "ng_socket.h"

/*
 * It's Ascii-art time!
 *   +-------------+   +-------------+
 *   |socket  (ctl)|   |socket (data)|
 *   +-------------+   +-------------+
 *          ^                 ^
 *          |                 |
 *          v                 v
 *    +-----------+     +-----------+
 *    |pcb   (ctl)|     |pcb  (data)|
 *    +-----------+     +-----------+
 *          ^                 ^
 *          |                 |
 *          v                 v
 *      +--------------------------+
 *      |   Socket type private    |
 *      |       data               |
 *      +--------------------------+
 *                   ^
 *                   |
 *                   v
 *           +----------------+
 *           | struct ng_node |
 *           +----------------+
 */

/* Netgraph node methods */
static ng_constructor_t	ngs_constructor;
static ng_rcvmsg_t	ngs_rcvmsg;
static ng_shutdown_t	ngs_rmnode;
static ng_newhook_t	ngs_newhook;
static ng_rcvdata_t	ngs_rcvdata;
static ng_disconnect_t	ngs_disconnect;

/* Internal methods */
static int	ng_attach_data(struct socket *so);
static int	ng_attach_cntl(struct socket *so);
static int	ng_attach_common(struct socket *so, int type);
static void	ng_detach_common(struct ngpcb *pcbp, int type);
/*static int	ng_internalize(struct mbuf *m, struct thread *td); */

static int	ng_connect_data(struct sockaddr *nam, struct ngpcb *pcbp);
static int	ng_connect_cntl(struct sockaddr *nam, struct ngpcb *pcbp);
static int	ng_bind(struct sockaddr *nam, struct ngpcb *pcbp);

static int	ngs_mod_event(module_t mod, int event, void *data);
static int	ship_msg(struct ngpcb *pcbp, struct ng_mesg *msg,
			struct sockaddr_ng *addr);

/* Netgraph type descriptor */
static struct ng_type typestruct = {
	NG_VERSION,
	NG_SOCKET_NODE_TYPE,
	ngs_mod_event,
	ngs_constructor,
	ngs_rcvmsg,
	ngs_rmnode,
	ngs_newhook,
	NULL,
	NULL,
	ngs_rcvdata,
	ngs_rcvdata,
	ngs_disconnect,
	NULL
};
NETGRAPH_INIT(socket, &typestruct);

/* Buffer space */
static u_long ngpdg_sendspace = 20 * 1024;	/* really max datagram size */
static u_long ngpdg_recvspace = 20 * 1024;

/* List of all sockets */
LIST_HEAD(, ngpcb) ngsocklist;

#define sotongpcb(so) ((struct ngpcb *)(so)->so_pcb)

/* If getting unexplained errors returned, set this to "Debugger("X"); */
#ifndef TRAP_ERROR
#define TRAP_ERROR
#endif

/***************************************************************
	Control sockets
***************************************************************/

static void
ngc_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (priv_check_cred(ai->p_ucred, PRIV_ROOT, NULL_CRED_OKAY) != 0)
		error = EPERM;
	else if (pcbp != NULL)
		error = EISCONN;
	else
		error = ng_attach_cntl(so);
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
ngc_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (pcbp) {
		ng_detach_common(pcbp, NG_CONTROL);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

static void
ngc_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *addr = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	struct ngpcb *const pcbp = sotongpcb(so);
	struct sockaddr_ng *const sap = (struct sockaddr_ng *) addr;
	struct ng_mesg *resp;
	struct mbuf *m0;
	char *xmsg, *path = NULL;
	int len;
	int error = 0;

	if (pcbp == NULL) {
		error = EINVAL;
		goto release;
	}
#ifdef	NOTYET
	if (control && (error = ng_internalize(control, p))) {
		if (pcbp->sockdata == NULL) {
			error = ENOTCONN;
			goto release;
		}
	}
#else	/* NOTYET */
	if (control) {
		error = EINVAL;
		goto release;
	}
#endif	/* NOTYET */

	/* Require destination as there may be >= 1 hooks on this node */
	if (addr == NULL) {
		error = EDESTADDRREQ;
		goto release;
	}

	/* Allocate an expendable buffer for the path, chop off
	 * the sockaddr header, and make sure it's NUL terminated */
	len = sap->sg_len - 2;
	path = kmalloc(len + 1, M_NETGRAPH, M_WAITOK);
	bcopy(sap->sg_data, path, len);
	path[len] = '\0';

	/* Move the actual message out of mbufs into a linear buffer.
	 * Start by adding up the size of the data. (could use mh_len?) */
	for (len = 0, m0 = m; m0 != NULL; m0 = m0->m_next)
		len += m0->m_len;

	/* Move the data into a linear buffer as well. Messages are not
	 * delivered in mbufs. */
	xmsg = kmalloc(len + 1, M_NETGRAPH, M_WAITOK);
	m_copydata(m, 0, len, xmsg);

	/* The callee will free the xmsg when done. The addr is our business. */
	error = ng_send_msg(pcbp->sockdata->node,
			    (struct ng_mesg *) xmsg, path, &resp);

	/* If the callee responded with a synchronous response, then put it
	 * back on the receive side of the socket; sap is source address. */
	if (error == 0 && resp != NULL)
		error = ship_msg(pcbp, resp, sap);

release:
	if (path != NULL)
		kfree(path, M_NETGRAPH);
	if (control != NULL)
		m_freem(control);
	if (m != NULL)
		m_freem(m);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

static void
ngc_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (pcbp)
		error = ng_bind(nam, pcbp);
	else
		error = EINVAL;
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
ngc_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (pcbp)
		error = ng_connect_cntl(nam, pcbp);
	else
		error = EINVAL;
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

/***************************************************************
	Data sockets
***************************************************************/

static void
ngd_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (pcbp)
		error = EISCONN;
	else
		error = ng_attach_data(so);
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
ngd_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (pcbp) {
		ng_detach_common(pcbp, NG_DATA);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

static void
ngd_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *addr = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	struct ngpcb *const pcbp = sotongpcb(so);
	struct sockaddr_ng *const sap = (struct sockaddr_ng *) addr;
	meta_p  mp = NULL;
	int     len, error;
	hook_p  hook = NULL;
	char	hookname[NG_HOOKSIZ];

	if ((pcbp == NULL) || (control != NULL)) {
		error = EINVAL;
		goto release;
	}
	if (pcbp->sockdata == NULL) {
		error = ENOTCONN;
		goto release;
	}
	/*
	 * If the user used any of these ways to not specify an address
	 * then handle specially.
	 */
	if ((sap == NULL)
	    || ((len = sap->sg_len - 2) <= 0)
	    || (*sap->sg_data == '\0')) {
		if (pcbp->sockdata->node->numhooks != 1) {
			error = EDESTADDRREQ;
			goto release;
		}
		/*
		 * if exactly one hook exists, just use it.
		 * Special case to allow write(2) to work on an ng_socket.
		 */
		hook = LIST_FIRST(&pcbp->sockdata->node->hooks);
	} else {
		if (len >= NG_HOOKSIZ) {
			error = EINVAL;
			goto release;
		}

		/*
		 * chop off the sockaddr header, and make sure it's NUL
		 * terminated
		 */
		bcopy(sap->sg_data, hookname, len);
		hookname[len] = '\0';

		/* Find the correct hook from 'hookname' */
		LIST_FOREACH(hook, &pcbp->sockdata->node->hooks, hooks) {
			if (strcmp(hookname, hook->name) == 0)
				break;
		}
		if (hook == NULL)
			error = EHOSTUNREACH;
	}

	/* Send data (OK if hook is NULL) */
	NG_SEND_DATA(error, hook, m, mp);	/* makes m NULL */

release:
	if (control != NULL)
		m_freem(control);
	if (m != NULL)
		m_freem(m);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

static void
ngd_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	if (pcbp) {
		error = ng_connect_data(nam, pcbp);
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

/*
 * Used for both data and control sockets
 */
static void
ng_setsockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **addr = msg->sockaddr.nm_nam;
	struct ngpcb *pcbp;
	struct sockaddr_ng *sg;
	int sg_len, namelen;
	int error;

	/* Why isn't sg_data a `char[1]' ? :-( */
	sg_len = sizeof(struct sockaddr_ng) - sizeof(sg->sg_data) + 1;

	crit_enter();
	pcbp = sotongpcb(so);
	if ((pcbp == NULL) || (pcbp->sockdata == NULL)) {
		crit_exit();
		error = EINVAL;
		goto out;
	}

	namelen = 0;		/* silence compiler ! */

	if (pcbp->sockdata->node->name != NULL)
		sg_len += namelen = strlen(pcbp->sockdata->node->name);

	sg = kmalloc(sg_len, M_SONAME, M_WAITOK | M_ZERO);

	if (pcbp->sockdata->node->name != NULL)
		bcopy(pcbp->sockdata->node->name, sg->sg_data, namelen);
	crit_exit();

	sg->sg_len = sg_len;
	sg->sg_family = AF_NETGRAPH;
	*addr = (struct sockaddr *)sg;
	error = 0;
out:
	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
}

/*
 * Attach a socket to it's protocol specific partner.
 * For a control socket, actually create a netgraph node and attach
 * to it as well.
 */

static int
ng_attach_cntl(struct socket *so)
{
	struct ngsock *privdata;
	struct ngpcb *pcbp;
	int error;

	/* Setup protocol control block */
	if ((error = ng_attach_common(so, NG_CONTROL)) != 0)
		return (error);
	pcbp = sotongpcb(so);

	/* Allocate node private info */
	privdata = kmalloc(sizeof(*privdata), M_NETGRAPH, M_WAITOK | M_ZERO);

	/* Make the generic node components */
	if ((error = ng_make_node_common(&typestruct, &privdata->node)) != 0) {
		kfree(privdata, M_NETGRAPH);
		ng_detach_common(pcbp, NG_CONTROL);
		return (error);
	}
	privdata->node->private = privdata;

	/* Link the pcb and the node private data */
	privdata->ctlsock = pcbp;
	pcbp->sockdata = privdata;
	privdata->refs++;
	return (0);
}

static int
ng_attach_data(struct socket *so)
{
	return(ng_attach_common(so, NG_DATA));
}

/*
 * Set up a socket protocol control block.
 * This code is shared between control and data sockets.
 */
static int
ng_attach_common(struct socket *so, int type)
{
	struct ngpcb *pcbp;
	int error;

	/* Standard socket setup stuff */
	error = soreserve(so, ngpdg_sendspace, ngpdg_recvspace, NULL);
	if (error)
		return (error);

	/* Allocate the pcb */
	pcbp = kmalloc(sizeof(*pcbp), M_PCB, M_WAITOK | M_ZERO);
	pcbp->type = type;

	/* Link the pcb and the socket */
	soreference(so);
	so->so_pcb = (caddr_t) pcbp;
	pcbp->ng_socket = so;

	/* Add the socket to linked list */
	LIST_INSERT_HEAD(&ngsocklist, pcbp, socks);
	return (0);
}

/*
 * Disassociate the socket from it's protocol specific
 * partner. If it's attached to a node's private data structure,
 * then unlink from that too. If we were the last socket attached to it,
 * then shut down the entire node. Shared code for control and data sockets.
 */
static void
ng_detach_common(struct ngpcb *pcbp, int which)
{
	struct ngsock *sockdata;
	struct socket *so;

	if (pcbp->sockdata) {
		sockdata = pcbp->sockdata;
		pcbp->sockdata = NULL;
		switch (which) {
		case NG_CONTROL:
			sockdata->ctlsock = NULL;
			break;
		case NG_DATA:
			sockdata->datasock = NULL;
			break;
		default:
			panic(__func__);
		}
		if ((--sockdata->refs == 0) && (sockdata->node != NULL))
			ng_rmnode(sockdata->node);
	}
	so = pcbp->ng_socket;
	so->so_pcb = NULL;
	pcbp->ng_socket = NULL;
	sofree(so);		/* remove pcb ref */

	LIST_REMOVE(pcbp, socks);
	kfree(pcbp, M_PCB);
}

#ifdef NOTYET
/*
 * File descriptors can be passed into a AF_NETGRAPH socket.
 * Note, that file descriptors cannot be passed OUT.
 * Only character device descriptors are accepted.
 * Character devices are useful to connect a graph to a device,
 * which after all is the purpose of this whole system.
 */
static int
ng_internalize(struct mbuf *control, struct thread *td)
{
	struct filedesc *fdp = p->p_fd;
	const struct cmsghdr *cm = mtod(control, const struct cmsghdr *);
	struct file *fp;
	struct vnode *vn;
	int oldfds;
	int fd;

	if (cm->cmsg_type != SCM_RIGHTS || cm->cmsg_level != SOL_SOCKET ||
	    cm->cmsg_len != control->m_len) {
		TRAP_ERROR;
		return (EINVAL);
	}

	/* Check there is only one FD. XXX what would more than one signify? */
	oldfds = (cm->cmsg_len - sizeof(*cm)) / sizeof(int);
	if (oldfds != 1) {
		TRAP_ERROR;
		return (EINVAL);
	}

	/* Check that the FD given is legit. and change it to a pointer to a
	 * struct file. */
	fd = *(int *) (cm + 1);
	if ((unsigned) fd >= fdp->fd_nfiles
	    || (fp = fdp->fd_files[fd].fp) == NULL) {
		return (EBADF);
	}

	/* Depending on what kind of resource it is, act differently. For
	 * devices, we treat it as a file. For a AF_NETGRAPH socket,
	 * shortcut straight to the node. */
	switch (fp->f_type) {
	case DTYPE_VNODE:
		vn = (struct vnode *) fp->f_data;
		if (vn && (vn->v_type == VCHR)) {
			/* for a VCHR, actually reference the FILE */
			fp->f_count++;
			/* XXX then what :) */
			/* how to pass on to other modules? */
		} else {
			TRAP_ERROR;
			return (EINVAL);
		}
		break;
	default:
		TRAP_ERROR;
		return (EINVAL);
	}
	return (0);
}
#endif	/* NOTYET */

/*
 * Connect the data socket to a named control socket node.
 */
static int
ng_connect_data(struct sockaddr *nam, struct ngpcb *pcbp)
{
	struct sockaddr_ng *sap;
	node_p farnode;
	struct ngsock *sockdata;
	int error;

	/* If we are already connected, don't do it again */
	if (pcbp->sockdata != NULL)
		return (EISCONN);

	/* Find the target (victim) and check it doesn't already have a data
	 * socket. Also check it is a 'socket' type node. */
	sap = (struct sockaddr_ng *) nam;
	if ((error = ng_path2node(NULL, sap->sg_data, &farnode, NULL)))
		return (error);

	if (strcmp(farnode->type->name, NG_SOCKET_NODE_TYPE) != 0)
		return (EINVAL);
	sockdata = farnode->private;
	if (sockdata->datasock != NULL)
		return (EADDRINUSE);

	/* Link the PCB and the private data struct. and note the extra
	 * reference */
	sockdata->datasock = pcbp;
	pcbp->sockdata = sockdata;
	sockdata->refs++;
	return (0);
}

/*
 * Connect the existing control socket node to a named node:hook.
 * The hook we use on this end is the same name as the remote node name.
 */
static int
ng_connect_cntl(struct sockaddr *nam, struct ngpcb *pcbp)
{
	struct ngsock *const sockdata = pcbp->sockdata;
	struct sockaddr_ng *sap;
	char *node, *hook;
	node_p farnode;
	int rtn, error;

	sap = (struct sockaddr_ng *) nam;
	rtn = ng_path_parse(sap->sg_data, &node, NULL, &hook);
	if (rtn < 0 || node == NULL || hook == NULL) {
		TRAP_ERROR;
		return (EINVAL);
	}
	farnode = ng_findname(sockdata->node, node);
	if (farnode == NULL) {
		TRAP_ERROR;
		return (EADDRNOTAVAIL);
	}

	/* Connect, using a hook name the same as the far node name. */
	error = ng_con_nodes(sockdata->node, node, farnode, hook);
	return error;
}

/*
 * Binding a socket means giving the corresponding node a name
 */
static int
ng_bind(struct sockaddr *nam, struct ngpcb *pcbp)
{
	struct ngsock *const sockdata = pcbp->sockdata;
	struct sockaddr_ng *const sap = (struct sockaddr_ng *) nam;

	if (sockdata == NULL) {
		TRAP_ERROR;
		return (EINVAL);
	}
	if (sap->sg_len < 3 || sap->sg_data[sap->sg_len - 3] != '\0') {
		TRAP_ERROR;
		return (EINVAL);
	}
	return (ng_name_node(sockdata->node, sap->sg_data));
}

/*
 * Take a message and pass it up to the control socket associated
 * with the node.
 */
static int
ship_msg(struct ngpcb *pcbp, struct ng_mesg *msg, struct sockaddr_ng *addr)
{
	struct socket *const so = pcbp->ng_socket;
	struct mbuf *mdata;
	int msglen;

	/* Copy the message itself into an mbuf chain */
	msglen = sizeof(struct ng_mesg) + msg->header.arglen;
	mdata = m_devget((caddr_t) msg, msglen, 0, NULL, NULL);

	/* Here we free the message, as we are the end of the line.
	 * We need to do that regardless of whether we got mbufs. */
	kfree(msg, M_NETGRAPH);

	if (mdata == NULL) {
		TRAP_ERROR;
		return (ENOBUFS);
	}

	/* Send it up to the socket */
	lwkt_gettoken(&so->so_rcv.ssb_token);
	if (ssb_appendaddr(&so->so_rcv,
	    (struct sockaddr *) addr, mdata, NULL) == 0) {
		lwkt_reltoken(&so->so_rcv.ssb_token);
		TRAP_ERROR;
		m_freem(mdata);
		return (ENOBUFS);
	}
	sorwakeup(so);
	lwkt_reltoken(&so->so_rcv.ssb_token);
	return (0);
}

/*
 * You can only create new nodes from the socket end of things.
 */
static int
ngs_constructor(node_p *nodep)
{
	return (EINVAL);
}

/*
 * We allow any hook to be connected to the node.
 * There is no per-hook private information though.
 */
static int
ngs_newhook(node_p node, hook_p hook, const char *name)
{
	hook->private = node->private;
	return (0);
}

/*
 * Incoming messages get passed up to the control socket.
 * Unless they are for us specifically (socket_type)
 */
static int
ngs_rcvmsg(node_p node, struct ng_mesg *msg, const char *retaddr,
	   struct ng_mesg **resp)
{
	struct ngsock *const sockdata = node->private;
	struct ngpcb *const pcbp = sockdata->ctlsock;
	struct sockaddr_ng *addr;
	int addrlen;
	int error = 0;

	/* Only allow mesgs to be passed if we have the control socket.
	 * Data sockets can only support the generic messages. */
	if (pcbp == NULL) {
		TRAP_ERROR;
		return (EINVAL);
	}

	if (msg->header.typecookie == NGM_SOCKET_COOKIE) {
		switch (msg->header.cmd) {
		case NGM_SOCK_CMD_NOLINGER:
			sockdata->flags |= NGS_FLAG_NOLINGER;
			break;
		case NGM_SOCK_CMD_LINGER:
			sockdata->flags &= ~NGS_FLAG_NOLINGER;
			break;
		default:
			error = EINVAL;		/* unknown command */
		}
		/* Free the message and return */
		kfree(msg, M_NETGRAPH);
		return(error);

	}
	/* Get the return address into a sockaddr */
	if ((retaddr == NULL) || (*retaddr == '\0'))
		retaddr = "";
	addrlen = strlen(retaddr);
	addr = kmalloc(addrlen + 4, M_NETGRAPH, M_NOWAIT);
	if (addr == NULL) {
		TRAP_ERROR;
		return (ENOMEM);
	}
	addr->sg_len = addrlen + 3;
	addr->sg_family = AF_NETGRAPH;
	bcopy(retaddr, addr->sg_data, addrlen);
	addr->sg_data[addrlen] = '\0';

	/* Send it up */
	error = ship_msg(pcbp, msg, addr);
	kfree(addr, M_NETGRAPH);
	return (error);
}

/*
 * Receive data on a hook
 */
static int
ngs_rcvdata(hook_p hook, struct mbuf *m, meta_p meta)
{
	struct ngsock *const sockdata = hook->node->private;
	struct ngpcb *const pcbp = sockdata->datasock;
	struct socket *so;
	struct sockaddr_ng *addr;
	char *addrbuf[NG_HOOKSIZ + 4];
	int addrlen;

	/* If there is no data socket, black-hole it */
	if (pcbp == NULL) {
		NG_FREE_DATA(m, meta);
		return (0);
	}
	so = pcbp->ng_socket;

	/* Get the return address into a sockaddr. */
	addrlen = strlen(hook->name);	/* <= NG_HOOKSIZ - 1 */
	addr = (struct sockaddr_ng *) addrbuf;
	addr->sg_len = addrlen + 3;
	addr->sg_family = AF_NETGRAPH;
	bcopy(hook->name, addr->sg_data, addrlen);
	addr->sg_data[addrlen] = '\0';

	/* We have no use for the meta data, free/clear it now. */
	NG_FREE_META(meta);

	/* Try to tell the socket which hook it came in on */
	lwkt_gettoken(&so->so_rcv.ssb_token);
	if (ssb_appendaddr(&so->so_rcv, (struct sockaddr *) addr, m, NULL) == 0) {
		lwkt_reltoken(&so->so_rcv.ssb_token);
		m_freem(m);
		TRAP_ERROR;
		return (ENOBUFS);
	}
	sorwakeup(so);
	lwkt_reltoken(&so->so_rcv.ssb_token);
	return (0);
}

/*
 * Hook disconnection
 *
 * For this type, removal of the last link destroys the node
 * if the NOLINGER flag is set.
 */
static int
ngs_disconnect(hook_p hook)
{
	struct ngsock *const sockdata = hook->node->private;

	if ((sockdata->flags & NGS_FLAG_NOLINGER )
	    && (hook->node->numhooks == 0)) {
		ng_rmnode(hook->node);
	}
	return (0);
}

/*
 * Do local shutdown processing.
 * In this case, that involves making sure the socket
 * knows we should be shutting down.
 */
static int
ngs_rmnode(node_p node)
{
	struct ngsock *const sockdata = node->private;
	struct ngpcb *const dpcbp = sockdata->datasock;
	struct ngpcb *const pcbp = sockdata->ctlsock;

	ng_cutlinks(node);
	ng_unname(node);

	if (dpcbp != NULL) {
		soisdisconnected(dpcbp->ng_socket);
		dpcbp->sockdata = NULL;
		sockdata->datasock = NULL;
		sockdata->refs--;
	}
	if (pcbp != NULL) {
		soisdisconnected(pcbp->ng_socket);
		pcbp->sockdata = NULL;
		sockdata->ctlsock = NULL;
		sockdata->refs--;
	}
	node->private = NULL;
	ng_unref(node);
	kfree(sockdata, M_NETGRAPH);
	return (0);
}

/*
 * Control and data socket type descriptors
 */

static struct pr_usrreqs ngc_usrreqs = {
	.pru_abort = NULL,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = ngc_attach,
	.pru_bind = ngc_bind,
	.pru_connect = ngc_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = pr_generic_notsupp,
	.pru_detach = ngc_detach,
	.pru_disconnect = NULL,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = NULL,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = ngc_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = NULL,
	.pru_sockaddr = ng_setsockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

static struct pr_usrreqs ngd_usrreqs = {
	.pru_abort = NULL,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = ngd_attach,
	.pru_bind = NULL,
	.pru_connect = ngd_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = pr_generic_notsupp,
	.pru_detach = ngd_detach,
	.pru_disconnect = NULL,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = NULL,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = ngd_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = NULL,
	.pru_sockaddr = ng_setsockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

/*
 * Definitions of protocols supported in the NETGRAPH domain.
 */

extern struct domain ngdomain;		/* stop compiler warnings */

static struct protosw ngsw[] = {
    {
	.pr_type = SOCK_DGRAM,
	.pr_domain = &ngdomain,
	.pr_protocol = NG_CONTROL,
	.pr_flags = PR_ATOMIC | PR_ADDR /* | PR_RIGHTS */,
	.pr_usrreqs = &ngc_usrreqs
    },
    {
	.pr_type = SOCK_DGRAM,
	.pr_domain = &ngdomain,
	.pr_protocol = NG_DATA,
	.pr_flags = PR_ATOMIC | PR_ADDR,
	.pr_usrreqs = &ngd_usrreqs
    }
};

struct domain ngdomain = {
	AF_NETGRAPH, "netgraph", NULL, NULL, NULL,
	ngsw, &ngsw[NELEM(ngsw)],
};

/*
 * Handle loading and unloading for this node type
 * This is to handle auxiliary linkages (e.g protocol domain addition).
 */
static int
ngs_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		/* Register protocol domain */
		net_add_domain(&ngdomain);
		break;
	case MOD_UNLOAD:
		/* Insure there are no open netgraph sockets */
		if (!LIST_EMPTY(&ngsocklist)) {
			error = EBUSY;
			break;
		}

#ifdef NOTYET
		/* Unregister protocol domain XXX can't do this yet.. */
		if ((error = net_rm_domain(&ngdomain)) != 0)
			break;
#else
		error = EBUSY;
#endif
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

SYSCTL_INT(_net_graph, OID_AUTO, family, CTLFLAG_RD, 0, AF_NETGRAPH, "");
SYSCTL_NODE(_net_graph, OID_AUTO, data, CTLFLAG_RW, 0, "DATA");
SYSCTL_INT(_net_graph_data, OID_AUTO, proto, CTLFLAG_RD, 0, NG_DATA, "");
SYSCTL_NODE(_net_graph, OID_AUTO, control, CTLFLAG_RW, 0, "CONTROL");
SYSCTL_INT(_net_graph_control, OID_AUTO, proto, CTLFLAG_RD, 0, NG_CONTROL, "");
