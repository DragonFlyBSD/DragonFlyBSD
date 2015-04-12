/*-
 * Copyright (c) 2011, Bryan Venteicher <bryanv@daemoninthecloset.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Driver for VirtIO network devices. */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/random.h>
#include <sys/sglist.h>
#include <sys/serialize.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>
#include <net/ifq_var.h>

#include <net/bpf.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>

#include <dev/virtual/virtio/virtio/virtio.h>
#include <dev/virtual/virtio/virtio/virtqueue.h>

#include "virtio_net.h"
#include "virtio_if.h"

struct vtnet_statistics {
	unsigned long		mbuf_alloc_failed;

	unsigned long		rx_frame_too_large;
	unsigned long		rx_enq_replacement_failed;
	unsigned long		rx_mergeable_failed;
	unsigned long		rx_csum_bad_ethtype;
	unsigned long		rx_csum_bad_start;
	unsigned long		rx_csum_bad_ipproto;
	unsigned long		rx_csum_bad_offset;
	unsigned long		rx_csum_failed;
	unsigned long		rx_csum_offloaded;
	unsigned long		rx_task_rescheduled;

	unsigned long		tx_csum_offloaded;
	unsigned long		tx_tso_offloaded;
	unsigned long		tx_csum_bad_ethtype;
	unsigned long		tx_tso_bad_ethtype;
	unsigned long		tx_task_rescheduled;
};

struct vtnet_softc {
	device_t		vtnet_dev;
	struct ifnet		*vtnet_ifp;
	struct lwkt_serialize	vtnet_slz;

	uint32_t		vtnet_flags;
#define VTNET_FLAG_LINK		0x0001
#define VTNET_FLAG_SUSPENDED	0x0002
#define VTNET_FLAG_CTRL_VQ	0x0004
#define VTNET_FLAG_CTRL_RX	0x0008
#define VTNET_FLAG_VLAN_FILTER	0x0010
#define VTNET_FLAG_TSO_ECN	0x0020
#define VTNET_FLAG_MRG_RXBUFS	0x0040
#define VTNET_FLAG_LRO_NOMRG	0x0080

	struct virtqueue	*vtnet_rx_vq;
	struct virtqueue	*vtnet_tx_vq;
	struct virtqueue	*vtnet_ctrl_vq;

	struct vtnet_tx_header	*vtnet_txhdrarea;
	uint32_t		vtnet_txhdridx;
	struct vtnet_mac_filter *vtnet_macfilter;

	int			vtnet_hdr_size;
	int			vtnet_tx_size;
	int			vtnet_rx_size;
	int			vtnet_rx_process_limit;
	int			vtnet_rx_mbuf_size;
	int			vtnet_rx_mbuf_count;
	int			vtnet_if_flags;
	int			vtnet_watchdog_timer;
	uint64_t		vtnet_features;

	struct task		vtnet_cfgchg_task;

	struct vtnet_statistics	vtnet_stats;

	struct callout		vtnet_tick_ch;

	eventhandler_tag	vtnet_vlan_attach;
	eventhandler_tag	vtnet_vlan_detach;

	struct ifmedia		vtnet_media;
	/*
	 * Fake media type; the host does not provide us with
	 * any real media information.
	 */
#define VTNET_MEDIATYPE		(IFM_ETHER | IFM_1000_T | IFM_FDX)
	char			vtnet_hwaddr[ETHER_ADDR_LEN];

	/*
	 * During reset, the host's VLAN filtering table is lost. The
	 * array below is used to restore all the VLANs configured on
	 * this interface after a reset.
	 */
#define VTNET_VLAN_SHADOW_SIZE	(4096 / 32)
	int			vtnet_nvlans;
	uint32_t		vtnet_vlan_shadow[VTNET_VLAN_SHADOW_SIZE];

	char			vtnet_mtx_name[16];
};

/*
 * When mergeable buffers are not negotiated, the vtnet_rx_header structure
 * below is placed at the beginning of the mbuf data. Use 4 bytes of pad to
 * both keep the VirtIO header and the data non-contiguous and to keep the
 * frame's payload 4 byte aligned.
 *
 * When mergeable buffers are negotiated, the host puts the VirtIO header in
 * the beginning of the first mbuf's data.
 */
#define VTNET_RX_HEADER_PAD	4
struct vtnet_rx_header {
	struct virtio_net_hdr	vrh_hdr;
	char			vrh_pad[VTNET_RX_HEADER_PAD];
} __packed;

/*
 * For each outgoing frame, the vtnet_tx_header below is allocated from
 * the vtnet_tx_header_zone.
 */
struct vtnet_tx_header {
	union {
		struct virtio_net_hdr		hdr;
		struct virtio_net_hdr_mrg_rxbuf	mhdr;
	} vth_uhdr;

	struct mbuf		*vth_mbuf;
};

MALLOC_DEFINE(M_VTNET, "VTNET_TX", "Outgoing VTNET TX frame header");

/*
 * The VirtIO specification does not place a limit on the number of MAC
 * addresses the guest driver may request to be filtered. In practice,
 * the host is constrained by available resources. To simplify this driver,
 * impose a reasonably high limit of MAC addresses we will filter before
 * falling back to promiscuous or all-multicast modes.
 */
#define VTNET_MAX_MAC_ENTRIES	128

struct vtnet_mac_table {
	uint32_t		nentries;
	uint8_t			macs[VTNET_MAX_MAC_ENTRIES][ETHER_ADDR_LEN];
} __packed;

struct vtnet_mac_filter {
	struct vtnet_mac_table	vmf_unicast;
	uint32_t		vmf_pad; /* Make tables non-contiguous. */
	struct vtnet_mac_table	vmf_multicast;
};

#define VTNET_WATCHDOG_TIMEOUT	5
#define VTNET_CSUM_OFFLOAD	(CSUM_TCP | CSUM_UDP)

/* Features desired/implemented by this driver. */
#define VTNET_FEATURES 		\
    (VIRTIO_NET_F_MAC		| \
     VIRTIO_NET_F_STATUS	| \
     VIRTIO_NET_F_CTRL_VQ	| \
     VIRTIO_NET_F_CTRL_RX	| \
     VIRTIO_NET_F_CTRL_VLAN	| \
     VIRTIO_NET_F_CSUM		| \
     VIRTIO_NET_F_HOST_TSO4	| \
     VIRTIO_NET_F_HOST_TSO6	| \
     VIRTIO_NET_F_HOST_ECN	| \
     VIRTIO_NET_F_GUEST_CSUM	| \
     VIRTIO_NET_F_GUEST_TSO4	| \
     VIRTIO_NET_F_GUEST_TSO6	| \
     VIRTIO_NET_F_GUEST_ECN	| \
     VIRTIO_NET_F_MRG_RXBUF)

/*
 * The VIRTIO_NET_F_GUEST_TSO[46] features permit the host to send us
 * frames larger than 1514 bytes. We do not yet support software LRO
 * via tcp_lro_rx().
 */
#define VTNET_LRO_FEATURES (VIRTIO_NET_F_GUEST_TSO4 | \
			    VIRTIO_NET_F_GUEST_TSO6 | VIRTIO_NET_F_GUEST_ECN)

#define VTNET_MAX_MTU		65536
#define VTNET_MAX_RX_SIZE	65550

/*
 * Used to preallocate the Vq indirect descriptors. The first segment
 * is reserved for the header.
 */
#define VTNET_MIN_RX_SEGS	2
#define VTNET_MAX_RX_SEGS	34
#define VTNET_MAX_TX_SEGS	34

#define IFCAP_TSO4              0x00100 /* can do TCP Segmentation Offload */
#define IFCAP_TSO6              0x00200 /* can do TCP6 Segmentation Offload */
#define IFCAP_LRO               0x00400 /* can do Large Receive Offload */
#define IFCAP_VLAN_HWFILTER     0x10000 /* interface hw can filter vlan tag */
#define IFCAP_VLAN_HWTSO        0x40000 /* can do IFCAP_TSO on VLANs */


/*
 * Assert we can receive and transmit the maximum with regular
 * size clusters.
 */
CTASSERT(((VTNET_MAX_RX_SEGS - 1) * MCLBYTES) >= VTNET_MAX_RX_SIZE);
CTASSERT(((VTNET_MAX_TX_SEGS - 1) * MCLBYTES) >= VTNET_MAX_MTU);

/*
 * Determine how many mbufs are in each receive buffer. For LRO without
 * mergeable descriptors, we must allocate an mbuf chain large enough to
 * hold both the vtnet_rx_header and the maximum receivable data.
 */
#define VTNET_NEEDED_RX_MBUFS(_sc)					\
	((_sc)->vtnet_flags & VTNET_FLAG_LRO_NOMRG) == 0 ? 1 :		\
	howmany(sizeof(struct vtnet_rx_header) + VTNET_MAX_RX_SIZE,	\
	(_sc)->vtnet_rx_mbuf_size)

static int	vtnet_modevent(module_t, int, void *);

static int	vtnet_probe(device_t);
static int	vtnet_attach(device_t);
static int	vtnet_detach(device_t);
static int	vtnet_suspend(device_t);
static int	vtnet_resume(device_t);
static int	vtnet_shutdown(device_t);
static int	vtnet_config_change(device_t);

static void	vtnet_negotiate_features(struct vtnet_softc *);
static int	vtnet_alloc_virtqueues(struct vtnet_softc *);
static void	vtnet_get_hwaddr(struct vtnet_softc *);
static void	vtnet_set_hwaddr(struct vtnet_softc *);
static int	vtnet_is_link_up(struct vtnet_softc *);
static void	vtnet_update_link_status(struct vtnet_softc *);
#if 0
static void	vtnet_watchdog(struct vtnet_softc *);
#endif
static void	vtnet_config_change_task(void *, int);
static int	vtnet_change_mtu(struct vtnet_softc *, int);
static int	vtnet_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

static int	vtnet_init_rx_vq(struct vtnet_softc *);
static void	vtnet_free_rx_mbufs(struct vtnet_softc *);
static void	vtnet_free_tx_mbufs(struct vtnet_softc *);
static void	vtnet_free_ctrl_vq(struct vtnet_softc *);

static struct mbuf * vtnet_alloc_rxbuf(struct vtnet_softc *, int,
		    struct mbuf **);
static int	vtnet_replace_rxbuf(struct vtnet_softc *,
		    struct mbuf *, int);
