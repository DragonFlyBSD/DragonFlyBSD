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
#include <sys/msgport2.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/thread.h>

#include <machine/stdarg.h>
#include <crypto/sha1.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_clone.h>
#include <net/if_var.h>
#include <net/ifq_var.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

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

/*
 * Note about carp's MP safe approach:
 *
 * Brief: carp_softc (softc), carp_softc_container (scc)
 *
 * - All configuration operation, e.g. ioctl, add/delete inet addresses
 *   is serialized by netisr0; not by carp's serializer
 *
 * - Backing interface's if_carp and carp_softc's relationship:
 *
 *                +---------+
 *     if_carp -->| carp_if |
 *                +---------+
 *                     |
 *                     |
 *                     V      +---------+
 *                  +-----+   |         |
 *                  | scc |-->|  softc  |
 *                  +-----+   |         |
 *                     |      +---------+
 *                     |
 *                     V      +---------+
 *                  +-----+   |         |
 *                  | scc |-->|  softc  |
 *                  +-----+   |         |
 *                            +---------+
 *
 * - if_carp creation, modification and deletion all happen in netisr0,
 *   as stated previously.  Since if_carp is accessed by multiple netisrs,
 *   the modification to if_carp is conducted in the following way:
 *
 *   Adding carp_softc:
 *
 *   1) Duplicate the old carp_if to new carp_if (ncif), and insert the
 *      to-be-added carp_softc to the new carp_if (ncif):
 *
 *        if_carp                     ncif
 *           |                         |
 *           V                         V
 *      +---------+               +---------+
 *      | carp_if |               | carp_if |
 *      +---------+               +---------+
 *           |                         |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *           |        +-------+        |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *                    +-------+        |
 *                                     |
 *                    +-------+        V
 *                    |       |     +-----+
 *                    | softc |<----| scc |
 *                    |       |     +-----+
 *                    +-------+
 *
 *   2) Switch save if_carp into ocif and switch if_carp to ncif:
 *      
 *          ocif                    if_carp
 *           |                         |
 *           V                         V
 *      +---------+               +---------+
 *      | carp_if |               | carp_if |
 *      +---------+               +---------+
 *           |                         |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *           |        +-------+        |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *                    +-------+        |
 *                                     |
 *                    +-------+        V
 *                    |       |     +-----+
 *                    | softc |<----| scc |
 *                    |       |     +-----+
 *                    +-------+
 *
 *   3) Run netmsg_service_sync(), which will make sure that
 *      ocif is no longer accessed (all network operations
 *      are happened only in network threads).
 *   4) Free ocif -- only carp_if and scc are freed.
 *
 *
 *   Removing carp_softc:
 *
 *   1) Duplicate the old carp_if to new carp_if (ncif); the to-be-deleted
 *      carp_softc will not be duplicated.
 *
 *        if_carp                     ncif
 *           |                         |
 *           V                         V
 *      +---------+               +---------+
 *      | carp_if |               | carp_if |
 *      +---------+               +---------+
 *           |                         |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *           |        +-------+        |
 *           |                         |
 *           V        +-------+        |
 *        +-----+     |       |        |
 *        | scc |---->| softc |        |
 *        +-----+     |       |        |
 *           |        +-------+        |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *                    +-------+
 *
 *   2) Switch save if_carp into ocif and switch if_carp to ncif:
 *      
 *          ocif                    if_carp
 *           |                         |
 *           V                         V
 *      +---------+               +---------+
 *      | carp_if |               | carp_if |
 *      +---------+               +---------+
 *           |                         |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *           |        +-------+        |
 *           |                         |
 *           V        +-------+        |
 *        +-----+     |       |        |
 *        | scc |---->| softc |        |
 *        +-----+     |       |        |
 *           |        +-------+        |
 *           |                         |
 *           V        +-------+        V
 *        +-----+     |       |     +-----+
 *        | scc |---->| softc |<----| scc |
 *        +-----+     |       |     +-----+
 *                    +-------+
 *
 *   3) Run netmsg_service_sync(), which will make sure that
 *      ocif is no longer accessed (all network operations
 *      are happened only in network threads).
 *   4) Free ocif -- only carp_if and scc are freed.
 *
 * - if_carp accessing:
 *   The accessing code should cache the if_carp in a local temporary
 *   variable and accessing the temporary variable along the code path
 *   instead of accessing if_carp later on.
 */

#define	CARP_IFNAME		"carp"
#define CARP_IS_RUNNING(ifp)	\
	(((ifp)->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))

struct carp_softc;

struct carp_vhaddr {
	uint32_t		vha_flags;	/* CARP_VHAF_ */
	struct in_ifaddr	*vha_ia;	/* carp address */
	struct in_ifaddr	*vha_iaback;	/* backing address */
	TAILQ_ENTRY(carp_vhaddr) vha_link;
};
TAILQ_HEAD(carp_vhaddr_list, carp_vhaddr);

struct netmsg_carp {
	struct netmsg_base	base;
	struct ifnet		*nc_carpdev;
	struct carp_softc	*nc_softc;
	void			*nc_data;
	size_t			nc_datalen;
};

struct carp_softc {
	struct arpcom		 arpcom;
	struct ifnet		*sc_carpdev;	/* parent interface */
	struct carp_vhaddr_list	 sc_vha_list;	/* virtual addr list */

	const struct in_ifaddr	*sc_ia;		/* primary iface address v4 */
	struct ip_moptions 	 sc_imo;

#ifdef INET6
	struct in6_ifaddr 	*sc_ia6;	/* primary iface address v6 */
	struct ip6_moptions 	 sc_im6o;
#endif /* INET6 */

	enum { INIT = 0, BACKUP, MASTER }
				 sc_state;
	boolean_t		 sc_dead;

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
	struct netmsg_carp	 sc_ad_msg;	/* adv timeout netmsg */
	struct callout		 sc_md_tmo;	/* ip4 master down timeout */
	struct callout 		 sc_md6_tmo;	/* ip6 master down timeout */
	struct netmsg_carp	 sc_md_msg;	/* master down timeout netmsg */

	LIST_ENTRY(carp_softc)	 sc_next;	/* Interface clue */
};

#define sc_if	arpcom.ac_if

struct carp_softc_container {
	TAILQ_ENTRY(carp_softc_container) scc_link;
	struct carp_softc	*scc_softc;
};
TAILQ_HEAD(carp_if, carp_softc_container);

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

static int carp_prio_ad = 1;
SYSCTL_INT(_net_inet_carp, OID_AUTO, prio_ad, CTLFLAG_RD,
    &carp_prio_ad, 0, "Prioritize advertisement packet");

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

static struct lwkt_token carp_listtok = LWKT_TOKEN_INITIALIZER(carp_list_token);

static void	carp_hmac_prepare(struct carp_softc *);
static void	carp_hmac_generate(struct carp_softc *, uint32_t *,
		    unsigned char *);
static int	carp_hmac_verify(struct carp_softc *, uint32_t *,
		    unsigned char *);
