/*
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net/if_vlan.c,v 1.15.2.13 2003/02/14 22:25:58 fenner Exp $
 */

/*
 * if_vlan.c - pseudo-device driver for IEEE 802.1Q virtual LANs.
 * Might be extended some day to also handle IEEE 802.1p priority
 * tagging.  This is sort of sneaky in the implementation, since
 * we need to pretend to be enough of an Ethernet implementation
 * to make arp work.  The way we do this is by telling everyone
 * that we are an Ethernet, and then catch the packets that
 * ether_output() left on our output queue queue when it calls
 * if_start(), rewrite them for use by the real outgoing interface,
 * and ask it to send them.
 *
 *
 * Note about vlan's MP safe approach:
 *
 * - All configuration operation, e.g. config, unconfig and change flags,
 *   is serialized by netisr0; not by vlan's serializer
 *
 * - Parent interface's trunk and vlans are linked in the following
 *   fashion:
 *                     CPU0     CPU1     CPU2     CPU3
 *   +--------------+--------+--------+--------+--------+
 *   | parent ifnet |trunk[0]|trunk[1]|trunk[2]|trunk[3]|
 *   +--------------+--------+--------+--------+--------+
 *                       |        |        |        |
 *                       V        V        V        V
 *   +--------------+--------+--------+--------+--------+
 *   |   vlan ifnet |entry[0]|entry[1]|entry[2]|entry[3]|
 *   +--------------+--------+--------+--------+--------+
 *                       |        |        |        |
 *                       V        V        V        V
 *   +--------------+--------+--------+--------+--------+
 *   |   vlan ifnet |entry[0]|entry[1]|entry[2]|entry[3]|
 *   +--------------+--------+--------+--------+--------+
 *
 * - Vlan is linked/unlinked onto parent interface's trunk using following
 *   way:
 *
 *       CPU0             CPU1             CPU2             CPU3
 *
 *      netisr0 <---------------------------------------------+
 *  (config/unconfig)                                         |
 *         |                                                  |
 *         | domsg                                            | replymsg
 *         |                                                  |
 *         V     fwdmsg           fwdmsg           fwdmsg     |
 *       ifnet0 --------> ifnet1 --------> ifnet2 --------> ifnet3
 *    (link/unlink)    (link/unlink)    (link/unlink)    (link/unlink)
 *
 * - Parent interface's trunk is destroyed in the following lockless way:
 *
 *     old_trunk = ifp->if_vlantrunks;
 *     ifp->if_vlantrunks = NULL;
 *     netmsg_service_sync();
 *     (*)
 *     free(old_trunk);
 *
 *   Since all of the accessing of if_vlantrunks only happens in network
 *   threads (percpu netisr and ifnet threads), after netmsg_service_sync()
 *   the network threads are promised to see only NULL if_vlantrunks; we
 *   are safe to free the "to be destroyed" parent interface's trunk
 *   afterwards.
 */

#ifndef NVLAN
#include "use_vlan.h"
#endif
#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/thread2.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/if_clone.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

struct ifvlan;

struct vlan_mc_entry {
	struct ether_addr		mc_addr;
	SLIST_ENTRY(vlan_mc_entry)	mc_entries;
};

struct vlan_entry {
	struct ifvlan		*ifv;
	LIST_ENTRY(vlan_entry)	ifv_link;
};

struct	ifvlan {
	struct	arpcom ifv_ac;	/* make this an interface */
	struct	ifnet *ifv_p;	/* parent inteface of this vlan */
	int ifv_pflags;		/* special flags we have set on parent */
	struct	ifv_linkmib {
		int	ifvm_parent;
		uint16_t ifvm_proto; /* encapsulation ethertype */
		uint16_t ifvm_tag; /* tag to apply on packets leaving if */
	}	ifv_mib;
	SLIST_HEAD(, vlan_mc_entry) vlan_mc_listhead;
	LIST_ENTRY(ifvlan) ifv_list;
	struct vlan_entry ifv_entries[1];
};
#define	ifv_if	ifv_ac.ac_if
#define	ifv_tag	ifv_mib.ifvm_tag

