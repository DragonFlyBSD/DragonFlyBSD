/*
 * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.
 */
/* $NetBSD: pcidevs,v 1.606 2004/01/06 19:44:17 matt Exp $ */

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

/*
 * NOTE: a fairly complete list of PCI codes can be found in:
 *
 *	share/misc/pci_vendors
 *
 * (but it doesn't always seem to match vendor documentation)
 */

/*
 * List of known PCI vendors
 */

#define	PCI_VENDOR_HAUPPAUGE	0x0070		/* Hauppauge Computer Works */
#define	PCI_VENDOR_TTTECH	0x0357		/* TTTech */
#define	PCI_VENDOR_ATI	0x1002		/* ATI Technologies */
#define	PCI_VENDOR_NS	0x100b		/* National Semiconductor */
#define	PCI_VENDOR_NCR	0x101a		/* AT&T Global Information Systems */
#define	PCI_VENDOR_AMD	0x1022		/* Advanced Micro Devices */
#define	PCI_VENDOR_DELL	0x1028		/* Dell Computer */
#define	PCI_VENDOR_SIS	0x1039		/* Silicon Integrated System */
#define	PCI_VENDOR_TI	0x104c		/* Texas Instruments */
#define	PCI_VENDOR_WINBOND	0x1050		/* Winbond Electronics */
#define	PCI_VENDOR_APPLE	0x106b		/* Apple Computer */
#define	PCI_VENDOR_QLOGIC	0x1077		/* Q Logic */
#define	PCI_VENDOR_SUN	0x108e		/* Sun Microsystems, Inc. */
#define	PCI_VENDOR_SII	0x1095		/* Silicon Image */
#define	PCI_VENDOR_BROOKTREE	0x109e		/* Brooktree */
#define	PCI_VENDOR_STB	0x10b4		/* STB Systems */
#define	PCI_VENDOR_3COM	0x10b7		/* 3Com */
#define	PCI_VENDOR_SMC	0x10b8		/* Standard Microsystems */
#define	PCI_VENDOR_SURECOM	0x10bd		/* Surecom Technology */
#define	PCI_VENDOR_NVIDIA	0x10de		/* Nvidia Corporation */
#define	PCI_VENDOR_REALTEK	0x10ec		/* Realtek Semiconductor */
#define	PCI_VENDOR_IODATA	0x10fc		/* I-O Data Device */
#define	PCI_VENDOR_VIATECH	0x1106		/* VIA Technologies */
#define	PCI_VENDOR_ACCTON	0x1113		/* Accton Technology */
#define	PCI_VENDOR_EFFICIENTNETS	0x111a		/* Efficent Networks */
#define	PCI_VENDOR_SCHNEIDERKOCH	0x1148		/* Schneider & Koch */
#define	PCI_VENDOR_DIGI	0x114f		/* Digi International */
#define	PCI_VENDOR_DLINK	0x1186		/* D-Link Systems */
#define	PCI_VENDOR_MARVELL	0x11ab		/* Marvell (was Galileo Technology) */
#define	PCI_VENDOR_LUCENT	0x11c1		/* Lucent Technologies */
#define	PCI_VENDOR_COMPEX	0x11f6		/* Compex */
#define	PCI_VENDOR_COMTROL	0x11fe		/* Comtrol */
#define	PCI_VENDOR_COREGA	0x1259		/* Corega */
#define	PCI_VENDOR_NORTEL	0x126c		/* Nortel Networks (Northern Telecom) */
#define	PCI_VENDOR_ALTEON	0x12ae		/* Alteon */
#define	PCI_VENDOR_USR2	0x16ec		/* US Robotics */
#define	PCI_VENDOR_FORTEMEDIA	0x1319		/* Forte Media */
#define	PCI_VENDOR_CNET	0x1371		/* CNet */
#define	PCI_VENDOR_LEVELONE	0x1394		/* Level One */
#define	PCI_VENDOR_HIFN	0x13a3		/* Hifn */
#define	PCI_VENDOR_SUNDANCETI	0x13f0		/* Sundance Technology */
#define	PCI_VENDOR_ASKEY	0x144f		/* Askey Computer Corp. */
#define	PCI_VENDOR_AVERMEDIA	0x1461		/* Avermedia Technologies */
#define	PCI_VENDOR_AIRONET	0x14b9		/* Aironet Wireless Communications */
#define	PCI_VENDOR_INVERTEX	0x14e1		/* Invertex */
#define	PCI_VENDOR_BROADCOM	0x14e4		/* Broadcom Corporation */
#define	PCI_VENDOR_PLANEX	0x14ea		/* Planex Communications */
#define	PCI_VENDOR_DELTA	0x1500		/* Delta Electronics */
#define	PCI_VENDOR_TERRATEC	0x153b		/* TerraTec Electronic */
#define	PCI_VENDOR_BLUESTEEL	0x15ab		/* Bluesteel Networks */
#define	PCI_VENDOR_NETSEC	0x1660		/* NetSec */
#define	PCI_VENDOR_ATHEROS	0x168c		/* Atheros Communications, Inc. */
#define	PCI_VENDOR_LINKSYS	0x1737		/* Linksys */
#define	PCI_VENDOR_ALTIMA	0x173b		/* Altima */
#define	PCI_VENDOR_PEPPERCON	0x1743		/* Peppercon AG */
#define	PCI_VENDOR_BELKIN	0x1799		/* Belkin */
#define	PCI_VENDOR_SILAN	0x1904		/* Hangzhou Silan Microelectronics */
#define	PCI_VENDOR_JMICRON	0x197b		/* JMicron Technology Corporation */
#define	PCI_VENDOR_ADDTRON	0x4033		/* Addtron Technology */
#define	PCI_VENDOR_ICOMPRESSION	0x4444		/* Conexant (iCompression) */
#define	PCI_VENDOR_NETVIN	0x4a14		/* NetVin */
#define	PCI_VENDOR_INTEL	0x8086		/* Intel */
#define	PCI_VENDOR_PROLAN	0x8c4a		/* ProLAN */
#define	PCI_VENDOR_KTI	0x8e2e		/* KTI */
#define	PCI_VENDOR_ADP	0x9004		/* Adaptec */
#define	PCI_VENDOR_INVALID	0xffff		/* INVALID VENDOR ID */

