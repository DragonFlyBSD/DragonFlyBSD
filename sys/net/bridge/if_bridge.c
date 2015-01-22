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
 *
 *
 * Bridge's route information is duplicated to each CPUs:
 *
 *      CPU0          CPU1          CPU2          CPU3
 * +-----------+ +-----------+ +-----------+ +-----------+
 * |  rtnode   | |  rtnode   | |  rtnode   | |  rtnode   |
 * |           | |           | |           | |           |
 * | dst eaddr | | dst eaddr | | dst eaddr | | dst eaddr |
 * +-----------+ +-----------+ +-----------+ +-----------+
 *       |         |                     |         |
 *       |         |                     |         |
 *       |         |     +----------+    |         |
 *       |         |     |  rtinfo  |    |         |
 *       |         +---->|          |<---+         |
 *       |               |  flags   |              |
 *       +-------------->|  timeout |<-------------+
 *                       |  dst_ifp |
 *                       +----------+
 *
 * We choose to put timeout and dst_ifp into shared part, so updating
 * them will be cheaper than using message forwarding.  Also there is
 * not need to use spinlock to protect the updating: timeout and dst_ifp
 * is not related and specific field's updating order has no importance.
 * The cache pollution by the share part should not be heavy: in a stable
 * setup, dst_ifp probably will be not changed in rtnode's life time,
 * while timeout is refreshed once per second; most of the time, timeout
 * and dst_ifp are read-only accessed.
 *
 *
 * Bridge route information installation on bridge_input path:
 *
 *      CPU0           CPU1         CPU2          CPU3
 *
 *                               tcp_thread2
 *                                    |
 *                                alloc nmsg
 *                    snd nmsg        |
 *                    w/o rtinfo      |
 *      ifnet0<-----------------------+
 *        |                           :
 *    lookup dst                      :
 *   rtnode exists?(Y)free nmsg       :
 *        |(N)                        :
 *        |
 *  alloc rtinfo
 *  alloc rtnode
 * install rtnode
 *        |
 *        +---------->ifnet1
 *        : fwd nmsg    |
 *        : w/ rtinfo   |
 *        :             |
 *        :             |
 *                 alloc rtnode
 *               (w/ nmsg's rtinfo)
 *                install rtnode
 *                      |
 *                      +---------->ifnet2
 *                      : fwd nmsg    |
 *                      : w/ rtinfo   |
 *                      :             |
 *                      :         same as ifnet1
 *                                    |
 *                                    +---------->ifnet3
 *                                    : fwd nmsg    |
 *                                    : w/ rtinfo   |
 *                                    :             |
 *                                    :         same as ifnet1
 *                                               free nmsg
 *                                                  :
 *                                                  :
 *
 * The netmsgs forwarded between protocol threads and ifnet threads are
 * allocated with (M_WAITOK|M_NULLOK), so it will not fail under most
 * cases (route information is too precious to be not installed :).
 * Since multiple threads may try to install route information for the
 * same dst eaddr, we look up route information in ifnet0.  However, this
 * looking up only need to be performed on ifnet0, which is the start
 * point of the route information installation process.
 *
 *
 * Bridge route information deleting/flushing:
 *
 *  CPU0            CPU1             CPU2             CPU3
 *
 * netisr0
 *   |
 * find suitable rtnodes,
 * mark their rtinfo dead
 *   |
 *   | domsg <------------------------------------------+
 *   |                                                  | replymsg
 *   |                                                  |
 *   V     fwdmsg           fwdmsg           fwdmsg     |
 * ifnet0 --------> ifnet1 --------> ifnet2 --------> ifnet3
 * delete rtnodes   delete rtnodes   delete rtnodes   delete rtnodes
 * w/ dead rtinfo   w/ dead rtinfo   w/ dead rtinfo   w/ dead rtinfo
 *                                                    free dead rtinfos
 *
 * All deleting/flushing operations are serialized by netisr0, so each
 * operation only reaps the route information marked dead by itself.
 *
 *
 * Bridge route information adding/deleting/flushing:
 * Since all operation is serialized by the fixed message flow between
 * ifnet threads, it is not possible to create corrupted per-cpu route
 * information.
 *
 *
 *
 * Percpu member interface list iteration with blocking operation:
 * Since one bridge could only delete one member interface at a time and
 * the deleted member interface is not freed after netmsg_service_sync(),
 * following way is used to make sure that even if the certain member
 * interface is ripped from the percpu list during the blocking operation,
 * the iteration still could keep going:
 *
 * TAILQ_FOREACH_MUTABLE(bif, sc->sc_iflists[mycpuid], bif_next, nbif) {
 *     blocking operation;
 *     blocking operation;
 *     ...
 *     ...
 *     if (nbif != NULL && !nbif->bif_onlist) {
 *         KKASSERT(bif->bif_onlist);
 *         nbif = TAILQ_NEXT(bif, bif_next);
 *     }
 * }
 *
 * As mentioned above only one member interface could be unlinked from the
 * percpu member interface list, so either bif or nbif may be not on the list,
 * but _not_ both.  To keep the list iteration, we don't care about bif, but
 * only nbif.  Since removed member interface will only be freed after we
 * finish our work, it is safe to access any field in an unlinked bif (here
 * bif_onlist).  If nbif is no longer on the list, then bif must be on the
 * list, so we change nbif to the next element of bif and keep going.
 */

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
#include <sys/priv.h>
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
#include <net/netisr2.h>

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
#define	BRIDGE_RTABLE_MAX		4096
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
#define	BRIDGE_IFCAPS_MASK		(IFCAP_TXCSUM | IFCAP_TSO)

typedef int	(*bridge_ctl_t)(struct bridge_softc *, void *);

struct netmsg_brctl {
	struct netmsg_base	base;
	bridge_ctl_t		bc_func;
	struct bridge_softc	*bc_sc;
	void			*bc_arg;
};

struct netmsg_brsaddr {
	struct netmsg_base	base;
	struct bridge_softc	*br_softc;
	struct ifnet		*br_dst_if;
	struct bridge_rtinfo	*br_rtinfo;
	int			br_setflags;
	uint8_t			br_dst[ETHER_ADDR_LEN];
	uint8_t			br_flags;
};

struct netmsg_braddbif {
	struct netmsg_base	base;
	struct bridge_softc	*br_softc;
	struct bridge_ifinfo	*br_bif_info;
	struct ifnet		*br_bif_ifp;
};

struct netmsg_brdelbif {
	struct netmsg_base	base;
	struct bridge_softc	*br_softc;
	struct bridge_ifinfo	*br_bif_info;
	struct bridge_iflist_head *br_bif_list;
};

struct netmsg_brsflags {
	struct netmsg_base	base;
	struct bridge_softc	*br_softc;
	struct bridge_ifinfo	*br_bif_info;
	uint32_t		br_bif_flags;
};

eventhandler_tag	bridge_detach_cookie = NULL;

extern	struct mbuf *(*bridge_input_p)(struct ifnet *, struct mbuf *);
extern	int (*bridge_output_p)(struct ifnet *, struct mbuf *);
extern	void (*bridge_dn_p)(struct mbuf *, struct ifnet *);
extern  struct ifnet *(*bridge_interface_p)(void *if_bridge);

static int	bridge_rtable_prune_period = BRIDGE_RTABLE_PRUNE_PERIOD;

static int	bridge_clone_create(struct if_clone *, int, caddr_t);
static int	bridge_clone_destroy(struct ifnet *);

static int	bridge_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bridge_mutecaps(struct bridge_ifinfo *, struct ifnet *, int);
static void	bridge_ifdetach(void *, struct ifnet *);
static void	bridge_init(void *);
static int	bridge_from_us(struct bridge_softc *, struct ether_header *);
static void	bridge_stop(struct ifnet *);
static void	bridge_start(struct ifnet *, struct ifaltq_subque *);
static struct mbuf *bridge_input(struct ifnet *, struct mbuf *);
static int	bridge_output(struct ifnet *, struct mbuf *);
static struct ifnet *bridge_interface(void *if_bridge);

static void	bridge_forward(struct bridge_softc *, struct mbuf *m);

static void	bridge_timer_handler(netmsg_t);
static void	bridge_timer(void *);

static void	bridge_start_bcast(struct bridge_softc *, struct mbuf *);
static void	bridge_broadcast(struct bridge_softc *, struct ifnet *,
		    struct mbuf *);
static void	bridge_span(struct bridge_softc *, struct mbuf *);

static int	bridge_rtupdate(struct bridge_softc *, const uint8_t *,
		    struct ifnet *, uint8_t);
static struct ifnet *bridge_rtlookup(struct bridge_softc *, const uint8_t *);
static void	bridge_rtreap(struct bridge_softc *);
static void	bridge_rtreap_async(struct bridge_softc *);
static void	bridge_rttrim(struct bridge_softc *);
static int	bridge_rtage_finddead(struct bridge_softc *);
static void	bridge_rtage(struct bridge_softc *);
static void	bridge_rtflush(struct bridge_softc *, int);
static int	bridge_rtdaddr(struct bridge_softc *, const uint8_t *);
static int	bridge_rtsaddr(struct bridge_softc *, const uint8_t *,
		    struct ifnet *, uint8_t);
static void	bridge_rtmsg_sync(struct bridge_softc *sc);
static void	bridge_rtreap_handler(netmsg_t);
static void	bridge_rtinstall_handler(netmsg_t);
static int	bridge_rtinstall_oncpu(struct bridge_softc *, const uint8_t *,
		    struct ifnet *, int, uint8_t, struct bridge_rtinfo **);

static void	bridge_rtable_init(struct bridge_softc *);
static void	bridge_rtable_fini(struct bridge_softc *);

static int	bridge_rtnode_addr_cmp(const uint8_t *, const uint8_t *);
static struct bridge_rtnode *bridge_rtnode_lookup(struct bridge_softc *,
		    const uint8_t *);
static void	bridge_rtnode_insert(struct bridge_softc *,
		    struct bridge_rtnode *);
static void	bridge_rtnode_destroy(struct bridge_softc *,
		    struct bridge_rtnode *);

static struct bridge_iflist *bridge_lookup_member(struct bridge_softc *,
		    const char *name);
static struct bridge_iflist *bridge_lookup_member_if(struct bridge_softc *,
		    struct ifnet *ifp);
static struct bridge_iflist *bridge_lookup_member_ifinfo(struct bridge_softc *,
		    struct bridge_ifinfo *);
static void	bridge_delete_member(struct bridge_softc *,
		    struct bridge_iflist *, int);
static void	bridge_delete_span(struct bridge_softc *,
		    struct bridge_iflist *);

static int	bridge_control(struct bridge_softc *, u_long,
			       bridge_ctl_t, void *);
static int	bridge_ioctl_init(struct bridge_softc *, void *);
static int	bridge_ioctl_stop(struct bridge_softc *, void *);
static int	bridge_ioctl_add(struct bridge_softc *, void *);
static int	bridge_ioctl_del(struct bridge_softc *, void *);
static void	bridge_ioctl_fillflags(struct bridge_softc *sc,
				struct bridge_iflist *bif, struct ifbreq *req);
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
static int	bridge_ioctl_reinit(struct bridge_softc *, void *);
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
static int	bridge_ioctl_sifbondwght(struct bridge_softc *, void *);
static int	bridge_pfil(struct mbuf **, struct ifnet *, struct ifnet *,
		    int);
static int	bridge_ip_checkbasic(struct mbuf **mp);
#ifdef INET6
static int	bridge_ip6_checkbasic(struct mbuf **mp);
#endif /* INET6 */
static int	bridge_fragment(struct ifnet *, struct mbuf *,
		    struct ether_header *, int, struct llc *);
static void	bridge_enqueue_handler(netmsg_t);
static void	bridge_handoff(struct bridge_softc *, struct ifnet *,
		    struct mbuf *, int);

static void	bridge_del_bif_handler(netmsg_t);
static void	bridge_add_bif_handler(netmsg_t);
static void	bridge_del_bif(struct bridge_softc *, struct bridge_ifinfo *,
		    struct bridge_iflist_head *);
static void	bridge_add_bif(struct bridge_softc *, struct bridge_ifinfo *,
		    struct ifnet *);

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_BRIDGE, bridge, CTLFLAG_RW, 0, "Bridge");