static void	carp_setroute(struct carp_softc *, int);
static void	carp_proto_input_c(struct carp_softc *, struct mbuf *,
		    struct carp_header *, sa_family_t);
static int 	carp_clone_create(struct if_clone *, int, caddr_t);
static int 	carp_clone_destroy(struct ifnet *);
static void	carp_detach(struct carp_softc *, boolean_t, boolean_t);
static void	carp_prepare_ad(struct carp_softc *, struct carp_header *);
static void	carp_send_ad_all(void);
static void	carp_send_ad_timeout(void *);
static void	carp_send_ad(struct carp_softc *);
static void	carp_send_arp(struct carp_softc *);
static void	carp_master_down_timeout(void *);
static void	carp_master_down(struct carp_softc *);
static void	carp_setrun(struct carp_softc *, sa_family_t);
static void	carp_set_state(struct carp_softc *, int);
static struct ifnet *carp_forus(struct carp_if *, const uint8_t *);

static void	carp_init(void *);
static int	carp_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static int	carp_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	carp_start(struct ifnet *, struct ifaltq_subque *);

static void	carp_multicast_cleanup(struct carp_softc *);
static void	carp_add_addr(struct carp_softc *, struct ifaddr *);
static void	carp_del_addr(struct carp_softc *, struct ifaddr *);
static void	carp_config_addr(struct carp_softc *, struct ifaddr *);
static void	carp_link_addrs(struct carp_softc *, struct ifnet *,
		    struct ifaddr *);
static void	carp_unlink_addrs(struct carp_softc *, struct ifnet *,
		    struct ifaddr *);
static void	carp_update_addrs(struct carp_softc *, struct ifaddr *);

static int	carp_config_vhaddr(struct carp_softc *, struct carp_vhaddr *,
		    struct in_ifaddr *);
static int	carp_activate_vhaddr(struct carp_softc *, struct carp_vhaddr *,
		    struct ifnet *, struct in_ifaddr *, int);
static void	carp_deactivate_vhaddr(struct carp_softc *,
		    struct carp_vhaddr *, boolean_t);
static int	carp_addroute_vhaddr(struct carp_softc *, struct carp_vhaddr *);
static void	carp_delroute_vhaddr(struct carp_softc *, struct carp_vhaddr *,
		    boolean_t);

#ifdef foo
static void	carp_sc_state(struct carp_softc *);
#endif
#ifdef INET6
static void	carp_send_na(struct carp_softc *);
#ifdef notyet
static int	carp_set_addr6(struct carp_softc *, struct sockaddr_in6 *);
static int	carp_del_addr6(struct carp_softc *, struct sockaddr_in6 *);
#endif
static void	carp_multicast6_cleanup(struct carp_softc *);
#endif
static void	carp_stop(struct carp_softc *, boolean_t);
static void	carp_suspend(struct carp_softc *, boolean_t);
static void	carp_ioctl_stop(struct carp_softc *);
static int	carp_ioctl_setvh(struct carp_softc *, void *, struct ucred *);
static void	carp_ioctl_ifcap(struct carp_softc *, int);
static int	carp_ioctl_getvh(struct carp_softc *, void *, struct ucred *);
static int	carp_ioctl_getdevname(struct carp_softc *, struct ifdrv *);
static int	carp_ioctl_getvhaddr(struct carp_softc *, struct ifdrv *);

static struct carp_if *carp_if_remove(struct carp_if *, struct carp_softc *);
static struct carp_if *carp_if_insert(struct carp_if *, struct carp_softc *);
static void	carp_if_free(struct carp_if *);

static void	carp_ifaddr(void *, struct ifnet *, enum ifaddr_event,
			    struct ifaddr *);
static void	carp_ifdetach(void *, struct ifnet *);

static void	carp_ifdetach_dispatch(netmsg_t);
static void	carp_clone_destroy_dispatch(netmsg_t);
static void	carp_init_dispatch(netmsg_t);
static void	carp_ioctl_stop_dispatch(netmsg_t);
static void	carp_ioctl_setvh_dispatch(netmsg_t);
static void	carp_ioctl_ifcap_dispatch(netmsg_t);
static void	carp_ioctl_getvh_dispatch(netmsg_t);
static void	carp_ioctl_getdevname_dispatch(netmsg_t);
static void	carp_ioctl_getvhaddr_dispatch(netmsg_t);
static void	carp_send_ad_timeout_dispatch(netmsg_t);
static void	carp_master_down_timeout_dispatch(netmsg_t);

static MALLOC_DEFINE(M_CARP, "CARP", "CARP interfaces");

static LIST_HEAD(, carp_softc) carpif_list;

static struct if_clone carp_cloner =
IF_CLONE_INITIALIZER(CARP_IFNAME, carp_clone_create, carp_clone_destroy,
		     0, IF_MAXUNIT);

static const uint8_t	carp_etheraddr[ETHER_ADDR_LEN] =
	{ 0, 0, 0x5e, 0, 1, 0 };

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
#endif
	struct carp_vhaddr *vha;

	KKASSERT(cmd == RTM_DELETE || cmd == RTM_ADD);

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		if (vha->vha_iaback == NULL)
			continue;
		if (cmd == RTM_DELETE)
			carp_delroute_vhaddr(sc, vha, FALSE);
		else
			carp_addroute_vhaddr(sc, vha);
	}

#ifdef INET6
	TAILQ_FOREACH(ifac, &sc->sc_if.if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr->sa_family == AF_INET6) {
			if (cmd == RTM_ADD)
				in6_ifaddloop(ifa);
			else
				in6_ifremloop(ifa);
		}
	}
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

	callout_init_mp(&sc->sc_ad_tmo);
	netmsg_init(&sc->sc_ad_msg.base, NULL, &netisr_adone_rport,
	    MSGF_DROPABLE | MSGF_PRIORITY, carp_send_ad_timeout_dispatch);
	sc->sc_ad_msg.nc_softc = sc;

	callout_init_mp(&sc->sc_md_tmo);
	callout_init_mp(&sc->sc_md6_tmo);
	netmsg_init(&sc->sc_md_msg.base, NULL, &netisr_adone_rport,
	    MSGF_DROPABLE | MSGF_PRIORITY, carp_master_down_timeout_dispatch);
	sc->sc_md_msg.nc_softc = sc;

	if_initname(ifp, CARP_IFNAME, unit);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = carp_init;
	ifp->if_ioctl = carp_ioctl;
	ifp->if_start = carp_start;

	ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_TSO;
	ifp->if_capenable = ifp->if_capabilities;
	/*
	 * Leave if_hwassist as it is; if_hwassist will be
	 * setup when this carp interface has parent.
	 */

	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, carp_etheraddr, NULL);

	ifp->if_type = IFT_CARP;
	ifp->if_output = carp_output;

	lwkt_gettoken(&carp_listtok);
	LIST_INSERT_HEAD(&carpif_list, sc, sc_next);
	lwkt_reltoken(&carp_listtok);

	return (0);
}

