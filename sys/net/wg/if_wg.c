/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2015-2021 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2021 Matt Dunwoodie <ncon@noconroy.net>
 * Copyright (c) 2019-2020 Rubicon Communications, LLC (Netgate)
 * Copyright (c) 2021 Kyle Evans <kevans@FreeBSD.org>
 * Copyright (c) 2022 The FreeBSD Foundation
 * Copyright (c) 2023-2024 Aaron LI <aly@aaronly.me>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/caps.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/objcache.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketops.h> /* so_pru_*() functions */
#include <sys/socketvar.h>
#include <sys/sockio.h> /* SIOC* ioctl commands */
#include <sys/taskqueue.h>
#include <sys/time.h>

#include <machine/atomic.h>

#include <net/bpf.h>
#include <net/ethernet.h> /* ETHERMTU */
#include <net/if.h>
#include <net/if_clone.h>
#include <net/if_types.h> /* IFT_WIREGUARD */
#include <net/if_var.h>
#include <net/ifq_var.h>
#include <net/netisr.h>
#include <net/radix.h>
#include <net/route.h> /* struct rtentry */

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/in6_var.h> /* in6_mask2len() */
#include <netinet6/nd6.h> /* ND_IFINFO() */

#include "wg_cookie.h"
#include "wg_noise.h"
#include "if_wg.h"

CTASSERT(WG_KEY_SIZE >= NOISE_PUBLIC_KEY_LEN);
CTASSERT(WG_KEY_SIZE >= NOISE_SYMMETRIC_KEY_LEN);

#define DEFAULT_MTU		(ETHERMTU - 80)
#define MAX_MTU			(IF_MAXMTU - 80)

#ifndef ENOKEY
#define ENOKEY			ENOENT
#endif

/*
 * mbuf flags to clear after in-place encryption/decryption, so that the
 * mbuf can be reused for re-entering the network stack or delivering to
 * the remote peer.
 *
 * For example, the M_HASH and M_LENCHECKED flags must be cleared for an
 * inbound packet; otherwise, panic is to be expected.
 */
#define MBUF_CLEARFLAGS		(M_COPYFLAGS & ~(M_PKTHDR | M_EOR | M_PRIO))

#define MAX_LOOPS		8 /* 0 means no loop allowed */
#define MTAG_WGLOOP		0x77676c70 /* wglp; cookie for loop check */

#define MAX_STAGED_PKT		128
#define MAX_QUEUED_PKT		1024
#define MAX_QUEUED_PKT_MASK	(MAX_QUEUED_PKT - 1)
#define MAX_QUEUED_HANDSHAKES	4096

#define REKEY_TIMEOUT_JITTER	(karc4random() % 334) /* msec */
#define MAX_TIMER_HANDSHAKES	(90 / REKEY_TIMEOUT)
#define NEW_HANDSHAKE_TIMEOUT	(REKEY_TIMEOUT + KEEPALIVE_TIMEOUT)
#define UNDERLOAD_TIMEOUT	1

/* First byte indicating packet type on the wire */
#define WG_PKT_INITIATION	htole32(1)
#define WG_PKT_RESPONSE		htole32(2)
#define WG_PKT_COOKIE		htole32(3)
#define WG_PKT_DATA		htole32(4)

#define WG_PKT_PADDING		16
#define WG_PKT_WITH_PADDING(n)	\
	(((n) + (WG_PKT_PADDING - 1)) & ~(WG_PKT_PADDING - 1))
#define WG_PKT_ENCRYPTED_LEN(n)	\
	(offsetof(struct wg_pkt_data, buf[(n)]) + NOISE_AUTHTAG_LEN)
#define WG_PKT_IS_INITIATION(m)	\
	(*mtod((m), uint32_t *) == WG_PKT_INITIATION && \
	 (size_t)(m)->m_pkthdr.len == sizeof(struct wg_pkt_initiation))
#define WG_PKT_IS_RESPONSE(m)	\
	(*mtod((m), uint32_t *) == WG_PKT_RESPONSE && \
	 (size_t)(m)->m_pkthdr.len == sizeof(struct wg_pkt_response))
#define WG_PKT_IS_COOKIE(m)	\
	(*mtod((m), uint32_t *) == WG_PKT_COOKIE && \
	 (size_t)(m)->m_pkthdr.len == sizeof(struct wg_pkt_cookie))
#define WG_PKT_IS_DATA(m)	\
	(*mtod((m), uint32_t *) == WG_PKT_DATA && \
	 (size_t)(m)->m_pkthdr.len >= WG_PKT_ENCRYPTED_LEN(0))


#define DPRINTF(sc, fmt, ...)	\
	if (sc->sc_ifp->if_flags & IFF_DEBUG) \
		if_printf(sc->sc_ifp, fmt, ##__VA_ARGS__)


struct wg_pkt_initiation {
	uint32_t		t;
	uint32_t		s_idx;
	uint8_t			ue[NOISE_PUBLIC_KEY_LEN];
	uint8_t			es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN];
	uint8_t			ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN];
	struct cookie_macs	m;
};

struct wg_pkt_response {
	uint32_t		t;
	uint32_t		s_idx;
	uint32_t		r_idx;
	uint8_t			ue[NOISE_PUBLIC_KEY_LEN];
	uint8_t			en[0 + NOISE_AUTHTAG_LEN];
	struct cookie_macs	m;
};

struct wg_pkt_cookie {
	uint32_t		t;
	uint32_t		r_idx;
	uint8_t			nonce[COOKIE_NONCE_SIZE];
	uint8_t			ec[COOKIE_ENCRYPTED_SIZE];
};

struct wg_pkt_data {
	uint32_t		t;
	uint32_t		r_idx;
	uint64_t		counter;
	uint8_t			buf[];
};

struct wg_endpoint {
	union {
		struct sockaddr		r_sa;
		struct sockaddr_in	r_sin;
#ifdef INET6
		struct sockaddr_in6	r_sin6;
#endif
	} e_remote;
	/*
	 * NOTE: No 'e_local' on DragonFly, because the socket upcall
	 *       and so_pru_soreceive() cannot provide the local
	 *       (i.e., destination) address of a received packet.
	 */
};

struct aip_addr {
	uint8_t		length; /* required by the radix code */
	union {
		uint8_t		bytes[16];
		uint32_t	ip;
		uint32_t	ip6[4];
		struct in_addr	in;
		struct in6_addr	in6;
	};
};

struct wg_aip {
	struct radix_node	 a_nodes[2]; /* make the first for casting */
	LIST_ENTRY(wg_aip)	 a_entry;
	struct aip_addr		 a_addr;
	struct aip_addr		 a_mask;
	struct wg_peer		*a_peer;
	sa_family_t		 a_af;
};

enum wg_packet_state {
	WG_PACKET_DEAD,		/* to be dropped */
	WG_PACKET_UNCRYPTED,	/* before encryption/decryption */
	WG_PACKET_CRYPTED,	/* after encryption/decryption */
};

struct wg_packet {
	STAILQ_ENTRY(wg_packet)	 p_serial;
	STAILQ_ENTRY(wg_packet)	 p_parallel;
	struct wg_endpoint	 p_endpoint;
	struct noise_keypair	*p_keypair;
	uint64_t		 p_counter;
	struct mbuf		*p_mbuf;
	int			 p_mtu;
	sa_family_t		 p_af;
	unsigned int		 p_state; /* atomic */
};

STAILQ_HEAD(wg_packet_list, wg_packet);

struct wg_queue {
	struct lock		 q_mtx;
	struct wg_packet_list	 q_queue;
	size_t			 q_len;
};

struct wg_peer {
	TAILQ_ENTRY(wg_peer)	 p_entry;
	unsigned long		 p_id;
	struct wg_softc		*p_sc;

	char			 p_description[WG_PEER_DESCR_SIZE];

	struct noise_remote	*p_remote;
	struct cookie_maker	*p_cookie;

	struct lock		 p_endpoint_lock;
	struct wg_endpoint	 p_endpoint;

	struct wg_queue		 p_stage_queue;
	struct wg_queue		 p_encrypt_serial;
	struct wg_queue		 p_decrypt_serial;

	bool			 p_enabled;
	bool			 p_need_another_keepalive;
	uint16_t		 p_persistent_keepalive_interval;
	struct callout		 p_new_handshake;
	struct callout		 p_send_keepalive;
	struct callout		 p_retry_handshake;
	struct callout		 p_zero_key_material;
	struct callout		 p_persistent_keepalive;

	struct lock		 p_handshake_mtx;
	struct timespec		 p_handshake_complete; /* nanotime */
	int			 p_handshake_retries;

	struct task		 p_send_task;
	struct task		 p_recv_task;
	struct taskqueue	*p_send_taskqueue;
	struct taskqueue	*p_recv_taskqueue;

	uint64_t		*p_tx_bytes;
	uint64_t		*p_rx_bytes;

	LIST_HEAD(, wg_aip)	 p_aips;
	size_t			 p_aips_num;
};

struct wg_socket {
	struct lock	 so_lock;
	struct socket	*so_so4;
	struct socket	*so_so6;
	uint32_t	 so_user_cookie;
	in_port_t	 so_port;
};

struct wg_softc {
	LIST_ENTRY(wg_softc)	 sc_entry;
	struct ifnet		*sc_ifp;
	int			 sc_flags;

	struct wg_socket	 sc_socket;

	TAILQ_HEAD(, wg_peer)	 sc_peers;
	size_t			 sc_peers_num;

	struct noise_local	*sc_local;
	struct cookie_checker	*sc_cookie;

	struct lock		 sc_aip_lock;
	struct radix_node_head	*sc_aip4;
	struct radix_node_head	*sc_aip6;

	struct taskqueue	*sc_handshake_taskqueue;
	struct task		 sc_handshake_task;
	struct wg_queue		 sc_handshake_queue;

	struct task		*sc_encrypt_tasks; /* one per CPU */
	struct task		*sc_decrypt_tasks; /* one per CPU */
	struct wg_queue		 sc_encrypt_parallel;
	struct wg_queue		 sc_decrypt_parallel;
	int			 sc_encrypt_last_cpu;
	int			 sc_decrypt_last_cpu;

	struct lock		 sc_lock;
};


static MALLOC_DEFINE(M_WG, "WG", "wireguard");
static MALLOC_DEFINE(M_WG_PACKET, "WG packet", "wireguard packet");

static const char wgname[] = "wg";

static struct objcache *wg_packet_zone;
static struct lock wg_mtx;
static struct taskqueue **wg_taskqueues; /* one taskqueue per CPU */
static struct radix_node_head *wg_maskhead; /* shared by all interfaces */
static LIST_HEAD(, wg_softc) wg_list = LIST_HEAD_INITIALIZER(wg_list);


/* Timers */
static void	wg_timers_enable(struct wg_peer *);
static void	wg_timers_disable(struct wg_peer *);

/* Allowed IP */
static int	wg_aip_add(struct wg_softc *, struct wg_peer *, sa_family_t,
			   const void *, uint8_t);
static struct wg_peer *
		wg_aip_lookup(struct wg_softc *, sa_family_t, const void *);
static void	wg_aip_remove_all(struct wg_softc *, struct wg_peer *);

/* Handshake */
static void	wg_send_initiation(struct wg_peer *);
static void	wg_send_response(struct wg_peer *);
static void	wg_send_cookie(struct wg_softc *, struct cookie_macs *,
			       uint32_t, struct wg_endpoint *);
static void	wg_send_keepalive(struct wg_peer *);

/* Transport Packet Functions */
static void	wg_peer_send_staged(struct wg_peer *);
static void	wg_deliver_out(void *, int);
static void	wg_deliver_in(void *, int);
static void	wg_upcall(struct socket *, void *, int);

/*----------------------------------------------------------------------------*/
/* Packet */

static struct wg_packet *
wg_packet_alloc(struct mbuf *m)
{
	struct wg_packet *pkt;

	if ((pkt = objcache_get(wg_packet_zone, M_NOWAIT)) == NULL)
		return (NULL);

	bzero(pkt, sizeof(*pkt)); /* objcache_get() doesn't ensure M_ZERO. */
	pkt->p_mbuf = m;

	return (pkt);
}

static void
wg_packet_free(struct wg_packet *pkt)
{
	if (pkt->p_keypair != NULL)
		noise_keypair_put(pkt->p_keypair);
	if (pkt->p_mbuf != NULL)
		m_freem(pkt->p_mbuf);
	objcache_put(wg_packet_zone, pkt);
}