static int	vtnet_newbuf(struct vtnet_softc *);
static void	vtnet_discard_merged_rxbuf(struct vtnet_softc *, int);
static void	vtnet_discard_rxbuf(struct vtnet_softc *, struct mbuf *);
static int	vtnet_enqueue_rxbuf(struct vtnet_softc *, struct mbuf *);
static void	vtnet_vlan_tag_remove(struct mbuf *);
static int	vtnet_rx_csum(struct vtnet_softc *, struct mbuf *,
		    struct virtio_net_hdr *);
static int	vtnet_rxeof_merged(struct vtnet_softc *, struct mbuf *, int);
static int	vtnet_rxeof(struct vtnet_softc *, int, int *);
static void	vtnet_rx_intr_task(void *);
static int	vtnet_rx_vq_intr(void *);

static void	vtnet_txeof(struct vtnet_softc *);
static struct mbuf * vtnet_tx_offload(struct vtnet_softc *, struct mbuf *,
		    struct virtio_net_hdr *);
static int	vtnet_enqueue_txbuf(struct vtnet_softc *, struct mbuf **,
		    struct vtnet_tx_header *);
static int	vtnet_encap(struct vtnet_softc *, struct mbuf **);
static void	vtnet_start_locked(struct ifnet *, struct ifaltq_subque *);
static void	vtnet_start(struct ifnet *, struct ifaltq_subque *);
static void	vtnet_tick(void *);
static void	vtnet_tx_intr_task(void *);
static int	vtnet_tx_vq_intr(void *);

static void	vtnet_stop(struct vtnet_softc *);
static int	vtnet_reinit(struct vtnet_softc *);
static void	vtnet_init_locked(struct vtnet_softc *);
static void	vtnet_init(void *);

static void	vtnet_exec_ctrl_cmd(struct vtnet_softc *, void *,
		    struct sglist *, int, int);

static void	vtnet_rx_filter(struct vtnet_softc *sc);
static int	vtnet_ctrl_rx_cmd(struct vtnet_softc *, int, int);
static int	vtnet_set_promisc(struct vtnet_softc *, int);
static int	vtnet_set_allmulti(struct vtnet_softc *, int);
static void	vtnet_rx_filter_mac(struct vtnet_softc *);

static int	vtnet_exec_vlan_filter(struct vtnet_softc *, int, uint16_t);
static void	vtnet_rx_filter_vlan(struct vtnet_softc *);
static void	vtnet_set_vlan_filter(struct vtnet_softc *, int, uint16_t);
static void	vtnet_register_vlan(void *, struct ifnet *, uint16_t);
static void	vtnet_unregister_vlan(void *, struct ifnet *, uint16_t);

static int	vtnet_ifmedia_upd(struct ifnet *);
static void	vtnet_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void	vtnet_add_statistics(struct vtnet_softc *);

static int	vtnet_enable_rx_intr(struct vtnet_softc *);
static int	vtnet_enable_tx_intr(struct vtnet_softc *);
static void	vtnet_disable_rx_intr(struct vtnet_softc *);
static void	vtnet_disable_tx_intr(struct vtnet_softc *);

/* Tunables. */
static int vtnet_csum_disable = 0;
TUNABLE_INT("hw.vtnet.csum_disable", &vtnet_csum_disable);
static int vtnet_tso_disable = 1;
TUNABLE_INT("hw.vtnet.tso_disable", &vtnet_tso_disable);
static int vtnet_lro_disable = 1;
TUNABLE_INT("hw.vtnet.lro_disable", &vtnet_lro_disable);

/*
 * Reducing the number of transmit completed interrupts can
 * improve performance. To do so, the define below keeps the
 * Tx vq interrupt disabled and adds calls to vtnet_txeof()
 * in the start and watchdog paths. The price to pay for this
 * is the m_free'ing of transmitted mbufs may be delayed until
 * the watchdog fires.
 */
#define VTNET_TX_INTR_MODERATION

static struct virtio_feature_desc vtnet_feature_desc[] = {
	{ VIRTIO_NET_F_CSUM,		"TxChecksum"	},
	{ VIRTIO_NET_F_GUEST_CSUM,	"RxChecksum"	},
	{ VIRTIO_NET_F_MAC,		"MacAddress"	},
	{ VIRTIO_NET_F_GSO,		"TxAllGSO"	},
	{ VIRTIO_NET_F_GUEST_TSO4,	"RxTSOv4"	},
	{ VIRTIO_NET_F_GUEST_TSO6,	"RxTSOv6"	},
	{ VIRTIO_NET_F_GUEST_ECN,	"RxECN"		},
	{ VIRTIO_NET_F_GUEST_UFO,	"RxUFO"		},
	{ VIRTIO_NET_F_HOST_TSO4,	"TxTSOv4"	},
	{ VIRTIO_NET_F_HOST_TSO6,	"TxTSOv6"	},
	{ VIRTIO_NET_F_HOST_ECN,	"TxTSOECN"	},
	{ VIRTIO_NET_F_HOST_UFO,	"TxUFO"		},
	{ VIRTIO_NET_F_MRG_RXBUF,	"MrgRxBuf"	},
	{ VIRTIO_NET_F_STATUS,		"Status"	},
	{ VIRTIO_NET_F_CTRL_VQ,		"ControlVq"	},
	{ VIRTIO_NET_F_CTRL_RX,		"RxMode"	},
	{ VIRTIO_NET_F_CTRL_VLAN,	"VLanFilter"	},
	{ VIRTIO_NET_F_CTRL_RX_EXTRA,	"RxModeExtra"	},
	{ VIRTIO_NET_F_MQ,		"RFS"		},
	{ 0, NULL }
};

static device_method_t vtnet_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtnet_probe),
	DEVMETHOD(device_attach,	vtnet_attach),
	DEVMETHOD(device_detach,	vtnet_detach),
	DEVMETHOD(device_suspend,	vtnet_suspend),
	DEVMETHOD(device_resume,	vtnet_resume),
	DEVMETHOD(device_shutdown,	vtnet_shutdown),

	/* VirtIO methods. */
	DEVMETHOD(virtio_config_change, vtnet_config_change),

	{ 0, 0 }
};

static driver_t vtnet_driver = {
	"vtnet",
	vtnet_methods,
	sizeof(struct vtnet_softc)
};

static devclass_t vtnet_devclass;

DRIVER_MODULE(vtnet, virtio_pci, vtnet_driver, vtnet_devclass,
	      vtnet_modevent, 0);
MODULE_VERSION(vtnet, 1);
MODULE_DEPEND(vtnet, virtio, 1, 1, 1);