/*
 * List of known products.  Grouped by vendor.
 */

/* 3COM Products */
#define	PCI_PRODUCT_3COM_3C996	0x0003		/* 3c996 10/100/1000 Ethernet */
#define	PCI_PRODUCT_3COM_3C940	0x1700		/* 3c940 Gigabit Ethernet */
#define	PCI_PRODUCT_3COM_3C590	0x5900		/* 3c590 Ethernet */
#define	PCI_PRODUCT_3COM_3C595TX	0x5950		/* 3c595-TX 10/100 Ethernet */
#define	PCI_PRODUCT_3COM_3C595T4	0x5951		/* 3c595-T4 10/100 Ethernet */
#define	PCI_PRODUCT_3COM_3C595MII	0x5952		/* 3c595-MII 10/100 Ethernet */
#define	PCI_PRODUCT_3COM_3C940B	0x80eb		/* 3c940B Gigabit Ethernet */
#define	PCI_PRODUCT_3COM_3C900TPO	0x9000		/* 3c900-TPO Ethernet */
#define	PCI_PRODUCT_3COM_3C900COMBO	0x9001		/* 3c900-COMBO Ethernet */
#define	PCI_PRODUCT_3COM_3C905TX	0x9050		/* 3c905-TX 10/100 Ethernet */
#define	PCI_PRODUCT_3COM_3C905T4	0x9051		/* 3c905-T4 10/100 Ethernet */
#define	PCI_PRODUCT_3COM_3C910SOHOB	0x9300		/* 3c910 OfficeConnect 10/100B Ethernet */
#define	PCI_PRODUCT_3COM_3CR990TX95	0x9902		/* 3CR990-TX-95 10/100 Ethernet with 3XP */
#define	PCI_PRODUCT_3COM_3CR990TX97	0x9903		/* 3CR990-TX-97 10/100 Ethernet with 3XP */
#define	PCI_PRODUCT_3COM_3C990B	0x9904		/* 3c990B 10/100 Ethernet with 3XP */
#define	PCI_PRODUCT_3COM_3CR990SVR95	0x9908		/* 3CR990-SVR-95 10/100 Ethernet with 3XP */
#define	PCI_PRODUCT_3COM_3CR990SVR97	0x9909		/* 3CR990-SVR-97 10/100 Ethernet with 3XP */
#define	PCI_PRODUCT_3COM_3C990BSVR	0x990a		/* 3c990BSVR 10/100 Ethernet with 3XP */

/* Accton products */
#define	PCI_PRODUCT_ACCTON_MPX5030	0x1211		/* MPX 5030/5038 Ethernet */

/* Adaptec products */
#define	PCI_PRODUCT_ADP_AIC5900	0x5900		/* AIC-5900 ATM */
#define	PCI_PRODUCT_ADP_AIC5905	0x5905		/* AIC-5905 ATM */
#define	PCI_PRODUCT_ADP_AIC6915	0x6915		/* AIC-6915 10/100 Ethernet */

/* Addtron Products */
#define	PCI_PRODUCT_ADDTRON_RHINEII	0x1320		/* Rhine II 10/100 Ethernet */
#define	PCI_PRODUCT_ADDTRON_8139	0x1360		/* 8139 Ethernet */

/* ADMtek products */
#define	PCI_PRODUCT_ADMTEK_ADM8211	0x8201		/* ADMtek ADM8211 11Mbps 802.11b WLAN */

