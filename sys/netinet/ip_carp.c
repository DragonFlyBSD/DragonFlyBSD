/*
 * Copyright (c) 2002 Michael Shalayeff. All rights reserved.
 * Copyright (c) 2003 Ryan McBride. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * $FreeBSD: src/sys/netinet/ip_carp.c,v 1.48 2007/02/02 09:39:09 glebius Exp $
 * $DragonFly: src/sys/netinet/ip_carp.c,v 1.10 2008/07/27 10:06:57 sephe Exp $
 */

#include "opt_carp.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/in_cksum.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <machine/stdarg.h>
#include <crypto/sha1.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_clone.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/if_ether.h>
#endif

#ifdef INET6
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet6/nd6.h>
#endif

#include <netinet/ip_carp.h>

#define	CARP_IFNAME		"carp"
#define CARP_IS_RUNNING(ifp)	\
	(((ifp)->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))

struct carp_vhaddr {
	uint32_t		vha_flags;	/* CARP_VHAF_ */
	const struct in_ifaddr	*vha_ia;	/* carp address */
	const struct in_ifaddr	*vha_iaback;	/* backing address */
	TAILQ_ENTRY(carp_vhaddr) vha_link;
};
TAILQ_HEAD(carp_vhaddr_list, carp_vhaddr);

struct carp_softc {
	struct ifnet		 sc_if;
	struct ifnet		*sc_carpdev;	/* parent interface */
	struct carp_vhaddr_list	 sc_vha_list;	/* virtual addr list */

	const struct in_ifaddr	*sc_ia;		/* primary iface address v4 */
	struct ip_moptions 	 sc_imo;

#ifdef INET6
	struct in6_ifaddr 	*sc_ia6;	/* primary iface address v6 */
	struct ip6_moptions 	 sc_im6o;
#endif /* INET6 */
	TAILQ_ENTRY(carp_softc)	 sc_list;

	enum { INIT = 0, BACKUP, MASTER }
				 sc_state;
	int			 sc_dead;

	int			 sc_suppress;

	int			 sc_sendad_errors;
#define	CARP_SENDAD_MAX_ERRORS	3
	int			 sc_sendad_success;
#define	CARP_SENDAD_MIN_SUCCESS 3

	int			 sc_vhid;
	int			 sc_advskew;
	int			 sc_naddrs;	/* actually used IPv4 vha */
	int			 sc_naddrs6;
	int			 sc_advbase;	/* seconds */
	int			 sc_init_counter;
	uint64_t		 sc_counter;

	/* authentication */
#define CARP_HMAC_PAD	64
	unsigned char		 sc_key[CARP_KEY_LEN];
	unsigned char		 sc_pad[CARP_HMAC_PAD];
	SHA1_CTX		 sc_sha1;

	struct callout		 sc_ad_tmo;	/* advertisement timeout */
	struct callout		 sc_md_tmo;	/* master down timeout */
	struct callout 		 sc_md6_tmo;	/* master down timeout */

	LIST_ENTRY(carp_softc)	 sc_next;	/* Interface clue */
};

struct carp_if {
	TAILQ_HEAD(, carp_softc) vhif_vrs;
};

enum	{ CARP_COUNT_MASTER, CARP_COUNT_RUNNING };

SYSCTL_DECL(_net_inet_carp);

static int carp_opts[CARPCTL_MAXID] = { 0, 1, 0, 1, 0, 0 }; /* XXX for now */
SYSCTL_INT(_net_inet_carp, CARPCTL_ALLOW, allow, CTLFLAG_RW,
    &carp_opts[CARPCTL_ALLOW], 0, "Accept incoming CARP packets");
SYSCTL_INT(_net_inet_carp, CARPCTL_PREEMPT, preempt, CTLFLAG_RW,
    &carp_opts[CARPCTL_PREEMPT], 0, "high-priority backup preemption mode");
SYSCTL_INT(_net_inet_carp, CARPCTL_LOG, log, CTLFLAG_RW,
    &carp_opts[CARPCTL_LOG], 0, "log bad carp packets");
SYSCTL_INT(_net_inet_carp, CARPCTL_ARPBALANCE, arpbalance, CTLFLAG_RW,
    &carp_opts[CARPCTL_ARPBALANCE], 0, "balance arp responses");

static int carp_suppress_preempt = 0;
SYSCTL_INT(_net_inet_carp, OID_AUTO, suppress_preempt, CTLFLAG_RD,
    &carp_suppress_preempt, 0, "Preemption is suppressed");

static struct carpstats carpstats;
SYSCTL_STRUCT(_net_inet_carp, CARPCTL_STATS, stats, CTLFLAG_RW,
    &carpstats, carpstats,
    "CARP statistics (struct carpstats, netinet/ip_carp.h)");

#define	CARP_LOG(...)	do {				\
	if (carp_opts[CARPCTL_LOG] > 0)			\
		log(LOG_INFO, __VA_ARGS__);		\
} while (0)

#define	CARP_DEBUG(...)	do {				\
	if (carp_opts[CARPCTL_LOG] > 1)			\
		log(LOG_DEBUG, __VA_ARGS__);		\
} while (0)

static void	carp_hmac_prepare(struct carp_softc *);
static void	carp_hmac_generate(struct carp_softc *, uint32_t *,
		    unsigned char *);
static int	carp_hmac_verify(struct carp_softc *, uint32_t *,
		    unsigned char *);
static void	carp_setroute(struct carp_softc *, int);
static void	carp_input_c(struct mbuf *, struct carp_header *, sa_family_t);
static int 	carp_clone_create(struct if_clone *, int, caddr_t);
static void 	carp_clone_destroy(struct ifnet *);
static void	carp_detach(struct carp_softc *, int);
static int	carp_prepare_ad(struct mbuf *, struct carp_softc *,
		    struct carp_header *);
static void	carp_send_ad_all(void);
static void	carp_send_ad_timeout(void *);
static void	carp_send_ad(struct carp_softc *);
static void	carp_send_arp(struct carp_softc *);
static void	carp_master_down_timeout(void *);
static void	carp_master_down(struct carp_softc *);
static int	carp_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static int	carp_looutput(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	carp_start(struct ifnet *);
static void	carp_setrun(struct carp_softc *, sa_family_t);
static void	carp_set_state(struct carp_softc *, int);

static void	carp_multicast_cleanup(struct carp_softc *);
static void	carp_add_addr(struct carp_softc *, struct ifaddr *);
static void	carp_del_addr(struct carp_softc *, struct ifaddr *);
static void	carp_config_addr(struct carp_softc *, struct ifaddr *);
static void	carp_link_addrs(struct carp_softc *, struct ifnet *,
		    struct ifaddr *);
static void	carp_unlink_addrs(struct carp_softc *, struct ifnet *,
		    struct ifaddr *);

static int	carp_get_vhaddr(struct carp_softc *, struct ifdrv *);
static int	carp_config_vhaddr(struct carp_softc *, struct carp_vhaddr *);
static int	carp_activate_vhaddr(struct carp_softc *, struct carp_vhaddr *,
		    struct ifnet *, const struct in_ifaddr *, int);
static void	carp_deactivate_vhaddr(struct carp_softc *,
		    struct carp_vhaddr *);

static void	carp_sc_state(struct carp_softc *);
#ifdef INET6
static void	carp_send_na(struct carp_softc *);
static int	carp_set_addr6(struct carp_softc *, struct sockaddr_in6 *);
static int	carp_del_addr6(struct carp_softc *, struct sockaddr_in6 *);
static void	carp_multicast6_cleanup(struct carp_softc *);
#endif
static void	carp_stop(struct carp_softc *, int);
static void	carp_reset(struct carp_softc *, int);

static void	carp_ifaddr(void *, struct ifnet *, enum ifaddr_event,
			    struct ifaddr *);
static void	carp_ifdetach(void *, struct ifnet *);

static MALLOC_DEFINE(M_CARP, "CARP", "CARP interfaces");

static LIST_HEAD(, carp_softc) carpif_list;

static struct if_clone carp_cloner =
IF_CLONE_INITIALIZER(CARP_IFNAME, carp_clone_create, carp_clone_destroy,
		     0, IF_MAXUNIT);

static eventhandler_tag carp_ifdetach_event;
static eventhandler_tag carp_ifaddr_event;

static __inline void
carp_insert_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha_new)
{
	struct carp_vhaddr *vha;
	u_long new_addr, addr;

	KKASSERT((vha_new->vha_flags & CARP_VHAF_ONLIST) == 0);

	/*
	 * Virtual address list is sorted; smaller one first
	 */
	new_addr = ntohl(vha_new->vha_ia->ia_addr.sin_addr.s_addr);

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		addr = ntohl(vha->vha_ia->ia_addr.sin_addr.s_addr);

		if (addr > new_addr)
			break;
	}
	if (vha == NULL)
		TAILQ_INSERT_TAIL(&sc->sc_vha_list, vha_new, vha_link);
	else
		TAILQ_INSERT_BEFORE(vha, vha_new, vha_link);
	vha_new->vha_flags |= CARP_VHAF_ONLIST;
}

static __inline void
carp_remove_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha)
{
	KKASSERT(vha->vha_flags & CARP_VHAF_ONLIST);
	vha->vha_flags &= ~CARP_VHAF_ONLIST;
	TAILQ_REMOVE(&sc->sc_vha_list, vha, vha_link);
}