static int
vtnet_modevent(module_t mod, int type, void *unused)
{
	int error;

	error = 0;

	switch (type) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
vtnet_probe(device_t dev)
{
	if (virtio_get_device_type(dev) != VIRTIO_ID_NETWORK)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Networking Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtnet_attach(device_t dev)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;
	int tx_size, error;

	sc = device_get_softc(dev);
	sc->vtnet_dev = dev;

	lwkt_serialize_init(&sc->vtnet_slz);
	callout_init(&sc->vtnet_tick_ch);

	ifmedia_init(&sc->vtnet_media, IFM_IMASK, vtnet_ifmedia_upd,
		     vtnet_ifmedia_sts);
	ifmedia_add(&sc->vtnet_media, VTNET_MEDIATYPE, 0, NULL);
	ifmedia_set(&sc->vtnet_media, VTNET_MEDIATYPE);

	vtnet_add_statistics(sc);

	virtio_set_feature_desc(dev, vtnet_feature_desc);
	vtnet_negotiate_features(sc);

	if (virtio_with_feature(dev, VIRTIO_NET_F_MRG_RXBUF)) {
		sc->vtnet_flags |= VTNET_FLAG_MRG_RXBUFS;
		sc->vtnet_hdr_size = sizeof(struct virtio_net_hdr_mrg_rxbuf);
	} else {
		sc->vtnet_hdr_size = sizeof(struct virtio_net_hdr);
	}

	sc->vtnet_rx_mbuf_size = MCLBYTES;
	sc->vtnet_rx_mbuf_count = VTNET_NEEDED_RX_MBUFS(sc);

	if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_VQ)) {
		sc->vtnet_flags |= VTNET_FLAG_CTRL_VQ;

		if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_RX))
			sc->vtnet_flags |= VTNET_FLAG_CTRL_RX;
		if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_VLAN))
			sc->vtnet_flags |= VTNET_FLAG_VLAN_FILTER;
	}

	vtnet_get_hwaddr(sc);

	error = vtnet_alloc_virtqueues(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueues\n");
		goto fail;
	}

	ifp = sc->vtnet_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = vtnet_init;
	ifp->if_start = vtnet_start;
	ifp->if_ioctl = vtnet_ioctl;

	sc->vtnet_rx_size = virtqueue_size(sc->vtnet_rx_vq);
	sc->vtnet_rx_process_limit = sc->vtnet_rx_size;

	tx_size = virtqueue_size(sc->vtnet_tx_vq);
	sc->vtnet_tx_size = tx_size;
	sc->vtnet_txhdridx = 0;
	sc->vtnet_txhdrarea = contigmalloc(
	    ((sc->vtnet_tx_size / 2) + 1) * sizeof(struct vtnet_tx_header),
	    M_VTNET, M_WAITOK, 0, BUS_SPACE_MAXADDR, 4, 0);
	if (sc->vtnet_txhdrarea == NULL) {
		device_printf(dev, "cannot contigmalloc the tx headers\n");
		goto fail;
	}
	sc->vtnet_macfilter = contigmalloc(
	    sizeof(struct vtnet_mac_filter),
	    M_DEVBUF, M_WAITOK, 0, BUS_SPACE_MAXADDR, 4, 0);
	if (sc->vtnet_macfilter == NULL) {
		device_printf(dev,
		    "cannot contigmalloc the mac filter table\n");
		goto fail;
	}
	ifq_set_maxlen(&ifp->if_snd, tx_size - 1);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, sc->vtnet_hwaddr, NULL);

	if (virtio_with_feature(dev, VIRTIO_NET_F_STATUS)){
		//ifp->if_capabilities |= IFCAP_LINKSTATE;
		 kprintf("add dynamic link state\n");
	}

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_JUMBO_MTU | IFCAP_VLAN_MTU;

	if (virtio_with_feature(dev, VIRTIO_NET_F_CSUM)) {
		ifp->if_capabilities |= IFCAP_TXCSUM;

		if (virtio_with_feature(dev, VIRTIO_NET_F_HOST_TSO4))
			ifp->if_capabilities |= IFCAP_TSO4;
		if (virtio_with_feature(dev, VIRTIO_NET_F_HOST_TSO6))
			ifp->if_capabilities |= IFCAP_TSO6;
		if (ifp->if_capabilities & IFCAP_TSO)
			ifp->if_capabilities |= IFCAP_VLAN_HWTSO;

		if (virtio_with_feature(dev, VIRTIO_NET_F_HOST_ECN))
			sc->vtnet_flags |= VTNET_FLAG_TSO_ECN;
	}

	if (virtio_with_feature(dev, VIRTIO_NET_F_GUEST_CSUM)) {
		ifp->if_capabilities |= IFCAP_RXCSUM;

		if (virtio_with_feature(dev, VIRTIO_NET_F_GUEST_TSO4) ||
		    virtio_with_feature(dev, VIRTIO_NET_F_GUEST_TSO6))
			ifp->if_capabilities |= IFCAP_LRO;
	}

	if (ifp->if_capabilities & IFCAP_HWCSUM) {
		/*
		 * VirtIO does not support VLAN tagging, but we can fake
		 * it by inserting and removing the 802.1Q header during
		 * transmit and receive. We are then able to do checksum
		 * offloading of VLAN frames.
		 */
		ifp->if_capabilities |=
			IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM;
	}

	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Capabilities after here are not enabled by default.
	 */

	if (sc->vtnet_flags & VTNET_FLAG_VLAN_FILTER) {
		ifp->if_capabilities |= IFCAP_VLAN_HWFILTER;

		sc->vtnet_vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
		    vtnet_register_vlan, sc, EVENTHANDLER_PRI_FIRST);
		sc->vtnet_vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
		    vtnet_unregister_vlan, sc, EVENTHANDLER_PRI_FIRST);
	}

	TASK_INIT(&sc->vtnet_cfgchg_task, 0, vtnet_config_change_task, sc);

	error = virtio_setup_intr(dev, &sc->vtnet_slz);
	if (error) {
		device_printf(dev, "cannot setup virtqueue interrupts\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	/*
	 * Device defaults to promiscuous mode for backwards
	 * compatibility. Turn it off if possible.
	 */
	if (sc->vtnet_flags & VTNET_FLAG_CTRL_RX) {
		lwkt_serialize_enter(&sc->vtnet_slz);
		if (vtnet_set_promisc(sc, 0) != 0) {
			ifp->if_flags |= IFF_PROMISC;
			device_printf(dev,
			    "cannot disable promiscuous mode\n");
		}
		lwkt_serialize_exit(&sc->vtnet_slz);
	} else
		ifp->if_flags |= IFF_PROMISC;

fail:
	if (error)
		vtnet_detach(dev);

	return (error);
}

static int
vtnet_detach(device_t dev)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->vtnet_ifp;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(&sc->vtnet_slz);
		vtnet_stop(sc);
		lwkt_serialize_exit(&sc->vtnet_slz);

		callout_stop(&sc->vtnet_tick_ch);
		taskqueue_drain(taskqueue_swi, &sc->vtnet_cfgchg_task);

		ether_ifdetach(ifp);
	}

	if (sc->vtnet_vlan_attach != NULL) {
		EVENTHANDLER_DEREGISTER(vlan_config, sc->vtnet_vlan_attach);
		sc->vtnet_vlan_attach = NULL;
	}
	if (sc->vtnet_vlan_detach != NULL) {
		EVENTHANDLER_DEREGISTER(vlan_unconfg, sc->vtnet_vlan_detach);
		sc->vtnet_vlan_detach = NULL;
	}

	if (ifp) {
		if_free(ifp);
		sc->vtnet_ifp = NULL;
	}

	if (sc->vtnet_rx_vq != NULL)
		vtnet_free_rx_mbufs(sc);
	if (sc->vtnet_tx_vq != NULL)
		vtnet_free_tx_mbufs(sc);
	if (sc->vtnet_ctrl_vq != NULL)
		vtnet_free_ctrl_vq(sc);

	if (sc->vtnet_txhdrarea != NULL) {
		contigfree(sc->vtnet_txhdrarea,
		    ((sc->vtnet_tx_size / 2) + 1) *
		    sizeof(struct vtnet_tx_header), M_VTNET);
		sc->vtnet_txhdrarea = NULL;
	}
	if (sc->vtnet_macfilter != NULL) {
		contigfree(sc->vtnet_macfilter,
		    sizeof(struct vtnet_mac_filter), M_DEVBUF);
		sc->vtnet_macfilter = NULL;
	}

	ifmedia_removeall(&sc->vtnet_media);

	return (0);
}

static int
vtnet_suspend(device_t dev)
{
	struct vtnet_softc *sc;

	sc = device_get_softc(dev);

	lwkt_serialize_enter(&sc->vtnet_slz);
	vtnet_stop(sc);
	sc->vtnet_flags |= VTNET_FLAG_SUSPENDED;
	lwkt_serialize_exit(&sc->vtnet_slz);

	return (0);
}

static int
vtnet_resume(device_t dev)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->vtnet_ifp;

	lwkt_serialize_enter(&sc->vtnet_slz);
	if (ifp->if_flags & IFF_UP)
		vtnet_init_locked(sc);
	sc->vtnet_flags &= ~VTNET_FLAG_SUSPENDED;
	lwkt_serialize_exit(&sc->vtnet_slz);

	return (0);
}

static int
vtnet_shutdown(device_t dev)
{

	/*
	 * Suspend already does all of what we need to
	 * do here; we just never expect to be resumed.
	 */
	return (vtnet_suspend(dev));
}

static int
vtnet_config_change(device_t dev)
{
	struct vtnet_softc *sc;

	sc = device_get_softc(dev);

	taskqueue_enqueue(taskqueue_thread[mycpuid], &sc->vtnet_cfgchg_task);

	return (1);
}

static void
vtnet_negotiate_features(struct vtnet_softc *sc)
{
	device_t dev;
	uint64_t mask, features;

	dev = sc->vtnet_dev;
	mask = 0;

	if (vtnet_csum_disable)
		mask |= VIRTIO_NET_F_CSUM | VIRTIO_NET_F_GUEST_CSUM;

	/*
	 * TSO and LRO are only available when their corresponding
	 * checksum offload feature is also negotiated.
	 */

	if (vtnet_csum_disable || vtnet_tso_disable)
		mask |= VIRTIO_NET_F_HOST_TSO4 | VIRTIO_NET_F_HOST_TSO6 |
		    VIRTIO_NET_F_HOST_ECN;

	if (vtnet_csum_disable || vtnet_lro_disable)
		mask |= VTNET_LRO_FEATURES;

	features = VTNET_FEATURES & ~mask;
	features |= VIRTIO_F_NOTIFY_ON_EMPTY;
	sc->vtnet_features = virtio_negotiate_features(dev, features);
}

static int
vtnet_alloc_virtqueues(struct vtnet_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info[3];
	int nvqs, rxsegs;

	dev = sc->vtnet_dev;
	nvqs = 2;

	/*
	 * Indirect descriptors are not needed for the Rx
	 * virtqueue when mergeable buffers are negotiated.
	 * The header is placed inline with the data, not
	 * in a separate descriptor, and mbuf clusters are
	 * always physically contiguous.
	 */
	if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
		rxsegs = sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG ?
		    VTNET_MAX_RX_SEGS : VTNET_MIN_RX_SEGS;
	} else
		rxsegs = 0;

	VQ_ALLOC_INFO_INIT(&vq_info[0], rxsegs,
	    vtnet_rx_vq_intr, sc, &sc->vtnet_rx_vq,
	    "%s receive", device_get_nameunit(dev));

	VQ_ALLOC_INFO_INIT(&vq_info[1], VTNET_MAX_TX_SEGS,
	    vtnet_tx_vq_intr, sc, &sc->vtnet_tx_vq,
	    "%s transmit", device_get_nameunit(dev));

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_VQ) {
		nvqs++;

		VQ_ALLOC_INFO_INIT(&vq_info[2], 0, NULL, NULL,
		    &sc->vtnet_ctrl_vq, "%s control",
		    device_get_nameunit(dev));
	}

	return (virtio_alloc_virtqueues(dev, 0, nvqs, vq_info));
}

static void
vtnet_get_hwaddr(struct vtnet_softc *sc)
{
	device_t dev;

	dev = sc->vtnet_dev;

	if (virtio_with_feature(dev, VIRTIO_NET_F_MAC)) {
		virtio_read_device_config(dev,
		    offsetof(struct virtio_net_config, mac),
		    sc->vtnet_hwaddr, ETHER_ADDR_LEN);
	} else {
		/* Generate random locally administered unicast address. */
		sc->vtnet_hwaddr[0] = 0xB2;
		karc4rand(&sc->vtnet_hwaddr[1], ETHER_ADDR_LEN - 1);

		vtnet_set_hwaddr(sc);
	}
}

static void
vtnet_set_hwaddr(struct vtnet_softc *sc)
{
	device_t dev;

	dev = sc->vtnet_dev;

	virtio_write_device_config(dev,
	    offsetof(struct virtio_net_config, mac),
	    sc->vtnet_hwaddr, ETHER_ADDR_LEN);
}

static int
vtnet_is_link_up(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;
	uint16_t status;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	ASSERT_SERIALIZED(&sc->vtnet_slz);

	status = virtio_read_dev_config_2(dev,
			offsetof(struct virtio_net_config, status));

	return ((status & VIRTIO_NET_S_LINK_UP) != 0);
}

static void
vtnet_update_link_status(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;
	struct ifaltq_subque *ifsq;
	int link;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;
	ifsq = ifq_get_subq_default(&ifp->if_snd);

	link = vtnet_is_link_up(sc);

	if (link && ((sc->vtnet_flags & VTNET_FLAG_LINK) == 0)) {
		sc->vtnet_flags |= VTNET_FLAG_LINK;
		if (bootverbose)
			device_printf(dev, "Link is up\n");
		ifp->if_link_state = LINK_STATE_UP;
		if_link_state_change(ifp);
		if (!ifsq_is_empty(ifsq))
			vtnet_start_locked(ifp, ifsq);
	} else if (!link && (sc->vtnet_flags & VTNET_FLAG_LINK)) {
		sc->vtnet_flags &= ~VTNET_FLAG_LINK;
		if (bootverbose)
			device_printf(dev, "Link is down\n");

		ifp->if_link_state = LINK_STATE_DOWN;
		if_link_state_change(ifp);
	}
}