/* Aironet Wireless Communicasions products */
#define	PCI_PRODUCT_AIRONET_PC4xxx	0x0001		/* Aironet PC4500/PC4800 Wireless LAN Adapter */
#define	PCI_PRODUCT_AIRONET_350	0x0350		/* Aironet 350 Wireless LAN Adapter */
#define	PCI_PRODUCT_AIRONET_MPI350	0xa504		/* Aironet 350 miniPCI Wireless LAN Adapter */
#define	PCI_PRODUCT_AIRONET_PC4500	0x4500		/* Aironet PC4500 Wireless LAN Adapter */
#define	PCI_PRODUCT_AIRONET_PC4800	0x4800		/* Aironet PC4800 Wireless LAN Adapter */

/* Alteon products */
#define	PCI_PRODUCT_ALTEON_BCM5700	0x0003		/* ACEnic BCM5700 10/100/1000 Ethernet */
#define	PCI_PRODUCT_ALTEON_BCM5701	0x0004		/* ACEnic BCM5701 10/100/1000 Ethernet */

/* Altima products */
#define	PCI_PRODUCT_ALTIMA_AC1000	0x03e8		/* AC1000 Gigabit Ethernet */
#define	PCI_PRODUCT_ALTIMA_AC1001	0x03e9		/* AC1001 Gigabit Ethernet */
#define	PCI_PRODUCT_ALTIMA_AC9100	0x03ea		/* AC9100 Gigabit Ethernet */

/* AMD products */
#define	PCI_PRODUCT_AMD_AMD64_MISC	0x1103		/* AMD64 Miscellaneous configuration */
#define	PCI_PRODUCT_AMD_AMD64_F10_MISC	0x1203		/* Family 10h Miscellaneous */
#define	PCI_PRODUCT_AMD_AMD64_F11_MISC	0x1303		/* Family 11h Miscellaneous */
#define	PCI_PRODUCT_AMD_PCNET_PCI	0x2000		/* PCnet-PCI Ethernet */
#define	PCI_PRODUCT_AMD_PCNET_HOME	0x2001		/* PCnet-Home HomePNA Ethernet */
#define	PCI_PRODUCT_AMD_GEODE_LX_PCHB	0x2080		/* Geode LX */
#define	PCI_PRODUCT_AMD_CS5536_PCIB	0x2090		/* CS5536 ISA */

/* Apple products */
#define	PCI_PRODUCT_APPLE_BCM5701	0x1645		/* BCM5701 */

/* ATI products */
#define	PCI_PRODUCT_ATI_SB600_SATA	0x4380		/* SB600 SATA */
#define	PCI_PRODUCT_ATI_SB700_AHCI	0x4391		/* SB700 AHCI */

/* Atheros products */
#define	PCI_PRODUCT_ATHEROS_AR5210	0x0007		/* AR5210 */

/* Belkin products */
#define	PCI_PRODUCT_BELKIN_F5D6001	0x6001		/* F5D6001 802.11b */

/* Bluesteel Networks */
#define	PCI_PRODUCT_BLUESTEEL_5501	0x0000		/* 5501 */
#define	PCI_PRODUCT_BLUESTEEL_5601	0x5601		/* 5601 */

