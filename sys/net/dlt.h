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

/*
 * Values starting with 104 are used for newly-assigned link-layer
 * header type values; for those link-layer header types, the DLT_
 * value returned by pcap_datalink() and passed to pcap_open_dead(),
 * and the LINKTYPE_ value that appears in capture files, are the
 * same.
 *
 * DLT_MATCHING_MIN is the lowest such value; DLT_MATCHING_MAX is
 * the highest such value.
 */
#define DLT_MATCHING_MIN	104

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

/*
 * Reserved for private use.  If you have some link-layer header type
 * that you want to use within your organization, with the capture files
 * using that link-layer header type not ever be sent outside your
 * organization, you can use these values.
 *
 * No libpcap release will use these for any purpose, nor will any
 * tcpdump release use them, either.
 *
 * Do *NOT* use these in capture files that you expect anybody not using
 * your private versions of capture-file-reading tools to read; in
 * particular, do *NOT* use them in products, otherwise you may find that
 * people won't be able to use tcpdump, or snort, or Ethereal, or... to
 * read capture files from your firewall/intrusion detection/traffic
 * monitoring/etc. appliance, or whatever product uses that DLT_ value,
 * and you may also find that the developers of those applications will
 * not accept patches to let them read those files.
 *
 * Also, do not use them if somebody might send you a capture using them
 * for *their* private type and tools using them for *your* private type
 * would have to read them.
 *
 * Instead, ask "tcpdump-workers@lists.tcpdump.org" for a new DLT_ value,
 * as per the comment above, and use the type you're given.
 */
#define DLT_USER0		147
#define DLT_USER1		148
#define DLT_USER2		149
#define DLT_USER3		150
#define DLT_USER4		151
#define DLT_USER5		152
#define DLT_USER6		153
#define DLT_USER7		154
#define DLT_USER8		155
#define DLT_USER9		156
#define DLT_USER10		157
#define DLT_USER11		158
#define DLT_USER12		159
#define DLT_USER13		160
#define DLT_USER14		161
#define DLT_USER15		162

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
/*
 * This used to be "USB packets, beginning with a USB setup header;
 * requested by Paolo Abeni <paolo.abeni@email.it>."
 *
 * However, that header didn't work all that well - it left out some
 * useful information - and was abandoned in favor of the DLT_USB_LINUX
 * header.
 *
 * This is now used by FreeBSD for its BPF taps for USB; that has its
 * own headers.  So it is written, so it is done.
 *
 * For source-code compatibility, we also define DLT_USB to have this
 * value.  We do it numerically so that, if code that includes this
 * file (directly or indirectly) also includes an OS header that also
 * defines DLT_USB as 186, we don't get a redefinition warning.
 * (NetBSD 7 does that.)
 */
#define DLT_USB_FREEBSD		186
#define DLT_USB			186

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
#define DLT_IEEE802_15_4_NONASK_PHY	215

/*
 * David Gibson <david@gibson.dropbear.id.au> requested this for
 * captures from the Linux kernel /dev/input/eventN devices. This
 * is used to communicate keystrokes and mouse movements from the
 * Linux kernel to display systems, such as Xorg.
 */
#define DLT_LINUX_EVDEV		216

/*
 * GSM Um and Abis interfaces, preceded by a "gsmtap" header.
 *
 * Requested by Harald Welte <laforge@gnumonks.org>.
 */
#define DLT_GSMTAP_UM		217
#define DLT_GSMTAP_ABIS		218

/*
 * MPLS, with an MPLS label as the link-layer header.
 * Requested by Michele Marchetto <michele@openbsd.org> on behalf
 * of OpenBSD.
 */
#define DLT_MPLS		219

/*
 * USB packets, beginning with a Linux USB header, with the USB header
 * padded to 64 bytes; required for memory-mapped access.
 */
#define DLT_USB_LINUX_MMAPPED	220

/*
 * DECT packets, with a pseudo-header; requested by
 * Matthias Wenzel <tcpdump@mazzoo.de>.
 */
#define DLT_DECT		221

/*
 * From: "Lidwa, Eric (GSFC-582.0)[SGT INC]" <eric.lidwa-1@nasa.gov>
 * Date: Mon, 11 May 2009 11:18:30 -0500
 *
 * DLT_AOS. We need it for AOS Space Data Link Protocol.
 *   I have already written dissectors for but need an OK from
 *   legal before I can submit a patch.
 *
 */
#define DLT_AOS                 222

/*
 * Wireless HART (Highway Addressable Remote Transducer)
 * From the HART Communication Foundation
 * IES/PAS 62591
 *
 * Requested by Sam Roberts <vieuxtech@gmail.com>.
 */
#define DLT_WIHART		223

