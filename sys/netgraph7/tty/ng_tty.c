/*-
 * (MPSAFE)
 *
 * ng_tty.c
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
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD: src/sys/netgraph/ng_tty.c,v 1.37 2006/11/06 13:42:03 rwatson Exp $
 * $Whistle: ng_tty.c,v 1.21 1999/11/01 09:24:52 julian Exp $
 */

/*
 * This file implements a terminal line discipline that is also a
 * netgraph node. Installing this line discipline on a terminal device
 * instantiates a new netgraph node of this type, which allows access
 * to the device via the "hook" hook of the node.
 *
 * Once the line discipline is installed, you can find out the name
 * of the corresponding netgraph node via a NGIOCGINFO ioctl().
 *
 * Incoming characters are delievered to the hook one at a time, each
 * in its own mbuf. You may optionally define a ``hotchar,'' which causes
 * incoming characters to be buffered up until either the hotchar is
 * seen or the mbuf is full (MHLEN bytes). Then all buffered characters
 * are immediately delivered.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/tty.h>
#include <sys/ttycom.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netgraph7/netgraph.h>
#include <netgraph7/ng_message.h>
#include "ng_tty.h"

/* Misc defs */
#define MAX_MBUFQ		3	/* Max number of queued mbufs */
#define NGT_HIWATER		400	/* High water mark on output */

/* Per-node private info */
struct ngt_sc {
	struct	tty *tp;		/* Terminal device */
	node_p	node;			/* Netgraph node */
	hook_p	hook;			/* Netgraph hook */
	struct	ifqueue outq;		/* Queue of outgoing data */
	struct	mbuf *m;		/* Incoming data buffer */
	short	hotchar;		/* Hotchar, or -1 if none */
	u_int	flags;			/* Flags */
	struct	callout	chand;		/* See man timeout(9) */
};
typedef struct ngt_sc *sc_p;

/* Flags */
#define FLG_DEBUG		0x0002
#define	FLG_DIE			0x0004

/* Line discipline methods */
static int	ngt_open(struct cdev *dev, struct tty *tp);
static int	ngt_close(struct tty *tp, int flag);
static int	ngt_read(struct tty *tp, struct uio *uio, int flag);
static int	ngt_write(struct tty *tp, struct uio *uio, int flag);
static int	ngt_tioctl(struct tty *tp,
		    u_long cmd, caddr_t data, int flag, struct ucred *cred);
static int	ngt_input(int c, struct tty *tp);
static int	ngt_start(struct tty *tp);

/* Netgraph methods */
static ng_constructor_t	ngt_constructor;
static ng_rcvmsg_t	ngt_rcvmsg;
static ng_shutdown_t	ngt_shutdown;
static ng_newhook_t	ngt_newhook;
static ng_connect_t	ngt_connect;
static ng_rcvdata_t	ngt_rcvdata;
static ng_disconnect_t	ngt_disconnect;
static int		ngt_mod_event(module_t mod, int event, void *data);

/* Other stuff */
static void	ngt_timeout(node_p node, hook_p hook, void *arg1, int arg2);

#define ERROUT(x)		do { error = (x); goto done; } while (0)

/* Line discipline descriptor */
static struct linesw ngt_disc = {
	.l_open =	ngt_open,
	.l_close =	ngt_close,
	.l_read =	ngt_read,
	.l_write =	ngt_write,
	.l_ioctl =	ngt_tioctl,
	.l_rint =	ngt_input,
	.l_start =	ngt_start,
	.l_modem =	ttymodem,
};

/* Netgraph node type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_TTY_NODE_TYPE,
	.mod_event =	ngt_mod_event,
	.constructor =	ngt_constructor,
	.rcvmsg =	ngt_rcvmsg,
	.shutdown =	ngt_shutdown,
	.newhook =	ngt_newhook,
	.connect =	ngt_connect,
	.rcvdata =	ngt_rcvdata,
	.disconnect =	ngt_disconnect,
};
NETGRAPH_INIT(tty, &typestruct);

/*
 * Locking:
 *
 * - node private data and tp->t_lsc is protected by mutex in struct
 *   ifqueue, locking is done using IF_XXX() macros.
 * - in all tty methods we should acquire node ifqueue mutex, when accessing
 *   private data.
 * - in _rcvdata() we should use locked versions of IF_{EN,DE}QUEUE() since
 *   we may have multiple _rcvdata() threads.
 * - when calling any of tty methods from netgraph methods, we should
 *   acquire tty locking (now Giant).
 *
 * - ngt_unit is incremented atomically.
 */

static int ngt_unit;
static int ngt_ldisc;