static void
carp_hmac_prepare(struct carp_softc *sc)
{
	uint8_t version = CARP_VERSION, type = CARP_ADVERTISEMENT;
	uint8_t vhid = sc->sc_vhid & 0xff;
	int i;
#ifdef INET6
	struct ifaddr_container *ifac;
	struct in6_addr in6;
#endif
#ifdef INET
	struct carp_vhaddr *vha;
#endif

	/* XXX: possible race here */

	/* compute ipad from key */
	bzero(sc->sc_pad, sizeof(sc->sc_pad));
	bcopy(sc->sc_key, sc->sc_pad, sizeof(sc->sc_key));
	for (i = 0; i < sizeof(sc->sc_pad); i++)
		sc->sc_pad[i] ^= 0x36;

	/* precompute first part of inner hash */
	SHA1Init(&sc->sc_sha1);
	SHA1Update(&sc->sc_sha1, sc->sc_pad, sizeof(sc->sc_pad));
	SHA1Update(&sc->sc_sha1, (void *)&version, sizeof(version));
	SHA1Update(&sc->sc_sha1, (void *)&type, sizeof(type));
	SHA1Update(&sc->sc_sha1, (void *)&vhid, sizeof(vhid));
#ifdef INET
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		SHA1Update(&sc->sc_sha1,
		    (const uint8_t *)&vha->vha_ia->ia_addr.sin_addr,
		    sizeof(struct in_addr));
	}
#endif /* INET */
#ifdef INET6
	TAILQ_FOREACH(ifac, &sc->sc_if.if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr->sa_family == AF_INET6) {
			in6 = ifatoia6(ifa)->ia_addr.sin6_addr;
			in6_clearscope(&in6);
			SHA1Update(&sc->sc_sha1, (void *)&in6, sizeof(in6));
		}
	}
#endif /* INET6 */

	/* convert ipad to opad */
	for (i = 0; i < sizeof(sc->sc_pad); i++)
		sc->sc_pad[i] ^= 0x36 ^ 0x5c;
}

static void
carp_hmac_generate(struct carp_softc *sc, uint32_t counter[2],
    unsigned char md[20])
{
	SHA1_CTX sha1ctx;

	/* fetch first half of inner hash */
	bcopy(&sc->sc_sha1, &sha1ctx, sizeof(sha1ctx));

	SHA1Update(&sha1ctx, (void *)counter, sizeof(sc->sc_counter));
	SHA1Final(md, &sha1ctx);

	/* outer hash */
	SHA1Init(&sha1ctx);
	SHA1Update(&sha1ctx, sc->sc_pad, sizeof(sc->sc_pad));
	SHA1Update(&sha1ctx, md, 20);
	SHA1Final(md, &sha1ctx);
}

static int
carp_hmac_verify(struct carp_softc *sc, uint32_t counter[2],
    unsigned char md[20])
{
	unsigned char md2[20];

	carp_hmac_generate(sc, counter, md2);
	return (bcmp(md, md2, sizeof(md2)));
}

static void
carp_setroute(struct carp_softc *sc, int cmd)
{
#ifdef INET6
	struct ifaddr_container *ifac;

	crit_enter();
	TAILQ_FOREACH(ifac, &sc->sc_if.if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr->sa_family == AF_INET6) {
			if (cmd == RTM_ADD)
				in6_ifaddloop(ifa);
			else
				in6_ifremloop(ifa);
		}
	}
	crit_exit();
#endif /* INET6 */
}

static int
carp_clone_create(struct if_clone *ifc, int unit, caddr_t param __unused)
{
	struct carp_softc *sc;
	struct ifnet *ifp;

	sc = kmalloc(sizeof(*sc), M_CARP, M_WAITOK | M_ZERO);
	ifp = &sc->sc_if;

	sc->sc_suppress = 0;
	sc->sc_advbase = CARP_DFLTINTV;
	sc->sc_vhid = -1;	/* required setting */
	sc->sc_advskew = 0;
	sc->sc_init_counter = 1;
	sc->sc_naddrs = 0;
	sc->sc_naddrs6 = 0;

	TAILQ_INIT(&sc->sc_vha_list);

#ifdef INET6
	sc->sc_im6o.im6o_multicast_hlim = CARP_DFLTTL;
#endif

	callout_init(&sc->sc_ad_tmo);
	callout_init(&sc->sc_md_tmo);
	callout_init(&sc->sc_md6_tmo);

	ifp->if_softc = sc;
        if_initname(ifp, CARP_IFNAME, unit);	
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_LOOPBACK;
	ifp->if_ioctl = carp_ioctl;
	ifp->if_output = carp_looutput;
	ifp->if_start = carp_start;
	ifp->if_type = IFT_CARP;
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	ifp->if_hdrlen = 0;
	if_attach(ifp, NULL);
	bpfattach(ifp, DLT_NULL, sizeof(u_int));

	crit_enter();
	LIST_INSERT_HEAD(&carpif_list, sc, sc_next);
	crit_exit();

	return (0);
}

static void
carp_clone_destroy(struct ifnet *ifp)
{
	struct carp_softc *sc = ifp->if_softc;

	sc->sc_dead = 1;
	carp_detach(sc, 1);

	crit_enter();
	LIST_REMOVE(sc, sc_next);
	crit_exit();
	bpfdetach(ifp);
	if_detach(ifp);

	KASSERT(sc->sc_naddrs == 0, ("certain inet address is still active\n"));
	kfree(sc, M_CARP);
}

static void
carp_detach(struct carp_softc *sc, int detach)
{
	struct carp_if *cif;

	carp_reset(sc, detach);

	carp_multicast_cleanup(sc);
#ifdef INET6
	carp_multicast6_cleanup(sc);
#endif

	if (!sc->sc_dead && detach) {
		struct carp_vhaddr *vha;

		TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
			carp_deactivate_vhaddr(sc, vha);
		KKASSERT(sc->sc_naddrs == 0);
	}

	if (sc->sc_carpdev != NULL) {
		cif = sc->sc_carpdev->if_carp;
		TAILQ_REMOVE(&cif->vhif_vrs, sc, sc_list);
		if (TAILQ_EMPTY(&cif->vhif_vrs)) {
			ifpromisc(sc->sc_carpdev, 0);
			sc->sc_carpdev->if_carp = NULL;
			kfree(cif, M_CARP);
		}
		sc->sc_carpdev = NULL;
		sc->sc_ia = NULL;
	}
}

/* Detach an interface from the carp. */
static void
carp_ifdetach(void *arg __unused, struct ifnet *ifp)
{
	struct carp_if *cif = ifp->if_carp;
	struct carp_softc *sc;

	while (ifp->if_carp &&
	       (sc = TAILQ_FIRST(&cif->vhif_vrs)) != NULL)
		carp_detach(sc, 1);
}

/*
 * process input packet.
 * we have rearranged checks order compared to the rfc,
 * but it seems more efficient this way or not possible otherwise.
 */