struct vlan_trunk {
	LIST_HEAD(, vlan_entry) vlan_list;
};

struct netmsg_vlan {
	struct netmsg_base base;
	struct ifvlan	*nv_ifv;
	struct ifnet	*nv_ifp_p;
	const char	*nv_parent_name;
	uint16_t	nv_vlantag;
};

#define VLANNAME	"vlan"

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_L2VLAN, vlan, CTLFLAG_RW, 0, "IEEE 802.1Q VLAN");
SYSCTL_NODE(_net_link_vlan, PF_LINK, link, CTLFLAG_RW, 0, "for consistency");

static MALLOC_DEFINE(M_VLAN, "vlan", "802.1Q Virtual LAN Interface");
static LIST_HEAD(, ifvlan) ifv_list;

static int	vlan_clone_create(struct if_clone *, int, caddr_t);
static int	vlan_clone_destroy(struct ifnet *);
static void	vlan_ifdetach(void *, struct ifnet *);

static void	vlan_init(void *);
static void	vlan_start(struct ifnet *, struct ifaltq_subque *);
static int	vlan_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	vlan_input(struct mbuf *);

static int	vlan_setflags(struct ifvlan *, struct ifnet *, int);
static int	vlan_setflag(struct ifvlan *, struct ifnet *, int, int,
			     int (*)(struct ifnet *, int));
static int	vlan_config_flags(struct ifvlan *ifv);
static void	vlan_clrmulti(struct ifvlan *, struct ifnet *);
static int	vlan_setmulti(struct ifvlan *, struct ifnet *);
static int	vlan_config_multi(struct ifvlan *);
static int	vlan_config(struct ifvlan *, const char *, uint16_t);
static int	vlan_unconfig(struct ifvlan *);
static void	vlan_link(struct ifvlan *, struct ifnet *);
static void	vlan_unlink(struct ifvlan *, struct ifnet *);

static void	vlan_config_dispatch(netmsg_t);
static void	vlan_unconfig_dispatch(netmsg_t);
static void	vlan_link_dispatch(netmsg_t);
static void	vlan_unlink_dispatch(netmsg_t);
static void	vlan_multi_dispatch(netmsg_t);
static void	vlan_flags_dispatch(netmsg_t);
static void	vlan_ifdetach_dispatch(netmsg_t);

/* Special flags we should propagate to parent */
static struct {
	int flag;
	int (*func)(struct ifnet *, int);
} vlan_pflags[] = {
	{ IFF_PROMISC, ifpromisc },
	{ IFF_ALLMULTI, if_allmulti },
	{ 0, NULL }
};

static eventhandler_tag vlan_ifdetach_cookie;
static struct if_clone vlan_cloner =
	IF_CLONE_INITIALIZER("vlan", vlan_clone_create, vlan_clone_destroy,
			     NVLAN, IF_MAXUNIT);

/*
 * Handle IFF_* flags that require certain changes on the parent:
 * if "set" is true, update parent's flags respective to our if_flags;
 * if "set" is false, forcedly clear the flags set on parent.
 */
static int
vlan_setflags(struct ifvlan *ifv, struct ifnet *ifp_p, int set)
{
	int error, i;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	for (i = 0; vlan_pflags[i].func != NULL; i++) {
		error = vlan_setflag(ifv, ifp_p, vlan_pflags[i].flag,
				     set, vlan_pflags[i].func);
		if (error)
			return error;
	}
	return 0;
}

/* Handle a reference counted flag that should be set on the parent as well */
static int
vlan_setflag(struct ifvlan *ifv, struct ifnet *ifp_p, int flag, int set,
	     int (*func)(struct ifnet *, int))
{
	struct ifnet *ifp = &ifv->ifv_if;
	int error, ifv_flag;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	ifv_flag = set ? (ifp->if_flags & flag) : 0;

	/*
	 * See if recorded parent's status is different from what
	 * we want it to be.  If it is, flip it.  We record parent's
	 * status in ifv_pflags so that we won't clear parent's flag
	 * we haven't set.  In fact, we don't clear or set parent's
	 * flags directly, but get or release references to them.
	 * That's why we can be sure that recorded flags still are
	 * in accord with actual parent's flags.
	 */
	if (ifv_flag != (ifv->ifv_pflags & flag)) {
		error = func(ifp_p, ifv_flag);
		if (error)
			return error;
		ifv->ifv_pflags &= ~flag;
		ifv->ifv_pflags |= ifv_flag;
	}
	return 0;
}