static void
carp_clone_destroy_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;

	sc->sc_dead = TRUE;
	carp_detach(sc, TRUE, FALSE);

	callout_stop_sync(&sc->sc_ad_tmo);
	callout_stop_sync(&sc->sc_md_tmo);
	callout_stop_sync(&sc->sc_md6_tmo);

	crit_enter();
	lwkt_dropmsg(&sc->sc_ad_msg.base.lmsg);
	lwkt_dropmsg(&sc->sc_md_msg.base.lmsg);
	crit_exit();

	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static int
carp_clone_destroy(struct ifnet *ifp)
{
	struct carp_softc *sc = ifp->if_softc;
	struct netmsg_carp cmsg;

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_clone_destroy_dispatch);
	cmsg.nc_softc = sc;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	lwkt_gettoken(&carp_listtok);
	LIST_REMOVE(sc, sc_next);
	lwkt_reltoken(&carp_listtok);

	bpfdetach(ifp);
	if_detach(ifp);

	KASSERT(sc->sc_naddrs == 0, ("certain inet address is still active"));
	kfree(sc, M_CARP);

	return 0;
}

static struct carp_if *
carp_if_remove(struct carp_if *ocif, struct carp_softc *sc)
{
	struct carp_softc_container *oscc, *scc;
	struct carp_if *cif;
	int count = 0;
#ifdef INVARIANTS
	int found = 0;
#endif

	TAILQ_FOREACH(oscc, ocif, scc_link) {
		++count;
#ifdef INVARIANTS
		if (oscc->scc_softc == sc)
			found = 1;
#endif
	}
	KASSERT(found, ("%s carp_softc is not on carp_if", __func__));

	if (count == 1) {
		/* Last one is going to be unlinked */
		return NULL;
	}

	cif = kmalloc(sizeof(*cif), M_CARP, M_WAITOK | M_ZERO);
	TAILQ_INIT(cif);

	TAILQ_FOREACH(oscc, ocif, scc_link) {
		if (oscc->scc_softc == sc)
			continue;

		scc = kmalloc(sizeof(*scc), M_CARP, M_WAITOK | M_ZERO);
		scc->scc_softc = oscc->scc_softc;
		TAILQ_INSERT_TAIL(cif, scc, scc_link);
	}

	return cif;
}

static struct carp_if *
carp_if_insert(struct carp_if *ocif, struct carp_softc *sc)
{
	struct carp_softc_container *oscc;
	int onlist;

	onlist = 0;
	if (ocif != NULL) {
		TAILQ_FOREACH(oscc, ocif, scc_link) {
			if (oscc->scc_softc == sc)
				onlist = 1;
		}
	}

#ifdef INVARIANTS
	if (sc->sc_carpdev != NULL) {
		KASSERT(onlist, ("%s is not on %s carp list",
		    sc->sc_if.if_xname, sc->sc_carpdev->if_xname));
	} else {
		KASSERT(!onlist, ("%s is already on carp list",
		    sc->sc_if.if_xname));
	}
#endif

	if (!onlist) {
		struct carp_if *cif;
		struct carp_softc_container *new_scc, *scc;
		int inserted = 0;

		cif = kmalloc(sizeof(*cif), M_CARP, M_WAITOK | M_ZERO);
		TAILQ_INIT(cif);

		new_scc = kmalloc(sizeof(*new_scc), M_CARP, M_WAITOK | M_ZERO);
		new_scc->scc_softc = sc;

		if (ocif != NULL) {
			TAILQ_FOREACH(oscc, ocif, scc_link) {
				if (!inserted &&
				    oscc->scc_softc->sc_vhid > sc->sc_vhid) {
					TAILQ_INSERT_TAIL(cif, new_scc,
					    scc_link);
					inserted = 1;
				}

				scc = kmalloc(sizeof(*scc), M_CARP,
				    M_WAITOK | M_ZERO);
				scc->scc_softc = oscc->scc_softc;
				TAILQ_INSERT_TAIL(cif, scc, scc_link);
			}
		}
		if (!inserted)
			TAILQ_INSERT_TAIL(cif, new_scc, scc_link);

		return cif;
	} else {
		return ocif;
	}
}

static void
carp_if_free(struct carp_if *cif)
{
	struct carp_softc_container *scc;

	while ((scc = TAILQ_FIRST(cif)) != NULL) {
		TAILQ_REMOVE(cif, scc, scc_link);
		kfree(scc, M_CARP);
	}
	kfree(cif, M_CARP);
}

static void
carp_detach(struct carp_softc *sc, boolean_t detach, boolean_t del_iaback)
{
	carp_suspend(sc, detach);

	carp_multicast_cleanup(sc);
#ifdef INET6
	carp_multicast6_cleanup(sc);
#endif

	if (!sc->sc_dead && detach) {
		struct carp_vhaddr *vha;

		TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
			carp_deactivate_vhaddr(sc, vha, del_iaback);
		KKASSERT(sc->sc_naddrs == 0);
	}

	if (sc->sc_carpdev != NULL) {
		struct ifnet *ifp = sc->sc_carpdev;
		struct carp_if *ocif = ifp->if_carp;

		ifp->if_carp = carp_if_remove(ocif, sc);
		KASSERT(ifp->if_carp != ocif,
		    ("%s carp_if_remove failed", __func__));

		sc->sc_carpdev = NULL;
		sc->sc_ia = NULL;
		sc->arpcom.ac_if.if_hwassist = 0;

		/*
		 * Make sure that all protocol threads see the
		 * sc_carpdev and if_carp changes
		 */
		netmsg_service_sync();

		if (ifp->if_carp == NULL) {
			/*
			 * No more carp interfaces using
			 * ifp as the backing interface,
			 * move it out of promiscous mode.
			 */
			ifpromisc(ifp, 0);
		}

		/*
		 * The old carp list could be safely free now,
		 * since no one can access it.
		 */
		carp_if_free(ocif);
	}
}

static void
carp_ifdetach_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct ifnet *ifp = cmsg->nc_carpdev;

	while (ifp->if_carp) {
		struct carp_softc_container *scc;

		scc = TAILQ_FIRST((struct carp_if *)(ifp->if_carp));
		carp_detach(scc->scc_softc, TRUE, TRUE);
	}
	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

/* Detach an interface from the carp. */
static void
carp_ifdetach(void *arg __unused, struct ifnet *ifp)
{
	struct netmsg_carp cmsg;

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ifdetach_dispatch);
	cmsg.nc_carpdev = ifp;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);
}

/*
 * process input packet.
 * we have rearranged checks order compared to the rfc,
 * but it seems more efficient this way or not possible otherwise.
 */