static int pfil_onlyip = 1; /* only pass IP[46] packets when pfil is enabled */
static int pfil_bridge = 1; /* run pfil hooks on the bridge interface */
static int pfil_member = 1; /* run pfil hooks on the member interface */
static int bridge_debug;
SYSCTL_INT(_net_link_bridge, OID_AUTO, pfil_onlyip, CTLFLAG_RW,
    &pfil_onlyip, 0, "Only pass IP packets when pfil is enabled");
SYSCTL_INT(_net_link_bridge, OID_AUTO, pfil_bridge, CTLFLAG_RW,
    &pfil_bridge, 0, "Packet filter on the bridge interface");
SYSCTL_INT(_net_link_bridge, OID_AUTO, pfil_member, CTLFLAG_RW,
    &pfil_member, 0, "Packet filter on the member interface");
SYSCTL_INT(_net_link_bridge, OID_AUTO, debug, CTLFLAG_RW,
    &bridge_debug, 0, "Bridge debug mode");

struct bridge_control_arg {
	union {
		struct ifbreq ifbreq;
		struct ifbifconf ifbifconf;
		struct ifbareq ifbareq;
		struct ifbaconf ifbaconf;
		struct ifbrparam ifbrparam;
	} bca_u;
	int	bca_len;
	void	*bca_uptr;
	void	*bca_kptr;
};

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

	{ bridge_ioctl_sifbondwght,	sizeof(struct ifbreq),
	  BC_F_COPYIN|BC_F_SUSER },

};
static const int bridge_control_table_size = NELEM(bridge_control_table);

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
		bridge_interface_p = bridge_interface;
		bridge_detach_cookie = EVENTHANDLER_REGISTER(
		    ifnet_detach_event, bridge_ifdetach, NULL,
		    EVENTHANDLER_PRI_ANY);
#if 0 /* notyet */
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
		bridge_interface_p = NULL;
#if 0 /* notyet */
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
bridge_clone_create(struct if_clone *ifc, int unit, caddr_t param __unused)
{
	struct bridge_softc *sc;
	struct ifnet *ifp;
	u_char eaddr[6];
	int cpu, rnd;

	sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
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
	netmsg_init(&sc->sc_brtimemsg, NULL, &netisr_adone_rport,
		    MSGF_DROPABLE, bridge_timer_handler);
	sc->sc_brtimemsg.lmsg.u.ms_resultp = sc;

	callout_init(&sc->sc_bstpcallout);
	netmsg_init(&sc->sc_bstptimemsg, NULL, &netisr_adone_rport,
		    MSGF_DROPABLE, bstp_tick_handler);
	sc->sc_bstptimemsg.lmsg.u.ms_resultp = sc;

	/* Initialize per-cpu member iface lists */
	sc->sc_iflists = kmalloc(sizeof(*sc->sc_iflists) * ncpus,
				 M_DEVBUF, M_WAITOK);
	for (cpu = 0; cpu < ncpus; ++cpu)
		TAILQ_INIT(&sc->sc_iflists[cpu]);

	TAILQ_INIT(&sc->sc_spanlist);

	ifp->if_softc = sc;
	if_initname(ifp, ifc->ifc_name, unit);
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_ioctl = bridge_ioctl;
	ifp->if_start = bridge_start;
	ifp->if_init = bridge_init;
	ifp->if_type = IFT_ETHER;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);
	ifp->if_hdrlen = ETHER_HDR_LEN;

	/*
	 * Generate a random ethernet address and use the private AC:DE:48
	 * OUI code.
	 */
	rnd = karc4random();
	bcopy(&rnd, &eaddr[0], 4); /* ETHER_ADDR_LEN == 6 */
	rnd = karc4random();
	bcopy(&rnd, &eaddr[2], 4); /* ETHER_ADDR_LEN == 6 */

	eaddr[0] &= ~1;	/* clear multicast bit */
	eaddr[0] |= 2;	/* set the LAA bit */

	ether_ifattach(ifp, eaddr, NULL);
	/* Now undo some of the damage... */
	ifp->if_baudrate = 0;
	/*ifp->if_type = IFT_BRIDGE;*/

	crit_enter();	/* XXX MP */
	LIST_INSERT_HEAD(&bridge_list, sc, sc_list);
	crit_exit();

	return (0);
}

static void
bridge_delete_dispatch(netmsg_t msg)
{
	struct bridge_softc *sc = msg->lmsg.u.ms_resultp;
	struct ifnet *bifp = sc->sc_ifp;
	struct bridge_iflist *bif;

	ifnet_serialize_all(bifp);

	while ((bif = TAILQ_FIRST(&sc->sc_iflists[mycpuid])) != NULL)
		bridge_delete_member(sc, bif, 0);

	while ((bif = TAILQ_FIRST(&sc->sc_spanlist)) != NULL)
		bridge_delete_span(sc, bif);

	ifnet_deserialize_all(bifp);

	lwkt_replymsg(&msg->lmsg, 0);
}

/*
 * bridge_clone_destroy:
 *
 *	Destroy a bridge instance.
 */