/*
 * Program our multicast filter. What we're actually doing is
 * programming the multicast filter of the parent. This has the
 * side effect of causing the parent interface to receive multicast
 * traffic that it doesn't really want, which ends up being discarded
 * later by the upper protocol layers. Unfortunately, there's no way
 * to avoid this: there really is only one physical interface.
 */
static int
vlan_setmulti(struct ifvlan *ifv, struct ifnet *ifp_p)
{
	struct ifmultiaddr *ifma;
	struct vlan_mc_entry *mc = NULL;
	struct sockaddr_dl sdl;
	struct ifnet *ifp = &ifv->ifv_if;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	/*
	 * First, remove any existing filter entries.
	 */
	vlan_clrmulti(ifv, ifp_p);

	/*
	 * Save the filter entries to be added to parent.
	 *
	 * TODO: need ifnet_serialize_main
	 */
	ifnet_serialize_all(ifp);
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		/* Save a copy */
		mc = kmalloc(sizeof(struct vlan_mc_entry), M_VLAN, M_WAITOK);
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		      &mc->mc_addr, ETHER_ADDR_LEN);
		SLIST_INSERT_HEAD(&ifv->vlan_mc_listhead, mc, mc_entries);
	}
	ifnet_deserialize_all(ifp);

	/*
	 * Now program new ones.
	 */
	bzero(&sdl, sizeof(sdl));
	sdl.sdl_len = sizeof(sdl);
	sdl.sdl_family = AF_LINK;
	sdl.sdl_index = ifp_p->if_index;
	sdl.sdl_type = IFT_ETHER;
	sdl.sdl_alen = ETHER_ADDR_LEN;

	/*
	 * Program the parent multicast filter
	 */
	SLIST_FOREACH(mc, &ifv->vlan_mc_listhead, mc_entries) {
		int error;

		bcopy(&mc->mc_addr, LLADDR(&sdl), ETHER_ADDR_LEN);
		error = if_addmulti(ifp_p, (struct sockaddr *)&sdl, NULL);
		if (error) {
			/* XXX probably should keep going */
			return error;
		}
	}
	return 0;
}

static void
vlan_clrmulti(struct ifvlan *ifv, struct ifnet *ifp_p)
{
	struct vlan_mc_entry *mc;
	struct sockaddr_dl sdl;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	bzero(&sdl, sizeof(sdl));
	sdl.sdl_len = sizeof(sdl);
	sdl.sdl_family = AF_LINK;
	sdl.sdl_index = ifp_p->if_index;
	sdl.sdl_type = IFT_ETHER;
	sdl.sdl_alen = ETHER_ADDR_LEN;

	while ((mc = SLIST_FIRST(&ifv->vlan_mc_listhead)) != NULL) {
		bcopy(&mc->mc_addr, LLADDR(&sdl), ETHER_ADDR_LEN);
		if_delmulti(ifp_p, (struct sockaddr *)&sdl); /* ignore error */

		SLIST_REMOVE_HEAD(&ifv->vlan_mc_listhead, mc_entries);
		kfree(mc, M_VLAN);
	}
}