#if 0
static void
vtnet_watchdog(struct vtnet_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vtnet_ifp;

#ifdef VTNET_TX_INTR_MODERATION
	vtnet_txeof(sc);
#endif

	if (sc->vtnet_watchdog_timer == 0 || --sc->vtnet_watchdog_timer)
		return;

	if_printf(ifp, "watchdog timeout -- resetting\n");
#ifdef VTNET_DEBUG
	virtqueue_dump(sc->vtnet_tx_vq);
#endif
	ifp->if_oerrors++;
	ifp->if_flags &= ~IFF_RUNNING;
	vtnet_init_locked(sc);
}
#endif

static void
vtnet_config_change_task(void *arg, int pending)
{
	struct vtnet_softc *sc;

	sc = arg;

	lwkt_serialize_enter(&sc->vtnet_slz);
	vtnet_update_link_status(sc);
	lwkt_serialize_exit(&sc->vtnet_slz);
}

static int
vtnet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data,struct ucred *cr)
{
	struct vtnet_softc *sc;
	struct ifreq *ifr;
	int reinit, mask, error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *) data;
	reinit = 0;
	error = 0;

	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > VTNET_MAX_MTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			lwkt_serialize_enter(&sc->vtnet_slz);
			error = vtnet_change_mtu(sc, ifr->ifr_mtu);
			lwkt_serialize_exit(&sc->vtnet_slz);
		}
		break;

	case SIOCSIFFLAGS:
		lwkt_serialize_enter(&sc->vtnet_slz);
		if ((ifp->if_flags & IFF_UP) == 0) {
			if (ifp->if_flags & IFF_RUNNING)
				vtnet_stop(sc);
		} else if (ifp->if_flags & IFF_RUNNING) {
			if ((ifp->if_flags ^ sc->vtnet_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) {
				if (sc->vtnet_flags & VTNET_FLAG_CTRL_RX)
					vtnet_rx_filter(sc);
				else
					error = ENOTSUP;
			}
		} else
			vtnet_init_locked(sc);

		if (error == 0)
			sc->vtnet_if_flags = ifp->if_flags;
		lwkt_serialize_exit(&sc->vtnet_slz);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		lwkt_serialize_enter(&sc->vtnet_slz);
		if ((sc->vtnet_flags & VTNET_FLAG_CTRL_RX) &&
		    (ifp->if_flags & IFF_RUNNING))
			vtnet_rx_filter_mac(sc);
		lwkt_serialize_exit(&sc->vtnet_slz);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->vtnet_media, cmd);
		break;

	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		lwkt_serialize_enter(&sc->vtnet_slz);

		if (mask & IFCAP_TXCSUM) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= VTNET_CSUM_OFFLOAD;
			else
				ifp->if_hwassist &= ~VTNET_CSUM_OFFLOAD;
		}

		if (mask & IFCAP_TSO4) {
			ifp->if_capenable ^= IFCAP_TSO4;
			if (ifp->if_capenable & IFCAP_TSO4)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}

		if (mask & IFCAP_RXCSUM) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			reinit = 1;
		}

		if (mask & IFCAP_LRO) {
			ifp->if_capenable ^= IFCAP_LRO;
			reinit = 1;
		}

		if (mask & IFCAP_VLAN_HWFILTER) {
			ifp->if_capenable ^= IFCAP_VLAN_HWFILTER;
			reinit = 1;
		}

		if (mask & IFCAP_VLAN_HWTSO)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;

		if (mask & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;

		if (reinit && (ifp->if_flags & IFF_RUNNING)) {
			ifp->if_flags &= ~IFF_RUNNING;
			vtnet_init_locked(sc);
		}
		//VLAN_CAPABILITIES(ifp);

		lwkt_serialize_exit(&sc->vtnet_slz);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static int
vtnet_change_mtu(struct vtnet_softc *sc, int new_mtu)
{
	struct ifnet *ifp;
	int new_frame_size, clsize;

	ifp = sc->vtnet_ifp;

	if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
		new_frame_size = sizeof(struct vtnet_rx_header) +
		    sizeof(struct ether_vlan_header) + new_mtu;

		if (new_frame_size > MJUM9BYTES)
			return (EINVAL);

		if (new_frame_size <= MCLBYTES)
			clsize = MCLBYTES;
		else
			clsize = MJUM9BYTES;
	} else {
		new_frame_size = sizeof(struct virtio_net_hdr_mrg_rxbuf) +
		    sizeof(struct ether_vlan_header) + new_mtu;

		if (new_frame_size <= MCLBYTES)
			clsize = MCLBYTES;
		else
			clsize = MJUMPAGESIZE;
	}

	sc->vtnet_rx_mbuf_size = clsize;
	sc->vtnet_rx_mbuf_count = VTNET_NEEDED_RX_MBUFS(sc);
	KASSERT(sc->vtnet_rx_mbuf_count < VTNET_MAX_RX_SEGS,
	    ("too many rx mbufs: %d", sc->vtnet_rx_mbuf_count));

	ifp->if_mtu = new_mtu;

	if (ifp->if_flags & IFF_RUNNING) {
		ifp->if_flags &= ~IFF_RUNNING;
		vtnet_init_locked(sc);
	}

	return (0);
}

static int
vtnet_init_rx_vq(struct vtnet_softc *sc)
{
	struct virtqueue *vq;
	int nbufs, error;

	vq = sc->vtnet_rx_vq;
	nbufs = 0;
	error = ENOSPC;

	while (!virtqueue_full(vq)) {
		if ((error = vtnet_newbuf(sc)) != 0)
			break;
		nbufs++;
	}

	if (nbufs > 0) {
		virtqueue_notify(vq, &sc->vtnet_slz);

		/*
		 * EMSGSIZE signifies the virtqueue did not have enough
		 * entries available to hold the last mbuf. This is not
		 * an error. We should not get ENOSPC since we check if
		 * the virtqueue is full before attempting to add a
		 * buffer.
		 */
		if (error == EMSGSIZE)
			error = 0;
	}

	return (error);
}

static void
vtnet_free_rx_mbufs(struct vtnet_softc *sc)
{
	struct virtqueue *vq;
	struct mbuf *m;
	int last;

	vq = sc->vtnet_rx_vq;
	last = 0;

	while ((m = virtqueue_drain(vq, &last)) != NULL)
		m_freem(m);

	KASSERT(virtqueue_empty(vq), ("mbufs remaining in Rx Vq"));
}

static void
vtnet_free_tx_mbufs(struct vtnet_softc *sc)
{
	struct virtqueue *vq;
	struct vtnet_tx_header *txhdr;
	int last;

	vq = sc->vtnet_tx_vq;
	last = 0;

	while ((txhdr = virtqueue_drain(vq, &last)) != NULL) {
		m_freem(txhdr->vth_mbuf);
	}

	KASSERT(virtqueue_empty(vq), ("mbufs remaining in Tx Vq"));
}

static void
vtnet_free_ctrl_vq(struct vtnet_softc *sc)
{
	/*
	 * The control virtqueue is only polled, therefore
	 * it should already be empty.
	 */
	KASSERT(virtqueue_empty(sc->vtnet_ctrl_vq),
		("Ctrl Vq not empty"));
}

static struct mbuf *
vtnet_alloc_rxbuf(struct vtnet_softc *sc, int nbufs, struct mbuf **m_tailp)
{
	struct mbuf *m_head, *m_tail, *m;
	int i, clsize;

	clsize = sc->vtnet_rx_mbuf_size;

	/*use getcl instead of getjcl. see  if_mxge.c comment line 2398*/
	//m_head = m_getjcl(M_DONTWAIT, MT_DATA, M_PKTHDR, clsize);
	m_head = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR );
	if (m_head == NULL)
		goto fail;

	m_head->m_len = clsize;
	m_tail = m_head;

	if (nbufs > 1) {
		KASSERT(sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG,
			("chained Rx mbuf requested without LRO_NOMRG"));

		for (i = 0; i < nbufs - 1; i++) {
			//m = m_getjcl(M_DONTWAIT, MT_DATA, 0, clsize);
			m = m_getcl(M_NOWAIT, MT_DATA, 0);
			if (m == NULL)
				goto fail;

			m->m_len = clsize;
			m_tail->m_next = m;
			m_tail = m;
		}
	}

	if (m_tailp != NULL)
		*m_tailp = m_tail;

	return (m_head);

fail:
	sc->vtnet_stats.mbuf_alloc_failed++;
	m_freem(m_head);

	return (NULL);
}

static int
vtnet_replace_rxbuf(struct vtnet_softc *sc, struct mbuf *m0, int len0)
{
	struct mbuf *m, *m_prev;
	struct mbuf *m_new, *m_tail;
	int len, clsize, nreplace, error;

	m = m0;
	m_prev = NULL;
	len = len0;

	m_tail = NULL;
	clsize = sc->vtnet_rx_mbuf_size;
	nreplace = 0;

	if (m->m_next != NULL)
		KASSERT(sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG,
		    ("chained Rx mbuf without LRO_NOMRG"));

	/*
	 * Since LRO_NOMRG mbuf chains are so large, we want to avoid
	 * allocating an entire chain for each received frame. When
	 * the received frame's length is less than that of the chain,
	 * the unused mbufs are reassigned to the new chain.
	 */
	while (len > 0) {
		/*
		 * Something is seriously wrong if we received
		 * a frame larger than the mbuf chain. Drop it.
		 */
		if (m == NULL) {
			sc->vtnet_stats.rx_frame_too_large++;
			return (EMSGSIZE);
		}

		KASSERT(m->m_len == clsize,
		    ("mbuf length not expected cluster size: %d",
		    m->m_len));

		m->m_len = MIN(m->m_len, len);
		len -= m->m_len;

		m_prev = m;
		m = m->m_next;
		nreplace++;
	}

	KASSERT(m_prev != NULL, ("m_prev == NULL"));
	KASSERT(nreplace <= sc->vtnet_rx_mbuf_count,
		("too many replacement mbufs: %d/%d", nreplace,
		sc->vtnet_rx_mbuf_count));

	m_new = vtnet_alloc_rxbuf(sc, nreplace, &m_tail);
	if (m_new == NULL) {
		m_prev->m_len = clsize;
		return (ENOBUFS);
	}

	/*
	 * Move unused mbufs, if any, from the original chain
	 * onto the end of the new chain.
	 */
	if (m_prev->m_next != NULL) {
		m_tail->m_next = m_prev->m_next;
		m_prev->m_next = NULL;
	}

	error = vtnet_enqueue_rxbuf(sc, m_new);
	if (error) {
		/*
		 * BAD! We could not enqueue the replacement mbuf chain. We
		 * must restore the m0 chain to the original state if it was
		 * modified so we can subsequently discard it.
		 *
		 * NOTE: The replacement is suppose to be an identical copy
		 * to the one just dequeued so this is an unexpected error.
		 */
		sc->vtnet_stats.rx_enq_replacement_failed++;

		if (m_tail->m_next != NULL) {
			m_prev->m_next = m_tail->m_next;
			m_tail->m_next = NULL;
		}

		m_prev->m_len = clsize;
		m_freem(m_new);
	}

	return (error);
}