/*----------------------------------------------------------------------------*/
/*
 * Packet Queue Functions
 *
 * WireGuard uses the following queues:
 * - per-interface handshake queue: track incoming handshake packets
 * - per-peer staged queue: track the outgoing packets sent by that peer
 * - per-interface parallel encrypt and decrypt queues
 * - per-peer serial encrypt and decrypt queues
 *
 * For one interface, the handshake packets are only tracked in the handshake
 * queue and are processed in serial.  However, all data packets are tracked
 * in two queues: a serial queue and a parallel queue.  Specifically, the
 * outgoing packets (from the staged queue) will be queued in both the
 * parallel encrypt and the serial encrypt queues; the incoming packets will
 * be queued in both the parallel decrypt and the serial decrypt queues.
 *
 * - The parallel queues are used to distribute the encryption/decryption work
 *   across all CPUs.  The per-CPU wg_{encrypt,decrypt}_worker() work on the
 *   parallel queues.
 * - The serial queues ensure that packets are not reordered and are
 *   delivered in sequence for each peer.  The per-peer wg_deliver_{in,out}()
 *   work on the serial queues.
 */

static void wg_queue_purge(struct wg_queue *);

static void
wg_queue_init(struct wg_queue *queue, const char *name)
{
	lockinit(&queue->q_mtx, name, 0, 0);
	STAILQ_INIT(&queue->q_queue);
	queue->q_len = 0;
}

static void
wg_queue_deinit(struct wg_queue *queue)
{
	wg_queue_purge(queue);
	lockuninit(&queue->q_mtx);
}

static size_t
wg_queue_len(const struct wg_queue *queue)
{
	return (queue->q_len);
}

static bool
wg_queue_enqueue_handshake(struct wg_queue *hs, struct wg_packet *pkt)
{
	bool ok = false;

	lockmgr(&hs->q_mtx, LK_EXCLUSIVE);
	if (hs->q_len < MAX_QUEUED_HANDSHAKES) {
		STAILQ_INSERT_TAIL(&hs->q_queue, pkt, p_parallel);
		hs->q_len++;
		ok = true;
	}
	lockmgr(&hs->q_mtx, LK_RELEASE);

	if (!ok)
		wg_packet_free(pkt);

	return (ok);
}

static struct wg_packet *
wg_queue_dequeue_handshake(struct wg_queue *hs)
{
	struct wg_packet *pkt;

	lockmgr(&hs->q_mtx, LK_EXCLUSIVE);
	pkt = STAILQ_FIRST(&hs->q_queue);
	if (pkt != NULL) {
		STAILQ_REMOVE_HEAD(&hs->q_queue, p_parallel);
		hs->q_len--;
	}
	lockmgr(&hs->q_mtx, LK_RELEASE);

	return (pkt);
}

static void
wg_queue_push_staged(struct wg_queue *staged, struct wg_packet *pkt)
{
	struct wg_packet *old = NULL;

	lockmgr(&staged->q_mtx, LK_EXCLUSIVE);
	if (staged->q_len >= MAX_STAGED_PKT) {
		old = STAILQ_FIRST(&staged->q_queue);
		STAILQ_REMOVE_HEAD(&staged->q_queue, p_parallel);
		staged->q_len--;
	}
	STAILQ_INSERT_TAIL(&staged->q_queue, pkt, p_parallel);
	staged->q_len++;
	lockmgr(&staged->q_mtx, LK_RELEASE);

	if (old != NULL)
		wg_packet_free(old);
}

static void
wg_queue_enlist_staged(struct wg_queue *staged, struct wg_packet_list *list)
{
	struct wg_packet *pkt, *tpkt;

	STAILQ_FOREACH_MUTABLE(pkt, list, p_parallel, tpkt)
		wg_queue_push_staged(staged, pkt);
	STAILQ_INIT(list);
}

static void
wg_queue_delist_staged(struct wg_queue *staged, struct wg_packet_list *list)
{
	STAILQ_INIT(list);
	lockmgr(&staged->q_mtx, LK_EXCLUSIVE);
	STAILQ_CONCAT(list, &staged->q_queue);
	staged->q_len = 0;
	lockmgr(&staged->q_mtx, LK_RELEASE);
}

static void
wg_queue_purge(struct wg_queue *staged)
{
	struct wg_packet_list list;
	struct wg_packet *pkt, *tpkt;

	wg_queue_delist_staged(staged, &list);
	STAILQ_FOREACH_MUTABLE(pkt, &list, p_parallel, tpkt)
		wg_packet_free(pkt);
}

static bool
wg_queue_both(struct wg_queue *parallel, struct wg_queue *serial,
	      struct wg_packet *pkt)
{
	pkt->p_state = WG_PACKET_UNCRYPTED;

	lockmgr(&serial->q_mtx, LK_EXCLUSIVE);
	if (serial->q_len < MAX_QUEUED_PKT) {
		serial->q_len++;
		STAILQ_INSERT_TAIL(&serial->q_queue, pkt, p_serial);
	} else {
		lockmgr(&serial->q_mtx, LK_RELEASE);
		wg_packet_free(pkt);
		return (false);
	}
	lockmgr(&serial->q_mtx, LK_RELEASE);

	lockmgr(&parallel->q_mtx, LK_EXCLUSIVE);
	if (parallel->q_len < MAX_QUEUED_PKT) {
		parallel->q_len++;
		STAILQ_INSERT_TAIL(&parallel->q_queue, pkt, p_parallel);
	} else {
		lockmgr(&parallel->q_mtx, LK_RELEASE);
		/*
		 * Cannot just free the packet because it's already queued
		 * in the serial queue.  Instead, set its state to DEAD and
		 * let the serial worker to free it.
		 */
		pkt->p_state = WG_PACKET_DEAD;
		return (false);
	}
	lockmgr(&parallel->q_mtx, LK_RELEASE);

	return (true);
}

static struct wg_packet *
wg_queue_dequeue_serial(struct wg_queue *serial)
{
	struct wg_packet *pkt = NULL;

	lockmgr(&serial->q_mtx, LK_EXCLUSIVE);
	if (serial->q_len > 0 &&
	    STAILQ_FIRST(&serial->q_queue)->p_state != WG_PACKET_UNCRYPTED) {
		/*
		 * Dequeue both CRYPTED packets (to be delivered) and
		 * DEAD packets (to be freed).
		 */
		serial->q_len--;
		pkt = STAILQ_FIRST(&serial->q_queue);
		STAILQ_REMOVE_HEAD(&serial->q_queue, p_serial);
	}
	lockmgr(&serial->q_mtx, LK_RELEASE);

	return (pkt);
}

static struct wg_packet *
wg_queue_dequeue_parallel(struct wg_queue *parallel)
{
	struct wg_packet *pkt = NULL;

	lockmgr(&parallel->q_mtx, LK_EXCLUSIVE);
	if (parallel->q_len > 0) {
		parallel->q_len--;
		pkt = STAILQ_FIRST(&parallel->q_queue);
		STAILQ_REMOVE_HEAD(&parallel->q_queue, p_parallel);
	}
	lockmgr(&parallel->q_mtx, LK_RELEASE);

	return (pkt);
}

/*----------------------------------------------------------------------------*/
/* Peer */

static struct wg_peer *
wg_peer_create(struct wg_softc *sc, const uint8_t pub_key[WG_KEY_SIZE])
{
	static unsigned long peer_counter = 0;
	struct wg_peer *peer;

	KKASSERT(lockstatus(&sc->sc_lock, curthread) == LK_EXCLUSIVE);

	peer = kmalloc(sizeof(*peer), M_WG, M_WAITOK | M_ZERO);

	peer->p_remote = noise_remote_alloc(sc->sc_local, pub_key, peer);
	if (noise_remote_enable(peer->p_remote) != 0) {
		kfree(peer, M_WG);
		return (NULL);
	}

	peer->p_cookie = cookie_maker_alloc(pub_key);

	peer->p_id = ++peer_counter;
	peer->p_sc = sc;
	peer->p_tx_bytes = kmalloc(sizeof(*peer->p_tx_bytes) * ncpus,
				   M_WG, M_WAITOK | M_ZERO);
	peer->p_rx_bytes = kmalloc(sizeof(*peer->p_rx_bytes) * ncpus,
				   M_WG, M_WAITOK | M_ZERO);

	lockinit(&peer->p_endpoint_lock, "wg_peer_endpoint", 0, 0);
	lockinit(&peer->p_handshake_mtx, "wg_peer_handshake", 0, 0);

	wg_queue_init(&peer->p_stage_queue, "stageq");
	wg_queue_init(&peer->p_encrypt_serial, "txq");
	wg_queue_init(&peer->p_decrypt_serial, "rxq");

	callout_init_mp(&peer->p_new_handshake);
	callout_init_mp(&peer->p_send_keepalive);
	callout_init_mp(&peer->p_retry_handshake);
	callout_init_mp(&peer->p_persistent_keepalive);
	callout_init_mp(&peer->p_zero_key_material);

	TASK_INIT(&peer->p_send_task, 0, wg_deliver_out, peer);
	TASK_INIT(&peer->p_recv_task, 0, wg_deliver_in, peer);

	/* Randomly choose the taskqueues to distribute the load. */
	peer->p_send_taskqueue = wg_taskqueues[karc4random() % ncpus];
	peer->p_recv_taskqueue = wg_taskqueues[karc4random() % ncpus];

	LIST_INIT(&peer->p_aips);

	TAILQ_INSERT_TAIL(&sc->sc_peers, peer, p_entry);
	sc->sc_peers_num++;

	if (sc->sc_ifp->if_link_state == LINK_STATE_UP)
		wg_timers_enable(peer);

	DPRINTF(sc, "Peer %ld created\n", peer->p_id);
	return (peer);
}

static void
wg_peer_destroy(struct wg_peer *peer)
{
	struct wg_softc *sc = peer->p_sc;

	KKASSERT(lockstatus(&sc->sc_lock, curthread) == LK_EXCLUSIVE);

	/*
	 * Disable remote and timers.  This will prevent any new handshakes
	 * from occuring.
	 */
	noise_remote_disable(peer->p_remote);
	wg_timers_disable(peer);

	/*
	 * Remove all allowed IPs, so no more packets will be routed to
	 * this peer.
	 */
	wg_aip_remove_all(sc, peer);

	/* Remove peer from the interface, then free. */
	sc->sc_peers_num--;
	TAILQ_REMOVE(&sc->sc_peers, peer, p_entry);

	/*
	 * While there are no references remaining, we may still have
	 * p_{send,recv}_task executing (think empty queue, but
	 * wg_deliver_{in,out} needs to check the queue).  We should wait
	 * for them and then free.
	 */
	taskqueue_drain(peer->p_recv_taskqueue, &peer->p_recv_task);
	taskqueue_drain(peer->p_send_taskqueue, &peer->p_send_task);

	callout_terminate(&peer->p_new_handshake);
	callout_terminate(&peer->p_send_keepalive);
	callout_terminate(&peer->p_retry_handshake);
	callout_terminate(&peer->p_persistent_keepalive);
	callout_terminate(&peer->p_zero_key_material);

	wg_queue_deinit(&peer->p_decrypt_serial);
	wg_queue_deinit(&peer->p_encrypt_serial);
	wg_queue_deinit(&peer->p_stage_queue);

	kfree(peer->p_tx_bytes, M_WG);
	kfree(peer->p_rx_bytes, M_WG);

	lockuninit(&peer->p_endpoint_lock);
	lockuninit(&peer->p_handshake_mtx);

	noise_remote_free(peer->p_remote);
	cookie_maker_free(peer->p_cookie);

	DPRINTF(sc, "Peer %ld destroyed\n", peer->p_id);
	kfree(peer, M_WG);
}

static void
wg_peer_destroy_all(struct wg_softc *sc)
{
	struct wg_peer *peer, *tpeer;

	TAILQ_FOREACH_MUTABLE(peer, &sc->sc_peers, p_entry, tpeer)
		wg_peer_destroy(peer);
}

static int
wg_peer_set_sockaddr(struct wg_peer *peer, const struct sockaddr *remote)
{
	int ret = 0;

	lockmgr(&peer->p_endpoint_lock, LK_EXCLUSIVE);

	memcpy(&peer->p_endpoint.e_remote, remote,
	       sizeof(peer->p_endpoint.e_remote));
	if (remote->sa_family == AF_INET)
		memcpy(&peer->p_endpoint.e_remote.r_sin, remote,
		       sizeof(peer->p_endpoint.e_remote.r_sin));
#ifdef INET6
	else if (remote->sa_family == AF_INET6)
		memcpy(&peer->p_endpoint.e_remote.r_sin6, remote,
		       sizeof(peer->p_endpoint.e_remote.r_sin6));
#endif
	else
		ret = EAFNOSUPPORT;

	/* No 'e_local' to clear on DragonFly. */

	lockmgr(&peer->p_endpoint_lock, LK_RELEASE);
	return (ret);
}

static int
wg_peer_get_sockaddr(struct wg_peer *peer, struct sockaddr *remote)
{
	int ret = ENOENT;

	lockmgr(&peer->p_endpoint_lock, LK_SHARED);
	if (peer->p_endpoint.e_remote.r_sa.sa_family != AF_UNSPEC) {
		memcpy(remote, &peer->p_endpoint.e_remote,
		       sizeof(peer->p_endpoint.e_remote));
		ret = 0;
	}
	lockmgr(&peer->p_endpoint_lock, LK_RELEASE);
	return (ret);
}