static int
vlan_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		LIST_INIT(&ifv_list);
		vlan_input_p = vlan_input;
		vlan_ifdetach_cookie =
		EVENTHANDLER_REGISTER(ifnet_detach_event,
				      vlan_ifdetach, NULL,
				      EVENTHANDLER_PRI_ANY);
		if_clone_attach(&vlan_cloner);
		break;

	case MOD_UNLOAD:
		if_clone_detach(&vlan_cloner);

		vlan_input_p = NULL;
		/*
		 * Make sure that all protocol threads see vlan_input_p change.
		 */
		netmsg_service_sync();

		EVENTHANDLER_DEREGISTER(ifnet_detach_event,
					vlan_ifdetach_cookie);
		while (!LIST_EMPTY(&ifv_list))
			vlan_clone_destroy(&LIST_FIRST(&ifv_list)->ifv_if);
		break;
	}
	return 0;
}

static moduledata_t vlan_mod = {
	"if_vlan",
	vlan_modevent,
	0
};

DECLARE_MODULE(if_vlan, vlan_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);

static void
vlan_ifdetach_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct ifnet *ifp_p = vmsg->nv_ifp_p;
	struct vlan_trunk *vlantrunks, *trunk;
	struct vlan_entry *ifve;

	vlantrunks = ifp_p->if_vlantrunks;
	if (vlantrunks == NULL)
		goto reply;
	trunk = &vlantrunks[mycpuid];

	while (ifp_p->if_vlantrunks &&
	       (ifve = LIST_FIRST(&trunk->vlan_list)) != NULL)
		vlan_unconfig(ifve->ifv);
reply:
	lwkt_replymsg(&vmsg->base.lmsg, 0);
}

static void
vlan_ifdetach(void *arg __unused, struct ifnet *ifp)
{
	struct netmsg_vlan vmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_ifdetach_dispatch);
	vmsg.nv_ifp_p = ifp;

	lwkt_domsg(netisr_cpuport(0), &vmsg.base.lmsg, 0);
}

static int
vlan_clone_create(struct if_clone *ifc, int unit, caddr_t param __unused)
{
	struct ifvlan *ifv;
	struct ifnet *ifp;
	int vlan_size, i;

	vlan_size = sizeof(struct ifvlan)
		  + ((ncpus - 1) * sizeof(struct vlan_entry));
	ifv = kmalloc(vlan_size, M_VLAN, M_WAITOK | M_ZERO);
	SLIST_INIT(&ifv->vlan_mc_listhead);
	for (i = 0; i < ncpus; ++i)
		ifv->ifv_entries[i].ifv = ifv;

	crit_enter();	/* XXX not MP safe */
	LIST_INSERT_HEAD(&ifv_list, ifv, ifv_list);
	crit_exit();

	ifp = &ifv->ifv_if;
	ifp->if_softc = ifv;
	if_initname(ifp, "vlan", unit);
	/* NB: flags are not set here */
	ifp->if_linkmib = &ifv->ifv_mib;
	ifp->if_linkmiblen = sizeof ifv->ifv_mib;
	/* NB: mtu is not set here */

	ifp->if_init = vlan_init;
	ifp->if_start = vlan_start;
	ifp->if_ioctl = vlan_ioctl;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);
	ether_ifattach(ifp, ifv->ifv_ac.ac_enaddr, NULL);
	/* Now undo some of the damage... */
	ifp->if_data.ifi_type = IFT_L2VLAN;
	ifp->if_data.ifi_hdrlen = EVL_ENCAPLEN;

	return (0);
}

static int
vlan_clone_destroy(struct ifnet *ifp)
{
	struct ifvlan *ifv = ifp->if_softc;

	crit_enter();	/* XXX not MP safe */
	LIST_REMOVE(ifv, ifv_list);
	crit_exit();

	vlan_unconfig(ifv);
	ether_ifdetach(ifp);

	kfree(ifv, M_VLAN);

	return 0;
}

static void
vlan_init(void *xsc)
{
	struct ifvlan *ifv = xsc;
	struct ifnet *ifp = &ifv->ifv_if;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (ifv->ifv_p != NULL)
		ifp->if_flags |= IFF_RUNNING;
}