static int
vtnet_newbuf(struct vtnet_softc *sc)
{
	struct mbuf *m;
	int error;

	m = vtnet_alloc_rxbuf(sc, sc->vtnet_rx_mbuf_count, NULL);
	if (m == NULL)
		return (ENOBUFS);

	error = vtnet_enqueue_rxbuf(sc, m);
	if (error)
		m_freem(m);

	return (error);
}

static void
vtnet_discard_merged_rxbuf(struct vtnet_softc *sc, int nbufs)
{
	struct virtqueue *vq;
	struct mbuf *m;

	vq = sc->vtnet_rx_vq;

	while (--nbufs > 0) {
		if ((m = virtqueue_dequeue(vq, NULL)) == NULL)
			break;
		vtnet_discard_rxbuf(sc, m);
	}
}

static void
vtnet_discard_rxbuf(struct vtnet_softc *sc, struct mbuf *m)
{
	int error;

	/*
	 * Requeue the discarded mbuf. This should always be
	 * successful since it was just dequeued.
	 */
	error = vtnet_enqueue_rxbuf(sc, m);
	KASSERT(error == 0, ("cannot requeue discarded mbuf"));
}

static int
vtnet_enqueue_rxbuf(struct vtnet_softc *sc, struct mbuf *m)
{
	struct sglist sg;
	struct sglist_seg segs[VTNET_MAX_RX_SEGS];
	struct vtnet_rx_header *rxhdr;
	struct virtio_net_hdr *hdr;
	uint8_t *mdata;
	int offset, error;

	ASSERT_SERIALIZED(&sc->vtnet_slz);
	if ((sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG) == 0)
		KASSERT(m->m_next == NULL, ("chained Rx mbuf"));

	sglist_init(&sg, VTNET_MAX_RX_SEGS, segs);

	mdata = mtod(m, uint8_t *);
	offset = 0;

	if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
		rxhdr = (struct vtnet_rx_header *) mdata;
		hdr = &rxhdr->vrh_hdr;
		offset += sizeof(struct vtnet_rx_header);

		error = sglist_append(&sg, hdr, sc->vtnet_hdr_size);
		KASSERT(error == 0, ("cannot add header to sglist"));
	}

	error = sglist_append(&sg, mdata + offset, m->m_len - offset);
	if (error)
		return (error);

	if (m->m_next != NULL) {
		error = sglist_append_mbuf(&sg, m->m_next);
		if (error)
			return (error);
	}

	return (virtqueue_enqueue(sc->vtnet_rx_vq, m, &sg, 0, sg.sg_nseg));
}

static void
vtnet_vlan_tag_remove(struct mbuf *m)
{
	struct ether_vlan_header *evl;

	evl = mtod(m, struct ether_vlan_header *);

	m->m_pkthdr.ether_vlantag = ntohs(evl->evl_tag);
	m->m_flags |= M_VLANTAG;

	/* Strip the 802.1Q header. */
	bcopy((char *) evl, (char *) evl + ETHER_VLAN_ENCAP_LEN,
	    ETHER_HDR_LEN - ETHER_TYPE_LEN);
	m_adj(m, ETHER_VLAN_ENCAP_LEN);
}

/*
 * Alternative method of doing receive checksum offloading. Rather
 * than parsing the received frame down to the IP header, use the
 * csum_offset to determine which CSUM_* flags are appropriate. We
 * can get by with doing this only because the checksum offsets are
 * unique for the things we care about.
 */
static int
vtnet_rx_csum(struct vtnet_softc *sc, struct mbuf *m,
    struct virtio_net_hdr *hdr)
{
	struct ether_header *eh;
	struct ether_vlan_header *evh;
	struct udphdr *udp;
	int csum_len;
	uint16_t eth_type;

	csum_len = hdr->csum_start + hdr->csum_offset;

	if (csum_len < sizeof(struct ether_header) + sizeof(struct ip))
		return (1);
	if (m->m_len < csum_len)
		return (1);

	eh = mtod(m, struct ether_header *);
	eth_type = ntohs(eh->ether_type);
	if (eth_type == ETHERTYPE_VLAN) {
		evh = mtod(m, struct ether_vlan_header *);
		eth_type = ntohs(evh->evl_proto);
	}

	if (eth_type != ETHERTYPE_IP && eth_type != ETHERTYPE_IPV6) {
		sc->vtnet_stats.rx_csum_bad_ethtype++;
		return (1);
	}

	/* Use the offset to determine the appropriate CSUM_* flags. */
	switch (hdr->csum_offset) {
	case offsetof(struct udphdr, uh_sum):
		if (m->m_len < hdr->csum_start + sizeof(struct udphdr))
			return (1);
		udp = (struct udphdr *)(mtod(m, uint8_t *) + hdr->csum_start);
		if (udp->uh_sum == 0)
			return (0);

		/* FALLTHROUGH */

	case offsetof(struct tcphdr, th_sum):
		m->m_pkthdr.csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
		m->m_pkthdr.csum_data = 0xFFFF;
		break;

	default:
		sc->vtnet_stats.rx_csum_bad_offset++;
		return (1);
	}

	sc->vtnet_stats.rx_csum_offloaded++;

	return (0);
}

static int
vtnet_rxeof_merged(struct vtnet_softc *sc, struct mbuf *m_head, int nbufs)
{
	struct ifnet *ifp;
	struct virtqueue *vq;
	struct mbuf *m, *m_tail;
	int len;

	ifp = sc->vtnet_ifp;
	vq = sc->vtnet_rx_vq;
	m_tail = m_head;

	while (--nbufs > 0) {
		m = virtqueue_dequeue(vq, &len);
		if (m == NULL) {
			ifp->if_ierrors++;
			goto fail;
		}

		if (vtnet_newbuf(sc) != 0) {
			ifp->if_iqdrops++;
			vtnet_discard_rxbuf(sc, m);
			if (nbufs > 1)
				vtnet_discard_merged_rxbuf(sc, nbufs);
			goto fail;
		}

		if (m->m_len < len)
			len = m->m_len;

		m->m_len = len;
		m->m_flags &= ~M_PKTHDR;

		m_head->m_pkthdr.len += len;
		m_tail->m_next = m;
		m_tail = m;
	}

	return (0);

fail:
	sc->vtnet_stats.rx_mergeable_failed++;
	m_freem(m_head);

	return (1);
}

static int
vtnet_rxeof(struct vtnet_softc *sc, int count, int *rx_npktsp)
{
	struct virtio_net_hdr lhdr;
	struct ifnet *ifp;
	struct virtqueue *vq;
	struct mbuf *m;
	struct ether_header *eh;
	struct virtio_net_hdr *hdr;
	struct virtio_net_hdr_mrg_rxbuf *mhdr;
	int len, deq, nbufs, adjsz, rx_npkts;

	ifp = sc->vtnet_ifp;
	vq = sc->vtnet_rx_vq;
	hdr = &lhdr;
	deq = 0;
	rx_npkts = 0;

	ASSERT_SERIALIZED(&sc->vtnet_slz);

	while (--count >= 0) {
		m = virtqueue_dequeue(vq, &len);
		if (m == NULL)
			break;
		deq++;

		if (len < sc->vtnet_hdr_size + ETHER_HDR_LEN) {
			ifp->if_ierrors++;
			vtnet_discard_rxbuf(sc, m);
			continue;
		}

		if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
			nbufs = 1;
			adjsz = sizeof(struct vtnet_rx_header);
			/*
			 * Account for our pad between the header and
			 * the actual start of the frame.
			 */
			len += VTNET_RX_HEADER_PAD;
		} else {
			mhdr = mtod(m, struct virtio_net_hdr_mrg_rxbuf *);
			nbufs = mhdr->num_buffers;
			adjsz = sizeof(struct virtio_net_hdr_mrg_rxbuf);
		}

		if (vtnet_replace_rxbuf(sc, m, len) != 0) {
			ifp->if_iqdrops++;
			vtnet_discard_rxbuf(sc, m);
			if (nbufs > 1)
				vtnet_discard_merged_rxbuf(sc, nbufs);
			continue;
		}

		m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.csum_flags = 0;

		if (nbufs > 1) {
			if (vtnet_rxeof_merged(sc, m, nbufs) != 0)
				continue;
		}

		ifp->if_ipackets++;

		/*
		 * Save copy of header before we strip it. For both mergeable
		 * and non-mergeable, the VirtIO header is placed first in the
		 * mbuf's data. We no longer need num_buffers, so always use a
		 * virtio_net_hdr.
		 */
		memcpy(hdr, mtod(m, void *), sizeof(struct virtio_net_hdr));
		m_adj(m, adjsz);

		if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) {
			eh = mtod(m, struct ether_header *);
			if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
				vtnet_vlan_tag_remove(m);

				/*
				 * With the 802.1Q header removed, update the
				 * checksum starting location accordingly.
				 */
				if (hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)
					hdr->csum_start -=
					    ETHER_VLAN_ENCAP_LEN;
			}
		}

		if (ifp->if_capenable & IFCAP_RXCSUM &&
		    hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
			if (vtnet_rx_csum(sc, m, hdr) != 0)
				sc->vtnet_stats.rx_csum_failed++;
		}

		lwkt_serialize_exit(&sc->vtnet_slz);
		rx_npkts++;
		ifp->if_input(ifp, m, NULL, -1);
		lwkt_serialize_enter(&sc->vtnet_slz);

		/*
		 * The interface may have been stopped while we were
		 * passing the packet up the network stack.
		 */
		if ((ifp->if_flags & IFF_RUNNING) == 0)
			break;
	}

	virtqueue_notify(vq, &sc->vtnet_slz);

	if (rx_npktsp != NULL)
		*rx_npktsp = rx_npkts;

	return (count > 0 ? 0 : EAGAIN);
}

static void
vtnet_rx_intr_task(void *arg)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;
	int more;

	sc = arg;
	ifp = sc->vtnet_ifp;

next:
//	lwkt_serialize_enter(&sc->vtnet_slz);

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		vtnet_enable_rx_intr(sc);
//		lwkt_serialize_exit(&sc->vtnet_slz);
		return;
	}

	more = vtnet_rxeof(sc, sc->vtnet_rx_process_limit, NULL);
	if (!more && vtnet_enable_rx_intr(sc) != 0) {
		vtnet_disable_rx_intr(sc);
		more = 1;
	}