static void
wg_peer_set_endpoint(struct wg_peer *peer, const struct wg_endpoint *e)
{
	KKASSERT(e->e_remote.r_sa.sa_family != AF_UNSPEC);

	if (__predict_true(memcmp(e, &peer->p_endpoint, sizeof(*e)) == 0))
		return;

	lockmgr(&peer->p_endpoint_lock, LK_EXCLUSIVE);
	peer->p_endpoint = *e;
	lockmgr(&peer->p_endpoint_lock, LK_RELEASE);
}

static void
wg_peer_get_endpoint(struct wg_peer *peer, struct wg_endpoint *e)
{
	if (__predict_true(memcmp(e, &peer->p_endpoint, sizeof(*e)) == 0))
		return;

	lockmgr(&peer->p_endpoint_lock, LK_SHARED);
	*e = peer->p_endpoint;
	lockmgr(&peer->p_endpoint_lock, LK_RELEASE);
}

/*----------------------------------------------------------------------------*/
/* Allowed IP */

static int
wg_aip_add(struct wg_softc *sc, struct wg_peer *peer, sa_family_t af,
	   const void *addr, uint8_t cidr)
{
	struct radix_node_head	*head;
	struct radix_node	*node;
	struct wg_aip		*aip;
	int			 ret = 0;

	aip = kmalloc(sizeof(*aip), M_WG, M_WAITOK | M_ZERO);
	aip->a_peer = peer;
	aip->a_af = af;

	switch (af) {
	case AF_INET:
		if (cidr > 32)
			cidr = 32;
		head = sc->sc_aip4;
		aip->a_addr.in = *(const struct in_addr *)addr;
		aip->a_mask.ip =
		    htonl(~((1LL << (32 - cidr)) - 1) & 0xffffffff);
		aip->a_addr.ip &= aip->a_mask.ip;
		aip->a_addr.length = aip->a_mask.length =
		    offsetof(struct aip_addr, in) + sizeof(struct in_addr);
		break;
#ifdef INET6
	case AF_INET6:
		if (cidr > 128)
			cidr = 128;
		head = sc->sc_aip6;
		aip->a_addr.in6 = *(const struct in6_addr *)addr;
		in6_prefixlen2mask(&aip->a_mask.in6, cidr);
		aip->a_addr.ip6[0] &= aip->a_mask.ip6[0];
		aip->a_addr.ip6[1] &= aip->a_mask.ip6[1];
		aip->a_addr.ip6[2] &= aip->a_mask.ip6[2];
		aip->a_addr.ip6[3] &= aip->a_mask.ip6[3];
		aip->a_addr.length = aip->a_mask.length =
		    offsetof(struct aip_addr, in6) + sizeof(struct in6_addr);
		break;
#endif
	default:
		kfree(aip, M_WG);
		return (EAFNOSUPPORT);
	}

	lockmgr(&sc->sc_aip_lock, LK_EXCLUSIVE);
	node = head->rnh_addaddr(&aip->a_addr, &aip->a_mask, head,
				 aip->a_nodes);
	if (node != NULL) {
		KKASSERT(node == aip->a_nodes);
		LIST_INSERT_HEAD(&peer->p_aips, aip, a_entry);
		peer->p_aips_num++;
	} else {
		/*
		 * Two possibilities:
		 * - out of memory failure
		 * - entry already exists
		 */
		node = head->rnh_lookup(&aip->a_addr, &aip->a_mask, head);
		if (node == NULL) {
			kfree(aip, M_WG);
			ret = ENOMEM;
		} else {
			KKASSERT(node != aip->a_nodes);
			kfree(aip, M_WG);
			aip = (struct wg_aip *)node;
			if (aip->a_peer != peer) {
				/* Replace the peer. */
				LIST_REMOVE(aip, a_entry);
				aip->a_peer->p_aips_num--;
				aip->a_peer = peer;
				LIST_INSERT_HEAD(&peer->p_aips, aip, a_entry);
				aip->a_peer->p_aips_num++;
			}
		}
	}
	lockmgr(&sc->sc_aip_lock, LK_RELEASE);

	return (ret);
}

static struct wg_peer *
wg_aip_lookup(struct wg_softc *sc, sa_family_t af, const void *a)
{
	struct radix_node_head	*head;
	struct radix_node	*node;
	struct wg_peer		*peer;
	struct aip_addr		 addr;

	switch (af) {
	case AF_INET:
		head = sc->sc_aip4;
		memcpy(&addr.in, a, sizeof(addr.in));
		addr.length = offsetof(struct aip_addr, in) + sizeof(addr.in);
		break;
	case AF_INET6:
		head = sc->sc_aip6;
		memcpy(&addr.in6, a, sizeof(addr.in6));
		addr.length = offsetof(struct aip_addr, in6) + sizeof(addr.in6);
		break;
	default:
		return (NULL);
	}

	lockmgr(&sc->sc_aip_lock, LK_SHARED);
	node = head->rnh_matchaddr(&addr, head);
	if (node != NULL) {
		peer = ((struct wg_aip *)node)->a_peer;
		noise_remote_ref(peer->p_remote);
	} else {
		peer = NULL;
	}
	lockmgr(&sc->sc_aip_lock, LK_RELEASE);

	return (peer);
}

static void
wg_aip_remove_all(struct wg_softc *sc, struct wg_peer *peer)
{
	struct radix_node_head	*head;
	struct radix_node	*node;
	struct wg_aip		*aip, *taip;

	lockmgr(&sc->sc_aip_lock, LK_EXCLUSIVE);

	LIST_FOREACH_MUTABLE(aip, &peer->p_aips, a_entry, taip) {
		switch (aip->a_af) {
		case AF_INET:
			head = sc->sc_aip4;
			break;
		case AF_INET6:
			head = sc->sc_aip6;
			break;
		default:
			panic("%s: impossible aip %p", __func__, aip);
		}
		node = head->rnh_deladdr(&aip->a_addr, &aip->a_mask, head);
		if (node == NULL)
			panic("%s: failed to delete aip %p", __func__, aip);
		LIST_REMOVE(aip, a_entry);
		peer->p_aips_num--;
		kfree(aip, M_WG);
	}

	if (!LIST_EMPTY(&peer->p_aips) || peer->p_aips_num != 0)
		panic("%s: could not delete all aips for peer %ld",
		      __func__, peer->p_id);

	lockmgr(&sc->sc_aip_lock, LK_RELEASE);
}

/*----------------------------------------------------------------------------*/
/* Socket */

static int	wg_socket_open(struct socket **, sa_family_t, in_port_t *,
			       void *);
static int	wg_socket_set_sockopt(struct socket *, struct socket *,
				      int, void *, size_t);

static int
wg_socket_init(struct wg_softc *sc, in_port_t port)
{
	struct wg_socket	*so = &sc->sc_socket;
	struct socket		*so4 = NULL, *so6 = NULL;
	in_port_t		 bound_port = port;
	uint32_t		 cookie;
	int			 ret;

	/*
	 * When a host or a jail doesn't support the AF, sobind() would
	 * return EADDRNOTAVAIL.  Handle this case in order to support such
	 * IPv4-only or IPv6-only environments.
	 *
	 * However, in a dual-stack environment, both IPv4 and IPv6 sockets
	 * must bind the same port.
	 */
	ret = wg_socket_open(&so4, AF_INET, &bound_port, sc);
	if (ret != 0 && ret != EADDRNOTAVAIL)
		goto error;

#ifdef INET6
	ret = wg_socket_open(&so6, AF_INET6, &bound_port, sc);
	if (ret != 0 && ret != EADDRNOTAVAIL)
		goto error;
#endif

	if (so4 == NULL && so6 == NULL) {
		ret = EAFNOSUPPORT;
		goto error;
	}

	cookie = so->so_user_cookie;
	if (cookie != 0) {
		ret = wg_socket_set_sockopt(so4, so6, SO_USER_COOKIE,
					    &cookie, sizeof(cookie));
		if (ret != 0)
			goto error;
	}

	KKASSERT(lockstatus(&sc->sc_lock, curthread) == LK_EXCLUSIVE);

	lockinit(&so->so_lock, "wg socket lock", 0, 0);

	if (so->so_so4 != NULL)
		soclose(so->so_so4, 0);
	if (so->so_so6 != NULL)
		soclose(so->so_so6, 0);
	so->so_so4 = so4;
	so->so_so6 = so6;
	so->so_port = bound_port;

	return (0);

error:
	if (so4 != NULL)
		soclose(so4, 0);
	if (so6 != NULL)
		soclose(so6, 0);
	return (ret);
}

static int
wg_socket_open(struct socket **so, sa_family_t af, in_port_t *port,
	       void *upcall_arg)
{
	struct sockaddr_in	 sin;
#ifdef INET6
	struct sockaddr_in6	 sin6;
#endif
	struct sockaddr		*sa, *bound_sa;
	int			 ret;

	if (af == AF_INET) {
		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(struct sockaddr_in);
		sin.sin_family = AF_INET;
		sin.sin_port = htons(*port);
		sa = sintosa(&sin);
#ifdef INET6
	} else if (af == AF_INET6) {
		bzero(&sin6, sizeof(sin6));
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = htons(*port);
		sa = sintosa(&sin6);
#endif
	} else {
		return (EAFNOSUPPORT);
	}

	ret = socreate(af, so, SOCK_DGRAM, IPPROTO_UDP, curthread);
	if (ret != 0)
		return (ret);

	(*so)->so_upcall = wg_upcall;
	(*so)->so_upcallarg = upcall_arg;
	atomic_set_int(&(*so)->so_rcv.ssb_flags, SSB_UPCALL);

	ret = sobind(*so, sa, curthread);
	if (ret != 0)
		goto error;

	if (*port == 0) {
		ret = so_pru_sockaddr(*so, &bound_sa);
		if (ret != 0)
			goto error;
		if (bound_sa->sa_family == AF_INET)
			*port = ntohs(satosin(bound_sa)->sin_port);
		else
			*port = ntohs(satosin6(bound_sa)->sin6_port);
		kfree(bound_sa, M_SONAME);
	}

	return (0);

error:
	if (*so != NULL) {
		soclose(*so, 0);
		*so = NULL;
	}
	return (ret);
}

static void
wg_socket_uninit(struct wg_softc *sc)
{
	struct wg_socket *so = &sc->sc_socket;

	KKASSERT(lockstatus(&sc->sc_lock, curthread) == LK_EXCLUSIVE);

	lockmgr(&so->so_lock, LK_EXCLUSIVE);

	if (so->so_so4 != NULL) {
		soclose(so->so_so4, 0);
		so->so_so4 = NULL;
	}
	if (so->so_so6 != NULL) {
		soclose(so->so_so6, 0);
		so->so_so6 = NULL;
	}

	lockmgr(&so->so_lock, LK_RELEASE);
	lockuninit(&so->so_lock);
}

static int
wg_socket_set_sockopt(struct socket *so4, struct socket *so6,
		      int name, void *val, size_t len)
{
	struct sockopt sopt = {
		.sopt_dir = SOPT_SET,
		.sopt_level = SOL_SOCKET,
		.sopt_name = name,
		.sopt_val = val,
		.sopt_valsize = len,
	};
	int ret;

	if (so4 != NULL) {
		ret = sosetopt(so4, &sopt);
		if (ret != 0)
			return (ret);
	}
	if (so6 != NULL) {
		ret = sosetopt(so6, &sopt);
		if (ret != 0)
			return (ret);
	}

	return (0);
}

static int
wg_socket_set_cookie(struct wg_softc *sc, uint32_t user_cookie)
{
	struct wg_socket	*so;
	int			 ret;

	KKASSERT(lockstatus(&sc->sc_lock, curthread) == LK_EXCLUSIVE);

	so = &sc->sc_socket;
	lockmgr(&so->so_lock, LK_EXCLUSIVE);

	ret = wg_socket_set_sockopt(so->so_so4, so->so_so6, SO_USER_COOKIE,
				    &user_cookie, sizeof(user_cookie));
	if (ret == 0)
		so->so_user_cookie = user_cookie;

	lockmgr(&so->so_lock, LK_RELEASE);
	return (ret);
}