static void
vlan_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct ifvlan *ifv = ifp->if_softc;
	struct ifnet *ifp_p = ifv->ifv_p;
	struct mbuf *m;
	lwkt_port_t p_port;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_ALTQ_SQ_SERIALIZED_HW(ifsq);

	if (ifp_p == NULL) {
		ifsq_purge(ifsq);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	p_port = netisr_cpuport(
	    ifsq_get_cpuid(ifq_get_subq_default(&ifp_p->if_snd)));
	for (;;) {
		struct netmsg_packet *nmp;

		m = ifsq_dequeue(ifsq);
		if (m == NULL)
			break;
		BPF_MTAP(ifp, m);

		/*
		 * Do not run parent's if_start() if the parent is not up,
		 * or parent's driver will cause a system crash.
		 */
		if ((ifp_p->if_flags & (IFF_UP | IFF_RUNNING)) !=
		    (IFF_UP | IFF_RUNNING)) {
			m_freem(m);
			IFNET_STAT_INC(ifp, collisions, 1);
			continue;
		}

		/*
		 * We need some way to tell the interface where the packet
		 * came from so that it knows how to find the VLAN tag to
		 * use, so we set the ether_vlantag in the mbuf packet header
		 * to our vlan tag.  We also set the M_VLANTAG flag in the
		 * mbuf to let the parent driver know that the ether_vlantag
		 * is really valid.
		 */
		m->m_pkthdr.ether_vlantag = ifv->ifv_tag;
		m->m_flags |= M_VLANTAG;

		nmp = &m->m_hdr.mh_netmsg;

		netmsg_init(&nmp->base, NULL, &netisr_apanic_rport,
			    0, vlan_start_dispatch);
		nmp->nm_packet = m;
		nmp->base.lmsg.u.ms_resultp = ifp_p;

		lwkt_sendmsg(p_port, &nmp->base.lmsg);
		IFNET_STAT_INC(ifp, opackets, 1);
	}
}

static void
vlan_input(struct mbuf *m)
{
	struct ifvlan *ifv = NULL;
	struct ifnet *rcvif;
	struct vlan_trunk *vlantrunks;
	struct vlan_entry *entry;

	rcvif = m->m_pkthdr.rcvif;
	KKASSERT(m->m_flags & M_VLANTAG);

	vlantrunks = rcvif->if_vlantrunks;
	if (vlantrunks == NULL) {
		IFNET_STAT_INC(rcvif, noproto, 1);
		m_freem(m);
		return;
	}

	crit_enter();	/* XXX Necessary? */
	LIST_FOREACH(entry, &vlantrunks[mycpuid].vlan_list, ifv_link) {
		if (entry->ifv->ifv_tag ==
		    EVL_VLANOFTAG(m->m_pkthdr.ether_vlantag)) {
			ifv = entry->ifv;
			break;
		}
	}
	crit_exit();

	/*
	 * Packet is discarded if:
	 * - no corresponding vlan(4) interface
	 * - vlan(4) interface has not been completely set up yet,
	 *   or is being destroyed (ifv->ifv_p != rcvif)
	 */
	if (ifv == NULL || ifv->ifv_p != rcvif) {
		IFNET_STAT_INC(rcvif, noproto, 1);
		m_freem(m);
		return;
	}

	/*
	 * Clear M_VLANTAG, before the packet is handed to
	 * vlan(4) interface
	 */
	m->m_flags &= ~M_VLANTAG;

	ether_reinput_oncpu(&ifv->ifv_if, m, REINPUT_RUNBPF);
}

static void
vlan_link_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct ifvlan *ifv = vmsg->nv_ifv;
	struct ifnet *ifp_p = vmsg->nv_ifp_p;
	struct vlan_entry *entry;
	struct vlan_trunk *vlantrunks, *trunk;
	int cpu = mycpuid;

	vlantrunks = ifp_p->if_vlantrunks;
	KASSERT(vlantrunks != NULL,
		("vlan trunk has not been initialized yet"));

	entry = &ifv->ifv_entries[cpu];
	trunk = &vlantrunks[cpu];

	crit_enter();
	LIST_INSERT_HEAD(&trunk->vlan_list, entry, ifv_link);
	crit_exit();

	ifnet_forwardmsg(&vmsg->base.lmsg, cpu + 1);
}