static int
bridge_clone_destroy(struct ifnet *ifp)
{
	struct bridge_softc *sc = ifp->if_softc;
	struct netmsg_base msg;

	ifnet_serialize_all(ifp);

	bridge_stop(ifp);
	ifp->if_flags &= ~IFF_UP;

	ifnet_deserialize_all(ifp);

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, bridge_delete_dispatch);
	msg.lmsg.u.ms_resultp = sc;
	lwkt_domsg(BRIDGE_CFGPORT, &msg.lmsg, 0);

	crit_enter();	/* XXX MP */
	LIST_REMOVE(sc, sc_list);
	crit_exit();

	ether_ifdetach(ifp);

	/* Tear down the routing table. */
	bridge_rtable_fini(sc);

	/* Free per-cpu member iface lists */
	kfree(sc->sc_iflists, M_DEVBUF);

	kfree(sc, M_DEVBUF);

	return 0;
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
	struct bridge_control_arg args;
	struct ifdrv *ifd = (struct ifdrv *) data;
	const struct bridge_control *bc;
	int error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

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
			   (bc->bc_flags & BC_F_COPYOUT)) {
			error = EINVAL;
			break;
		}

		if (bc->bc_flags & BC_F_SUSER) {
			error = priv_check_cred(cr, PRIV_ROOT, NULL_CRED_OKAY);
			if (error)
				break;
		}

		if (ifd->ifd_len != bc->bc_argsize ||
		    ifd->ifd_len > sizeof(args.bca_u)) {
			error = EINVAL;
			break;
		}

		memset(&args, 0, sizeof(args));
		if (bc->bc_flags & BC_F_COPYIN) {
			error = copyin(ifd->ifd_data, &args.bca_u,
				       ifd->ifd_len);
			if (error)
				break;
		}

		error = bridge_control(sc, cmd, bc->bc_func, &args);
		if (error) {
			KKASSERT(args.bca_len == 0 && args.bca_kptr == NULL);
			break;
		}

		if (bc->bc_flags & BC_F_COPYOUT) {
			error = copyout(&args, ifd->ifd_data, ifd->ifd_len);
			if (args.bca_len != 0) {
				KKASSERT(args.bca_kptr != NULL);
				if (!error) {
					error = copyout(args.bca_kptr,
						args.bca_uptr, args.bca_len);
				}
				kfree(args.bca_kptr, M_TEMP);
			} else {
				KKASSERT(args.bca_kptr == NULL);
			}
		} else {
			KKASSERT(args.bca_len == 0 && args.bca_kptr == NULL);
		}
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

		/*
		 * If running and link flag state change we have to
		 * reinitialize as well.
		 */
		if ((ifp->if_flags & IFF_RUNNING) &&
		    (ifp->if_flags & (IFF_LINK0|IFF_LINK1|IFF_LINK2)) !=
		    sc->sc_copy_flags) {
			sc->sc_copy_flags = ifp->if_flags &
					(IFF_LINK0|IFF_LINK1|IFF_LINK2);
			bridge_control(sc, 0, bridge_ioctl_reinit, NULL);
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
bridge_mutecaps(struct bridge_ifinfo *bif_info, struct ifnet *ifp, int mute)
{
	struct ifreq ifr;

	if (ifp->if_ioctl == NULL)
		return;

	bzero(&ifr, sizeof(ifr));
	ifr.ifr_reqcap = ifp->if_capenable;

	if (mute) {
		/* mask off and save capabilities */
		bif_info->bifi_mutecap = ifr.ifr_reqcap & BRIDGE_IFCAPS_MASK;
		if (bif_info->bifi_mutecap != 0)
			ifr.ifr_reqcap &= ~BRIDGE_IFCAPS_MASK;
	} else {
		/* restore muted capabilities */
		ifr.ifr_reqcap |= bif_info->bifi_mutecap;
	}

	if (bif_info->bifi_mutecap != 0) {
		ifnet_serialize_all(ifp);
		ifp->if_ioctl(ifp, SIOCSIFCAP, (caddr_t)&ifr, NULL);
		ifnet_deserialize_all(ifp);
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

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if (strcmp(bif->bif_ifp->if_xname, name) == 0)
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

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if (bif->bif_ifp == member_ifp)
			return (bif);
	}
	return (NULL);
}

/*
 * bridge_lookup_member_ifinfo:
 *
 *	Lookup a bridge member interface by bridge_ifinfo.
 */
static struct bridge_iflist *
bridge_lookup_member_ifinfo(struct bridge_softc *sc,
			    struct bridge_ifinfo *bif_info)
{
	struct bridge_iflist *bif;

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if (bif->bif_info == bif_info)
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
	struct bridge_ifinfo *bif_info = bif->bif_info;
	struct bridge_iflist_head saved_bifs;

	ASSERT_IFNET_SERIALIZED_ALL(bifp);
	KKASSERT(bif_info != NULL);

	ifs->if_bridge = NULL;

	/*
	 * Release bridge interface's serializer:
	 * - To avoid possible dead lock.
	 * - Various sync operation will block the current thread.
	 */
	ifnet_deserialize_all(bifp);

	if (!gone) {
		switch (ifs->if_type) {
		case IFT_ETHER:
		case IFT_L2VLAN:
			/*
			 * Take the interface out of promiscuous mode.
			 */
			ifpromisc(ifs, 0);
			bridge_mutecaps(bif_info, ifs, 0);
			break;

		case IFT_GIF:
			break;

		default:
			panic("bridge_delete_member: impossible");
			break;
		}
	}

	/*
	 * Remove bifs from percpu linked list.
	 *
	 * Removed bifs are not freed immediately, instead,
	 * they are saved in saved_bifs.  They will be freed
	 * after we make sure that no one is accessing them,
	 * i.e. after following netmsg_service_sync()
	 */
	TAILQ_INIT(&saved_bifs);
	bridge_del_bif(sc, bif_info, &saved_bifs);

	/*
	 * Make sure that all protocol threads:
	 * o  see 'ifs' if_bridge is changed
	 * o  know that bif is removed from the percpu linked list
	 */
	netmsg_service_sync();

	/*
	 * Free the removed bifs
	 */
	KKASSERT(!TAILQ_EMPTY(&saved_bifs));
	while ((bif = TAILQ_FIRST(&saved_bifs)) != NULL) {
		TAILQ_REMOVE(&saved_bifs, bif, bif_next);
		kfree(bif, M_DEVBUF);
	}

	/* See the comment in bridge_ioctl_stop() */
	bridge_rtmsg_sync(sc);
	bridge_rtdelete(sc, ifs, IFBF_FLUSHALL | IFBF_FLUSHSYNC);

	ifnet_serialize_all(bifp);

	if (bifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	/*
	 * Free the bif_info after bstp_initialization(), so that
	 * bridge_softc.sc_root_port will not reference a dangling
	 * pointer.
	 */
	kfree(bif_info, M_DEVBUF);
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

	TAILQ_REMOVE(&sc->sc_iflists[mycpuid], bif, bif_next);
	kfree(bif, M_DEVBUF);
}

static int
bridge_ioctl_init(struct bridge_softc *sc, void *arg __unused)
{
	struct ifnet *ifp = sc->sc_ifp;

	if (ifp->if_flags & IFF_RUNNING)
		return 0;

	callout_reset(&sc->sc_brcallout, bridge_rtable_prune_period * hz,
	    bridge_timer, sc);

	ifp->if_flags |= IFF_RUNNING;
	bstp_initialization(sc);
	return 0;
}

static int
bridge_ioctl_stop(struct bridge_softc *sc, void *arg __unused)
{
	struct ifnet *ifp = sc->sc_ifp;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return 0;

	callout_stop(&sc->sc_brcallout);

	crit_enter();
	lwkt_dropmsg(&sc->sc_brtimemsg.lmsg);
	crit_exit();

	bstp_stop(sc);

	ifp->if_flags &= ~IFF_RUNNING;

	ifnet_deserialize_all(ifp);

	/* Let everyone know that we are stopped */
	netmsg_service_sync();

	/*
	 * Sync ifnetX msgports in the order we forward rtnode
	 * installation message.  This is used to make sure that
	 * all rtnode installation messages sent by bridge_rtupdate()
	 * during above netmsg_service_sync() are flushed.
	 */
	bridge_rtmsg_sync(sc);
	bridge_rtflush(sc, IFBF_FLUSHDYN | IFBF_FLUSHSYNC);

	ifnet_serialize_all(ifp);
	return 0;
}

static int
bridge_ioctl_add(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;
	struct bridge_ifinfo *bif_info;
	struct ifnet *ifs, *bifp;
	int error = 0;

	bifp = sc->sc_ifp;
	ASSERT_IFNET_SERIALIZED_ALL(bifp);

	ifs = ifunit_netisr(req->ifbr_ifsname);
	if (ifs == NULL)
		return (ENOENT);

	/* If it's in the span list, it can't be a member. */
	TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next)
		if (ifs == bif->bif_ifp)
			return (EBUSY);

	/* Allow the first Ethernet member to define the MTU */
	if (ifs->if_type != IFT_GIF) {
		if (TAILQ_EMPTY(&sc->sc_iflists[mycpuid])) {
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

	bif_info = kmalloc(sizeof(*bif_info), M_DEVBUF, M_WAITOK | M_ZERO);
	bif_info->bifi_priority = BSTP_DEFAULT_PORT_PRIORITY;
	bif_info->bifi_path_cost = BSTP_DEFAULT_PATH_COST;
	bif_info->bifi_ifp = ifs;
	bif_info->bifi_bond_weight = 1;

	/*
	 * Release bridge interface's serializer:
	 * - To avoid possible dead lock.
	 * - Various sync operation will block the current thread.
	 */
	ifnet_deserialize_all(bifp);

	switch (ifs->if_type) {
	case IFT_ETHER:
	case IFT_L2VLAN:
		/*
		 * Place the interface into promiscuous mode.
		 */
		error = ifpromisc(ifs, 1);
		if (error) {
			ifnet_serialize_all(bifp);
			goto out;
		}
		bridge_mutecaps(bif_info, ifs, 1);
		break;

	case IFT_GIF: /* :^) */
		break;

	default:
		error = EINVAL;
		ifnet_serialize_all(bifp);
		goto out;
	}

	/*
	 * Add bifs to percpu linked lists
	 */
	bridge_add_bif(sc, bif_info, ifs);

	ifnet_serialize_all(bifp);

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
		if (bif_info != NULL)
			kfree(bif_info, M_DEVBUF);
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
	bridge_ioctl_fillflags(sc, bif, req);
	return (0);
}

static void
bridge_ioctl_fillflags(struct bridge_softc *sc, struct bridge_iflist *bif,
		       struct ifbreq *req)
{
	req->ifbr_ifsflags = bif->bif_flags;
	req->ifbr_state = bif->bif_state;
	req->ifbr_priority = bif->bif_priority;
	req->ifbr_path_cost = bif->bif_path_cost;
	req->ifbr_bond_weight = bif->bif_bond_weight;
	req->ifbr_portno = bif->bif_ifp->if_index & 0xff;
	if (bif->bif_flags & IFBIF_STP) {
		req->ifbr_peer_root = bif->bif_peer_root;
		req->ifbr_peer_bridge = bif->bif_peer_bridge;
		req->ifbr_peer_cost = bif->bif_peer_cost;
		req->ifbr_peer_port = bif->bif_peer_port;
		if (bstp_supersedes_port_info(sc, bif)) {
			req->ifbr_designated_root = bif->bif_peer_root;
			req->ifbr_designated_bridge = bif->bif_peer_bridge;
			req->ifbr_designated_cost = bif->bif_peer_cost;
			req->ifbr_designated_port = bif->bif_peer_port;
		} else {
			req->ifbr_designated_root = sc->sc_bridge_id;
			req->ifbr_designated_bridge = sc->sc_bridge_id;
			req->ifbr_designated_cost = bif->bif_path_cost +
						    bif->bif_peer_cost;
			req->ifbr_designated_port = bif->bif_port_id;
		}
	} else {
		req->ifbr_peer_root = 0;
		req->ifbr_peer_bridge = 0;
		req->ifbr_peer_cost = 0;
		req->ifbr_peer_port = 0;
		req->ifbr_designated_root = 0;
		req->ifbr_designated_bridge = 0;
		req->ifbr_designated_cost = 0;
		req->ifbr_designated_port = 0;
	}
}

static int
bridge_ioctl_sifflags(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;
	struct ifnet *bifp = sc->sc_ifp;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	if (req->ifbr_ifsflags & IFBIF_SPAN) {
		/* SPAN is readonly */
		return (EINVAL);
	}

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

	bif->bif_flags = (bif->bif_flags & IFBIF_KEEPMASK) |
			 (req->ifbr_ifsflags & ~IFBIF_KEEPMASK);
	if (bifp->if_flags & IFF_RUNNING)
		bstp_initialization(sc);

	return (0);
}

static int
bridge_ioctl_scache(struct bridge_softc *sc, void *arg)
{
	struct ifbrparam *param = arg;
	struct ifnet *ifp = sc->sc_ifp;

	sc->sc_brtmax = param->ifbrp_csize;

	ifnet_deserialize_all(ifp);
	bridge_rttrim(sc);
	ifnet_serialize_all(ifp);

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
	struct bridge_control_arg *bc_arg = arg;
	struct ifbifconf *bifc = arg;
	struct bridge_iflist *bif;
	struct ifbreq *breq;
	int count, len;

	count = 0;
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next)
		count++;
	TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next)
		count++;

	if (bifc->ifbic_len == 0) {
		bifc->ifbic_len = sizeof(*breq) * count;
		return 0;
	} else if (count == 0 || bifc->ifbic_len < sizeof(*breq)) {
		bifc->ifbic_len = 0;
		return 0;
	}

	len = min(bifc->ifbic_len, sizeof(*breq) * count);
	KKASSERT(len >= sizeof(*breq));

	breq = kmalloc(len, M_TEMP, M_WAITOK | M_NULLOK | M_ZERO);
	if (breq == NULL) {
		bifc->ifbic_len = 0;
		return ENOMEM;
	}
	bc_arg->bca_kptr = breq;

	count = 0;
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if (len < sizeof(*breq))
			break;

		strlcpy(breq->ifbr_ifsname, bif->bif_ifp->if_xname,
			sizeof(breq->ifbr_ifsname));
		bridge_ioctl_fillflags(sc, bif, breq);
		breq++;
		count++;
		len -= sizeof(*breq);
	}
	TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next) {
		if (len < sizeof(*breq))
			break;

		strlcpy(breq->ifbr_ifsname, bif->bif_ifp->if_xname,
			sizeof(breq->ifbr_ifsname));
		breq->ifbr_ifsflags = bif->bif_flags;
		breq->ifbr_portno = bif->bif_ifp->if_index & 0xff;
		breq++;
		count++;
		len -= sizeof(*breq);
	}

	bifc->ifbic_len = sizeof(*breq) * count;
	KKASSERT(bifc->ifbic_len > 0);

	bc_arg->bca_len = bifc->ifbic_len;
	bc_arg->bca_uptr = bifc->ifbic_req;
	return 0;
}

static int
bridge_ioctl_rts(struct bridge_softc *sc, void *arg)
{
	struct bridge_control_arg *bc_arg = arg;
	struct ifbaconf *bac = arg;
	struct bridge_rtnode *brt;
	struct ifbareq *bareq;
	int count, len;

	count = 0;
	LIST_FOREACH(brt, &sc->sc_rtlists[mycpuid], brt_list)
		count++;

	if (bac->ifbac_len == 0) {
		bac->ifbac_len = sizeof(*bareq) * count;
		return 0;
	} else if (count == 0 || bac->ifbac_len < sizeof(*bareq)) {
		bac->ifbac_len = 0;
		return 0;
	}

	len = min(bac->ifbac_len, sizeof(*bareq) * count);
	KKASSERT(len >= sizeof(*bareq));

	bareq = kmalloc(len, M_TEMP, M_WAITOK | M_NULLOK | M_ZERO);
	if (bareq == NULL) {
		bac->ifbac_len = 0;
		return ENOMEM;
	}
	bc_arg->bca_kptr = bareq;

	count = 0;
	LIST_FOREACH(brt, &sc->sc_rtlists[mycpuid], brt_list) {
		struct bridge_rtinfo *bri = brt->brt_info;
		time_t expire;

		if (len < sizeof(*bareq))
			break;

		strlcpy(bareq->ifba_ifsname, bri->bri_ifp->if_xname,
			sizeof(bareq->ifba_ifsname));
		memcpy(bareq->ifba_dst, brt->brt_addr, sizeof(brt->brt_addr));
		expire = bri->bri_expire;
		if ((bri->bri_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC &&
		    time_uptime < expire)
			bareq->ifba_expire = expire - time_uptime;
		else
			bareq->ifba_expire = 0;
		bareq->ifba_flags = bri->bri_flags;
		bareq++;
		count++;
		len -= sizeof(*bareq);
	}

	bac->ifbac_len = sizeof(*bareq) * count;
	KKASSERT(bac->ifbac_len > 0);

	bc_arg->bca_len = bac->ifbac_len;
	bc_arg->bca_uptr = bac->ifbac_req;
	return 0;
}

static int
bridge_ioctl_saddr(struct bridge_softc *sc, void *arg)
{
	struct ifbareq *req = arg;
	struct bridge_iflist *bif;
	struct ifnet *ifp = sc->sc_ifp;
	int error;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	bif = bridge_lookup_member(sc, req->ifba_ifsname);
	if (bif == NULL)
		return (ENOENT);

	ifnet_deserialize_all(ifp);
	error = bridge_rtsaddr(sc, req->ifba_dst, bif->bif_ifp,
			       req->ifba_flags);
	ifnet_serialize_all(ifp);
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
	struct ifnet *ifp = sc->sc_ifp;
	int error;

	ifnet_deserialize_all(ifp);
	error = bridge_rtdaddr(sc, req->ifba_dst);
	ifnet_serialize_all(ifp);
	return error;
}

static int
bridge_ioctl_flush(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct ifnet *ifp = sc->sc_ifp;

	ifnet_deserialize_all(ifp);
	bridge_rtflush(sc, req->ifbr_ifsflags | IFBF_FLUSHSYNC);
	ifnet_serialize_all(ifp);

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
bridge_ioctl_reinit(struct bridge_softc *sc, void *arg __unused)
{
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
bridge_ioctl_sifbondwght(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;

	bif = bridge_lookup_member(sc, req->ifbr_ifsname);
	if (bif == NULL)
		return (ENOENT);

	bif->bif_bond_weight = req->ifbr_bond_weight;

	/* no reinit needed */

	return (0);
}

static int
bridge_ioctl_addspan(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;
	struct ifnet *ifs;
	struct bridge_ifinfo *bif_info;

	ifs = ifunit_netisr(req->ifbr_ifsname);
	if (ifs == NULL)
		return (ENOENT);

	TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next)
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

	/*
	 * bif_info is needed for bif_flags
	 */
        bif_info = kmalloc(sizeof(*bif_info), M_DEVBUF, M_WAITOK | M_ZERO);
        bif_info->bifi_ifp = ifs;

	bif = kmalloc(sizeof(*bif), M_DEVBUF, M_WAITOK | M_ZERO);
	bif->bif_ifp = ifs;
	bif->bif_info = bif_info;
	bif->bif_flags = IFBIF_SPAN;
	/* NOTE: span bif does not need bridge_ifinfo */

	TAILQ_INSERT_HEAD(&sc->sc_spanlist, bif, bif_next);

	sc->sc_span = 1;

	return (0);
}

static int
bridge_ioctl_delspan(struct bridge_softc *sc, void *arg)
{
	struct ifbreq *req = arg;
	struct bridge_iflist *bif;
	struct ifnet *ifs;

	ifs = ifunit_netisr(req->ifbr_ifsname);
	if (ifs == NULL)
		return (ENOENT);

	TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next)
		if (ifs == bif->bif_ifp)
			break;

	if (bif == NULL)
		return (ENOENT);

	bridge_delete_span(sc, bif);

	if (TAILQ_EMPTY(&sc->sc_spanlist))
		sc->sc_span = 0;

	return (0);
}

static void
bridge_ifdetach_dispatch(netmsg_t msg)
{
	struct ifnet *ifp, *bifp;
	struct bridge_softc *sc;
	struct bridge_iflist *bif;

	ifp = msg->lmsg.u.ms_resultp;
	sc = ifp->if_bridge;

	/* Check if the interface is a bridge member */
	if (sc != NULL) {
		bifp = sc->sc_ifp;

		ifnet_serialize_all(bifp);

		bif = bridge_lookup_member_if(sc, ifp);
		if (bif != NULL) {
			bridge_delete_member(sc, bif, 1);
		} else {
			/* XXX Why bif will be NULL? */
		}

		ifnet_deserialize_all(bifp);
		goto reply;
	}

	crit_enter();	/* XXX MP */

	/* Check if the interface is a span port */
	LIST_FOREACH(sc, &bridge_list, sc_list) {
		bifp = sc->sc_ifp;

		ifnet_serialize_all(bifp);

		TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next)
			if (ifp == bif->bif_ifp) {
				bridge_delete_span(sc, bif);
				break;
			}

		ifnet_deserialize_all(bifp);
	}

	crit_exit();

reply:
	lwkt_replymsg(&msg->lmsg, 0);
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
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, bridge_ifdetach_dispatch);
	msg.lmsg.u.ms_resultp = ifp;

	lwkt_domsg(BRIDGE_CFGPORT, &msg.lmsg, 0);
}