/*
 * Fibre Channel FC-2 frames, beginning with a Frame_Header.
 * Requested by Kahou Lei <kahou82@gmail.com>.
 */
#define DLT_FC_2		224

/*
 * Fibre Channel FC-2 frames, beginning with an encoding of the
 * SOF, and ending with an encoding of the EOF.
 *
 * The encodings represent the frame delimiters as 4-byte sequences
 * representing the corresponding ordered sets, with K28.5
 * represented as 0xBC, and the D symbols as the corresponding
 * byte values; for example, SOFi2, which is K28.5 - D21.5 - D1.2 - D21.2,
 * is represented as 0xBC 0xB5 0x55 0x55.
 *
 * Requested by Kahou Lei <kahou82@gmail.com>.
 */
#define DLT_FC_2_WITH_FRAME_DELIMS	225

/*
 * Solaris ipnet pseudo-header; requested by Darren Reed <Darren.Reed@Sun.COM>.
 *
 * The pseudo-header starts with a one-byte version number; for version 2,
 * the pseudo-header is:
 *
 * struct dl_ipnetinfo {
 *     u_int8_t   dli_version;
 *     u_int8_t   dli_family;
 *     u_int16_t  dli_htype;
 *     u_int32_t  dli_pktlen;
 *     u_int32_t  dli_ifindex;
 *     u_int32_t  dli_grifindex;
 *     u_int32_t  dli_zsrc;
 *     u_int32_t  dli_zdst;
 * };
 *
 * dli_version is 2 for the current version of the pseudo-header.
 *
 * dli_family is a Solaris address family value, so it's 2 for IPv4
 * and 26 for IPv6.
 *
 * dli_htype is a "hook type" - 0 for incoming packets, 1 for outgoing
 * packets, and 2 for packets arriving from another zone on the same
 * machine.
 *
 * dli_pktlen is the length of the packet data following the pseudo-header
 * (so the captured length minus dli_pktlen is the length of the
 * pseudo-header, assuming the entire pseudo-header was captured).
 *
 * dli_ifindex is the interface index of the interface on which the
 * packet arrived.
 *
 * dli_grifindex is the group interface index number (for IPMP interfaces).
 *
 * dli_zsrc is the zone identifier for the source of the packet.
 *
 * dli_zdst is the zone identifier for the destination of the packet.
 *
 * A zone number of 0 is the global zone; a zone number of 0xffffffff
 * means that the packet arrived from another host on the network, not
 * from another zone on the same machine.
 *
 * An IPv4 or IPv6 datagram follows the pseudo-header; dli_family indicates
 * which of those it is.
 */
#define DLT_IPNET		226

/*
 * CAN (Controller Area Network) frames, with a pseudo-header as supplied
 * by Linux SocketCAN, and with multi-byte numerical fields in that header
 * in big-endian byte order.
 *
 * See Documentation/networking/can.txt in the Linux source.
 *
 * Requested by Felix Obenhuber <felix@obenhuber.de>.
 */
#define DLT_CAN_SOCKETCAN	227

/*
 * Raw IPv4/IPv6; different from DLT_RAW in that the DLT_ value specifies
 * whether it's v4 or v6.  Requested by Darren Reed <Darren.Reed@Sun.COM>.
 */
#define DLT_IPV4		228
#define DLT_IPV6		229

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing), and with no FCS at the end of the frame; requested by
 * Jon Smirl <jonsmirl@gmail.com>.
 */
#define DLT_IEEE802_15_4_NOFCS	230

/*
 * Raw D-Bus:
 *
 *	http://www.freedesktop.org/wiki/Software/dbus
 *
 * messages:
 *
 *	http://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-messages
 *
 * starting with the endianness flag, followed by the message type, etc.,
 * but without the authentication handshake before the message sequence:
 *
 *	http://dbus.freedesktop.org/doc/dbus-specification.html#auth-protocol
 *
 * Requested by Martin Vidner <martin@vidner.net>.
 */
#define DLT_DBUS		231

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 */
#define DLT_JUNIPER_VS			232
#define DLT_JUNIPER_SRX_E2E		233
#define DLT_JUNIPER_FIBRECHANNEL	234

/*
 * DVB-CI (DVB Common Interface for communication between a PC Card
 * module and a DVB receiver).  See
 *
 *	http://www.kaiser.cx/pcap-dvbci.html
 *
 * for the specification.
 *
 * Requested by Martin Kaiser <martin@kaiser.cx>.
 */
#define DLT_DVB_CI		235

/*
 * Variant of 3GPP TS 27.010 multiplexing protocol (similar to, but
 * *not* the same as, 27.010).  Requested by Hans-Christoph Schemmel
 * <hans-christoph.schemmel@cinterion.com>.
 */