static int
wg_send(struct wg_softc *sc, struct wg_endpoint *e, struct mbuf *m)
{
	struct wg_socket	*so;
	struct sockaddr		*sa;
	int			 len, ret;

	so = &sc->sc_socket;
	sa = &e->e_remote.r_sa;
	len = m->m_pkthdr.len;
	ret = 0;

	/*
	 * NOTE: DragonFly by default sends UDP packets asynchronously,
	 *       unless the 'net.inet.udp.sosend_async' sysctl MIB is set
	 *       to 0 or the 'MSG_SYNC' flag is set for so_pru_sosend().
	 *       And in the async mode, an error code cannot really be
	 *       replied to the caller.  So so_pru_sosend() may return 0
	 *       even if the packet fails to send.
	 */
	lockmgr(&so->so_lock, LK_SHARED);
	if (sa->sa_family == AF_INET && so->so_so4 != NULL) {
		ret = so_pru_sosend(so->so_so4, sa, NULL /* uio */,
				    m, NULL /* control */, 0 /* flags */,
				    curthread);
#ifdef INET6
	} else if (sa->sa_family == AF_INET6 && so->so_so6 != NULL) {
		ret = so_pru_sosend(so->so_so6, sa, NULL /* uio */,
				    m, NULL /* control */, 0 /* flags */,
				    curthread);
#endif
	} else {
		ret = ENOTCONN;
		m_freem(m);
	}
	lockmgr(&so->so_lock, LK_RELEASE);

	if (ret == 0) {
		IFNET_STAT_INC(sc->sc_ifp, opackets, 1);
		IFNET_STAT_INC(sc->sc_ifp, obytes, len);
	} else {
		IFNET_STAT_INC(sc->sc_ifp, oerrors, 1);
	}

	return (ret);
}

static void
wg_send_buf(struct wg_softc *sc, struct wg_endpoint *e, const void *buf,
	    size_t len)
{
	struct mbuf	*m;
	int		 ret;

	/*
	 * This function only sends handshake packets of known lengths that
	 * are <= MHLEN, so it's safe to just use m_gethdr() and memcpy().
	 */
	KKASSERT(len <= MHLEN);

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		DPRINTF(sc, "Unable to allocate mbuf\n");
		return;
	}

	/* Just plain copy as it's a single mbuf. */
	memcpy(mtod(m, void *), buf, len);
	m->m_pkthdr.len = m->m_len = len;

	/* Give high priority to the handshake packets. */
	m->m_flags |= M_PRIO;

	ret = wg_send(sc, e, m);
	if (ret != 0)
		DPRINTF(sc, "Unable to send packet: %d\n", ret);
}

/*----------------------------------------------------------------------------*/
/*
 * Timers
 *
 * These functions handle the timeout callbacks for a WireGuard session, and
 * provide an "event-based" model for controlling WireGuard session timers.
 */

static void	wg_timers_run_send_initiation(struct wg_peer *, bool);
static void	wg_timers_run_retry_handshake(void *);
static void	wg_timers_run_send_keepalive(void *);
static void	wg_timers_run_new_handshake(void *);
static void	wg_timers_run_zero_key_material(void *);
static void	wg_timers_run_persistent_keepalive(void *);

static void
wg_timers_enable(struct wg_peer *peer)
{
	atomic_store_bool(&peer->p_enabled, true);
	wg_timers_run_persistent_keepalive(peer);
}

static void
wg_timers_disable(struct wg_peer *peer)
{
	atomic_store_bool(&peer->p_enabled, false);
	atomic_store_bool(&peer->p_need_another_keepalive, false);

	/* Cancel the callouts and wait for them to complete. */
	callout_drain(&peer->p_new_handshake);
	callout_drain(&peer->p_send_keepalive);
	callout_drain(&peer->p_retry_handshake);
	callout_drain(&peer->p_persistent_keepalive);
	callout_drain(&peer->p_zero_key_material);
}

static void
wg_timers_set_persistent_keepalive(struct wg_peer *peer, uint16_t interval)
{
	atomic_store_16(&peer->p_persistent_keepalive_interval, interval);
	if (atomic_load_bool(&peer->p_enabled))
		wg_timers_run_persistent_keepalive(peer);
}

static bool
wg_timers_get_persistent_keepalive(struct wg_peer *peer, uint16_t *interval)
{
	*interval = atomic_load_16(&peer->p_persistent_keepalive_interval);
	return (*interval > 0);
}

static void
wg_timers_get_last_handshake(struct wg_peer *peer, struct timespec *time)
{
	lockmgr(&peer->p_handshake_mtx, LK_EXCLUSIVE);
	*time = peer->p_handshake_complete;
	lockmgr(&peer->p_handshake_mtx, LK_RELEASE);
}

/*
 * Should be called after an authenticated data packet is sent.
 */
static void
wg_timers_event_data_sent(struct wg_peer *peer)
{
	int ticks;

	if (atomic_load_bool(&peer->p_enabled) &&
	    !callout_pending(&peer->p_new_handshake)) {
		ticks = NEW_HANDSHAKE_TIMEOUT * hz +
			REKEY_TIMEOUT_JITTER * hz / 1000;
		callout_reset(&peer->p_new_handshake, ticks,
			      wg_timers_run_new_handshake, peer);
	}
}

/*
 * Should be called after an authenticated data packet is received.
 */
static void
wg_timers_event_data_received(struct wg_peer *peer)
{
	if (atomic_load_bool(&peer->p_enabled)) {
		if (!callout_pending(&peer->p_send_keepalive)) {
			callout_reset(&peer->p_send_keepalive,
				      KEEPALIVE_TIMEOUT * hz,
				      wg_timers_run_send_keepalive, peer);
		} else {
			atomic_store_bool(&peer->p_need_another_keepalive,
					  true);
		}
	}
}

/*
 * Should be called before any type of authenticated packet is to be sent,
 * whether keepalive, data, or handshake.
 */
static void
wg_timers_event_any_authenticated_packet_sent(struct wg_peer *peer)
{
	callout_stop(&peer->p_send_keepalive);
}

/*
 * Should be called after any type of authenticated packet is received,
 * whether keepalive, data, or handshake.
 */
static void
wg_timers_event_any_authenticated_packet_received(struct wg_peer *peer)
{
	callout_stop(&peer->p_new_handshake);
}

/*
 * Should be called before a packet with authentication (whether keepalive,
 * data, or handshakem) is sent, or after one is received.
 */
static void
wg_timers_event_any_authenticated_packet_traversal(struct wg_peer *peer)
{
	uint16_t interval;

	interval = atomic_load_16(&peer->p_persistent_keepalive_interval);
	if (atomic_load_bool(&peer->p_enabled) && interval > 0) {
		callout_reset(&peer->p_persistent_keepalive, interval * hz,
			      wg_timers_run_persistent_keepalive, peer);
	}
}

/*
 * Should be called after a handshake initiation message is sent.
 */
static void
wg_timers_event_handshake_initiated(struct wg_peer *peer)
{
	int ticks;

	if (atomic_load_bool(&peer->p_enabled)) {
		ticks = REKEY_TIMEOUT * hz + REKEY_TIMEOUT_JITTER * hz / 1000;
		callout_reset(&peer->p_retry_handshake, ticks,
			      wg_timers_run_retry_handshake, peer);
	}
}

/*
 * Should be called after a handshake response message is received and
 * processed, or when getting key confirmation via the first data message.
 */
static void
wg_timers_event_handshake_complete(struct wg_peer *peer)
{
	if (atomic_load_bool(&peer->p_enabled)) {
		lockmgr(&peer->p_handshake_mtx, LK_EXCLUSIVE);
		callout_stop(&peer->p_retry_handshake);
		peer->p_handshake_retries = 0;
		getnanotime(&peer->p_handshake_complete);
		lockmgr(&peer->p_handshake_mtx, LK_RELEASE);

		wg_timers_run_send_keepalive(peer);
	}
}

/*
 * Should be called after an ephemeral key is created, which is before sending
 * a handshake response or after receiving a handshake response.
 */
static void
wg_timers_event_session_derived(struct wg_peer *peer)
{
	if (atomic_load_bool(&peer->p_enabled)) {
		callout_reset(&peer->p_zero_key_material,
			      REJECT_AFTER_TIME * 3 * hz,
			      wg_timers_run_zero_key_material, peer);
	}
}

/*
 * Should be called after data packet sending failure, or after the old
 * keypairs expiring (or near expiring).
 */
static void
wg_timers_event_want_initiation(struct wg_peer *peer)
{
	if (atomic_load_bool(&peer->p_enabled))
		wg_timers_run_send_initiation(peer, false);
}

static void
wg_timers_run_send_initiation(struct wg_peer *peer, bool is_retry)
{
	if (!is_retry)
		peer->p_handshake_retries = 0;
	if (noise_remote_initiation_expired(peer->p_remote))
		wg_send_initiation(peer);
}

static void
wg_timers_run_retry_handshake(void *_peer)
{
	struct wg_peer *peer = _peer;

	lockmgr(&peer->p_handshake_mtx, LK_EXCLUSIVE);
	if (peer->p_handshake_retries <= MAX_TIMER_HANDSHAKES) {
		peer->p_handshake_retries++;
		lockmgr(&peer->p_handshake_mtx, LK_RELEASE);

		DPRINTF(peer->p_sc, "Handshake for peer %ld did not complete "
			"after %d seconds, retrying (try %d)\n", peer->p_id,
			REKEY_TIMEOUT, peer->p_handshake_retries + 1);
		wg_timers_run_send_initiation(peer, true);
	} else {
		lockmgr(&peer->p_handshake_mtx, LK_RELEASE);

		DPRINTF(peer->p_sc, "Handshake for peer %ld did not complete "
			"after %d retries, giving up\n", peer->p_id,
			MAX_TIMER_HANDSHAKES + 2);
		callout_stop(&peer->p_send_keepalive);
		wg_queue_purge(&peer->p_stage_queue);
		if (atomic_load_bool(&peer->p_enabled) &&
		    !callout_pending(&peer->p_zero_key_material)) {
			callout_reset(&peer->p_zero_key_material,
				      REJECT_AFTER_TIME * 3 * hz,
				      wg_timers_run_zero_key_material, peer);
		}
	}
}

static void
wg_timers_run_send_keepalive(void *_peer)
{
	struct wg_peer *peer = _peer;

	wg_send_keepalive(peer);

	if (atomic_load_bool(&peer->p_enabled) &&
	    atomic_load_bool(&peer->p_need_another_keepalive)) {
		atomic_store_bool(&peer->p_need_another_keepalive, false);
		callout_reset(&peer->p_send_keepalive, KEEPALIVE_TIMEOUT * hz,
			      wg_timers_run_send_keepalive, peer);
	}
}

static void
wg_timers_run_persistent_keepalive(void *_peer)
{
	struct wg_peer *peer = _peer;

	if (atomic_load_16(&peer->p_persistent_keepalive_interval) > 0)
		wg_send_keepalive(peer);
}

static void
wg_timers_run_new_handshake(void *_peer)
{
	struct wg_peer *peer = _peer;

	DPRINTF(peer->p_sc, "Retrying handshake with peer %ld, "
		"because we stopped hearing back after %d seconds\n",
		peer->p_id, NEW_HANDSHAKE_TIMEOUT);
	wg_timers_run_send_initiation(peer, false);
}

static void
wg_timers_run_zero_key_material(void *_peer)
{
	struct wg_peer *peer = _peer;

	DPRINTF(peer->p_sc, "Zeroing out keys for peer %ld, "
		"since we haven't received a new one in %d seconds\n",
		peer->p_id, REJECT_AFTER_TIME * 3);
	noise_remote_keypairs_clear(peer->p_remote);
}

/*----------------------------------------------------------------------------*/
/* Handshake */

static void
wg_peer_send_buf(struct wg_peer *peer, const void *buf, size_t len)
{
	struct wg_endpoint endpoint;

	peer->p_tx_bytes[mycpuid] += len;

	wg_timers_event_any_authenticated_packet_traversal(peer);
	wg_timers_event_any_authenticated_packet_sent(peer);

	wg_peer_get_endpoint(peer, &endpoint);
	wg_send_buf(peer->p_sc, &endpoint, buf, len);
}

static void
wg_send_initiation(struct wg_peer *peer)
{
	struct wg_pkt_initiation pkt;

	if (!noise_create_initiation(peer->p_remote, &pkt.s_idx, pkt.ue,
				     pkt.es, pkt.ets))
		return;

	DPRINTF(peer->p_sc, "Sending handshake initiation to peer %ld\n",
		peer->p_id);

	pkt.t = WG_PKT_INITIATION;
	cookie_maker_mac(peer->p_cookie, &pkt.m, &pkt,
			 sizeof(pkt) - sizeof(pkt.m));
	wg_peer_send_buf(peer, &pkt, sizeof(pkt));
	wg_timers_event_handshake_initiated(peer);
}

static void
wg_send_response(struct wg_peer *peer)
{
	struct wg_pkt_response pkt;

	if (!noise_create_response(peer->p_remote, &pkt.s_idx, &pkt.r_idx,
				   pkt.ue, pkt.en))
		return;

	DPRINTF(peer->p_sc, "Sending handshake response to peer %ld\n",
		peer->p_id);

	wg_timers_event_session_derived(peer);
	pkt.t = WG_PKT_RESPONSE;
	cookie_maker_mac(peer->p_cookie, &pkt.m, &pkt,
			 sizeof(pkt) - sizeof(pkt.m));
	wg_peer_send_buf(peer, &pkt, sizeof(pkt));
}

