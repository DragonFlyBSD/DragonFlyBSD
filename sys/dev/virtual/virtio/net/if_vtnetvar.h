/*-
 * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
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

#ifndef _IF_VTNETVAR_H
#define _IF_VTNETVAR_H

struct vtnet_statistics {
	uint64_t	mbuf_alloc_failed;

	uint64_t	rx_frame_too_large;
	uint64_t	rx_enq_replacement_failed;
	uint64_t	rx_mergeable_failed;
	uint64_t	rx_csum_bad_ethtype;
	uint64_t	rx_csum_bad_ipproto;
	uint64_t	rx_csum_bad_offset;
	uint64_t	rx_csum_failed;
	uint64_t	rx_csum_offloaded;
	uint64_t	rx_task_rescheduled;

	uint64_t	tx_csum_offloaded;
	uint64_t	tx_tso_offloaded;
	uint64_t	tx_csum_bad_ethtype;
	uint64_t	tx_tso_bad_ethtype;
	uint64_t	tx_defragged;
	uint64_t	tx_defrag_failed;
	uint64_t	tx_task_rescheduled;
};

struct vtnet_softc {
	device_t		vtnet_dev;
	struct ifnet		*vtnet_ifp;
	struct lwkt_serialize	vtnet_slz;

	uint32_t		vtnet_flags;
#define VTNET_FLAG_LINK		0x0001
#define VTNET_FLAG_SUSPENDED	0x0002
#define VTNET_FLAG_MAC		0x0004
#define VTNET_FLAG_CTRL_VQ	0x0008
#define VTNET_FLAG_CTRL_RX	0x0010
#define VTNET_FLAG_CTRL_MAC	0x0020
#define VTNET_FLAG_VLAN_FILTER	0x0040
#define VTNET_FLAG_TSO_ECN	0x0080
#define VTNET_FLAG_MRG_RXBUFS	0x0100
#define VTNET_FLAG_LRO_NOMRG	0x0200

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

	struct mbuf *vth_mbuf;
};

/*
 * The VirtIO specification does not place a limit on the number of MAC
 * addresses the guest driver may request to be filtered. In practice,
 * the host is constrained by available resources. To simplify this driver,
 * impose a reasonably high limit of MAC addresses we will filter before
 * falling back to promiscuous or all-multicast modes.
 */
#define VTNET_MAX_MAC_ENTRIES	128

struct vtnet_mac_table {
	uint32_t	nentries;
	uint8_t		macs[VTNET_MAX_MAC_ENTRIES][ETHER_ADDR_LEN];
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
     VIRTIO_NET_F_CTRL_MAC_ADDR	| \
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
 * mergeable buffers, we must allocate an mbuf chain large enough to
 * hold both the vtnet_rx_header and the maximum receivable data.
 */
#define VTNET_NEEDED_RX_MBUFS(_sc)					\
	((_sc)->vtnet_flags & VTNET_FLAG_LRO_NOMRG) == 0 ? 1 :		\
	howmany(sizeof(struct vtnet_rx_header) + VTNET_MAX_RX_SIZE,	\
	(_sc)->vtnet_rx_mbuf_size)

#endif /* _IF_VTNETVAR_H */