#define DLT_MUX27010		236

/*
 * STANAG 5066 D_PDUs.  Requested by M. Baris Demiray
 * <barisdemiray@gmail.com>.
 */
#define DLT_STANAG_5066_D_PDU	237

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 */
#define DLT_JUNIPER_ATM_CEMIC	238

/*
 * NetFilter LOG messages
 * (payload of netlink NFNL_SUBSYS_ULOG/NFULNL_MSG_PACKET packets)
 *
 * Requested by Jakub Zawadzki <darkjames-ws@darkjames.pl>
 */
#define DLT_NFLOG		239

/*
 * Hilscher Gesellschaft fuer Systemautomation mbH link-layer type
 * for Ethernet packets with a 4-byte pseudo-header and always
 * with the payload including the FCS, as supplied by their
 * netANALYZER hardware and software.
 *
 * Requested by Holger P. Frommer <HPfrommer@hilscher.com>
 */
#define DLT_NETANALYZER		240

/*
 * Hilscher Gesellschaft fuer Systemautomation mbH link-layer type
 * for Ethernet packets with a 4-byte pseudo-header and FCS and
 * with the Ethernet header preceded by 7 bytes of preamble and
 * 1 byte of SFD, as supplied by their netANALYZER hardware and
 * software.
 *
 * Requested by Holger P. Frommer <HPfrommer@hilscher.com>
 */
#define DLT_NETANALYZER_TRANSPARENT	241

/*
 * IP-over-InfiniBand, as specified by RFC 4391.
 *
 * Requested by Petr Sumbera <petr.sumbera@oracle.com>.
 */
#define DLT_IPOIB		242

/*
 * MPEG-2 transport stream (ISO 13818-1/ITU-T H.222.0).
 *
 * Requested by Guy Martin <gmsoft@tuxicoman.be>.
 */
#define DLT_MPEG_2_TS		243

/*
 * ng4T GmbH's UMTS Iub/Iur-over-ATM and Iub/Iur-over-IP format as
 * used by their ng40 protocol tester.
 *
 * Requested by Jens Grimmer <jens.grimmer@ng4t.com>.
 */
#define DLT_NG40		244

/*
 * Pseudo-header giving adapter number and flags, followed by an NFC
 * (Near-Field Communications) Logical Link Control Protocol (LLCP) PDU,
 * as specified by NFC Forum Logical Link Control Protocol Technical
 * Specification LLCP 1.1.
 *
 * Requested by Mike Wakerly <mikey@google.com>.
 */
#define DLT_NFC_LLCP		245

/*
 * 246 is used as LINKTYPE_PFSYNC; do not use it for any other purpose.
 *
 * DLT_PFSYNC has different values on different platforms, and all of
 * them collide with something used elsewhere.  On platforms that
 * don't already define it, define it as 246.
 */
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__) && !defined(__DragonFly__) && !defined(__APPLE__)
#define DLT_PFSYNC		246
#endif

/*
 * Raw InfiniBand packets, starting with the Local Routing Header.
 *
 * Requested by Oren Kladnitsky <orenk@mellanox.com>.
 */
#define DLT_INFINIBAND		247

/*
 * SCTP, with no lower-level protocols (i.e., no IPv4 or IPv6).
 *
 * Requested by Michael Tuexen <Michael.Tuexen@lurchi.franken.de>.
 */
#define DLT_SCTP		248

/*
 * USB packets, beginning with a USBPcap header.
 *
 * Requested by Tomasz Mon <desowin@gmail.com>
 */
#define DLT_USBPCAP		249

/*
 * Schweitzer Engineering Laboratories "RTAC" product serial-line
 * packets.
 *
 * Requested by Chris Bontje <chris_bontje@selinc.com>.
 */
#define DLT_RTAC_SERIAL		250

/*
 * Bluetooth Low Energy air interface link-layer packets.
 *
 * Requested by Mike Kershaw <dragorn@kismetwireless.net>.
 */
#define DLT_BLUETOOTH_LE_LL	251

/*
 * DLT type for upper-protocol layer PDU saves from wireshark.
 *
 * the actual contents are determined by two TAGs stored with each
 * packet:
 *   EXP_PDU_TAG_LINKTYPE          the link type (LINKTYPE_ value) of the
 *				   original packet.
 *
 *   EXP_PDU_TAG_PROTO_NAME        the name of the wireshark dissector
 * 				   that can make sense of the data stored.
 */
#define DLT_WIRESHARK_UPPER_PDU	252

/*
 * DLT type for the netlink protocol (nlmon devices).
 */
#define DLT_NETLINK		253

/*
 * Bluetooth Linux Monitor headers for the BlueZ stack.
 */