static void
wg_send_cookie(struct wg_softc *sc, struct cookie_macs *cm, uint32_t idx,
	       struct wg_endpoint *e)
{
	struct wg_pkt_cookie pkt;

	DPRINTF(sc, "Sending cookie response for denied handshake message\n");

	pkt.t = WG_PKT_COOKIE;
	pkt.r_idx = idx;

	cookie_checker_create_payload(sc->sc_cookie, cm, pkt.nonce,
				      pkt.ec, &e->e_remote.r_sa);
	wg_send_buf(sc, e, &pkt, sizeof(pkt));
}

static void
wg_send_keepalive(struct wg_peer *peer)
{
	struct wg_packet *pkt;
	struct mbuf *m;

	if (wg_queue_len(&peer->p_stage_queue) > 0)
		goto send;
	if ((m = m_gethdr(M_NOWAIT, MT_DATA)) == NULL)
		return;
	if ((pkt = wg_packet_alloc(m)) == NULL) {
		m_freem(m);
		return;
	}

	wg_queue_push_staged(&peer->p_stage_queue, pkt);
	DPRINTF(peer->p_sc, "Sending keepalive packet to peer %ld\n",
		peer->p_id);
send:
	wg_peer_send_staged(peer);
}

static bool
wg_is_underload(struct wg_softc *sc)
{
	/*
	 * This is global, so that the load calculation applies to the
	 * whole system.  Don't care about races with it at all.
	 */
	static struct timespec	last_underload; /* nanouptime */
	struct timespec		now;
	bool			underload;

	underload = (wg_queue_len(&sc->sc_handshake_queue) >=
		     MAX_QUEUED_HANDSHAKES / 8);
	if (underload) {
		getnanouptime(&last_underload);
	} else if (timespecisset(&last_underload)) {
		getnanouptime(&now);
		now.tv_sec -= UNDERLOAD_TIMEOUT;
		underload = timespeccmp(&last_underload, &now, >);
		if (!underload)
			timespecclear(&last_underload);
	}

	return (underload);
}

static void
wg_handshake(struct wg_softc *sc, struct wg_packet *pkt)
{
	struct wg_pkt_initiation	*init;
	struct wg_pkt_response		*resp;
	struct wg_pkt_cookie		*cook;
	struct wg_endpoint		*e;
	struct wg_peer			*peer;
	struct mbuf			*m;
	struct noise_remote		*remote = NULL;
	bool				 underload;
	int				 ret;

	pkt->p_mbuf = m_pullup(pkt->p_mbuf, pkt->p_mbuf->m_pkthdr.len);
	if (pkt->p_mbuf == NULL)
		goto error;

	underload = wg_is_underload(sc);
	m = pkt->p_mbuf;
	e = &pkt->p_endpoint;

	switch (*mtod(m, uint32_t *)) {
	case WG_PKT_INITIATION:
		init = mtod(m, struct wg_pkt_initiation *);

		ret = cookie_checker_validate_macs(sc->sc_cookie, &init->m,
		    init, sizeof(*init) - sizeof(init->m), underload,
		    &e->e_remote.r_sa);
		if (ret != 0) {
			switch (ret) {
			case EINVAL:
				DPRINTF(sc, "Invalid initiation MAC\n");
				break;
			case ECONNREFUSED:
				DPRINTF(sc, "Handshake ratelimited\n");
				break;
			case EAGAIN:
				wg_send_cookie(sc, &init->m, init->s_idx, e);
				break;
			default:
				/*
				 * cookie_checker_validate_macs() seems could
				 * return EAFNOSUPPORT, but that is actually
				 * impossible, because packets of unsupported
				 * AF have been already dropped.
				 */
				panic("%s: unexpected return: %d",
				      __func__, ret);
			}
			goto error;
		}

		remote = noise_consume_initiation(sc->sc_local, init->s_idx,
						  init->ue, init->es,
						  init->ets);
		if (remote == NULL) {
			DPRINTF(sc, "Invalid handshake initiation\n");
			goto error;
		}

		peer = noise_remote_arg(remote);
		DPRINTF(sc, "Receiving handshake initiation from peer %ld\n",
			peer->p_id);

		wg_peer_set_endpoint(peer, e);
		wg_send_response(peer);
		break;

	case WG_PKT_RESPONSE:
		resp = mtod(m, struct wg_pkt_response *);

		ret = cookie_checker_validate_macs(sc->sc_cookie, &resp->m,
		    resp, sizeof(*resp) - sizeof(resp->m), underload,
		    &e->e_remote.r_sa);
		if (ret != 0) {
			switch (ret) {
			case EINVAL:
				DPRINTF(sc, "Invalid response MAC\n");
				break;
			case ECONNREFUSED:
				DPRINTF(sc, "Handshake ratelimited\n");
				break;
			case EAGAIN:
				wg_send_cookie(sc, &resp->m, resp->s_idx, e);
				break;
			default:
				/* See also the comment above. */
				panic("%s: unexpected return: %d",
				      __func__, ret);
			}
			goto error;
		}

		remote = noise_consume_response(sc->sc_local, resp->s_idx,
						resp->r_idx, resp->ue,
						resp->en);
		if (remote == NULL) {
			DPRINTF(sc, "Invalid handshake response\n");
			goto error;
		}

		peer = noise_remote_arg(remote);
		DPRINTF(sc, "Receiving handshake response from peer %ld\n",
			peer->p_id);

		wg_peer_set_endpoint(peer, e);
		wg_timers_event_session_derived(peer);
		wg_timers_event_handshake_complete(peer);
		break;

	case WG_PKT_COOKIE:
		cook = mtod(m, struct wg_pkt_cookie *);

		/*
		 * A cookie message can be a reply to an initiation message
		 * or to a response message.  In the latter case, the noise
		 * index has been transformed from a remote entry to a
		 * keypair entry.  Therefore, we need to lookup the index
		 * for both remote and keypair entries.
		 */
		remote = noise_remote_index(sc->sc_local, cook->r_idx);
		if (remote == NULL) {
			DPRINTF(sc, "Unknown cookie index\n");
			goto error;
		}

		peer = noise_remote_arg(remote);
		if (cookie_maker_consume_payload(peer->p_cookie, cook->nonce,
						 cook->ec) == 0) {
			DPRINTF(sc, "Receiving cookie response\n");
		} else {
			DPRINTF(sc, "Could not decrypt cookie response\n");
			goto error;
		}

		goto not_authenticated;

	default:
		panic("%s: invalid packet in handshake queue", __func__);
	}

	wg_timers_event_any_authenticated_packet_received(peer);
	wg_timers_event_any_authenticated_packet_traversal(peer);

not_authenticated:
	IFNET_STAT_INC(sc->sc_ifp, ipackets, 1);
	IFNET_STAT_INC(sc->sc_ifp, ibytes, m->m_pkthdr.len);
	peer->p_rx_bytes[mycpuid] += m->m_pkthdr.len;
	noise_remote_put(remote);
	wg_packet_free(pkt);

	return;

error:
	IFNET_STAT_INC(sc->sc_ifp, ierrors, 1);
	if (remote != NULL)
		noise_remote_put(remote);
	wg_packet_free(pkt);
}

static void
wg_handshake_worker(void *arg, int pending __unused)
{
	struct wg_softc		*sc = arg;
	struct wg_queue		*queue = &sc->sc_handshake_queue;
	struct wg_packet	*pkt;

	while ((pkt = wg_queue_dequeue_handshake(queue)) != NULL)
		wg_handshake(sc, pkt);
}

/*----------------------------------------------------------------------------*/
/* Transport Packet Functions */

static inline void
wg_bpf_ptap(struct ifnet *ifp, struct mbuf *m, sa_family_t af)
{
	uint32_t bpf_af;

	if (ifp->if_bpf == NULL)
		return;

	bpf_gettoken();
	/* Double check after obtaining the token. */
	if (ifp->if_bpf != NULL) {
		/* Prepend the AF as a 4-byte field for DLT_NULL. */
		bpf_af = (uint32_t)af;
		bpf_ptap(ifp->if_bpf, m, &bpf_af, sizeof(bpf_af));
	}
	bpf_reltoken();
}

static inline unsigned int
calculate_padding(struct wg_packet *pkt)
{
	unsigned int padded_size, last_unit;

	last_unit = pkt->p_mbuf->m_pkthdr.len;

	/* Keepalive packets don't set p_mtu, but also have a length of zero. */
	if (__predict_false(pkt->p_mtu == 0))
		return WG_PKT_WITH_PADDING(last_unit) - last_unit;

	/*
	 * Just in case the packet is bigger than the MTU and would cause
	 * the final subtraction to overflow.
	 */
	if (__predict_false(last_unit > pkt->p_mtu))
		last_unit %= pkt->p_mtu;

	padded_size = MIN(pkt->p_mtu, WG_PKT_WITH_PADDING(last_unit));
	return (padded_size - last_unit);
}

static inline int
determine_af_and_pullup(struct mbuf **m, sa_family_t *af)
{
	const struct ip		*ip;
	const struct ip6_hdr	*ip6;
	int			 len;

	ip = mtod(*m, const struct ip *);
	ip6 = mtod(*m, const struct ip6_hdr *);
	len = (*m)->m_pkthdr.len;

	if (len >= sizeof(*ip) && ip->ip_v == IPVERSION)
		*af = AF_INET;
#ifdef INET6
	else if (len >= sizeof(*ip6) &&
		 (ip6->ip6_vfc & IPV6_VERSION_MASK) == IPV6_VERSION)
		*af = AF_INET6;
#endif
	else
		return (EAFNOSUPPORT);

	*m = m_pullup(*m, (*af == AF_INET ? sizeof(*ip) : sizeof(*ip6)));
	if (*m == NULL)
		return (ENOBUFS);

	return (0);
}

static void
wg_encrypt(struct wg_softc *sc, struct wg_packet *pkt)
{
	static const uint8_t	 padding[WG_PKT_PADDING] = { 0 };
	struct wg_pkt_data	*data;
	struct wg_peer		*peer;
	struct noise_remote	*remote;
	struct mbuf		*m;
	unsigned int		 padlen, state = WG_PACKET_DEAD;
	uint32_t		 idx;

	remote = noise_keypair_remote(pkt->p_keypair);
	peer = noise_remote_arg(remote);
	m = pkt->p_mbuf;

	padlen = calculate_padding(pkt);
	if (padlen != 0 && !m_append(m, padlen, padding))
		goto out;

	if (noise_keypair_encrypt(pkt->p_keypair, &idx, pkt->p_counter, m) != 0)
		goto out;

	M_PREPEND(m, sizeof(struct wg_pkt_data), M_NOWAIT);
	if (m == NULL)
		goto out;
	data = mtod(m, struct wg_pkt_data *);
	data->t = WG_PKT_DATA;
	data->r_idx = idx;
	data->counter = htole64(pkt->p_counter);

	state = WG_PACKET_CRYPTED;

out:
	pkt->p_mbuf = m;
	atomic_store_rel_int(&pkt->p_state, state);
	taskqueue_enqueue(peer->p_send_taskqueue, &peer->p_send_task);
	noise_remote_put(remote);
}

static void
wg_decrypt(struct wg_softc *sc, struct wg_packet *pkt)
{
	struct wg_peer		*peer, *allowed_peer;
	struct noise_remote	*remote;
	struct mbuf		*m;
	unsigned int		 state = WG_PACKET_DEAD;
	int			 len;

	remote = noise_keypair_remote(pkt->p_keypair);
	peer = noise_remote_arg(remote);
	m = pkt->p_mbuf;

	pkt->p_counter = le64toh(mtod(m, struct wg_pkt_data *)->counter);
	m_adj(m, sizeof(struct wg_pkt_data));

	if (noise_keypair_decrypt(pkt->p_keypair, pkt->p_counter, m) != 0)
		goto out;

	/* A packet with a length of zero is a keepalive packet. */
	if (__predict_false(m->m_pkthdr.len == 0)) {
		DPRINTF(sc, "Receiving keepalive packet from peer %ld\n",
			peer->p_id);
		state = WG_PACKET_CRYPTED;
		goto out;
	}

	/*
	 * Extract the source address for wg_aip_lookup(), and trim the
	 * packet if it was padded before encryption.
	 */
	if (determine_af_and_pullup(&m, &pkt->p_af) != 0)
		goto out;
	if (pkt->p_af == AF_INET) {
		const struct ip *ip = mtod(m, const struct ip *);
		allowed_peer = wg_aip_lookup(sc, AF_INET, &ip->ip_src);
		len = ntohs(ip->ip_len);
		if (len >= sizeof(struct ip) && len < m->m_pkthdr.len)
			m_adj(m, len - m->m_pkthdr.len);
	} else {
		const struct ip6_hdr *ip6 = mtod(m, const struct ip6_hdr *);
		allowed_peer = wg_aip_lookup(sc, AF_INET6, &ip6->ip6_src);
		len = ntohs(ip6->ip6_plen) + sizeof(struct ip6_hdr);
		if (len < m->m_pkthdr.len)
			m_adj(m, len - m->m_pkthdr.len);
	}

	/* Drop the reference, since no need to dereference it. */
	if (allowed_peer != NULL)
		noise_remote_put(allowed_peer->p_remote);

	if (__predict_false(peer != allowed_peer)) {
		DPRINTF(sc, "Packet has disallowed src IP from peer %ld\n",
			peer->p_id);
		goto out;
	}

	state = WG_PACKET_CRYPTED;

out:
	pkt->p_mbuf = m;
	atomic_store_rel_int(&pkt->p_state, state);
	taskqueue_enqueue(peer->p_recv_taskqueue, &peer->p_recv_task);
	noise_remote_put(remote);
}