int
carp_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	struct carp_header *ch;
	int len, iphlen;

	iphlen = *offp;
	*mp = NULL;

	carpstats.carps_ipackets++;

	if (!carp_opts[CARPCTL_ALLOW]) {
		m_freem(m);
		return(IPPROTO_DONE);
	}

	/* Check if received on a valid carp interface */
	if (m->m_pkthdr.rcvif->if_carp == NULL) {
		carpstats.carps_badif++;
		CARP_LOG("carp_input: packet received on non-carp "
		    "interface: %s\n",
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return(IPPROTO_DONE);
	}

	/* Verify that the IP TTL is CARP_DFLTTL. */
	if (ip->ip_ttl != CARP_DFLTTL) {
		carpstats.carps_badttl++;
		CARP_LOG("carp_input: received ttl %d != %d on %s\n",
		    ip->ip_ttl, CARP_DFLTTL,
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return(IPPROTO_DONE);
	}

	/* Minimal CARP packet size */
	len = iphlen + sizeof(*ch);

	/*
	 * Verify that the received packet length is
	 * not less than the CARP header
	 */
	if (m->m_pkthdr.len < len) {
		carpstats.carps_badlen++;
		CARP_LOG("packet too short %d on %s\n", m->m_pkthdr.len,
			 m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return(IPPROTO_DONE);
	}

	/* Make sure that CARP header is contiguous */
	if (len > m->m_len) {
		m = m_pullup(m, len);
		if (m == NULL) {
			carpstats.carps_hdrops++;
			CARP_LOG("carp_input: m_pullup failed\n");
			return(IPPROTO_DONE);
		}
		ip = mtod(m, struct ip *);
	}
	ch = (struct carp_header *)((uint8_t *)ip + iphlen);

	/* Verify the CARP checksum */
	if (in_cksum_skip(m, len, iphlen)) {
		carpstats.carps_badsum++;
		CARP_LOG("carp_input: checksum failed on %s\n",
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return(IPPROTO_DONE);
	}
	carp_input_c(m, ch, AF_INET);
	return(IPPROTO_DONE);
}

#ifdef INET6
int
carp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct carp_header *ch;
	u_int len;

	carpstats.carps_ipackets6++;

	if (!carp_opts[CARPCTL_ALLOW]) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* check if received on a valid carp interface */
	if (m->m_pkthdr.rcvif->if_carp == NULL) {
		carpstats.carps_badif++;
		CARP_LOG("carp6_input: packet received on non-carp "
		    "interface: %s\n",
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* verify that the IP TTL is 255 */
	if (ip6->ip6_hlim != CARP_DFLTTL) {
		carpstats.carps_badttl++;
		CARP_LOG("carp6_input: received ttl %d != 255 on %s\n",
		    ip6->ip6_hlim,
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* verify that we have a complete carp packet */
	len = m->m_len;
	IP6_EXTHDR_GET(ch, struct carp_header *, m, *offp, sizeof(*ch));
	if (ch == NULL) {
		carpstats.carps_badlen++;
		CARP_LOG("carp6_input: packet size %u too small\n", len);
		return (IPPROTO_DONE);
	}

	/* verify the CARP checksum */
	if (in_cksum_range(m, 0, *offp, sizeof(*ch))) {
		carpstats.carps_badsum++;
		CARP_LOG("carp6_input: checksum failed, on %s\n",
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	carp_input_c(m, ch, AF_INET6);
	return (IPPROTO_DONE);
}
#endif /* INET6 */

static void
carp_input_c(struct mbuf *m, struct carp_header *ch, sa_family_t af)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ifnet *cifp;
	struct carp_softc *sc;
	uint64_t tmp_counter;
	struct timeval sc_tv, ch_tv;

	/* verify that the VHID is valid on the receiving interface */
	TAILQ_FOREACH(sc, &((struct carp_if *)ifp->if_carp)->vhif_vrs, sc_list)
		if (sc->sc_vhid == ch->carp_vhid)
			break;

	if (!sc || !CARP_IS_RUNNING(&sc->sc_if)) {
		carpstats.carps_badvhid++;
		m_freem(m);
		return;
	}
	cifp = &sc->sc_if;

	getmicrotime(&cifp->if_lastchange);
	cifp->if_ipackets++;
	cifp->if_ibytes += m->m_pkthdr.len;

	if (cifp->if_bpf) {
		struct ip *ip = mtod(m, struct ip *);

		/* BPF wants net byte order */
		ip->ip_len = htons(ip->ip_len + (ip->ip_hl << 2));
		ip->ip_off = htons(ip->ip_off);
		bpf_mtap(cifp->if_bpf, m);
	}

	/* verify the CARP version. */
	if (ch->carp_version != CARP_VERSION) {
		carpstats.carps_badver++;
		cifp->if_ierrors++;
		CARP_LOG("%s; invalid version %d\n", cifp->if_xname,
			 ch->carp_version);
		m_freem(m);
		return;
	}

	/* verify the hash */
	if (carp_hmac_verify(sc, ch->carp_counter, ch->carp_md)) {
		carpstats.carps_badauth++;
		cifp->if_ierrors++;
		CARP_LOG("%s: incorrect hash\n", cifp->if_xname);
		m_freem(m);
		return;
	}

	tmp_counter = ntohl(ch->carp_counter[0]);
	tmp_counter = tmp_counter<<32;
	tmp_counter += ntohl(ch->carp_counter[1]);

	/* XXX Replay protection goes here */

	sc->sc_init_counter = 0;
	sc->sc_counter = tmp_counter;

	sc_tv.tv_sec = sc->sc_advbase;
	if (carp_suppress_preempt && sc->sc_advskew <  240)
		sc_tv.tv_usec = 240 * 1000000 / 256;
	else
		sc_tv.tv_usec = sc->sc_advskew * 1000000 / 256;
	ch_tv.tv_sec = ch->carp_advbase;
	ch_tv.tv_usec = ch->carp_advskew * 1000000 / 256;

	switch (sc->sc_state) {
	case INIT:
		break;

	case MASTER:
		/*
		 * If we receive an advertisement from a master who's going to
		 * be more frequent than us, go into BACKUP state.
		 */
		if (timevalcmp(&sc_tv, &ch_tv, >) ||
		    timevalcmp(&sc_tv, &ch_tv, ==)) {
			callout_stop(&sc->sc_ad_tmo);
			CARP_DEBUG("%s: MASTER -> BACKUP "
			   "(more frequent advertisement received)\n",
			   cifp->if_xname);
			carp_set_state(sc, BACKUP);
			carp_setrun(sc, 0);
			carp_setroute(sc, RTM_DELETE);
		}
		break;

	case BACKUP:
		/*
		 * If we're pre-empting masters who advertise slower than us,
		 * and this one claims to be slower, treat him as down.
		 */
		if (carp_opts[CARPCTL_PREEMPT] &&
		    timevalcmp(&sc_tv, &ch_tv, <)) {
			CARP_DEBUG("%s: BACKUP -> MASTER "
			    "(preempting a slower master)\n", cifp->if_xname);
			carp_master_down(sc);
			break;
		}

		/*
		 *  If the master is going to advertise at such a low frequency
		 *  that he's guaranteed to time out, we'd might as well just
		 *  treat him as timed out now.
		 */
		sc_tv.tv_sec = sc->sc_advbase * 3;
		if (timevalcmp(&sc_tv, &ch_tv, <)) {
			CARP_DEBUG("%s: BACKUP -> MASTER (master timed out)\n",
				   cifp->if_xname);
			carp_master_down(sc);
			break;
		}

		/*
		 * Otherwise, we reset the counter and wait for the next
		 * advertisement.
		 */
		carp_setrun(sc, af);
		break;
	}
	m_freem(m);
}

static int
carp_prepare_ad(struct mbuf *m, struct carp_softc *sc, struct carp_header *ch)
{
	struct ifnet *cifp = &sc->sc_if;
	struct m_tag *mtag;

	if (sc->sc_init_counter) {
		/* this could also be seconds since unix epoch */
		sc->sc_counter = karc4random();
		sc->sc_counter = sc->sc_counter << 32;
		sc->sc_counter += karc4random();
	} else {
		sc->sc_counter++;
	}

	ch->carp_counter[0] = htonl((sc->sc_counter >> 32) & 0xffffffff);
	ch->carp_counter[1] = htonl(sc->sc_counter & 0xffffffff);

	carp_hmac_generate(sc, ch->carp_counter, ch->carp_md);

	/* Tag packet for carp_output */
	mtag = m_tag_get(PACKET_TAG_CARP, sizeof(struct ifnet *), MB_DONTWAIT);
	if (mtag == NULL) {
		m_freem(m);
		cifp->if_oerrors++;
		return ENOMEM;
	}
	bcopy(&cifp, (caddr_t)(mtag + 1), sizeof(struct ifnet *));
	m_tag_prepend(m, mtag);

	return 0;
}

static void
carp_send_ad_all(void)
{
	struct carp_softc *sc;

	LIST_FOREACH(sc, &carpif_list, sc_next) {
		if (sc->sc_carpdev == NULL)
			continue;

		if (CARP_IS_RUNNING(&sc->sc_if) && sc->sc_state == MASTER)
			carp_send_ad(sc);
	}
}

static void
carp_send_ad_timeout(void *xsc)
{
	carp_send_ad(xsc);
}

static void
carp_send_ad(struct carp_softc *sc)
{
	struct ifnet *cifp = &sc->sc_if;
	struct carp_header ch;
	struct timeval tv;
	struct carp_header *ch_ptr;
	struct mbuf *m;
	int len, advbase, advskew;

	if (!CARP_IS_RUNNING(cifp)) {
		/* Bow out */
		advbase = 255;
		advskew = 255;
	} else {
		advbase = sc->sc_advbase;
		if (!carp_suppress_preempt || sc->sc_advskew > 240)
			advskew = sc->sc_advskew;
		else
			advskew = 240;
		tv.tv_sec = advbase;
		tv.tv_usec = advskew * 1000000 / 256;
	}

	ch.carp_version = CARP_VERSION;
	ch.carp_type = CARP_ADVERTISEMENT;
	ch.carp_vhid = sc->sc_vhid;
	ch.carp_advbase = advbase;
	ch.carp_advskew = advskew;
	ch.carp_authlen = 7;	/* XXX DEFINE */
	ch.carp_pad1 = 0;	/* must be zero */
	ch.carp_cksum = 0;

#ifdef INET
	if (sc->sc_ia != NULL) {
		struct ip *ip;

		MGETHDR(m, MB_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			cifp->if_oerrors++;
			carpstats.carps_onomem++;
			/* XXX maybe less ? */
			if (advbase != 255 || advskew != 255)
				callout_reset(&sc->sc_ad_tmo, tvtohz_high(&tv),
				    carp_send_ad_timeout, sc);
			return;
		}
		len = sizeof(*ip) + sizeof(ch);
		m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = NULL;
		m->m_len = len;
		MH_ALIGN(m, m->m_len);
		m->m_flags |= M_MCAST;
		ip = mtod(m, struct ip *);
		ip->ip_v = IPVERSION;
		ip->ip_hl = sizeof(*ip) >> 2;
		ip->ip_tos = IPTOS_LOWDELAY;
		ip->ip_len = len;
		ip->ip_id = ip_newid();
		ip->ip_off = IP_DF;
		ip->ip_ttl = CARP_DFLTTL;
		ip->ip_p = IPPROTO_CARP;
		ip->ip_sum = 0;
		ip->ip_src = sc->sc_ia->ia_addr.sin_addr;
		ip->ip_dst.s_addr = htonl(INADDR_CARP_GROUP);

		ch_ptr = (struct carp_header *)(&ip[1]);
		bcopy(&ch, ch_ptr, sizeof(ch));
		if (carp_prepare_ad(m, sc, ch_ptr))
			return;
		ch_ptr->carp_cksum = in_cksum_skip(m, len, sizeof(*ip));

		getmicrotime(&cifp->if_lastchange);
		cifp->if_opackets++;
		cifp->if_obytes += len;
		carpstats.carps_opackets++;

		if (ip_output(m, NULL, NULL, IP_RAWOUTPUT, &sc->sc_imo, NULL)) {
			cifp->if_oerrors++;
			if (sc->sc_sendad_errors < INT_MAX)
				sc->sc_sendad_errors++;
			if (sc->sc_sendad_errors == CARP_SENDAD_MAX_ERRORS) {
				carp_suppress_preempt++;
				if (carp_suppress_preempt == 1) {
					carp_send_ad_all();
				}
			}
			sc->sc_sendad_success = 0;
		} else {
			if (sc->sc_sendad_errors >= CARP_SENDAD_MAX_ERRORS) {
				if (++sc->sc_sendad_success >=
				    CARP_SENDAD_MIN_SUCCESS) {
					carp_suppress_preempt--;
					sc->sc_sendad_errors = 0;
				}
			} else {
				sc->sc_sendad_errors = 0;
			}
		}
	}
#endif /* INET */
#ifdef INET6
	if (sc->sc_ia6) {
		struct ip6_hdr *ip6;

		MGETHDR(m, MB_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			cifp->if_oerrors++;
			carpstats.carps_onomem++;
			/* XXX maybe less ? */
			if (advbase != 255 || advskew != 255)
				callout_reset(&sc->sc_ad_tmo, tvtohz_high(&tv),
				    carp_send_ad_timeout, sc);
			return;
		}
		len = sizeof(*ip6) + sizeof(ch);
		m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = NULL;
		m->m_len = len;
		MH_ALIGN(m, m->m_len);
		m->m_flags |= M_MCAST;
		ip6 = mtod(m, struct ip6_hdr *);
		bzero(ip6, sizeof(*ip6));
		ip6->ip6_vfc |= IPV6_VERSION;
		ip6->ip6_hlim = CARP_DFLTTL;
		ip6->ip6_nxt = IPPROTO_CARP;
		bcopy(&sc->sc_ia6->ia_addr.sin6_addr, &ip6->ip6_src,
		    sizeof(struct in6_addr));
		/* set the multicast destination */

		ip6->ip6_dst.s6_addr16[0] = htons(0xff02);
		ip6->ip6_dst.s6_addr8[15] = 0x12;
		if (in6_setscope(&ip6->ip6_dst, sc->sc_carpdev, NULL) != 0) {
			cifp->if_oerrors++;
			m_freem(m);
			CARP_LOG("%s: in6_setscope failed\n", __func__);
			return;
		}

		ch_ptr = (struct carp_header *)(&ip6[1]);
		bcopy(&ch, ch_ptr, sizeof(ch));
		if (carp_prepare_ad(m, sc, ch_ptr))
			return;
		ch_ptr->carp_cksum = in_cksum_skip(m, len, sizeof(*ip6));

		getmicrotime(&cifp->if_lastchange);
		cifp->if_opackets++;
		cifp->if_obytes += len;
		carpstats.carps_opackets6++;

		if (ip6_output(m, NULL, NULL, 0, &sc->sc_im6o, NULL, NULL)) {
			cifp->if_oerrors++;
			if (sc->sc_sendad_errors < INT_MAX)
				sc->sc_sendad_errors++;
			if (sc->sc_sendad_errors == CARP_SENDAD_MAX_ERRORS) {
				carp_suppress_preempt++;
				if (carp_suppress_preempt == 1) {
					carp_send_ad_all();
				}
			}
			sc->sc_sendad_success = 0;
		} else {
			if (sc->sc_sendad_errors >= CARP_SENDAD_MAX_ERRORS) {
				if (++sc->sc_sendad_success >=
				    CARP_SENDAD_MIN_SUCCESS) {
					carp_suppress_preempt--;
					sc->sc_sendad_errors = 0;
				}
			} else {
				sc->sc_sendad_errors = 0;
			}
		}
	}
#endif /* INET6 */

	if (advbase != 255 || advskew != 255)
		callout_reset(&sc->sc_ad_tmo, tvtohz_high(&tv),
		    carp_send_ad_timeout, sc);
}

/*
 * Broadcast a gratuitous ARP request containing
 * the virtual router MAC address for each IP address
 * associated with the virtual router.
 */
static void
carp_send_arp(struct carp_softc *sc)
{
	const struct carp_vhaddr *vha;

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		if (vha->vha_iaback == NULL)
			continue;

		arp_iainit(sc->sc_carpdev, &vha->vha_ia->ia_addr.sin_addr,
			   IF_LLADDR(&sc->sc_if));
	}
}

#ifdef INET6
static void
carp_send_na(struct carp_softc *sc)
{
	struct ifaddr_container *ifac;
	struct in6_addr *in6;
	static struct in6_addr mcast = IN6ADDR_LINKLOCAL_ALLNODES_INIT;

	TAILQ_FOREACH(ifac, &sc->sc_if.if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		in6 = &ifatoia6(ifa)->ia_addr.sin6_addr;
		nd6_na_output(sc->sc_carpdev, &mcast, in6,
		    ND_NA_FLAG_OVERRIDE, 1, NULL);
		DELAY(1000);	/* XXX */
	}
}
#endif /* INET6 */

static __inline const struct carp_vhaddr *
carp_find_addr(const struct carp_softc *sc, const struct in_addr *addr)
{
	struct carp_vhaddr *vha;

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		if (vha->vha_iaback == NULL)
			continue;

		if (vha->vha_ia->ia_addr.sin_addr.s_addr == addr->s_addr)
			return vha;
	}
	return NULL;
}

static int
carp_iamatch_balance(const struct carp_if *cif, const struct in_addr *itaddr,
		     const struct in_addr *isaddr, uint8_t **enaddr)
{
	const struct carp_softc *vh;
	int index, count = 0;

	/*
	 * XXX proof of concept implementation.
	 * We use the source ip to decide which virtual host should
	 * handle the request. If we're master of that virtual host,
	 * then we respond, otherwise, just drop the arp packet on
	 * the floor.
	 */

	TAILQ_FOREACH(vh, &cif->vhif_vrs, sc_list) {
		if (!CARP_IS_RUNNING(&vh->sc_if))
			continue;

		if (carp_find_addr(vh, itaddr) != NULL)
			count++;
	}
	if (count == 0)
		return 0;

	/* this should be a hash, like pf_hash() */
	index = ntohl(isaddr->s_addr) % count;
	count = 0;

	TAILQ_FOREACH(vh, &cif->vhif_vrs, sc_list) {
		if (!CARP_IS_RUNNING(&vh->sc_if))
			continue;

		if (carp_find_addr(vh, itaddr) == NULL)
			continue;

		if (count == index) {
			if (vh->sc_state == MASTER) {
				*enaddr = IF_LLADDR(&vh->sc_if);
				return 1;
			} else {
				return 0;
			}
		}
		count++;
	}
	return 0;
}

int
carp_iamatch(const void *v, const struct in_addr *itaddr,
	     const struct in_addr *isaddr, uint8_t **enaddr)
{
	const struct carp_if *cif = v;
	const struct carp_softc *vh;

	if (carp_opts[CARPCTL_ARPBALANCE])
		return carp_iamatch_balance(cif, itaddr, isaddr, enaddr);

	TAILQ_FOREACH(vh, &cif->vhif_vrs, sc_list) {
		if (!CARP_IS_RUNNING(&vh->sc_if) || vh->sc_state != MASTER)
			continue;

		if (carp_find_addr(vh, itaddr) != NULL) {
			*enaddr = IF_LLADDR(&vh->sc_if);
			return 1;
		}
	}
	return 0;
}

#ifdef INET6
struct ifaddr *
carp_iamatch6(void *v, struct in6_addr *taddr)
{
	struct carp_if *cif = v;
	struct carp_softc *vh;

	TAILQ_FOREACH(vh, &cif->vhif_vrs, sc_list) {
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &vh->sc_if.if_addrheads[mycpuid],
			      ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (IN6_ARE_ADDR_EQUAL(taddr,
			    &ifatoia6(ifa)->ia_addr.sin6_addr) &&
			    CARP_IS_RUNNING(&vh->sc_if) &&
			    vh->sc_state == MASTER) {
				return (ifa);
			}
		}
	}
	return (NULL);
}

void *
carp_macmatch6(void *v, struct mbuf *m, const struct in6_addr *taddr)
{
	struct m_tag *mtag;
	struct carp_if *cif = v;
	struct carp_softc *sc;

	TAILQ_FOREACH(sc, &cif->vhif_vrs, sc_list) {
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &sc->sc_if.if_addrheads[mycpuid],
			      ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (IN6_ARE_ADDR_EQUAL(taddr,
			    &ifatoia6(ifa)->ia_addr.sin6_addr) &&
			    CARP_IS_RUNNING(&sc->sc_if)) {
				struct ifnet *ifp = &sc->sc_if;

				mtag = m_tag_get(PACKET_TAG_CARP,
				    sizeof(struct ifnet *), MB_DONTWAIT);
				if (mtag == NULL) {
					/* better a bit than nothing */
					return (IF_LLADDR(ifp));
				}
				bcopy(&ifp, (caddr_t)(mtag + 1),
				    sizeof(struct ifnet *));
				m_tag_prepend(m, mtag);

				return (IF_LLADDR(ifp));
			}
		}
	}
	return (NULL);
}
#endif

int
carp_forus(const void *v, const void *dhost)
{
	const struct carp_if *cif = v;
	const struct carp_softc *vh;
	const uint8_t *ena = dhost;

	if (ena[0] || ena[1] || ena[2] != 0x5e || ena[3] || ena[4] != 1)
		return 0;

	TAILQ_FOREACH(vh, &cif->vhif_vrs, sc_list) {
		const struct ifnet *cifp = &vh->sc_if;

		if (CARP_IS_RUNNING(cifp) && vh->sc_state == MASTER &&
		    !bcmp(dhost, IF_LLADDR(cifp), ETHER_ADDR_LEN))
			return 1;
	}
	return 0;
}

static void
carp_master_down_timeout(void *xsc)
{
	struct carp_softc *sc = xsc;

	CARP_DEBUG("%s: BACKUP -> MASTER (master timed out)\n",
		   sc->sc_if.if_xname);
	carp_master_down(sc);
}

static void
carp_master_down(struct carp_softc *sc)
{
	switch (sc->sc_state) {
	case INIT:
		kprintf("%s: master_down event in INIT state\n",
			sc->sc_if.if_xname);
		break;

	case MASTER:
		break;

	case BACKUP:
		carp_set_state(sc, MASTER);
		carp_send_ad(sc);
		carp_send_arp(sc);
#ifdef INET6
		carp_send_na(sc);
#endif /* INET6 */
		carp_setrun(sc, 0);
		carp_setroute(sc, RTM_ADD);
		break;
	}
}

/*
 * When in backup state, af indicates whether to reset the master down timer
 * for v4 or v6. If it's set to zero, reset the ones which are already pending.
 */
static void
carp_setrun(struct carp_softc *sc, sa_family_t af)
{
	struct ifnet *cifp = &sc->sc_if;
	struct timeval tv;

	if (sc->sc_carpdev == NULL) {
		carp_set_state(sc, INIT);
		return;
	}

	if ((cifp->if_flags & IFF_RUNNING) && sc->sc_vhid > 0 &&
	    (sc->sc_naddrs || sc->sc_naddrs6)) {
		/* Nothing */
	} else {
		carp_setroute(sc, RTM_DELETE);
		return;
	}

	switch (sc->sc_state) {
	case INIT:
		if (carp_opts[CARPCTL_PREEMPT] && !carp_suppress_preempt) {
			carp_send_ad(sc);
			carp_send_arp(sc);
#ifdef INET6
			carp_send_na(sc);
#endif /* INET6 */
			CARP_DEBUG("%s: INIT -> MASTER (preempting)\n",
				   cifp->if_xname);
			carp_set_state(sc, MASTER);
			carp_setroute(sc, RTM_ADD);
		} else {
			CARP_DEBUG("%s: INIT -> BACKUP\n", cifp->if_xname);
			carp_set_state(sc, BACKUP);
			carp_setroute(sc, RTM_DELETE);
			carp_setrun(sc, 0);
		}
		break;

	case BACKUP:
		callout_stop(&sc->sc_ad_tmo);
		tv.tv_sec = 3 * sc->sc_advbase;
		tv.tv_usec = sc->sc_advskew * 1000000 / 256;
		switch (af) {
#ifdef INET
		case AF_INET:
			callout_reset(&sc->sc_md_tmo, tvtohz_high(&tv),
			    carp_master_down_timeout, sc);
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			callout_reset(&sc->sc_md6_tmo, tvtohz_high(&tv),
			    carp_master_down_timeout, sc);
			break;
#endif /* INET6 */
		default:
			if (sc->sc_naddrs)
				callout_reset(&sc->sc_md_tmo, tvtohz_high(&tv),
				    carp_master_down_timeout, sc);
			if (sc->sc_naddrs6)
				callout_reset(&sc->sc_md6_tmo, tvtohz_high(&tv),
				    carp_master_down_timeout, sc);
			break;
		}
		break;

	case MASTER:
		tv.tv_sec = sc->sc_advbase;
		tv.tv_usec = sc->sc_advskew * 1000000 / 256;
		callout_reset(&sc->sc_ad_tmo, tvtohz_high(&tv),
		    carp_send_ad_timeout, sc);
		break;
	}
}

static void
carp_multicast_cleanup(struct carp_softc *sc)
{
	struct ip_moptions *imo = &sc->sc_imo;

	if (imo->imo_num_memberships == 0)
		return;
	KKASSERT(imo->imo_num_memberships == 1);

	in_delmulti(imo->imo_membership[0]);
	imo->imo_membership[0] = NULL;
	imo->imo_num_memberships = 0;
	imo->imo_multicast_ifp = NULL;
}

#ifdef INET6
static void
carp_multicast6_cleanup(struct carp_softc *sc)
{
	struct ip6_moptions *im6o = &sc->sc_im6o;

	while (!LIST_EMPTY(&im6o->im6o_memberships)) {
		struct in6_multi_mship *imm =
		    LIST_FIRST(&im6o->im6o_memberships);

		LIST_REMOVE(imm, i6mm_chain);
		in6_leavegroup(imm);
	}
	im6o->im6o_multicast_ifp = NULL;
}
#endif

static int
carp_get_vhaddr(struct carp_softc *sc, struct ifdrv *ifd)
{
	const struct carp_vhaddr *vha;
	struct ifcarpvhaddr *carpa, *carpa0;
	int count, len, error;

	count = 0;
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
		++count;

	if (ifd->ifd_len == 0) {
		ifd->ifd_len = count * sizeof(*carpa);
		return 0;
	} else if (count == 0 || ifd->ifd_len < sizeof(*carpa)) {
		ifd->ifd_len = 0;
		return 0;
	}
	len = min(ifd->ifd_len, sizeof(*carpa) * count);
	KKASSERT(len >= sizeof(*carpa));

	carpa0 = carpa = kmalloc(len, M_TEMP, M_WAITOK | M_NULLOK | M_ZERO);
	if (carpa == NULL)
		return ENOMEM;

	count = 0;
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		if (len < sizeof(*carpa))
			break;

		carpa->carpa_flags = vha->vha_flags;
		carpa->carpa_addr.sin_family = AF_INET;
		carpa->carpa_addr.sin_addr = vha->vha_ia->ia_addr.sin_addr;

		carpa->carpa_baddr.sin_family = AF_INET;
		if (vha->vha_iaback == NULL) {
			carpa->carpa_baddr.sin_addr.s_addr = INADDR_ANY;
		} else {
			carpa->carpa_baddr.sin_addr =
			vha->vha_iaback->ia_addr.sin_addr;
		}

		++carpa;
		++count;
		len -= sizeof(*carpa);
	}
	ifd->ifd_len = sizeof(*carpa) * count;
	KKASSERT(ifd->ifd_len > 0);

	error = copyout(carpa0, ifd->ifd_data, ifd->ifd_len);
	kfree(carpa0, M_TEMP);
	return error;
}

static int
carp_config_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha)
{
	struct ifnet *ifp;
	struct in_ifaddr *ia_if;
	struct in_ifaddr_container *iac;
	const struct sockaddr_in *sin;
	u_long iaddr;
	int own;

	KKASSERT(vha->vha_ia != NULL);

	sin = &vha->vha_ia->ia_addr;
	iaddr = ntohl(sin->sin_addr.s_addr);

	ia_if = NULL;
	own = 0;
	TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
		struct in_ifaddr *ia = iac->ia;

		if ((ia->ia_flags & IFA_ROUTE) == 0)
			continue;

		if (ia->ia_ifp->if_type == IFT_CARP)
			continue;

		/* and, yeah, we need a multicast-capable iface too */
		if ((ia->ia_ifp->if_flags & IFF_MULTICAST) == 0)
			continue;

		if ((iaddr & ia->ia_subnetmask) == ia->ia_subnet) {
			if (sin->sin_addr.s_addr ==
			    ia->ia_addr.sin_addr.s_addr)
				own = 1;
			if (ia_if == NULL)
				ia_if = ia;
			else if (sc->sc_carpdev != NULL &&
				 sc->sc_carpdev == ia->ia_ifp)
				ia_if = ia;
		}
	}

	carp_deactivate_vhaddr(sc, vha);
	if (!ia_if)
		return ENOENT;

	ifp = ia_if->ia_ifp;

	/* XXX Don't allow parent iface to be changed */
	if (sc->sc_carpdev != NULL && sc->sc_carpdev != ifp)
		return EEXIST;

	return carp_activate_vhaddr(sc, vha, ifp, ia_if, own);
}

