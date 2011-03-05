/*
 * Copyright (c) 2000 Jason L. Wright (jason@thought.net)
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
 *      This product includes software developed by Jason L. Wright
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
 * $OpenBSD: bridgestp.c,v 1.5 2001/03/22 03:48:29 jason Exp $
 * $NetBSD: bridgestp.c,v 1.5 2003/11/28 08:56:48 keihan Exp $
 * $FreeBSD: src/sys/net/bridgestp.c,v 1.7 2005/10/11 02:58:32 thompsa Exp $
 */

/*
 * Implementation of the spanning tree protocol as defined in
 * ISO/IEC Final DIS 15802-3 (IEEE P802.1D/D17), May 25, 1998.
 * (In English: IEEE 802.1D, Draft 17, 1998)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_llc.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <net/bridge/if_bridgevar.h>

/* BPDU message types */
#define	BSTP_MSGTYPE_CFG	0x00		/* Configuration */
#define	BSTP_MSGTYPE_TCN	0x80		/* Topology chg notification */

/* BPDU flags */
#define	BSTP_FLAG_TC		0x01		/* Topology change */
#define	BSTP_FLAG_TCA		0x80		/* Topology change ack */

#define	BSTP_MESSAGE_AGE_INCR	(1 * 256)	/* in 256ths of a second */
#define	BSTP_TICK_VAL		(1 * 256)	/* in 256ths of a second */

/*
 * Because BPDU's do not make nicely aligned structures, two different
 * declarations are used: bstp_?bpdu (wire representation, packed) and
 * bstp_*_unit (internal, nicely aligned version).
 */

/* configuration bridge protocol data unit */
struct bstp_cbpdu {
	uint8_t		cbu_dsap;		/* LLC: destination sap */
	uint8_t		cbu_ssap;		/* LLC: source sap */
	uint8_t		cbu_ctl;		/* LLC: control */
	uint16_t	cbu_protoid;		/* protocol id */
	uint8_t		cbu_protover;		/* protocol version */
	uint8_t		cbu_bpdutype;		/* message type */
	uint8_t		cbu_flags;		/* flags (below) */

	/* root id */
	uint16_t	cbu_rootpri;		/* root priority */
	uint8_t	cbu_rootaddr[6];	/* root address */

	uint32_t	cbu_rootpathcost;	/* root path cost */

	/* bridge id */
	uint16_t	cbu_bridgepri;		/* bridge priority */
	uint8_t		cbu_bridgeaddr[6];	/* bridge address */

	uint16_t	cbu_portid;		/* port id */
	uint16_t	cbu_messageage;		/* current message age */
	uint16_t	cbu_maxage;		/* maximum age */
	uint16_t	cbu_hellotime;		/* hello time */
	uint16_t	cbu_forwarddelay;	/* forwarding delay */
} __attribute__((__packed__));

/* topology change notification bridge protocol data unit */
struct bstp_tbpdu {
	uint8_t		tbu_dsap;		/* LLC: destination sap */
	uint8_t		tbu_ssap;		/* LLC: source sap */
	uint8_t		tbu_ctl;		/* LLC: control */
	uint16_t	tbu_protoid;		/* protocol id */
	uint8_t		tbu_protover;		/* protocol version */
	uint8_t		tbu_bpdutype;		/* message type */
} __attribute__((__packed__));

const uint8_t bstp_etheraddr[] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };

static void	bstp_initialize_port(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_ifupdstatus(struct bridge_softc *, struct bridge_iflist *);
static void	bstp_enable_port(struct bridge_softc *, struct bridge_iflist *);
static void	bstp_disable_port(struct bridge_softc *,
		    struct bridge_iflist *);
#ifdef notused
static void	bstp_enable_change_detection(struct bridge_iflist *);
static void	bstp_disable_change_detection(struct bridge_iflist *);
#endif /* notused */
static int	bstp_root_bridge(struct bridge_softc *sc);
static void	bstp_transmit_config(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_transmit_tcn(struct bridge_softc *);
static void	bstp_received_config_bpdu(struct bridge_softc *,
		    struct bridge_iflist *, struct bstp_config_unit *);
static void	bstp_received_tcn_bpdu(struct bridge_softc *,
		    struct bridge_iflist *, struct bstp_tcn_unit *);
static void	bstp_record_config_information(struct bridge_softc *,
		    struct bridge_iflist *, struct bstp_config_unit *);
static void	bstp_record_config_timeout_values(struct bridge_softc *,
		    struct bstp_config_unit *);
static void	bstp_config_bpdu_generation(struct bridge_softc *);
static void	bstp_send_config_bpdu(struct bridge_softc *,
		    struct bridge_iflist *, struct bstp_config_unit *);
static void	bstp_configuration_update(struct bridge_softc *);
static void	bstp_port_state_selection(struct bridge_softc *);
static void	bstp_clear_peer_info(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_make_forwarding(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_make_blocking(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_make_l1blocking(struct bridge_softc *sc,
		    struct bridge_iflist *bif);
static void	bstp_adjust_bonded_states(struct bridge_softc *sc,
		    struct bridge_iflist *obif);
static void	bstp_set_port_state(struct bridge_iflist *, uint8_t);
#ifdef notused
static void	bstp_set_bridge_priority(struct bridge_softc *, uint64_t);
static void	bstp_set_port_priority(struct bridge_softc *,
		    struct bridge_iflist *, uint16_t);
static void	bstp_set_path_cost(struct bridge_softc *,
		    struct bridge_iflist *, uint32_t);
#endif /* notused */
static void	bstp_topology_change_detection(struct bridge_softc *);
static void	bstp_topology_change_acknowledged(struct bridge_softc *);
static void	bstp_acknowledge_topology_change(struct bridge_softc *,
		    struct bridge_iflist *);

static void	bstp_tick(void *);
static void	bstp_timer_start(struct bridge_timer *, uint16_t);
static void	bstp_timer_stop(struct bridge_timer *);
static int	bstp_timer_expired(struct bridge_timer *, uint16_t);

static void	bstp_hold_timer_expiry(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_message_age_timer_expiry(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_forward_delay_timer_expiry(struct bridge_softc *,
		    struct bridge_iflist *);
static void	bstp_topology_change_timer_expiry(struct bridge_softc *);
static void	bstp_tcn_timer_expiry(struct bridge_softc *);
static void	bstp_hello_timer_expiry(struct bridge_softc *);
static int	bstp_addr_cmp(const uint8_t *, const uint8_t *);

/*
 * When transmitting a config we tack on our path cost to
 * our aggregated path-to-root cost.
 */
static void
bstp_transmit_config(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	if (bif->bif_hold_timer.active) {
		bif->bif_config_pending = 1;
		return;
	}

	bif->bif_config_bpdu.cu_message_type = BSTP_MSGTYPE_CFG;
	bif->bif_config_bpdu.cu_rootid = sc->sc_designated_root;
	bif->bif_config_bpdu.cu_root_path_cost = sc->sc_designated_cost +
						 bif->bif_path_cost;
	bif->bif_config_bpdu.cu_bridge_id = sc->sc_bridge_id;
	bif->bif_config_bpdu.cu_port_id = bif->bif_port_id;

	if (bstp_root_bridge(sc)) {
		bif->bif_config_bpdu.cu_message_age = 0;
	} else if (sc->sc_root_port) {
		bif->bif_config_bpdu.cu_message_age =
			sc->sc_root_port->bif_message_age_timer.value +
			BSTP_MESSAGE_AGE_INCR;
	} else {
		bif->bif_config_bpdu.cu_message_age = BSTP_MESSAGE_AGE_INCR;
	}

	bif->bif_config_bpdu.cu_max_age = sc->sc_max_age;
	bif->bif_config_bpdu.cu_hello_time = sc->sc_hello_time;
	bif->bif_config_bpdu.cu_forward_delay = sc->sc_forward_delay;
	bif->bif_config_bpdu.cu_topology_change_acknowledgment
	    = bif->bif_topology_change_acknowledge;
	bif->bif_config_bpdu.cu_topology_change = sc->sc_topology_change;

	if (bif->bif_config_bpdu.cu_message_age < sc->sc_max_age ||
	    (sc->sc_ifp->if_flags & IFF_LINK1)) {
		bif->bif_topology_change_acknowledge = 0;
		bif->bif_config_pending = 0;
		bstp_send_config_bpdu(sc, bif, &bif->bif_config_bpdu);
		bstp_timer_start(&bif->bif_hold_timer, 0);
	}
}

static void
bstp_send_config_bpdu(struct bridge_softc *sc, struct bridge_iflist *bif,
		      struct bstp_config_unit *cu)
{
	struct ifnet *ifp;
	struct mbuf *m;
	struct ether_header *eh;
	struct bstp_cbpdu bpdu;

	ifp = bif->bif_ifp;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	MGETHDR(m, MB_DONTWAIT, MT_DATA);
	if (m == NULL)
		return;

	eh = mtod(m, struct ether_header *);

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = sizeof(*eh) + sizeof(bpdu);
	m->m_len = m->m_pkthdr.len;

	bpdu.cbu_ssap = bpdu.cbu_dsap = LLC_8021D_LSAP;
	bpdu.cbu_ctl = LLC_UI;
	bpdu.cbu_protoid = htons(0);
	bpdu.cbu_protover = 0;
	bpdu.cbu_bpdutype = cu->cu_message_type;
	bpdu.cbu_flags = (cu->cu_topology_change ? BSTP_FLAG_TC : 0) |
	    (cu->cu_topology_change_acknowledgment ? BSTP_FLAG_TCA : 0);

	bpdu.cbu_rootpri = htons(cu->cu_rootid >> 48);
	bpdu.cbu_rootaddr[0] = cu->cu_rootid >> 40;
	bpdu.cbu_rootaddr[1] = cu->cu_rootid >> 32;
	bpdu.cbu_rootaddr[2] = cu->cu_rootid >> 24;
	bpdu.cbu_rootaddr[3] = cu->cu_rootid >> 16;
	bpdu.cbu_rootaddr[4] = cu->cu_rootid >> 8;
	bpdu.cbu_rootaddr[5] = cu->cu_rootid >> 0;

	bpdu.cbu_rootpathcost = htonl(cu->cu_root_path_cost);

	bpdu.cbu_bridgepri = htons(cu->cu_bridge_id >> 48);
	bpdu.cbu_bridgeaddr[0] = cu->cu_bridge_id >> 40;
	bpdu.cbu_bridgeaddr[1] = cu->cu_bridge_id >> 32;
	bpdu.cbu_bridgeaddr[2] = cu->cu_bridge_id >> 24;
	bpdu.cbu_bridgeaddr[3] = cu->cu_bridge_id >> 16;
	bpdu.cbu_bridgeaddr[4] = cu->cu_bridge_id >> 8;
	bpdu.cbu_bridgeaddr[5] = cu->cu_bridge_id >> 0;

	bpdu.cbu_portid = htons(cu->cu_port_id);
	bpdu.cbu_messageage = htons(cu->cu_message_age);
	bpdu.cbu_maxage = htons(cu->cu_max_age);
	bpdu.cbu_hellotime = htons(cu->cu_hello_time);
	bpdu.cbu_forwarddelay = htons(cu->cu_forward_delay);

	/*
	 * Packets sent from the bridge always use the bridge MAC
	 * as the source.
	 */
	memcpy(eh->ether_shost, IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN);
	memcpy(eh->ether_dhost, bstp_etheraddr, ETHER_ADDR_LEN);
	eh->ether_type = htons(sizeof(bpdu));

	memcpy(mtod(m, caddr_t) + sizeof(*eh), &bpdu, sizeof(bpdu));

	bridge_enqueue(ifp, m);
}

static int
bstp_root_bridge(struct bridge_softc *sc)
{
	return (sc->sc_designated_root == sc->sc_bridge_id);
}

/*
 * Returns TRUE if the recorded information from our peer has a shorter
 * graph distance than our current best.
 */
int
bstp_supersedes_port_info(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	if (bif->bif_peer_root < sc->sc_designated_root)
		return (1);
	if (bif->bif_peer_root > sc->sc_designated_root)
		return (0);

	/*
	 * Both bif_peer_cost and sc_designated_cost have NOT added in
	 * bif->bif_path_cost, so we can optimize it out.
	 */
	if (bif->bif_peer_cost < sc->sc_designated_cost)
		return (1);
	if (bif->bif_peer_cost > sc->sc_designated_cost)
		return (0);

	if (bif->bif_peer_bridge < sc->sc_designated_bridge)
		return (1);
	if (bif->bif_peer_bridge > sc->sc_designated_bridge)
		return (0);

	/* bridge_id or bridge+port collision w/peer returns TRUE */
	if (bif->bif_peer_bridge != sc->sc_bridge_id)
		return (1);
	if (bif->bif_peer_port <= sc->sc_designated_port)
		return (1);
	return (0);
}

/*
 * The shorter graph distance represented by cu (most of which is also
 * already stored in our bif_peer_* fields) becomes the designated info.
 *
 * NOTE: sc_designated_cost does not include bif_path_cost, it is added
 *	 in later on a port-by-port basis as needed.
 */
static void
bstp_record_config_information(struct bridge_softc *sc,
			       struct bridge_iflist *bif,
			       struct bstp_config_unit *cu)
{
	sc->sc_designated_root = bif->bif_peer_root;
	sc->sc_designated_cost = bif->bif_peer_cost;
	sc->sc_designated_bridge = bif->bif_peer_bridge;
	sc->sc_designated_port = bif->bif_peer_port;
	bstp_timer_start(&bif->bif_message_age_timer, cu->cu_message_age);
}

static void
bstp_record_config_timeout_values(struct bridge_softc *sc,
				  struct bstp_config_unit *config)
{
	sc->sc_max_age = config->cu_max_age;
	sc->sc_hello_time = config->cu_hello_time;
	sc->sc_forward_delay = config->cu_forward_delay;
	sc->sc_topology_change = config->cu_topology_change;
}

static void
bstp_config_bpdu_generation(struct bridge_softc *sc)
{
	struct bridge_iflist *bif, *nbif;

	TAILQ_FOREACH_MUTABLE(bif, &sc->sc_iflists[mycpuid], bif_next, nbif) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bif->bif_state != BSTP_IFSTATE_DISABLED &&
		    ((sc->sc_ifp->if_flags & IFF_LINK1) ||
		     (bif->bif_flags & IFBIF_DESIGNATED))) {
			bstp_transmit_config(sc, bif);
		}

		if (nbif != NULL && !nbif->bif_onlist) {
			KKASSERT(bif->bif_onlist);
			nbif = TAILQ_NEXT(bif, bif_next);
		}
	}
}

static void
bstp_transmit_tcn(struct bridge_softc *sc)
{
	struct bstp_tbpdu bpdu;
	struct ifnet *ifp;
	struct ether_header *eh;
	struct mbuf *m;

	if (sc->sc_root_port == NULL)	/* all iterfaces disabled */
		return;

	ifp = sc->sc_root_port->bif_ifp;
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	MGETHDR(m, MB_DONTWAIT, MT_DATA);
	if (m == NULL)
		return;

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = sizeof(*eh) + sizeof(bpdu);
	m->m_len = m->m_pkthdr.len;

	eh = mtod(m, struct ether_header *);

	/*
	 * Packets sent from the bridge always use the bridge MAC
	 * as the source.
	 */
	memcpy(eh->ether_shost, IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN);
	memcpy(eh->ether_dhost, bstp_etheraddr, ETHER_ADDR_LEN);
	eh->ether_type = htons(sizeof(bpdu));

	bpdu.tbu_ssap = bpdu.tbu_dsap = LLC_8021D_LSAP;
	bpdu.tbu_ctl = LLC_UI;
	bpdu.tbu_protoid = 0;
	bpdu.tbu_protover = 0;
	bpdu.tbu_bpdutype = BSTP_MSGTYPE_TCN;

	memcpy(mtod(m, caddr_t) + sizeof(*eh), &bpdu, sizeof(bpdu));

	bridge_enqueue(ifp, m);
}

/*
 * Recalculate sc->sc_designated* and sc->sc_root_port (if our bridge
 * is calculated to be the root bridge).  We do this by initializing
 * the designated variables to point at us and then scan our peers.
 * Any uninitialized peers will have a max-value root.
 *
 * Clear IFBIF_DESIGNATED on any ports which no longer match the criteria
 * required to be a designated port.  Only aged out ports and the root
 * port can be designated.
 *
 * If we win we do a second scan to determine which port on our bridge
 * is the best.
 */
static void
bstp_configuration_update(struct bridge_softc *sc)
{
	uint64_t	designated_root = sc->sc_bridge_id;
	uint64_t	designated_bridge = sc->sc_bridge_id;
	uint32_t	designated_cost = 0xFFFFFFFFU;
	uint32_t	designated_root_cost = 0xFFFFFFFFU;
	uint16_t	designated_port = 65535;
	struct bridge_iflist *root_port = NULL;
	struct bridge_iflist *bif;

	/*
	 * Resolve information from our peers.  Aged peers will have
	 * a maxed bif_peer_root and not come under consideration.
	 */
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bif->bif_state == BSTP_IFSTATE_DISABLED ||
		    bif->bif_state == BSTP_IFSTATE_L1BLOCKING) {
			continue;
		}

		if (bif->bif_peer_root > designated_root)
			continue;
		if (bif->bif_peer_root < designated_root)
			goto set_port;

		/*
		 * NOTE: The designated_cost temporary variable already added
		 *	 in the path code of the related root port.
		 */
		if (bif->bif_peer_cost + bif->bif_path_cost > designated_cost)
			continue;
		if (bif->bif_peer_cost + bif->bif_path_cost < designated_cost)
			goto set_port;

		if (bif->bif_peer_bridge > designated_bridge)
			continue;
		if (bif->bif_peer_bridge < designated_bridge)
			goto set_port;

		if (bif->bif_peer_port > designated_port)
			continue;
		if (bif->bif_peer_port < designated_port)
			goto set_port;

		/*
		 * Same root, path cost, bridge, and port.  Set the root
		 * only if we do not already have it.
		 */
		if (root_port)
			continue;

		/*
		 * New root port (from peers)
		 *
		 * NOTE: Temporarily add bif_path_cost into the designated
		 *	 cost to reduce complexity in the loop, it will be
		 *	 subtracted out when we are done.
		 */
set_port:
		designated_root = bif->bif_peer_root;
		designated_cost = bif->bif_peer_cost + bif->bif_path_cost;
		designated_root_cost = bif->bif_peer_cost;
		designated_bridge = bif->bif_peer_bridge;
		designated_port = bif->bif_peer_port;
		root_port = bif;
	}

	/*
	 * root_port will be NULL at the start here if all of our
	 * peers are aged or are not as good a root as our bridge would
	 * be.  It can also be NULL due to all related links being
	 * disabled.
	 *
	 * If the root winds up being our bridge scan again against local
	 * information.  Unconditionally update IFBIF_DESIGNATED.
	 */
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		bif->bif_flags &= ~(IFBIF_DESIGNATED | IFBIF_ROOT);
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bif->bif_state == BSTP_IFSTATE_DISABLED ||
		    bif->bif_state == BSTP_IFSTATE_L1BLOCKING) {
			continue;
		}

		/*
		 * Set DESIGNATED for an aged or unknown peer.
		 */
		if (bif->bif_peer_bridge == 0xFFFFFFFFFFFFFFFFLLU)
			bif->bif_flags |= IFBIF_DESIGNATED;
		if (designated_root != sc->sc_bridge_id)
			continue;

		/*
		 * This is only reached if our bridge is the root bridge,
		 * select the root port (IFBIF_DESIGNATED is set at the
		 * end).
		 *
		 * Since we are the root the peer cost should already include
		 * our path cost.  We still need the combined costs from both
		 * our point of view and the peer's point of view to match
		 * up with the peer.
		 *
		 * If we used ONLY our path cost here we would have no peer
		 * path cost in the calculation and would reach a different
		 * conclusion than our peer has reached.
		 */
		if (bif->bif_peer_cost > designated_cost)
			continue;
		if (bif->bif_peer_cost < designated_cost)
			goto set_port2;

		if (bif->bif_port_id > designated_port)
			continue;
		if (bif->bif_port_id < designated_port)
			goto set_port2;
		/* degenerate case (possible peer collision w/our key */

		/*
		 * New port.  Since we are the root, the root cost is always
		 * 0.  Since we are the root the peer path should be our
		 * path cost + peer path cost.
		 */
set_port2:
		designated_cost = bif->bif_peer_cost;
		designated_root_cost = 0;
		designated_bridge = sc->sc_bridge_id;
		designated_port = bif->bif_port_id;
		root_port = bif;
	}

	/*
	 * Update aggregate information.  The selected root port always
	 * becomes a designated port (along with aged ports).  This can
	 * either be the port whos peer is closest to the root or it
	 * can be one of our ports if our bridge is the root.
	 *
	 * The root cost we record in sc_designated_root does not include
	 * bif_path_cost of the root port, since we may transmit this
	 * out of any port we will add the cost back in later on on
	 * a per-port basis.
	 *
	 * root_port can be NULL here (if all links are disabled)
	 */
	if (root_port) {
		sc->sc_designated_root = designated_root;
		sc->sc_designated_cost = designated_root_cost;
		sc->sc_designated_bridge = designated_bridge;
		sc->sc_designated_port = designated_port;
		root_port->bif_flags |= IFBIF_DESIGNATED | IFBIF_ROOT;
	} else {
		sc->sc_designated_root = designated_root;
		sc->sc_designated_cost = designated_cost;
		sc->sc_designated_bridge = designated_bridge;
		sc->sc_designated_port = designated_port;
	}
	sc->sc_root_port = root_port;
}

/*
 * Calculate the desired state for each interface link on our bridge.
 *
 * The best port will match against sc->sc_root_port (whether we are root
 * or whether that port is the closest to the root).  We push this port
 * towards a FORWARDING state.
 *
 * Next come designated ports, either aged ports or ports with no peer info
 * (yet), or the peer who is closest to the root. We push this port towards
 * a FORWARDING state as well.
 *
 * Any remaining ports are pushed towards a BLOCKING state.  Both sides of
 * the port (us and our peer) should wind up placing the two ends in this
 * state or bad things happen.
 */
static void
bstp_port_state_selection(struct bridge_softc *sc)
{
	struct bridge_iflist *bif, *nbif;

	TAILQ_FOREACH_MUTABLE(bif, &sc->sc_iflists[mycpuid], bif_next, nbif) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (sc->sc_root_port &&
		    bif->bif_info == sc->sc_root_port->bif_info) {
			bif->bif_config_pending = 0;
			bif->bif_topology_change_acknowledge = 0;
			bstp_make_forwarding(sc, bif);
		} else if (bif->bif_flags & IFBIF_DESIGNATED) {
			bstp_timer_stop(&bif->bif_message_age_timer);
			bstp_make_forwarding(sc, bif);
		} else {
			bif->bif_config_pending = 0;
			bif->bif_topology_change_acknowledge = 0;
			bstp_make_blocking(sc, bif);
		}

		if (nbif != NULL && !nbif->bif_onlist) {
			KKASSERT(bif->bif_onlist);
			nbif = TAILQ_NEXT(bif, bif_next);
		}
	}
}