/* Broadcom Corporation products */
#define	PCI_PRODUCT_BROADCOM_BCM5752	0x1600		/* BCM5752 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5752M	0x1601		/* BCM5752M */
#define	PCI_PRODUCT_BROADCOM_BCM5725	0x1643		/* BCM5725 */
#define	PCI_PRODUCT_BROADCOM_BCM5700	0x1644		/* BCM5700 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5701	0x1645		/* BCM5701 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5702	0x1646		/* BCM5702 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5703	0x1647		/* BCM5703 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5704C	0x1648		/* BCM5704C Gigabit Ethernet (1000BASE-T) */
#define	PCI_PRODUCT_BROADCOM_BCM5704S_ALT	0x1649		/* BCM5704S Alt */
#define	PCI_PRODUCT_BROADCOM_BCM5705	0x1653		/* BCM5705 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5705K	0x1654		/* BCM5705K 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5717	0x1655		/* BCM5717 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5718	0x1656		/* BCM5718 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5719	0x1657		/* BCM5719 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5720	0x1658		/* BCM5720 */
#define	PCI_PRODUCT_BROADCOM_BCM5721	0x1659		/* BCM5721 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5722	0x165a		/* BCM5722 */
#define	PCI_PRODUCT_BROADCOM_BCM5723	0x165b		/* BCM5723 */
#define	PCI_PRODUCT_BROADCOM_BCM5705M	0x165d		/* BCM5705M 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5705M_ALT	0x165e		/* BCM5705M 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5720_ALT	0x165f		/* BCM5720 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5717C	0x1665		/* BCM5717C 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5714	0x1668		/* BCM5714 1000baseT Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5714S	0x1669		/* BCM5714S */
#define	PCI_PRODUCT_BROADCOM_BCM5780	0x166a		/* BCM5780 */
#define	PCI_PRODUCT_BROADCOM_BCM5780S	0x166b		/* BCM5780S */
#define	PCI_PRODUCT_BROADCOM_BCM5705F	0x166e		/* BCM5705F */
#define	PCI_PRODUCT_BROADCOM_BCM5754M	0x1672		/* BCM5754M */
#define	PCI_PRODUCT_BROADCOM_BCM5755M	0x1673		/* BCM5755M */
#define	PCI_PRODUCT_BROADCOM_BCM5756	0x1674		/* BCM5756 */
#define	PCI_PRODUCT_BROADCOM_BCM5750	0x1676		/* BCM5750 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5751	0x1677		/* BCM5751 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5715	0x1678		/* BCM5715 */
#define	PCI_PRODUCT_BROADCOM_BCM5715S	0x1679		/* BCM5715S */
#define	PCI_PRODUCT_BROADCOM_BCM5754	0x167a		/* BCM5754 */
#define	PCI_PRODUCT_BROADCOM_BCM5755	0x167b		/* BCM5755 */
#define	PCI_PRODUCT_BROADCOM_BCM5750M	0x167c		/* BCM5750M 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5751M	0x167d		/* BCM5751M 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5751F	0x167e		/* BCM5751F */
#define	PCI_PRODUCT_BROADCOM_BCM5787F	0x167f		/* BCM5787F */
#define	PCI_PRODUCT_BROADCOM_BCM5761E	0x1680		/* BCM5761E */
#define	PCI_PRODUCT_BROADCOM_BCM5761	0x1681		/* BCM5761 */
#define	PCI_PRODUCT_BROADCOM_BCM57762	0x1682		/* BCM57762 */
#define	PCI_PRODUCT_BROADCOM_BCM5764	0x1684		/* BCM5764 */
#define	PCI_PRODUCT_BROADCOM_BCM57766	0x1686		/* BCM57766 */
#define	PCI_PRODUCT_BROADCOM_BCM5762	0x1687		/* BCM5762 */
#define	PCI_PRODUCT_BROADCOM_BCM5761S	0x1688		/* BCM5761S */
#define	PCI_PRODUCT_BROADCOM_BCM5761SE	0x1689		/* BCM5761SE */
#define	PCI_PRODUCT_BROADCOM_BCM57760	0x1690		/* BCM57760 */
#define	PCI_PRODUCT_BROADCOM_BCM57788	0x1691		/* BCM57788 */
#define	PCI_PRODUCT_BROADCOM_BCM57780	0x1692		/* BCM57780 */
#define	PCI_PRODUCT_BROADCOM_BCM5787M	0x1693		/* BCM5787M */
#define	PCI_PRODUCT_BROADCOM_BCM57790	0x1694		/* BCM57790 */
#define	PCI_PRODUCT_BROADCOM_BCM5782	0x1696		/* BCM5782 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5784	0x1698		/* BCM5784 */
#define	PCI_PRODUCT_BROADCOM_BCM5785G	0x1699		/* BCM5785G */
#define	PCI_PRODUCT_BROADCOM_BCM5786	0x169a		/* BCM5786 */
#define	PCI_PRODUCT_BROADCOM_BCM5787	0x169b		/* BCM5787 */
#define	PCI_PRODUCT_BROADCOM_BCM5788	0x169c		/* BCM5788 10/100/1000 Enternet */
#define	PCI_PRODUCT_BROADCOM_BCM5789	0x169d		/* BCM5789 10/100/1000 Enternet */
#define	PCI_PRODUCT_BROADCOM_BCM5785F	0x16a0		/* BCM5785F */
#define	PCI_PRODUCT_BROADCOM_BCM5702X	0x16a6		/* BCM5702X 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5703X	0x16a7		/* BCM5703X 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5704S	0x16a8		/* BCM5704S Gigabit Ethernet (1000BASE-X) */
#define	PCI_PRODUCT_BROADCOM_BCM57761	0x16b0		/* BCM57761 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM57781	0x16b1		/* BCM57781 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM57791	0x16b2		/* BCM57791 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM57786	0x16b3		/* BCM57786 */
#define	PCI_PRODUCT_BROADCOM_BCM57765	0x16b4		/* BCM57765 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM57785	0x16b5		/* BCM57785 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM57795	0x16b6		/* BCM57795 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM57782	0x16b7		/* BCM57782 */
#define	PCI_PRODUCT_BROADCOM_BCM5702_ALT	0x16c6		/* BCM5702 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5703A3	0x16c7		/* BCM5703 10/100/1000 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5781	0x16dd		/* BCM5781 */
#define	PCI_PRODUCT_BROADCOM_BCM5727	0x16f3		/* BCM5727 */
#define	PCI_PRODUCT_BROADCOM_BCM5753	0x16f7		/* BCM5753 */
#define	PCI_PRODUCT_BROADCOM_BCM5753M	0x16fd		/* BCM5753M */
#define	PCI_PRODUCT_BROADCOM_BCM5753F	0x16fe		/* BCM5753F */
#define	PCI_PRODUCT_BROADCOM_BCM5903M	0x16ff		/* BCM5903M */
#define	PCI_PRODUCT_BROADCOM_BCM4401B0	0x170c		/* BCM4401-B0 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5901	0x170d		/* BCM5901 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5901A2	0x170e		/* BCM5901A 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM5906	0x1712		/* BCM5906 */
#define	PCI_PRODUCT_BROADCOM_BCM5906M	0x1713		/* BCM5906M */
#define	PCI_PRODUCT_BROADCOM_BCM4301	0x4301		/* BCM4301 802.11b Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4307	0x4307		/* BCM4307 802.11b Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4311	0x4311		/* BCM4311 802.11a/b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4312	0x4312		/* BCM4312 802.11a/b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4318	0x4318		/* BCM4318 802.11b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4319	0x4319		/* BCM4319 802.11a/b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4306_1	0x4320		/* BCM4306 802.11b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4306_2	0x4321		/* BCM4306 802.11a Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4309	0x4324		/* BCM4309 802.11a/b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4306_3	0x4325		/* BCM4306 802.11b/g Wireless Lan */
#define	PCI_PRODUCT_BROADCOM_BCM4401	0x4401		/* BCM4401 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_BCM4402	0x4402		/* BCM4402 10/100 Ethernet */
#define	PCI_PRODUCT_BROADCOM_5801	0x5801		/* 5801 Security processor */
#define	PCI_PRODUCT_BROADCOM_5802	0x5802		/* 5802 Security processor */
#define	PCI_PRODUCT_BROADCOM_5805	0x5805		/* 5805 Security processor */
#define	PCI_PRODUCT_BROADCOM_5820	0x5820		/* 5820 Security processor */
#define	PCI_PRODUCT_BROADCOM_5821	0x5821		/* 5821 Security processor */
#define	PCI_PRODUCT_BROADCOM_5822	0x5822		/* 5822 Security processor */
#define	PCI_PRODUCT_BROADCOM_5823	0x5823		/* 5823 Security processor */