/******************************************************************
		    LINE DISCIPLINE METHODS
******************************************************************/

/*
 * Set our line discipline on the tty.
 * Called from device open routine or ttioctl()
 */
static int
ngt_open(struct cdev *dev, struct tty *tp)
{
	struct thread *const td = curthread;	/* XXX */
	char name[sizeof(NG_TTY_NODE_TYPE) + 8];
	sc_p sc;
	int error;

	/* Super-user only */
	error = priv_check(td, PRIV_NETGRAPH_TTY);
	if (error)
		return (error);

	/* Initialize private struct */
	sc = kmalloc(sizeof(*sc), M_NETGRAPH, M_WAITOK | M_ZERO);

	crit_enter();
	lwkt_gettoken(&tty_token);
	sc->tp = tp;
	sc->hotchar = NG_TTY_DFL_HOTCHAR;
	sc->outq.ifq_maxlen = MAX_MBUFQ;

	/* Setup netgraph node */
	error = ng_make_node_common(&typestruct, &sc->node);
	if (error) {
		kfree(sc, M_NETGRAPH);
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (error);
	}

	atomic_add_int(&ngt_unit, 1);
	ksnprintf(name, sizeof(name), "%s%d", typestruct.name, ngt_unit);

	/* Assign node its name */
	if ((error = ng_name_node(sc->node, name))) {
		sc->flags |= FLG_DIE;
		NG_NODE_UNREF(sc->node);
		log(LOG_ERR, "%s: node name exists?\n", name);
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (error);
	}

	/* Set back pointers */
	NG_NODE_SET_PRIVATE(sc->node, sc);
	tp->t_sc = sc;

	callout_init_mp(&sc->chand);

	/*
	 * Pre-allocate cblocks to the an appropriate amount.
	 * I'm not sure what is appropriate.
	 */
	ttyflush(tp, FREAD | FWRITE);
	clist_alloc_cblocks(&tp->t_canq, 0, 0);
	clist_alloc_cblocks(&tp->t_rawq, 0, 0);
	clist_alloc_cblocks(&tp->t_outq,
	    MLEN + NGT_HIWATER, MLEN + NGT_HIWATER);

	lwkt_reltoken(&tty_token);
	crit_exit();
	return (error);
}

/*
 * Line specific close routine, called from device close routine
 * and from ttioctl. This causes the node to be destroyed as well.
 */