/*
 * Clear peer info, effectively makes the port looked aged out.
 * It becomes a designated go-to port.
 */
static void
bstp_clear_peer_info(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	bif->bif_peer_root = 0xFFFFFFFFFFFFFFFFLLU;
	bif->bif_peer_cost = 0xFFFFFFFFU;
	bif->bif_peer_bridge = 0xFFFFFFFFFFFFFFFFLLU;
	bif->bif_peer_port = 0xFFFFU;

	if (bif->bif_state != BSTP_IFSTATE_DISABLED &&
	    bif->bif_state != BSTP_IFSTATE_L1BLOCKING) {
		bif->bif_flags |= IFBIF_DESIGNATED;
	}
}

static void
bstp_make_forwarding(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	if (bif->bif_state == BSTP_IFSTATE_BLOCKING ||
	    bif->bif_state == BSTP_IFSTATE_BONDED) {
		bstp_set_port_state(bif, BSTP_IFSTATE_LISTENING);
		bstp_timer_start(&bif->bif_forward_delay_timer, 0);
	}
}

static void
bstp_make_blocking(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	if (bif->bif_state != BSTP_IFSTATE_DISABLED &&
	    bif->bif_state != BSTP_IFSTATE_BLOCKING &&
	    bif->bif_state != BSTP_IFSTATE_BONDED &&
	    bif->bif_state != BSTP_IFSTATE_L1BLOCKING) {
		if ((bif->bif_state == BSTP_IFSTATE_FORWARDING) ||
		    (bif->bif_state == BSTP_IFSTATE_LEARNING)) {
			if (bif->bif_change_detection_enabled) {
				bstp_topology_change_detection(sc);
			}
		}
		bstp_set_port_state(bif, BSTP_IFSTATE_BLOCKING);
		bridge_rtdelete(sc, bif->bif_ifp, IFBF_FLUSHDYN);
		bstp_timer_stop(&bif->bif_forward_delay_timer);
		if (sc->sc_ifp->if_flags & IFF_LINK2)
			bstp_adjust_bonded_states(sc, bif);
	}
}