static void
wg_encrypt_worker(void *arg, int pending __unused)
{
	struct wg_softc		*sc = arg;
	struct wg_queue		*queue = &sc->sc_encrypt_parallel;
	struct wg_packet	*pkt;

	while ((pkt = wg_queue_dequeue_parallel(queue)) != NULL)
		wg_encrypt(sc, pkt);
}

static void
wg_decrypt_worker(void *arg, int pending __unused)
{
	struct wg_softc		*sc = arg;
	struct wg_queue		*queue = &sc->sc_decrypt_parallel;
	struct wg_packet	*pkt;

	while ((pkt = wg_queue_dequeue_parallel(queue)) != NULL)
		wg_decrypt(sc, pkt);
}

static void
wg_encrypt_dispatch(struct wg_softc *sc)
{
	int cpu;

	/*
	 * The update to encrypt_last_cpu is racy such that we may
	 * reschedule the task for the same CPU multiple times, but
	 * the race doesn't really matter.
	 */
	cpu = (sc->sc_encrypt_last_cpu + 1) % ncpus;
	sc->sc_encrypt_last_cpu = cpu;
	taskqueue_enqueue(wg_taskqueues[cpu], &sc->sc_encrypt_tasks[cpu]);
}

static void
wg_decrypt_dispatch(struct wg_softc *sc)
{
	int cpu;

	cpu = (sc->sc_decrypt_last_cpu + 1) % ncpus;
	sc->sc_decrypt_last_cpu = cpu;
	taskqueue_enqueue(wg_taskqueues[cpu], &sc->sc_decrypt_tasks[cpu]);
}

static void
wg_deliver_out(void *arg, int pending __unused)
{
	struct wg_peer		*peer = arg;
	struct wg_softc		*sc = peer->p_sc;
	struct wg_queue		*queue = &peer->p_encrypt_serial;
	struct wg_endpoint	 endpoint;
	struct wg_packet	*pkt;
	struct mbuf		*m;
	int			 len, cpu;

	cpu = mycpuid;

	while ((pkt = wg_queue_dequeue_serial(queue)) != NULL) {
		if (atomic_load_acq_int(&pkt->p_state) != WG_PACKET_CRYPTED) {
			IFNET_STAT_INC(sc->sc_ifp, oerrors, 1);
			wg_packet_free(pkt);
			continue;
		}

		m = pkt->p_mbuf;
		m->m_flags &= ~MBUF_CLEARFLAGS;
		len = m->m_pkthdr.len;

		pkt->p_mbuf = NULL;
		wg_packet_free(pkt);

		/*
		 * The keepalive timers -- both persistent and mandatory --
		 * are part of the internal state machine, which needs to be
		 * cranked whether or not the packet was actually sent.
		 */
		wg_timers_event_any_authenticated_packet_traversal(peer);
		wg_timers_event_any_authenticated_packet_sent(peer);

		wg_peer_get_endpoint(peer, &endpoint);
		if (wg_send(sc, &endpoint, m) == 0) {
			peer->p_tx_bytes[cpu] += len;
			if (len > WG_PKT_ENCRYPTED_LEN(0))
				wg_timers_event_data_sent(peer);
			if (noise_keypair_should_refresh(peer->p_remote, true))
				wg_timers_event_want_initiation(peer);
		}
	}
}

static void
wg_deliver_in(void *arg, int pending __unused)
{
	struct wg_peer		*peer = arg;
	struct wg_softc		*sc = peer->p_sc;
	struct wg_queue		*queue = &peer->p_decrypt_serial;
	struct wg_packet	*pkt;
	struct ifnet		*ifp;
	struct mbuf		*m;
	size_t			 rx_bytes;
	int			 cpu;

	cpu = mycpuid;
	ifp = sc->sc_ifp;

	while ((pkt = wg_queue_dequeue_serial(queue)) != NULL) {
		if (atomic_load_acq_int(&pkt->p_state) != WG_PACKET_CRYPTED ||
		    noise_keypair_counter_check(pkt->p_keypair, pkt->p_counter)
		    != 0) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			wg_packet_free(pkt);
			continue;
		}

		if (noise_keypair_received_with(pkt->p_keypair))
			wg_timers_event_handshake_complete(peer);

		wg_timers_event_any_authenticated_packet_received(peer);
		wg_timers_event_any_authenticated_packet_traversal(peer);
		wg_peer_set_endpoint(peer, &pkt->p_endpoint);

		m = pkt->p_mbuf;
		rx_bytes = WG_PKT_ENCRYPTED_LEN(m->m_pkthdr.len);
		peer->p_rx_bytes[cpu] += rx_bytes;
		IFNET_STAT_INC(ifp, ipackets, 1);
		IFNET_STAT_INC(ifp, ibytes, rx_bytes);

		if (m->m_pkthdr.len > 0) {
			if (ifp->if_capenable & IFCAP_RXCSUM) {
				/*
				 * The packet is authentic as ensured by the
				 * AEAD tag, so we can tell the networking
				 * stack that this packet has valid checksums
				 * and thus is unnecessary to check again.
				 */
				if (m->m_pkthdr.csum_flags & CSUM_IP)
					m->m_pkthdr.csum_flags |=
					    (CSUM_IP_CHECKED | CSUM_IP_VALID);
				if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
					m->m_pkthdr.csum_flags |=
					    (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
					m->m_pkthdr.csum_data = 0xffff;
				}
			}
			m->m_flags &= ~MBUF_CLEARFLAGS;
			m->m_pkthdr.rcvif = ifp;

			wg_bpf_ptap(ifp, m, pkt->p_af);

			netisr_queue((pkt->p_af == AF_INET ?
				      NETISR_IP : NETISR_IPV6), m);
			pkt->p_mbuf = NULL;

			wg_timers_event_data_received(peer);
		}

		wg_packet_free(pkt);

		if (noise_keypair_should_refresh(peer->p_remote, false))
			wg_timers_event_want_initiation(peer);
	}
}

static void
wg_input(struct wg_softc *sc, struct mbuf *m, const struct sockaddr *sa)
{
	struct noise_remote	*remote;
	struct wg_pkt_data	*data;
	struct wg_packet	*pkt;
	struct wg_peer		*peer;
	struct mbuf		*defragged;

	/*
	 * Defragment mbufs early on in order to:
	 * - make the crypto a lot faster;
	 * - make the subsequent m_pullup()'s no-ops.
	 */
	defragged = m_defrag(m, M_NOWAIT);
	if (defragged != NULL)
		m = defragged; /* The original mbuf chain is freed. */

	/* Ensure the packet is not shared before modifying it. */
	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		IFNET_STAT_INC(sc->sc_ifp, iqdrops, 1);
		return;
	}

	/* Pullup enough to read packet type */
	if ((m = m_pullup(m, sizeof(uint32_t))) == NULL) {
		IFNET_STAT_INC(sc->sc_ifp, iqdrops, 1);
		return;
	}

	if ((pkt = wg_packet_alloc(m)) == NULL) {
		IFNET_STAT_INC(sc->sc_ifp, iqdrops, 1);
		m_freem(m);
		return;
	}

	/* Save the remote address and port for later use. */
	switch (sa->sa_family) {
	case AF_INET:
		pkt->p_endpoint.e_remote.r_sin =
		    *(const struct sockaddr_in *)sa;
		break;
#ifdef INET6
	case AF_INET6:
		pkt->p_endpoint.e_remote.r_sin6 =
		    *(const struct sockaddr_in6 *)sa;
		break;
#endif
	default:
		DPRINTF(sc, "Unsupported packet address family\n");
		goto error;
	}

	if (WG_PKT_IS_INITIATION(m) ||
	    WG_PKT_IS_RESPONSE(m) ||
	    WG_PKT_IS_COOKIE(m)) {
		if (!wg_queue_enqueue_handshake(&sc->sc_handshake_queue, pkt)) {
			IFNET_STAT_INC(sc->sc_ifp, iqdrops, 1);
			DPRINTF(sc, "Dropping handshake packet\n");
		}
		taskqueue_enqueue(sc->sc_handshake_taskqueue,
				  &sc->sc_handshake_task);
		return;
	}

	if (WG_PKT_IS_DATA(m)) {
		/* Pullup the whole header to read r_idx below. */
		pkt->p_mbuf = m_pullup(m, sizeof(struct wg_pkt_data));
		if (pkt->p_mbuf == NULL)
			goto error;

		data = mtod(pkt->p_mbuf, struct wg_pkt_data *);
		pkt->p_keypair = noise_keypair_lookup(sc->sc_local,
						      data->r_idx);
		if (pkt->p_keypair == NULL)
			goto error;

		remote = noise_keypair_remote(pkt->p_keypair);
		peer = noise_remote_arg(remote);
		if (!wg_queue_both(&sc->sc_decrypt_parallel,
				   &peer->p_decrypt_serial, pkt))
			IFNET_STAT_INC(sc->sc_ifp, iqdrops, 1);

		wg_decrypt_dispatch(sc);
		noise_remote_put(remote);
		return;
	}

error:
	IFNET_STAT_INC(sc->sc_ifp, ierrors, 1);
	wg_packet_free(pkt);
}

static void
wg_upcall(struct socket *so, void *arg, int waitflag __unused)
{
	struct wg_softc		*sc = arg;
	struct sockaddr		*from;
	struct sockbuf		 sio;
	int			 ret, flags;

	/*
	 * For UDP, soreceive typically pulls just one packet,
	 * so loop to get the whole batch.
	 */
	do {
		sbinit(&sio, 1000000000); /* really large to receive all */
		flags = MSG_DONTWAIT;
		ret = so_pru_soreceive(so, &from, NULL, &sio, NULL, &flags);
		if (ret != 0 || sio.sb_mb == NULL) {
			if (from != NULL)
				kfree(from, M_SONAME);
			break;
		}
		wg_input(sc, sio.sb_mb, from);
		kfree(from, M_SONAME);
	} while (sio.sb_mb != NULL);
}

static void
wg_peer_send_staged(struct wg_peer *peer)
{
	struct wg_softc		*sc = peer->p_sc;
	struct wg_packet	*pkt, *tpkt;
	struct wg_packet_list	 list;
	struct noise_keypair	*keypair = NULL;

	wg_queue_delist_staged(&peer->p_stage_queue, &list);

	if (STAILQ_EMPTY(&list))
		return;

	if ((keypair = noise_keypair_current(peer->p_remote)) == NULL)
		goto error;

	/*
	 * We now try to assign counters to all of the packets in the queue.
	 * If we can't assign counters for all of them, we just consider it
	 * a failure and wait for the next handshake.
	 */
	STAILQ_FOREACH(pkt, &list, p_parallel) {
		if (!noise_keypair_counter_next(keypair, &pkt->p_counter))
			goto error;
	}
	STAILQ_FOREACH_MUTABLE(pkt, &list, p_parallel, tpkt) {
		pkt->p_keypair = noise_keypair_ref(keypair);
		if (!wg_queue_both(&sc->sc_encrypt_parallel,
				   &peer->p_encrypt_serial, pkt))
			IFNET_STAT_INC(sc->sc_ifp, oqdrops, 1);
	}

	wg_encrypt_dispatch(sc);
	noise_keypair_put(keypair);
	return;

error:
	if (keypair != NULL)
		noise_keypair_put(keypair);
	wg_queue_enlist_staged(&peer->p_stage_queue, &list);
	wg_timers_event_want_initiation(peer);
}

