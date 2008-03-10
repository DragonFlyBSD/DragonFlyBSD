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
 * $DragonFly: src/sys/net/vlan/if_vlan.c,v 1.28 2008/03/10 10:47:57 sephe Exp $
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
 * XXX It's incorrect to assume that we must always kludge up
 * headers on the physical device's behalf: some devices support
 * VLAN tag insertion and extraction in firmware. For these cases,
 * one can change the behavior of the vlan interface by setting
 * the LINK0 flag on it (that is setting the vlan interface's LINK0
 * flag, _not_ the parent's LINK0 flag; we try to leave the parent
 * alone). If the interface has the LINK0 flag set, then it will
 * not modify the ethernet header on output, because the parent
 * can do that for itself. On input, the parent can call vlan_input_tag()
 * directly in order to supply us with an incoming mbuf and the vlan
 * tag value that goes with it.
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
#include <net/vlan/if_vlan_var.h>
#include <net/netmsg2.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

struct vlan_mc_entry {
	struct ether_addr		mc_addr;
	SLIST_ENTRY(vlan_mc_entry)	mc_entries;
};

struct	ifvlan {
	struct	arpcom ifv_ac;	/* make this an interface */
	struct	ifnet *ifv_p;	/* parent inteface of this vlan */
	struct	ifv_linkmib {
		int	ifvm_parent;
		u_int16_t ifvm_proto; /* encapsulation ethertype */
		u_int16_t ifvm_tag; /* tag to apply on packets leaving if */
	}	ifv_mib;
	SLIST_HEAD(__vlan_mchead, vlan_mc_entry)	vlan_mc_listhead;
	LIST_ENTRY(ifvlan) ifv_list;
	struct resource *r_unit;	/* resource allocated for this unit */
};
#define	ifv_if	ifv_ac.ac_if
#define	ifv_tag	ifv_mib.ifvm_tag

#define VLANNAME	"vlan"

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_L2VLAN, vlan, CTLFLAG_RW, 0, "IEEE 802.1Q VLAN");
SYSCTL_NODE(_net_link_vlan, PF_LINK, link, CTLFLAG_RW, 0, "for consistency");

static MALLOC_DEFINE(M_VLAN, "vlan", "802.1Q Virtual LAN Interface");
static LIST_HEAD(, ifvlan) ifv_list;

static	int vlan_clone_create(struct if_clone *, int);
static	void vlan_clone_destroy(struct ifnet *);
static	void vlan_start(struct ifnet *ifp);
static	void vlan_ifinit(void *foo);
static	int vlan_input(const struct ether_header *eh, struct mbuf *m);
static	int vlan_input_tag(struct mbuf *m, uint16_t t);
static	int vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr,
		struct ucred *cr);
static	int vlan_setmulti(struct ifnet *ifp);
static	int vlan_unconfig(struct ifnet *ifp);
static	int vlan_config(struct ifvlan *ifv, struct ifnet *p);

struct if_clone vlan_cloner = IF_CLONE_INITIALIZER("vlan", vlan_clone_create,
    vlan_clone_destroy, NVLAN, IF_MAXUNIT);

void	vlan_start_dispatch(struct netmsg *);

/*
 * Program our multicast filter. What we're actually doing is
 * programming the multicast filter of the parent. This has the
 * side effect of causing the parent interface to receive multicast
 * traffic that it doesn't really want, which ends up being discarded
 * later by the upper protocol layers. Unfortunately, there's no way
 * to avoid this: there really is only one physical interface.
 */
static int
vlan_setmulti(struct ifnet *ifp)
{
	struct ifnet		*ifp_p;
	struct ifmultiaddr	*ifma, *rifma = NULL;
	struct ifvlan		*sc;
	struct vlan_mc_entry	*mc = NULL;
	struct sockaddr_dl	sdl;
	int			error;

	/* Find the parent. */
	sc = ifp->if_softc;
	ifp_p = sc->ifv_p;

	/*
	 * If we don't have a parent, just remember the membership for
	 * when we do.
	 */
	if (ifp_p == NULL)
		return(0);

	bzero((char *)&sdl, sizeof sdl);
	sdl.sdl_len = sizeof sdl;
	sdl.sdl_family = AF_LINK;
	sdl.sdl_index = ifp_p->if_index;
	sdl.sdl_type = IFT_ETHER;
	sdl.sdl_alen = ETHER_ADDR_LEN;

	/* First, remove any existing filter entries. */
	while(SLIST_FIRST(&sc->vlan_mc_listhead) != NULL) {
		mc = SLIST_FIRST(&sc->vlan_mc_listhead);
		bcopy((char *)&mc->mc_addr, LLADDR(&sdl), ETHER_ADDR_LEN);
		error = if_delmulti(ifp_p, (struct sockaddr *)&sdl);
		if (error)
			return(error);
		SLIST_REMOVE_HEAD(&sc->vlan_mc_listhead, mc_entries);
		kfree(mc, M_VLAN);
	}

	/* Now program new ones. */
	LIST_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		mc = kmalloc(sizeof(struct vlan_mc_entry), M_VLAN, M_WAITOK);
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    (char *)&mc->mc_addr, ETHER_ADDR_LEN);
		SLIST_INSERT_HEAD(&sc->vlan_mc_listhead, mc, mc_entries);
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    LLADDR(&sdl), ETHER_ADDR_LEN);
		error = if_addmulti(ifp_p, (struct sockaddr *)&sdl, &rifma);
		if (error)
			return(error);
	}

	return(0);
}