static void
bstp_make_l1blocking(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	int was_forwarding = (bif->bif_state == BSTP_IFSTATE_FORWARDING);

	switch(bif->bif_state) {
	case BSTP_IFSTATE_LISTENING:
	case BSTP_IFSTATE_LEARNING:
	case BSTP_IFSTATE_FORWARDING:
	case BSTP_IFSTATE_BLOCKING:
	case BSTP_IFSTATE_BONDED:
		bstp_set_port_state(bif, BSTP_IFSTATE_L1BLOCKING);
		bridge_rtdelete(sc, bif->bif_ifp, IFBF_FLUSHDYN);
		bstp_timer_stop(&bif->bif_forward_delay_timer);
		bstp_timer_stop(&bif->bif_link1_timer);
		if (bif->bif_flags & IFBIF_DESIGNATED) {
			bif->bif_flags &= ~IFBIF_DESIGNATED;
			bstp_configuration_update(sc);
			bstp_port_state_selection(sc);
		}
		if (was_forwarding && (sc->sc_ifp->if_flags & IFF_LINK2))
			bstp_adjust_bonded_states(sc, bif);
		break;
	default:
		break;
	}
}

/*
 * Member (bif) changes to or from a FORWARDING state.  All members in the
 * same bonding group which are in a BLOCKING or BONDED state must be set
 * to either BLOCKING or BONDED based on whether any members in the bonding
 * group remain in the FORWARDING state.
 *
 * Going between the BLOCKING and BONDED states does not require a
 * configuration update.
 */