static int
wg_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	  struct rtentry *rt)
{
	struct wg_softc		*sc = ifp->if_softc;
	struct wg_packet	*pkt = NULL;
	struct wg_peer		*peer = NULL;
	struct mbuf		*defragged;
	sa_family_t		 af = AF_UNSPEC;
	int			 ret;

	if (dst->sa_family == AF_UNSPEC) {
		/*
		 * Specially handle packets written/injected by BPF.
		 * The packets have the same DLT_NULL link-layer type
		 * (i.e., 4-byte link-layer header in host byte order).
		 */
		dst->sa_family = *(mtod(m, uint32_t *));
		m_adj(m, sizeof(uint32_t));
	}
	if (dst->sa_family == AF_UNSPEC) {
		ret = EAFNOSUPPORT;
		goto error;
	}

	wg_bpf_ptap(ifp, m, dst->sa_family);

	if (__predict_false(if_tunnel_check_nesting(ifp, m, MTAG_WGLOOP,
						    MAX_LOOPS) != 0)) {
		DPRINTF(sc, "Packet looped\n");
		ret = ELOOP;
		goto error;
	}

	defragged = m_defrag(m, M_NOWAIT);
	if (defragged != NULL)
		m = defragged;

	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		ret = ENOBUFS;
		goto error;
	}

	if ((ret = determine_af_and_pullup(&m, &af)) != 0)
		goto error;
	if (af != dst->sa_family) {
		ret = EAFNOSUPPORT;
		goto error;
	}

	if ((pkt = wg_packet_alloc(m)) == NULL) {
		ret = ENOBUFS;
		goto error;
	}

	pkt->p_af = af;
	pkt->p_mtu = ifp->if_mtu;
	if (rt != NULL && rt->rt_rmx.rmx_mtu > 0 &&
	    rt->rt_rmx.rmx_mtu < pkt->p_mtu)
		pkt->p_mtu = rt->rt_rmx.rmx_mtu;

	peer = wg_aip_lookup(sc, af,
			     (af == AF_INET ?
			      (void *)&mtod(m, struct ip *)->ip_dst :
			      (void *)&mtod(m, struct ip6_hdr *)->ip6_dst));
	if (__predict_false(peer == NULL)) {
		ret = ENOKEY;
		goto error;
	}
	if (__predict_false(peer->p_endpoint.e_remote.r_sa.sa_family
			    == AF_UNSPEC)) {
		DPRINTF(sc, "No valid endpoint has been configured or "
			"discovered for peer %ld\n", peer->p_id);
		ret = EHOSTUNREACH;
		goto error;
	}

	wg_queue_push_staged(&peer->p_stage_queue, pkt);
	wg_peer_send_staged(peer);
	noise_remote_put(peer->p_remote);

	return (0);

error:
	IFNET_STAT_INC(ifp, oerrors, 1);
	if (ret == ELOOP) {
		/* Skip ICMP error for ELOOP to avoid infinite loop. */
		m_freem(m); /* m cannot be NULL */
		m = NULL;
	}
	if (m != NULL) {
		if (af == AF_INET)
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_HOST, 0, 0);
#ifdef INET6
		else if (af == AF_INET6)
			icmp6_error(m, ICMP6_DST_UNREACH, 0, 0);
#endif
		else
			m_freem(m);
	}
	if (pkt != NULL) {
		pkt->p_mbuf = NULL; /* m already freed above */
		wg_packet_free(pkt);
	}
	if (peer != NULL)
		noise_remote_put(peer->p_remote);
	return (ret);
}

/*----------------------------------------------------------------------------*/
/* Interface Functions */

static int	wg_up(struct wg_softc *);
static void	wg_down(struct wg_softc *);

static int
wg_ioctl_get(struct wg_softc *sc, struct wg_data_io *data, bool privileged)
{
	struct wg_interface_io	*iface_p, iface_o;
	struct wg_peer_io	*peer_p, peer_o;
	struct wg_aip_io	*aip_p, aip_o;
	struct wg_peer		*peer;
	struct wg_aip		*aip;
	size_t			 size, peer_count, aip_count;
	int			 cpu, ret = 0;

	lockmgr(&sc->sc_lock, LK_SHARED);

	/* Determine the required data size. */
	size = sizeof(struct wg_interface_io);
	size += sizeof(struct wg_peer_io) * sc->sc_peers_num;
	TAILQ_FOREACH(peer, &sc->sc_peers, p_entry)
		size += sizeof(struct wg_aip_io) * peer->p_aips_num;

	/* Return the required size for userland allocation. */
	if (data->wgd_size < size) {
		data->wgd_size = size;
		lockmgr(&sc->sc_lock, LK_RELEASE);
		return (0);
	}

	iface_p = data->wgd_interface;
	bzero(&iface_o, sizeof(iface_o));
	/*
	 * No need to acquire the 'sc_socket.so_lock', because 'sc_lock'
	 * is acquired and that's enough to prevent modifications to
	 * 'sc_socket' members.
	 */
	if (sc->sc_socket.so_port != 0) {
		iface_o.i_port = sc->sc_socket.so_port;
		iface_o.i_flags |= WG_INTERFACE_HAS_PORT;
	}
	if (sc->sc_socket.so_user_cookie != 0) {
		iface_o.i_cookie = sc->sc_socket.so_user_cookie;
		iface_o.i_flags |= WG_INTERFACE_HAS_COOKIE;
	}
	if (noise_local_keys(sc->sc_local, iface_o.i_public,
			     iface_o.i_private)) {
		iface_o.i_flags |= WG_INTERFACE_HAS_PUBLIC;
		if (privileged)
			iface_o.i_flags |= WG_INTERFACE_HAS_PRIVATE;
		else
			bzero(iface_o.i_private, sizeof(iface_o.i_private));
	}

	peer_count = 0;
	peer_p = &iface_p->i_peers[0];
	TAILQ_FOREACH(peer, &sc->sc_peers, p_entry) {
		bzero(&peer_o, sizeof(peer_o));

		peer_o.p_flags |= WG_PEER_HAS_PUBLIC;
		if (noise_remote_keys(peer->p_remote, peer_o.p_public,
				      peer_o.p_psk)) {
			if (privileged)
				peer_o.p_flags |= WG_PEER_HAS_PSK;
			else
				bzero(peer_o.p_psk, sizeof(peer_o.p_psk));
		}
		if (wg_timers_get_persistent_keepalive(peer, &peer_o.p_pka))
			peer_o.p_flags |= WG_PEER_HAS_PKA;
		if (wg_peer_get_sockaddr(peer, &peer_o.p_sa) == 0)
			peer_o.p_flags |= WG_PEER_HAS_ENDPOINT;
		for (cpu = 0; cpu < ncpus; cpu++) {
			peer_o.p_rxbytes += peer->p_rx_bytes[cpu];
			peer_o.p_txbytes += peer->p_tx_bytes[cpu];
		}
		wg_timers_get_last_handshake(peer, &peer_o.p_last_handshake);
		peer_o.p_id = (uint64_t)peer->p_id;
		strlcpy(peer_o.p_description, peer->p_description,
			sizeof(peer_o.p_description));

		aip_count = 0;
		aip_p = &peer_p->p_aips[0];
		LIST_FOREACH(aip, &peer->p_aips, a_entry) {
			bzero(&aip_o, sizeof(aip_o));
			aip_o.a_af = aip->a_af;
			if (aip->a_af == AF_INET) {
				aip_o.a_cidr = bitcount32(aip->a_mask.ip);
				memcpy(&aip_o.a_ipv4, &aip->a_addr.in,
				       sizeof(aip->a_addr.in));
			} else if (aip->a_af == AF_INET6) {
				aip_o.a_cidr = in6_mask2len(&aip->a_mask.in6,
							    NULL);
				memcpy(&aip_o.a_ipv6, &aip->a_addr.in6,
				       sizeof(aip->a_addr.in6));
			}

			ret = copyout(&aip_o, aip_p, sizeof(aip_o));
			if (ret != 0)
				goto out;

			aip_p++;
			aip_count++;
		}
		KKASSERT(aip_count == peer->p_aips_num);
		peer_o.p_aips_count = aip_count;

		ret = copyout(&peer_o, peer_p, sizeof(peer_o));
		if (ret != 0)
			goto out;

		peer_p = (struct wg_peer_io *)aip_p;
		peer_count++;
	}
	KKASSERT(peer_count == sc->sc_peers_num);
	iface_o.i_peers_count = peer_count;

	ret = copyout(&iface_o, iface_p, sizeof(iface_o));

out:
	lockmgr(&sc->sc_lock, LK_RELEASE);
	explicit_bzero(&iface_o, sizeof(iface_o));
	explicit_bzero(&peer_o, sizeof(peer_o));
	return (ret);
}

static int
wg_ioctl_set(struct wg_softc *sc, struct wg_data_io *data)
{
	struct wg_interface_io	*iface_p, iface_o;
	struct wg_peer_io	*peer_p, peer_o;
	struct wg_aip_io	*aip_p, aip_o;
	struct wg_peer		*peer;
	struct noise_remote	*remote;
	uint8_t			 public[WG_KEY_SIZE], private[WG_KEY_SIZE];
	size_t			 i, j;
	int			 ret;

	remote = NULL;
	lockmgr(&sc->sc_lock, LK_EXCLUSIVE);

	iface_p = data->wgd_interface;
	if ((ret = copyin(iface_p, &iface_o, sizeof(iface_o))) != 0)
		goto error;

	if (iface_o.i_flags & WG_INTERFACE_REPLACE_PEERS)
		wg_peer_destroy_all(sc);

	if ((iface_o.i_flags & WG_INTERFACE_HAS_PRIVATE) &&
	    (!noise_local_keys(sc->sc_local, NULL, private) ||
	     timingsafe_bcmp(private, iface_o.i_private, WG_KEY_SIZE) != 0)) {
		if (curve25519_generate_public(public, iface_o.i_private)) {
			remote = noise_remote_lookup(sc->sc_local, public);
			if (remote != NULL) {
				/* Remove the conflicting peer. */
				peer = noise_remote_arg(remote);
				wg_peer_destroy(peer);
				noise_remote_put(remote);
			}
		}

		/*
		 * Set the private key.
		 *
		 * Note: we might be removing the private key.
		 */
		if (noise_local_set_private(sc->sc_local, iface_o.i_private))
			cookie_checker_update(sc->sc_cookie, public);
		else
			cookie_checker_update(sc->sc_cookie, NULL);
	}

	if ((iface_o.i_flags & WG_INTERFACE_HAS_PORT) &&
	    iface_o.i_port != sc->sc_socket.so_port) {
		if (sc->sc_ifp->if_flags & IFF_RUNNING) {
			ret = wg_socket_init(sc, iface_o.i_port);
			if (ret != 0)
				goto error;
		} else {
			sc->sc_socket.so_port = iface_o.i_port;
		}
	}

	if (iface_o.i_flags & WG_INTERFACE_HAS_COOKIE) {
		ret = wg_socket_set_cookie(sc, iface_o.i_cookie);
		if (ret != 0)
			goto error;
	}

	peer_p = &iface_p->i_peers[0];
	for (i = 0; i < iface_o.i_peers_count; i++) {
		if ((ret = copyin(peer_p, &peer_o, sizeof(peer_o))) != 0)
			goto error;

		/* Peer must have public key. */
		if ((peer_o.p_flags & WG_PEER_HAS_PUBLIC) == 0)
			goto next_peer;
		/* Ignore peer that has the same public key. */
		if (noise_local_keys(sc->sc_local, public, NULL) &&
		    memcmp(public, peer_o.p_public, WG_KEY_SIZE) == 0)
			goto next_peer;

		/* Lookup peer, or create if it doesn't exist. */
		remote = noise_remote_lookup(sc->sc_local, peer_o.p_public);
		if (remote != NULL) {
			peer = noise_remote_arg(remote);
		} else {
			if (peer_o.p_flags & (WG_PEER_REMOVE | WG_PEER_UPDATE))
				goto next_peer;

			peer = wg_peer_create(sc, peer_o.p_public);
			if (peer == NULL) {
				ret = ENOMEM;
				goto error;
			}

			/* No allowed IPs to remove for a new peer. */
			peer_o.p_flags &= ~WG_PEER_REPLACE_AIPS;
		}

		if (peer_o.p_flags & WG_PEER_REMOVE) {
			wg_peer_destroy(peer);
			goto next_peer;
		}

		if (peer_o.p_flags & WG_PEER_HAS_ENDPOINT) {
			ret = wg_peer_set_sockaddr(peer, &peer_o.p_sa);
			if (ret != 0)
				goto error;
		}
		if (peer_o.p_flags & WG_PEER_HAS_PSK)
			noise_remote_set_psk(peer->p_remote, peer_o.p_psk);
		if (peer_o.p_flags & WG_PEER_HAS_PKA)
			wg_timers_set_persistent_keepalive(peer, peer_o.p_pka);
		if (peer_o.p_flags & WG_PEER_SET_DESCRIPTION)
			strlcpy(peer->p_description, peer_o.p_description,
				sizeof(peer->p_description));

		if (peer_o.p_flags & WG_PEER_REPLACE_AIPS)
			wg_aip_remove_all(sc, peer);

		for (j = 0; j < peer_o.p_aips_count; j++) {
			aip_p = &peer_p->p_aips[j];
			if ((ret = copyin(aip_p, &aip_o, sizeof(aip_o))) != 0)
				goto error;
			ret = wg_aip_add(sc, peer, aip_o.a_af, &aip_o.a_addr,
					 aip_o.a_cidr);
			if (ret != 0)
				goto error;
		}

		if (sc->sc_ifp->if_link_state == LINK_STATE_UP)
			wg_peer_send_staged(peer);

	next_peer:
		if (remote != NULL) {
			noise_remote_put(remote);
			remote = NULL;
		}
		aip_p = &peer_p->p_aips[peer_o.p_aips_count];
		peer_p = (struct wg_peer_io *)aip_p;
	}

error:
	if (remote != NULL)
		noise_remote_put(remote);
	lockmgr(&sc->sc_lock, LK_RELEASE);
	explicit_bzero(&iface_o, sizeof(iface_o));
	explicit_bzero(&peer_o, sizeof(peer_o));
	explicit_bzero(&aip_o, sizeof(aip_o));
	explicit_bzero(public, sizeof(public));
	explicit_bzero(private, sizeof(private));
	return (ret);
}