/*
 * bridge_init:
 *
 *	Initialize a bridge interface.
 */
static void
bridge_init(void *xsc)
{
	bridge_control(xsc, SIOCSIFFLAGS, bridge_ioctl_init, NULL);
}

/*
 * bridge_stop:
 *
 *	Stop the bridge interface.
 */
static void
bridge_stop(struct ifnet *ifp)
{
	bridge_control(ifp->if_softc, SIOCSIFFLAGS, bridge_ioctl_stop, NULL);
}

/*
 * Returns TRUE if the packet is being sent 'from us'... from our bridge
 * interface or from any member of our bridge interface.  This is used
 * later on to force the MAC to be the MAC of our bridge interface.
 */
static int
bridge_from_us(struct bridge_softc *sc, struct ether_header *eh)
{
	struct bridge_iflist *bif;

	if (memcmp(eh->ether_shost, IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN) == 0)
		return (1);

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if (memcmp(eh->ether_shost, IF_LLADDR(bif->bif_ifp),
			   ETHER_ADDR_LEN) == 0) {
			return (1);
		}
	}
	return (0);
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
	struct netmsg_packet *nmp;

	mbuftrackid(m, 64);

	nmp = &m->m_hdr.mh_netmsg;
	netmsg_init(&nmp->base, NULL, &netisr_apanic_rport,
		    0, bridge_enqueue_handler);
	nmp->nm_packet = m;
	nmp->base.lmsg.u.ms_resultp = dst_ifp;

	lwkt_sendmsg_oncpu(netisr_cpuport(mycpuid), &nmp->base.lmsg);
}

/*
 * After looking up dst_if in our forwarding table we still have to
 * deal with channel bonding.  Find the best interface in the bonding set.
 */
static struct ifnet *
bridge_select_unicast(struct bridge_softc *sc, struct ifnet *dst_if,
		      int from_blocking, struct mbuf *m)
{
	struct bridge_iflist *bif, *nbif;
	struct ifnet *alt_if;
	int alt_priority;
	int priority;

	/*
	 * Unicast, kinda replicates the output side of bridge_output().
	 *
	 * Even though this is a uni-cast packet we may have to select
	 * an interface from a bonding set.
	 */
	bif = bridge_lookup_member_if(sc, dst_if);
	if (bif == NULL) {
		/* Not a member of the bridge (anymore?) */
		return NULL;
	}

	/*
	 * If STP is enabled on the target we are an equal opportunity
	 * employer and do not necessarily output to dst_if.  Instead
	 * scan available links with the same MAC as the current dst_if
	 * and choose the best one.
	 *
	 * We also need to do this because arp entries tag onto a particular
	 * interface and if it happens to be dead then the packets will
	 * go into a bit bucket.
	 *
	 * If LINK2 is set the matching links are bonded and we-round robin.
	 * (the MAC address must be the same for the participating links).
	 * In this case links in a STP FORWARDING or BONDED state are
	 * allowed for unicast packets.
	 */
	if (bif->bif_flags & IFBIF_STP) {
		alt_if = NULL;
		alt_priority = 0;
		priority = 0;

		TAILQ_FOREACH_MUTABLE(bif, &sc->sc_iflists[mycpuid],
				     bif_next, nbif) {
			/*
			 * dst_if may imply a bonding set so we must compare
			 * MAC addresses.
			 */
			if (memcmp(IF_LLADDR(bif->bif_ifp),
				   IF_LLADDR(dst_if),
				   ETHER_ADDR_LEN) != 0) {
				continue;
			}

			if ((bif->bif_ifp->if_flags & IFF_RUNNING) == 0)
				continue;

			/*
			 * NOTE: We allow tranmissions through a BLOCKING
			 *	 or LEARNING interface only as a last resort.
			 *	 We DISALLOW both cases if the receiving
			 *
			 * NOTE: If we send a packet through a learning
			 *	 interface the receiving end (if also in
			 *	 LEARNING) will throw it away, so this is
			 *	 the ultimate last resort.
			 */
			switch(bif->bif_state) {
			case BSTP_IFSTATE_BLOCKING:
				if (from_blocking == 0 &&
				    bif->bif_priority + 256 > alt_priority) {
					alt_priority = bif->bif_priority + 256;
					alt_if = bif->bif_ifp;
				}
				continue;
			case BSTP_IFSTATE_LEARNING:
				if (from_blocking == 0 &&
				    bif->bif_priority > alt_priority) {
					alt_priority = bif->bif_priority;
					alt_if = bif->bif_ifp;
				}
				continue;
			case BSTP_IFSTATE_L1BLOCKING:
			case BSTP_IFSTATE_LISTENING:
			case BSTP_IFSTATE_DISABLED:
				continue;
			default:
				/* FORWARDING, BONDED */
				break;
			}

			/*
			 * XXX we need to use the toepliz hash or
			 *     something like that instead of
			 *     round-robining.
			 */
			if (sc->sc_ifp->if_flags & IFF_LINK2) {
				dst_if = bif->bif_ifp;
				if (++bif->bif_bond_count >=
				    bif->bif_bond_weight) {
					bif->bif_bond_count = 0;
					TAILQ_REMOVE(&sc->sc_iflists[mycpuid],
						     bif, bif_next);
					TAILQ_INSERT_TAIL(
						     &sc->sc_iflists[mycpuid],
						     bif, bif_next);
				}
				priority = 1;
				break;
			}

			/*
			 * Select best interface in the FORWARDING or
			 * BONDED set.  Well, there shouldn't be any
			 * in a BONDED state if LINK2 is not set (they
			 * will all be in a BLOCKING) state, but there
			 * could be a transitory condition here.
			 */
			if (bif->bif_priority > priority) {
				priority = bif->bif_priority;
				dst_if = bif->bif_ifp;
			}
		}

		/*
		 * If no suitable interfaces were found but a suitable
		 * alternative interface was found, use the alternative
		 * interface.
		 */
		if (priority == 0 && alt_if)
			dst_if = alt_if;
	}

	/*
	 * At this point, we're dealing with a unicast frame
	 * going to a different interface.
	 */
	if ((dst_if->if_flags & IFF_RUNNING) == 0)
		dst_if = NULL;
	return (dst_if);
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
	struct bridge_iflist *bif, *nbif;
	struct ether_header *eh;
	struct ifnet *dst_if, *alt_if, *bifp;
	int from_us;
	int alt_priority;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);
	mbuftrackid(m, 65);

	/*
	 * Make sure that we are still a member of a bridge interface.
	 */
	if (sc == NULL) {
		m_freem(m);
		return (0);
	}
	bifp = sc->sc_ifp;

	/*
	 * Acquire header
	 */
	if (m->m_len < ETHER_HDR_LEN) {
		m = m_pullup(m, ETHER_HDR_LEN);
		if (m == NULL) {
			IFNET_STAT_INC(bifp, oerrors, 1);
			return (0);
		}
	}
	eh = mtod(m, struct ether_header *);
	from_us = bridge_from_us(sc, eh);

	/*
	 * If bridge is down, but the original output interface is up,
	 * go ahead and send out that interface.  Otherwise, the packet
	 * is dropped below.
	 */
	if ((bifp->if_flags & IFF_RUNNING) == 0) {
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
		struct mbuf *mc;
		int used = 0;
		int found = 0;

		if (sc->sc_span)
			bridge_span(sc, m);

		alt_if = NULL;
		alt_priority = 0;
		TAILQ_FOREACH_MUTABLE(bif, &sc->sc_iflists[mycpuid],
				     bif_next, nbif) {
			dst_if = bif->bif_ifp;

			if ((dst_if->if_flags & IFF_RUNNING) == 0)
				continue;

			/*
			 * If this is not the original output interface,
			 * and the interface is participating in spanning
			 * tree, make sure the port is in a state that
			 * allows forwarding.
			 *
			 * We keep track of a possible backup IF if we are
			 * unable to find any interfaces to forward through.
			 *
			 * NOTE: Currently round-robining is not implemented
			 *	 across bonded interface groups (needs an
			 *	 algorithm to track each group somehow).
			 *
			 *	 Similarly we track only one alternative
			 *	 interface if no suitable interfaces are
			 *	 found.
			 */
			if (dst_if != ifp &&
			    (bif->bif_flags & IFBIF_STP) != 0) {
				switch (bif->bif_state) {
				case BSTP_IFSTATE_BONDED:
					if (bif->bif_priority + 512 >
					    alt_priority) {
						alt_priority =
						    bif->bif_priority + 512;
						alt_if = bif->bif_ifp;
					}
					continue;
				case BSTP_IFSTATE_BLOCKING:
					if (bif->bif_priority + 256 >
					    alt_priority) {
						alt_priority =
						    bif->bif_priority + 256;
						alt_if = bif->bif_ifp;
					}
					continue;
				case BSTP_IFSTATE_LEARNING:
					if (bif->bif_priority > alt_priority) {
						alt_priority =
						    bif->bif_priority;
						alt_if = bif->bif_ifp;
					}
					continue;
				case BSTP_IFSTATE_L1BLOCKING:
				case BSTP_IFSTATE_LISTENING:
				case BSTP_IFSTATE_DISABLED:
					continue;
				default:
					/* FORWARDING */
					break;
				}
			}

			KKASSERT(used == 0);
			if (TAILQ_NEXT(bif, bif_next) == NULL) {
				used = 1;
				mc = m;
			} else {
				mc = m_copypacket(m, MB_DONTWAIT);
				if (mc == NULL) {
					IFNET_STAT_INC(bifp, oerrors, 1);
					continue;
				}
			}

			/*
			 * If the packet is 'from' us override ether_shost.
			 */
			bridge_handoff(sc, dst_if, mc, from_us);
			found = 1;

			if (nbif != NULL && !nbif->bif_onlist) {
				KKASSERT(bif->bif_onlist);
				nbif = TAILQ_NEXT(bif, bif_next);
			}
		}

		/*
		 * If we couldn't find anything use the backup interface
		 * if we have one.
		 */
		if (found == 0 && alt_if) {
			KKASSERT(used == 0);
			mc = m;
			used = 1;
			bridge_handoff(sc, alt_if, mc, from_us);
		}

		if (used == 0)
			m_freem(m);
		return (0);
	}

	/*
	 * Unicast
	 */