static void
vlan_link(struct ifvlan *ifv, struct ifnet *ifp_p)
{
	struct netmsg_vlan vmsg;

	/* Assert in netisr0 */
	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	if (ifp_p->if_vlantrunks == NULL) {
		struct vlan_trunk *vlantrunks;
		int i;

		vlantrunks = kmalloc(sizeof(*vlantrunks) * ncpus, M_VLAN,
				     M_WAITOK | M_ZERO);
		for (i = 0; i < ncpus; ++i)
			LIST_INIT(&vlantrunks[i].vlan_list);

		ifp_p->if_vlantrunks = vlantrunks;
	}

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_link_dispatch);
	vmsg.nv_ifv = ifv;
	vmsg.nv_ifp_p = ifp_p;

	ifnet_domsg(&vmsg.base.lmsg, 0);
}

static void
vlan_config_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct ifvlan *ifv;
	struct ifnet *ifp_p, *ifp;
	struct sockaddr_dl *sdl1, *sdl2;
	int error;

	/* Assert in netisr0 */

	ifp_p = ifunit(vmsg->nv_parent_name);
	if (ifp_p == NULL) {
		error = ENOENT;
		goto reply;
	}

	if (ifp_p->if_data.ifi_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto reply;
	}

	ifv = vmsg->nv_ifv;
	ifp = &ifv->ifv_if;

	if (ifv->ifv_p) {
		error = EBUSY;
		goto reply;
	}

	/* Link vlan into parent's vlantrunk */
	vlan_link(ifv, ifp_p);

	ifnet_serialize_all(ifp);

	ifv->ifv_tag = vmsg->nv_vlantag;
	if (ifp_p->if_capenable & IFCAP_VLAN_MTU)
		ifp->if_mtu = ifp_p->if_mtu;
	else
		ifp->if_mtu = ifp_p->if_data.ifi_mtu - EVL_ENCAPLEN;

	/*
	 * Copy only a selected subset of flags from the parent.
	 * Other flags are none of our business.
	 */
#define VLAN_INHERIT_FLAGS	(IFF_BROADCAST | IFF_MULTICAST | \
				 IFF_SIMPLEX | IFF_POINTOPOINT)

	ifp->if_flags &= ~VLAN_INHERIT_FLAGS;
	ifp->if_flags |= (ifp_p->if_flags & VLAN_INHERIT_FLAGS);

#undef VLAN_INHERIT_FLAGS

	/*
	 * Set up our ``Ethernet address'' to reflect the underlying
	 * physical interface's.
	 */
	sdl1 = IF_LLSOCKADDR(ifp);
	sdl2 = IF_LLSOCKADDR(ifp_p);
	sdl1->sdl_type = IFT_ETHER;
	sdl1->sdl_alen = ETHER_ADDR_LEN;
	bcopy(LLADDR(sdl2), LLADDR(sdl1), ETHER_ADDR_LEN);
	bcopy(LLADDR(sdl2), ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	/*
	 * Release vlan's serializer before reprogramming parent's
	 * multicast filter to avoid possible dead lock.
	 */
	ifnet_deserialize_all(ifp);

	/*
	 * Configure multicast addresses that may already be
	 * joined on the vlan device.
	 */
	vlan_setmulti(ifv, ifp_p);

	/*
	 * Set flags on the parent, if necessary.
	 */
	vlan_setflags(ifv, ifp_p, 1);

	/*
	 * Connect to parent after everything have been set up,
	 * so input/output could know that vlan is ready to go
	 */
	ifv->ifv_p = ifp_p;
	error = 0;
reply:
	lwkt_replymsg(&vmsg->base.lmsg, error);
}

static int
vlan_config(struct ifvlan *ifv, const char *parent_name, uint16_t vlantag)
{
	struct netmsg_vlan vmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_config_dispatch);
	vmsg.nv_ifv = ifv;
	vmsg.nv_parent_name = parent_name;
	vmsg.nv_vlantag = vlantag;

	return lwkt_domsg(netisr_cpuport(0), &vmsg.base.lmsg, 0);
}

