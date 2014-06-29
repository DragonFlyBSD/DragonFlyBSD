/*
 * ng_eiface.c
 *
 * Copyright (c) 1999-2000, Vitaly V Belekhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * 	$Id: ng_eiface.c,v 1.14 2000/03/15 12:28:44 vitaly Exp $
 * $FreeBSD: src/sys/netgraph/ng_eiface.c,v 1.4.2.5 2002/12/17 21:47:48 julian Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/serialize.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/netisr.h>


#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include "ng_eiface.h"

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if_arp.h>

static const struct ng_parse_struct_field ng_eiface_par_fields[]
	= NG_EIFACE_PAR_FIELDS;

static const struct ng_parse_type ng_eiface_par_type = {
	&ng_parse_struct_type,
	&ng_eiface_par_fields
};

static const struct ng_cmdlist ng_eiface_cmdlist[] = {
	{
	  NGM_EIFACE_COOKIE,
	  NGM_EIFACE_SET,
	  "set",
	  &ng_eiface_par_type,
	  NULL
	},
	{ 0 }
};

/* Node private data */
struct ng_eiface_private {
	struct arpcom	arpcom;		/* per-interface network data */
	struct	ifnet *ifp;		/* This interface */
	node_p	node;			/* Our netgraph node */
	hook_p	ether;			/* Hook for ethernet stream */
	struct	private *next;		/* When hung on the free list */
};
typedef struct ng_eiface_private *priv_p;

/* Interface methods */
static void	ng_eiface_init(void *xsc);
static void	ng_eiface_start(struct ifnet *ifp, struct ifaltq_subque *);
static int	ng_eiface_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data,
				struct ucred *cr);
#ifdef DEBUG
static void	ng_eiface_print_ioctl(struct ifnet *ifp, int cmd, caddr_t data);
#endif

/* Netgraph methods */
static ng_constructor_t	ng_eiface_constructor;
static ng_rcvmsg_t	ng_eiface_rcvmsg;
static ng_shutdown_t	ng_eiface_rmnode;
static ng_newhook_t	ng_eiface_newhook;
static ng_rcvdata_t	ng_eiface_rcvdata;
static ng_connect_t	ng_eiface_connect;
static ng_disconnect_t	ng_eiface_disconnect;

/* Node type descriptor */
static struct ng_type typestruct = {
	NG_VERSION,
	NG_EIFACE_NODE_TYPE,
	NULL,
	ng_eiface_constructor,
	ng_eiface_rcvmsg,
	ng_eiface_rmnode,
	ng_eiface_newhook,
	NULL,
	ng_eiface_connect,
	ng_eiface_rcvdata,
	ng_eiface_rcvdata,
	ng_eiface_disconnect,
	ng_eiface_cmdlist
};
NETGRAPH_INIT(eiface, &typestruct);

static char ng_eiface_ifname[] = NG_EIFACE_EIFACE_NAME;
static int ng_eiface_next_unit;

/************************************************************************
			INTERFACE STUFF
 ************************************************************************/

/*
 * Process an ioctl for the virtual interface
 */
static int
ng_eiface_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ifreq *const ifr = (struct ifreq *) data;
	int error = 0;

#ifdef DEBUG
	ng_eiface_print_ioctl(ifp, cmd, data);
#endif
	crit_enter();
	switch (cmd) {

	/* These two are mostly handled at a higher layer */
	case SIOCSIFADDR:
		error = ether_ioctl(ifp, cmd, data);
		break;
	case SIOCGIFADDR:
		break;

	/* Set flags */
	case SIOCSIFFLAGS:
		/*
		 * If the interface is marked up and stopped, then start it.
		 * If it is marked down and running, then stop it.
		 */
		if (ifr->ifr_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING)) {
				ifq_clr_oactive(&ifp->if_snd);
				ifp->if_flags |= IFF_RUNNING;
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				ifp->if_flags &= ~IFF_RUNNING;
				ifq_clr_oactive(&ifp->if_snd);
			}
		}
		break;

	/* Set the interface MTU */
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > NG_EIFACE_MTU_MAX
		    || ifr->ifr_mtu < NG_EIFACE_MTU_MIN)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	/* Stuff that's not supported */
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = 0;
		break;
	case SIOCSIFPHYS:
		error = EOPNOTSUPP;
		break;

	default:
		error = EINVAL;
		break;
	}
	crit_exit();
	return (error);
}

static void
ng_eiface_init(void *xsc)
{
	priv_p sc = xsc;
	struct ifnet *ifp = sc->ifp;

	crit_enter();

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	crit_exit();

}

