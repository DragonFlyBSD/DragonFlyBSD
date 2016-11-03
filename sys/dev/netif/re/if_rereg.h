/*
 * Copyright (c) 2004
 *	Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * Copyright (c) 1997, 1998-2003
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_rlreg.h,v 1.42 2004/05/24 19:39:23 jhb Exp $
 */

#define RE_TXLIST_ADDR_LO	0x0020	/* 64 bits, 256 byte alignment */
#define RE_TXLIST_ADDR_HI	0x0024	/* 64 bits, 256 byte alignment */

#define RE_RXLIST_ADDR_LO	0x00E4	/* 64 bits, 256 byte alignment */
#define RE_RXLIST_ADDR_HI	0x00E8	/* 64 bits, 256 byte alignment */

#define RE_TIMERINT		0x0058	/* 32 bits */

/*
 * Config 2 register bits
 */
#define RE_CFG2_PCICLK_MASK	0x07
#define RE_CFG2_PCICLK_33MHZ	0x00
#define RE_CFG2_PCICLK_66MHZ	0x01
#define RE_CFG2_PCI64		0x08

#define RE_TX_LIST_CNT		4
#define RE_MIN_FRAMELEN		60

#define RE_IM_MAGIC		0x5050
#define RE_IM_RXTIME(t)		((t) & 0xf)
#define RE_IM_TXTIME(t)		(((t) & 0xf) << 8)

/*
 * The 8169/8168 gigE chips support descriptor-based TX and RX.
 * In fact, they even support TCP large send.  Descriptors
 * must be allocated in contiguous blocks that are aligned on a
 * 256-byte boundary.
 */

/*
 * RX/TX descriptor definition.  When large send mode is enabled, the
 * lower 11 bits of the TX re_cmd word are used to hold the MSS, and
 * the checksum offload bits are disabled.  The structure layout is
 * the same for RX and TX descriptors.
 */

struct re_desc {
	uint32_t		re_cmdstat;
	uint32_t		re_control;
	uint32_t		re_bufaddr_lo;
	uint32_t		re_bufaddr_hi;
};

#define RE_TDESC_CMD_FRAGLEN	0x0000FFFF
#define RE_TDESC_CMD_TCPCSUM	0x00010000	/* TCP checksum enable */
#define RE_TDESC_CMD_UDPCSUM	0x00020000	/* UDP checksum enable */
#define RE_TDESC_CMD_IPCSUM	0x00040000	/* IP header checksum enable */
#define RE_TDESC_CMD_MSSVAL	0x07FF0000	/* Large send MSS value */
#define RE_TDESC_CMD_LGSEND	0x08000000	/* TCP large send enb */
#define RE_TDESC_CMD_EOF	0x10000000	/* end of frame marker */
#define RE_TDESC_CMD_SOF	0x20000000	/* start of frame marker */
#define RE_TDESC_CMD_EOR	0x40000000	/* end of ring marker */
#define RE_TDESC_CMD_OWN	0x80000000	/* chip owns descriptor */

#define RE_TDESC_CTL_INSTAG	0x00020000	/* Insert VLAN tag */
#define RE_TDESC_CTL_TAGDATA	0x0000FFFF	/* TAG data */
#define RE_TDESC_CTL_IPCSUM	0x20000000	/* IP header csum, MAC2 only */
#define RE_TDESC_CTL_TCPCSUM	0x60000000	/* TCP csum, MAC2 only */
#define RE_TDESC_CTL_UDPCSUM	0xa0000000	/* UDP csum, MAC2 only */

/*
 * Error bits are valid only on the last descriptor of a frame
 * (i.e. RE_TDESC_CMD_EOF == 1)
 */

#define RE_TDESC_STAT_COLCNT	0x000F0000	/* collision count */
#define RE_TDESC_STAT_EXCESSCOL	0x00100000	/* excessive collisions */
#define RE_TDESC_STAT_LINKFAIL	0x00200000	/* link faulure */
#define RE_TDESC_STAT_OWINCOL	0x00400000	/* out-of-window collision */
#define RE_TDESC_STAT_TXERRSUM	0x00800000	/* transmit error summary */
#define RE_TDESC_STAT_UNDERRUN	0x02000000	/* TX underrun occured */
#define RE_TDESC_STAT_OWN	0x80000000

