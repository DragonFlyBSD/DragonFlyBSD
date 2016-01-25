/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)bpf.h	8.1 (Berkeley) 6/10/93
 *	@(#)bpf.h	1.34 (LBL)     6/16/96
 *
 * @(#) $Header: /tcpdump/master/libpcap/pcap-bpf.h,v 1.34.2.24 2007/09/19 02:52:12 guy Exp $ (LBL)
 *
 * $FreeBSD: src/sys/net/bpf.h,v 1.21.2.4 2002/07/05 14:40:00 fenner Exp $
 * $DragonFly: src/sys/net/bpf.h,v 1.12 2008/03/14 09:52:10 matthias Exp $
 */

#ifndef _NET_DLT_H_
#define _NET_DLT_H_

/*
 * Data-link level type codes.
 */
#define DLT_NULL	0	/* no link-layer encapsulation */
#define DLT_EN10MB	1	/* Ethernet (10Mb) */
#define DLT_EN3MB	2	/* Experimental Ethernet (3Mb) */
#define DLT_AX25	3	/* Amateur Radio AX.25 */
#define DLT_PRONET	4	/* Proteon ProNET Token Ring */
#define DLT_CHAOS	5	/* Chaos */
#define DLT_IEEE802	6	/* 802.5 Token Ring */
#define DLT_ARCNET	7	/* ARCNET */
#define DLT_SLIP	8	/* Serial Line IP */
#define DLT_PPP		9	/* Point-to-point Protocol */
#define DLT_FDDI	10	/* FDDI */
#define DLT_ATM_RFC1483	11	/* LLC/SNAP encapsulated atm */
#define DLT_RAW		12	/* raw IP */

/* 13 - 14 unused */

#define DLT_SLIP_BSDOS	15	/* BSD/OS Serial Line IP */
#define DLT_PPP_BSDOS	16	/* BSD/OS Point-to-point Protocol */
#define DLT_PFSYNC	18	/* Packet filter state syncing */
#define DLT_ATM_CLIP	19	/* Linux Classical-IP over ATM */

/* 20 - 31 unused */

#define DLT_REDBACK_SMARTEDGE	32	/* Redback SmartEdge 400/800 */

/* 33 - 49 unused */

#define DLT_PPP_SERIAL	50	/* PPP over serial with HDLC encapsulation */
#define DLT_PPP_ETHER	51	/* PPP over Ethernet */

/* 52 - 98 unused */

#define DLT_SYMANTEC_FIREWALL	99	/* Symantec Enterprise Firewall */

/* 100 - 103 unused */

#define DLT_C_HDLC		104	/* Cisco HDLC */
#define DLT_CHDLC		DLT_C_HDLC
#define DLT_IEEE802_11		105	/* IEEE 802.11 wireless */
#define DLT_FRELAY		107	/* Q.922 Frame Relay */
#define DLT_LOOP		108	/* loopback */
#define DLT_ENC			109	/* Encapsulated packets for IPsec */

/* 110 - 112 unused */

#define DLT_LINUX_SLL		113	/* Linux cooked sockets */
#define DLT_LTALK		114	/* Apple LocalTalk hardware */
#define DLT_ECONET		115	/* Acorn Econet */
#define DLT_IPFILTER		116	/* OpenBSD ipfilter */
#define DLT_PFLOG		117	/* Packet filter logging */
#define DLT_CISCO_IOS		118	/* Cisco-internal use */
#define DLT_PRISM_HEADER	119	/* 802.11 plus Prism II radio header */
#define DLT_AIRONET_HEADER	120	/* 802.11 plus Aironet radio header */
#define DLT_HHDLC		121	/* Siemens HiPath HDLC */
#define DLT_IP_OVER_FC		122	/* RFC2625 IP-over-Fibre Channel */
#define DLT_SUNATM		123	/* Solaris+SunATM */
#define DLT_RIO			124	/* RapidIO */
#define DLT_PCI_EXP		125	/* PCI Express */
#define DLT_AURORA		126	/* Xilinx Aurora link layer */
#ifndef DLT_IEEE802_11_RADIO
#define DLT_IEEE802_11_RADIO	127	/* 802.11 plus radiotap radio header */
#endif
#define DLT_TZSP		128	/* Tazmen Sniffer Protocol */
#define DLT_ARCNET_LINUX	129	/* Linux ARCNET */
#define DLT_JUNIPER_MLPPP	130	/* Juniper private */
#define DLT_JUNIPER_MLFR	131	/* Juniper private */
#define DLT_JUNIPER_ES		132	/* Juniper private */
#define DLT_JUNIPER_GGSN	133	/* Juniper private */
#define DLT_JUNIPER_MFR		134	/* Juniper private */
#define DLT_JUNIPER_ATM2	135	/* Juniper private */
#define DLT_JUNIPER_SERVICES	136	/* Juniper private */
#define DLT_JUNIPER_ATM1	137	/* Juniper private */
#define DLT_APPLE_IP_OVER_IEEE1394 138	/* Apple IP-over-IEEE 1394 */
#define DLT_MTP2_WITH_PHDR	139	/* pseudo-header with various info,
					 * followed by MTP2 */
#define DLT_MTP2		140	/* MTP2, w/o pseudo-header */
#define DLT_MTP3		141	/* MTP3, w/o pseudo-header or MTP2 */
#define DLT_SCCP		142	/* SCCP, w/o pseudo-header or MTP2
					 * or MTP3 */
#define DLT_DOCSIS		143	/* DOCSIS MAC frames */
#define DLT_LINUX_IRDA		144	/* Linux IrDA */
#define DLT_IBM_SP		145	/* IBM SP switch */
#define DLT_IBM_SN		146	/* IBM Next Federation switch */

/* 147 - 162 unused */

#define DLT_IEEE802_11_RADIO_AVS 163	/* 802.11 plus AVS radio header */
#define DLT_JUNIPER_MONITOR	164	/* Juniper private */
#define DLT_BACNET_MS_TP	165	/* BACnet MS/TP */
#define DLT_PPP_PPPD		166	/* Linux PPP variant */
#define DLT_JUNIPER_PPPOE	167	/* Juniper private */
#define DLT_JUNIPER_PPPOE_ATM	168	/* Juniper private */
#define DLT_GPRS_LLC		169	/* GPRS LLC */
#define DLT_GPF_T		170	/* GPF-T (ITU-T G.7041/Y.1303) */
#define DLT_GPF_F		171	/* GPF-F (ITU-T G.7041/Y.1303) */
#define DLT_GCOM_T1E1		172	/* Gcom's T1/E1 */
#define DLT_GCOM_SERIAL		173	/* Gcom's T1/E1 */
#define DLT_JUNIPER_PIC_PEER	174	/* Juniper private */
#define DLT_ERF_ETH		175	/* Ethernet plus ERF header */
#define DLT_ERF_POS		176	/* Packet-over-SONET plus ERF header */
#define DLT_LINUX_LAPD		177	/* raw LAPD plus addition info */
#define DLT_JUNIPER_ETHER	178	/* Juniper private */
#define DLT_JUNIPER_PPP		179	/* Juniper private */
#define DLT_JUNIPER_FRELAY	180	/* Juniper private */
#define DLT_JUNIPER_CHDLC	181	/* Juniper private */
#define DLT_MFR                 182	/* Multi Link Frame Relay (FRF.16) */
#define DLT_JUNIPER_VP          183	/* Juniper private */
#define DLT_A429                184	/* Arinc 429 frames */
#define DLT_A653_ICM            185	/* Arinc 653 Interpartition
					 * Communication messages */
#define DLT_USB			186	/* USB packets plus USB setup header */
#define DLT_BLUETOOTH_HCI_H4	187	/* Bluetooth HCI UART transport layer
					 * (part H:4) */
#define DLT_IEEE802_16_MAC_CPS	188	/* IEEE 802.16 MAC Common Part
					 * Sublayer */
#define DLT_USB_LINUX		189	/* USB packets plus Linux USB header */
#define DLT_CAN20B		190	/* Controller Area Network (CAN)
					 * v. 2.0B */
#define DLT_IEEE802_15_4_LINUX	191	/* IEEE 802.15.4, with address fields
					 * padded */
#define DLT_PPI			192	/* Per Packet Information encapsulated
					 * packets */
#define DLT_IEEE802_16_MAC_CPS_RADIO 193 /* 802.16 MAC Common Part Sublayer
					  * plus a radiotap radio header */
#define DLT_JUNIPER_ISM		194	/* Juniper private */
#define DLT_IEEE802_15_4	195	/* IEEE 802.15.4 */
#define DLT_SITA		196	/* SITA plus a pseudo-header */
#define DLT_ERF			197	/* Endace ERF records plus
					 * pseudo-header */
#define DLT_RAIF1		198	/* Ethernet plus special header */
#define DLT_IPMB		199	/* IPMB packet for IPMI */
#define DLT_JUNIPER_ST		200	/* Juniper private */
#define DLT_BLUETOOTH_HCI_H4_WITH_PHDR 201 /* Bluetooth HCI UART transport
					    * layer (part H:4) plus
					    * pseudo-header */
#define DLT_AX25_KISS		202	/* AX.25 packet with a 1-byte KISS
					 * header */
#define DLT_LAPD		203	/* LAPD packets from an ISDN channel,
					 * starting with the address field,
					 * with no pseudo-header */
#define DLT_PPP_WITH_DIR	204	/* PPP */
#define DLT_C_HDLC_WITH_DIR	205	/* Cisco HDLC */
#define DLT_FRELAY_WITH_DIR	206	/* Frame Relay */
#define DLT_LAPB_WITH_DIR	207	/* LAPB */

/* 208 unused */

#define DLT_IPMB_LINUX		209	/* IPMB with a Linux-specific
					 * pseudo-header */
#define DLT_FLEXRAY		210	/* FlexRay automotive bus */
#define DLT_MOST		211	/* Media Oriented Systems Transport
					 * (MOST) bus */
#define DLT_LIN			212	/* Local Interconnect Network
					 * (LIN) bus */
#define DLT_X2E_SERIAL		213	/* X2E-private */
#define DLT_X2E_XORAYA		214	/* X2E-private */
#define DLT_IEEE802_15_4_NONASK_PHY 215	/* IEEE 802.15.4, with the PHY-level
					 * data for non-ASK PHYs */

#endif	/* !_NET_DLT_H_ */