/* Brooktree products */
#define	PCI_PRODUCT_BROOKTREE_BT848	0x0350		/* Bt848 Video Capture */
#define	PCI_PRODUCT_BROOKTREE_BT849	0x0351		/* Bt849 Video Capture */
#define	PCI_PRODUCT_BROOKTREE_BT878	0x036e		/* Bt878 Video Capture */
#define	PCI_PRODUCT_BROOKTREE_BT879	0x036f		/* Bt879 Video Capture */

/* CNet produts */
#define	PCI_PRODUCT_CNET_GIGACARD	0x434e		/* GigaCard */

/* Compex products - XXX better descriptions */
#define	PCI_PRODUCT_COMPEX_NE2KETHER	0x1401		/* Ethernet */
#define	PCI_PRODUCT_COMPEX_RL100ATX	0x2011		/* RL100-ATX 10/100 Ethernet */

/* Comtrol products */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT32EXT	0x0001		/* RocketPort 32 port external */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT8EXT	0x0002		/* RocketPort 8 port external */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT16EXT	0x0003		/* RocketPort 16 port external */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT4QUAD	0x0004		/* RocketPort 4 port w/ quad cable */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT8OCTA	0x0005		/* RocketPort 8 port w/ octa cable */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT8RJ	0x0006		/* RocketPort 8 port w/ RJ11s */
#define	PCI_PRODUCT_COMTROL_ROCKETPORT4RJ	0x0007		/* RocketPort 4 port w/ RJ11s */
#define	PCI_PRODUCT_COMTROL_ROCKETMODEM6	0x000c		/* RocketModem 6 port */
#define	PCI_PRODUCT_COMTROL_ROCKETMODEM4	0x000d		/* RocketModem 4 port */

/* Corega products */
#define	PCI_PRODUCT_COREGA_CB_TXD	0xa117		/* FEther CB-TXD 10/100 Ethernet */
#define	PCI_PRODUCT_COREGA_2CB_TXD	0xa11e		/* FEther II CB-TXD 10/100 Ethernet */
#define	PCI_PRODUCT_COREGA_CG_LAPCIGT	0xc017		/* CG-LAPCIGT 10/100/1000 Ethernet */

/* Delta products */
#define	PCI_PRODUCT_DELTA_RHINEII	0x1320		/* Rhine II 10/100 Ethernet */
#define	PCI_PRODUCT_DELTA_8139	0x1360		/* 8139 Ethernet */