//	lwkt_serialize_exit(&sc->vtnet_slz);

	if (more) {
		sc->vtnet_stats.rx_task_rescheduled++;
		goto next;
	}
}

static int
vtnet_rx_vq_intr(void *xsc)
{
	struct vtnet_softc *sc;

	sc = xsc;

	vtnet_disable_rx_intr(sc);
	vtnet_rx_intr_task(sc);

	return (1);
}

static void
vtnet_txeof(struct vtnet_softc *sc)
{
	struct virtqueue *vq;
	struct ifnet *ifp;
	struct vtnet_tx_header *txhdr;
	int deq;

	vq = sc->vtnet_tx_vq;
	ifp = sc->vtnet_ifp;
	deq = 0;

	ASSERT_SERIALIZED(&sc->vtnet_slz);

	while ((txhdr = virtqueue_dequeue(vq, NULL)) != NULL) {
		deq++;
		ifp->if_opackets++;
		m_freem(txhdr->vth_mbuf);
	}

	if (deq > 0) {
		ifq_clr_oactive(&ifp->if_snd);
		if (virtqueue_empty(vq))
			sc->vtnet_watchdog_timer = 0;
	}
}

static struct mbuf *
vtnet_tx_offload(struct vtnet_softc *sc, struct mbuf *m,
    struct virtio_net_hdr *hdr)
{
	struct ifnet *ifp;
	struct ether_header *eh;
	struct ether_vlan_header *evh;
	struct ip *ip;
	struct ip6_hdr *ip6;
	struct tcphdr *tcp;
	int ip_offset;
	uint16_t eth_type, csum_start;
	uint8_t ip_proto, gso_type;

	ifp = sc->vtnet_ifp;
	M_ASSERTPKTHDR(m);

	ip_offset = sizeof(struct ether_header);
	if (m->m_len < ip_offset) {
		if ((m = m_pullup(m, ip_offset)) == NULL)
			return (NULL);
	}

	eh = mtod(m, struct ether_header *);
	eth_type = ntohs(eh->ether_type);
	if (eth_type == ETHERTYPE_VLAN) {
		ip_offset = sizeof(struct ether_vlan_header);
		if (m->m_len < ip_offset) {
			if ((m = m_pullup(m, ip_offset)) == NULL)
				return (NULL);
		}
		evh = mtod(m, struct ether_vlan_header *);
		eth_type = ntohs(evh->evl_proto);
	}

	switch (eth_type) {
	case ETHERTYPE_IP:
		if (m->m_len < ip_offset + sizeof(struct ip)) {
			m = m_pullup(m, ip_offset + sizeof(struct ip));
			if (m == NULL)
				return (NULL);
		}

		ip = (struct ip *)(mtod(m, uint8_t *) + ip_offset);
		ip_proto = ip->ip_p;
		csum_start = ip_offset + (ip->ip_hl << 2);
		gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
		break;

	case ETHERTYPE_IPV6:
		if (m->m_len < ip_offset + sizeof(struct ip6_hdr)) {
			m = m_pullup(m, ip_offset + sizeof(struct ip6_hdr));
			if (m == NULL)
				return (NULL);
		}

		ip6 = (struct ip6_hdr *)(mtod(m, uint8_t *) + ip_offset);
		/*
		 * XXX Assume no extension headers are present. Presently,
		 * this will always be true in the case of TSO, and FreeBSD
		 * does not perform checksum offloading of IPv6 yet.
		 */
		ip_proto = ip6->ip6_nxt;
		csum_start = ip_offset + sizeof(struct ip6_hdr);
		gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
		break;

	default:
		return (m);
	}

	if (m->m_pkthdr.csum_flags & VTNET_CSUM_OFFLOAD) {
		hdr->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->csum_start = csum_start;
		hdr->csum_offset = m->m_pkthdr.csum_data;

		sc->vtnet_stats.tx_csum_offloaded++;
	}

	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		if (ip_proto != IPPROTO_TCP)
			return (m);

		if (m->m_len < csum_start + sizeof(struct tcphdr)) {
			m = m_pullup(m, csum_start + sizeof(struct tcphdr));
			if (m == NULL)
				return (NULL);
		}

		tcp = (struct tcphdr *)(mtod(m, uint8_t *) + csum_start);
		hdr->gso_type = gso_type;
		hdr->hdr_len = csum_start + (tcp->th_off << 2);
		hdr->gso_size = m->m_pkthdr.tso_segsz;

		if (tcp->th_flags & TH_CWR) {
			/*
			 * Drop if we did not negotiate VIRTIO_NET_F_HOST_ECN.
			 * ECN support is only configurable globally with the
			 * net.inet.tcp.ecn.enable sysctl knob.
			 */
			if ((sc->vtnet_flags & VTNET_FLAG_TSO_ECN) == 0) {
				if_printf(ifp, "TSO with ECN not supported "
				    "by host\n");
				m_freem(m);
				return (NULL);
			}

			hdr->gso_type |= VIRTIO_NET_HDR_GSO_ECN;
		}

		sc->vtnet_stats.tx_tso_offloaded++;
	}

	return (m);
}

static int
vtnet_enqueue_txbuf(struct vtnet_softc *sc, struct mbuf **m_head,
    struct vtnet_tx_header *txhdr)
{
	struct sglist sg;
	struct sglist_seg segs[VTNET_MAX_TX_SEGS];
	struct virtqueue *vq;
	struct mbuf *m;
	int collapsed, error;

	vq = sc->vtnet_tx_vq;
	m = *m_head;
	collapsed = 0;

	sglist_init(&sg, VTNET_MAX_TX_SEGS, segs);
	error = sglist_append(&sg, &txhdr->vth_uhdr, sc->vtnet_hdr_size);
	KASSERT(error == 0 && sg.sg_nseg == 1,
	    ("cannot add header to sglist"));

again:
	error = sglist_append_mbuf(&sg, m);
	if (error) {
		if (collapsed)
			goto fail;

		//m = m_collapse(m, M_NOWAIT, VTNET_MAX_TX_SEGS - 1);
		m = m_defrag(m, M_NOWAIT);
		if (m == NULL)
			goto fail;

		*m_head = m;
		collapsed = 1;
		goto again;
	}

	txhdr->vth_mbuf = m;

	return (virtqueue_enqueue(vq, txhdr, &sg, sg.sg_nseg, 0));

fail:
	m_freem(*m_head);
	*m_head = NULL;

	return (ENOBUFS);
}

static struct mbuf *
vtnet_vlan_tag_insert(struct mbuf *m)
{
	struct mbuf *n;
	struct ether_vlan_header *evl;

	if (M_WRITABLE(m) == 0) {
		n = m_dup(m, M_NOWAIT);
		m_freem(m);
		if ((m = n) == NULL)
			return (NULL);
	}

	M_PREPEND(m, ETHER_VLAN_ENCAP_LEN, M_NOWAIT);
	if (m == NULL)
		return (NULL);
	if (m->m_len < sizeof(struct ether_vlan_header)) {
		m = m_pullup(m, sizeof(struct ether_vlan_header));
		if (m == NULL)
			return (NULL);
	}

	/* Insert 802.1Q header into the existing Ethernet header. */
	evl = mtod(m, struct ether_vlan_header *);
	bcopy((char *) evl + ETHER_VLAN_ENCAP_LEN,
	      (char *) evl, ETHER_HDR_LEN - ETHER_TYPE_LEN);
	evl->evl_encap_proto = htons(ETHERTYPE_VLAN);
	evl->evl_tag = htons(m->m_pkthdr.ether_vlantag);
	m->m_flags &= ~M_VLANTAG;

	return (m);
}

static int
vtnet_encap(struct vtnet_softc *sc, struct mbuf **m_head)
{
	struct vtnet_tx_header *txhdr;
	struct virtio_net_hdr *hdr;
	struct mbuf *m;
	int error;

	txhdr = &sc->vtnet_txhdrarea[sc->vtnet_txhdridx];
	memset(txhdr, 0, sizeof(struct vtnet_tx_header));

	/*
	 * Always use the non-mergeable header to simplify things. When
	 * the mergeable feature is negotiated, the num_buffers field
	 * must be set to zero. We use vtnet_hdr_size later to enqueue
	 * the correct header size to the host.
	 */
	hdr = &txhdr->vth_uhdr.hdr;
	m = *m_head;

	error = ENOBUFS;

	if (m->m_flags & M_VLANTAG) {
		//m = ether_vlanencap(m, m->m_pkthdr.ether_vtag);
		m = vtnet_vlan_tag_insert(m);
		if ((*m_head = m) == NULL)
			goto fail;
		m->m_flags &= ~M_VLANTAG;
	}

	if (m->m_pkthdr.csum_flags != 0) {
		m = vtnet_tx_offload(sc, m, hdr);
		if ((*m_head = m) == NULL)
			goto fail;
	}

	error = vtnet_enqueue_txbuf(sc, m_head, txhdr);
	if (error == 0)
		sc->vtnet_txhdridx =
		    (sc->vtnet_txhdridx + 1) % ((sc->vtnet_tx_size / 2) + 1);
fail:
	return (error);
}

static void
vtnet_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct vtnet_softc *sc;

	sc = ifp->if_softc;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	lwkt_serialize_enter(&sc->vtnet_slz);
	vtnet_start_locked(ifp, ifsq);
	lwkt_serialize_exit(&sc->vtnet_slz);
}