sendunicast:
	dst_if = bridge_select_unicast(sc, dst_if, 0, m);

	if (sc->sc_span)
		bridge_span(sc, m);
	if (dst_if == NULL)
		m_freem(m);
	else
		bridge_handoff(sc, dst_if, m, from_us);
	return (0);
}

/*
 * Returns the bridge interface associated with an ifc.
 * Pass ifp->if_bridge (must not be NULL).  Used by the ARP
 * code to supply the bridge for the is-at info, making
 * the bridge responsible for matching local addresses.
 *
 * Without this the ARP code will supply bridge member interfaces
 * for the is-at which makes it difficult the bridge to fail-over
 * interfaces (amoung other things).
 */
static struct ifnet *
bridge_interface(void *if_bridge)
{
	struct bridge_softc *sc = if_bridge;
	return (sc->sc_ifp);
}

/*
 * bridge_start:
 *
 *	Start output on a bridge.
 */
static void
bridge_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct bridge_softc *sc = ifp->if_softc;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_ALTQ_SQ_SERIALIZED_HW(ifsq);

	ifsq_set_oactive(ifsq);
	for (;;) {
		struct ifnet *dst_if = NULL;
		struct ether_header *eh;
		struct mbuf *m;

		m = ifsq_dequeue(ifsq);
		if (m == NULL)
			break;
		mbuftrackid(m, 75);

		if (m->m_len < sizeof(*eh)) {
			m = m_pullup(m, sizeof(*eh));
			if (m == NULL) {
				IFNET_STAT_INC(ifp, oerrors, 1);
				continue;
			}
		}
		eh = mtod(m, struct ether_header *);

		BPF_MTAP(ifp, m);
		IFNET_STAT_INC(ifp, opackets, 1);

		if ((m->m_flags & (M_BCAST|M_MCAST)) == 0)
			dst_if = bridge_rtlookup(sc, eh->ether_dhost);

		/*
		 * Multicast or broadcast
		 */
		if (dst_if == NULL) {
			bridge_start_bcast(sc, m);
			continue;
		}

		/*
		 * Unicast
		 */
		dst_if = bridge_select_unicast(sc, dst_if, 0, m);

		if (dst_if == NULL)
			m_freem(m);
		else
			bridge_enqueue(dst_if, m);
	}
	ifsq_clr_oactive(ifsq);
}

/*
 * bridge_forward:
 *
 *	Forward packets received on a bridge interface via the input
 *	path.
 *
 *	This implements the forwarding function of the bridge.
 */
static void
bridge_forward(struct bridge_softc *sc, struct mbuf *m)
{
	struct bridge_iflist *bif;
	struct ifnet *src_if, *dst_if, *ifp;
	struct ether_header *eh;
	int from_blocking;

	mbuftrackid(m, 66);
	src_if = m->m_pkthdr.rcvif;
	ifp = sc->sc_ifp;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	/*
	 * packet coming in on the bridge is also going out on the bridge,
	 * but ether code won't adjust output stats for the bridge because
	 * we are changing the interface to something else.
	 */
	IFNET_STAT_INC(ifp, opackets, 1);
	IFNET_STAT_INC(ifp, obytes, m->m_pkthdr.len);

	/*
	 * Look up the bridge_iflist.
	 */
	bif = bridge_lookup_member_if(sc, src_if);
	if (bif == NULL) {
		/* Interface is not a bridge member (anymore?) */
		m_freem(m);
		return;
	}

	/*
	 * In spanning tree mode receiving a packet from an interface
	 * in a BLOCKING state is allowed, it could be a member of last
	 * resort from the sender's point of view, but forwarding it is
	 * not allowed.
	 *
	 * The sender's spanning tree will eventually sync up and the
	 * sender will go into a BLOCKING state too (but this still may be
	 * an interface of last resort during state changes).
	 */
	if (bif->bif_flags & IFBIF_STP) {
		switch (bif->bif_state) {
		case BSTP_IFSTATE_L1BLOCKING:
		case BSTP_IFSTATE_LISTENING:
		case BSTP_IFSTATE_DISABLED:
			m_freem(m);
			return;
		default:
			/* learning, blocking, bonded, forwarding */
			break;
		}
		from_blocking = (bif->bif_state == BSTP_IFSTATE_BLOCKING);
	} else {
		from_blocking = 0;
	}

	eh = mtod(m, struct ether_header *);

	/*
	 * If the interface is learning, and the source
	 * address is valid and not multicast, record
	 * the address.
	 */
	if ((bif->bif_flags & IFBIF_LEARNING) != 0 &&
	    from_blocking == 0 &&
	    ETHER_IS_MULTICAST(eh->ether_shost) == 0 &&
	    (eh->ether_shost[0] == 0 &&
	     eh->ether_shost[1] == 0 &&
	     eh->ether_shost[2] == 0 &&
	     eh->ether_shost[3] == 0 &&
	     eh->ether_shost[4] == 0 &&
	     eh->ether_shost[5] == 0) == 0) {
		bridge_rtupdate(sc, eh->ether_shost, src_if, IFBAF_DYNAMIC);
	}

	/*
	 * Don't forward from an interface in the listening or learning
	 * state.  That is, in the learning state we learn information
	 * but we throw away the packets.
	 *
	 * We let through packets on interfaces in the blocking state.
	 * The blocking state is applicable to the send side, not the
	 * receive side.
	 */
	if ((bif->bif_flags & IFBIF_STP) != 0 &&
	    (bif->bif_state == BSTP_IFSTATE_LISTENING ||
	     bif->bif_state == BSTP_IFSTATE_LEARNING)) {
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
	 *
	 * src_if implies the entire bonding set so we have to compare MAC
	 * addresses and not just if pointers.
	 */
	if ((m->m_flags & (M_BCAST|M_MCAST)) == 0) {
		dst_if = bridge_rtlookup(sc, eh->ether_dhost);
		if (dst_if && memcmp(IF_LLADDR(src_if), IF_LLADDR(dst_if),
				     ETHER_ADDR_LEN) == 0) {
			m_freem(m);
			return;
		}
	} else {
		/* ...forward it to all interfaces. */
		IFNET_STAT_INC(ifp, imcasts, 1);
		dst_if = NULL;
	}

	/*
	 * Brodcast if we do not have forwarding information.  However, if
	 * we received the packet on a blocking interface we do not do this
	 * (unless you really want to blow up your network).
	 */
	if (dst_if == NULL) {
		if (from_blocking)
			m_freem(m);
		else
			bridge_broadcast(sc, src_if, m);
		return;
	}

	dst_if = bridge_select_unicast(sc, dst_if, from_blocking, m);

	if (dst_if == NULL) {
		m_freem(m);
		return;
	}

	if (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
	    || inet6_pfil_hook.ph_hashooks > 0
#endif
	    ) {
		if (bridge_pfil(&m, ifp, src_if, PFIL_IN) != 0)
			return;
		if (m == NULL)
			return;

		if (bridge_pfil(&m, ifp, dst_if, PFIL_OUT) != 0)
			return;
		if (m == NULL)
			return;
	}
	bridge_handoff(sc, dst_if, m, 0);
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

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);
	mbuftrackid(m, 67);

	/*
	 * Make sure that we are still a member of a bridge interface.
	 */
	if (sc == NULL)
		return m;

	new_ifp = NULL;
	bifp = sc->sc_ifp;

	if ((bifp->if_flags & IFF_RUNNING) == 0)
		goto out;

	/*
	 * Implement support for bridge monitoring.  If this flag has been
	 * set on this interface, discard the packet once we push it through
	 * the bpf(4) machinery, but before we do, increment various counters
	 * associated with this bridge.
	 */
	if (bifp->if_flags & IFF_MONITOR) {
		/*
		 * Change input interface to this bridge
		 *
		 * Update bridge's ifnet statistics
		 */
		m->m_pkthdr.rcvif = bifp;

		BPF_MTAP(bifp, m);
		IFNET_STAT_INC(bifp, ipackets, 1);
		IFNET_STAT_INC(bifp, ibytes, m->m_pkthdr.len);
		if (m->m_flags & (M_MCAST | M_BCAST))
			IFNET_STAT_INC(bifp, imcasts, 1);

		m_freem(m);
		m = NULL;
		goto out;
	}

	/*
	 * Handle the ether_header
	 *
	 * In all cases if the packet is destined for us via our MAC
	 * we must clear BRIDGE_MBUF_TAGGED to ensure that we don't
	 * repeat the source MAC out the same interface.
	 *
	 * This first test against our bridge MAC is the fast-path.
	 *
	 * NOTE!  The bridge interface can serve as an endpoint for
	 *	  communication but normally there are no IPs associated
	 *	  with it so you cannot route through it.  Instead what
	 *	  you do is point your default route *THROUGH* the bridge
	 *	  to the actual default router for one of the bridged spaces.
	 *
	 *	  Another possibility is to put all your IP specifications
	 *	  on the bridge instead of on the individual interfaces.  If
	 *	  you do this it should be possible to use the bridge as an
	 *	  end point and route (rather than switch) through it using
	 *	  the default route or ipfw forwarding rules.
	 */

	/*
	 * Acquire header
	 */
	if (m->m_len < ETHER_HDR_LEN) {
		m = m_pullup(m, ETHER_HDR_LEN);
		if (m == NULL)
			goto out;
	}
	eh = mtod(m, struct ether_header *);
	m->m_pkthdr.fw_flags |= BRIDGE_MBUF_TAGGED;
	bcopy(eh->ether_shost, m->m_pkthdr.ether_br_shost, ETHER_ADDR_LEN);

	if ((bridge_debug & 1) &&
	    (ntohs(eh->ether_type) == ETHERTYPE_ARP ||
	    ntohs(eh->ether_type) == ETHERTYPE_REVARP)) {
		kprintf("%02x:%02x:%02x:%02x:%02x:%02x "
			"%02x:%02x:%02x:%02x:%02x:%02x type %04x "
			"lla %02x:%02x:%02x:%02x:%02x:%02x\n",
			eh->ether_dhost[0],
			eh->ether_dhost[1],
			eh->ether_dhost[2],
			eh->ether_dhost[3],
			eh->ether_dhost[4],
			eh->ether_dhost[5],
			eh->ether_shost[0],
			eh->ether_shost[1],
			eh->ether_shost[2],
			eh->ether_shost[3],
			eh->ether_shost[4],
			eh->ether_shost[5],
			eh->ether_type,
			((u_char *)IF_LLADDR(bifp))[0],
			((u_char *)IF_LLADDR(bifp))[1],
			((u_char *)IF_LLADDR(bifp))[2],
			((u_char *)IF_LLADDR(bifp))[3],
			((u_char *)IF_LLADDR(bifp))[4],
			((u_char *)IF_LLADDR(bifp))[5]
		);
	}

	/*
	 * If the packet is for us, set the packets source as the
	 * bridge, and return the packet back to ifnet.if_input for
	 * local processing.
	 */
	if (memcmp(eh->ether_dhost, IF_LLADDR(bifp), ETHER_ADDR_LEN) == 0) {
		/*
		 * We must still record the source interface in our
		 * addr cache, otherwise our bridge won't know where
		 * to send responses and will broadcast them.
		 */
		bif = bridge_lookup_member_if(sc, ifp);
		if ((bif->bif_flags & IFBIF_LEARNING) &&
		    ((bif->bif_flags & IFBIF_STP) == 0 ||
		     bif->bif_state != BSTP_IFSTATE_BLOCKING)) {
			bridge_rtupdate(sc, eh->ether_shost,
					ifp, IFBAF_DYNAMIC);
		}

		/*
		 * Perform pfil hooks.
		 */
		m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
		KASSERT(bifp->if_bridge == NULL,
			("loop created in bridge_input"));
		if (pfil_member != 0) {
			if (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
			    || inet6_pfil_hook.ph_hashooks > 0
#endif
			) {
				if (bridge_pfil(&m, NULL, ifp, PFIL_IN) != 0)
					goto out;
				if (m == NULL)
					goto out;
			}
		}

		/*
		 * Set new_ifp and skip to the end.  This will trigger code
		 * to reinput the packet and run it into our stack.
		 */
		new_ifp = bifp;
		goto out;
	}

	/*
	 * Tap all packets arriving on the bridge, no matter if
	 * they are local destinations or not.  In is in.
	 *
	 * Update bridge's ifnet statistics
	 */
	BPF_MTAP(bifp, m);
	IFNET_STAT_INC(bifp, ipackets, 1);
	IFNET_STAT_INC(bifp, ibytes, m->m_pkthdr.len);
	if (m->m_flags & (M_MCAST | M_BCAST))
		IFNET_STAT_INC(bifp, imcasts, 1);

	bif = bridge_lookup_member_if(sc, ifp);
	if (bif == NULL)
		goto out;

	if (sc->sc_span)
		bridge_span(sc, m);

	if (m->m_flags & (M_BCAST | M_MCAST)) {
		/*
		 * Tap off 802.1D packets; they do not get forwarded.
		 */
		if (memcmp(eh->ether_dhost, bstp_etheraddr,
			    ETHER_ADDR_LEN) == 0) {
			ifnet_serialize_all(bifp);
			bstp_input(sc, bif, m);
			ifnet_deserialize_all(bifp);

			/* m is freed by bstp_input */
			m = NULL;
			goto out;
		}

		/*
		 * Other than 802.11d packets, ignore packets if the
		 * interface is not in a good state.
		 *
		 * NOTE: Broadcast/mcast packets received on a blocking or
		 *	 learning interface are allowed for local processing.
		 *
		 *	 The sending side of a blocked port will stop
		 *	 transmitting when a better alternative is found.
		 *	 However, later on we will disallow the forwarding
		 *	 of bcast/mcsat packets over a blocking interface.
		 */
		if (bif->bif_flags & IFBIF_STP) {
			switch (bif->bif_state) {
			case BSTP_IFSTATE_L1BLOCKING:
			case BSTP_IFSTATE_LISTENING:
			case BSTP_IFSTATE_DISABLED:
				goto out;
			default:
				/* blocking, learning, bonded, forwarding */
				break;
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

		/*
		 * It's just too dangerous to allow bcast/mcast over a
		 * blocked interface, eventually the network will sort
		 * itself out and a better path will be found.
		 */
		if ((bif->bif_flags & IFBIF_STP) == 0 ||
		    bif->bif_state != BSTP_IFSTATE_BLOCKING) {
			bridge_forward(sc, mc);
		}

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
			/*
			 * Don't tap to bpf(4) again; we have already done
			 * the tapping.
			 *
			 * Leave m_pkthdr.rcvif alone, so ARP replies are
			 * processed as coming in on the correct interface.
			 *
			 * Clear the bridge flag for local processing in
			 * case the packet gets routed.
			 */
			mc2->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			ether_reinput_oncpu(bifp, mc2, 0);
		}

		/* Return the original packet for local processing. */
		goto out;
	}

	/*
	 * Input of a unicast packet.  We have to allow unicast packets
	 * input from links in the BLOCKING state as this might be an
	 * interface of last resort.
	 *
	 * NOTE: We explicitly ignore normal packets received on a link
	 *	 in the BLOCKING state.  The point of being in that state
	 *	 is to avoid getting duplicate packets.
	 *
	 *	 HOWEVER, if LINK2 is set the normal spanning tree code
	 *	 will mark an interface BLOCKING to avoid multi-cast/broadcast
	 *	 loops.  Unicast packets CAN still loop if we allow the
	 *	 case (hence we only do it in LINK2), but it isn't quite as
	 *	 bad as a broadcast packet looping.
	 */
	if (bif->bif_flags & IFBIF_STP) {
		switch (bif->bif_state) {
		case BSTP_IFSTATE_L1BLOCKING:
		case BSTP_IFSTATE_LISTENING:
		case BSTP_IFSTATE_DISABLED:
			goto out;
		default:
			/* blocking, bonded, forwarding, learning */
			break;
		}
	}

	/*
	 * Unicast.  Make sure it's not for us.
	 *
	 * This loop is MPSAFE; the only blocking operation (bridge_rtupdate)
	 * is followed by breaking out of the loop.
	 */
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if (bif->bif_ifp->if_type != IFT_ETHER)
			continue;

		/*
		 * It is destined for an interface linked to the bridge.
		 * We want the bridge itself to take care of link level
		 * forwarding to member interfaces so reinput on the bridge.
		 * i.e. if you ping an IP on a target interface associated
		 * with the bridge, the arp is-at response should indicate
		 * the bridge MAC.
		 *
		 * Only update our addr list when learning if the port
		 * is not in a blocking state.  If it is we still allow
		 * the packet but we do not try to learn from it.
		 */
		if (memcmp(IF_LLADDR(bif->bif_ifp), eh->ether_dhost,
			   ETHER_ADDR_LEN) == 0) {
			if (bif->bif_ifp != ifp) {
				/* XXX loop prevention */
				m->m_flags |= M_ETHER_BRIDGED;
			}
			if ((bif->bif_flags & IFBIF_LEARNING) &&
			    ((bif->bif_flags & IFBIF_STP) == 0 ||
			     bif->bif_state != BSTP_IFSTATE_BLOCKING)) {
				bridge_rtupdate(sc, eh->ether_shost,
						ifp, IFBAF_DYNAMIC);
			}
			new_ifp = bifp; /* not bif->bif_ifp */
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			goto out;
		}

		/*
		 * Ignore received packets that were sent by us.
		 */
		if (memcmp(IF_LLADDR(bif->bif_ifp), eh->ether_shost,
			   ETHER_ADDR_LEN) == 0) {
			m_freem(m);
			m = NULL;
			goto out;
		}
	}

	/*
	 * It isn't for us.
	 *
	 * Perform the bridge forwarding function, but disallow bridging
	 * to interfaces in the blocking state if the packet came in on
	 * an interface in the blocking state.
	 *
	 * (bridge_forward also updates the addr cache).
	 */
	bridge_forward(sc, m);
	m = NULL;

	/*
	 * ether_reinput_oncpu() will reprocess rcvif as
	 * coming from new_ifp (since we do not specify
	 * REINPUT_KEEPRCVIF).
	 */
out:
	if (new_ifp != NULL) {
		/*
		 * Clear the bridge flag for local processing in
		 * case the packet gets routed.
		 */
		ether_reinput_oncpu(new_ifp, m, REINPUT_RUNBPF);
		m = NULL;
	}
	return (m);
}