static void
vlan_unlink_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct ifvlan *ifv = vmsg->nv_ifv;
	struct vlan_entry *entry;
	int cpu = mycpuid;

	KASSERT(vmsg->nv_ifp_p->if_vlantrunks != NULL,
		("vlan trunk has not been initialized yet"));
	entry = &ifv->ifv_entries[cpu];

	crit_enter();
	LIST_REMOVE(entry, ifv_link);
	crit_exit();

	ifnet_forwardmsg(&vmsg->base.lmsg, cpu + 1);
}

static void
vlan_unlink(struct ifvlan *ifv, struct ifnet *ifp_p)
{
	struct vlan_trunk *vlantrunks = ifp_p->if_vlantrunks;
	struct netmsg_vlan vmsg;

	/* Assert in netisr0 */
	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	KASSERT(ifp_p->if_vlantrunks != NULL,
		("vlan trunk has not been initialized yet"));

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_unlink_dispatch);
	vmsg.nv_ifv = ifv;
	vmsg.nv_ifp_p = ifp_p;

	ifnet_domsg(&vmsg.base.lmsg, 0);

	crit_enter();
	if (LIST_EMPTY(&vlantrunks[mycpuid].vlan_list)) {
		ifp_p->if_vlantrunks = NULL;

		/*
		 * Make sure that all protocol threads see if_vlantrunks change.
		 */
		netmsg_service_sync();
		kfree(vlantrunks, M_VLAN);
	}
	crit_exit();
}

static void
vlan_unconfig_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct sockaddr_dl *sdl;
	struct ifvlan *ifv;
	struct ifnet *ifp_p, *ifp;
	int error;

	/* Assert in netisr0 */

	ifv = vmsg->nv_ifv;
	ifp = &ifv->ifv_if;

	if (ifp->if_flags & IFF_UP)
		if_down(ifp);

	ifnet_serialize_all(ifp);

	ifp->if_flags &= ~IFF_RUNNING;

	/*
	 * Save parent ifnet pointer and disconnect from parent.
	 *
	 * This is done early in this function, so input/output could
	 * know that we are disconnecting.
	 */
	ifp_p = ifv->ifv_p;
	ifv->ifv_p = NULL;

	/*
	 * Release vlan's serializer before reprogramming parent's
	 * multicast filter to avoid possible dead lock.
	 */
	ifnet_deserialize_all(ifp);

	if (ifp_p) {
		/*
		 * Since the interface is being unconfigured, we need to
		 * empty the list of multicast groups that we may have joined
		 * while we were alive from the parent's list.
		 */
		vlan_clrmulti(ifv, ifp_p);

		/* Clear parent's flags which was set by us. */
		vlan_setflags(ifv, ifp_p, 0);
	}

	ifnet_serialize_all(ifp);

	ifp->if_mtu = ETHERMTU;

	/* Clear our MAC address. */
	sdl = IF_LLSOCKADDR(ifp);
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ETHER_ADDR_LEN;
	bzero(LLADDR(sdl), ETHER_ADDR_LEN);
	bzero(ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	ifnet_deserialize_all(ifp);

	/* Unlink vlan from parent's vlantrunk */
	if (ifp_p != NULL && ifp_p->if_vlantrunks != NULL)
		vlan_unlink(ifv, ifp_p);

	error = 0;
	lwkt_replymsg(&vmsg->base.lmsg, error);
}

static int
vlan_unconfig(struct ifvlan *ifv)
{
	struct netmsg_vlan vmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_unconfig_dispatch);
	vmsg.nv_ifv = ifv;

	return lwkt_domsg(netisr_cpuport(0), &vmsg.base.lmsg, 0);
}