static int
vlan_modevent(module_t mod, int type, void *data) 
{ 

	switch (type) { 
	case MOD_LOAD: 
		LIST_INIT(&ifv_list);
		vlan_input_p = vlan_input;
		vlan_input_tag_p = vlan_input_tag;
		if_clone_attach(&vlan_cloner);
		break; 
	case MOD_UNLOAD: 
		if_clone_detach(&vlan_cloner);
		vlan_input_p = NULL;
		vlan_input_tag_p = NULL;
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

static int
vlan_clone_create(struct if_clone *ifc, int unit)
{
	struct ifvlan *ifv;
	struct ifnet *ifp;

	ifv = kmalloc(sizeof(struct ifvlan), M_VLAN, M_WAITOK | M_ZERO);
	ifp = &ifv->ifv_if;
	SLIST_INIT(&ifv->vlan_mc_listhead);

	crit_enter();
	LIST_INSERT_HEAD(&ifv_list, ifv, ifv_list);
	crit_exit();

	ifp->if_softc = ifv;
	if_initname(ifp, "vlan", unit);
	/* NB: flags are not set here */
	ifp->if_linkmib = &ifv->ifv_mib;
	ifp->if_linkmiblen = sizeof ifv->ifv_mib;
	/* NB: mtu is not set here */

	ifp->if_init = vlan_ifinit;
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

static void
vlan_clone_destroy(struct ifnet *ifp)
{
	struct ifvlan *ifv = ifp->if_softc;

	crit_enter();

	LIST_REMOVE(ifv, ifv_list);
	vlan_unconfig(ifp);
	ether_ifdetach(ifp);

	crit_exit();

	kfree(ifv, M_VLAN);
}

static void
vlan_ifinit(void *foo)
{
	return;
}

static void
vlan_start(struct ifnet *ifp)
{
	struct ifvlan *ifv;
	struct ifnet *ifp_p;
	struct mbuf *m;

	ifv = ifp->if_softc;
	ifp_p = ifv->ifv_p;

	ifp->if_flags |= IFF_OACTIVE;
	for (;;) {
		struct netmsg_packet *nmp;
		struct netmsg *nmsg;
		struct lwkt_port *port;

		m = ifq_dequeue(&ifp->if_snd, NULL);
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
			ifp->if_data.ifi_collisions++;
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
		nmsg = &nmp->nm_netmsg;

		netmsg_init(nmsg, &netisr_apanic_rport, 0, vlan_start_dispatch);
		nmp->nm_packet = m;
		nmsg->nm_lmsg.u.ms_resultp = ifp_p;

		port = cpu_portfn(ifp_p->if_index % ncpus /* XXX */);
		lwkt_sendmsg(port, &nmp->nm_netmsg.nm_lmsg);
		ifp->if_opackets++;
	}
	ifp->if_flags &= ~IFF_OACTIVE;
}

static int
vlan_input_tag( struct mbuf *m, uint16_t t)
{
	struct bpf_if *bif;
	struct ifvlan *ifv;
	struct ifnet *rcvif;

	rcvif = m->m_pkthdr.rcvif;

	ASSERT_SERIALIZED(rcvif->if_serializer);

	/*
	 * Fake up a header and send the packet to the physical interface's
	 * bpf tap if active.
	 */
	if ((bif = rcvif->if_bpf) != NULL) {
		struct ether_header *eh;
		struct ether_vlan_header evh;

		eh = mtod(m, struct ether_header *);
		m_adj(m, ETHER_HDR_LEN);
		bcopy(eh, &evh, 2*ETHER_ADDR_LEN);
		evh.evl_encap_proto = htons(ETHERTYPE_VLAN);
		evh.evl_tag = htons(t);
		evh.evl_proto = eh->ether_type;
		bpf_ptap(bif, m, &evh, ETHER_HDR_LEN + EVL_ENCAPLEN);
		/* XXX assumes data was left intact */
		M_PREPEND(m, ETHER_HDR_LEN, MB_WAIT); 
	}

	for (ifv = LIST_FIRST(&ifv_list); ifv != NULL;
	    ifv = LIST_NEXT(ifv, ifv_list)) {
		if (rcvif == ifv->ifv_p && ifv->ifv_tag == t)
			break;
	}

	if (ifv == NULL || (ifv->ifv_if.if_flags & IFF_UP) == 0) {
		m_freem(m);
		return -1;	/* So the parent can take note */
	}

	/*
	 * Having found a valid vlan interface corresponding to
	 * the given source interface and vlan tag, run the
	 * the real packet through ether_input().
	 */
	m->m_pkthdr.rcvif = &ifv->ifv_if;

	ifv->ifv_if.if_ipackets++;
	lwkt_serialize_exit(rcvif->if_serializer);
	lwkt_serialize_enter(ifv->ifv_if.if_serializer);
	ether_input(&ifv->ifv_if, m);
	lwkt_serialize_exit(ifv->ifv_if.if_serializer);
	lwkt_serialize_enter(rcvif->if_serializer);
	return 0;
}

static int
vlan_input(const struct ether_header *eh, struct mbuf *m)
{
	struct ifvlan *ifv;
	struct ifnet *rcvif;
	struct ether_header eh_copy;

	rcvif = m->m_pkthdr.rcvif;
	ASSERT_SERIALIZED(rcvif->if_serializer);

	for (ifv = LIST_FIRST(&ifv_list); ifv != NULL;
	    ifv = LIST_NEXT(ifv, ifv_list)) {
		if (rcvif == ifv->ifv_p
		    && (EVL_VLANOFTAG(ntohs(*mtod(m, u_int16_t *)))
			== ifv->ifv_tag))
			break;
	}

	if (ifv == NULL || (ifv->ifv_if.if_flags & IFF_UP) == 0) {
		rcvif->if_noproto++;
		m_freem(m);
		return -1;	/* so ether_input can take note */
	}

	/*
	 * Having found a valid vlan interface corresponding to
	 * the given source interface and vlan tag, remove the
	 * remaining encapsulation (ether_vlan_header minus the ether_header
	 * that had already been removed) and run the real packet
	 * through ether_input() a second time (it had better be
	 * reentrant!).
	 */
	eh_copy = *eh;
	eh_copy.ether_type = mtod(m, u_int16_t *)[1];	/* evl_proto */
	m->m_pkthdr.rcvif = &ifv->ifv_if;
	m_adj(m, EVL_ENCAPLEN);
	M_PREPEND(m, ETHER_HDR_LEN, MB_WAIT); 
	*(struct ether_header *)mtod(m, void *) = eh_copy;

	ifv->ifv_if.if_ipackets++;
	lwkt_serialize_exit(rcvif->if_serializer);
	lwkt_serialize_enter(ifv->ifv_if.if_serializer);
	ether_input(&ifv->ifv_if, m);
	lwkt_serialize_exit(ifv->ifv_if.if_serializer);
	lwkt_serialize_enter(rcvif->if_serializer);
	return 0;
}

static int
vlan_config(struct ifvlan *ifv, struct ifnet *p)
{
	struct sockaddr_dl *sdl1, *sdl2;

	if (p->if_data.ifi_type != IFT_ETHER)
		return EPROTONOSUPPORT;
	if (ifv->ifv_p)
		return EBUSY;
	ifv->ifv_p = p;
	if (p->if_capenable & IFCAP_VLAN_MTU)
		ifv->ifv_if.if_mtu = p->if_mtu;
	else
		ifv->ifv_if.if_mtu = p->if_data.ifi_mtu - EVL_ENCAPLEN;

	/*
	 * Copy only a selected subset of flags from the parent.
	 * Other flags are none of our business.
	 */
	ifv->ifv_if.if_flags = (p->if_flags &
	    (IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX | IFF_POINTOPOINT));

	/*
	 * Set up our ``Ethernet address'' to reflect the underlying
	 * physical interface's.
	 */
	sdl1 = IF_LLSOCKADDR(&ifv->ifv_if);
	sdl2 = IF_LLSOCKADDR(p);
	sdl1->sdl_type = IFT_ETHER;
	sdl1->sdl_alen = ETHER_ADDR_LEN;
	bcopy(LLADDR(sdl2), LLADDR(sdl1), ETHER_ADDR_LEN);
	bcopy(LLADDR(sdl2), ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	/*
	 * Configure multicast addresses that may already be
	 * joined on the vlan device.
	 */
	vlan_setmulti(&ifv->ifv_if);

	return 0;
}

static int
vlan_unconfig(struct ifnet *ifp)
{
	struct sockaddr_dl *sdl;
	struct vlan_mc_entry *mc;
	struct ifvlan *ifv;
	struct ifnet *p;
	int error;

	ifv = ifp->if_softc;
	p = ifv->ifv_p;

	if (p) {
		struct sockaddr_dl sdl;

		/*
		 * Since the interface is being unconfigured, we need to
		 * empty the list of multicast groups that we may have joined
		 * while we were alive from the parent's list.
		 */
		bzero((char *)&sdl, sizeof sdl);
		sdl.sdl_len = sizeof sdl;
		sdl.sdl_family = AF_LINK;
		sdl.sdl_index = p->if_index;
		sdl.sdl_type = IFT_ETHER;
		sdl.sdl_alen = ETHER_ADDR_LEN;

		while(SLIST_FIRST(&ifv->vlan_mc_listhead) != NULL) {
			mc = SLIST_FIRST(&ifv->vlan_mc_listhead);
			bcopy((char *)&mc->mc_addr, LLADDR(&sdl), ETHER_ADDR_LEN);
			error = if_delmulti(p, (struct sockaddr *)&sdl);
			if (error)
				return(error);
			SLIST_REMOVE_HEAD(&ifv->vlan_mc_listhead, mc_entries);
			kfree(mc, M_VLAN);
		}
	}

	/* Disconnect from parent. */
	ifv->ifv_p = NULL;
	ifv->ifv_if.if_mtu = ETHERMTU;

	/* Clear our MAC address. */
	sdl = IF_LLSOCKADDR(&ifv->ifv_if);
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ETHER_ADDR_LEN;
	bzero(LLADDR(sdl), ETHER_ADDR_LEN);
	bzero(ifv->ifv_ac.ac_enaddr, ETHER_ADDR_LEN);

	return 0;
}

static int
vlan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ifaddr *ifa;
	struct ifnet *p;
	struct ifreq *ifr;
	struct ifvlan *ifv;
	struct vlanreq vlr;
	int error = 0;

	ifr = (struct ifreq *)data;
	ifa = (struct ifaddr *)data;
	ifv = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);
	crit_enter();

	switch (cmd) {
	case SIOCGIFMEDIA:
		if (ifv->ifv_p != NULL) {
			lwkt_serialize_exit(ifp->if_serializer);
			lwkt_serialize_enter(ifv->ifv_p->if_serializer);
			error = ifv->ifv_p->if_ioctl(ifv->ifv_p,
						     SIOCGIFMEDIA, data, cr);
			lwkt_serialize_exit(ifv->ifv_p->if_serializer);
			lwkt_serialize_enter(ifp->if_serializer);
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
		} else
			error = EINVAL;
		break;

	case SIOCSIFMEDIA:
		error = EINVAL;
		break;

	case SIOCSETVLAN:
		error = copyin(ifr->ifr_data, &vlr, sizeof vlr);
		if (error)
			break;
		if (vlr.vlr_parent[0] == '\0') {
			vlan_unconfig(ifp);
			if (ifp->if_flags & IFF_UP)
				if_down(ifp);
			ifp->if_flags &= ~IFF_RUNNING;
			break;
		}
		p = ifunit(vlr.vlr_parent);
		if (p == 0) {
			error = ENOENT;
			break;
		}
		error = vlan_config(ifv, p);
		if (error)
			break;
		ifv->ifv_tag = vlr.vlr_tag;
		ifp->if_flags |= IFF_RUNNING;
		break;
		
	case SIOCGETVLAN:
		bzero(&vlr, sizeof vlr);
		if (ifv->ifv_p) {
			strlcpy(vlr.vlr_parent, ifv->ifv_p->if_xname,
			    sizeof(vlr.vlr_parent));
			vlr.vlr_tag = ifv->ifv_tag;
		}
		error = copyout(&vlr, ifr->ifr_data, sizeof vlr);
		break;
		
	case SIOCSIFFLAGS:
		/*
		 * We don't support promiscuous mode
		 * right now because it would require help from the
		 * underlying drivers, which hasn't been implemented.
		 */
		if (ifr->ifr_flags & (IFF_PROMISC)) {
			ifp->if_flags &= ~(IFF_PROMISC);
			error = EINVAL;
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = vlan_setmulti(ifp);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	crit_exit();

	return error;
}