/*
 * bridge_start_bcast:
 *
 *	Broadcast the packet sent from bridge to all member
 *	interfaces.
 *	This is a simplified version of bridge_broadcast(), however,
 *	this function expects caller to hold bridge's serializer.
 */
static void
bridge_start_bcast(struct bridge_softc *sc, struct mbuf *m)
{
	struct bridge_iflist *bif;
	struct mbuf *mc;
	struct ifnet *dst_if, *alt_if, *bifp;
	int used = 0;
	int found = 0;
	int alt_priority;

	mbuftrackid(m, 68);
	bifp = sc->sc_ifp;
	ASSERT_IFNET_SERIALIZED_ALL(bifp);

	/*
	 * Following loop is MPSAFE; nothing is blocking
	 * in the loop body.
	 *
	 * NOTE: We transmit through an member in the BLOCKING state only
	 *	 as a last resort.
	 */
	alt_if = NULL;
	alt_priority = 0;

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		dst_if = bif->bif_ifp;

		if (bif->bif_flags & IFBIF_STP) {
			switch (bif->bif_state) {
			case BSTP_IFSTATE_BLOCKING:
				if (bif->bif_priority > alt_priority) {
					alt_priority = bif->bif_priority;
					alt_if = bif->bif_ifp;
				}
				/* fall through */
			case BSTP_IFSTATE_L1BLOCKING:
			case BSTP_IFSTATE_DISABLED:
				continue;
			default:
				/* listening, learning, bonded, forwarding */
				break;
			}
		}

		if ((bif->bif_flags & IFBIF_DISCOVER) == 0 &&
		    (m->m_flags & (M_BCAST|M_MCAST)) == 0)
			continue;

		if ((dst_if->if_flags & IFF_RUNNING) == 0)
			continue;

		if (TAILQ_NEXT(bif, bif_next) == NULL) {
			mc = m;
			used = 1;
		} else {
			mc = m_copypacket(m, MB_DONTWAIT);
			if (mc == NULL) {
				IFNET_STAT_INC(bifp, oerrors, 1);
				continue;
			}
		}
		found = 1;
		bridge_enqueue(dst_if, mc);
	}

	if (found == 0 && alt_if) {
		KKASSERT(used == 0);
		mc = m;
		used = 1;
		bridge_enqueue(alt_if, mc);
	}

	if (used == 0)
		m_freem(m);
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
		 struct mbuf *m)
{
	struct bridge_iflist *bif, *nbif;
	struct ether_header *eh;
	struct mbuf *mc;
	struct ifnet *dst_if, *alt_if, *bifp;
	int used;
	int found;
	int alt_priority;
	int from_us;

	mbuftrackid(m, 69);
	bifp = sc->sc_ifp;
	ASSERT_IFNET_NOT_SERIALIZED_ALL(bifp);

	eh = mtod(m, struct ether_header *);
	from_us = bridge_from_us(sc, eh);

	if (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
	    || inet6_pfil_hook.ph_hashooks > 0
#endif
	    ) {
		if (bridge_pfil(&m, bifp, src_if, PFIL_IN) != 0)
			return;
		if (m == NULL)
			return;

		/* Filter on the bridge interface before broadcasting */
		if (bridge_pfil(&m, bifp, NULL, PFIL_OUT) != 0)
			return;
		if (m == NULL)
			return;
	}

	alt_if = NULL;
	alt_priority = 0;
	found = 0;
	used = 0;

	TAILQ_FOREACH_MUTABLE(bif, &sc->sc_iflists[mycpuid], bif_next, nbif) {
		dst_if = bif->bif_ifp;

		if ((dst_if->if_flags & IFF_RUNNING) == 0)
			continue;

		/*
		 * Don't bounce the packet out the same interface it came
		 * in on.  We have to test MAC addresses because a packet
		 * can come in a bonded interface and we don't want it to
		 * be echod out the forwarding interface for the same bonding
		 * set.
		 */
		if (src_if && memcmp(IF_LLADDR(src_if), IF_LLADDR(dst_if),
				     ETHER_ADDR_LEN) == 0) {
			continue;
		}

		/*
		 * Generally speaking we only broadcast through forwarding
		 * interfaces.  If no interfaces are available we select
		 * a BONDED, BLOCKING, or LEARNING interface to forward
		 * through.
		 */
		if (bif->bif_flags & IFBIF_STP) {
			switch (bif->bif_state) {
			case BSTP_IFSTATE_BONDED:
				if (bif->bif_priority + 512 > alt_priority) {
					alt_priority = bif->bif_priority + 512;
					alt_if = bif->bif_ifp;
				}
				continue;
			case BSTP_IFSTATE_BLOCKING:
				if (bif->bif_priority + 256 > alt_priority) {
					alt_priority = bif->bif_priority + 256;
					alt_if = bif->bif_ifp;
				}
				continue;
			case BSTP_IFSTATE_LEARNING:
				if (bif->bif_priority > alt_priority) {
					alt_priority = bif->bif_priority;
					alt_if = bif->bif_ifp;
				}
				continue;
			case BSTP_IFSTATE_L1BLOCKING:
			case BSTP_IFSTATE_DISABLED:
			case BSTP_IFSTATE_LISTENING:
				continue;
			default:
				/* forwarding */
				break;
			}
		}

		if ((bif->bif_flags & IFBIF_DISCOVER) == 0 &&
		    (m->m_flags & (M_BCAST|M_MCAST)) == 0) {
			continue;
		}

		if (TAILQ_NEXT(bif, bif_next) == NULL) {
			mc = m;
			used = 1;
		} else {
			mc = m_copypacket(m, MB_DONTWAIT);
			if (mc == NULL) {
				IFNET_STAT_INC(sc->sc_ifp, oerrors, 1);
				continue;
			}
		}
		found = 1;

		/*
		 * Filter on the output interface.  Pass a NULL bridge
		 * interface pointer so we do not redundantly filter on
		 * the bridge for each interface we broadcast on.
		 */
		if (inet_pfil_hook.ph_hashooks > 0
#ifdef INET6
		    || inet6_pfil_hook.ph_hashooks > 0
#endif
		    ) {
			if (bridge_pfil(&mc, NULL, dst_if, PFIL_OUT) != 0)
				continue;
			if (mc == NULL)
				continue;
		}
		bridge_handoff(sc, dst_if, mc, from_us);

		if (nbif != NULL && !nbif->bif_onlist) {
			KKASSERT(bif->bif_onlist);
			nbif = TAILQ_NEXT(bif, bif_next);
		}
	}