int
carp_proto_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct carp_header *ch;
	struct carp_softc *sc;
	int len, iphlen;

	iphlen = *offp;
	*mp = NULL;

	carpstats.carps_ipackets++;

	if (!carp_opts[CARPCTL_ALLOW]) {
		m_freem(m);
		goto back;
	}

	/* Check if received on a valid carp interface */
	if (ifp->if_type != IFT_CARP) {
		carpstats.carps_badif++;
		CARP_LOG("carp_proto_input: packet received on non-carp "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	if (!CARP_IS_RUNNING(ifp)) {
		carpstats.carps_badif++;
		CARP_LOG("carp_proto_input: packet received on stopped carp "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	sc = ifp->if_softc;
	if (sc->sc_carpdev == NULL) {
		carpstats.carps_badif++;
		CARP_LOG("carp_proto_input: packet received on defunc carp "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	if (!IN_MULTICAST(ntohl(ip->ip_dst.s_addr))) {
		carpstats.carps_badif++;
		CARP_LOG("carp_proto_input: non-mcast packet on "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	/* Verify that the IP TTL is CARP_DFLTTL. */
	if (ip->ip_ttl != CARP_DFLTTL) {
		carpstats.carps_badttl++;
		CARP_LOG("carp_proto_input: received ttl %d != %d on %s\n",
		    ip->ip_ttl, CARP_DFLTTL, ifp->if_xname);
		m_freem(m);
		goto back;
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
		    ifp->if_xname);
		m_freem(m);
		goto back;
	}

	/* Make sure that CARP header is contiguous */
	if (len > m->m_len) {
		m = m_pullup(m, len);
		if (m == NULL) {
			carpstats.carps_hdrops++;
			CARP_LOG("carp_proto_input: m_pullup failed\n");
			goto back;
		}
		ip = mtod(m, struct ip *);
	}
	ch = (struct carp_header *)((uint8_t *)ip + iphlen);

	/* Verify the CARP checksum */
	if (in_cksum_skip(m, len, iphlen)) {
		carpstats.carps_badsum++;
		CARP_LOG("carp_proto_input: checksum failed on %s\n",
		    ifp->if_xname);
		m_freem(m);
		goto back;
	}
	carp_proto_input_c(sc, m, ch, AF_INET);
back:
	return(IPPROTO_DONE);
}

#ifdef INET6
int
carp6_proto_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct carp_header *ch;
	struct carp_softc *sc;
	u_int len;

	carpstats.carps_ipackets6++;

	if (!carp_opts[CARPCTL_ALLOW]) {
		m_freem(m);
		goto back;
	}

	/* check if received on a valid carp interface */
	if (ifp->if_type != IFT_CARP) {
		carpstats.carps_badif++;
		CARP_LOG("carp6_proto_input: packet received on non-carp "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	if (!CARP_IS_RUNNING(ifp)) {
		carpstats.carps_badif++;
		CARP_LOG("carp_proto_input: packet received on stopped carp "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	sc = ifp->if_softc;
	if (sc->sc_carpdev == NULL) {
		carpstats.carps_badif++;
		CARP_LOG("carp6_proto_input: packet received on defunc-carp "
		    "interface: %s\n", ifp->if_xname);
		m_freem(m);
		goto back;
	}

	/* verify that the IP TTL is 255 */
	if (ip6->ip6_hlim != CARP_DFLTTL) {
		carpstats.carps_badttl++;
		CARP_LOG("carp6_proto_input: received ttl %d != 255 on %s\n",
		    ip6->ip6_hlim, ifp->if_xname);
		m_freem(m);
		goto back;
	}

	/* verify that we have a complete carp packet */
	len = m->m_len;
	IP6_EXTHDR_GET(ch, struct carp_header *, m, *offp, sizeof(*ch));
	if (ch == NULL) {
		carpstats.carps_badlen++;
		CARP_LOG("carp6_proto_input: packet size %u too small\n", len);
		goto back;
	}

	/* verify the CARP checksum */
	if (in_cksum_range(m, 0, *offp, sizeof(*ch))) {
		carpstats.carps_badsum++;
		CARP_LOG("carp6_proto_input: checksum failed, on %s\n",
		    ifp->if_xname);
		m_freem(m);
		goto back;
	}

	carp_proto_input_c(sc, m, ch, AF_INET6);
back:
	return (IPPROTO_DONE);
}
#endif /* INET6 */

static void
carp_proto_input_c(struct carp_softc *sc, struct mbuf *m,
    struct carp_header *ch, sa_family_t af)
{
	struct ifnet *cifp;
	uint64_t tmp_counter;
	struct timeval sc_tv, ch_tv;

	if (sc->sc_vhid != ch->carp_vhid) {
		/*
		 * CARP uses multicast, however, multicast packets
		 * are tapped to all CARP interfaces on the physical
		 * interface receiving the CARP packets, so we don't
		 * update any stats here.
		 */
		m_freem(m);
		return;
	}
	cifp = &sc->sc_if;

	/* verify the CARP version. */
	if (ch->carp_version != CARP_VERSION) {
		carpstats.carps_badver++;
		CARP_LOG("%s; invalid version %d\n", cifp->if_xname,
			 ch->carp_version);
		m_freem(m);
		return;
	}

	/* verify the hash */
	if (carp_hmac_verify(sc, ch->carp_counter, ch->carp_md)) {
		carpstats.carps_badauth++;
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

struct mbuf *
carp_input(void *v, struct mbuf *m)
{
	struct carp_if *cif = v;
	struct ether_header *eh;
	struct carp_softc_container *scc;
	struct ifnet *ifp;

	eh = mtod(m, struct ether_header *);

	ifp = carp_forus(cif, eh->ether_dhost);
	if (ifp != NULL) {
		ether_reinput_oncpu(ifp, m, REINPUT_RUNBPF);
		return NULL;
	}

	if ((m->m_flags & (M_BCAST | M_MCAST)) == 0)
		return m;

	/*
	 * XXX Should really check the list of multicast addresses
	 * for each CARP interface _before_ copying.
	 */
	TAILQ_FOREACH(scc, cif, scc_link) {
		struct carp_softc *sc = scc->scc_softc;
		struct mbuf *m0;

		if ((sc->sc_if.if_flags & IFF_UP) == 0)
			continue;

		m0 = m_dup(m, M_NOWAIT);
		if (m0 == NULL)
			continue;

		ether_reinput_oncpu(&sc->sc_if, m0, REINPUT_RUNBPF);
	}
	return m;
}

static void
carp_prepare_ad(struct carp_softc *sc, struct carp_header *ch)
{
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
	struct carp_softc *sc = xsc;
	struct netmsg_carp *cmsg = &sc->sc_ad_msg;

	KASSERT(mycpuid == 0, ("%s not on cpu0 but on cpu%d",
	    __func__, mycpuid));

	crit_enter();
	if (cmsg->base.lmsg.ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(0), &cmsg->base.lmsg);
	crit_exit();
}

static void
carp_send_ad_timeout_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&cmsg->base.lmsg, 0);
	crit_exit();

	carp_send_ad(sc);
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

		MGETHDR(m, M_NOWAIT, MT_HEADER);
		if (m == NULL) {
			IFNET_STAT_INC(cifp, oerrors, 1);
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
		if (carp_prio_ad)
			m->m_flags |= M_PRIO;
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
		carp_prepare_ad(sc, ch_ptr);
		ch_ptr->carp_cksum = in_cksum_skip(m, len, sizeof(*ip));

		getmicrotime(&cifp->if_lastchange);
		IFNET_STAT_INC(cifp, opackets, 1);
		IFNET_STAT_INC(cifp, obytes, len);
		carpstats.carps_opackets++;

		if (ip_output(m, NULL, NULL, IP_RAWOUTPUT, &sc->sc_imo, NULL)) {
			IFNET_STAT_INC(cifp, oerrors, 1);
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

		MGETHDR(m, M_NOWAIT, MT_HEADER);
		if (m == NULL) {
			IFNET_STAT_INC(cifp, oerrors, 1);
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
			IFNET_STAT_INC(cifp, oerrors, 1);
			m_freem(m);
			CARP_LOG("%s: in6_setscope failed\n", __func__);
			return;
		}

		ch_ptr = (struct carp_header *)(&ip6[1]);
		bcopy(&ch, ch_ptr, sizeof(ch));
		carp_prepare_ad(sc, ch_ptr);
		ch_ptr->carp_cksum = in_cksum_skip(m, len, sizeof(*ip6));

		getmicrotime(&cifp->if_lastchange);
		IFNET_STAT_INC(cifp, opackets, 1);
		IFNET_STAT_INC(cifp, obytes, len);
		carpstats.carps_opackets6++;

		if (ip6_output(m, NULL, NULL, 0, &sc->sc_im6o, NULL, NULL)) {
			IFNET_STAT_INC(cifp, oerrors, 1);
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
		arp_gratuitous(&sc->sc_if, &vha->vha_ia->ia_ifa);
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

#ifdef notyet
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
#endif

int
carp_iamatch(const struct in_ifaddr *ia)
{
	const struct carp_softc *sc = ia->ia_ifp->if_softc;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));

#ifdef notyet
	if (carp_opts[CARPCTL_ARPBALANCE])
		return carp_iamatch_balance(cif, itaddr, isaddr, enaddr);
#endif

	if (!CARP_IS_RUNNING(&sc->sc_if) || sc->sc_state != MASTER)
		return 0;

	return 1;
}

#ifdef INET6
struct ifaddr *
carp_iamatch6(void *v, struct in6_addr *taddr)
{
#ifdef foo
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
#endif
	return (NULL);
}

void *
carp_macmatch6(void *v, struct mbuf *m, const struct in6_addr *taddr)
{
#ifdef foo
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
				    sizeof(struct ifnet *), M_NOWAIT);
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
#endif
	return (NULL);
}
#endif

static struct ifnet *
carp_forus(struct carp_if *cif, const uint8_t *dhost)
{
	struct carp_softc_container *scc;

	if (memcmp(dhost, carp_etheraddr, ETHER_ADDR_LEN - 1) != 0)
		return NULL;

	TAILQ_FOREACH(scc, cif, scc_link) {
		struct carp_softc *sc = scc->scc_softc;
		struct ifnet *ifp = &sc->sc_if;

		if (CARP_IS_RUNNING(ifp) && sc->sc_state == MASTER &&
		    !bcmp(dhost, IF_LLADDR(ifp), ETHER_ADDR_LEN))
			return ifp;
	}
	return NULL;
}

static void
carp_master_down_timeout(void *xsc)
{
	struct carp_softc *sc = xsc;
	struct netmsg_carp *cmsg = &sc->sc_md_msg;

	KASSERT(mycpuid == 0, ("%s not on cpu0 but on cpu%d",
	    __func__, mycpuid));

	crit_enter();
	if (cmsg->base.lmsg.ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(0), &cmsg->base.lmsg);
	crit_exit();
}

static void
carp_master_down_timeout_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&cmsg->base.lmsg, 0);
	crit_exit();

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

static void
carp_ioctl_getvhaddr_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;
	const struct carp_vhaddr *vha;
	struct ifcarpvhaddr *carpa, *carpa0;
	int count, len, error = 0;

	count = 0;
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
		++count;

	if (cmsg->nc_datalen == 0) {
		cmsg->nc_datalen = count * sizeof(*carpa);
		goto back;
	} else if (count == 0 || cmsg->nc_datalen < sizeof(*carpa)) {
		cmsg->nc_datalen = 0;
		goto back;
	}
	len = min(cmsg->nc_datalen, sizeof(*carpa) * count);
	KKASSERT(len >= sizeof(*carpa));

	carpa0 = carpa = kmalloc(len, M_TEMP, M_WAITOK | M_NULLOK | M_ZERO);
	if (carpa == NULL) {
		error = ENOMEM; 
		goto back;
	}

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
	cmsg->nc_datalen = sizeof(*carpa) * count;
	KKASSERT(cmsg->nc_datalen > 0);

	cmsg->nc_data = carpa0;

back:
	lwkt_replymsg(&cmsg->base.lmsg, error);
}

static int
carp_ioctl_getvhaddr(struct carp_softc *sc, struct ifdrv *ifd)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;
	int error;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	ifnet_deserialize_all(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ioctl_getvhaddr_dispatch);
	cmsg.nc_softc = sc;
	cmsg.nc_datalen = ifd->ifd_len;

	error = lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	if (!error) {
		if (cmsg.nc_data != NULL) {
			error = copyout(cmsg.nc_data, ifd->ifd_data,
			    cmsg.nc_datalen);
			kfree(cmsg.nc_data, M_TEMP);
		}
		ifd->ifd_len = cmsg.nc_datalen;
	} else {
		KASSERT(cmsg.nc_data == NULL,
		    ("%s temp vhaddr is alloc upon error", __func__));
	}

	ifnet_serialize_all(ifp);
	return error;
}

static int
carp_config_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha,
    struct in_ifaddr *ia_del)
{
	struct ifnet *ifp;
	struct in_ifaddr *ia_if;
	const struct in_ifaddr *ia_vha;
	struct in_ifaddr_container *iac;
	int own, ia_match_carpdev;

	KKASSERT(vha->vha_ia != NULL);
	ia_vha = vha->vha_ia;

	ia_if = NULL;
	own = 0;
	ia_match_carpdev = 0;
	TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
		struct in_ifaddr *ia = iac->ia;

		if (ia == ia_del)
			continue;

		if (ia->ia_ifp->if_type == IFT_CARP)
			continue;

		if ((ia->ia_ifp->if_flags & IFF_UP) == 0)
			continue;

		/* and, yeah, we need a multicast-capable iface too */
		if ((ia->ia_ifp->if_flags & IFF_MULTICAST) == 0)
			continue;

		if (ia_vha->ia_subnetmask == ia->ia_subnetmask &&
		    ia_vha->ia_subnet == ia->ia_subnet) {
			if (ia_vha->ia_addr.sin_addr.s_addr ==
			    ia->ia_addr.sin_addr.s_addr)
				own = 1;
			if (ia_if == NULL) {
				ia_if = ia;
			} else if (sc->sc_carpdev != NULL &&
			    sc->sc_carpdev == ia->ia_ifp) {
				ia_if = ia;
				if (ia_if->ia_flags & IFA_ROUTE) {
					/*
					 * Address with prefix route
					 * is prefered
					 */
					break;
				}
				ia_match_carpdev = 1;
			} else if (!ia_match_carpdev) {
				if (ia->ia_flags & IFA_ROUTE) {
					/*
					 * Address with prefix route
					 * is prefered over others.
					 */
					ia_if = ia;
				}
			}
		}
	}

	carp_deactivate_vhaddr(sc, vha, FALSE);
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

	if (carp_config_vhaddr(sc, vha_new, NULL) != 0) {
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
	KASSERT(vha != NULL, ("no corresponding vhaddr %p", carp_ifa));

	/*
	 * Remove the vhaddr from the list before deactivating
	 * the vhaddr, so that the HMAC could be correctly
	 * updated in carp_deactivate_vhaddr()
	 */
	carp_remove_vhaddr(sc, vha);

	carp_deactivate_vhaddr(sc, vha, FALSE);
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
	KASSERT(vha != NULL, ("no corresponding vhaddr %p", carp_ifa));

	/* Remove then reinsert, to keep the vhaddr list sorted */
	carp_remove_vhaddr(sc, vha);
	carp_insert_vhaddr(sc, vha);

	if (carp_config_vhaddr(sc, vha, NULL) != 0) {
		/* See the comment in carp_add_addr() */
		carp_hmac_prepare(sc);
		carp_set_state(sc, INIT);
		carp_setrun(sc, 0);
	}
}

#ifdef notyet

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

#ifdef foo
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
#endif
	sc->sc_ia6 = ia;
	sc->sc_carpdev = ifp;

#ifdef foo
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
#endif

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
#ifdef foo
		TAILQ_REMOVE(&cif->vhif_vrs, sc, sc_list);
		if (TAILQ_EMPTY(&cif->vhif_vrs)) {
			sc->sc_carpdev->if_carp = NULL;
			kfree(cif, M_IFADDR);
		}
#endif
	}
	return (error);
}
#endif /* INET6 */

#endif

static int
carp_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr, struct ucred *cr)
{
	struct carp_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)addr;
	struct ifdrv *ifd = (struct ifdrv *)addr;
	int error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				carp_init(sc);
		} else if (ifp->if_flags & IFF_RUNNING) {
			carp_ioctl_stop(sc);
		}
		break;

	case SIOCSIFCAP:
		carp_ioctl_ifcap(sc, ifr->ifr_reqcap);
		break;

	case SIOCSVH:
		error = carp_ioctl_setvh(sc, ifr->ifr_data, cr);
		break;

	case SIOCGVH:
		error = carp_ioctl_getvh(sc, ifr->ifr_data, cr);
		break;

	case SIOCGDRVSPEC:
		switch (ifd->ifd_cmd) {
		case CARPGDEVNAME:
			error = carp_ioctl_getdevname(sc, ifd);
			break;

		case CARPGVHADDR:
			error = carp_ioctl_getvhaddr(sc, ifd);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		error = ether_ioctl(ifp, cmd, addr);
		break;
	}

	return error;
}

static void
carp_ioctl_stop_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;

	carp_stop(sc, FALSE);
	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static void
carp_ioctl_stop(struct carp_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	ifnet_deserialize_all(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ioctl_stop_dispatch);
	cmsg.nc_softc = sc;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	ifnet_serialize_all(ifp);
}

static void
carp_ioctl_setvh_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	const struct carpreq *carpr = cmsg->nc_data;
	int error;

	error = 1;
	if ((ifp->if_flags & IFF_RUNNING) &&
	    sc->sc_state != INIT && carpr->carpr_state != sc->sc_state) {
		switch (carpr->carpr_state) {
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
	if (carpr->carpr_vhid > 0) {
		if (carpr->carpr_vhid > 255) {
			error = EINVAL;
			goto back;
		}
		if (sc->sc_carpdev) {
			struct carp_if *cif = sc->sc_carpdev->if_carp;
			struct carp_softc_container *scc;

			TAILQ_FOREACH(scc, cif, scc_link) {
				struct carp_softc *vr = scc->scc_softc;

				if (vr != sc &&
				    vr->sc_vhid == carpr->carpr_vhid) {
					error = EEXIST;
					goto back;
				}
			}
		}
		sc->sc_vhid = carpr->carpr_vhid;

		IF_LLADDR(ifp)[5] = sc->sc_vhid;
		bcopy(IF_LLADDR(ifp), sc->arpcom.ac_enaddr,
		    ETHER_ADDR_LEN);

		error--;
	}
	if (carpr->carpr_advbase > 0 || carpr->carpr_advskew > 0) {
		if (carpr->carpr_advskew >= 255) {
			error = EINVAL;
			goto back;
		}
		if (carpr->carpr_advbase > 255) {
			error = EINVAL;
			goto back;
		}
		sc->sc_advbase = carpr->carpr_advbase;
		sc->sc_advskew = carpr->carpr_advskew;
		error--;
	}
	bcopy(carpr->carpr_key, sc->sc_key, sizeof(sc->sc_key));
	if (error > 0) {
		error = EINVAL;
	} else {
		error = 0;
		carp_setrun(sc, 0);
	}
back:
	carp_hmac_prepare(sc);

	lwkt_replymsg(&cmsg->base.lmsg, error);
}

static int
carp_ioctl_setvh(struct carp_softc *sc, void *udata, struct ucred *cr)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;
	struct carpreq carpr;
	int error;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	ifnet_deserialize_all(ifp);

	error = priv_check_cred(cr, PRIV_ROOT, NULL_CRED_OKAY);
	if (error)
		goto back;

	error = copyin(udata, &carpr, sizeof(carpr));
	if (error)
		goto back;

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ioctl_setvh_dispatch);
	cmsg.nc_softc = sc;
	cmsg.nc_data = &carpr;

	error = lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

back:
	ifnet_serialize_all(ifp);
	return error;
}