static void
carp_add_addr(struct carp_softc *sc, struct ifaddr *carp_ifa)
{
	struct carp_vhaddr *vha_new;
	struct in_ifaddr *carp_ia;
#ifdef INVARIANTS
	struct carp_vhaddr *vha;
#endif

	KKASSERT(carp_ifa->ifa_addr->sa_family == AF_INET);
	carp_ia = ifatoia(carp_ifa);

#ifdef INVARIANTS
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
		KKASSERT(vha->vha_ia != NULL && vha->vha_ia != carp_ia);
#endif

	vha_new = kmalloc(sizeof(*vha_new), M_CARP, M_WAITOK | M_ZERO);
	vha_new->vha_ia = carp_ia;
	carp_insert_vhaddr(sc, vha_new);

	if (carp_config_vhaddr(sc, vha_new) != 0) {
		/*
		 * If the above configuration fails, it may only mean
		 * that the new address is problematic.  However, the
		 * carp(4) interface may already have several working
		 * addresses.  Since the expected behaviour of
		 * SIOC[AS]IFADDR is to put the NIC into working state,
		 * we try starting the state machine manually here with
		 * the hope that the carp(4)'s previously working
		 * addresses still could be brought up.
		 */
		carp_hmac_prepare(sc);
		carp_set_state(sc, INIT);
		carp_setrun(sc, 0);
	}
}