/* Digi International products */
#define	PCI_PRODUCT_DIGI_SYNC570I_2PB1	0x5010		/* SYNC/570i-PCI 2 port (mapped below 1M) */
#define	PCI_PRODUCT_DIGI_SYNC570I_4PB1	0x5011		/* SYNC/570i-PCI 4 port (mapped below 1M) */
#define	PCI_PRODUCT_DIGI_SYNC570I_2P	0x5012		/* SYNC/570i-PCI 2 port */
#define	PCI_PRODUCT_DIGI_SYNC570I_4P	0x5013		/* SYNC/570i-PCI 4 port */

/* D-Link Systems products */
#define	PCI_PRODUCT_DLINK_DL1002	0x1002		/* DL-1002 10/100 Ethernet */
#define	PCI_PRODUCT_DLINK_DFE530TXPLUS	0x1300		/* DFE-530TXPLUS 10/100 Ethernet */
#define	PCI_PRODUCT_DLINK_DFE690TXD	0x1340		/* DFE-690TXD 10/100 Ethernet */
#define	PCI_PRODUCT_DLINK_DFE520TX_C1	0x4200		/* DFE-520TX C1 */
#define	PCI_PRODUCT_DLINK_DGE528T	0x4300		/* DGE-528T Gigabit Ethernet */
#define	PCI_PRODUCT_DLINK_DGE530T_B1	0x4b01		/* DGE-530T B1 */
#define	PCI_PRODUCT_DLINK_DGE530T_A1	0x4c00		/* DGE-530T A1 */

/* Efficient Networks products */
#define	PCI_PRODUCT_EFFICIENTNETS_ENI155PF	0x0000		/* 155P-MF1 ATM (FPGA) */
#define	PCI_PRODUCT_EFFICIENTNETS_ENI155PA	0x0002		/* 155P-MF1 ATM (ASIC) */

/* Marvell (was Galileo Technology) products */
#define	PCI_PRODUCT_MARVELL_YUKON	0x4320		/* Yukon 88E8001/8003/8010 */
#define	PCI_PRODUCT_MARVELL_YUKON_BELKIN	0x5005		/* Yukon (Belkin F5D5005) */
#define	PCI_PRODUCT_MARVELL_88SE6121	0x6121		/* 88SE6121 SATA/ATA controller */
#define	PCI_PRODUCT_MARVELL_88SE6145	0x6145		/* 88SE6145 SATA/ATA controller */

/* Hifn products */
#define	PCI_PRODUCT_HIFN_7751	0x0005		/* 7751 */
#define	PCI_PRODUCT_HIFN_6500	0x0006		/* 6500 */
#define	PCI_PRODUCT_HIFN_7811	0x0007		/* 7811 */
#define	PCI_PRODUCT_HIFN_7951	0x0012		/* 7951 */
#define	PCI_PRODUCT_HIFN_7955	0x0020		/* 7954/7955 */
#define	PCI_PRODUCT_HIFN_7956	0x001d		/* 7956 */

/* Conexant (iCompression, GlobeSpan) products */
#define	PCI_PRODUCT_ICOMPRESSION_ITVC16	0x0016		/* iTVC16 MPEG2 codec */
#define	PCI_PRODUCT_ICOMPRESSION_ITVC15	0x0803		/* iTVC15 MPEG2 codec */

/* Intel products */
#define	PCI_PRODUCT_INTEL_PRO_WL_2100	0x1043		/* PRO/Wireless LAN 2100 3B Mini PCI Adapter */
#define	PCI_PRODUCT_INTEL_82597EX	0x1048		/* PRO/10GbE LR Server Adapter */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_5	0x1064		/* PRO/100 VE (LOM) Ethernet Controller with 82562ET/EZ/GT/GZ */

/* Invertex */
#define	PCI_PRODUCT_INVERTEX_AEON	0x0005		/* AEON */

/* JMicron Technology Corporation products */
#define	PCI_PRODUCT_JMICRON_JMC250	0x0250		/* JMC250 PCI Express Gigabit Ethernet */
#define	PCI_PRODUCT_JMICRON_JMC260	0x0260		/* JMC260 PCI Express Fast Ethernet */

/* KTI products - XXX better descriptions */
#define	PCI_PRODUCT_KTI_NE2KETHER	0x3000		/* Ethernet */

/* Level One products */
#define	PCI_PRODUCT_LEVELONE_LXT1001	0x0001		/* LXT-1001 10/100/1000 Ethernet */

/* Linksys products */
#define	PCI_PRODUCT_LINKSYS_EG1032	0x1032		/* EG1032 v2 Instant Gigabit Network Adapter */
#define	PCI_PRODUCT_LINKSYS_EG1064	0x1064		/* EG1064 v2 Instant Gigabit Network Adapter */

/* Lucent Technologies products */
#define	PCI_PRODUCT_LUCENT_ET1310	0xed00		/* ET1310 10/100/1000M Ethernet */
#define	PCI_PRODUCT_LUCENT_ET1310_FAST	0xed01		/* ET1310 10/100M Ethernet */