static void
carp_ioctl_ifcap_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int reqcap = *((const int *)(cmsg->nc_data));
	int mask;

	mask = reqcap ^ ifp->if_capenable;
	if (mask & IFCAP_TXCSUM) {
		ifp->if_capenable ^= IFCAP_TXCSUM;
		if ((ifp->if_capenable & IFCAP_TXCSUM) &&
		    sc->sc_carpdev != NULL) {
			ifp->if_hwassist |=
			    (sc->sc_carpdev->if_hwassist &
			     (CSUM_IP | CSUM_UDP | CSUM_TCP));
		} else {
			ifp->if_hwassist &= ~(CSUM_IP | CSUM_UDP | CSUM_TCP);
		}
	}
	if (mask & IFCAP_TSO) {
		ifp->if_capenable ^= IFCAP_TSO;
		if ((ifp->if_capenable & IFCAP_TSO) &&
		    sc->sc_carpdev != NULL) {
			ifp->if_hwassist |=
			    (sc->sc_carpdev->if_hwassist & CSUM_TSO);
		} else {
			ifp->if_hwassist &= ~CSUM_TSO;
		}
	}

	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static void
carp_ioctl_ifcap(struct carp_softc *sc, int reqcap)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	ifnet_deserialize_all(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ioctl_ifcap_dispatch);
	cmsg.nc_softc = sc;
	cmsg.nc_data = &reqcap;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	ifnet_serialize_all(ifp);
}