static void
carp_del_addr(struct carp_softc *sc, struct ifaddr *carp_ifa)
{
	struct carp_vhaddr *vha;
	struct in_ifaddr *carp_ia;

	KKASSERT(carp_ifa->ifa_addr->sa_family == AF_INET);
	carp_ia = ifatoia(carp_ifa);

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		KKASSERT(vha->vha_ia != NULL);
		if (vha->vha_ia == carp_ia)
			break;
	}
	KASSERT(vha != NULL, ("no corresponding vhaddr %p\n", carp_ifa));

	/*
	 * Remove the vhaddr from the list before deactivating
	 * the vhaddr, so that the HMAC could be correctly
	 * updated in carp_deactivate_vhaddr()
	 */
	carp_remove_vhaddr(sc, vha);

	carp_deactivate_vhaddr(sc, vha);
	kfree(vha, M_CARP);
}

static void
carp_config_addr(struct carp_softc *sc, struct ifaddr *carp_ifa)
{
	struct carp_vhaddr *vha;
	struct in_ifaddr *carp_ia;

	KKASSERT(carp_ifa->ifa_addr->sa_family == AF_INET);
	carp_ia = ifatoia(carp_ifa);

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		KKASSERT(vha->vha_ia != NULL);
		if (vha->vha_ia == carp_ia)
			break;
	}
	KASSERT(vha != NULL, ("no corresponding vhaddr %p\n", carp_ifa));

	/* Remove then reinsert, to keep the vhaddr list sorted */
	carp_remove_vhaddr(sc, vha);
	carp_insert_vhaddr(sc, vha);

	if (carp_config_vhaddr(sc, vha) != 0) {
		/* See the comment in carp_add_addr() */
		carp_hmac_prepare(sc);
		carp_set_state(sc, INIT);
		carp_setrun(sc, 0);
	}
}