/*
 * This routine is called to deliver a packet out the interface.
 * We simply relay the packet to
 * the ether hook, if it is connected.
 */

static void
ng_eiface_start(struct ifnet *ifp, struct ifaltq_subque *ifsq __unused)
{
	const priv_p priv = (priv_p) ifp->if_softc;
	meta_p meta = NULL;
	int len, error = 0;
	struct mbuf *m;

	/* Check interface flags */
	if ((ifp->if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING))
		return;

	/* Don't do anything if output is active */
	if(ifq_is_oactive(&ifp->if_snd))
		return;

	ifq_set_oactive(&ifp->if_snd);

	/*
	 * Grab a packet to transmit.
	 */
	m = ifq_dequeue(&ifp->if_snd);

	/* If there's nothing to send, return. */
	if(m == NULL)
	{
		ifq_clr_oactive(&ifp->if_snd);
		return;
	}

	BPF_MTAP(ifp, m);

	/* Copy length before the mbuf gets invalidated */
	len = m->m_pkthdr.len;

	/* Send packet; if hook is not connected, mbuf will get freed. */
	NG_SEND_DATA(error, priv->ether, m, meta);

	/* Update stats */
	if (error == 0) {
		IFNET_STAT_INC(ifp, obytes, len);
		IFNET_STAT_INC(ifp, opackets, 1);
	}

	ifq_clr_oactive(&ifp->if_snd);

	return;
}

#ifdef DEBUG
/*
 * Display an ioctl to the virtual interface
 */

static void
ng_eiface_print_ioctl(struct ifnet *ifp, int cmd, caddr_t data)
{
	char   *str;

	switch (cmd & IOC_DIRMASK) {
	case IOC_VOID:
		str = "IO";
		break;
	case IOC_OUT:
		str = "IOR";
		break;
	case IOC_IN:
		str = "IOW";
		break;
	case IOC_INOUT:
		str = "IORW";
		break;
	default:
		str = "IO??";
	}
	log(LOG_DEBUG, "%s: %s('%c', %d, char[%d])\n",
	       ifp->if_xname,
	       str,
	       IOCGROUP(cmd),
	       cmd & 0xff,
	       IOCPARM_LEN(cmd));
}
#endif /* DEBUG */

/************************************************************************
			NETGRAPH NODE STUFF
 ************************************************************************/

/*
 * Constructor for a node
 */
static int
ng_eiface_constructor(node_p *nodep)
{
	struct ifnet *ifp;
	node_p node;
	priv_p priv;
	int error = 0;

	/* Allocate node and interface private structures */
	priv = kmalloc(sizeof(*priv), M_NETGRAPH, M_WAITOK | M_ZERO);

	ifp = &(priv->arpcom.ac_if);

	/* Link them together */
	ifp->if_softc = priv;
	priv->ifp = ifp;

	/* Call generic node constructor */
	if ((error = ng_make_node_common(&typestruct, nodep))) {
		kfree(priv, M_NETGRAPH);
		return (error);
	}
	node = *nodep;

	/* Link together node and private info */
	node->private = priv;
	priv->node = node;

	/* Initialize interface structure */
	if_initname(ifp, ng_eiface_ifname, ng_eiface_next_unit++);
	ifp->if_init = ng_eiface_init;
	ifp->if_start = ng_eiface_start;
	ifp->if_ioctl = ng_eiface_ioctl;
	ifp->if_watchdog = NULL;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifp->if_flags = (IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST);

	/* Give this node name *
	bzero(ifname, sizeof(ifname));
	ksprintf(ifname, "if%s", ifp->if_xname);
	ng_name_node(node, ifname);
	*/

	/* Attach the interface */
	ether_ifattach(ifp, priv->arpcom.ac_enaddr, NULL);

	/* Done */
	return (0);
}

/*
 * Give our ok for a hook to be added
 */
static int
ng_eiface_newhook(node_p node, hook_p hook, const char *name)
{
	priv_p priv = node->private;

	if (strcmp(name, NG_EIFACE_HOOK_ETHER))
		return (EPFNOSUPPORT);
	if (priv->ether != NULL)
		return (EISCONN);
	priv->ether = hook;
	hook->private = &priv->ether;

	return (0);
}

/*
 * Receive a control message
 */