static int
ngt_close(struct tty *tp, int flag)
{
	const sc_p sc = (sc_p) tp->t_sc;

	crit_enter();
	lwkt_gettoken(&tty_token);
	ttyflush(tp, FREAD | FWRITE);
	clist_free_cblocks(&tp->t_outq);
	if (sc != NULL) {
		if (callout_pending(&sc->chand))
			ng_uncallout(&sc->chand, sc->node);
		tp->t_sc = NULL;
		sc->flags |= FLG_DIE;
		ng_rmnode_self(sc->node);
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * Once the device has been turned into a node, we don't allow reading.
 */
static int
ngt_read(struct tty *tp, struct uio *uio, int flag)
{
	return (EIO);
}

/*
 * Once the device has been turned into a node, we don't allow writing.
 */
static int
ngt_write(struct tty *tp, struct uio *uio, int flag)
{
	return (EIO);
}

/*
 * We implement the NGIOCGINFO ioctl() defined in ng_message.h.
 */
static int
ngt_tioctl(struct tty *tp, u_long cmd, caddr_t data, int flag, struct ucred *cred)
{
	const sc_p sc = (sc_p) tp->t_sc;

	if (sc == NULL) {
		/* No node attached */
		return (0);
	}

	crit_enter();
	lwkt_gettoken(&tty_token);
	switch (cmd) {
	case NGIOCGINFO:
	    {
		struct nodeinfo *const ni = (struct nodeinfo *) data;
		const node_p node = sc->node;

		bzero(ni, sizeof(*ni));
		if (NG_NODE_HAS_NAME(node))
			strncpy(ni->name, NG_NODE_NAME(node), sizeof(ni->name) - 1);
		strncpy(ni->type, node->nd_type->name, sizeof(ni->type) - 1);
		ni->id = (u_int32_t) ng_node2ID(node);
		ni->hooks = NG_NODE_NUMHOOKS(node);
		break;
	    }
	default:
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (ENOIOCTL);
	}

	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * Receive data coming from the device. We get one character at
 * a time, which is kindof silly.
 *
 * Full locking of softc is not required, since we are the only
 * user of sc->m.
 */
static int
ngt_input(int c, struct tty *tp)
{
	sc_p sc;
	node_p node;
	struct mbuf *m;
	int error = 0;

	crit_enter();
	lwkt_gettoken(&tty_token);
	sc = (sc_p) tp->t_sc;
	if (sc == NULL) {
		/* No node attached */
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (0);
	}

	node = sc->node;

	if (tp != sc->tp)
		panic("ngt_input");

	/* Check for error conditions */
	if ((tp->t_state & TS_CONNECTED) == 0) {
		if (sc->flags & FLG_DEBUG)
			log(LOG_DEBUG, "%s: no carrier\n", NG_NODE_NAME(node));
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (0);
	}
	if (c & TTY_ERRORMASK) {
		/* framing error or overrun on this char */
		if (sc->flags & FLG_DEBUG)
			log(LOG_DEBUG, "%s: line error %x\n",
			    NG_NODE_NAME(node), c & TTY_ERRORMASK);
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (0);
	}
	c &= TTY_CHARMASK;

	/* Get a new header mbuf if we need one */
	if (!(m = sc->m)) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (!m) {
			if (sc->flags & FLG_DEBUG)
				log(LOG_ERR,
				    "%s: can't get mbuf\n", NG_NODE_NAME(node));
			lwkt_reltoken(&tty_token);
			crit_exit();
			return (ENOBUFS);
		}
		m->m_len = m->m_pkthdr.len = 0;
		m->m_pkthdr.rcvif = NULL;
		sc->m = m;
	}

	/* Add char to mbuf */
	*mtod(m, u_char *) = c;
	m->m_data++;
	m->m_len++;
	m->m_pkthdr.len++;

	/* Ship off mbuf if it's time */
	if (sc->hotchar == -1 || c == sc->hotchar || m->m_len >= MHLEN) {
		m->m_data = m->m_pktdat;
		sc->m = NULL;

		/*
		 * We have built our mbuf without checking that we actually
		 * have a hook to send it. This was done to avoid
		 * acquiring mutex on each character. Check now.
		 *
		 */

		if (sc->hook == NULL) {
			m_freem(m);
			lwkt_reltoken(&tty_token);
			crit_exit();
			return (0);		/* XXX: original behavior */
		}
		NG_SEND_DATA_ONLY(error, sc->hook, m);	/* Will queue */
	}

	lwkt_reltoken(&tty_token);
	crit_exit();
	return (error);
}

/*
 * This is called when the device driver is ready for more output.
 * Also called from ngt_rcv_data() when a new mbuf is available for output.
 */
static int
ngt_start(struct tty *tp)
{
	const sc_p sc = (sc_p) tp->t_sc;

	crit_enter();
	lwkt_gettoken(&tty_token);
	while (tp->t_outq.c_cc < NGT_HIWATER) {	/* XXX 2.2 specific ? */
		struct mbuf *m;

		/* Remove first mbuf from queue */
		IF_DEQUEUE(&sc->outq, m);
		if (m == NULL)
			break;

		/* Send as much of it as possible */
		while (m != NULL) {
			int     sent;

			sent = m->m_len
			    - b_to_q(mtod(m, u_char *), m->m_len, &tp->t_outq);
			m->m_data += sent;
			m->m_len -= sent;
			if (m->m_len > 0)
				break;	/* device can't take no more */
			m = m_free(m);
		}

		/* Put remainder of mbuf chain (if any) back on queue */
		if (m != NULL) {
			IF_PREPEND(&sc->outq, m);
			break;
		}
	}

	/* Call output process whether or not there is any output. We are
	 * being called in lieu of ttstart and must do what it would. */
	if (tp->t_oproc != NULL)
		(*tp->t_oproc) (tp);

	/* This timeout is needed for operation on a pseudo-tty, because the
	 * pty code doesn't call pppstart after it has drained the t_outq. */
	/* XXX: outq not locked */
	if (!IF_QEMPTY(&sc->outq) && !callout_pending(&sc->chand))
		ng_callout(&sc->chand, sc->node, NULL, 1, ngt_timeout, NULL, 0);

	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * We still have data to output to the device, so try sending more.
 */
static void
ngt_timeout(node_p node, hook_p hook, void *arg1, int arg2)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	crit_enter();
	lwkt_gettoken(&tty_token);
	ngt_start(sc->tp);
	lwkt_reltoken(&tty_token);
	crit_exit();
}

/******************************************************************
		    NETGRAPH NODE METHODS
******************************************************************/

/*
 * Initialize a new node of this type.
 *
 * We only allow nodes to be created as a result of setting
 * the line discipline on a tty, so always return an error if not.
 */
static int
ngt_constructor(node_p node)
{
	return (EOPNOTSUPP);
}

/*
 * Add a new hook. There can only be one.
 */
static int
ngt_newhook(node_p node, hook_p hook, const char *name)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_TTY_HOOK))
		return (EINVAL);
	crit_enter();
	lwkt_gettoken(&tty_token);
	if (sc->hook) {
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (EISCONN);
	}
	sc->hook = hook;
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * Set the hook into queueing mode (for outgoing packets),
 * so that we wont deliver mbuf thru the whole graph holding
 * tty locks.
 */