static void
carp_ioctl_getvh_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;
	struct carpreq *carpr = cmsg->nc_data;

	carpr->carpr_state = sc->sc_state;
	carpr->carpr_vhid = sc->sc_vhid;
	carpr->carpr_advbase = sc->sc_advbase;
	carpr->carpr_advskew = sc->sc_advskew;
	bcopy(sc->sc_key, carpr->carpr_key, sizeof(carpr->carpr_key));

	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static int
carp_ioctl_getvh(struct carp_softc *sc, void *udata, struct ucred *cr)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;
	struct carpreq carpr;
	int error;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	ifnet_deserialize_all(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ioctl_getvh_dispatch);
	cmsg.nc_softc = sc;
	cmsg.nc_data = &carpr;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	error = priv_check_cred(cr, PRIV_ROOT, NULL_CRED_OKAY);
	if (error)
		bzero(carpr.carpr_key, sizeof(carpr.carpr_key));

	error = copyout(&carpr, udata, sizeof(carpr));

	ifnet_serialize_all(ifp);
	return error;
}

static void
carp_ioctl_getdevname_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;
	char *devname = cmsg->nc_data;

	bzero(devname, IFNAMSIZ);
	if (sc->sc_carpdev != NULL)
		strlcpy(devname, sc->sc_carpdev->if_xname, IFNAMSIZ);

	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static int