static void
bstp_adjust_bonded_states(struct bridge_softc *sc, struct bridge_iflist *obif)
{
	struct bridge_iflist *bif;
	int state = BSTP_IFSTATE_BLOCKING;

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bif->bif_state != BSTP_IFSTATE_FORWARDING)
			continue;
		if (memcmp(IF_LLADDR(bif->bif_ifp), IF_LLADDR(obif->bif_ifp),
			   ETHER_ADDR_LEN) != 0) {
			continue;
		}
		state = BSTP_IFSTATE_BONDED;
		break;
	}
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bif->bif_state != BSTP_IFSTATE_BLOCKING &&
		    bif->bif_state != BSTP_IFSTATE_BONDED) {
			continue;
		}
		if (memcmp(IF_LLADDR(bif->bif_ifp), IF_LLADDR(obif->bif_ifp),
			   ETHER_ADDR_LEN) != 0) {
			continue;
		}
		if (bif->bif_bond_weight == 0)
			bif->bif_state = BSTP_IFSTATE_BLOCKING;
		else
			bif->bif_state = state;
	}
}

static void
bstp_set_port_state(struct bridge_iflist *bif, uint8_t state)
{
	bif->bif_state = state;
}

static void
bstp_topology_change_detection(struct bridge_softc *sc)
{
	if (bstp_root_bridge(sc)) {
		sc->sc_topology_change = 1;
		bstp_timer_start(&sc->sc_topology_change_timer, 0);
	} else if (!sc->sc_topology_change_detected) {
		bstp_transmit_tcn(sc);
		bstp_timer_start(&sc->sc_tcn_timer, 0);
	}
	sc->sc_topology_change_detected = 1;
}

static void
bstp_topology_change_acknowledged(struct bridge_softc *sc)
{
	sc->sc_topology_change_detected = 0;
	bstp_timer_stop(&sc->sc_tcn_timer);
}

static void
bstp_acknowledge_topology_change(struct bridge_softc *sc,
				 struct bridge_iflist *bif)
{
	bif->bif_topology_change_acknowledge = 1;
	bstp_transmit_config(sc, bif);
}

void
bstp_input(struct bridge_softc *sc, struct bridge_iflist *bif, struct mbuf *m)
{
	struct ether_header *eh;
	struct bstp_tbpdu tpdu;
	struct bstp_cbpdu cpdu;
	struct bstp_config_unit cu;
	struct bstp_tcn_unit tu;
	uint16_t len;

	if ((bif->bif_flags & IFBIF_STP) == 0)
		goto out;

	/*
	 * The L1BLOCKING (ping pong failover) test needs to reset the
	 * timer if LINK1 is active.
	 */
	if (bif->bif_state == BSTP_IFSTATE_L1BLOCKING) {
		bstp_set_port_state(bif, BSTP_IFSTATE_BLOCKING);
		if (sc->sc_ifp->if_flags & IFF_LINK1)
			bstp_timer_start(&bif->bif_link1_timer, 0);
		bstp_make_forwarding(sc, bif);
	} else if (sc->sc_ifp->if_flags & IFF_LINK1) {
		bstp_timer_start(&bif->bif_link1_timer, 0);
	}

	eh = mtod(m, struct ether_header *);

	len = ntohs(eh->ether_type);
	if (len < sizeof(tpdu))
		goto out;

	m_adj(m, ETHER_HDR_LEN);

	if (m->m_pkthdr.len > len)
		m_adj(m, len - m->m_pkthdr.len);
	if (m->m_len < sizeof(tpdu) &&
	    (m = m_pullup(m, sizeof(tpdu))) == NULL)
		goto out;

	memcpy(&tpdu, mtod(m, caddr_t), sizeof(tpdu));

	if (tpdu.tbu_dsap != LLC_8021D_LSAP ||
	    tpdu.tbu_ssap != LLC_8021D_LSAP ||
	    tpdu.tbu_ctl != LLC_UI)
		goto out;
	if (tpdu.tbu_protoid != 0 || tpdu.tbu_protover != 0)
		goto out;

	switch (tpdu.tbu_bpdutype) {
	case BSTP_MSGTYPE_TCN:
		tu.tu_message_type = tpdu.tbu_bpdutype;
		bstp_received_tcn_bpdu(sc, bif, &tu);
		break;
	case BSTP_MSGTYPE_CFG:
		if (m->m_len < sizeof(cpdu) &&
		    (m = m_pullup(m, sizeof(cpdu))) == NULL)
			goto out;
		memcpy(&cpdu, mtod(m, caddr_t), sizeof(cpdu));

		cu.cu_rootid =
		    (((uint64_t)ntohs(cpdu.cbu_rootpri)) << 48) |
		    (((uint64_t)cpdu.cbu_rootaddr[0]) << 40) |
		    (((uint64_t)cpdu.cbu_rootaddr[1]) << 32) |
		    (((uint64_t)cpdu.cbu_rootaddr[2]) << 24) |
		    (((uint64_t)cpdu.cbu_rootaddr[3]) << 16) |
		    (((uint64_t)cpdu.cbu_rootaddr[4]) << 8) |
		    (((uint64_t)cpdu.cbu_rootaddr[5]) << 0);

		cu.cu_bridge_id =
		    (((uint64_t)ntohs(cpdu.cbu_bridgepri)) << 48) |
		    (((uint64_t)cpdu.cbu_bridgeaddr[0]) << 40) |
		    (((uint64_t)cpdu.cbu_bridgeaddr[1]) << 32) |
		    (((uint64_t)cpdu.cbu_bridgeaddr[2]) << 24) |
		    (((uint64_t)cpdu.cbu_bridgeaddr[3]) << 16) |
		    (((uint64_t)cpdu.cbu_bridgeaddr[4]) << 8) |
		    (((uint64_t)cpdu.cbu_bridgeaddr[5]) << 0);

		cu.cu_root_path_cost = ntohl(cpdu.cbu_rootpathcost);
		cu.cu_message_age = ntohs(cpdu.cbu_messageage);
		cu.cu_max_age = ntohs(cpdu.cbu_maxage);
		cu.cu_hello_time = ntohs(cpdu.cbu_hellotime);
		cu.cu_forward_delay = ntohs(cpdu.cbu_forwarddelay);
		cu.cu_port_id = ntohs(cpdu.cbu_portid);
		cu.cu_message_type = cpdu.cbu_bpdutype;
		cu.cu_topology_change_acknowledgment =
		    (cpdu.cbu_flags & BSTP_FLAG_TCA) ? 1 : 0;
		cu.cu_topology_change =
		    (cpdu.cbu_flags & BSTP_FLAG_TC) ? 1 : 0;
		bstp_received_config_bpdu(sc, bif, &cu);
		break;
	default:
		goto out;
	}
out:
	if (m)
		m_freem(m);
}