static int
ngt_connect(hook_p hook)
{
	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));
	/*
	 * XXX: While ngt_start() is Giant-locked, queue incoming
	 * packets, too. Otherwise we acquire Giant holding some
	 * IP stack locks, e.g. divinp, and this makes WITNESS scream.
	 */
	NG_HOOK_FORCE_QUEUE(hook);
	return (0);
}

/*
 * Disconnect the hook
 */
static int
ngt_disconnect(hook_p hook)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	crit_enter();
	lwkt_gettoken(&tty_token);
	if (hook != sc->hook)
		panic(__func__);
	sc->hook = NULL;
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * Remove this node. The does the netgraph portion of the shutdown.
 * This should only be called indirectly from ngt_close().
 *
 * tp->t_lsc is already NULL, so we should be protected from
 * tty calls now.
 */
static int
ngt_shutdown(node_p node)
{
	const sc_p sc = NG_NODE_PRIVATE(node);

	crit_enter();
	lwkt_gettoken(&tty_token);
	if (!(sc->flags & FLG_DIE)) {
		lwkt_reltoken(&tty_token);
		crit_exit();
		return (EOPNOTSUPP);
	}

	/* Free resources */
	IF_DRAIN(&sc->outq);
	m_freem(sc->m);
	kfree(sc, M_NETGRAPH);
	lwkt_reltoken(&tty_token);
	crit_exit();
	return (0);
}

/*
 * Receive incoming data from netgraph system. Put it on our
 * output queue and start output if necessary.
 */
static int
ngt_rcvdata(hook_p hook, item_p item)
{
	const sc_p sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf *m;
	int qlen;

	if (hook != sc->hook)
		panic(__func__);

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	crit_enter();
	if (IF_QFULL(&sc->outq)) {
		IF_DROP(&sc->outq);
		crit_exit();
		NG_FREE_M(m);
		return (ENOBUFS);
	}

	IF_ENQUEUE(&sc->outq, m);
	qlen = sc->outq.ifq_len;
	crit_exit();

	/*
	 * If qlen > 1, then we should already have a scheduled callout.
	 */
	if (qlen == 1) {
		lwkt_gettoken(&tty_token);
		ngt_start(sc->tp);
		lwkt_reltoken(&tty_token);
	}

	return (0);
}

/*
 * Receive control message
 */
static int
ngt_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	const sc_p sc = NG_NODE_PRIVATE(node);
	struct ng_mesg *msg, *resp = NULL;
	int error = 0;

	NGI_GET_MSG(item, msg);
	switch (msg->header.typecookie) {
	case NGM_TTY_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TTY_SET_HOTCHAR:
		    {
			int     hotchar;

			if (msg->header.arglen != sizeof(int))
				ERROUT(EINVAL);
			hotchar = *((int *) msg->data);
			if (hotchar != (u_char) hotchar && hotchar != -1)
				ERROUT(EINVAL);
			sc->hotchar = hotchar;	/* race condition is OK */
			break;
		    }
		case NGM_TTY_GET_HOTCHAR:
			NG_MKRESPONSE(resp, msg, sizeof(int), M_WAITOK | M_NULLOK);
			if (!resp)
				ERROUT(ENOMEM);
			/* Race condition here is OK */
			*((int *) resp->data) = sc->hotchar;
			break;
		default:
			ERROUT(EINVAL);
		}
		break;
	default:
		ERROUT(EINVAL);
	}
done:
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/******************************************************************
		    	INITIALIZATION
******************************************************************/

/*
 * Handle loading and unloading for this node type
 */
static int
ngt_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		/* Register line discipline */
		crit_enter();
		if ((ngt_ldisc = ldisc_register(NETGRAPHDISC, &ngt_disc)) < 0) {
			crit_exit();
			log(LOG_ERR, "%s: can't register line discipline",
			    __func__);
			return (EIO);
		}
		crit_exit();
		break;

	case MOD_UNLOAD:

		/* Unregister line discipline */
		crit_enter();
		ldisc_deregister(ngt_ldisc);
		crit_exit();
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