#ifdef INET6
static int
carp_set_addr6(struct carp_softc *sc, struct sockaddr_in6 *sin6)
{
	struct ifnet *ifp;
	struct carp_if *cif;
	struct in6_ifaddr *ia, *ia_if;
	struct ip6_moptions *im6o = &sc->sc_im6o;
	struct in6_multi_mship *imm;
	struct in6_addr in6;
	int own, error;

	if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
		carp_setrun(sc, 0);
		return (0);
	}

	/* we have to do it by hands to check we won't match on us */
	ia_if = NULL; own = 0;
	for (ia = in6_ifaddr; ia; ia = ia->ia_next) {
		int i;

		for (i = 0; i < 4; i++) {
			if ((sin6->sin6_addr.s6_addr32[i] &
			    ia->ia_prefixmask.sin6_addr.s6_addr32[i]) !=
			    (ia->ia_addr.sin6_addr.s6_addr32[i] &
			    ia->ia_prefixmask.sin6_addr.s6_addr32[i]))
				break;
		}
		/* and, yeah, we need a multicast-capable iface too */
		if (ia->ia_ifp != &sc->sc_if &&
		    (ia->ia_ifp->if_flags & IFF_MULTICAST) &&
		    (i == 4)) {
			if (!ia_if)
				ia_if = ia;
			if (IN6_ARE_ADDR_EQUAL(&sin6->sin6_addr,
			    &ia->ia_addr.sin6_addr))
				own++;
		}
	}

	if (!ia_if)
		return (EADDRNOTAVAIL);
	ia = ia_if;
	ifp = ia->ia_ifp;

	if (ifp == NULL || (ifp->if_flags & IFF_MULTICAST) == 0 ||
	    (im6o->im6o_multicast_ifp && im6o->im6o_multicast_ifp != ifp))
		return (EADDRNOTAVAIL);

	if (!sc->sc_naddrs6) {
		im6o->im6o_multicast_ifp = ifp;

		/* join CARP multicast address */
		bzero(&in6, sizeof(in6));
		in6.s6_addr16[0] = htons(0xff02);
		in6.s6_addr8[15] = 0x12;
		if (in6_setscope(&in6, ifp, NULL) != 0)
			goto cleanup;
		if ((imm = in6_joingroup(ifp, &in6, &error)) == NULL)
			goto cleanup;
		LIST_INSERT_HEAD(&im6o->im6o_memberships, imm, i6mm_chain);

		/* join solicited multicast address */
		bzero(&in6, sizeof(in6));
		in6.s6_addr16[0] = htons(0xff02);
		in6.s6_addr32[1] = 0;
		in6.s6_addr32[2] = htonl(1);
		in6.s6_addr32[3] = sin6->sin6_addr.s6_addr32[3];
		in6.s6_addr8[12] = 0xff;
		if (in6_setscope(&in6, ifp, NULL) != 0)
			goto cleanup;
		if ((imm = in6_joingroup(ifp, &in6, &error)) == NULL)
			goto cleanup;
		LIST_INSERT_HEAD(&im6o->im6o_memberships, imm, i6mm_chain);
	}

	if (!ifp->if_carp) {
		cif = kmalloc(sizeof(*cif), M_CARP, M_WAITOK | M_ZERO);

		if ((error = ifpromisc(ifp, 1))) {
			kfree(cif, M_CARP);
			goto cleanup;
		}

		TAILQ_INIT(&cif->vhif_vrs);
		ifp->if_carp = cif;
	} else {
		struct carp_softc *vr;

		cif = ifp->if_carp;
		TAILQ_FOREACH(vr, &cif->vhif_vrs, sc_list) {
			if (vr != sc && vr->sc_vhid == sc->sc_vhid) {
				error = EINVAL;
				goto cleanup;
			}
		}
	}
	sc->sc_ia6 = ia;
	sc->sc_carpdev = ifp;

	{ /* XXX prevent endless loop if already in queue */
	struct carp_softc *vr, *after = NULL;
	int myself = 0;
	cif = ifp->if_carp;

	TAILQ_FOREACH(vr, &cif->vhif_vrs, sc_list) {
		if (vr == sc)
			myself = 1;
		if (vr->sc_vhid < sc->sc_vhid)
			after = vr;
	}

	if (!myself) {
		/* We're trying to keep things in order */
		if (after == NULL)
			TAILQ_INSERT_TAIL(&cif->vhif_vrs, sc, sc_list);
		else
			TAILQ_INSERT_AFTER(&cif->vhif_vrs, after, sc, sc_list);
	}
	}

	sc->sc_naddrs6++;
	if (own)
		sc->sc_advskew = 0;
	carp_sc_state(sc);
	carp_setrun(sc, 0);

	return (0);

cleanup:
	/* clean up multicast memberships */
	if (!sc->sc_naddrs6) {
		while (!LIST_EMPTY(&im6o->im6o_memberships)) {
			imm = LIST_FIRST(&im6o->im6o_memberships);
			LIST_REMOVE(imm, i6mm_chain);
			in6_leavegroup(imm);
		}
	}
	return (error);
}

static int
carp_del_addr6(struct carp_softc *sc, struct sockaddr_in6 *sin6)
{
	int error = 0;

	if (!--sc->sc_naddrs6) {
		struct carp_if *cif = sc->sc_carpdev->if_carp;
		struct ip6_moptions *im6o = &sc->sc_im6o;

		callout_stop(&sc->sc_ad_tmo);
		sc->sc_vhid = -1;
		while (!LIST_EMPTY(&im6o->im6o_memberships)) {
			struct in6_multi_mship *imm =
			    LIST_FIRST(&im6o->im6o_memberships);

			LIST_REMOVE(imm, i6mm_chain);
			in6_leavegroup(imm);
		}
		im6o->im6o_multicast_ifp = NULL;
		TAILQ_REMOVE(&cif->vhif_vrs, sc, sc_list);
		if (TAILQ_EMPTY(&cif->vhif_vrs)) {
			sc->sc_carpdev->if_carp = NULL;
			kfree(cif, M_IFADDR);
		}
	}
	return (error);
}
#endif /* INET6 */

static int
carp_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr, struct ucred *cr)
{
	struct carp_softc *sc = ifp->if_softc, *vr;
	struct carpreq carpr;
	struct ifaddr *ifa;
	struct ifreq *ifr;
	struct ifaliasreq *ifra;
	struct ifdrv *ifd;
	char devname[IFNAMSIZ];
	int error = 0;

	ifa = (struct ifaddr *)addr;
	ifra = (struct ifaliasreq *)addr;
	ifr = (struct ifreq *)addr;
	ifd = (struct ifdrv *)addr;

	switch (cmd) {
	case SIOCSIFADDR:
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			ifp->if_flags |= IFF_UP | IFF_RUNNING;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			ifp->if_flags |= IFF_UP | IFF_RUNNING;
			error = carp_set_addr6(sc, satosin6(ifa->ifa_addr));
			break;
#endif /* INET6 */
		default:
			error = EAFNOSUPPORT;
			break;
		}
		break;

	case SIOCAIFADDR:
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			panic("SIOCAIFADDR should never be seen\n");
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			ifp->if_flags |= IFF_UP | IFF_RUNNING;
			error = carp_set_addr6(sc, satosin6(&ifra->ifra_addr));
			break;
#endif /* INET6 */
		default:
			error = EAFNOSUPPORT;
			break;
		}
		break;

	case SIOCDIFADDR:
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			panic("SIOCDIFADDR should never be seen\n");
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			error = carp_del_addr6(sc, satosin6(&ifra->ifra_addr));
			break;