static void
vtnet_start_locked(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct vtnet_softc *sc;
	struct virtqueue *vq;
	struct mbuf *m0;
	int enq;

	sc = ifp->if_softc;
	vq = sc->vtnet_tx_vq;
	enq = 0;

	ASSERT_SERIALIZED(&sc->vtnet_slz);

	if ((ifp->if_flags & (IFF_RUNNING)) !=
	    IFF_RUNNING || ((sc->vtnet_flags & VTNET_FLAG_LINK) == 0))
		return;

#ifdef VTNET_TX_INTR_MODERATION
	if (virtqueue_nused(vq) >= sc->vtnet_tx_size / 2)
		vtnet_txeof(sc);
#endif

	while (!ifsq_is_empty(ifsq)) {
		if (virtqueue_full(vq)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m0 = ifq_dequeue(&ifp->if_snd);
		if (m0 == NULL)
			break;

		if (vtnet_encap(sc, &m0) != 0) {
			if (m0 == NULL)
				break;
			ifq_prepend(&ifp->if_snd, m0);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		enq++;
		ETHER_BPF_MTAP(ifp, m0);
	}

	if (enq > 0) {
		virtqueue_notify(vq, &sc->vtnet_slz);
		sc->vtnet_watchdog_timer = VTNET_WATCHDOG_TIMEOUT;
	}
}

static void
vtnet_tick(void *xsc)
{
	struct vtnet_softc *sc;

	sc = xsc;

#if 0
	ASSERT_SERIALIZED(&sc->vtnet_slz);
#ifdef VTNET_DEBUG
	virtqueue_dump(sc->vtnet_rx_vq);
	virtqueue_dump(sc->vtnet_tx_vq);
#endif

	vtnet_watchdog(sc);
	callout_reset(&sc->vtnet_tick_ch, hz, vtnet_tick, sc);
#endif
}

static void
vtnet_tx_intr_task(void *arg)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;
	struct ifaltq_subque *ifsq;

	sc = arg;
	ifp = sc->vtnet_ifp;
	ifsq = ifq_get_subq_default(&ifp->if_snd);

next:
//	lwkt_serialize_enter(&sc->vtnet_slz);

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		vtnet_enable_tx_intr(sc);
//		lwkt_serialize_exit(&sc->vtnet_slz);
		return;
	}

	vtnet_txeof(sc);

	if (!ifsq_is_empty(ifsq))
		vtnet_start_locked(ifp, ifsq);

	if (vtnet_enable_tx_intr(sc) != 0) {
		vtnet_disable_tx_intr(sc);
		sc->vtnet_stats.tx_task_rescheduled++;
//		lwkt_serialize_exit(&sc->vtnet_slz);
		goto next;
	}

//	lwkt_serialize_exit(&sc->vtnet_slz);
}

static int
vtnet_tx_vq_intr(void *xsc)
{
	struct vtnet_softc *sc;

	sc = xsc;

	vtnet_disable_tx_intr(sc);
	vtnet_tx_intr_task(sc);

	return (1);
}

static void
vtnet_stop(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	ASSERT_SERIALIZED(&sc->vtnet_slz);

	sc->vtnet_watchdog_timer = 0;
	callout_stop(&sc->vtnet_tick_ch);
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags &= ~(IFF_RUNNING);

	vtnet_disable_rx_intr(sc);
	vtnet_disable_tx_intr(sc);

	/*
	 * Stop the host VirtIO adapter. Note this will reset the host
	 * adapter's state back to the pre-initialized state, so in
	 * order to make the device usable again, we must drive it
	 * through virtio_reinit() and virtio_reinit_complete().
	 */
	virtio_stop(dev);

	sc->vtnet_flags &= ~VTNET_FLAG_LINK;

	vtnet_free_rx_mbufs(sc);
	vtnet_free_tx_mbufs(sc);
}

static int
vtnet_reinit(struct vtnet_softc *sc)
{
	struct ifnet *ifp;
	uint64_t features;

	ifp = sc->vtnet_ifp;
	features = sc->vtnet_features;

	/*
	 * Re-negotiate with the host, removing any disabled receive
	 * features. Transmit features are disabled only on our side
	 * via if_capenable and if_hwassist.
	 */

	if (ifp->if_capabilities & IFCAP_RXCSUM) {
		if ((ifp->if_capenable & IFCAP_RXCSUM) == 0)
			features &= ~VIRTIO_NET_F_GUEST_CSUM;
	}

	if (ifp->if_capabilities & IFCAP_LRO) {
		if ((ifp->if_capenable & IFCAP_LRO) == 0)
			features &= ~VTNET_LRO_FEATURES;
	}

	if (ifp->if_capabilities & IFCAP_VLAN_HWFILTER) {
		if ((ifp->if_capenable & IFCAP_VLAN_HWFILTER) == 0)
			features &= ~VIRTIO_NET_F_CTRL_VLAN;
	}

	return (virtio_reinit(sc->vtnet_dev, features));
}

static void
vtnet_init_locked(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;
	int error;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	ASSERT_SERIALIZED(&sc->vtnet_slz);

	if (ifp->if_flags & IFF_RUNNING)
		return;

	/* Stop host's adapter, cancel any pending I/O. */
	vtnet_stop(sc);

	/* Reinitialize the host device. */
	error = vtnet_reinit(sc);
	if (error) {
		device_printf(dev,
		    "reinitialization failed, stopping device...\n");
		vtnet_stop(sc);
		return;
	}

	/* Update host with assigned MAC address. */
	bcopy(IF_LLADDR(ifp), sc->vtnet_hwaddr, ETHER_ADDR_LEN);
	vtnet_set_hwaddr(sc);

	ifp->if_hwassist = 0;
	if (ifp->if_capenable & IFCAP_TXCSUM)
		ifp->if_hwassist |= VTNET_CSUM_OFFLOAD;
	if (ifp->if_capenable & IFCAP_TSO4)
		ifp->if_hwassist |= CSUM_TSO;

	error = vtnet_init_rx_vq(sc);
	if (error) {
		device_printf(dev,
		    "cannot allocate mbufs for Rx virtqueue\n");
		vtnet_stop(sc);
		return;
	}

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_VQ) {
		if (sc->vtnet_flags & VTNET_FLAG_CTRL_RX) {
			/* Restore promiscuous and all-multicast modes. */
			vtnet_rx_filter(sc);

			/* Restore filtered MAC addresses. */
			vtnet_rx_filter_mac(sc);
		}

		/* Restore VLAN filters. */
		if (ifp->if_capenable & IFCAP_VLAN_HWFILTER)
			vtnet_rx_filter_vlan(sc);
	}

	{
		vtnet_enable_rx_intr(sc);
		vtnet_enable_tx_intr(sc);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	virtio_reinit_complete(dev);

	vtnet_update_link_status(sc);
	callout_reset(&sc->vtnet_tick_ch, hz, vtnet_tick, sc);
}

static void
vtnet_init(void *xsc)
{
	struct vtnet_softc *sc;

	sc = xsc;

	lwkt_serialize_enter(&sc->vtnet_slz);
	vtnet_init_locked(sc);
	lwkt_serialize_exit(&sc->vtnet_slz);
}

static void
vtnet_exec_ctrl_cmd(struct vtnet_softc *sc, void *cookie,
    struct sglist *sg, int readable, int writable)
{
	struct virtqueue *vq;
	void *c;

	vq = sc->vtnet_ctrl_vq;

	ASSERT_SERIALIZED(&sc->vtnet_slz);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_CTRL_VQ,
	    ("no control virtqueue"));
	KASSERT(virtqueue_empty(vq),
	    ("control command already enqueued"));

	if (virtqueue_enqueue(vq, cookie, sg, readable, writable) != 0)
		return;

	virtqueue_notify(vq, &sc->vtnet_slz);

	/*
	 * Poll until the command is complete. Previously, we would
	 * sleep until the control virtqueue interrupt handler woke
	 * us up, but dropping the VTNET_MTX leads to serialization
	 * difficulties.
	 *
	 * Furthermore, it appears QEMU/KVM only allocates three MSIX
	 * vectors. Two of those vectors are needed for the Rx and Tx
	 * virtqueues. We do not support sharing both a Vq and config
	 * changed notification on the same MSIX vector.
	 */
	c = virtqueue_poll(vq, NULL);
	KASSERT(c == cookie, ("unexpected control command response"));
}

static void
vtnet_rx_filter(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	ASSERT_SERIALIZED(&sc->vtnet_slz);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_CTRL_RX,
	    ("CTRL_RX feature not negotiated"));

	if (vtnet_set_promisc(sc, ifp->if_flags & IFF_PROMISC) != 0)
		device_printf(dev, "cannot %s promiscuous mode\n",
		    ifp->if_flags & IFF_PROMISC ? "enable" : "disable");

	if (vtnet_set_allmulti(sc, ifp->if_flags & IFF_ALLMULTI) != 0)
		device_printf(dev, "cannot %s all-multicast mode\n",
		    ifp->if_flags & IFF_ALLMULTI ? "enable" : "disable");
}

static int
vtnet_ctrl_rx_cmd(struct vtnet_softc *sc, int cmd, int on)
{
	struct virtio_net_ctrl_hdr hdr __aligned(2);
	struct sglist_seg segs[3];
	struct sglist sg;
	uint8_t onoff, ack;
	int error;

	if ((sc->vtnet_flags & VTNET_FLAG_CTRL_RX) == 0)
		return (ENOTSUP);

	error = 0;

	hdr.class = VIRTIO_NET_CTRL_RX;
	hdr.cmd = cmd;
	onoff = !!on;
	ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 3, segs);
	error |= sglist_append(&sg, &hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &onoff, sizeof(uint8_t));
	error |= sglist_append(&sg, &ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 3,
	    ("error adding Rx filter message to sglist"));

	vtnet_exec_ctrl_cmd(sc, &ack, &sg, sg.sg_nseg - 1, 1);

	return (ack == VIRTIO_NET_OK ? 0 : EIO);
}

static int
vtnet_set_promisc(struct vtnet_softc *sc, int on)
{

	return (vtnet_ctrl_rx_cmd(sc, VIRTIO_NET_CTRL_RX_PROMISC, on));
}

static int
vtnet_set_allmulti(struct vtnet_softc *sc, int on)
{

	return (vtnet_ctrl_rx_cmd(sc, VIRTIO_NET_CTRL_RX_ALLMULTI, on));
}

