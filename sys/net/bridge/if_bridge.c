/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $OpenBSD: if_bridge.c,v 1.60 2001/06/15 03:38:33 itojun Exp $
 * $NetBSD: if_bridge.c,v 1.31 2005/06/01 19:45:34 jdc Exp $
 * $FreeBSD: src/sys/net/if_bridge.c,v 1.26 2005/10/13 23:05:55 thompsa Exp $
 * $DragonFly: src/sys/net/bridge/if_bridge.c,v 1.42 2008/07/27 10:06:57 sephe Exp $
 */

/*
 * Network interface bridge support.
 *
 * TODO:
 *
 *	- Currently only supports Ethernet-like interfaces (Ethernet,
 *	  802.11, VLANs on Ethernet, etc.)  Figure out a nice way
 *	  to bridge other types of interfaces (FDDI-FDDI, and maybe
 *	  consider heterogenous bridges).
 */

#include <sys/cdefs.h>

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/protosw.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/socket.h> /* for net/if.h */
#include <sys/sockio.h>
#include <sys/ctype.h>  /* string functions */
#include <sys/kernel.h>
#include <sys/random.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/mpipe.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/pfil.h>
#include <net/ifq_var.h>
#include <net/if_clone.h>

#include <netinet/in.h> /* for struct arpcom */
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif
#include <netinet/if_ether.h> /* for struct arpcom */
#include <net/bridge/if_bridgevar.h>
#include <net/if_llc.h>
#include <net/netmsg2.h>

#include <net/route.h>
#include <sys/in_cksum.h>

/*
 * Size of the route hash table.  Must be a power of two.
 */
#ifndef BRIDGE_RTHASH_SIZE
#define	BRIDGE_RTHASH_SIZE		1024
#endif

#define	BRIDGE_RTHASH_MASK		(BRIDGE_RTHASH_SIZE - 1)

/*
 * Maximum number of addresses to cache.
 */
#ifndef BRIDGE_RTABLE_MAX
#define	BRIDGE_RTABLE_MAX		100
#endif

/*
 * Spanning tree defaults.
 */
#define	BSTP_DEFAULT_MAX_AGE		(20 * 256)
#define	BSTP_DEFAULT_HELLO_TIME		(2 * 256)
#define	BSTP_DEFAULT_FORWARD_DELAY	(15 * 256)
#define	BSTP_DEFAULT_HOLD_TIME		(1 * 256)
#define	BSTP_DEFAULT_BRIDGE_PRIORITY	0x8000
#define	BSTP_DEFAULT_PORT_PRIORITY	0x80
#define	BSTP_DEFAULT_PATH_COST		55

/*
 * Timeout (in seconds) for entries learned dynamically.
 */
#ifndef BRIDGE_RTABLE_TIMEOUT
#define	BRIDGE_RTABLE_TIMEOUT		(20 * 60)	/* same as ARP */
#endif

/*
 * Number of seconds between walks of the route list.
 */
#ifndef BRIDGE_RTABLE_PRUNE_PERIOD
#define	BRIDGE_RTABLE_PRUNE_PERIOD	(5 * 60)
#endif

/*
 * List of capabilities to mask on the member interface.
 */
#define	BRIDGE_IFCAPS_MASK		IFCAP_TXCSUM

eventhandler_tag	bridge_detach_cookie = NULL;

extern	struct mbuf *(*bridge_input_p)(struct ifnet *, struct mbuf *);
extern	int (*bridge_output_p)(struct ifnet *, struct mbuf *);
extern	void (*bridge_dn_p)(struct mbuf *, struct ifnet *);

typedef int	(*bridge_ctl_t)(struct bridge_softc *, void *);

static int	bridge_rtable_prune_period = BRIDGE_RTABLE_PRUNE_PERIOD;

static int	bridge_clone_create(struct if_clone *, int);
static void	bridge_clone_destroy(struct ifnet *);

static int	bridge_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bridge_mutecaps(struct bridge_iflist *, int);
static void	bridge_ifdetach(void *arg __unused, struct ifnet *);
static void	bridge_init(void *);
static void	bridge_stop(struct ifnet *);
static void	bridge_start(struct ifnet *);
static struct mbuf *bridge_input(struct ifnet *, struct mbuf *);
static int	bridge_output(struct ifnet *, struct mbuf *);

static void	bridge_forward(struct bridge_softc *, struct mbuf *m);

static void	bridge_timer(void *);

static void	bridge_broadcast(struct bridge_softc *, struct ifnet *,
		    struct mbuf *, int);
static void	bridge_span(struct bridge_softc *, struct mbuf *);

static int	bridge_rtupdate(struct bridge_softc *, const uint8_t *,
		    struct ifnet *, int, uint8_t);
static struct ifnet *bridge_rtlookup(struct bridge_softc *, const uint8_t *);
static void	bridge_rttrim(struct bridge_softc *);
static void	bridge_rtage(struct bridge_softc *);
static void	bridge_rtflush(struct bridge_softc *, int);
static int	bridge_rtdaddr(struct bridge_softc *, const uint8_t *);

static int	bridge_rtable_init(struct bridge_softc *);
static void	bridge_rtable_fini(struct bridge_softc *);

static int	bridge_rtnode_addr_cmp(const uint8_t *, const uint8_t *);
static struct bridge_rtnode *bridge_rtnode_lookup(struct bridge_softc *,
		    const uint8_t *);
static int	bridge_rtnode_insert(struct bridge_softc *,
		    struct bridge_rtnode *);
static void	bridge_rtnode_destroy(struct bridge_softc *,
		    struct bridge_rtnode *);

static struct bridge_iflist *bridge_lookup_member(struct bridge_softc *,
		    const char *name);
static struct bridge_iflist *bridge_lookup_member_if(struct bridge_softc *,
		    struct ifnet *ifp);
static void	bridge_delete_member(struct bridge_softc *,
		    struct bridge_iflist *, int);
static void	bridge_delete_span(struct bridge_softc *,
		    struct bridge_iflist *);

static int	bridge_control(struct bridge_softc *, u_long,
			       bridge_ctl_t, void *);
static int	bridge_ioctl_add(struct bridge_softc *, void *);
static int	bridge_ioctl_del(struct bridge_softc *, void *);
static int	bridge_ioctl_gifflags(struct bridge_softc *, void *);
static int	bridge_ioctl_sifflags(struct bridge_softc *, void *);
static int	bridge_ioctl_scache(struct bridge_softc *, void *);
static int	bridge_ioctl_gcache(struct bridge_softc *, void *);
static int	bridge_ioctl_gifs(struct bridge_softc *, void *);
static int	bridge_ioctl_rts(struct bridge_softc *, void *);
static int	bridge_ioctl_saddr(struct bridge_softc *, void *);
static int	bridge_ioctl_sto(struct bridge_softc *, void *);
static int	bridge_ioctl_gto(struct bridge_softc *, void *);
static int	bridge_ioctl_daddr(struct bridge_softc *, void *);
static int	bridge_ioctl_flush(struct bridge_softc *, void *);
static int	bridge_ioctl_gpri(struct bridge_softc *, void *);
static int	bridge_ioctl_spri(struct bridge_softc *, void *);
static int	bridge_ioctl_ght(struct bridge_softc *, void *);
static int	bridge_ioctl_sht(struct bridge_softc *, void *);
static int	bridge_ioctl_gfd(struct bridge_softc *, void *);
static int	bridge_ioctl_sfd(struct bridge_softc *, void *);
static int	bridge_ioctl_gma(struct bridge_softc *, void *);
static int	bridge_ioctl_sma(struct bridge_softc *, void *);
static int	bridge_ioctl_sifprio(struct bridge_softc *, void *);
static int	bridge_ioctl_sifcost(struct bridge_softc *, void *);
static int	bridge_ioctl_addspan(struct bridge_softc *, void *);
static int	bridge_ioctl_delspan(struct bridge_softc *, void *);
static int	bridge_pfil(struct mbuf **, struct ifnet *, struct ifnet *,
		    int);
static int	bridge_ip_checkbasic(struct mbuf **mp);
#ifdef INET6
static int	bridge_ip6_checkbasic(struct mbuf **mp);
#endif /* INET6 */
static int	bridge_fragment(struct ifnet *, struct mbuf *,
		    struct ether_header *, int, struct llc *);
static void	bridge_enqueue_internal(struct ifnet *, struct mbuf *m,
					netisr_fn_t);
static void	bridge_enqueue_handler(struct netmsg *);
static void	bridge_pfil_enqueue_handler(struct netmsg *);
static void	bridge_pfil_enqueue(struct ifnet *, struct mbuf *, int);
static void	bridge_handoff_notags(struct ifnet *, struct mbuf *);
static void	bridge_handoff(struct ifnet *, struct mbuf *);

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_BRIDGE, bridge, CTLFLAG_RW, 0, "Bridge");

static int pfil_onlyip = 1; /* only pass IP[46] packets when pfil is enabled */
static int pfil_bridge = 1; /* run pfil hooks on the bridge interface */
static int pfil_member = 1; /* run pfil hooks on the member interface */
SYSCTL_INT(_net_link_bridge, OID_AUTO, pfil_onlyip, CTLFLAG_RW,
    &pfil_onlyip, 0, "Only pass IP packets when pfil is enabled");
SYSCTL_INT(_net_link_bridge, OID_AUTO, pfil_bridge, CTLFLAG_RW,
    &pfil_bridge, 0, "Packet filter on the bridge interface");
SYSCTL_INT(_net_link_bridge, OID_AUTO, pfil_member, CTLFLAG_RW,
    &pfil_member, 0, "Packet filter on the member interface");

struct bridge_control {
	bridge_ctl_t	bc_func;
	int		bc_argsize;
	int		bc_flags;
};

#define	BC_F_COPYIN		0x01	/* copy arguments in */
#define	BC_F_COPYOUT		0x02	/* copy arguments out */
#define	BC_F_SUSER		0x04	/* do super-user check */