#endif /* INET6 */
		default:
			error = EAFNOSUPPORT;
			break;
		}
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0) {
				ifp->if_flags |= IFF_RUNNING;
				carp_set_state(sc, INIT);
				carp_setrun(sc, 0);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			carp_stop(sc, 0);
		}
		break;

	case SIOCSVH:
		error = priv_check_cred(cr, PRIV_ROOT, NULL_CRED_OKAY);
		if (error)
			break;
		error = copyin(ifr->ifr_data, &carpr, sizeof(carpr));
		if (error)
			break;

		error = 1;
		if ((ifp->if_flags & IFF_RUNNING) &&
		    sc->sc_state != INIT && carpr.carpr_state != sc->sc_state) {
			switch (carpr.carpr_state) {
			case BACKUP:
				callout_stop(&sc->sc_ad_tmo);
				carp_set_state(sc, BACKUP);
				carp_setrun(sc, 0);
				carp_setroute(sc, RTM_DELETE);
				break;

			case MASTER:
				carp_master_down(sc);
				break;

			default:
				break;
			}
		}
		if (carpr.carpr_vhid > 0) {
			if (carpr.carpr_vhid > 255) {
				error = EINVAL;
				break;
			}
			if (sc->sc_carpdev) {
				struct carp_if *cif = sc->sc_carpdev->if_carp;

				TAILQ_FOREACH(vr, &cif->vhif_vrs, sc_list) {
					if (vr != sc &&
					    vr->sc_vhid == carpr.carpr_vhid)
						return EEXIST;
				}
			}
			sc->sc_vhid = carpr.carpr_vhid;
			IF_LLADDR(ifp)[0] = 0;
			IF_LLADDR(ifp)[1] = 0;
			IF_LLADDR(ifp)[2] = 0x5e;
			IF_LLADDR(ifp)[3] = 0;
			IF_LLADDR(ifp)[4] = 1;
			IF_LLADDR(ifp)[5] = sc->sc_vhid;
			error--;
		}
		if (carpr.carpr_advbase > 0 || carpr.carpr_advskew > 0) {
			if (carpr.carpr_advskew >= 255) {
				error = EINVAL;
				break;
			}
			if (carpr.carpr_advbase > 255) {
				error = EINVAL;
				break;
			}
			sc->sc_advbase = carpr.carpr_advbase;
			sc->sc_advskew = carpr.carpr_advskew;
			error--;
		}
		bcopy(carpr.carpr_key, sc->sc_key, sizeof(sc->sc_key));
		if (error > 0) {
			error = EINVAL;
		} else {
			error = 0;
			carp_setrun(sc, 0);
		}
		break;

	case SIOCGVH:
		bzero(&carpr, sizeof(carpr));
		carpr.carpr_state = sc->sc_state;
		carpr.carpr_vhid = sc->sc_vhid;
		carpr.carpr_advbase = sc->sc_advbase;
		carpr.carpr_advskew = sc->sc_advskew;
		error = priv_check_cred(cr, PRIV_ROOT, NULL_CRED_OKAY);
		if (error == 0) {
			bcopy(sc->sc_key, carpr.carpr_key,
			      sizeof(carpr.carpr_key));
		}

		error = copyout(&carpr, ifr->ifr_data, sizeof(carpr));
		break;

	case SIOCGDRVSPEC:
		switch (ifd->ifd_cmd) {
		case CARPGDEVNAME:
			if (ifd->ifd_len != sizeof(devname))
				error = EINVAL;
			break;

		case CARPGVHADDR:
			break;

		default:
			error = EINVAL;
			break;
		}
		if (error)
			break;

		switch (ifd->ifd_cmd) {
		case CARPGVHADDR:
			error = carp_get_vhaddr(sc, ifd);
			break;

		case CARPGDEVNAME:
			bzero(devname, sizeof(devname));
			if (sc->sc_carpdev != NULL) {
				strlcpy(devname, sc->sc_carpdev->if_xname,
					sizeof(devname));
			}
			error = copyout(devname, ifd->ifd_data,
					sizeof(devname));
			break;
		}
		break;

	default:
		error = EINVAL;
		break;
	}
	carp_hmac_prepare(sc);
	return error;
}

/*
 * XXX: this is looutput. We should eventually use it from there.
 */
static int
carp_looutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	uint32_t af;

	M_ASSERTPKTHDR(m); /* check if we have the packet header */

	if (rt && rt->rt_flags & (RTF_REJECT|RTF_BLACKHOLE)) {
		m_freem(m);
		return (rt->rt_flags & RTF_BLACKHOLE ? 0 :
			rt->rt_flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
	}

	ifp->if_opackets++;
	ifp->if_obytes += m->m_pkthdr.len;

	/* BPF writes need to be handled specially. */
	if (dst->sa_family == AF_UNSPEC) {
		bcopy(dst->sa_data, &af, sizeof(af));
		dst->sa_family = af;
	}

#if 1	/* XXX */
	switch (dst->sa_family) {
	case AF_INET:
	case AF_INET6:
	case AF_IPX:
	case AF_APPLETALK:
		break;

	default:
		m_freem(m);
		return (EAFNOSUPPORT);
	}
#endif
	return (if_simloop(ifp, m, dst->sa_family, 0));
}

/*
 * Start output on carp interface. This function should never be called.
 */
static void
carp_start(struct ifnet *ifp)
{
#ifdef DEBUG
	kprintf("%s: start called\n", ifp->if_xname);
#endif
}

int
carp_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *sa,
    struct rtentry *rt)
{
	struct m_tag *mtag;
	struct carp_softc *sc;
	struct ifnet *carp_ifp;
	struct ether_header *eh;

	if (!sa)
		return (0);

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		break;
#endif /* INET6 */
	default:
		return (0);
	}

	mtag = m_tag_find(m, PACKET_TAG_CARP, NULL);
	if (mtag == NULL)
		return (0);

	bcopy(mtag + 1, &carp_ifp, sizeof(struct ifnet *));
	sc = carp_ifp->if_softc;

	/* Set the source MAC address to Virtual Router MAC Address */
	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_L2VLAN:
		eh = mtod(m, struct ether_header *);
		eh->ether_shost[0] = 0;
		eh->ether_shost[1] = 0;
		eh->ether_shost[2] = 0x5e;
		eh->ether_shost[3] = 0;
		eh->ether_shost[4] = 1;
		eh->ether_shost[5] = sc->sc_vhid;
		break;

	default:
		if_printf(ifp, "carp is not supported for this "
			  "interface type\n");
		return (EOPNOTSUPP);
	}
	return (0);
}

static void
carp_set_state(struct carp_softc *sc, int state)
{
	struct ifnet *cifp = &sc->sc_if;

	if (sc->sc_state == state)
		return;
	sc->sc_state = state;

	switch (sc->sc_state) {
	case BACKUP:
		cifp->if_link_state = LINK_STATE_DOWN;
		break;

	case MASTER:
		cifp->if_link_state = LINK_STATE_UP;
		break;

	default:
		cifp->if_link_state = LINK_STATE_UNKNOWN;
		break;
	}
	rt_ifmsg(cifp);
}

void
carp_group_demote_adj(struct ifnet *ifp, int adj)
{
	struct ifg_list	*ifgl;
	int *dm;
	
	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next) {
		if (!strcmp(ifgl->ifgl_group->ifg_group, IFG_ALL))
			continue;
		dm = &ifgl->ifgl_group->ifg_carp_demoted;

		if (*dm + adj >= 0)
			*dm += adj;
		else
			*dm = 0;

		if (adj > 0 && *dm == 1)
			carp_send_ad_all();
		CARP_LOG("%s demoted group %s to %d", ifp->if_xname,
                    ifgl->ifgl_group->ifg_group, *dm);
	}
}

void
carp_carpdev_state(void *v)
{
	struct carp_if *cif = v;
	struct carp_softc *sc;

	TAILQ_FOREACH(sc, &cif->vhif_vrs, sc_list)
		carp_sc_state(sc);
}

static void
carp_sc_state(struct carp_softc *sc)
{
	if (!(sc->sc_carpdev->if_flags & IFF_UP)) {
		callout_stop(&sc->sc_ad_tmo);
		callout_stop(&sc->sc_md_tmo);
		callout_stop(&sc->sc_md6_tmo);
		carp_set_state(sc, INIT);
		carp_setrun(sc, 0);
		if (!sc->sc_suppress) {
			carp_suppress_preempt++;
			if (carp_suppress_preempt == 1)
				carp_send_ad_all();
		}
		sc->sc_suppress = 1;
	} else {
		carp_set_state(sc, INIT);
		carp_setrun(sc, 0);
		if (sc->sc_suppress)
			carp_suppress_preempt--;
		sc->sc_suppress = 0;
	}
}

static void
carp_stop(struct carp_softc *sc, int detach)
{
	sc->sc_if.if_flags &= ~IFF_RUNNING;

	callout_stop(&sc->sc_ad_tmo);
	callout_stop(&sc->sc_md_tmo);
	callout_stop(&sc->sc_md6_tmo);

	if (!detach && sc->sc_state == MASTER)
		carp_send_ad(sc);

	if (sc->sc_suppress)
		carp_suppress_preempt--;
	sc->sc_suppress = 0;

	if (sc->sc_sendad_errors >= CARP_SENDAD_MAX_ERRORS)
		carp_suppress_preempt--;
	sc->sc_sendad_errors = 0;
	sc->sc_sendad_success = 0;

	carp_set_state(sc, INIT);
	carp_setrun(sc, 0);
}

static void
carp_reset(struct carp_softc *sc, int detach)
{
	struct ifnet *cifp = &sc->sc_if;

	carp_stop(sc, detach);
	if (!sc->sc_dead && (cifp->if_flags & IFF_UP))
		cifp->if_flags |= IFF_RUNNING;
}

static int
carp_activate_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha,
		     struct ifnet *ifp, const struct in_ifaddr *ia_if, int own)
{
	struct ip_moptions *imo = &sc->sc_imo;
	struct carp_if *cif;
	struct carp_softc *vr, *after = NULL;
	int onlist, error;
#ifdef INVARIANTS
	int assert_onlist;
#endif

	KKASSERT(vha->vha_ia != NULL);

	KASSERT(ia_if != NULL, ("NULL backing address\n"));
	KASSERT(vha->vha_iaback == NULL, ("%p is already activated\n", vha));
	KASSERT((vha->vha_flags & CARP_VHAF_OWNER) == 0,
		("inactive vhaddr %p is the address owner\n", vha));

	KASSERT(sc->sc_carpdev == NULL || sc->sc_carpdev == ifp,
		("%s is already on %s\n", sc->sc_if.if_xname,
		 sc->sc_carpdev->if_xname));