carp_ioctl_getdevname(struct carp_softc *sc, struct ifdrv *ifd)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;
	char devname[IFNAMSIZ];
	int error;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (ifd->ifd_len != sizeof(devname))
		return EINVAL;

	ifnet_deserialize_all(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_ioctl_getdevname_dispatch);
	cmsg.nc_softc = sc;
	cmsg.nc_data = devname;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	error = copyout(devname, ifd->ifd_data, sizeof(devname));

	ifnet_serialize_all(ifp);
	return error;
}

static void
carp_init_dispatch(netmsg_t msg)
{
	struct netmsg_carp *cmsg = (struct netmsg_carp *)msg;
	struct carp_softc *sc = cmsg->nc_softc;

	sc->sc_if.if_flags |= IFF_RUNNING;
	carp_hmac_prepare(sc);
	carp_set_state(sc, INIT);
	carp_setrun(sc, 0);

	lwkt_replymsg(&cmsg->base.lmsg, 0);
}

static void
carp_init(void *xsc)
{
	struct carp_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct netmsg_carp cmsg;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	ifnet_deserialize_all(ifp);

	bzero(&cmsg, sizeof(cmsg));
	netmsg_init(&cmsg.base, NULL, &curthread->td_msgport, 0,
	    carp_init_dispatch);
	cmsg.nc_softc = sc;

	lwkt_domsg(netisr_cpuport(0), &cmsg.base.lmsg, 0);

	ifnet_serialize_all(ifp);
}

static int
carp_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct carp_softc *sc = ifp->if_softc;
	struct ifnet *carpdev;
	int error = 0;

	carpdev = sc->sc_carpdev;
	if (carpdev != NULL) {
		if (m->m_flags & M_MCAST)
			IFNET_STAT_INC(ifp, omcasts, 1);
		IFNET_STAT_INC(ifp, obytes, m->m_pkthdr.len + ETHER_HDR_LEN);
		IFNET_STAT_INC(ifp, opackets, 1);

		/*
		 * NOTE:
		 * CARP's ifp is passed to backing device's
		 * if_output method.
		 */
		carpdev->if_output(ifp, m, dst, rt);
	} else {
		IFNET_STAT_INC(ifp, oerrors, 1);
		m_freem(m);
		error = ENETUNREACH;
	}
	return error;
}

/*
 * Start output on carp interface. This function should never be called.
 */
static void
carp_start(struct ifnet *ifp, struct ifaltq_subque *ifsq __unused)
{
	panic("%s: start called", ifp->if_xname);
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

#ifdef foo
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
#endif

static void
carp_stop(struct carp_softc *sc, boolean_t detach)
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
carp_suspend(struct carp_softc *sc, boolean_t detach)
{
	struct ifnet *cifp = &sc->sc_if;

	carp_stop(sc, detach);

	/* Retain the running state, if we are not dead yet */
	if (!sc->sc_dead && (cifp->if_flags & IFF_UP))
		cifp->if_flags |= IFF_RUNNING;
}

static int
carp_activate_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha,
    struct ifnet *ifp, struct in_ifaddr *ia_if, int own)
{
	struct ip_moptions *imo = &sc->sc_imo;
	struct carp_if *ocif = ifp->if_carp;
	int error;

	KKASSERT(vha->vha_ia != NULL);

	KASSERT(ia_if != NULL, ("NULL backing address"));
	KASSERT(vha->vha_iaback == NULL, ("%p is already activated", vha));
	KASSERT((vha->vha_flags & CARP_VHAF_OWNER) == 0,
		("inactive vhaddr %p is the address owner", vha));

	KASSERT(sc->sc_carpdev == NULL || sc->sc_carpdev == ifp,
		("%s is already on %s", sc->sc_if.if_xname,
		 sc->sc_carpdev->if_xname));

	if (ocif == NULL) {
		KASSERT(sc->sc_carpdev == NULL,
			("%s is already on %s", sc->sc_if.if_xname,
			 sc->sc_carpdev->if_xname));

		error = ifpromisc(ifp, 1);
		if (error)
			return error;
	} else {
		struct carp_softc_container *scc;

		TAILQ_FOREACH(scc, ocif, scc_link) {
			struct carp_softc *vr = scc->scc_softc;

			if (vr != sc && vr->sc_vhid == sc->sc_vhid)
				return EINVAL;
		}
	}

	ifp->if_carp = carp_if_insert(ocif, sc);
	KASSERT(ifp->if_carp != NULL, ("%s carp_if_insert failed", __func__));

	sc->sc_ia = ia_if;
	sc->sc_carpdev = ifp;
	sc->arpcom.ac_if.if_hwassist = 0;
	if (sc->arpcom.ac_if.if_capenable & IFCAP_TXCSUM) {
		sc->arpcom.ac_if.if_hwassist |=
		    (ifp->if_hwassist & (CSUM_IP | CSUM_UDP | CSUM_TCP));
	}
	if (sc->arpcom.ac_if.if_capenable & IFCAP_TSO)
		sc->arpcom.ac_if.if_hwassist |= (ifp->if_hwassist & CSUM_TSO);

	/*
	 * Make sure that all protocol threads see the sc_carpdev and
	 * if_carp changes
	 */
	netmsg_service_sync();