	if (found == 0 && alt_if) {
		KKASSERT(used == 0);
		mc = m;
		used = 1;
		bridge_enqueue(alt_if, mc);
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
	struct ifnet *dst_if, *bifp;
	struct mbuf *mc;

	mbuftrackid(m, 70);
	bifp = sc->sc_ifp;
	ifnet_serialize_all(bifp);

	TAILQ_FOREACH(bif, &sc->sc_spanlist, bif_next) {
		dst_if = bif->bif_ifp;

		if ((dst_if->if_flags & IFF_RUNNING) == 0)
			continue;

		mc = m_copypacket(m, MB_DONTWAIT);
		if (mc == NULL) {
			IFNET_STAT_INC(sc->sc_ifp, oerrors, 1);
			continue;
		}
		bridge_enqueue(dst_if, mc);
	}

	ifnet_deserialize_all(bifp);
}

static void
bridge_rtmsg_sync_handler(netmsg_t msg)
{
	ifnet_forwardmsg(&msg->lmsg, mycpuid + 1);
}

static void
bridge_rtmsg_sync(struct bridge_softc *sc)
{
	struct netmsg_base msg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, bridge_rtmsg_sync_handler);
	ifnet_domsg(&msg.lmsg, 0);
}

static __inline void
bridge_rtinfo_update(struct bridge_rtinfo *bri, struct ifnet *dst_if,
		     int setflags, uint8_t flags, uint32_t timeo)
{
	if ((bri->bri_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC &&
	    bri->bri_ifp != dst_if)
		bri->bri_ifp = dst_if;
	if ((flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC &&
	    bri->bri_expire != time_uptime + timeo)
		bri->bri_expire = time_uptime + timeo;
	if (setflags)
		bri->bri_flags = flags;
}

static int
bridge_rtinstall_oncpu(struct bridge_softc *sc, const uint8_t *dst,
		       struct ifnet *dst_if, int setflags, uint8_t flags,
		       struct bridge_rtinfo **bri0)
{
	struct bridge_rtnode *brt;
	struct bridge_rtinfo *bri;

	if (mycpuid == 0) {
		brt = bridge_rtnode_lookup(sc, dst);
		if (brt != NULL) {
			/*
			 * rtnode for 'dst' already exists.  We inform the
			 * caller about this by leaving bri0 as NULL.  The
			 * caller will terminate the intallation upon getting
			 * NULL bri0.  However, we still need to update the
			 * rtinfo.
			 */
			KKASSERT(*bri0 == NULL);

			/* Update rtinfo */
			bridge_rtinfo_update(brt->brt_info, dst_if, setflags,
					     flags, sc->sc_brttimeout);
			return 0;
		}

		/*
		 * We only need to check brtcnt on CPU0, since if limit
		 * is to be exceeded, ENOSPC is returned.  Caller knows
		 * this and will terminate the installation.
		 */
		if (sc->sc_brtcnt >= sc->sc_brtmax)
			return ENOSPC;

		KKASSERT(*bri0 == NULL);
		bri = kmalloc(sizeof(struct bridge_rtinfo), M_DEVBUF,
				  M_WAITOK | M_ZERO);
		*bri0 = bri;

		/* Setup rtinfo */
		bri->bri_flags = IFBAF_DYNAMIC;
		bridge_rtinfo_update(bri, dst_if, setflags, flags,
				     sc->sc_brttimeout);
	} else {
		bri = *bri0;
		KKASSERT(bri != NULL);
	}

	brt = kmalloc(sizeof(struct bridge_rtnode), M_DEVBUF,
		      M_WAITOK | M_ZERO);
	memcpy(brt->brt_addr, dst, ETHER_ADDR_LEN);
	brt->brt_info = bri;

	bridge_rtnode_insert(sc, brt);
	return 0;
}

static void
bridge_rtinstall_handler(netmsg_t msg)
{
	struct netmsg_brsaddr *brmsg = (struct netmsg_brsaddr *)msg;
	int error;

	error = bridge_rtinstall_oncpu(brmsg->br_softc,
				       brmsg->br_dst, brmsg->br_dst_if,
				       brmsg->br_setflags, brmsg->br_flags,
				       &brmsg->br_rtinfo);
	if (error) {
		KKASSERT(mycpuid == 0 && brmsg->br_rtinfo == NULL);
		lwkt_replymsg(&brmsg->base.lmsg, error);
		return;
	} else if (brmsg->br_rtinfo == NULL) {
		/* rtnode already exists for 'dst' */
		KKASSERT(mycpuid == 0);
		lwkt_replymsg(&brmsg->base.lmsg, 0);
		return;
	}
	ifnet_forwardmsg(&brmsg->base.lmsg, mycpuid + 1);
}

/*
 * bridge_rtupdate:
 *
 *	Add/Update a bridge routing entry.
 */
static int
bridge_rtupdate(struct bridge_softc *sc, const uint8_t *dst,
		struct ifnet *dst_if, uint8_t flags)
{
	struct bridge_rtnode *brt;

	/*
	 * A route for this destination might already exist.  If so,
	 * update it, otherwise create a new one.
	 */
	if ((brt = bridge_rtnode_lookup(sc, dst)) == NULL) {
		struct netmsg_brsaddr *brmsg;

		if (sc->sc_brtcnt >= sc->sc_brtmax)
			return ENOSPC;

		brmsg = kmalloc(sizeof(*brmsg), M_LWKTMSG, M_WAITOK | M_NULLOK);
		if (brmsg == NULL)
			return ENOMEM;

		netmsg_init(&brmsg->base, NULL, &netisr_afree_rport,
			    0, bridge_rtinstall_handler);
		memcpy(brmsg->br_dst, dst, ETHER_ADDR_LEN);
		brmsg->br_dst_if = dst_if;
		brmsg->br_flags = flags;
		brmsg->br_setflags = 0;
		brmsg->br_softc = sc;
		brmsg->br_rtinfo = NULL;

		ifnet_sendmsg(&brmsg->base.lmsg, 0);
		return 0;
	}
	bridge_rtinfo_update(brt->brt_info, dst_if, 0, flags,
			     sc->sc_brttimeout);
	return 0;
}

static int
bridge_rtsaddr(struct bridge_softc *sc, const uint8_t *dst,
	       struct ifnet *dst_if, uint8_t flags)
{
	struct netmsg_brsaddr brmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	netmsg_init(&brmsg.base, NULL, &curthread->td_msgport,
		    0, bridge_rtinstall_handler);
	memcpy(brmsg.br_dst, dst, ETHER_ADDR_LEN);
	brmsg.br_dst_if = dst_if;
	brmsg.br_flags = flags;
	brmsg.br_setflags = 1;
	brmsg.br_softc = sc;
	brmsg.br_rtinfo = NULL;

	return ifnet_domsg(&brmsg.base.lmsg, 0);
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
		return NULL;
	return brt->brt_info->bri_ifp;
}

static void
bridge_rtreap_handler(netmsg_t msg)
{
	struct bridge_softc *sc = msg->lmsg.u.ms_resultp;
	struct bridge_rtnode *brt, *nbrt;

	LIST_FOREACH_MUTABLE(brt, &sc->sc_rtlists[mycpuid], brt_list, nbrt) {
		if (brt->brt_info->bri_dead)
			bridge_rtnode_destroy(sc, brt);
	}
	ifnet_forwardmsg(&msg->lmsg, mycpuid + 1);
}

static void
bridge_rtreap(struct bridge_softc *sc)
{
	struct netmsg_base msg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, bridge_rtreap_handler);
	msg.lmsg.u.ms_resultp = sc;

	ifnet_domsg(&msg.lmsg, 0);
}

static void
bridge_rtreap_async(struct bridge_softc *sc)
{
	struct netmsg_base *msg;

	msg = kmalloc(sizeof(*msg), M_LWKTMSG, M_WAITOK);

	netmsg_init(msg, NULL, &netisr_afree_rport,
		    0, bridge_rtreap_handler);
	msg->lmsg.u.ms_resultp = sc;

	ifnet_sendmsg(&msg->lmsg, 0);
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
	struct bridge_rtnode *brt;
	int dead;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	/* Make sure we actually need to do this. */
	if (sc->sc_brtcnt <= sc->sc_brtmax)
		return;

	/*
	 * Find out how many rtnodes are dead
	 */
	dead = bridge_rtage_finddead(sc);
	KKASSERT(dead <= sc->sc_brtcnt);

	if (sc->sc_brtcnt - dead <= sc->sc_brtmax) {
		/* Enough dead rtnodes are found */
		bridge_rtreap(sc);
		return;
	}

	/*
	 * Kill some dynamic rtnodes to meet the brtmax
	 */
	LIST_FOREACH(brt, &sc->sc_rtlists[mycpuid], brt_list) {
		struct bridge_rtinfo *bri = brt->brt_info;

		if (bri->bri_dead) {
			/*
			 * We have counted this rtnode in
			 * bridge_rtage_finddead()
			 */
			continue;
		}

		if ((bri->bri_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
			bri->bri_dead = 1;
			++dead;
			KKASSERT(dead <= sc->sc_brtcnt);

			if (sc->sc_brtcnt - dead <= sc->sc_brtmax) {
				/* Enough rtnodes are collected */
				break;
			}
		}
	}
	if (dead)
		bridge_rtreap(sc);
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
	struct netmsg_base *msg;

	KKASSERT(mycpuid == BRIDGE_CFGCPU);

	crit_enter();

	if (callout_pending(&sc->sc_brcallout) ||
	    !callout_active(&sc->sc_brcallout)) {
		crit_exit();
		return;
	}
	callout_deactivate(&sc->sc_brcallout);

	msg = &sc->sc_brtimemsg;
	KKASSERT(msg->lmsg.ms_flags & MSGF_DONE);
	lwkt_sendmsg_oncpu(BRIDGE_CFGPORT, &msg->lmsg);

	crit_exit();
}

static void
bridge_timer_handler(netmsg_t msg)
{
	struct bridge_softc *sc = msg->lmsg.u.ms_resultp;

	KKASSERT(&curthread->td_msgport == BRIDGE_CFGPORT);

	crit_enter();
	/* Reply ASAP */
	lwkt_replymsg(&msg->lmsg, 0);
	crit_exit();

	bridge_rtage(sc);
	if (sc->sc_ifp->if_flags & IFF_RUNNING) {
		callout_reset(&sc->sc_brcallout,
		    bridge_rtable_prune_period * hz, bridge_timer, sc);
	}
}

static int
bridge_rtage_finddead(struct bridge_softc *sc)
{
	struct bridge_rtnode *brt;
	int dead = 0;

	LIST_FOREACH(brt, &sc->sc_rtlists[mycpuid], brt_list) {
		struct bridge_rtinfo *bri = brt->brt_info;

		if ((bri->bri_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC &&
		    time_uptime >= bri->bri_expire) {
			bri->bri_dead = 1;
			++dead;
			KKASSERT(dead <= sc->sc_brtcnt);
		}
	}
	return dead;
}

/*
 * bridge_rtage:
 *
 *	Perform an aging cycle.
 */
static void
bridge_rtage(struct bridge_softc *sc)
{
	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	if (bridge_rtage_finddead(sc))
		bridge_rtreap(sc);
}

/*
 * bridge_rtflush:
 *
 *	Remove all dynamic addresses from the bridge.
 */
static void
bridge_rtflush(struct bridge_softc *sc, int bf)
{
	struct bridge_rtnode *brt;
	int reap;

	reap = 0;
	LIST_FOREACH(brt, &sc->sc_rtlists[mycpuid], brt_list) {
		struct bridge_rtinfo *bri = brt->brt_info;

		if ((bf & IFBF_FLUSHALL) ||
		    (bri->bri_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
			bri->bri_dead = 1;
			reap = 1;
		}
	}
	if (reap) {
		if (bf & IFBF_FLUSHSYNC)
			bridge_rtreap(sc);
		else
			bridge_rtreap_async(sc);
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

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	if ((brt = bridge_rtnode_lookup(sc, addr)) == NULL)
		return (ENOENT);

	/* TODO: add a cheaper delete operation */
	brt->brt_info->bri_dead = 1;
	bridge_rtreap(sc);
	return (0);
}

/*
 * bridge_rtdelete:
 *
 *	Delete routes to a speicifc member interface.
 */
void
bridge_rtdelete(struct bridge_softc *sc, struct ifnet *ifp, int bf)
{
	struct bridge_rtnode *brt;
	int reap;

	reap = 0;
	LIST_FOREACH(brt, &sc->sc_rtlists[mycpuid], brt_list) {
		struct bridge_rtinfo *bri = brt->brt_info;

		if (bri->bri_ifp == ifp &&
		    ((bf & IFBF_FLUSHALL) ||
		     (bri->bri_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC)) {
			bri->bri_dead = 1;
			reap = 1;
		}
	}
	if (reap) {
		if (bf & IFBF_FLUSHSYNC)
			bridge_rtreap(sc);
		else
			bridge_rtreap_async(sc);
	}
}

/*
 * bridge_rtable_init:
 *
 *	Initialize the route table for this bridge.
 */
static void
bridge_rtable_init(struct bridge_softc *sc)
{
	int cpu;

	/*
	 * Initialize per-cpu hash tables
	 */
	sc->sc_rthashs = kmalloc(sizeof(*sc->sc_rthashs) * ncpus,
				 M_DEVBUF, M_WAITOK);
	for (cpu = 0; cpu < ncpus; ++cpu) {
		int i;

		sc->sc_rthashs[cpu] =
		kmalloc(sizeof(struct bridge_rtnode_head) * BRIDGE_RTHASH_SIZE,
			M_DEVBUF, M_WAITOK);

		for (i = 0; i < BRIDGE_RTHASH_SIZE; i++)
			LIST_INIT(&sc->sc_rthashs[cpu][i]);
	}
	sc->sc_rthash_key = karc4random();

	/*
	 * Initialize per-cpu lists
	 */
	sc->sc_rtlists = kmalloc(sizeof(struct bridge_rtnode_head) * ncpus,
				 M_DEVBUF, M_WAITOK);
	for (cpu = 0; cpu < ncpus; ++cpu)
		LIST_INIT(&sc->sc_rtlists[cpu]);
}

/*
 * bridge_rtable_fini:
 *
 *	Deconstruct the route table for this bridge.
 */
static void
bridge_rtable_fini(struct bridge_softc *sc)
{
	int cpu;

	/*
	 * Free per-cpu hash tables
	 */
	for (cpu = 0; cpu < ncpus; ++cpu)
		kfree(sc->sc_rthashs[cpu], M_DEVBUF);
	kfree(sc->sc_rthashs, M_DEVBUF);

	/*
	 * Free per-cpu lists
	 */
	kfree(sc->sc_rtlists, M_DEVBUF);
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
	LIST_FOREACH(brt, &sc->sc_rthashs[mycpuid][hash], brt_hash) {
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
 *	Insert the specified bridge node into the route table.
 *	Caller has to make sure that rtnode does not exist.
 */
static void
bridge_rtnode_insert(struct bridge_softc *sc, struct bridge_rtnode *brt)
{
	struct bridge_rtnode *lbrt;
	uint32_t hash;
	int dir;

	hash = bridge_rthash(sc, brt->brt_addr);

	lbrt = LIST_FIRST(&sc->sc_rthashs[mycpuid][hash]);
	if (lbrt == NULL) {
		LIST_INSERT_HEAD(&sc->sc_rthashs[mycpuid][hash],
				  brt, brt_hash);
		goto out;
	}

	do {
		dir = bridge_rtnode_addr_cmp(brt->brt_addr, lbrt->brt_addr);
		KASSERT(dir != 0, ("rtnode already exist"));

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

	panic("no suitable position found for rtnode");
out:
	LIST_INSERT_HEAD(&sc->sc_rtlists[mycpuid], brt, brt_list);
	if (mycpuid == 0) {
		/*
		 * Update the brtcnt.
		 * We only need to do it once and we do it on CPU0.
		 */
		sc->sc_brtcnt++;
	}
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

	if (mycpuid + 1 == ncpus) {
		/* Free rtinfo associated with rtnode on the last cpu */
		kfree(brt->brt_info, M_DEVBUF);
	}
	kfree(brt, M_DEVBUF);

	if (mycpuid == 0) {
		/* Update brtcnt only on CPU0 */
		sc->sc_brtcnt--;
	}
}

static __inline int
bridge_post_pfil(struct mbuf *m)
{
	if (m->m_pkthdr.fw_flags & IPFORWARD_MBUF_TAGGED)
		return EOPNOTSUPP;

	/* Not yet */
	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED)
		return EOPNOTSUPP;

	return 0;
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
		 * packets, these will not be checked by pfil(9)
		 * and passed unconditionally so the default is to drop.
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
	switch (ether_type) {
	case ETHERTYPE_IP:
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
		if (pfil_bridge && dir == PFIL_OUT && bifp != NULL) {
			error = pfil_run_hooks(&inet_pfil_hook, mp, bifp, dir);
			if (*mp == NULL || error != 0) /* filter may consume */
				break;
			error = bridge_post_pfil(*mp);
			if (error)
				break;
		}

		if (pfil_member && ifp != NULL) {
			error = pfil_run_hooks(&inet_pfil_hook, mp, ifp, dir);
			if (*mp == NULL || error != 0) /* filter may consume */
				break;
			error = bridge_post_pfil(*mp);
			if (error)
				break;
		}

		if (pfil_bridge && dir == PFIL_IN && bifp != NULL) {
			error = pfil_run_hooks(&inet_pfil_hook, mp, bifp, dir);
			if (*mp == NULL || error != 0) /* filter may consume */
				break;
			error = bridge_post_pfil(*mp);
			if (error)
				break;
		}

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
			if ((*mp = m_pullup(*mp, hlen)) == NULL)
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
	case ETHERTYPE_IPV6:
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
	default:
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
#if 0 /* notyet */
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
		if ((m = m_pullup(m, hlen)) == NULL) {
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
#if 0 /* notyet */
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
bridge_enqueue_handler(netmsg_t msg)
{
	struct netmsg_packet *nmp;
	struct ifnet *dst_ifp;
	struct mbuf *m;

	nmp = &msg->packet;
	m = nmp->nm_packet;
	dst_ifp = nmp->base.lmsg.u.ms_resultp;
	mbuftrackid(m, 71);

	bridge_handoff(dst_ifp->if_bridge, dst_ifp, m, 1);
}

static void
bridge_handoff(struct bridge_softc *sc, struct ifnet *dst_ifp,
	       struct mbuf *m, int from_us)
{
	struct mbuf *m0;
	struct ifnet *bifp;

	bifp = sc->sc_ifp;
	mbuftrackid(m, 72);

	/* We may be sending a fragment so traverse the mbuf */
	for (; m; m = m0) {
		struct altq_pktattr pktattr;

		m0 = m->m_nextpkt;
		m->m_nextpkt = NULL;

		/*
		 * If being sent from our host override ether_shost
		 * with the bridge MAC.  This is mandatory for ARP
		 * so things don't get confused.  In particular we
		 * don't want ARPs to get associated with link interfaces
		 * under the bridge which might or might not stay valid.
		 *
		 * Also override ether_shost when relaying a packet out
		 * the same interface it came in on, due to multi-homed
		 * addresses & default routes, otherwise switches will
		 * get very confused.
		 *
		 * Otherwise if we are in transparent mode.
		 */
		if (from_us || m->m_pkthdr.rcvif == dst_ifp) {
			m_copyback(m,
				   offsetof(struct ether_header, ether_shost),
				   ETHER_ADDR_LEN, IF_LLADDR(sc->sc_ifp));
		} else if ((bifp->if_flags & IFF_LINK0) &&
			   (m->m_pkthdr.fw_flags & BRIDGE_MBUF_TAGGED)) {
			m_copyback(m,
				   offsetof(struct ether_header, ether_shost),
				   ETHER_ADDR_LEN,
				   m->m_pkthdr.ether_br_shost);
		} /* else retain shost */

		if (ifq_is_enabled(&dst_ifp->if_snd))
			altq_etherclassify(&dst_ifp->if_snd, m, &pktattr);

		ifq_dispatch(dst_ifp, m, &pktattr);
	}
}

static void
bridge_control_dispatch(netmsg_t msg)
{
	struct netmsg_brctl *bc_msg = (struct netmsg_brctl *)msg;
	struct ifnet *bifp = bc_msg->bc_sc->sc_ifp;
	int error;

	ifnet_serialize_all(bifp);
	error = bc_msg->bc_func(bc_msg->bc_sc, bc_msg->bc_arg);
	ifnet_deserialize_all(bifp);

	lwkt_replymsg(&bc_msg->base.lmsg, error);
}

static int
bridge_control(struct bridge_softc *sc, u_long cmd,
	       bridge_ctl_t bc_func, void *bc_arg)
{
	struct ifnet *bifp = sc->sc_ifp;
	struct netmsg_brctl bc_msg;
	int error;

	ASSERT_IFNET_SERIALIZED_ALL(bifp);

	bzero(&bc_msg, sizeof(bc_msg));

	netmsg_init(&bc_msg.base, NULL, &curthread->td_msgport,
		    0, bridge_control_dispatch);
	bc_msg.bc_func = bc_func;
	bc_msg.bc_sc = sc;
	bc_msg.bc_arg = bc_arg;

	ifnet_deserialize_all(bifp);
	error = lwkt_domsg(BRIDGE_CFGPORT, &bc_msg.base.lmsg, 0);
	ifnet_serialize_all(bifp);
	return error;
}

static void
bridge_add_bif_handler(netmsg_t msg)
{
	struct netmsg_braddbif *amsg = (struct netmsg_braddbif *)msg;
	struct bridge_softc *sc;
	struct bridge_iflist *bif;

	sc = amsg->br_softc;

	bif = kmalloc(sizeof(*bif), M_DEVBUF, M_WAITOK | M_ZERO);
	bif->bif_ifp = amsg->br_bif_ifp;
	bif->bif_onlist = 1;
	bif->bif_info = amsg->br_bif_info;

	/*
	 * runs through bif_info
	 */
	bif->bif_flags = IFBIF_LEARNING | IFBIF_DISCOVER;

	TAILQ_INSERT_HEAD(&sc->sc_iflists[mycpuid], bif, bif_next);

	ifnet_forwardmsg(&amsg->base.lmsg, mycpuid + 1);
}

static void
bridge_add_bif(struct bridge_softc *sc, struct bridge_ifinfo *bif_info,
	       struct ifnet *ifp)
{
	struct netmsg_braddbif amsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	netmsg_init(&amsg.base, NULL, &curthread->td_msgport,
		    0, bridge_add_bif_handler);
	amsg.br_softc = sc;
	amsg.br_bif_info = bif_info;
	amsg.br_bif_ifp = ifp;

	ifnet_domsg(&amsg.base.lmsg, 0);
}

static void
bridge_del_bif_handler(netmsg_t msg)
{
	struct netmsg_brdelbif *dmsg = (struct netmsg_brdelbif *)msg;
	struct bridge_softc *sc;
	struct bridge_iflist *bif;

	sc = dmsg->br_softc;

	/*
	 * Locate the bif associated with the br_bif_info
	 * on the current CPU
	 */
	bif = bridge_lookup_member_ifinfo(sc, dmsg->br_bif_info);
	KKASSERT(bif != NULL && bif->bif_onlist);

	/* Remove the bif from the current CPU's iflist */
	bif->bif_onlist = 0;
	TAILQ_REMOVE(dmsg->br_bif_list, bif, bif_next);

	/* Save the removed bif for later freeing */
	TAILQ_INSERT_HEAD(dmsg->br_bif_list, bif, bif_next);

	ifnet_forwardmsg(&dmsg->base.lmsg, mycpuid + 1);
}

static void
bridge_del_bif(struct bridge_softc *sc, struct bridge_ifinfo *bif_info,
	       struct bridge_iflist_head *saved_bifs)
{
	struct netmsg_brdelbif dmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(sc->sc_ifp);

	netmsg_init(&dmsg.base, NULL, &curthread->td_msgport,
		    0, bridge_del_bif_handler);
	dmsg.br_softc = sc;
	dmsg.br_bif_info = bif_info;
	dmsg.br_bif_list = saved_bifs;

	ifnet_domsg(&dmsg.base.lmsg, 0);
}