const struct bridge_control bridge_control_table[] = {
	{ bridge_ioctl_add,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },
	{ bridge_ioctl_del,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_gifflags,	sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_COPYOUT },
	{ bridge_ioctl_sifflags,	sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_scache,		sizeof(struct ifbrparam),
	  BC_F_COPYIN|BC_F_SUSER },
	{ bridge_ioctl_gcache,		sizeof(struct ifbrparam),
	  BC_F_COPYOUT },

	{ bridge_ioctl_gifs,		sizeof(struct ifbifconf),
	  BC_F_COPYIN|BC_F_COPYOUT },
	{ bridge_ioctl_rts,		sizeof(struct ifbaconf),
	  BC_F_COPYIN|BC_F_COPYOUT },

	{ bridge_ioctl_saddr,		sizeof(struct ifbareq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_sto,		sizeof(struct ifbrparam),
	  BC_F_COPYIN|BC_F_SUSER },
	{ bridge_ioctl_gto,		sizeof(struct ifbrparam),
	  BC_F_COPYOUT },

	{ bridge_ioctl_daddr,		sizeof(struct ifbareq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_flush,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_gpri,		sizeof(struct ifbrparam),
	  BC_F_COPYOUT },
	{ bridge_ioctl_spri,		sizeof(struct ifbrparam),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_ght,		sizeof(struct ifbrparam),
	  BC_F_COPYOUT },
	{ bridge_ioctl_sht,		sizeof(struct ifbrparam),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_gfd,		sizeof(struct ifbrparam),
	  BC_F_COPYOUT },
	{ bridge_ioctl_sfd,		sizeof(struct ifbrparam),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_gma,		sizeof(struct ifbrparam),
	  BC_F_COPYOUT },
	{ bridge_ioctl_sma,		sizeof(struct ifbrparam),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_sifprio,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_sifcost,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },

	{ bridge_ioctl_addspan,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },
	{ bridge_ioctl_delspan,		sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },
};
const int bridge_control_table_size =
    sizeof(bridge_control_table) / sizeof(bridge_control_table[0]);

LIST_HEAD(, bridge_softc) bridge_list;

struct if_clone bridge_cloner = IF_CLONE_INITIALIZER("bridge",
				bridge_clone_create,
				bridge_clone_destroy, 0, IF_MAXUNIT);

static int
bridge_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		LIST_INIT(&bridge_list);
		if_clone_attach(&bridge_cloner);
		bridge_input_p = bridge_input;
		bridge_output_p = bridge_output;
		bridge_detach_cookie = EVENTHANDLER_REGISTER(
		    ifnet_detach_event, bridge_ifdetach, NULL,
		    EVENTHANDLER_PRI_ANY);
#if notyet
		bstp_linkstate_p = bstp_linkstate;
#endif
		break;
	case MOD_UNLOAD:
		if (!LIST_EMPTY(&bridge_list))
			return (EBUSY);
		EVENTHANDLER_DEREGISTER(ifnet_detach_event,
		    bridge_detach_cookie);
		if_clone_detach(&bridge_cloner);
		bridge_input_p = NULL;
		bridge_output_p = NULL;
#if notyet
		bstp_linkstate_p = NULL;
#endif
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t bridge_mod = {
	"if_bridge",
	bridge_modevent,
	0
};

DECLARE_MODULE(if_bridge, bridge_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);


/*
 * bridge_clone_create:
 *
 *	Create a new bridge instance.
 */
static int
bridge_clone_create(struct if_clone *ifc, int unit)
{
	struct bridge_softc *sc;
	struct ifnet *ifp;
	u_char eaddr[6];

	sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = sc->sc_ifp = &sc->sc_if;

	sc->sc_brtmax = BRIDGE_RTABLE_MAX;
	sc->sc_brttimeout = BRIDGE_RTABLE_TIMEOUT;
	sc->sc_bridge_max_age = BSTP_DEFAULT_MAX_AGE;
	sc->sc_bridge_hello_time = BSTP_DEFAULT_HELLO_TIME;
	sc->sc_bridge_forward_delay = BSTP_DEFAULT_FORWARD_DELAY;
	sc->sc_bridge_priority = BSTP_DEFAULT_BRIDGE_PRIORITY;
	sc->sc_hold_time = BSTP_DEFAULT_HOLD_TIME;

	/* Initialize our routing table. */
	bridge_rtable_init(sc);

	callout_init(&sc->sc_brcallout);
	callout_init(&sc->sc_bstpcallout);

	LIST_INIT(&sc->sc_iflist);
	LIST_INIT(&sc->sc_spanlist);

	ifp->if_softc = sc;
	if_initname(ifp, ifc->ifc_name, unit);
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_ioctl = bridge_ioctl;
	ifp->if_start = bridge_start;
	ifp->if_init = bridge_init;
	ifp->if_type = IFT_BRIDGE;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	ifq_set_ready(&ifp->if_snd);
	ifp->if_hdrlen = ETHER_HDR_LEN;

	/*
	 * Generate a random ethernet address and use the private AC:DE:48
	 * OUI code.
	 */
	{
		int rnd = karc4random();
		bcopy(&rnd, &eaddr[0], 4); /* ETHER_ADDR_LEN == 6 */
		rnd = karc4random();
		bcopy(&rnd, &eaddr[2], 4); /* ETHER_ADDR_LEN == 6 */
	}
	eaddr[0] &= ~1;		/* clear multicast bit */
	eaddr[0] |= 2;		/* set the LAA bit */

	ether_ifattach(ifp, eaddr, NULL);
	/* Now undo some of the damage... */
	ifp->if_baudrate = 0;
	ifp->if_type = IFT_BRIDGE;

	crit_enter();	/* XXX MP */
	LIST_INSERT_HEAD(&bridge_list, sc, sc_list);
	crit_exit();

	return (0);
}

static void
bridge_delete_dispatch(struct netmsg *nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->nm_lmsg;
	struct bridge_softc *sc = lmsg->u.ms_resultp;
	struct ifnet *bifp = sc->sc_ifp;
	struct bridge_iflist *bif;

	lwkt_serialize_enter(bifp->if_serializer);

	while ((bif = LIST_FIRST(&sc->sc_iflist)) != NULL)
		bridge_delete_member(sc, bif, 0);

	while ((bif = LIST_FIRST(&sc->sc_spanlist)) != NULL)
		bridge_delete_span(sc, bif);

	lwkt_serialize_exit(bifp->if_serializer);

	lwkt_replymsg(lmsg, 0);
}

/*
 * bridge_clone_destroy:
 *
 *	Destroy a bridge instance.
 */
static void
bridge_clone_destroy(struct ifnet *ifp)
{
	struct bridge_softc *sc = ifp->if_softc;
	struct lwkt_msg *lmsg;
	struct netmsg nmsg;

	lwkt_serialize_enter(ifp->if_serializer);

	bridge_stop(ifp);
	ifp->if_flags &= ~IFF_UP;

	callout_stop(&sc->sc_brcallout);
	callout_stop(&sc->sc_bstpcallout);

	lwkt_serialize_exit(ifp->if_serializer);

	netmsg_init(&nmsg, &curthread->td_msgport, 0, bridge_delete_dispatch);
	lmsg = &nmsg.nm_lmsg;
	lmsg->u.ms_resultp = sc;
	lwkt_domsg(cpu_portfn(0), lmsg, 0);

	crit_enter();	/* XXX MP */
	LIST_REMOVE(sc, sc_list);
	crit_exit();

	ether_ifdetach(ifp);

	/* Tear down the routing table. */
	bridge_rtable_fini(sc);

	kfree(sc, M_DEVBUF);
}

/*
 * bridge_ioctl:
 *
 *	Handle a control request from the operator.
 */
static int
bridge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct bridge_softc *sc = ifp->if_softc;
	union {
		struct ifbreq ifbreq;
		struct ifbifconf ifbifconf;
		struct ifbareq ifbareq;
		struct ifbaconf ifbaconf;
		struct ifbrparam ifbrparam;
	} args;
	struct ifdrv *ifd = (struct ifdrv *) data;
	const struct bridge_control *bc;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCGDRVSPEC:
	case SIOCSDRVSPEC:
		if (ifd->ifd_cmd >= bridge_control_table_size) {
			error = EINVAL;
			break;
		}
		bc = &bridge_control_table[ifd->ifd_cmd];

		if (cmd == SIOCGDRVSPEC &&
		    (bc->bc_flags & BC_F_COPYOUT) == 0) {
			error = EINVAL;
			break;
		} else if (cmd == SIOCSDRVSPEC &&
		    (bc->bc_flags & BC_F_COPYOUT) != 0) {
			error = EINVAL;
			break;
		}

		if (bc->bc_flags & BC_F_SUSER) {
			error = suser_cred(cr, NULL_CRED_OKAY);
			if (error)
				break;
		}

		if (ifd->ifd_len != bc->bc_argsize ||
		    ifd->ifd_len > sizeof(args)) {
			error = EINVAL;
			break;
		}

		memset(&args, 0, sizeof(args));
		if (bc->bc_flags & BC_F_COPYIN) {
			error = copyin(ifd->ifd_data, &args, ifd->ifd_len);
			if (error)
				break;
		}

		error = bridge_control(sc, cmd, bc->bc_func, &args);
		if (error)
			break;

		if (bc->bc_flags & BC_F_COPYOUT)
			error = copyout(&args, ifd->ifd_data, ifd->ifd_len);
		break;

	case SIOCSIFFLAGS:
		if (!(ifp->if_flags & IFF_UP) &&
		    (ifp->if_flags & IFF_RUNNING)) {
			/*
			 * If interface is marked down and it is running,
			 * then stop it.
			 */
			bridge_stop(ifp);
		} else if ((ifp->if_flags & IFF_UP) &&
		    !(ifp->if_flags & IFF_RUNNING)) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			ifp->if_init(sc);
		}
		break;

	case SIOCSIFMTU:
		/* Do not allow the MTU to be changed on the bridge */
		error = EINVAL;
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

/*
 * bridge_mutecaps:
 *
 *	Clear or restore unwanted capabilities on the member interface
 */
static void
bridge_mutecaps(struct bridge_iflist *bif, int mute)
{
	struct ifnet *ifp = bif->bif_ifp;
	struct ifreq ifr;
	int error;

	if (ifp->if_ioctl == NULL)
		return;

	bzero(&ifr, sizeof(ifr));
	ifr.ifr_reqcap = ifp->if_capenable;

	if (mute) {
		/* mask off and save capabilities */
		bif->bif_mutecap = ifr.ifr_reqcap & BRIDGE_IFCAPS_MASK;
		if (bif->bif_mutecap != 0)
			ifr.ifr_reqcap &= ~BRIDGE_IFCAPS_MASK;
	} else {
		/* restore muted capabilities */
		ifr.ifr_reqcap |= bif->bif_mutecap;
	}

	if (bif->bif_mutecap != 0) {
		lwkt_serialize_enter(ifp->if_serializer);
		error = ifp->if_ioctl(ifp, SIOCSIFCAP, (caddr_t)&ifr, NULL);
		lwkt_serialize_exit(ifp->if_serializer);
	}
}

/*
 * bridge_lookup_member:
 *
 *	Lookup a bridge member interface.
 */
static struct bridge_iflist *
bridge_lookup_member(struct bridge_softc *sc, const char *name)
{
	struct bridge_iflist *bif;
	struct ifnet *ifp;

	LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
		ifp = bif->bif_ifp;
		if (strcmp(ifp->if_xname, name) == 0)
			return (bif);
	}

	return (NULL);
}