#define DLT_BLUETOOTH_LINUX_MONITOR	254

/*
 * Bluetooth Basic Rate/Enhanced Data Rate baseband packets, as
 * captured by Ubertooth.
 */
#define DLT_BLUETOOTH_BREDR_BB	255

/*
 * Bluetooth Low Energy link layer packets, as captured by Ubertooth.
 */
#define DLT_BLUETOOTH_LE_LL_WITH_PHDR	256

/*
 * PROFIBUS data link layer.
 */
#define DLT_PROFIBUS_DL		257

/*
 * Apple's DLT_PKTAP headers.
 *
 * Sadly, the folks at Apple either had no clue that the DLT_USERn values
 * are for internal use within an organization and partners only, and
 * didn't know that the right way to get a link-layer header type is to
 * ask tcpdump.org for one, or knew and didn't care, so they just
 * used DLT_USER2, which causes problems for everything except for
 * their version of tcpdump.
 *
 * So I'll just give them one; hopefully this will show up in a
 * libpcap release in time for them to get this into 10.10 Big Sur
 * or whatever Mavericks' successor is called.  LINKTYPE_PKTAP
 * will be 258 *even on OS X*; that is *intentional*, so that
 * PKTAP files look the same on *all* OSes (different OSes can have
 * different numerical values for a given DLT_, but *MUST NOT* have
 * different values for what goes in a file, as files can be moved
 * between OSes!).
 *
 * When capturing, on a system with a Darwin-based OS, on a device
 * that returns 149 (DLT_USER2 and Apple's DLT_PKTAP) with this
 * version of libpcap, the DLT_ value for the pcap_t  will be DLT_PKTAP,
 * and that will continue to be DLT_USER2 on Darwin-based OSes. That way,
 * binary compatibility with Mavericks is preserved for programs using
 * this version of libpcap.  This does mean that if you were using
 * DLT_USER2 for some capture device on OS X, you can't do so with
 * this version of libpcap, just as you can't with Apple's libpcap -
 * on OS X, they define DLT_PKTAP to be DLT_USER2, so programs won't
 * be able to distinguish between PKTAP and whatever you were using
 * DLT_USER2 for.
 *
 * If the program saves the capture to a file using this version of
 * libpcap's pcap_dump code, the LINKTYPE_ value in the file will be
 * LINKTYPE_PKTAP, which will be 258, even on Darwin-based OSes.
 * That way, the file will *not* be a DLT_USER2 file.  That means
 * that the latest version of tcpdump, when built with this version
 * of libpcap, and sufficiently recent versions of Wireshark will
 * be able to read those files and interpret them correctly; however,
 * Apple's version of tcpdump in OS X 10.9 won't be able to handle
 * them.  (Hopefully, Apple will pick up this version of libpcap,
 * and the corresponding version of tcpdump, so that tcpdump will
 * be able to handle the old LINKTYPE_USER2 captures *and* the new
 * LINKTYPE_PKTAP captures.)
 */
#ifdef __APPLE__
#define DLT_PKTAP	DLT_USER2
#else
#define DLT_PKTAP	258
#endif

/*
 * Ethernet packets preceded by a header giving the last 6 octets
 * of the preamble specified by 802.3-2012 Clause 65, section
 * 65.1.3.2 "Transmit".
 */
#define DLT_EPON	259

/*
 * IPMI trace packets, as specified by Table 3-20 "Trace Data Block Format"
 * in the PICMG HPM.2 specification.
 */
#define DLT_IPMI_HPM_2	260

/*
 * per  Joshua Wright <jwright@hasborg.com>, formats for Zwave captures.
 */
#define DLT_ZWAVE_R1_R2  261
#define DLT_ZWAVE_R3     262

/*
 * per Steve Karg <skarg@users.sourceforge.net>, formats for Wattstopper
 * Digital Lighting Management room bus serial protocol captures.
 */
#define DLT_WATTSTOPPER_DLM     263

/*
 * ISO 14443 contactless smart card messages.
 */
#define DLT_ISO_14443	264

/*
 * Radio data system (RDS) groups.  IEC 62106.
 * Per Jonathan Brucker <jonathan.brucke@gmail.com>.
 */
#define DLT_RDS		265

/*
 * In case the code that includes this file (directly or indirectly)
 * has also included OS files that happen to define DLT_MATCHING_MAX,
 * with a different value (perhaps because that OS hasn't picked up
 * the latest version of our DLT definitions), we undefine the
 * previous value of DLT_MATCHING_MAX.
 */
#ifdef DLT_MATCHING_MAX
#undef DLT_MATCHING_MAX
#endif
#define DLT_MATCHING_MAX	265	/* highest value in the "matching" range */

#endif	/* !_NET_DLT_H_ */