static int
vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ifvlan *ifv = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifnet *ifp_p;
	struct vlanreq vlr;
	int error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (cmd) {
	case SIOCGIFMEDIA:
		ifp_p = ifv->ifv_p;
		if (ifp_p != NULL) {
			/*
			 * Release vlan interface's serializer to void
			 * possible dead lock.
			 */
			ifnet_deserialize_all(ifp);

			ifnet_serialize_all(ifp_p);
			error = ifp_p->if_ioctl(ifp_p, SIOCGIFMEDIA, data, cr);
			ifnet_deserialize_all(ifp_p);

			ifnet_serialize_all(ifp);

			if (ifv->ifv_p == NULL || ifv->ifv_p != ifp_p) {
				/*
				 * We are disconnected from the original
				 * parent interface or the parent interface
				 * is changed, after vlan interface's
				 * serializer is released.
				 */
				error = EINVAL;
			}

			/* Limit the result to the parent's current config. */
			if (error == 0) {
				struct ifmediareq *ifmr;

				ifmr = (struct ifmediareq *) data;
				if (ifmr->ifm_count >= 1 && ifmr->ifm_ulist) {
					ifmr->ifm_count = 1;
					error = copyout(&ifmr->ifm_current,
						ifmr->ifm_ulist, 
						sizeof(int));
				}
			}
		} else {
			error = EINVAL;
		}
		break;

	case SIOCSIFMEDIA:
		error = EINVAL;
		break;

	case SIOCSETVLAN:
		error = copyin(ifr->ifr_data, &vlr, sizeof vlr);
		if (error)
			break;

		ifnet_deserialize_all(ifp);
		if (vlr.vlr_parent[0] == '\0')
			error = vlan_unconfig(ifv);
		else
			error = vlan_config(ifv, vlr.vlr_parent, vlr.vlr_tag);
		ifnet_serialize_all(ifp);
		break;

	case SIOCGETVLAN:
		bzero(&vlr, sizeof(vlr));
		if (ifv->ifv_p) {
			strlcpy(vlr.vlr_parent, ifv->ifv_p->if_xname,
			    sizeof(vlr.vlr_parent));
			vlr.vlr_tag = ifv->ifv_tag;
		}
		error = copyout(&vlr, ifr->ifr_data, sizeof vlr);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ifp->if_init(ifp);
		else
			ifp->if_flags &= ~IFF_RUNNING;

		/*
		 * We should propagate selected flags to the parent,
		 * e.g., promiscuous mode.
		 */
		ifnet_deserialize_all(ifp);
		error = vlan_config_flags(ifv);
		ifnet_serialize_all(ifp);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ifnet_deserialize_all(ifp);
		error = vlan_config_multi(ifv);
		ifnet_serialize_all(ifp);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static void
vlan_multi_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct ifvlan *ifv = vmsg->nv_ifv;
	int error = 0;

	/*
	 * If we don't have a parent, just remember the membership for
	 * when we do.
	 */
	if (ifv->ifv_p != NULL)
		error = vlan_setmulti(ifv, ifv->ifv_p);
	lwkt_replymsg(&vmsg->base.lmsg, error);
}

static int
vlan_config_multi(struct ifvlan *ifv)
{
	struct netmsg_vlan vmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_multi_dispatch);
	vmsg.nv_ifv = ifv;

	return lwkt_domsg(netisr_cpuport(0), &vmsg.base.lmsg, 0);
}

static void
vlan_flags_dispatch(netmsg_t msg)
{
	struct netmsg_vlan *vmsg = (struct netmsg_vlan *)msg;
	struct ifvlan *ifv = vmsg->nv_ifv;
	int error = 0;

	/*
	 * If we don't have a parent, just remember the flags for
	 * when we do.
	 */
	if (ifv->ifv_p != NULL)
		error = vlan_setflags(ifv, ifv->ifv_p, 1);
	lwkt_replymsg(&vmsg->base.lmsg, error);
}

static int
vlan_config_flags(struct ifvlan *ifv)
{
	struct netmsg_vlan vmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(&ifv->ifv_if);

	bzero(&vmsg, sizeof(vmsg));

	netmsg_init(&vmsg.base, NULL, &curthread->td_msgport,
		    0, vlan_flags_dispatch);
	vmsg.nv_ifv = ifv;

	return lwkt_domsg(netisr_cpuport(0), &vmsg.base.lmsg, 0);
}