static void
bstp_received_config_bpdu(struct bridge_softc *sc, struct bridge_iflist *bif,
			  struct bstp_config_unit *cu)
{
	int iamroot;

	iamroot = bstp_root_bridge(sc);

	if (bif->bif_state != BSTP_IFSTATE_DISABLED) {
		/*
		 * Record information from peer.  The peer_cost field
		 * does not include the local bif->bif_path_cost, it will
		 * be added in as needed (since it can be modified manually
		 * this way we don't have to worry about fixups).
		 */
		bif->bif_peer_root = cu->cu_rootid;
		bif->bif_peer_cost = cu->cu_root_path_cost;
		bif->bif_peer_bridge = cu->cu_bridge_id;
		bif->bif_peer_port = cu->cu_port_id;

		if (bstp_supersedes_port_info(sc, bif)) {
			bstp_record_config_information(sc, bif, cu);
			bstp_configuration_update(sc);
			bstp_port_state_selection(sc);

			/*
			 * If our bridge loses its root status (?)
			 *
			 * Hello's (unsolicited CFG packets) are generated
			 * every hello period of LINK1 is set, otherwise
			 * we are no longer the root bridge and must stop
			 * generating unsolicited CFG packets.
			 */
			if (iamroot && bstp_root_bridge(sc) == 0) {
				if ((sc->sc_ifp->if_flags & IFF_LINK1) == 0)
					bstp_timer_stop(&sc->sc_hello_timer);

				if (sc->sc_topology_change_detected) {
					bstp_timer_stop(
					    &sc->sc_topology_change_timer);
					bstp_transmit_tcn(sc);
					bstp_timer_start(&sc->sc_tcn_timer, 0);
				}
			}

			if (sc->sc_root_port &&
			    bif->bif_info == sc->sc_root_port->bif_info) {
				bstp_record_config_timeout_values(sc, cu);
				bstp_config_bpdu_generation(sc);

				if (cu->cu_topology_change_acknowledgment)
					bstp_topology_change_acknowledged(sc);
			}
		} else if (bif->bif_flags & IFBIF_DESIGNATED) {
			/*
			 * Update designated ports (aged out peers or
			 * the port closest to the root) at a faster pace.
			 *
			 * Clear our designated flag if we aren't marked
			 * as the root port.
			 */
			bstp_transmit_config(sc, bif);
			if ((bif->bif_flags & IFBIF_ROOT) == 0) {
				bif->bif_flags &= ~IFBIF_DESIGNATED;
				bstp_configuration_update(sc);
				bstp_port_state_selection(sc);
			}
		}
	}
}

static void
bstp_received_tcn_bpdu(struct bridge_softc *sc, struct bridge_iflist *bif,
		       struct bstp_tcn_unit *tcn)
{
	if (bif->bif_state != BSTP_IFSTATE_DISABLED &&
	    (bif->bif_flags & IFBIF_DESIGNATED)) {
		bstp_topology_change_detection(sc);
		bstp_acknowledge_topology_change(sc, bif);
	}
}

/*
 * link1 forces continuous hello's (the bridge interface must be cycled
 * to start them up), so keep the timer hot if that is the case, otherwise
 * only send HELLO's if we are the root.
 */
static void
bstp_hello_timer_expiry(struct bridge_softc *sc)
{
	bstp_config_bpdu_generation(sc);

	if ((sc->sc_ifp->if_flags & IFF_LINK1) || bstp_root_bridge(sc))
		bstp_timer_start(&sc->sc_hello_timer, 0);
}

static void
bstp_message_age_timer_expiry(struct bridge_softc *sc,
			      struct bridge_iflist *bif)
{
	int iamroot;

	iamroot = bstp_root_bridge(sc);
	bstp_clear_peer_info(sc, bif);
	bstp_configuration_update(sc);
	bstp_port_state_selection(sc);

	/*
	 * If we've become the root and were not the root before
	 * we have some cleanup to do.  This also occurs if we
	 * wind up being completely isolated.
	 */
	if (iamroot == 0 && bstp_root_bridge(sc)) {
		sc->sc_max_age = sc->sc_bridge_max_age;
		sc->sc_hello_time = sc->sc_bridge_hello_time;
		sc->sc_forward_delay = sc->sc_bridge_forward_delay;

		bstp_topology_change_detection(sc);
		bstp_timer_stop(&sc->sc_tcn_timer);
		bstp_config_bpdu_generation(sc);
		bstp_timer_start(&sc->sc_hello_timer, 0);
	}
}

static void
bstp_forward_delay_timer_expiry(struct bridge_softc *sc,
				struct bridge_iflist *bif)
{
	if (bif->bif_state == BSTP_IFSTATE_LISTENING) {
		bstp_set_port_state(bif, BSTP_IFSTATE_LEARNING);
		bstp_timer_start(&bif->bif_forward_delay_timer, 0);
	} else if (bif->bif_state == BSTP_IFSTATE_LEARNING) {
		bstp_set_port_state(bif, BSTP_IFSTATE_FORWARDING);
		if (sc->sc_designated_bridge == sc->sc_bridge_id &&
		    bif->bif_change_detection_enabled) {
			bstp_topology_change_detection(sc);
		}
		if (sc->sc_ifp->if_flags & IFF_LINK2)
			bstp_adjust_bonded_states(sc, bif);
	}
}

static void
bstp_tcn_timer_expiry(struct bridge_softc *sc)
{
	bstp_transmit_tcn(sc);
	bstp_timer_start(&sc->sc_tcn_timer, 0);
}

static void
bstp_topology_change_timer_expiry(struct bridge_softc *sc)
{
	sc->sc_topology_change_detected = 0;
	sc->sc_topology_change = 0;
}