/* NetVin products - XXX better descriptions */
#define	PCI_PRODUCT_NETVIN_5000	0x5000		/* 5000 Ethernet */

/* National Semiconductor products */
#define	PCI_PRODUCT_NS_DP83815	0x0020		/* DP83815 10/100 Ethernet */
#define	PCI_PRODUCT_NS_DP83820	0x0022		/* DP83820 10/100/1000 Ethernet */
#define	PCI_PRODUCT_NS_SCx200_XBUS	0x0505		/* SCx200 X-BUS */
#define	PCI_PRODUCT_NS_SC1100_XBUS	0x0515		/* SC1100 X-Bus */

/* Network Security Technologies, Inc. */
#define	PCI_PRODUCT_NETSEC_7751	0x7751		/* 7751 */

/* Nortel products */
#define	PCI_PRODUCT_NORTEL_BAYSTACK_21	0x1211		/* Baystack 21 (Accton MPX EN5038) */

/* Nvidia Corporation products */
#define	PCI_PRODUCT_NVIDIA_MCP04_LAN1	0x0037		/* MCP04 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP04_LAN2	0x0038		/* MCP04 Lan */
#define	PCI_PRODUCT_NVIDIA_CK804_LAN1	0x0056		/* CK804 Lan */
#define	PCI_PRODUCT_NVIDIA_CK804_LAN2	0x0057		/* CK804 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_LAN	0x0066		/* nForce2 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN2	0x0086		/* nForce3 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN3	0x008c		/* nForce3 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN1	0x00d6		/* nForce3 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN4	0x00df		/* nForce3 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN5	0x00e6		/* nForce3 Lan */
#define	PCI_PRODUCT_NVIDIA_NFORCE_LAN	0x01c3		/* nForce Lan */
#define	PCI_PRODUCT_NVIDIA_MCP51_LAN1	0x0268		/* MCP51 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP51_LAN2	0x0269		/* MCP51 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP55_LAN1	0x0372		/* MCP55 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP55_LAN2	0x0373		/* MCP55 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN1	0x03e5		/* MCP61 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN2	0x03e6		/* MCP61 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN3	0x03ee		/* MCP61 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN4	0x03ef		/* MCP61 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_1	0x044c		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_2	0x044d		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_3	0x044e		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_4	0x044f		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN1	0x0450		/* MCP65 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN2	0x0451		/* MCP65 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN3	0x0452		/* MCP65 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN4	0x0453		/* MCP65 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_5	0x045c		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_6	0x045d		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_7	0x045e		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_8	0x045f		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN1	0x054c		/* MCP67 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN2	0x054d		/* MCP67 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN3	0x054e		/* MCP67 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN4	0x054f		/* MCP67 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP67_AHCI_1	0x0554		/* MCP67 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN1	0x0760		/* MCP77 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN2	0x0761		/* MCP77 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN3	0x0762		/* MCP77 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN4	0x0763		/* MCP77 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN1	0x07dc		/* MCP73 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN2	0x07dd		/* MCP73 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN3	0x07de		/* MCP73 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN4	0x07df		/* MCP73 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN1	0x0ab0		/* MCP79 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN2	0x0ab1		/* MCP79 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN3	0x0ab2		/* MCP79 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN4	0x0ab3		/* MCP79 Lan */
#define	PCI_PRODUCT_NVIDIA_MCP79_AHCI_1	0x0ab8		/* MCP79 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_AHCI_5	0x0ad4		/* MCP77 AHCI */

/* Peppercon products */
#define	PCI_PRODUCT_PEPPERCON_ROLF	0x8139		/* ROL/F-100 Fast Ethernet Adapter with ROL */

/* Planex products */
#define	PCI_PRODUCT_PLANEX_FNW_3800_TX	0xab07		/* FNW-3800-TX 10/100 Ethernet */

/* ProLAN products - XXX better descriptions */
#define	PCI_PRODUCT_PROLAN_NE2KETHER	0x1980		/* Ethernet */

/* QLogic products */
#define	PCI_PRODUCT_QLOGIC_ISP1020	0x1020		/* ISP1020 */
#define	PCI_PRODUCT_QLOGIC_ISP1080	0x1080		/* ISP1080 */
#define	PCI_PRODUCT_QLOGIC_ISP1240	0x1240		/* ISP1240 */
#define	PCI_PRODUCT_QLOGIC_ISP2100	0x2100		/* ISP2100 */

/* Ralink Technologies products */
#define	PCI_PRODUCT_RALINK_RT2560	0x0201		/* RT2560 802.11b/g */
#define	PCI_PRODUCT_RALINK_RT2561S	0x0301		/* RT2561S 802.11b/g */
#define	PCI_PRODUCT_RALINK_RT2561	0x0302		/* RT2561 802.11b/g */
#define	PCI_PRODUCT_RALINK_RT2661	0x0401		/* RT2661 802.11b/g/n */