static int
wg_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cred)
{
	struct wg_data_io	*wgd;
	struct wg_softc		*sc;
	struct ifreq		*ifr;
	bool			 privileged;
	int			 ret, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	ret = 0;

	switch (cmd) {
	case SIOCSWG:
		ret = caps_priv_check(cred, SYSCAP_RESTRICTEDROOT);
		if (ret == 0) {
			wgd = (struct wg_data_io *)data;
			ret = wg_ioctl_set(sc, wgd);
		}
		break;
	case SIOCGWG:
		privileged =
		    (caps_priv_check(cred, SYSCAP_RESTRICTEDROOT) == 0);
		wgd = (struct wg_data_io *)data;
		ret = wg_ioctl_get(sc, wgd, privileged);
		break;
	/* Interface IOCTLs */
	case SIOCSIFADDR:
		/*
		 * This differs from *BSD norms, but is more uniform with how
		 * WireGuard behaves elsewhere.
		 */
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ret = wg_up(sc);
		else
			wg_down(sc);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu <= 0 || ifr->ifr_mtu > MAX_MTU)
			ret = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCSIFCAP:
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (mask & IFCAP_RXCSUM)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	default:
		ret = ENOTTY;
	}

	return (ret);
}

static int
wg_up(struct wg_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct wg_peer *peer;
	int ret = 0;

	lockmgr(&sc->sc_lock, LK_EXCLUSIVE);

	/* Silent success if we're already running. */
	if (ifp->if_flags & IFF_RUNNING)
		goto out;
	ifp->if_flags |= IFF_RUNNING;

	ret = wg_socket_init(sc, sc->sc_socket.so_port);
	if (ret == 0) {
		TAILQ_FOREACH(peer, &sc->sc_peers, p_entry)
			wg_timers_enable(peer);
		ifp->if_link_state = LINK_STATE_UP;
		if_link_state_change(ifp);
	} else {
		ifp->if_flags &= ~IFF_RUNNING;
		DPRINTF(sc, "Unable to initialize sockets: %d\n", ret);
	}

out:
	lockmgr(&sc->sc_lock, LK_RELEASE);
	return (ret);
}

static void
wg_down(struct wg_softc *sc)
{
	struct ifnet	*ifp = sc->sc_ifp;
	struct wg_peer	*peer;
	int		 i;

	lockmgr(&sc->sc_lock, LK_EXCLUSIVE);

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		lockmgr(&sc->sc_lock, LK_RELEASE);
		return;
	}
	ifp->if_flags &= ~IFF_RUNNING;

	/* Cancel all tasks. */
	while (taskqueue_cancel(sc->sc_handshake_taskqueue,
				&sc->sc_handshake_task, NULL) != 0) {
		taskqueue_drain(sc->sc_handshake_taskqueue,
				&sc->sc_handshake_task);
	}
	for (i = 0; i < ncpus; i++) {
		while (taskqueue_cancel(wg_taskqueues[i],
					&sc->sc_encrypt_tasks[i], NULL) != 0) {
			taskqueue_drain(wg_taskqueues[i],
					&sc->sc_encrypt_tasks[i]);
		}
		while (taskqueue_cancel(wg_taskqueues[i],
					&sc->sc_decrypt_tasks[i], NULL) != 0) {
			taskqueue_drain(wg_taskqueues[i],
					&sc->sc_decrypt_tasks[i]);
		}
	}

	TAILQ_FOREACH(peer, &sc->sc_peers, p_entry) {
		wg_queue_purge(&peer->p_stage_queue);
		wg_timers_disable(peer);
	}

	wg_queue_purge(&sc->sc_handshake_queue);

	TAILQ_FOREACH(peer, &sc->sc_peers, p_entry) {
		noise_remote_handshake_clear(peer->p_remote);
		noise_remote_keypairs_clear(peer->p_remote);
	}

	ifp->if_link_state = LINK_STATE_DOWN;
	if_link_state_change(ifp);
	wg_socket_uninit(sc);

	lockmgr(&sc->sc_lock, LK_RELEASE);
}

static int
wg_clone_create(struct if_clone *ifc __unused, int unit,
		caddr_t params __unused, caddr_t data __unused)
{
	struct wg_softc *sc;
	struct ifnet *ifp;
	int i;

	sc = kmalloc(sizeof(*sc), M_WG, M_WAITOK | M_ZERO);

	if (!rn_inithead(&sc->sc_aip4, wg_maskhead,
			 offsetof(struct aip_addr, in)) ||
	    !rn_inithead(&sc->sc_aip6, wg_maskhead,
			 offsetof(struct aip_addr, in6))) {
		if (sc->sc_aip4 != NULL)
			rn_freehead(sc->sc_aip4);
		if (sc->sc_aip6 != NULL)
			rn_freehead(sc->sc_aip6);
		kfree(sc, M_WG);
		return (ENOMEM);
	}

	lockinit(&sc->sc_lock, "wg softc lock", 0, 0);
	lockinit(&sc->sc_aip_lock, "wg aip lock", 0, 0);

	sc->sc_local = noise_local_alloc();
	sc->sc_cookie = cookie_checker_alloc();

	TAILQ_INIT(&sc->sc_peers);

	sc->sc_handshake_taskqueue = wg_taskqueues[karc4random() % ncpus];
	TASK_INIT(&sc->sc_handshake_task, 0, wg_handshake_worker, sc);
	wg_queue_init(&sc->sc_handshake_queue, "hsq");

	sc->sc_encrypt_tasks = kmalloc(sizeof(*sc->sc_encrypt_tasks) * ncpus,
				       M_WG, M_WAITOK | M_ZERO);
	sc->sc_decrypt_tasks = kmalloc(sizeof(*sc->sc_decrypt_tasks) * ncpus,
				       M_WG, M_WAITOK | M_ZERO);
	for (i = 0; i < ncpus; i++) {
		TASK_INIT(&sc->sc_encrypt_tasks[i], 0, wg_encrypt_worker, sc);
		TASK_INIT(&sc->sc_decrypt_tasks[i], 0, wg_decrypt_worker, sc);
	}
	wg_queue_init(&sc->sc_encrypt_parallel, "encp");
	wg_queue_init(&sc->sc_decrypt_parallel, "decp");

	ifp = sc->sc_ifp = if_alloc(IFT_WIREGUARD);
	if_initname(ifp, wgname, unit);
	ifp->if_softc = sc;
	ifp->if_mtu = DEFAULT_MTU;
	ifp->if_flags = IFF_NOARP | IFF_MULTICAST;
	ifp->if_capabilities = ifp->if_capenable = IFCAP_RXCSUM;
	ifp->if_output = wg_output;
	ifp->if_ioctl = wg_ioctl;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	if_attach(ifp, NULL);

	/* DLT_NULL link-layer header: a 4-byte field in host byte order */
	bpfattach(ifp, DLT_NULL, sizeof(uint32_t));

#ifdef INET6
	/* NOTE: ND_IFINFO() is only available after if_attach(). */
	ND_IFINFO(ifp)->flags &= ~ND6_IFF_AUTO_LINKLOCAL;
	ND_IFINFO(ifp)->flags |= ND6_IFF_NO_DAD;
#endif

	lockmgr(&wg_mtx, LK_EXCLUSIVE);
	LIST_INSERT_HEAD(&wg_list, sc, sc_entry);
	lockmgr(&wg_mtx, LK_RELEASE);

	return (0);
}

static int
wg_clone_destroy(struct ifnet *ifp)
{
	struct wg_softc *sc = ifp->if_softc;

	wg_down(sc);

	lockmgr(&sc->sc_lock, LK_EXCLUSIVE);

	kfree(sc->sc_encrypt_tasks, M_WG);
	kfree(sc->sc_decrypt_tasks, M_WG);
	wg_queue_deinit(&sc->sc_handshake_queue);
	wg_queue_deinit(&sc->sc_encrypt_parallel);
	wg_queue_deinit(&sc->sc_decrypt_parallel);

	wg_peer_destroy_all(sc);

	/*
	 * Detach and free the interface before the sc_aip4 and sc_aip6 radix
	 * trees, because the purge of interface's IPv6 addresses can cause
	 * packet transmission and thus wg_aip_lookup() calls.
	 */
	bpfdetach(ifp);
	if_detach(ifp);
	if_free(ifp);

	/*
	 * All peers have been removed, so the sc_aip4 and sc_aip6 radix trees
	 * must be empty now.
	 */
	rn_freehead(sc->sc_aip4);
	rn_freehead(sc->sc_aip6);
	lockuninit(&sc->sc_aip_lock);

	cookie_checker_free(sc->sc_cookie);
	noise_local_free(sc->sc_local);

	lockmgr(&wg_mtx, LK_EXCLUSIVE);
	LIST_REMOVE(sc, sc_entry);
	lockmgr(&wg_mtx, LK_RELEASE);

	lockmgr(&sc->sc_lock, LK_RELEASE);
	lockuninit(&sc->sc_lock);
	kfree(sc, M_WG);

	return (0);
}

/*----------------------------------------------------------------------------*/
/* Module Interface */

#ifdef WG_SELFTESTS
#include "selftest/allowedips.c"
static bool
wg_run_selftests(void)
{
	bool ret = true;

	ret &= wg_allowedips_selftest();
	ret &= noise_counter_selftest();
	ret &= cookie_selftest();

	kprintf("%s: %s\n", __func__, ret ? "pass" : "FAIL");
	return (ret);
}
#else /* !WG_SELFTESTS */
static inline bool
wg_run_selftests(void)
{
	return (true);
}
#endif /* WG_SELFTESTS */

static struct if_clone wg_cloner = IF_CLONE_INITIALIZER(
	wgname, wg_clone_create, wg_clone_destroy, 0, IF_MAXUNIT);

static int
wg_module_init(void)
{
	int i, ret;

	lockinit(&wg_mtx, "wg mtx lock", 0, 0);

	wg_packet_zone = objcache_create_simple(M_WG_PACKET,
						sizeof(struct wg_packet));
	if (wg_packet_zone == NULL)
		return (ENOMEM);

	wg_taskqueues = kmalloc(sizeof(*wg_taskqueues) * ncpus, M_WG,
				M_WAITOK | M_ZERO);
	for (i = 0; i < ncpus; i++) {
		wg_taskqueues[i] = taskqueue_create("wg_taskq", M_WAITOK,
						    taskqueue_thread_enqueue,
						    &wg_taskqueues[i]);
		taskqueue_start_threads(&wg_taskqueues[i], 1,
					TDPRI_KERN_DAEMON, i,
					"wg_taskq_cpu_%d", i);
	}

	if (!rn_inithead(&wg_maskhead, NULL, 0))
		return (ENOMEM);

	ret = cookie_init();
	if (ret != 0)
		return (ret);
	ret = noise_init();
	if (ret != 0)
		return (ret);

	ret = if_clone_attach(&wg_cloner);
	if (ret != 0)
		return (ret);

	if (!wg_run_selftests())
		return (ENOTRECOVERABLE);

	return (0);
}

static int
wg_module_deinit(void)
{
	int i;

	lockmgr(&wg_mtx, LK_EXCLUSIVE);

	if (!LIST_EMPTY(&wg_list)) {
		lockmgr(&wg_mtx, LK_RELEASE);
		return (EBUSY);
	}

	if_clone_detach(&wg_cloner);

	noise_deinit();
	cookie_deinit();

	for (i = 0; i < ncpus; i++)
		taskqueue_free(wg_taskqueues[i]);
	kfree(wg_taskqueues, M_WG);

	rn_flush(wg_maskhead, rn_freemask);
	rn_freehead(wg_maskhead);

	if (wg_packet_zone != NULL)
		objcache_destroy(wg_packet_zone);

	lockmgr(&wg_mtx, LK_RELEASE);
	lockuninit(&wg_mtx);

	return (0);
}

static int
wg_module_event_handler(module_t mod __unused, int what, void *arg __unused)
{
	switch (what) {
	case MOD_LOAD:
		return wg_module_init();
	case MOD_UNLOAD:
		return wg_module_deinit();
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t wg_moduledata = {
	"if_wg",
	wg_module_event_handler,
	NULL
};

DECLARE_MODULE(if_wg, wg_moduledata, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(if_wg, 1); /* WireGuard version */
MODULE_DEPEND(if_wg, crypto, 1, 1, 1);