	KASSERT(imo->imo_multicast_ifp == NULL ||
		imo->imo_multicast_ifp == ifp,
		("%s didn't leave mcast group on %s\n",
		 sc->sc_if.if_xname, imo->imo_multicast_ifp->if_xname));

	if (imo->imo_num_memberships == 0) {
		struct in_addr addr;

		addr.s_addr = htonl(INADDR_CARP_GROUP);
		if ((imo->imo_membership[0] = in_addmulti(&addr, ifp)) == NULL)
			return ENOBUFS;
		imo->imo_num_memberships++;
		imo->imo_multicast_ifp = ifp;
		imo->imo_multicast_ttl = CARP_DFLTTL;
		imo->imo_multicast_loop = 0;
	}

	if (!ifp->if_carp) {
		KASSERT(sc->sc_carpdev == NULL,
			("%s is already on %s\n", sc->sc_if.if_xname,
			 sc->sc_carpdev->if_xname));

		cif = kmalloc(sizeof(*cif), M_CARP, M_WAITOK | M_ZERO);

		error = ifpromisc(ifp, 1);
		if (error) {
			kfree(cif, M_CARP);
			goto cleanup;
		}

		TAILQ_INIT(&cif->vhif_vrs);
		ifp->if_carp = cif;
	} else {
		cif = ifp->if_carp;
		TAILQ_FOREACH(vr, &cif->vhif_vrs, sc_list) {
			if (vr != sc && vr->sc_vhid == sc->sc_vhid) {
				error = EINVAL;
				goto cleanup;
			}
		}
	}

#ifdef INVARIANTS
	if (sc->sc_carpdev != NULL)
		assert_onlist = 1;
	else
		assert_onlist = 0;
#endif
	sc->sc_ia = ia_if;
	sc->sc_carpdev = ifp;

	cif = ifp->if_carp;
	onlist = 0;
	TAILQ_FOREACH(vr, &cif->vhif_vrs, sc_list) {
		if (vr == sc)
			onlist = 1;
		if (vr->sc_vhid < sc->sc_vhid)
			after = vr;
	}

#ifdef INVARIANTS
	if (assert_onlist) {
		KASSERT(onlist, ("%s is not on %s carp list\n",
			sc->sc_if.if_xname, ifp->if_xname));
	} else {
		KASSERT(!onlist, ("%s is already on %s carp list\n",
			sc->sc_if.if_xname, ifp->if_xname));
	}
#endif

	if (!onlist) {
		/* We're trying to keep things in order */
		if (after == NULL)
			TAILQ_INSERT_TAIL(&cif->vhif_vrs, sc, sc_list);
		else
			TAILQ_INSERT_AFTER(&cif->vhif_vrs, after, sc, sc_list);
	}

	vha->vha_iaback = ia_if;
	sc->sc_naddrs++;

	if (own) {
		vha->vha_flags |= CARP_VHAF_OWNER;

		/* XXX save user configured advskew? */
		sc->sc_advskew = 0;
	}

	carp_hmac_prepare(sc);
	carp_set_state(sc, INIT);
	carp_setrun(sc, 0);
	return 0;
cleanup:
	carp_multicast_cleanup(sc);
	return error;
}

static void
carp_deactivate_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha)
{
	KKASSERT(vha->vha_ia != NULL);

	carp_hmac_prepare(sc);

	if (vha->vha_iaback == NULL) {
		KASSERT((vha->vha_flags & CARP_VHAF_OWNER) == 0,
			("inactive vhaddr %p is the address owner\n", vha));
		return;
	}

	vha->vha_flags &= ~CARP_VHAF_OWNER;

	KKASSERT(sc->sc_naddrs > 0);
	vha->vha_iaback = NULL;
	sc->sc_naddrs--;
	if (!sc->sc_naddrs) {
		if (sc->sc_naddrs6) {
			carp_multicast_cleanup(sc);
			sc->sc_ia = NULL;
		} else {
			carp_detach(sc, 0);
		}
	}
}

static void
carp_link_addrs(struct carp_softc *sc, struct ifnet *ifp, struct ifaddr *ifa_if)
{
	struct carp_vhaddr *vha;
	struct in_ifaddr *ia_if;

	KKASSERT(ifa_if->ifa_addr->sa_family == AF_INET);
	ia_if = ifatoia(ifa_if);

	if ((ia_if->ia_flags & IFA_ROUTE) == 0)
		return;

	/*
	 * Test each inactive vhaddr against the newly added address.
	 * If the newly added address could be the backing address,
	 * then activate the matching vhaddr.
	 */
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		const struct in_ifaddr *ia;
		u_long iaddr;
		int own;

		if (vha->vha_iaback != NULL)
			continue;

		ia = vha->vha_ia;
		iaddr = ntohl(ia->ia_addr.sin_addr.s_addr);

		if ((iaddr & ia_if->ia_subnetmask) != ia_if->ia_subnet)
			continue;

		own = 0;
		if (ia->ia_addr.sin_addr.s_addr ==
		    ia_if->ia_addr.sin_addr.s_addr)
			own = 1;

		carp_activate_vhaddr(sc, vha, ifp, ia_if, own);
	}
}

static void
carp_unlink_addrs(struct carp_softc *sc, struct ifnet *ifp,
		  struct ifaddr *ifa_if)
{
	struct carp_vhaddr *vha;
	struct in_ifaddr *ia_if;

	KKASSERT(ifa_if->ifa_addr->sa_family == AF_INET);
	ia_if = ifatoia(ifa_if);

	/*
	 * Ad src address is deleted; set it to NULL.
	 * Following loop will try pick up a new ad src address
	 * if one of the vhaddr could retain its backing address.
	 */
	if (sc->sc_ia == ia_if)
		sc->sc_ia = NULL;

	/*
	 * Test each active vhaddr against the deleted address.
	 * If the deleted address is vhaddr address's backing
	 * address, then deactivate the vhaddr.
	 */
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		if (vha->vha_iaback == NULL)
			continue;

		if (vha->vha_iaback == ia_if)
			carp_deactivate_vhaddr(sc, vha);
		else if (sc->sc_ia == NULL)
			sc->sc_ia = vha->vha_iaback;
	}
}

static void
carp_update_addrs(struct carp_softc *sc)
{
	struct carp_vhaddr *vha;

	KKASSERT(sc->sc_carpdev == NULL);

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
		carp_config_vhaddr(sc, vha);
}

static void
carp_ifaddr(void *arg __unused, struct ifnet *ifp,
	    enum ifaddr_event event, struct ifaddr *ifa)
{
	struct carp_softc *sc;

	if (ifa->ifa_addr->sa_family != AF_INET)
		return;

	if (ifp->if_type == IFT_CARP) {
		/*
		 * Address is changed on carp(4) interface
		 */
		switch (event) {
		case IFADDR_EVENT_ADD:
			carp_add_addr(ifp->if_softc, ifa);
			break;

		case IFADDR_EVENT_CHANGE:
			carp_config_addr(ifp->if_softc, ifa);
			break;

		case IFADDR_EVENT_DELETE:
			carp_del_addr(ifp->if_softc, ifa);
			break;
		}
		return;
	}

	/*
	 * Address is changed on non-carp(4) interface
	 */
	if ((ifp->if_flags & IFF_MULTICAST) == 0)
		return;

	crit_enter();
	LIST_FOREACH(sc, &carpif_list, sc_next) {
		if (sc->sc_carpdev != NULL && sc->sc_carpdev != ifp) {
			/* Not the parent iface; skip */
			continue;
		}

		switch (event) {
		case IFADDR_EVENT_ADD:
			carp_link_addrs(sc, ifp, ifa);
			break;

		case IFADDR_EVENT_DELETE:
			if (sc->sc_carpdev != NULL) {
				carp_unlink_addrs(sc, ifp, ifa);
				if (sc->sc_carpdev == NULL)
					carp_update_addrs(sc);
			} else {
				/*
				 * The carp(4) interface didn't have a
				 * parent iface, so it is not possible
				 * that it will contain any address to
				 * be unlinked.
				 */
			}
			break;

		case IFADDR_EVENT_CHANGE:
			if (sc->sc_carpdev == NULL) {
				/*
				 * The carp(4) interface didn't have a
				 * parent iface, so it is not possible
				 * that it will contain any address to
				 * be updated.
				 */
				carp_link_addrs(sc, ifp, ifa);
			} else {
				/*
				 * First try breaking tie with the old
				 * address.  Then see whether we could
				 * link certain vhaddr to the new address.
				 * If that fails, i.e. carpdev is NULL,
				 * we try a global update.
				 *
				 * NOTE: The above order is critical.
				 */
				carp_unlink_addrs(sc, ifp, ifa);
				carp_link_addrs(sc, ifp, ifa);
				if (sc->sc_carpdev == NULL)
					carp_update_addrs(sc);
			}
			break;
		}
	}
	crit_exit();
}

static int
carp_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		LIST_INIT(&carpif_list);
		carp_ifdetach_event =
		EVENTHANDLER_REGISTER(ifnet_detach_event, carp_ifdetach, NULL,
				      EVENTHANDLER_PRI_ANY);
		carp_ifaddr_event =
		EVENTHANDLER_REGISTER(ifaddr_event, carp_ifaddr, NULL,
				      EVENTHANDLER_PRI_ANY);
		if_clone_attach(&carp_cloner);
		break;

	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(ifnet_detach_event,
					carp_ifdetach_event);
		EVENTHANDLER_DEREGISTER(ifaddr_event,
					carp_ifaddr_event);
		if_clone_detach(&carp_cloner);
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

static moduledata_t carp_mod = {
	"carp",
	carp_modevent,
	0
};
DECLARE_MODULE(carp, carp_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