/* Realtek (Creative Labs?) products */
#define	PCI_PRODUCT_REALTEK_RT8029	0x8029		/* 8029 Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8129	0x8129		/* 8129 10/100 Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8101E	0x8136		/* 8101E PCIe 10/10 Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8139B	0x8138		/* 8139B 10/100 Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8139	0x8139		/* 8139 10/100 Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8169SC	0x8167		/* 8169SC/8110SC Single-chip Gigabit Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8168	0x8168		/* 8168/8111B PCIe Gigabit Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8169	0x8169		/* 8169 10/100/1000 Ethernet */
#define	PCI_PRODUCT_REALTEK_RT8180	0x8180		/* 8180 802.11b */

/* Hangzhou Silan Microelectronics products */
#define	PCI_PRODUCT_SILAN_SC92031	0x2031		/* SC92031 based fast ethernet adapter */
#define	PCI_PRODUCT_SILAN_8139D	0x8139		/* 8139D fast ethernet adapter */

/* Silicon Integrated System products */
#define	PCI_PRODUCT_SIS_900	0x0900		/* SiS 900 10/100 Ethernet */
#define	PCI_PRODUCT_SIS_7016	0x7016		/* SiS 7016 10/100 Ethernet */

/* SMC products */
#define	PCI_PRODUCT_SMC_83C170	0x0005		/* 83C170 (\"EPIC/100\") Fast Ethernet */

/* Sun Microsystems, Inc. products */
#define	PCI_PRODUCT_SUN_5821	0x5454		/* Sun bcm5821 */
#define	PCI_PRODUCT_SUN_SCA1K	0x5455		/* Crypto Accelerator 1000 */

/* Sundance Technology products */
#define	PCI_PRODUCT_SUNDANCETI_ST201	0x0201		/* ST201 10/100 Ethernet */
#define	PCI_PRODUCT_SUNDANCETI_ST201_0	0x0200		/* ST201 10/100 Ethernet */

/* Surecom Technology products */
#define	PCI_PRODUCT_SURECOM_NE34	0x0e34		/* NE-34 Ethernet */

/* Schneider & Koch (really SysKonnect) products */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SKNET_GE	0x4300		/* SK-NET GE */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9821v2	0x4320		/* SK-9821 v2.0 */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK_9DX1	0x4400		/* SK-NET SK-9DX1 Gigabit Ethernet */
/* These next two are are really subsystem IDs */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK_9D41	0x4441		/* SK-9D41 1000BASE-X */

/* SII products */
#define	PCI_PRODUCT_SII_3132	0x3132		/* Sii3132 */

/* TTTech */
#define	PCI_PRODUCT_TTTECH_MC322	0x000a		/* MC322 */

/* Texas Instruments products */
#define	PCI_PRODUCT_TI_ACX100A	0x8400		/* ACX100A 802.11b */
#define	PCI_PRODUCT_TI_ACX100B	0x8401		/* ACX100B 802.11b */
#define	PCI_PRODUCT_TI_ACX111	0x9066		/* ACX111 802.11b/g */

/* US Robotics products */
#define	PCI_PRODUCT_USR2_997902	0x0116		/* Robotics 997902 Gigabit Ethernet */

/* VIA Technologies products, from http://www.via.com.tw/ */
#define	PCI_PRODUCT_VIATECH_VT86C926	0x0926		/* VT86C926 Amazon PCI-Ethernet Controller */
#define	PCI_PRODUCT_VIATECH_VT3043	0x3043		/* VT3043 (Rhine) 10/100 Ethernet */
#define	PCI_PRODUCT_VIATECH_VT6105M	0x3053		/* VT6105M (Rhine III) 10/100 Ethernet */
#define	PCI_PRODUCT_VIATECH_VT6102	0x3065		/* VT6102 (Rhine II) 10/100 Ethernet */
#define	PCI_PRODUCT_VIATECH_VT6105	0x3106		/* VT6105 (Rhine III) 10/100 Ethernet */
#define	PCI_PRODUCT_VIATECH_VT612X	0x3119		/* VT612X 10/100/1000 Ethernet */
#define	PCI_PRODUCT_VIATECH_VT8623_VGA	0x3122		/* VT8623 (Apollo CLE266) VGA Controller */
#define	PCI_PRODUCT_VIATECH_VT8623	0x3123		/* VT8623 (Apollo CLE266) CPU-PCI Bridge */
#define	PCI_PRODUCT_VIATECH_VT8251_SATA	0x3349		/* VT8251 SATA */
#define	PCI_PRODUCT_VIATECH_VT86C100A	0x6100		/* VT86C100A (Rhine-II) 10/100 Ethernet */

/* Winbond Electronics products */
#define	PCI_PRODUCT_WINBOND_W89C840F	0x0840		/* W89C840F 10/100 Ethernet */
#define	PCI_PRODUCT_WINBOND_W89C940F	0x0940		/* W89C940F Ethernet */