static int
ng_eiface_rcvmsg(node_p node, struct ng_mesg *msg,
		const char *retaddr, struct ng_mesg **rptr)
{
	const priv_p priv = node->private;
	struct ifnet *const ifp = priv->ifp;
	struct ng_mesg *resp = NULL;
	int error = 0;

	switch (msg->header.typecookie) {
	case NGM_EIFACE_COOKIE:
		switch (msg->header.cmd) {

		case NGM_EIFACE_SET:
		    {
		      struct ng_eiface_par *eaddr;

		      if (msg->header.arglen != sizeof(struct ng_eiface_par)) 
			{
			  error = EINVAL;
			  break;
			}
		      eaddr = (struct ng_eiface_par *)(msg->data);

		      priv->arpcom.ac_enaddr[0] = eaddr->oct0;
		      priv->arpcom.ac_enaddr[1] = eaddr->oct1;
		      priv->arpcom.ac_enaddr[2] = eaddr->oct2;
		      priv->arpcom.ac_enaddr[3] = eaddr->oct3;
		      priv->arpcom.ac_enaddr[4] = eaddr->oct4;
		      priv->arpcom.ac_enaddr[5] = eaddr->oct5;

		      break;
		    }

		case NGM_EIFACE_GET_IFNAME:
		    {
			struct ng_eiface_ifname *arg;

			NG_MKRESPONSE(resp, msg, sizeof(*arg), M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}
			arg = (struct ng_eiface_ifname *) resp->data;
			ksprintf(arg->ngif_name,
			    "%s", ifp->if_xname); /* XXX: strings */
			break;
		    }

		case NGM_EIFACE_GET_IFADDRS:
		    {
			struct ifaddr_container *ifac;
			caddr_t ptr;
			int buflen;

#define SA_SIZE(s)	((s)->sa_len<sizeof(*(s))? sizeof(*(s)):(s)->sa_len)

			/* Determine size of response and allocate it */
			buflen = 0;
			TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid],
				      ifa_link)
				buflen += SA_SIZE(ifac->ifa->ifa_addr);
			NG_MKRESPONSE(resp, msg, buflen, M_NOWAIT);
			if (resp == NULL) {
				error = ENOMEM;
				break;
			}

			/* Add addresses */
			ptr = resp->data;
			TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid],
				      ifa_link) {
				struct ifaddr *ifa = ifac->ifa;
				const int len = SA_SIZE(ifa->ifa_addr);

				if (buflen < len) {
					log(LOG_ERR, "%s: len changed?\n",
					    ifp->if_xname);
					break;
				}
				bcopy(ifa->ifa_addr, ptr, len);
				ptr += len;
				buflen -= len;
			}
			break;
#undef SA_SIZE
		    }

		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	if (rptr)
		*rptr = resp;
	else if (resp)
		kfree(resp, M_NETGRAPH);
	kfree(msg, M_NETGRAPH);
	return (error);
}

/*
 * Recive data from a hook. Pass the packet to the ether_input routine.
 */
static int
ng_eiface_rcvdata(hook_p hook, struct mbuf *m, meta_p meta)
{
	const priv_p priv = hook->node->private;
	struct ifnet *const ifp = priv->ifp;
	int error = 0;

	/* Meta-data is end its life here... */
	NG_FREE_META(meta);

	if (m == NULL) {
	    kprintf("ng_eiface: mbuf is null.\n");
	    return (EINVAL);
	}

	if ( !(ifp->if_flags & IFF_UP) )
		return (ENETDOWN);

	/* Note receiving interface */
	m->m_pkthdr.rcvif = ifp;

	/* Update interface stats */
	IFNET_STAT_INC(ifp, ipackets, 1);

	BPF_MTAP(ifp, m);

	ifp->if_input(ifp, m, NULL, -1);

	/* Done */
	return (error);
}

/*
 * Because the BSD networking code doesn't support the removal of
 * networking interfaces, iface nodes (once created) are persistent.
 * So this method breaks all connections and marks the interface
 * down, but does not remove the node.
 */
static int
ng_eiface_rmnode(node_p node)
{
	const priv_p priv = node->private;
	struct ifnet *const ifp = priv->ifp;

	ng_cutlinks(node);
	node->flags &= ~NG_INVALID;
	ifnet_serialize_all(ifp);
	ifp->if_flags &= ~(IFF_UP | IFF_RUNNING);
	ifq_clr_oactive(&ifp->if_snd);
	ifnet_deserialize_all(ifp);
	return (0);
}


/*
 * This is called once we've already connected a new hook to the other node.
 * It gives us a chance to balk at the last minute.
 */
static int
ng_eiface_connect(hook_p hook)
{
	/* be really amiable and just say "YUP that's OK by me! " */
	return (0);
}

/*
 * Hook disconnection
 */
static int
ng_eiface_disconnect(hook_p hook)
{
	const priv_p priv = hook->node->private;

	priv->ether = NULL;
	return (0);
}