	if (ocif != NULL && ifp->if_carp != ocif) {
		/*
		 * The old carp list could be safely free now,
		 * since no one can access it.
		 */
		carp_if_free(ocif);
	}

	vha->vha_iaback = ia_if;
	sc->sc_naddrs++;

	if (own) {
		vha->vha_flags |= CARP_VHAF_OWNER;

		/* XXX save user configured advskew? */
		sc->sc_advskew = 0;
	}

	carp_addroute_vhaddr(sc, vha);

	/*
	 * Join the multicast group only after the backing interface
	 * has been hooked with the CARP interface.
	 */
	KASSERT(imo->imo_multicast_ifp == NULL ||
		imo->imo_multicast_ifp == &sc->sc_if,
		("%s didn't leave mcast group on %s",
		 sc->sc_if.if_xname, imo->imo_multicast_ifp->if_xname));

	if (imo->imo_num_memberships == 0) {
		struct in_addr addr;

		addr.s_addr = htonl(INADDR_CARP_GROUP);
		imo->imo_membership[0] = in_addmulti(&addr, &sc->sc_if);
		if (imo->imo_membership[0] == NULL) {
			carp_deactivate_vhaddr(sc, vha, FALSE);
			return ENOBUFS;
		}

		imo->imo_num_memberships++;
		imo->imo_multicast_ifp = &sc->sc_if;
		imo->imo_multicast_ttl = CARP_DFLTTL;
		imo->imo_multicast_loop = 0;
	}

	carp_hmac_prepare(sc);
	carp_set_state(sc, INIT);
	carp_setrun(sc, 0);
	return 0;
}

static void
carp_deactivate_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha,
    boolean_t del_iaback)
{
	KKASSERT(vha->vha_ia != NULL);

	carp_hmac_prepare(sc);

	if (vha->vha_iaback == NULL) {
		KASSERT((vha->vha_flags & CARP_VHAF_OWNER) == 0,
			("inactive vhaddr %p is the address owner", vha));
		return;
	}

	vha->vha_flags &= ~CARP_VHAF_OWNER;
	carp_delroute_vhaddr(sc, vha, del_iaback);

	KKASSERT(sc->sc_naddrs > 0);
	vha->vha_iaback = NULL;
	sc->sc_naddrs--;
	if (!sc->sc_naddrs) {
		if (sc->sc_naddrs6) {
			carp_multicast_cleanup(sc);
			sc->sc_ia = NULL;
		} else {
			carp_detach(sc, FALSE, del_iaback);
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

	/*
	 * Test each inactive vhaddr against the newly added address.
	 * If the newly added address could be the backing address,
	 * then activate the matching vhaddr.
	 */
	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link) {
		const struct in_ifaddr *ia;
		int own;

		if (vha->vha_iaback != NULL)
			continue;

		ia = vha->vha_ia;
		if (ia->ia_subnetmask != ia_if->ia_subnetmask ||
		    ia->ia_subnet != ia_if->ia_subnet)
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
			carp_deactivate_vhaddr(sc, vha, TRUE);
		else if (sc->sc_ia == NULL)
			sc->sc_ia = vha->vha_iaback;
	}
}

static void
carp_update_addrs(struct carp_softc *sc, struct ifaddr *ifa_del)
{
	struct carp_vhaddr *vha;

	KKASSERT(sc->sc_carpdev == NULL);

	TAILQ_FOREACH(vha, &sc->sc_vha_list, vha_link)
		carp_config_vhaddr(sc, vha, ifatoia(ifa_del));
}

static void
carp_ifaddr(void *arg __unused, struct ifnet *ifp,
	    enum ifaddr_event event, struct ifaddr *ifa)
{
	struct carp_softc *sc;

	if (ifa->ifa_addr->sa_family != AF_INET)
		return;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));

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
				if (sc->sc_carpdev == NULL) {
					/*
					 * We no longer have the parent
					 * interface, however, certain
					 * virtual addresses, which are
					 * not used because they can't
					 * match the previous parent
					 * interface's addresses, may now
					 * match different interface's
					 * addresses.
					 */
					carp_update_addrs(sc, ifa);
				}
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
				if (sc->sc_carpdev == NULL) {
					/*
					 * See the comment in the above
					 * IFADDR_EVENT_DELETE block.
					 */
					carp_update_addrs(sc, NULL);
				}
			}
			break;
		}
	}
}

void
carp_proto_ctlinput(netmsg_t msg)
{
	int cmd = msg->ctlinput.nm_cmd;
	struct sockaddr *sa = msg->ctlinput.nm_arg;
	struct in_ifaddr_container *iac;

	/* We only process PRC_IFDOWN and PRC_IFUP commands */
	if (cmd != PRC_IFDOWN && cmd != PRC_IFUP)
		goto done;

	TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
		struct in_ifaddr *ia = iac->ia;
		struct ifnet *ifp = ia->ia_ifp;

		if (ifp->if_type == IFT_CARP)
			continue;

		if (ia->ia_ifa.ifa_addr == sa) {
			if (cmd == PRC_IFDOWN) {
				carp_ifaddr(NULL, ifp, IFADDR_EVENT_DELETE,
				    &ia->ia_ifa);
			} else if (cmd == PRC_IFUP) {
				carp_ifaddr(NULL, ifp, IFADDR_EVENT_ADD,
				    &ia->ia_ifa);
			}
			break;
		}
	}
done:
	lwkt_replymsg(&msg->lmsg, 0);
}

struct ifnet *
carp_parent(struct ifnet *cifp)
{
	struct carp_softc *sc;

	KKASSERT(cifp->if_type == IFT_CARP);
	sc = cifp->if_softc;

	return sc->sc_carpdev;
}

#define rtinitflags(x) \
	(((x)->ia_ifp->if_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) \
		 ? RTF_HOST : 0)

static int
carp_addroute_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha)
{
	struct in_ifaddr *ia, *iaback;

	if (sc->sc_state != MASTER)
		return 0;

	ia = vha->vha_ia;
	KKASSERT(ia != NULL);

	iaback = vha->vha_iaback;
	KKASSERT(iaback != NULL);

	return rtchange(&iaback->ia_ifa, &ia->ia_ifa);
}

static void
carp_delroute_vhaddr(struct carp_softc *sc, struct carp_vhaddr *vha,
    boolean_t del_iaback)
{
	struct in_ifaddr *ia, *iaback;

	ia = vha->vha_ia;
	KKASSERT(ia != NULL);

	iaback = vha->vha_iaback;
	KKASSERT(iaback != NULL);

	if (!del_iaback && (iaback->ia_ifp->if_flags & IFF_UP)) {
		rtchange(&ia->ia_ifa, &iaback->ia_ifa);
		return;
	}

	rtinit(&ia->ia_ifa, RTM_DELETE, rtinitflags(ia));
	in_ifadown_force(&ia->ia_ifa, 1);
	ia->ia_flags &= ~IFA_ROUTE;
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
				      EVENTHANDLER_PRI_FIRST);
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