/*
 * bridge_lookup_member_if:
 *
 *	Lookup a bridge member interface by ifnet*.
 */
static struct bridge_iflist *
bridge_lookup_member_if(struct bridge_softc *sc, struct ifnet *member_ifp)
{
	struct bridge_iflist *bif;

	LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
		if (bif->bif_ifp == member_ifp)
			return (bif);
	}

	return (NULL);
}

/*
 * bridge_delete_member:
 *
 *	Delete the specified member interface.
 */
static void
bridge_delete_member(struct bridge_softc *sc, struct bridge_iflist *bif,
    int gone)
{
	struct ifnet *ifs = bif->bif_ifp;
	struct ifnet *bifp = sc->sc_ifp;

	ASSERT_SERIALIZED(bifp->if_serializer);

	ifs->if_bridge = NULL;

	/*
	 * Release bridge interface's serializer:
	 * - To avoid possible dead lock.
	 * - netmsg_service_sync will block current thread.
	 */
	lwkt_serialize_exit(bifp->if_serializer);

	/*
	 * Make sure that all protocol threads see 'ifs' if_bridge change.
	 */
	netmsg_service_sync();

	if (!gone) {
		switch (ifs->if_type) {
		case IFT_ETHER:
		case IFT_L2VLAN:
			/*
			 * Take the interface out of promiscuous mode.
			 */
			ifpromisc(ifs, 0);
			bridge_mutecaps(bif, 0);
			break;

		case IFT_GIF:
			break;

		default:
			panic("bridge_delete_member: impossible");
			break;
		}
	}

	lwkt_serialize_enter(bifp->if_serializer);

	LIST_REMOVE(bif, bif_next);

	bridge_rtdelete(sc, ifs, IFBF_FLUSHALL);

	kfree(bif, M_DEVBUF);

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);
}

/*
 * bridge_delete_span:
 *
 *	Delete the specified span interface.
 */
static void
bridge_delete_span(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	KASSERT(bif->bif_ifp->if_bridge == NULL,
	    ("%s: not a span interface", __func__));

	LIST_REMOVE(bif, bif_next);
	kfree(bif, M_DEVBUF);
}

static int
bridge_ioctl_add(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif = NULL;
	struct ifnet *ifs, *bifp;
	int error = 0;

	bifp = sc->sc_ifp;
	ASSERT_SERIALIZED(bifp->if_serializer);

	ifs = ifunit(req->ifbr_ifsname);
	if (ifs == NULL)
		return (ENOENT);

	/* If it's in the span list, it can't be a member. */
	LIST_FOREACH(bif, &sc->sc_spanlist, bif_next)
		if (ifs == bif->bif_ifp)
			return (EBUSY);

	/* Allow the first Ethernet member to define the MTU */
	if (ifs->if_type != IFT_GIF) {
		if (LIST_EMPTY(&sc->sc_iflist)) {
			bifp->if_mtu = ifs->if_mtu;
		} else if (bifp->if_mtu != ifs->if_mtu) {
			if_printf(bifp, "invalid MTU for %s\n", ifs->if_xname);
			return (EINVAL);
		}
	}

	if (ifs->if_bridge == sc)
		return (EEXIST);

	if (ifs->if_bridge != NULL)
		return (EBUSY);

	bif = kmalloc(sizeof(*bif), M_DEVBUF, M_WAITOK|M_ZERO);
	bif->bif_ifp = ifs;
	bif->bif_flags = IFBIF_LEARNING | IFBIF_DISCOVER;
	bif->bif_priority = BSTP_DEFAULT_PORT_PRIORITY;
	bif->bif_path_cost = BSTP_DEFAULT_PATH_COST;

	switch (ifs->if_type) {
	case IFT_ETHER:
	case IFT_L2VLAN:
		/*
		 * Release bridge interface's serializer to
		 * avoid possible dead lock.
		 */
		lwkt_serialize_exit(bifp->if_serializer);

		/*
		 * Place the interface into promiscuous mode.
		 */
		error = ifpromisc(ifs, 1);
		if (error) {
			lwkt_serialize_enter(bifp->if_serializer);
			goto out;
		}

		bridge_mutecaps(bif, 1);

		lwkt_serialize_enter(bifp->if_serializer);
		break;

	case IFT_GIF: /* :^) */
		break;

	default:
		error = EINVAL;
		goto out;
	}

	LIST_INSERT_HEAD(&sc->sc_iflist, bif, bif_next);

	if (bifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);
	else
		bstp_stop(sc);

	/*
	 * Everything has been setup, so let the member interface
	 * deliver packets to this bridge on its input/output path.
	 */
	ifs->if_bridge = sc;
out:
	if (error) {
		if (bif != NULL)
			kfree(bif, M_DEVBUF);
	}
	return (error);
}

static int
bridge_ioctl_del(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	bridge_delete_member(sc, bif, 0);

	return (0);
}

static int
bridge_ioctl_gifflags(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	req->ifbr_ifsflags = bif->bif_flags;
	req->ifbr_state = bif->bif_state;
	req->ifbr_priority = bif->bif_priority;
	req->ifbr_path_cost = bif->bif_path_cost;
	req->ifbr_portno = bif->bif_ifp->if_index & 0xff;

	return (0);
}

static int
bridge_ioctl_sifflags(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	if (req->ifbr_ifsflags & IFBIF_SPAN)
		/* SPAN is readonly */
		return (EINVAL);

	if (req->ifbr_ifsflags & IFBIF_STP) {
		switch (bif->bif_ifp->if_type) {
		case IFT_ETHER:
			/* These can do spanning tree. */
			break;

		default:
			/* Nothing else can. */
			return (EINVAL);
		}
	}

	bif->bif_flags = req->ifbr_ifsflags;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_scache(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	sc->sc_brtmax = param->ifbrp_csize;
	bridge_rttrim(sc);

	return (0);
}

static int
bridge_ioctl_gcache(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	param->ifbrp_csize = sc->sc_brtmax;

	return (0);
}

static int
bridge_ioctl_gifs(struct bridge_softc *sc, void *arg)
{
	struct ifbifconf *bifc = arg;
	struct bridge_iflist *bif;
	struct ifbreq breq;
	int count, len, error = 0;

	count = 0;
	LIST_FOREACH(bif, &sc->sc_iflist, bif_next)
		count++;
	LIST_FOREACH(bif, &sc->sc_spanlist, bif_next)
		count++;

	if (bifc->ifbic_len == 0) {
		bifc->ifbic_len = sizeof(breq) * count;
		return (0);
	}

	count = 0;
	len = bifc->ifbic_len;
	memset(&breq, 0, sizeof breq);
	LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
		if (len < sizeof(breq))
			break;

		strlcpy(breq.ifbr_ifsname, bif->bif_ifp->if_xname,
		    sizeof(breq.ifbr_ifsname));
		breq.ifbr_ifsflags = bif->bif_flags;
		breq.ifbr_state = bif->bif_state;
		breq.ifbr_priority = bif->bif_priority;
		breq.ifbr_path_cost = bif->bif_path_cost;
		breq.ifbr_portno = bif->bif_ifp->if_index & 0xff;
		error = copyout(&breq, bifc->ifbic_req + count, sizeof(breq));
		if (error)
			break;
		count++;
		len -= sizeof(breq);
	}
	LIST_FOREACH(bif, &sc->sc_spanlist, bif_next) {
		if (len < sizeof(breq))
			break;

		strlcpy(breq.ifbr_ifsname, bif->bif_ifp->if_xname,
		    sizeof(breq.ifbr_ifsname));
		breq.ifbr_ifsflags = bif->bif_flags;
		breq.ifbr_state = bif->bif_state;
		breq.ifbr_priority = bif->bif_priority;
		breq.ifbr_path_cost = bif->bif_path_cost;
		breq.ifbr_portno = bif->bif_ifp->if_index & 0xff;
		error = copyout(&breq, bifc->ifbic_req + count, sizeof(breq));
		if (error)
			break;
		count++;
		len -= sizeof(breq);
	}

	bifc->ifbic_len = sizeof(breq) * count;
	return (error);
}