static void
bstp_hold_timer_expiry(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	if (bif->bif_config_pending)
		bstp_transmit_config(sc, bif);
}

/*
 * If no traffic received directly on this port for the specified
 * period with link1 set we go into a special blocking mode to
 * fail-over traffic to another port.
 */
static void
bstp_link1_timer_expiry(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	if (sc->sc_ifp->if_flags & IFF_LINK1)
		bstp_make_l1blocking(sc, bif);
}

static int
bstp_addr_cmp(const uint8_t *a, const uint8_t *b)
{
	int i, d;

	for (i = 0, d = 0; i < ETHER_ADDR_LEN && d == 0; i++) {
		d = ((int)a[i]) - ((int)b[i]);
	}

	return (d);
}

void
bstp_initialization(struct bridge_softc *sc)
{
	struct bridge_iflist *bif, *mif, *nbif;
	u_char *e_addr;

	KKASSERT(&curthread->td_msgport == BRIDGE_CFGPORT);

	/*
	 * Figure out our bridge ID, use the lowest-valued MAC.
	 * Include the bridge's own random MAC in the calculation.
	 */
	mif = NULL;

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bif->bif_ifp->if_type != IFT_ETHER)
			continue;
		if (mif == NULL) {
			mif = bif;
			continue;
		}

		bif->bif_port_id = (bif->bif_priority << 8) |
				   (bif->bif_ifp->if_index & 0xff);
		if (bstp_addr_cmp(IF_LLADDR(bif->bif_ifp),
				  IF_LLADDR(mif->bif_ifp)) < 0) {
			mif = bif;
			continue;
		}
	}
	if (mif == NULL) {
		bstp_stop(sc);
		return;
	}

	if (bstp_addr_cmp(IF_LLADDR(sc->sc_ifp), IF_LLADDR(mif->bif_ifp)) < 0)
		e_addr = IF_LLADDR(sc->sc_ifp);
	else
		e_addr = IF_LLADDR(mif->bif_ifp);

	sc->sc_bridge_id =
	    (((uint64_t)sc->sc_bridge_priority) << 48) |
	    (((uint64_t)e_addr[0]) << 40) |
	    (((uint64_t)e_addr[1]) << 32) |
	    (((uint64_t)e_addr[2]) << 24) |
	    (((uint64_t)e_addr[3]) << 16) |
	    (((uint64_t)e_addr[4]) << 8) |
	    (((uint64_t)e_addr[5]));

	/*
	 * Remainder of setup.
	 */

	sc->sc_designated_root = sc->sc_bridge_id;
	sc->sc_designated_cost = 0;
	sc->sc_root_port = NULL;

	sc->sc_max_age = sc->sc_bridge_max_age;
	sc->sc_hello_time = sc->sc_bridge_hello_time;
	sc->sc_forward_delay = sc->sc_bridge_forward_delay;
	sc->sc_topology_change_detected = 0;
	sc->sc_topology_change = 0;
	bstp_timer_stop(&sc->sc_tcn_timer);
	bstp_timer_stop(&sc->sc_topology_change_timer);

	if (callout_pending(&sc->sc_bstpcallout) == 0)
		callout_reset(&sc->sc_bstpcallout, hz,
		    bstp_tick, sc);

	TAILQ_FOREACH_MUTABLE(bif, &sc->sc_iflists[mycpuid], bif_next, nbif) {
		if (sc->sc_ifp->if_flags & IFF_LINK1)
			bstp_timer_start(&bif->bif_link1_timer, 0);
		if (bif->bif_flags & IFBIF_STP)
			bstp_ifupdstatus(sc, bif);
		else
			bstp_disable_port(sc, bif);

		if (nbif != NULL && !nbif->bif_onlist) {
			KKASSERT(bif->bif_onlist);
			nbif = TAILQ_NEXT(bif, bif_next);
		}
	}

	bstp_port_state_selection(sc);
	bstp_config_bpdu_generation(sc);
	bstp_timer_start(&sc->sc_hello_timer, 0);
}

void
bstp_stop(struct bridge_softc *sc)
{
	struct bridge_iflist *bif;
	struct lwkt_msg *lmsg;

	KKASSERT(&curthread->td_msgport == BRIDGE_CFGPORT);

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		bstp_set_port_state(bif, BSTP_IFSTATE_DISABLED);
		bstp_timer_stop(&bif->bif_hold_timer);
		bstp_timer_stop(&bif->bif_message_age_timer);
		bstp_timer_stop(&bif->bif_forward_delay_timer);
		bstp_timer_stop(&bif->bif_link1_timer);
	}

	callout_stop(&sc->sc_bstpcallout);

	bstp_timer_stop(&sc->sc_topology_change_timer);
	bstp_timer_stop(&sc->sc_tcn_timer);
	bstp_timer_stop(&sc->sc_hello_timer);

	crit_enter();
	lmsg = &sc->sc_bstptimemsg.lmsg;
	if ((lmsg->ms_flags & MSGF_DONE) == 0) {
		/* Pending to be processed; drop it */
		lwkt_dropmsg(lmsg);
	}
	crit_exit();
}

static void
bstp_initialize_port(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	int needs_adjust = (bif->bif_state == BSTP_IFSTATE_FORWARDING ||
			    bif->bif_state == BSTP_IFSTATE_BLOCKING ||
			    bif->bif_state == BSTP_IFSTATE_BONDED);

	bstp_set_port_state(bif, BSTP_IFSTATE_BLOCKING);
	bstp_clear_peer_info(sc, bif);
	bif->bif_topology_change_acknowledge = 0;
	bif->bif_config_pending = 0;
	bif->bif_change_detection_enabled = 1;
	bstp_timer_stop(&bif->bif_message_age_timer);
	bstp_timer_stop(&bif->bif_forward_delay_timer);
	bstp_timer_stop(&bif->bif_hold_timer);
	bstp_timer_stop(&bif->bif_link1_timer);
	if (needs_adjust && (sc->sc_ifp->if_flags & IFF_LINK2))
		bstp_adjust_bonded_states(sc, bif);
}

static void
bstp_enable_port(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	bstp_initialize_port(sc, bif);
	if (sc->sc_ifp->if_flags & IFF_LINK1)
		bstp_timer_start(&bif->bif_link1_timer, 0);
	bstp_configuration_update(sc);
	bstp_port_state_selection(sc);
}