static void
vtnet_rx_filter_mac(struct vtnet_softc *sc)
{
	struct virtio_net_ctrl_hdr hdr __aligned(2);
	struct vtnet_mac_filter *filter;
	struct sglist_seg segs[4];
	struct sglist sg;
	struct ifnet *ifp;
	struct ifaddr *ifa;
        struct ifaddr_container *ifac;
	struct ifmultiaddr *ifma;
	int ucnt, mcnt, promisc, allmulti, error;
	uint8_t ack;

	ifp = sc->vtnet_ifp;
	ucnt = 0;
	mcnt = 0;
	promisc = 0;
	allmulti = 0;
	error = 0;

	ASSERT_SERIALIZED(&sc->vtnet_slz);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_CTRL_RX,
	    ("CTRL_RX feature not negotiated"));

	/* Use the MAC filtering table allocated in vtnet_attach. */
	filter = sc->vtnet_macfilter;
	memset(filter, 0, sizeof(struct vtnet_mac_filter));

	/* Unicast MAC addresses: */
	//if_addr_rlock(ifp);
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		ifa = ifac->ifa;
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		else if (ucnt == VTNET_MAX_MAC_ENTRIES)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifa->ifa_addr),
		    &filter->vmf_unicast.macs[ucnt], ETHER_ADDR_LEN);
		ucnt++;
	}
	//if_addr_runlock(ifp);

	if (ucnt >= VTNET_MAX_MAC_ENTRIES) {
		promisc = 1;
		filter->vmf_unicast.nentries = 0;

		if_printf(ifp, "more than %d MAC addresses assigned, "
		    "falling back to promiscuous mode\n",
		    VTNET_MAX_MAC_ENTRIES);
	} else
		filter->vmf_unicast.nentries = ucnt;

	/* Multicast MAC addresses: */
	//if_maddr_rlock(ifp);
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		else if (mcnt == VTNET_MAX_MAC_ENTRIES)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &filter->vmf_multicast.macs[mcnt], ETHER_ADDR_LEN);
		mcnt++;
	}
	//if_maddr_runlock(ifp);

	if (mcnt >= VTNET_MAX_MAC_ENTRIES) {
		allmulti = 1;
		filter->vmf_multicast.nentries = 0;

		if_printf(ifp, "more than %d multicast MAC addresses "
		    "assigned, falling back to all-multicast mode\n",
		    VTNET_MAX_MAC_ENTRIES);
	} else
		filter->vmf_multicast.nentries = mcnt;

	if (promisc && allmulti)
		goto out;

	hdr.class = VIRTIO_NET_CTRL_MAC;
	hdr.cmd = VIRTIO_NET_CTRL_MAC_TABLE_SET;
	ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 4, segs);
	error |= sglist_append(&sg, &hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &filter->vmf_unicast,
	    sizeof(uint32_t) + filter->vmf_unicast.nentries * ETHER_ADDR_LEN);
	error |= sglist_append(&sg, &filter->vmf_multicast,
	    sizeof(uint32_t) + filter->vmf_multicast.nentries * ETHER_ADDR_LEN);
	error |= sglist_append(&sg, &ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 4,
	    ("error adding MAC filtering message to sglist"));

	vtnet_exec_ctrl_cmd(sc, &ack, &sg, sg.sg_nseg - 1, 1);

	if (ack != VIRTIO_NET_OK)
		if_printf(ifp, "error setting host MAC filter table\n");

out:
	if (promisc)
		if (vtnet_set_promisc(sc, 1) != 0)
			if_printf(ifp, "cannot enable promiscuous mode\n");
	if (allmulti)
		if (vtnet_set_allmulti(sc, 1) != 0)
			if_printf(ifp, "cannot enable all-multicast mode\n");
}

static int
vtnet_exec_vlan_filter(struct vtnet_softc *sc, int add, uint16_t tag)
{
	struct virtio_net_ctrl_hdr hdr __aligned(2);
	struct sglist_seg segs[3];
	struct sglist sg;
	uint8_t ack;
	int error;

	hdr.class = VIRTIO_NET_CTRL_VLAN;
	hdr.cmd = add ? VIRTIO_NET_CTRL_VLAN_ADD : VIRTIO_NET_CTRL_VLAN_DEL;
	ack = VIRTIO_NET_ERR;
	error = 0;

	sglist_init(&sg, 3, segs);
	error |= sglist_append(&sg, &hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &tag, sizeof(uint16_t));
	error |= sglist_append(&sg, &ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 3,
	    ("error adding VLAN control message to sglist"));

	vtnet_exec_ctrl_cmd(sc, &ack, &sg, sg.sg_nseg - 1, 1);

	return (ack == VIRTIO_NET_OK ? 0 : EIO);
}

static void
vtnet_rx_filter_vlan(struct vtnet_softc *sc)
{
	device_t dev;
	uint32_t w, mask;
	uint16_t tag;
	int i, nvlans, error;

	ASSERT_SERIALIZED(&sc->vtnet_slz);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_VLAN_FILTER,
	    ("VLAN_FILTER feature not negotiated"));

	dev = sc->vtnet_dev;
	nvlans = sc->vtnet_nvlans;
	error = 0;

	/* Enable filtering for each configured VLAN. */
	for (i = 0; i < VTNET_VLAN_SHADOW_SIZE && nvlans > 0; i++) {
		w = sc->vtnet_vlan_shadow[i];
		for (mask = 1, tag = i * 32; w != 0; mask <<= 1, tag++) {
			if ((w & mask) != 0) {
				w &= ~mask;
				nvlans--;
				if (vtnet_exec_vlan_filter(sc, 1, tag) != 0)
					error++;
			}
		}
	}

	KASSERT(nvlans == 0, ("VLAN count incorrect"));
	if (error)
		device_printf(dev, "cannot restore VLAN filter table\n");
}

static void
vtnet_set_vlan_filter(struct vtnet_softc *sc, int add, uint16_t tag)
{
	struct ifnet *ifp;
	int idx, bit;

	KASSERT(sc->vtnet_flags & VTNET_FLAG_VLAN_FILTER,
	    ("VLAN_FILTER feature not negotiated"));

	if ((tag == 0) || (tag > 4095))
		return;

	ifp = sc->vtnet_ifp;
	idx = (tag >> 5) & 0x7F;
	bit = tag & 0x1F;

	lwkt_serialize_enter(&sc->vtnet_slz);

	/* Update shadow VLAN table. */
	if (add) {
		sc->vtnet_nvlans++;
		sc->vtnet_vlan_shadow[idx] |= (1 << bit);
	} else {
		sc->vtnet_nvlans--;
		sc->vtnet_vlan_shadow[idx] &= ~(1 << bit);
	}

	if (ifp->if_capenable & IFCAP_VLAN_HWFILTER) {
		if (vtnet_exec_vlan_filter(sc, add, tag) != 0) {
			device_printf(sc->vtnet_dev,
			    "cannot %s VLAN %d %s the host filter table\n",
			    add ? "add" : "remove", tag,
			    add ? "to" : "from");
		}
	}

	lwkt_serialize_exit(&sc->vtnet_slz);
}

static void
vtnet_register_vlan(void *arg, struct ifnet *ifp, uint16_t tag)
{

	if (ifp->if_softc != arg)
		return;

	vtnet_set_vlan_filter(arg, 1, tag);
}

static void
vtnet_unregister_vlan(void *arg, struct ifnet *ifp, uint16_t tag)
{

	if (ifp->if_softc != arg)
		return;

	vtnet_set_vlan_filter(arg, 0, tag);
}

static int
vtnet_ifmedia_upd(struct ifnet *ifp)
{
	struct vtnet_softc *sc;
	struct ifmedia *ifm;

	sc = ifp->if_softc;
	ifm = &sc->vtnet_media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	return (0);
}

static void
vtnet_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vtnet_softc *sc;

	sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	lwkt_serialize_enter(&sc->vtnet_slz);
	if (vtnet_is_link_up(sc) != 0) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |= VTNET_MEDIATYPE;
	} else
		ifmr->ifm_active |= IFM_NONE;
	lwkt_serialize_exit(&sc->vtnet_slz);
}

static void
vtnet_add_statistics(struct vtnet_softc *sc)
{
	device_t dev;
	struct vtnet_statistics *stats;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtnet_dev;
	stats = &sc->vtnet_stats;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "mbuf_alloc_failed",
	    CTLFLAG_RD, &stats->mbuf_alloc_failed,
	    "Mbuf cluster allocation failures");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_frame_too_large",
	    CTLFLAG_RD, &stats->rx_frame_too_large,
	    "Received frame larger than the mbuf chain");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_enq_replacement_failed",
	    CTLFLAG_RD, &stats->rx_enq_replacement_failed,
	    "Enqueuing the replacement receive mbuf failed");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_mergeable_failed",
	    CTLFLAG_RD, &stats->rx_mergeable_failed,
	    "Mergeable buffers receive failures");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_csum_bad_ethtype",
	    CTLFLAG_RD, &stats->rx_csum_bad_ethtype,
	    "Received checksum offloaded buffer with unsupported "
	    "Ethernet type");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_csum_bad_start",
	    CTLFLAG_RD, &stats->rx_csum_bad_start,
	    "Received checksum offloaded buffer with incorrect start offset");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_csum_bad_ipproto",
	    CTLFLAG_RD, &stats->rx_csum_bad_ipproto,
	    "Received checksum offloaded buffer with incorrect IP protocol");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_csum_bad_offset",
	    CTLFLAG_RD, &stats->rx_csum_bad_offset,
	    "Received checksum offloaded buffer with incorrect offset");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_csum_failed",
	    CTLFLAG_RD, &stats->rx_csum_failed,
	    "Received buffer checksum offload failed");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_csum_offloaded",
	    CTLFLAG_RD, &stats->rx_csum_offloaded,
	    "Received buffer checksum offload succeeded");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "rx_task_rescheduled",
	    CTLFLAG_RD, &stats->rx_task_rescheduled,
	    "Times the receive interrupt task rescheduled itself");

	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "tx_csum_offloaded",
	    CTLFLAG_RD, &stats->tx_csum_offloaded,
	    "Offloaded checksum of transmitted buffer");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "tx_tso_offloaded",
	    CTLFLAG_RD, &stats->tx_tso_offloaded,
	    "Segmentation offload of transmitted buffer");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "tx_csum_bad_ethtype",
	    CTLFLAG_RD, &stats->tx_csum_bad_ethtype,
	    "Aborted transmit of checksum offloaded buffer with unknown "
	    "Ethernet type");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "tx_tso_bad_ethtype",
	    CTLFLAG_RD, &stats->tx_tso_bad_ethtype,
	    "Aborted transmit of TSO buffer with unknown Ethernet type");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "tx_task_rescheduled",
	    CTLFLAG_RD, &stats->tx_task_rescheduled,
	    "Times the transmit interrupt task rescheduled itself");
}

static int
vtnet_enable_rx_intr(struct vtnet_softc *sc)
{

	return (virtqueue_enable_intr(sc->vtnet_rx_vq));
}

static void
vtnet_disable_rx_intr(struct vtnet_softc *sc)
{

	virtqueue_disable_intr(sc->vtnet_rx_vq);
}

static int
vtnet_enable_tx_intr(struct vtnet_softc *sc)
{

#ifdef VTNET_TX_INTR_MODERATION
	return (0);
#else
	return (virtqueue_enable_intr(sc->vtnet_tx_vq));
#endif
}

static void
vtnet_disable_tx_intr(struct vtnet_softc *sc)
{

	virtqueue_disable_intr(sc->vtnet_tx_vq);
}