static int
bridge_ioctl_rts(struct bridge_softc *sc, void *arg)
{
	struct ifbaconf *bac = arg;
	struct bridge_rtnode *brt;
	struct ifbareq bareq;
	int count = 0, error = 0, len;

	if (bac->ifbac_len == 0)
		return (0);

	len = bac->ifbac_len;
	memset(&bareq, 0, sizeof(bareq));
	LIST_FOREACH(brt, &sc->sc_rtlist, brt_list) {
		if (len < sizeof(bareq))
			goto out;
		strlcpy(bareq.ifba_ifsname, brt->brt_ifp->if_xname,
		    sizeof(bareq.ifba_ifsname));
		memcpy(bareq.ifba_dst, brt->brt_addr, sizeof(brt->brt_addr));
		if ((brt->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC &&
				time_second < brt->brt_expire)
			bareq.ifba_expire = brt->brt_expire - time_second;
		else
			bareq.ifba_expire = 0;
		bareq.ifba_flags = brt->brt_flags;

		error = copyout(&bareq, bac->ifbac_req + count, sizeof(bareq));
		if (error)
			goto out;
		count++;
		len -= sizeof(bareq);
	}
out:
	bac->ifbac_len = sizeof(bareq) * count;
	return (error);
}

static int
bridge_ioctl_saddr(struct bridge_softc *sc, void *arg)
{
	struct ifbareq *req = arg;
	struct bridge_iflist *bif;
	int error;

	bif = bridge_lookup_member(sc, req->ifba_ifsname);
	if (bif == NULL)
		return (ENOENT);

	error = bridge_rtupdate(sc, req->ifba_dst, bif->bif_ifp, 1,
	    req->ifba_flags);

	return (error);
}

static int
bridge_ioctl_sto(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	sc->sc_brttimeout = param->ifbrp_ctime;

	return (0);
}

static int
bridge_ioctl_gto(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	param->ifbrp_ctime = sc->sc_brttimeout;

	return (0);
}

static int
bridge_ioctl_daddr(struct bridge_softc *sc, void *arg)
{
	struct ifbareq *req = arg;

	return (bridge_rtdaddr(sc, req->ifba_dst));
}

static int
bridge_ioctl_flush(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;

	bridge_rtflush(sc, req->ifbr_ifsflags);

	return (0);
}

static int
bridge_ioctl_gpri(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	param->ifbrp_prio = sc->sc_bridge_priority;

	return (0);
}

static int
bridge_ioctl_spri(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	sc->sc_bridge_priority = param->ifbrp_prio;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_ght(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	param->ifbrp_hellotime = sc->sc_bridge_hello_time >> 8;

	return (0);
}

static int
bridge_ioctl_sht(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	if (param->ifbrp_hellotime == 0)
		return (EINVAL);
	sc->sc_bridge_hello_time = param->ifbrp_hellotime << 8;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_gfd(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	param->ifbrp_fwddelay = sc->sc_bridge_forward_delay >> 8;

	return (0);
}

static int
bridge_ioctl_sfd(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	if (param->ifbrp_fwddelay == 0)
		return (EINVAL);
	sc->sc_bridge_forward_delay = param->ifbrp_fwddelay << 8;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_gma(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	param->ifbrp_maxage = sc->sc_bridge_max_age >> 8;

	return (0);
}

static int
bridge_ioctl_sma(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;

	if (param->ifbrp_maxage == 0)
		return (EINVAL);
	sc->sc_bridge_max_age = param->ifbrp_maxage << 8;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_sifprio(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	bif->bif_priority = req->ifbr_priority;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_sifcost(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	bif->bif_path_cost = req->ifbr_path_cost;

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_addspan(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif = NULL;
	struct ifnet *ifs;

	ifs = ifunit(req->ifbr_ifsname);
	if (ifs == NULL)
		return (ENOENT);

	LIST_FOREACH(bif, &sc->sc_spanlist, bif_next)
		if (ifs == bif->bif_ifp)
			return (EBUSY);

	if (ifs->if_bridge != NULL)
		return (EBUSY);

	switch (ifs->if_type) {
		case IFT_ETHER:
		case IFT_GIF:
		case IFT_L2VLAN:
			break;
		default:
			return (EINVAL);
	}

	bif = kmalloc(sizeof(*bif), M_DEVBUF, M_WAITOK|M_ZERO);

	bif->bif_ifp = ifs;
	bif->bif_flags = IFBIF_SPAN;

	LIST_INSERT_HEAD(&sc->sc_spanlist, bif, bif_next);

	return (0);
}

static int
bridge_ioctl_delspan(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;
	struct ifnet *ifs;

	ifs = ifunit(req->ifbr_ifsname);
	if (ifs == NULL)
		return (ENOENT);

	LIST_FOREACH(bif, &sc->sc_spanlist, bif_next)
		if (ifs == bif->bif_ifp)
			break;

	if (bif == NULL)
		return (ENOENT);

	bridge_delete_span(sc, bif);

	return (0);
}

static void
bridge_ifdetach_dispatch(struct netmsg *nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->nm_lmsg;
	struct ifnet *ifp, *bifp;
	struct bridge_softc *sc;
	struct bridge_iflist *bif;

	ifp = lmsg->u.ms_resultp;
	sc = ifp->if_bridge;

	/* Check if the interface is a bridge member */
	if (sc != NULL) {
		bifp = sc->sc_ifp;

		lwkt_serialize_enter(bifp->if_serializer);

		bif = bridge_lookup_member_if(sc, ifp);
		if (bif != NULL) {
			bridge_delete_member(sc, bif, 1);
		} else {
			/* XXX Why bif will be NULL? */
		}

		lwkt_serialize_exit(bifp->if_serializer);
		goto reply;
	}

	crit_enter();	/* XXX MP */

	/* Check if the interface is a span port */
	LIST_FOREACH(sc, &bridge_list, sc_list) {
		bifp = sc->sc_ifp;

		lwkt_serialize_enter(bifp->if_serializer);

		LIST_FOREACH(bif, &sc->sc_spanlist, bif_next)
			if (ifp == bif->bif_ifp) {
				bridge_delete_span(sc, bif);
				break;
			}

		lwkt_serialize_exit(bifp->if_serializer);
	}

	crit_exit();

reply:
	lwkt_replymsg(lmsg, 0);
}

/*
 * bridge_ifdetach:
 *
 *	Detach an interface from a bridge.  Called when a member
 *	interface is detaching.
 */
static void
bridge_ifdetach(void *arg __unused, struct ifnet *ifp)
{
	struct lwkt_msg *lmsg;
	struct netmsg nmsg;

	netmsg_init(&nmsg, &curthread->td_msgport, 0, bridge_ifdetach_dispatch);
	lmsg = &nmsg.nm_lmsg;
	lmsg->u.ms_resultp = ifp;

	lwkt_domsg(cpu_portfn(0), lmsg, 0);
}

/*
 * bridge_init:
 *
 *	Initialize a bridge interface.
 */
static void
bridge_init(void *xsc)
{
	struct bridge_softc *sc = (struct bridge_softc *)xsc;
	struct ifnet *ifp = sc->sc_ifp;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (ifp->if_flags & IFF_RUNNING)
		return;

	callout_reset(&sc->sc_brcallout, bridge_rtable_prune_period * hz,
	    bridge_timer, sc);

	ifp->if_flags |= IFF_RUNNING;
	bstp_initialization(sc);
	return;
}

/*
 * bridge_stop:
 *
 *	Stop the bridge interface.
 */
static void
bridge_stop(struct ifnet *ifp)
{
	struct bridge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	callout_stop(&sc->sc_brcallout);
	bstp_stop(sc);

	bridge_rtflush(sc, IFBF_FLUSHDYN);

	ifp->if_flags &= ~IFF_RUNNING;
}

static void
bridge_enqueue_internal(struct ifnet *dst_ifp, struct mbuf *m,
			netisr_fn_t handler)
{
	struct netmsg_packet *nmp;
	lwkt_port_t port;
	int cpu = mycpu->gd_cpuid;

	while (m->m_type == MT_TAG) {
		/* XXX see ether_output_frame for full rules check */
		m = m->m_next;
	}

	nmp = &m->m_hdr.mh_netmsg;
	netmsg_init(&nmp->nm_netmsg, &netisr_apanic_rport, 0, handler);
	nmp->nm_packet = m;
	nmp->nm_netmsg.nm_lmsg.u.ms_resultp = dst_ifp;

	port = cpu_portfn(cpu);
	lwkt_sendmsg(port, &nmp->nm_netmsg.nm_lmsg);
}

static void
bridge_pfil_enqueue(struct ifnet *dst_ifp, struct mbuf *m,
		    int runfilt)
{
	netisr_fn_t handler;

	if (runfilt && (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
	    || inet6_pfil_hook.ph_hashooks > 0
#endif
	    )) {
		handler = bridge_pfil_enqueue_handler;
	} else {
		handler = bridge_enqueue_handler;
	}
	bridge_enqueue_internal(dst_ifp, m, handler);
}

/*
 * bridge_enqueue:
 *
 *	Enqueue a packet on a bridge member interface.
 *
 */
void
bridge_enqueue(struct ifnet *dst_ifp, struct mbuf *m)
{
	bridge_enqueue_internal(dst_ifp, m, bridge_enqueue_handler);
}

/*
 * bridge_output:
 *
 *	Send output from a bridge member interface.  This
 *	performs the bridging function for locally originated
 *	packets.
 *
 *	The mbuf has the Ethernet header already attached.  We must
 *	enqueue or free the mbuf before returning.
 */
static int
bridge_output(struct ifnet *ifp, struct mbuf *m)
{
	struct bridge_softc *sc = ifp->if_bridge;
	struct ether_header *eh;
	struct ifnet *dst_if;

	ASSERT_NOT_SERIALIZED(ifp->if_serializer);

	/*
	 * Make sure that we are still a member of a bridge interface.
	 */
	if (sc == NULL) {
		m_freem(m);
		return (0);
	}

	if (m->m_len < ETHER_HDR_LEN) {
		m = m_pullup(m, ETHER_HDR_LEN);
		if (m == NULL)
			return (0);
	}

	/* Serialize our bridge interface. */
	lwkt_serialize_enter(sc->sc_ifp->if_serializer);

	eh = mtod(m, struct ether_header *);

	/*
	 * If bridge is down, but the original output interface is up,
	 * go ahead and send out that interface.  Otherwise, the packet
	 * is dropped below.
	 */
	if ((sc->sc_ifp->if_flags & IFF_RUNNING) == 0) {
		dst_if = ifp;
		goto sendunicast;
	}

	/*
	 * If the packet is a multicast, or we don't know a better way to
	 * get there, send to all interfaces.
	 */
	if (ETHER_IS_MULTICAST(eh->ether_dhost))
		dst_if = NULL;
	else
		dst_if = bridge_rtlookup(sc, eh->ether_dhost);
	if (dst_if == NULL) {
		struct bridge_iflist *bif;
		struct mbuf *mc;
		int used = 0;

		bridge_span(sc, m);

		LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
			dst_if = bif->bif_ifp;
			if ((dst_if->if_flags & IFF_RUNNING) == 0)
				continue;

			/*
			 * If this is not the original output interface,
			 * and the interface is participating in spanning
			 * tree, make sure the port is in a state that
			 * allows forwarding.
			 */
			if (dst_if != ifp &&
			    (bif->bif_flags & IFBIF_STP) != 0) {
				switch (bif->bif_state) {
				case BSTP_IFSTATE_BLOCKING:
				case BSTP_IFSTATE_LISTENING:
				case BSTP_IFSTATE_DISABLED:
					continue;
				}
			}

			if (LIST_NEXT(bif, bif_next) == NULL) {
				used = 1;
				mc = m;
			} else {
				mc = m_copypacket(m, MB_DONTWAIT);
				if (mc == NULL) {
					sc->sc_ifp->if_oerrors++;
					continue;
				}
			}
			bridge_enqueue(dst_if, mc);
		}
		if (used == 0)
			m_freem(m);
		lwkt_serialize_exit(sc->sc_ifp->if_serializer);
		return (0);
	}

sendunicast:
	/*
	 * XXX Spanning tree consideration here?
	 */

	bridge_span(sc, m);
	lwkt_serialize_exit(sc->sc_ifp->if_serializer);
	if ((dst_if->if_flags & IFF_RUNNING) == 0)
		m_freem(m);
	else
		bridge_enqueue(dst_if, m);
	return (0);
}

/*
 * bridge_start:
 *
 *	Start output on a bridge.
 *
 */
static void
bridge_start(struct ifnet *ifp)
{
	struct bridge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_flags |= IFF_OACTIVE;
	for (;;) {
		struct ifnet *dst_if = NULL;
		struct ether_header *eh;
		struct mbuf *m;

		m = ifq_dequeue(&ifp->if_snd, NULL);
		if (m == NULL)
			break;

		if (m->m_len < sizeof(*eh)) {
			m = m_pullup(m, sizeof(*eh));
			if (m == NULL) {
				ifp->if_oerrors++;
				continue;
			}
		}
		eh = mtod(m, struct ether_header *);

		BPF_MTAP(ifp, m);
		ifp->if_opackets++;

		if ((m->m_flags & (M_BCAST|M_MCAST)) == 0)
			dst_if = bridge_rtlookup(sc, eh->ether_dhost);

		if (dst_if == NULL)
			bridge_broadcast(sc, ifp, m, 0);
		else
			bridge_enqueue(dst_if, m);
	}
	ifp->if_flags &= ~IFF_OACTIVE;
}

/*
 * bridge_forward:
 *
 *	The forwarding function of the bridge.
 */
static void
bridge_forward(struct bridge_softc *sc, struct mbuf *m)
{
	struct bridge_iflist *bif;
	struct ifnet *src_if, *dst_if, *ifp;
	struct ether_header *eh;

	src_if = m->m_pkthdr.rcvif;
	ifp = sc->sc_ifp;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_ipackets++;
	ifp->if_ibytes += m->m_pkthdr.len;

	/*
	 * Look up the bridge_iflist.
	 */
	bif = bridge_lookup_member_if(sc, src_if);
	if (bif == NULL) {
		/* Interface is not a bridge member (anymore?) */
		m_freem(m);
		return;
	}

	if (bif->bif_flags & IFBIF_STP) {
		switch (bif->bif_state) {
		case BSTP_IFSTATE_BLOCKING:
		case BSTP_IFSTATE_LISTENING:
		case BSTP_IFSTATE_DISABLED:
			m_freem(m);
			return;
		}
	}

	eh = mtod(m, struct ether_header *);

	/*
	 * If the interface is learning, and the source
	 * address is valid and not multicast, record
	 * the address.
	 */
	if ((bif->bif_flags & IFBIF_LEARNING) != 0 &&
	    ETHER_IS_MULTICAST(eh->ether_shost) == 0 &&
	    (eh->ether_shost[0] == 0 &&
	     eh->ether_shost[1] == 0 &&
	     eh->ether_shost[2] == 0 &&
	     eh->ether_shost[3] == 0 &&
	     eh->ether_shost[4] == 0 &&
	     eh->ether_shost[5] == 0) == 0) {
		bridge_rtupdate(sc, eh->ether_shost, src_if, 0, IFBAF_DYNAMIC);
	}

	if ((bif->bif_flags & IFBIF_STP) != 0 &&
	    bif->bif_state == BSTP_IFSTATE_LEARNING) {
		m_freem(m);
		return;
	}

	/*
	 * At this point, the port either doesn't participate
	 * in spanning tree or it is in the forwarding state.
	 */

	/*
	 * If the packet is unicast, destined for someone on
	 * "this" side of the bridge, drop it.
	 */
	if ((m->m_flags & (M_BCAST|M_MCAST)) == 0) {
		dst_if = bridge_rtlookup(sc, eh->ether_dhost);
		if (src_if == dst_if) {
			m_freem(m);
			return;
		}
	} else {
		/* ...forward it to all interfaces. */
		sc->sc_ifp->if_imcasts++;
		dst_if = NULL;
	}

	if (dst_if == NULL) {
		bridge_broadcast(sc, src_if, m, 1);
		return;
	}

	/*
	 * At this point, we're dealing with a unicast frame
	 * going to a different interface.
	 */
	if ((dst_if->if_flags & IFF_RUNNING) == 0) {
		m_freem(m);
		return;
	}
	bif = bridge_lookup_member_if(sc, dst_if);
	if (bif == NULL) {
		/* Not a member of the bridge (anymore?) */
		m_freem(m);
		return;
	}

	if (bif->bif_flags & IFBIF_STP) {
		switch (bif->bif_state) {
		case BSTP_IFSTATE_DISABLED:
		case BSTP_IFSTATE_BLOCKING:
			m_freem(m);
			return;
		}
	}

	lwkt_serialize_exit(ifp->if_serializer);

	/* run the packet filter */
	if (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
	    || inet6_pfil_hook.ph_hashooks > 0
#endif
	    ) {
		if (bridge_pfil(&m, ifp, src_if, PFIL_IN) != 0)
			goto done;
		if (m == NULL)
			goto done;

		if (bridge_pfil(&m, ifp, dst_if, PFIL_OUT) != 0)
			goto done;
		if (m == NULL)
			goto done;
	}
	bridge_handoff(dst_if, m);

	/*
	 * ifp's serializer was held on entry and is expected to be held
	 * on return.
	 */
done:
	lwkt_serialize_enter(ifp->if_serializer);
}

/*
 * bridge_input:
 *
 *	Receive input from a member interface.  Queue the packet for
 *	bridging if it is not for us.
 */
static struct mbuf *
bridge_input(struct ifnet *ifp, struct mbuf *m)
{
	struct bridge_softc *sc = ifp->if_bridge;
	struct bridge_iflist *bif;
	struct ifnet *bifp, *new_ifp;
	struct ether_header *eh;
	struct mbuf *mc, *mc2;

	/*
	 * Make sure that we are still a member of a bridge interface.
	 */
	if (sc == NULL)
		return m;

	new_ifp = NULL;
	bifp = sc->sc_ifp;

	lwkt_serialize_enter(bifp->if_serializer);

	if ((bifp->if_flags & IFF_RUNNING) == 0)
		goto out;

	/*
	 * Implement support for bridge monitoring. If this flag has been
	 * set on this interface, discard the packet once we push it through
	 * the bpf(4) machinery, but before we do, increment the byte and
	 * packet counters associated with this interface.
	 */
	 if ((bifp->if_flags & IFF_MONITOR) != 0) {
	 	m->m_pkthdr.rcvif = bifp;
		BPF_MTAP(bifp, m);
		bifp->if_ipackets++;
		bifp->if_ibytes += m->m_pkthdr.len;
		m_freem(m);
		m = NULL;
		goto out;
	}

	eh = mtod(m, struct ether_header *);

	m->m_flags &= ~M_PROTO1; /* XXX Hack - loop prevention */

	if (memcmp(eh->ether_dhost, IF_LLADDR(bifp), ETHER_ADDR_LEN) == 0) {
		/*
		 * If the packet is for us, set the packets source as the
		 * bridge, and return the packet back to ifnet.if_input for
		 * local processing.
		 */
		KASSERT(bifp->if_bridge == NULL,
			("loop created in bridge_input"));
		new_ifp = bifp;
		goto out;
	}

	/*
	 * Tap all packets arriving on the bridge, no matter if
	 * they are local destinations or not.  In is in.
	 */
	BPF_MTAP(bifp, m);

	bif = bridge_lookup_member_if(sc, ifp);
	if (bif == NULL)
		goto out;

	bridge_span(sc, m);

	if (m->m_flags & (M_BCAST | M_MCAST)) {
		/* Tap off 802.1D packets; they do not get forwarded. */
		if (memcmp(eh->ether_dhost, bstp_etheraddr,
		    ETHER_ADDR_LEN) == 0) {
			m = bstp_input(sc, bif, m);
			KASSERT(m == NULL,
				("attempt to deliver 802.1D packet\n"));
			goto out;
		}

		if (bif->bif_flags & IFBIF_STP) {
			switch (bif->bif_state) {
			case BSTP_IFSTATE_BLOCKING:
			case BSTP_IFSTATE_LISTENING:
			case BSTP_IFSTATE_DISABLED:
				goto out;
			}
		}

		/*
		 * Make a deep copy of the packet and enqueue the copy
		 * for bridge processing; return the original packet for
		 * local processing.
		 */
		mc = m_dup(m, MB_DONTWAIT);
		if (mc == NULL)
			goto out;

		bridge_forward(sc, mc);

		/*
		 * Reinject the mbuf as arriving on the bridge so we have a
		 * chance at claiming multicast packets. We can not loop back
		 * here from ether_input as a bridge is never a member of a
		 * bridge.
		 */
		KASSERT(bifp->if_bridge == NULL,
			("loop created in bridge_input"));
		mc2 = m_dup(m, MB_DONTWAIT);
#ifdef notyet
		if (mc2 != NULL) {
			/* Keep the layer3 header aligned */
			int i = min(mc2->m_pkthdr.len, max_protohdr);
			mc2 = m_copyup(mc2, i, ETHER_ALIGN);
		}
#endif
		if (mc2 != NULL) {
			mc2->m_pkthdr.rcvif = bifp;
			bifp->if_ipackets++;
			bifp->if_input(bifp, mc2);
		}

		/* Return the original packet for local processing. */
		goto out;
	}

	if (bif->bif_flags & IFBIF_STP) {
		switch (bif->bif_state) {
		case BSTP_IFSTATE_BLOCKING:
		case BSTP_IFSTATE_LISTENING:
		case BSTP_IFSTATE_DISABLED:
			goto out;
		}
	}

	/*
	 * Unicast.  Make sure it's not for us.
	 */
	LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
		if (bif->bif_ifp->if_type != IFT_ETHER)
			continue;

		/* It is destined for us. */
		if (memcmp(IF_LLADDR(bif->bif_ifp), eh->ether_dhost,
		    ETHER_ADDR_LEN) == 0) {
			if (bif->bif_flags & IFBIF_LEARNING) {
				bridge_rtupdate(sc,
				    eh->ether_shost, ifp, 0, IFBAF_DYNAMIC);
			}

			if (bif->bif_ifp != ifp) {
				/* XXX loop prevention */
				m->m_flags |= M_PROTO1;
				new_ifp = bif->bif_ifp;
			}
			goto out;
		}

		/* We just received a packet that we sent out. */
		if (memcmp(IF_LLADDR(bif->bif_ifp), eh->ether_shost,
		    ETHER_ADDR_LEN) == 0) {
			m_freem(m);
			m = NULL;
			goto out;
		}
	}

	/* Perform the bridge forwarding function. */
	bridge_forward(sc, m);
	m = NULL;
out:
	lwkt_serialize_exit(bifp->if_serializer);

	if (new_ifp != NULL) {
		lwkt_serialize_enter(new_ifp->if_serializer);

		m->m_pkthdr.rcvif = new_ifp;
		new_ifp->if_ipackets++;
		new_ifp->if_input(new_ifp, m);
		m = NULL;

		lwkt_serialize_exit(new_ifp->if_serializer);
	}
	return (m);
}

/*
 * bridge_broadcast:
 *
 *	Send a frame to all interfaces that are members of
 *	the bridge, except for the one on which the packet
 *	arrived.
 */
static void
bridge_broadcast(struct bridge_softc *sc, struct ifnet *src_if,
    struct mbuf *m, int runfilt)
{
	struct bridge_iflist *bif;
	struct mbuf *mc;
	struct ifnet *dst_if, *bifp;
	int used = 0;

	bifp = sc->sc_ifp;

	ASSERT_SERIALIZED(bifp->if_serializer);

	/* run the packet filter */
	if (runfilt && (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
	    || inet6_pfil_hook.ph_hashooks > 0
#endif
	    )) {
		lwkt_serialize_exit(bifp->if_serializer);

		/* Filter on the bridge interface before broadcasting */

		if (bridge_pfil(&m, bifp, src_if, PFIL_IN) != 0)
			goto filt;
		if (m == NULL)
			goto filt;

		if (bridge_pfil(&m, bifp, NULL, PFIL_OUT) != 0)
			m = NULL;
filt:
		lwkt_serialize_enter(bifp->if_serializer);
		if (m == NULL)
			return;
	}

	LIST_FOREACH(bif, &sc->sc_iflist, bif_next) {
		dst_if = bif->bif_ifp;
		if (dst_if == src_if)
			continue;

		if (bif->bif_flags & IFBIF_STP) {
			switch (bif->bif_state) {
			case BSTP_IFSTATE_BLOCKING:
			case BSTP_IFSTATE_DISABLED:
				continue;
			}
		}

		if ((bif->bif_flags & IFBIF_DISCOVER) == 0 &&
		    (m->m_flags & (M_BCAST|M_MCAST)) == 0)
			continue;

		if ((dst_if->if_flags & IFF_RUNNING) == 0)
			continue;

		if (LIST_NEXT(bif, bif_next) == NULL) {
			mc = m;
			used = 1;
		} else {
			mc = m_copypacket(m, MB_DONTWAIT);
			if (mc == NULL) {
				sc->sc_ifp->if_oerrors++;
				continue;
			}
		}
		bridge_pfil_enqueue(dst_if, mc, runfilt);
	}
	if (used == 0)
		m_freem(m);
}

/*
 * bridge_span:
 *
 *	Duplicate a packet out one or more interfaces that are in span mode,
 *	the original mbuf is unmodified.
 */
static void
bridge_span(struct bridge_softc *sc, struct mbuf *m)
{
	struct bridge_iflist *bif;
	struct ifnet *dst_if;
	struct mbuf *mc;

	if (LIST_EMPTY(&sc->sc_spanlist))
		return;

	LIST_FOREACH(bif, &sc->sc_spanlist, bif_next) {
		dst_if = bif->bif_ifp;

		if ((dst_if->if_flags & IFF_RUNNING) == 0)
			continue;

		mc = m_copypacket(m, MB_DONTWAIT);
		if (mc == NULL) {
			sc->sc_ifp->if_oerrors++;
			continue;
		}

		bridge_enqueue(dst_if, mc);
	}
}

/*
 * bridge_rtupdate:
 *
 *	Add a bridge routing entry.
 *	Can be called from interrupt context.
 */
static int
bridge_rtupdate(struct bridge_softc *sc, const uint8_t *dst,
    struct ifnet *dst_if, int setflags, uint8_t flags)
{
	struct bridge_rtnode *brt;
	int error;

	/*
	 * A route for this destination might already exist.  If so,
	 * update it, otherwise create a new one.
	 */
	if ((brt = bridge_rtnode_lookup(sc, dst)) == NULL) {
		if (sc->sc_brtcnt >= sc->sc_brtmax)
			return (ENOSPC);

		/*
		 * Allocate a new bridge forwarding node, and
		 * initialize the expiration time and Ethernet
		 * address.
		 */
		brt = kmalloc(sizeof(struct bridge_rtnode), M_DEVBUF,
			      M_INTNOWAIT|M_ZERO);
		if (brt == NULL)
			return (ENOMEM);

		brt->brt_flags = IFBAF_DYNAMIC;
		memcpy(brt->brt_addr, dst, ETHER_ADDR_LEN);

		if ((error = bridge_rtnode_insert(sc, brt)) != 0) {
			kfree(brt, M_DEVBUF);
			return (error);
		}
	}

	if ((brt->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC)
		brt->brt_ifp = dst_if;
	if ((flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC)
		brt->brt_expire = time_second + sc->sc_brttimeout;
	if (setflags)
		brt->brt_flags = flags;

	return (0);
}

/*
 * bridge_rtlookup:
 *
 *	Lookup the destination interface for an address.
 */
static struct ifnet *
bridge_rtlookup(struct bridge_softc *sc, const uint8_t *addr)
{
	struct bridge_rtnode *brt;

	if ((brt = bridge_rtnode_lookup(sc, addr)) == NULL)
		return (NULL);

	return (brt->brt_ifp);
}

/*
 * bridge_rttrim:
 *
 *	Trim the routine table so that we have a number
 *	of routing entries less than or equal to the
 *	maximum number.
 */
static void
bridge_rttrim(struct bridge_softc *sc)
{
	struct bridge_rtnode *brt, *nbrt;

	/* Make sure we actually need to do this. */
	if (sc->sc_brtcnt <= sc->sc_brtmax)
		return;

	/* Force an aging cycle; this might trim enough addresses. */
	bridge_rtage(sc);
	if (sc->sc_brtcnt <= sc->sc_brtmax)
		return;

	for (brt = LIST_FIRST(&sc->sc_rtlist); brt != NULL; brt = nbrt) {
		nbrt = LIST_NEXT(brt, brt_list);
		if ((brt->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
			bridge_rtnode_destroy(sc, brt);
			if (sc->sc_brtcnt <= sc->sc_brtmax)
				return;
		}
	}
}

/*
 * bridge_timer:
 *
 *	Aging timer for the bridge.
 */
static void
bridge_timer(void *arg)
{
	struct bridge_softc *sc = arg;

	lwkt_serialize_enter(sc->sc_ifp->if_serializer);

	bridge_rtage(sc);

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		callout_reset(&sc->sc_brcallout,
		    bridge_rtable_prune_period * hz, bridge_timer, sc);

	lwkt_serialize_exit(sc->sc_ifp->if_serializer);
}

/*
 * bridge_rtage:
 *
 *	Perform an aging cycle.
 */
static void
bridge_rtage(struct bridge_softc *sc)
{
	struct bridge_rtnode *brt, *nbrt;

	for (brt = LIST_FIRST(&sc->sc_rtlist); brt != NULL; brt = nbrt) {
		nbrt = LIST_NEXT(brt, brt_list);
		if ((brt->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
			if (time_second >= brt->brt_expire)
				bridge_rtnode_destroy(sc, brt);
		}
	}
}

/*
 * bridge_rtflush:
 *
 *	Remove all dynamic addresses from the bridge.
 */
static void
bridge_rtflush(struct bridge_softc *sc, int full)
{
	struct bridge_rtnode *brt, *nbrt;

	for (brt = LIST_FIRST(&sc->sc_rtlist); brt != NULL; brt = nbrt) {
		nbrt = LIST_NEXT(brt, brt_list);
		if (full || (brt->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC)
			bridge_rtnode_destroy(sc, brt);
	}
}

/*
 * bridge_rtdaddr:
 *
 *	Remove an address from the table.
 */
static int
bridge_rtdaddr(struct bridge_softc *sc, const uint8_t *addr)
{
	struct bridge_rtnode *brt;

	if ((brt = bridge_rtnode_lookup(sc, addr)) == NULL)
		return (ENOENT);

	bridge_rtnode_destroy(sc, brt);
	return (0);
}

/*
 * bridge_rtdelete:
 *
 *	Delete routes to a speicifc member interface.
 */
void
bridge_rtdelete(struct bridge_softc *sc, struct ifnet *ifp, int full)
{
	struct bridge_rtnode *brt, *nbrt;

	for (brt = LIST_FIRST(&sc->sc_rtlist); brt != NULL; brt = nbrt) {
		nbrt = LIST_NEXT(brt, brt_list);
		if (brt->brt_ifp == ifp && (full ||
			    (brt->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC))
			bridge_rtnode_destroy(sc, brt);
	}
}

/*
 * bridge_rtable_init:
 *
 *	Initialize the route table for this bridge.
 */
static int
bridge_rtable_init(struct bridge_softc *sc)
{
	int i;

	sc->sc_rthash = kmalloc(sizeof(*sc->sc_rthash) * BRIDGE_RTHASH_SIZE,
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < BRIDGE_RTHASH_SIZE; i++)
		LIST_INIT(&sc->sc_rthash[i]);

	sc->sc_rthash_key = karc4random();

	LIST_INIT(&sc->sc_rtlist);

	return (0);
}

/*
 * bridge_rtable_fini:
 *
 *	Deconstruct the route table for this bridge.
 */
static void
bridge_rtable_fini(struct bridge_softc *sc)
{

	kfree(sc->sc_rthash, M_DEVBUF);
}

/*
 * The following hash function is adapted from "Hash Functions" by Bob Jenkins
 * ("Algorithm Alley", Dr. Dobbs Journal, September 1997).
 */
#define	mix(a, b, c)							\
do {									\
	a -= b; a -= c; a ^= (c >> 13);					\
	b -= c; b -= a; b ^= (a << 8);					\
	c -= a; c -= b; c ^= (b >> 13);					\
	a -= b; a -= c; a ^= (c >> 12);					\
	b -= c; b -= a; b ^= (a << 16);					\
	c -= a; c -= b; c ^= (b >> 5);					\
	a -= b; a -= c; a ^= (c >> 3);					\
	b -= c; b -= a; b ^= (a << 10);					\
	c -= a; c -= b; c ^= (b >> 15);					\
} while (/*CONSTCOND*/0)

static __inline uint32_t
bridge_rthash(struct bridge_softc *sc, const uint8_t *addr)
{
	uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = sc->sc_rthash_key;

	b += addr[5] << 8;
	b += addr[4];
	a += addr[3] << 24;
	a += addr[2] << 16;
	a += addr[1] << 8;
	a += addr[0];

	mix(a, b, c);

	return (c & BRIDGE_RTHASH_MASK);
}

#undef mix

static int
bridge_rtnode_addr_cmp(const uint8_t *a, const uint8_t *b)
{
	int i, d;

	for (i = 0, d = 0; i < ETHER_ADDR_LEN && d == 0; i++) {
		d = ((int)a[i]) - ((int)b[i]);
	}

	return (d);
}

/*
 * bridge_rtnode_lookup:
 *
 *	Look up a bridge route node for the specified destination.
 */
static struct bridge_rtnode *
bridge_rtnode_lookup(struct bridge_softc *sc, const uint8_t *addr)
{
	struct bridge_rtnode *brt;
	uint32_t hash;
	int dir;

	hash = bridge_rthash(sc, addr);
	LIST_FOREACH(brt, &sc->sc_rthash[hash], brt_hash) {
		dir = bridge_rtnode_addr_cmp(addr, brt->brt_addr);
		if (dir == 0)
			return (brt);
		if (dir > 0)
			return (NULL);
	}

	return (NULL);
}

/*
 * bridge_rtnode_insert:
 *
 *	Insert the specified bridge node into the route table.  We
 *	assume the entry is not already in the table.
 */
static int
bridge_rtnode_insert(struct bridge_softc *sc, struct bridge_rtnode *brt)
{
	struct bridge_rtnode *lbrt;
	uint32_t hash;
	int dir;

	hash = bridge_rthash(sc, brt->brt_addr);

	lbrt = LIST_FIRST(&sc->sc_rthash[hash]);
	if (lbrt == NULL) {
		LIST_INSERT_HEAD(&sc->sc_rthash[hash], brt, brt_hash);
		goto out;
	}

	do {
		dir = bridge_rtnode_addr_cmp(brt->brt_addr, lbrt->brt_addr);
		if (dir == 0)
			return (EEXIST);
		if (dir > 0) {
			LIST_INSERT_BEFORE(lbrt, brt, brt_hash);
			goto out;
		}
		if (LIST_NEXT(lbrt, brt_hash) == NULL) {
			LIST_INSERT_AFTER(lbrt, brt, brt_hash);
			goto out;
		}
		lbrt = LIST_NEXT(lbrt, brt_hash);
	} while (lbrt != NULL);

#ifdef DIAGNOSTIC
	panic("bridge_rtnode_insert: impossible");
#endif

out:
	LIST_INSERT_HEAD(&sc->sc_rtlist, brt, brt_list);
	sc->sc_brtcnt++;

	return (0);
}

/*
 * bridge_rtnode_destroy:
 *
 *	Destroy a bridge rtnode.
 */
static void
bridge_rtnode_destroy(struct bridge_softc *sc, struct bridge_rtnode *brt)
{

	LIST_REMOVE(brt, brt_hash);

	LIST_REMOVE(brt, brt_list);
	sc->sc_brtcnt--;
	kfree(brt, M_DEVBUF);
}

/*
 * Send bridge packets through pfil if they are one of the types pfil can deal
 * with, or if they are ARP or REVARP.  (pfil will pass ARP and REVARP without
 * question.) If *bifp or *ifp are NULL then packet filtering is skipped for
 * that interface.
 */
static int
bridge_pfil(struct mbuf **mp, struct ifnet *bifp, struct ifnet *ifp, int dir)
{
	int snap, error, i, hlen;
	struct ether_header *eh1, eh2;
	struct ip *ip;
	struct llc llc1;
	u_int16_t ether_type;

	snap = 0;
	error = -1;	/* Default error if not error == 0 */

	if (pfil_bridge == 0 && pfil_member == 0)
		return (0); /* filtering is disabled */

	i = min((*mp)->m_pkthdr.len, max_protohdr);
	if ((*mp)->m_len < i) {
	    *mp = m_pullup(*mp, i);
	    if (*mp == NULL) {
		kprintf("%s: m_pullup failed\n", __func__);
		return (-1);
	    }
	}

	eh1 = mtod(*mp, struct ether_header *);
	ether_type = ntohs(eh1->ether_type);

	/*
	 * Check for SNAP/LLC.
	 */
	if (ether_type < ETHERMTU) {
		struct llc *llc2 = (struct llc *)(eh1 + 1);

		if ((*mp)->m_len >= ETHER_HDR_LEN + 8 &&
		    llc2->llc_dsap == LLC_SNAP_LSAP &&
		    llc2->llc_ssap == LLC_SNAP_LSAP &&
		    llc2->llc_control == LLC_UI) {
			ether_type = htons(llc2->llc_un.type_snap.ether_type);
			snap = 1;
		}
	}

	/*
	 * If we're trying to filter bridge traffic, don't look at anything
	 * other than IP and ARP traffic.  If the filter doesn't understand
	 * IPv6, don't allow IPv6 through the bridge either.  This is lame
	 * since if we really wanted, say, an AppleTalk filter, we are hosed,
	 * but of course we don't have an AppleTalk filter to begin with.
	 * (Note that since pfil doesn't understand ARP it will pass *ALL*
	 * ARP traffic.)
	 */
	switch (ether_type) {
		case ETHERTYPE_ARP:
		case ETHERTYPE_REVARP:
			return (0); /* Automatically pass */
		case ETHERTYPE_IP:
#ifdef INET6
		case ETHERTYPE_IPV6:
#endif /* INET6 */
			break;
		default:
			/*
			 * Check to see if the user wants to pass non-ip
			 * packets, these will not be checked by pfil(9) and
			 * passed unconditionally so the default is to drop.
			 */
			if (pfil_onlyip)
				goto bad;
	}

	/* Strip off the Ethernet header and keep a copy. */
	m_copydata(*mp, 0, ETHER_HDR_LEN, (caddr_t) &eh2);
	m_adj(*mp, ETHER_HDR_LEN);

	/* Strip off snap header, if present */
	if (snap) {
		m_copydata(*mp, 0, sizeof(struct llc), (caddr_t) &llc1);
		m_adj(*mp, sizeof(struct llc));
	}

	/*
	 * Check the IP header for alignment and errors
	 */
	if (dir == PFIL_IN) {
		switch (ether_type) {
			case ETHERTYPE_IP:
				error = bridge_ip_checkbasic(mp);
				break;
#ifdef INET6
			case ETHERTYPE_IPV6:
				error = bridge_ip6_checkbasic(mp);
				break;
#endif /* INET6 */
			default:
				error = 0;
		}
		if (error)
			goto bad;
	}

	error = 0;

	/*
	 * Run the packet through pfil
	 */
	switch (ether_type)
	{
	case ETHERTYPE_IP :
		/*
		 * before calling the firewall, swap fields the same as
		 * IP does. here we assume the header is contiguous
		 */
		ip = mtod(*mp, struct ip *);

		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_off = ntohs(ip->ip_off);

		/*
		 * Run pfil on the member interface and the bridge, both can
		 * be skipped by clearing pfil_member or pfil_bridge.
		 *
		 * Keep the order:
		 *   in_if -> bridge_if -> out_if
		 */
		if (pfil_bridge && dir == PFIL_OUT && bifp != NULL)
			error = pfil_run_hooks(&inet_pfil_hook, mp, bifp,
					dir);

		if (*mp == NULL || error != 0) /* filter may consume */
			break;

		if (pfil_member && ifp != NULL)
			error = pfil_run_hooks(&inet_pfil_hook, mp, ifp,
					dir);

		if (*mp == NULL || error != 0) /* filter may consume */
			break;

		if (pfil_bridge && dir == PFIL_IN && bifp != NULL)
			error = pfil_run_hooks(&inet_pfil_hook, mp, bifp,
					dir);

		if (*mp == NULL || error != 0) /* filter may consume */
			break;

		/* check if we need to fragment the packet */
		if (pfil_member && ifp != NULL && dir == PFIL_OUT) {
			i = (*mp)->m_pkthdr.len;
			if (i > ifp->if_mtu) {
				error = bridge_fragment(ifp, *mp, &eh2, snap,
					    &llc1);
				return (error);
			}
		}

		/* Recalculate the ip checksum and restore byte ordering */
		ip = mtod(*mp, struct ip *);
		hlen = ip->ip_hl << 2;
		if (hlen < sizeof(struct ip))
			goto bad;
		if (hlen > (*mp)->m_len) {
			if ((*mp = m_pullup(*mp, hlen)) == 0)
				goto bad;
			ip = mtod(*mp, struct ip *);
			if (ip == NULL)
				goto bad;
		}
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);
		ip->ip_sum = 0;
		if (hlen == sizeof(struct ip))
			ip->ip_sum = in_cksum_hdr(ip);
		else
			ip->ip_sum = in_cksum(*mp, hlen);

		break;
#ifdef INET6
	case ETHERTYPE_IPV6 :
		if (pfil_bridge && dir == PFIL_OUT && bifp != NULL)
			error = pfil_run_hooks(&inet6_pfil_hook, mp, bifp,
					dir);

		if (*mp == NULL || error != 0) /* filter may consume */
			break;

		if (pfil_member && ifp != NULL)
			error = pfil_run_hooks(&inet6_pfil_hook, mp, ifp,
					dir);

		if (*mp == NULL || error != 0) /* filter may consume */
			break;

		if (pfil_bridge && dir == PFIL_IN && bifp != NULL)
			error = pfil_run_hooks(&inet6_pfil_hook, mp, bifp,
					dir);
		break;
#endif
	default :
		error = 0;
		break;
	}

	if (*mp == NULL)
		return (error);
	if (error != 0)
		goto bad;

	error = -1;

	/*
	 * Finally, put everything back the way it was and return
	 */
	if (snap) {
		M_PREPEND(*mp, sizeof(struct llc), MB_DONTWAIT);
		if (*mp == NULL)
			return (error);
		bcopy(&llc1, mtod(*mp, caddr_t), sizeof(struct llc));
	}

	M_PREPEND(*mp, ETHER_HDR_LEN, MB_DONTWAIT);
	if (*mp == NULL)
		return (error);
	bcopy(&eh2, mtod(*mp, caddr_t), ETHER_HDR_LEN);

	return (0);

bad:
	m_freem(*mp);
	*mp = NULL;
	return (error);
}

/*
 * Perform basic checks on header size since
 * pfil assumes ip_input has already processed
 * it for it.  Cut-and-pasted from ip_input.c.
 * Given how simple the IPv6 version is,
 * does the IPv4 version really need to be
 * this complicated?
 *
 * XXX Should we update ipstat here, or not?
 * XXX Right now we update ipstat but not
 * XXX csum_counter.
 */
static int
bridge_ip_checkbasic(struct mbuf **mp)
{
	struct mbuf *m = *mp;
	struct ip *ip;
	int len, hlen;
	u_short sum;

	if (*mp == NULL)
		return (-1);
#if notyet
	if (IP_HDR_ALIGNED_P(mtod(m, caddr_t)) == 0) {
		if ((m = m_copyup(m, sizeof(struct ip),
			(max_linkhdr + 3) & ~3)) == NULL) {
			/* XXXJRT new stat, please */
			ipstat.ips_toosmall++;
			goto bad;
		}
	} else
#endif
#ifndef __predict_false
#define __predict_false(x) x
#endif
	 if (__predict_false(m->m_len < sizeof (struct ip))) {
		if ((m = m_pullup(m, sizeof (struct ip))) == NULL) {
			ipstat.ips_toosmall++;
			goto bad;
		}
	}
	ip = mtod(m, struct ip *);
	if (ip == NULL) goto bad;

	if (ip->ip_v != IPVERSION) {
		ipstat.ips_badvers++;
		goto bad;
	}
	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(struct ip)) { /* minimum header length */
		ipstat.ips_badhlen++;
		goto bad;
	}
	if (hlen > m->m_len) {
		if ((m = m_pullup(m, hlen)) == 0) {
			ipstat.ips_badhlen++;
			goto bad;
		}
		ip = mtod(m, struct ip *);
		if (ip == NULL) goto bad;
	}

	if (m->m_pkthdr.csum_flags & CSUM_IP_CHECKED) {
		sum = !(m->m_pkthdr.csum_flags & CSUM_IP_VALID);
	} else {
		if (hlen == sizeof(struct ip)) {
			sum = in_cksum_hdr(ip);
		} else {
			sum = in_cksum(m, hlen);
		}
	}
	if (sum) {
		ipstat.ips_badsum++;
		goto bad;
	}

	/* Retrieve the packet length. */
	len = ntohs(ip->ip_len);

	/*
	 * Check for additional length bogosity
	 */
	if (len < hlen) {
		ipstat.ips_badlen++;
		goto bad;
	}

	/*
	 * Check that the amount of data in the buffers
	 * is as at least much as the IP header would have us expect.
	 * Drop packet if shorter than we expect.
	 */
	if (m->m_pkthdr.len < len) {
		ipstat.ips_tooshort++;
		goto bad;
	}

	/* Checks out, proceed */
	*mp = m;
	return (0);

bad:
	*mp = m;
	return (-1);
}

#ifdef INET6
/*
 * Same as above, but for IPv6.
 * Cut-and-pasted from ip6_input.c.
 * XXX Should we update ip6stat, or not?
 */
static int
bridge_ip6_checkbasic(struct mbuf **mp)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6;

	/*
	 * If the IPv6 header is not aligned, slurp it up into a new
	 * mbuf with space for link headers, in the event we forward
	 * it.  Otherwise, if it is aligned, make sure the entire base
	 * IPv6 header is in the first mbuf of the chain.
	 */
#if notyet
	if (IP6_HDR_ALIGNED_P(mtod(m, caddr_t)) == 0) {
		struct ifnet *inifp = m->m_pkthdr.rcvif;
		if ((m = m_copyup(m, sizeof(struct ip6_hdr),
			    (max_linkhdr + 3) & ~3)) == NULL) {
			/* XXXJRT new stat, please */
			ip6stat.ip6s_toosmall++;
			in6_ifstat_inc(inifp, ifs6_in_hdrerr);
			goto bad;
		}
	} else
#endif
	if (__predict_false(m->m_len < sizeof(struct ip6_hdr))) {
		struct ifnet *inifp = m->m_pkthdr.rcvif;
		if ((m = m_pullup(m, sizeof(struct ip6_hdr))) == NULL) {
			ip6stat.ip6s_toosmall++;
			in6_ifstat_inc(inifp, ifs6_in_hdrerr);
			goto bad;
		}
	}

	ip6 = mtod(m, struct ip6_hdr *);

	if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION) {
		ip6stat.ip6s_badvers++;
		in6_ifstat_inc(m->m_pkthdr.rcvif, ifs6_in_hdrerr);
		goto bad;
	}

	/* Checks out, proceed */
	*mp = m;
	return (0);

bad:
	*mp = m;
	return (-1);
}
#endif /* INET6 */

/*
 * bridge_fragment:
 *
 *	Return a fragmented mbuf chain.
 */
static int
bridge_fragment(struct ifnet *ifp, struct mbuf *m, struct ether_header *eh,
    int snap, struct llc *llc)
{
	struct mbuf *m0;
	struct ip *ip;
	int error = -1;

	if (m->m_len < sizeof(struct ip) &&
	    (m = m_pullup(m, sizeof(struct ip))) == NULL)
		goto out;
	ip = mtod(m, struct ip *);

	error = ip_fragment(ip, &m, ifp->if_mtu, ifp->if_hwassist,
		    CSUM_DELAY_IP);
	if (error)
		goto out;

	/* walk the chain and re-add the Ethernet header */
	for (m0 = m; m0; m0 = m0->m_nextpkt) {
		if (error == 0) {
			if (snap) {
				M_PREPEND(m0, sizeof(struct llc), MB_DONTWAIT);
				if (m0 == NULL) {
					error = ENOBUFS;
					continue;
				}
				bcopy(llc, mtod(m0, caddr_t),
				    sizeof(struct llc));
			}
			M_PREPEND(m0, ETHER_HDR_LEN, MB_DONTWAIT);
			if (m0 == NULL) {
				error = ENOBUFS;
				continue;
			}
			bcopy(eh, mtod(m0, caddr_t), ETHER_HDR_LEN);
		} else 
			m_freem(m);
	}

	if (error == 0)
		ipstat.ips_fragmented++;

	return (error);

out:
	if (m != NULL)
		m_freem(m);
	return (error);
}

static void
bridge_enqueue_handler(struct netmsg *nmsg)
{
	struct netmsg_packet *nmp;
	struct ifnet *dst_ifp;
	struct mbuf *m;

	nmp = (struct netmsg_packet *)nmsg;
	m = nmp->nm_packet;
	dst_ifp = nmp->nm_netmsg.nm_lmsg.u.ms_resultp;

	bridge_handoff_notags(dst_ifp, m);
}

static void
bridge_pfil_enqueue_handler(struct netmsg *nmsg)
{
	struct netmsg_packet *nmp;
	struct ifnet *dst_ifp;
	struct mbuf *m;

	nmp = (struct netmsg_packet *)nmsg;
	m = nmp->nm_packet;
	dst_ifp = nmp->nm_netmsg.nm_lmsg.u.ms_resultp;

	/*
	 * Filter on the output interface. Pass a NULL bridge interface
	 * pointer so we do not redundantly filter on the bridge for
	 * each interface we broadcast on.
	 */
	if (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
	    || inet6_pfil_hook.ph_hashooks > 0
#endif
	    ) {
		if (bridge_pfil(&m, NULL, dst_ifp, PFIL_OUT) != 0)
			return;
		if (m == NULL)
			return;
	}
	bridge_handoff_notags(dst_ifp, m);
}

static void
bridge_handoff(struct ifnet *dst_ifp, struct mbuf *m)
{
	while (m->m_type == MT_TAG) {
		/* XXX see ether_output_frame for full rules check */
		m = m->m_next;
	}
	bridge_handoff_notags(dst_ifp, m);
}

static void
bridge_handoff_notags(struct ifnet *dst_ifp, struct mbuf *m)
{
	struct mbuf *m0;

	KKASSERT(m->m_type != MT_TAG);

	lwkt_serialize_enter(dst_ifp->if_serializer);

	/* We may be sending a fragment so traverse the mbuf */
	for (; m; m = m0) {
		struct altq_pktattr pktattr;

		m0 = m->m_nextpkt;
		m->m_nextpkt = NULL;

		if (ifq_is_enabled(&dst_ifp->if_snd))
			altq_etherclassify(&dst_ifp->if_snd, m, &pktattr);

		ifq_handoff(dst_ifp, m, &pktattr);
	}

	lwkt_serialize_exit(dst_ifp->if_serializer);
}

struct netmsg_brgctl {
	struct netmsg		bc_nmsg;
	bridge_ctl_t		bc_func;
	struct bridge_softc	*bc_sc;
	void			*bc_arg;
};

static void
bridge_control_dispatch(struct netmsg *nmsg)
{
	struct netmsg_brgctl *bc_msg = (struct netmsg_brgctl *)nmsg;
	struct ifnet *bifp = bc_msg->bc_sc->sc_ifp;
	int error;

	lwkt_serialize_enter(bifp->if_serializer);
	error = bc_msg->bc_func(bc_msg->bc_sc, bc_msg->bc_arg);
	lwkt_serialize_exit(bifp->if_serializer);

	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static int
bridge_control(struct bridge_softc *sc, u_long cmd,
	       bridge_ctl_t bc_func, void *bc_arg)
{
	struct ifnet *bifp = sc->sc_ifp;
	struct netmsg_brgctl bc_msg;
	struct netmsg *nmsg;
	int error;

	ASSERT_SERIALIZED(bifp->if_serializer);

	if (cmd == SIOCGDRVSPEC) {
		/*
		 * Don't dispatch 'get' ioctl to netisr0;
		 * there are copyouts down deep inside
		 * specific bridge ioctl functions.
		 */
		return bc_func(sc, bc_arg);
	}

	bzero(&bc_msg, sizeof(bc_msg));
	nmsg = &bc_msg.bc_nmsg;

	netmsg_init(nmsg, &curthread->td_msgport, 0, bridge_control_dispatch);
	bc_msg.bc_func = bc_func;
	bc_msg.bc_sc = sc;
	bc_msg.bc_arg = bc_arg;

	lwkt_serialize_exit(bifp->if_serializer);
	error = lwkt_domsg(cpu_portfn(0), &nmsg->nm_lmsg, 0);
	lwkt_serialize_enter(bifp->if_serializer);
	return error;
}