/*
 * RX descriptor cmd/vlan definitions
 */

#define RE_RDESC_CMD_EOR	0x40000000
#define RE_RDESC_CMD_OWN	0x80000000
#define RE_RDESC_CMD_BUFLEN	0x00001FFF

#define RE_RDESC_STAT_OWN	0x80000000
#define RE_RDESC_STAT_EOR	0x40000000
#define RE_RDESC_STAT_SOF	0x20000000
#define RE_RDESC_STAT_EOF	0x10000000
#define RE_RDESC_STAT_FRALIGN	0x08000000	/* frame alignment error */
#define RE_RDESC_STAT_MCAST	0x04000000	/* multicast pkt received */
#define RE_RDESC_STAT_UCAST	0x02000000	/* unicast pkt received */
#define RE_RDESC_STAT_BCAST	0x01000000	/* broadcast pkt received */
#define RE_RDESC_STAT_BUFOFLOW	0x00800000	/* out of buffer space */
#define RE_RDESC_STAT_FIFOOFLOW	0x00400000	/* FIFO overrun */
#define RE_RDESC_STAT_GIANT	0x00200000	/* pkt > 4096 bytes */
#define RE_RDESC_STAT_RXERRSUM	0x00100000	/* RX error summary */
#define RE_RDESC_STAT_RUNT	0x00080000	/* runt packet received */
#define RE_RDESC_STAT_CRCERR	0x00040000	/* CRC error */
#define RE_RDESC_STAT_PROTOID	0x00030000	/* Protocol type */
#define RE_RDESC_STAT_IPSUMBAD	0x00008000	/* IP header checksum bad */
#define RE_RDESC_STAT_UDPSUMBAD	0x00004000	/* UDP checksum bad */
#define RE_RDESC_STAT_TCPSUMBAD	0x00002000	/* TCP checksum bad */
#define RE_RDESC_STAT_FRAGLEN	0x00001FFF	/* RX'ed frame/frag len */
#define RE_RDESC_STAT_GFRAGLEN	0x00003FFF	/* RX'ed frame/frag len */

#define RE_RDESC_CTL_HASTAG	0x00010000	/* VLAN tag available
						   (TAG data valid) */
#define RE_RDESC_CTL_TAGDATA	0x0000FFFF	/* TAG data */
#define RE_RDESC_CTL_PROTOIP4	0x40000000	/* IPv4 packet, MAC2 only */
#define RE_RDESC_CTL_PROTOIP6	0x80000000	/* IPv6 packet, MAC2 only */

#define RE_PROTOID_NONIP	0x00000000
#define RE_PROTOID_TCPIP	0x00010000
#define RE_PROTOID_UDPIP	0x00020000
#define RE_PROTOID_IP		0x00030000
#define RE_TCPPKT(x)		(((x) & RE_RDESC_STAT_PROTOID) == \
				 RE_PROTOID_TCPIP)
#define RE_UDPPKT(x)		(((x) & RE_RDESC_STAT_PROTOID) == \
				 RE_PROTOID_UDPIP)

/*
 * Statistics counter structure.
 */
struct re_stats {
	uint32_t		re_tx_pkts_lo;
	uint32_t		re_tx_pkts_hi;
	uint32_t		re_tx_errs_lo;
	uint32_t		re_tx_errs_hi;
	uint32_t		re_tx_errs;
	uint16_t		re_missed_pkts;
	uint16_t		re_rx_framealign_errs;
	uint32_t		re_tx_onecoll;
	uint32_t		re_tx_multicolls;
	uint32_t		re_rx_ucasts_hi;
	uint32_t		re_rx_ucasts_lo;
	uint32_t		re_rx_bcasts_lo;
	uint32_t		re_rx_bcasts_hi;
	uint32_t		re_rx_mcasts;
	uint16_t		re_tx_aborts;
	uint16_t		re_rx_underruns;
};

/*
 * General constants that are fun to know.
 *
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define RE_PCI_LOMEM		0x14
#define RE_PCI_LOIO		0x10

#define PCI_SUBDEVICE_LINKSYS_EG1032_REV3	0x0024