static void
bstp_disable_port(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	int was_forwarding = (bif->bif_state == BSTP_IFSTATE_FORWARDING);
	int iamroot;

	iamroot = bstp_root_bridge(sc);

	bstp_clear_peer_info(sc, bif);
	bstp_set_port_state(bif, BSTP_IFSTATE_DISABLED);
	bif->bif_topology_change_acknowledge = 0;
	bif->bif_config_pending = 0;
	bstp_timer_stop(&bif->bif_message_age_timer);
	bstp_timer_stop(&bif->bif_forward_delay_timer);
	bstp_timer_stop(&bif->bif_link1_timer);
	bstp_configuration_update(sc);
	bstp_port_state_selection(sc);
	bridge_rtdelete(sc, bif->bif_ifp, IFBF_FLUSHDYN);
	if (was_forwarding && (sc->sc_ifp->if_flags & IFF_LINK2))
		bstp_adjust_bonded_states(sc, bif);

	if (iamroot == 0 && bstp_root_bridge(sc)) {
		sc->sc_max_age = sc->sc_bridge_max_age;
		sc->sc_hello_time = sc->sc_bridge_hello_time;
		sc->sc_forward_delay = sc->sc_bridge_forward_delay;

		bstp_topology_change_detection(sc);
		bstp_timer_stop(&sc->sc_tcn_timer);
		bstp_config_bpdu_generation(sc);
		bstp_timer_start(&sc->sc_hello_timer, 0);
	}
}

void
bstp_linkstate(struct ifnet *ifp, int state)
{
	struct bridge_softc *sc;
	struct bridge_iflist *bif;

	sc = ifp->if_bridge;
	ifnet_serialize_all(sc->sc_ifp);

	/*
	 * bstp_ifupdstatus() may block, but it is the last
	 * operation of the member iface iteration, so we
	 * don't need to use LIST_FOREACH_MUTABLE()+bif_onlist
	 * check here.
	 */
	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;

		if (bif->bif_ifp == ifp) {
			bstp_ifupdstatus(sc, bif);
			break;
		}
	}
	ifnet_deserialize_all(sc->sc_ifp);
}

static void
bstp_ifupdstatus(struct bridge_softc *sc, struct bridge_iflist *bif)
{
	struct ifnet *ifp = bif->bif_ifp;
	struct ifmediareq ifmr;
	int error = 0;

	bzero((char *)&ifmr, sizeof(ifmr));
	ifnet_serialize_all(ifp);
	error = (*ifp->if_ioctl)(ifp, SIOCGIFMEDIA, (caddr_t)&ifmr, NULL);
	ifnet_deserialize_all(ifp);

	if ((error == 0) && (ifp->if_flags & IFF_UP)) {
	 	if (ifmr.ifm_status & IFM_ACTIVE) {
			if (bif->bif_state == BSTP_IFSTATE_DISABLED)
				bstp_enable_port(sc, bif);

		} else {
			if (bif->bif_state != BSTP_IFSTATE_DISABLED)
				bstp_disable_port(sc, bif);
		}
		return;
	}

	if (bif->bif_state != BSTP_IFSTATE_DISABLED)
		bstp_disable_port(sc, bif);
}

static void
bstp_tick(void *arg)
{
	struct bridge_softc *sc = arg;
	struct lwkt_msg *lmsg;

	KKASSERT(mycpuid == BRIDGE_CFGCPU);

	crit_enter();

	if (callout_pending(&sc->sc_bstpcallout) ||
	    !callout_active(&sc->sc_bstpcallout)) {
		crit_exit();
		return;
	}
	callout_deactivate(&sc->sc_bstpcallout);

	lmsg = &sc->sc_bstptimemsg.lmsg;
	KKASSERT(lmsg->ms_flags & MSGF_DONE);
	lwkt_sendmsg(BRIDGE_CFGPORT, lmsg);

	crit_exit();
}

void
bstp_tick_handler(netmsg_t msg)
{
	struct bridge_softc *sc = msg->lmsg.u.ms_resultp;
	struct bridge_iflist *bif;

	KKASSERT(&curthread->td_msgport == BRIDGE_CFGPORT);
	crit_enter();
	/* Reply ASAP */
	lwkt_replymsg(&msg->lmsg, 0);
	crit_exit();

	ifnet_serialize_all(sc->sc_ifp);

	/*
	 * NOTE:
	 * We don't need to worry that member iface is ripped
	 * from the per-cpu list during the blocking operation
	 * in the loop body, since deletion is serialized by
	 * BRIDGE_CFGPORT
	 */

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		/*
		 * XXX This can cause a lag in "link does away"
		 * XXX and "spanning tree gets updated".  We need
		 * XXX come sort of callback from the link state
		 * XXX update code to kick spanning tree.
		 * XXX --thorpej@NetBSD.org
		 */
		bstp_ifupdstatus(sc, bif);
	}

	if (bstp_timer_expired(&sc->sc_hello_timer, sc->sc_hello_time))
		bstp_hello_timer_expiry(sc);

	if (bstp_timer_expired(&sc->sc_tcn_timer, sc->sc_bridge_hello_time))
		bstp_tcn_timer_expiry(sc);

	if (bstp_timer_expired(&sc->sc_topology_change_timer,
	    sc->sc_topology_change_time))
		bstp_topology_change_timer_expiry(sc);

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bstp_timer_expired(&bif->bif_message_age_timer,
		    sc->sc_max_age))
			bstp_message_age_timer_expiry(sc, bif);
	}

	TAILQ_FOREACH(bif, &sc->sc_iflists[mycpuid], bif_next) {
		if ((bif->bif_flags & IFBIF_STP) == 0)
			continue;
		if (bstp_timer_expired(&bif->bif_forward_delay_timer,
		    sc->sc_forward_delay))
			bstp_forward_delay_timer_expiry(sc, bif);

		if (bstp_timer_expired(&bif->bif_hold_timer,
		    sc->sc_hold_time))
			bstp_hold_timer_expiry(sc, bif);

		if (bstp_timer_expired(&bif->bif_link1_timer,
		    sc->sc_hello_time * 10))
			bstp_link1_timer_expiry(sc, bif);
	}

	if (sc->sc_ifp->if_flags & IFF_RUNNING)
		callout_reset(&sc->sc_bstpcallout, hz, bstp_tick, sc);

	ifnet_deserialize_all(sc->sc_ifp);
}

static void
bstp_timer_start(struct bridge_timer *t, uint16_t v)
{
	t->value = v;
	t->active = 1;
}

static void
bstp_timer_stop(struct bridge_timer *t)
{
	t->value = 0;
	t->active = 0;
}

static int
bstp_timer_expired(struct bridge_timer *t, uint16_t v)
{
	if (t->active == 0)
		return (0);
	t->value += BSTP_TICK_VAL;
	if (t->value >= v) {
		bstp_timer_stop(t);
		return (1);
	}
	return (0);

}
