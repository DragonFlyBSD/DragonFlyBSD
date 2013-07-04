/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
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
 *
 * $OpenBSD: ahci.c,v 1.147 2009/02/16 21:19:07 miod Exp $
 */

#if defined(__DragonFly__)
#include "ahci_dragonfly.h"
#else
#error "build for OS unknown"
#endif
#include "pmreg.h"
#include "atascsi.h"

/* change to AHCI_DEBUG for dmesg spam */
#define NO_AHCI_DEBUG

#ifdef AHCI_DEBUG
#define DPRINTF(m, f...) do { if ((ahcidebug & (m)) == (m)) kprintf(f); } \
    while (0)
#define AHCI_D_TIMEOUT		0x00
#define AHCI_D_VERBOSE		0x01
#define AHCI_D_INTR		0x02
#define AHCI_D_XFER		0x08
int ahcidebug = AHCI_D_VERBOSE;
#else
#define DPRINTF(m, f...)
#endif

#define AHCI_PCI_ATI_SB600_MAGIC	0x40
#define AHCI_PCI_ATI_SB600_LOCKED	0x01

#define AHCI_REG_CAP		0x000 /* HBA Capabilities */
#define  AHCI_REG_CAP_NP(_r)		(((_r) & 0x1f)+1) /* Number of Ports */
#define  AHCI_REG_CAP_SXS		(1<<5) /* External SATA */
#define  AHCI_REG_CAP_EMS		(1<<6) /* Enclosure Mgmt */
#define  AHCI_REG_CAP_CCCS		(1<<7) /* Cmd Coalescing */
#define  AHCI_REG_CAP_NCS(_r)		((((_r) & 0x1f00)>>8)+1) /* NCmds*/
#define  AHCI_REG_CAP_PSC		(1<<13) /* Partial State Capable */
#define  AHCI_REG_CAP_SSC		(1<<14) /* Slumber State Capable */
#define  AHCI_REG_CAP_PMD		(1<<15) /* PIO Multiple DRQ Block */
#define  AHCI_REG_CAP_FBSS		(1<<16) /* FIS-Based Switching Supp */
#define  AHCI_REG_CAP_SPM		(1<<17) /* Port Multiplier */
#define  AHCI_REG_CAP_SAM		(1<<18) /* AHCI Only mode */
#define  AHCI_REG_CAP_SNZO		(1<<19) /* Non Zero DMA Offsets */
#define  AHCI_REG_CAP_ISS		(0xf<<20) /* Interface Speed Support */
#define  AHCI_REG_CAP_ISS_G1		(0x1<<20) /* Gen 1 (1.5 Gbps) */
#define  AHCI_REG_CAP_ISS_G2		(0x2<<20) /* Gen 2 (3 Gbps) */
#define  AHCI_REG_CAP_ISS_G3		(0x3<<20) /* Gen 3 (6 Gbps) */
#define  AHCI_REG_CAP_SCLO		(1<<24) /* Cmd List Override */
#define  AHCI_REG_CAP_SAL		(1<<25) /* Activity LED */
#define  AHCI_REG_CAP_SALP		(1<<26) /* Aggressive Link Pwr Mgmt */
#define  AHCI_REG_CAP_SSS		(1<<27) /* Staggered Spinup */
#define  AHCI_REG_CAP_SMPS		(1<<28) /* Mech Presence Switch */
#define  AHCI_REG_CAP_SSNTF		(1<<29) /* SNotification Register */
#define  AHCI_REG_CAP_SNCQ		(1<<30) /* Native Cmd Queuing */
#define  AHCI_REG_CAP_S64A		(1<<31) /* 64bit Addressing */
#define  AHCI_FMT_CAP		"\020" "\040S64A" "\037NCQ" "\036SSNTF" \
				    "\035SMPS" "\034SSS" "\033SALP" "\032SAL" \
				    "\031SCLO" "\024SNZO" "\023SAM" "\022SPM" \
				    "\021FBSS" "\020PMD" "\017SSC" "\016PSC" \
				    "\010CCCS" "\007EMS" "\006SXS"

#define AHCI_REG_GHC		0x004 /* Global HBA Control */
#define  AHCI_REG_GHC_HR		(1<<0) /* HBA Reset */
#define  AHCI_REG_GHC_IE		(1<<1) /* Interrupt Enable */
#define  AHCI_REG_GHC_MRSM		(1<<2) /* MSI Revert to Single Msg */
#define  AHCI_REG_GHC_AE		(1<<31) /* AHCI Enable */
#define AHCI_FMT_GHC		"\020" "\040AE" "\003MRSM" "\002IE" "\001HR"

#define AHCI_REG_IS		0x008 /* Interrupt Status */
#define AHCI_REG_PI		0x00c /* Ports Implemented */

#define AHCI_REG_VS		0x010 /* AHCI Version */
#define  AHCI_REG_VS_0_95		0x00000905 /* 0.95 */
#define  AHCI_REG_VS_1_0		0x00010000 /* 1.0 */
#define  AHCI_REG_VS_1_1		0x00010100 /* 1.1 */
#define  AHCI_REG_VS_1_2		0x00010200 /* 1.2 */
#define  AHCI_REG_VS_1_3		0x00010300 /* 1.3 */
#define  AHCI_REG_VS_1_4		0x00010400 /* 1.4 */
#define  AHCI_REG_VS_1_5		0x00010500 /* 1.5 (future...) */

#define AHCI_REG_CCC_CTL	0x014 /* Coalescing Control */
#define  AHCI_REG_CCC_CTL_INT(_r)	(((_r) & 0xf8) >> 3) /* CCC INT slot */

#define AHCI_REG_CCC_PORTS	0x018 /* Coalescing Ports */
#define AHCI_REG_EM_LOC		0x01c /* Enclosure Mgmt Location */
#define AHCI_REG_EM_CTL		0x020 /* Enclosure Mgmt Control */

#define AHCI_REG_CAP2		0x024 /* Host Capabilities Extended */
#define  AHCI_REG_CAP2_BOH		(1<<0) /* BIOS/OS Handoff */
#define  AHCI_REG_CAP2_NVMP		(1<<1) /* NVMHCI Present */
#define  AHCI_REG_CAP2_APST		(1<<2) /* A-Partial to Slumber Trans */
#define  AHCI_FMT_CAP2		"\020" "\003BOH" "\002NVMP" "\001BOH"

#define AHCI_REG_BOHC		0x028 /* BIOS/OS Handoff Control and Status */
#define  AHCI_REG_BOHC_BOS		(1<<0) /* BIOS Owned Semaphore */
#define  AHCI_REG_BOHC_OOS		(1<<1) /* OS Owned Semaphore */
#define  AHCI_REG_BOHC_SOOE		(1<<2) /* SMI on OS Own chg enable */
#define  AHCI_REG_BOHC_OOC		(1<<3) /* OS Ownership Change */
#define  AHCI_REG_BOHC_BB		(1<<4) /* BIOS Busy */
#define  AHCI_FMT_BOHC		"\020" "\005BB" "\004OOC" "\003SOOE" \
				"\002OOS" "\001BOS"

#define AHCI_PORT_REGION(_p)	(0x100 + ((_p) * 0x80))
#define AHCI_PORT_SIZE		0x80

#define AHCI_PREG_CLB		0x00 /* Cmd List Base Addr */
#define AHCI_PREG_CLBU		0x04 /* Cmd List Base Hi Addr */
#define AHCI_PREG_FB		0x08 /* FIS Base Addr */
#define AHCI_PREG_FBU		0x0c /* FIS Base Hi Addr */

#define AHCI_PREG_IS		0x10 /* Interrupt Status */
#define  AHCI_PREG_IS_DHRS		(1<<0) /* Device to Host FIS */
#define  AHCI_PREG_IS_PSS		(1<<1) /* PIO Setup FIS */
#define  AHCI_PREG_IS_DSS		(1<<2) /* DMA Setup FIS */
#define  AHCI_PREG_IS_SDBS		(1<<3) /* Set Device Bits FIS */
#define  AHCI_PREG_IS_UFS		(1<<4) /* Unknown FIS */
#define  AHCI_PREG_IS_DPS		(1<<5) /* Descriptor Processed */
#define  AHCI_PREG_IS_PCS		(1<<6) /* Port Change */
#define  AHCI_PREG_IS_DMPS		(1<<7) /* Device Mechanical Presence */
#define  AHCI_PREG_IS_PRCS		(1<<22) /* PhyRdy Change */
#define  AHCI_PREG_IS_IPMS		(1<<23) /* Incorrect Port Multiplier */
#define  AHCI_PREG_IS_OFS		(1<<24) /* Overflow */
#define  AHCI_PREG_IS_INFS		(1<<26) /* Interface Non-fatal Error */
#define  AHCI_PREG_IS_IFS		(1<<27) /* Interface Fatal Error */
#define  AHCI_PREG_IS_HBDS		(1<<28) /* Host Bus Data Error */
#define  AHCI_PREG_IS_HBFS		(1<<29) /* Host Bus Fatal Error */
#define  AHCI_PREG_IS_TFES		(1<<30) /* Task File Error */
#define  AHCI_PREG_IS_CPDS		(1<<31) /* Cold Presence Detect */
#define AHCI_PFMT_IS		"\20" "\040CPDS" "\037TFES" "\036HBFS" \
				    "\035HBDS" "\034IFS" "\033INFS" "\031OFS" \
				    "\030IPMS" "\027PRCS" "\010DMPS" "\006DPS" \
				    "\007PCS" "\005UFS" "\004SDBS" "\003DSS" \
				    "\002PSS" "\001DHRS"

#define AHCI_PREG_IE		0x14 /* Interrupt Enable */
#define  AHCI_PREG_IE_DHRE		(1<<0) /* Device to Host FIS */
#define  AHCI_PREG_IE_PSE		(1<<1) /* PIO Setup FIS */
#define  AHCI_PREG_IE_DSE		(1<<2) /* DMA Setup FIS */
#define  AHCI_PREG_IE_SDBE		(1<<3) /* Set Device Bits FIS */
#define  AHCI_PREG_IE_UFE		(1<<4) /* Unknown FIS */
#define  AHCI_PREG_IE_DPE		(1<<5) /* Descriptor Processed */
#define  AHCI_PREG_IE_PCE		(1<<6) /* Port Change */
#define  AHCI_PREG_IE_DMPE		(1<<7) /* Device Mechanical Presence */
#define  AHCI_PREG_IE_PRCE		(1<<22) /* PhyRdy Change */
#define  AHCI_PREG_IE_IPME		(1<<23) /* Incorrect Port Multiplier */
#define  AHCI_PREG_IE_OFE		(1<<24) /* Overflow */
#define  AHCI_PREG_IE_INFE		(1<<26) /* Interface Non-fatal Error */
#define  AHCI_PREG_IE_IFE		(1<<27) /* Interface Fatal Error */
#define  AHCI_PREG_IE_HBDE		(1<<28) /* Host Bus Data Error */
#define  AHCI_PREG_IE_HBFE		(1<<29) /* Host Bus Fatal Error */
#define  AHCI_PREG_IE_TFEE		(1<<30) /* Task File Error */
#define  AHCI_PREG_IE_CPDE		(1<<31) /* Cold Presence Detect */
#define AHCI_PFMT_IE		"\20" "\040CPDE" "\037TFEE" "\036HBFE" \
				    "\035HBDE" "\034IFE" "\033INFE" "\031OFE" \
				    "\030IPME" "\027PRCE" "\010DMPE" "\007PCE" \
				    "\006DPE" "\005UFE" "\004SDBE" "\003DSE" \
				    "\002PSE" "\001DHRE"

/*
 * NOTE: bits 22, 21, 20, 19, 18, 16, 15, 14, 13, 12:08, 07:05 are always
 *       read-only.  Other bits may be read-only when the related feature
 *	 is not supported by the HBA.
 */
#define AHCI_PREG_CMD		0x18 /* Command and Status */
#define  AHCI_PREG_CMD_ST		(1<<0) /* Start */
#define  AHCI_PREG_CMD_SUD		(1<<1) /* Spin Up Device */
#define  AHCI_PREG_CMD_POD		(1<<2) /* Power On Device */
#define  AHCI_PREG_CMD_CLO		(1<<3) /* Command List Override */
#define  AHCI_PREG_CMD_FRE		(1<<4) /* FIS Receive Enable */
#define  AHCI_PREG_CMD_CCS(_r)		(((_r) >> 8) & 0x1f) /* Curr CmdSlot# */
#define  AHCI_PREG_CMD_MPSS		(1<<13) /* Mech Presence State */
#define  AHCI_PREG_CMD_FR		(1<<14) /* FIS Receive Running */
#define  AHCI_PREG_CMD_CR		(1<<15) /* Command List Running */
#define  AHCI_PREG_CMD_CPS		(1<<16) /* Cold Presence State */
#define  AHCI_PREG_CMD_PMA		(1<<17) /* Port Multiplier Attached */
#define  AHCI_PREG_CMD_HPCP		(1<<18) /* Hot Plug Capable */
#define  AHCI_PREG_CMD_MPSP		(1<<19) /* Mech Presence Switch */
#define  AHCI_PREG_CMD_CPD		(1<<20) /* Cold Presence Detection */
#define  AHCI_PREG_CMD_ESP		(1<<21) /* External SATA Port */
#define  AHCI_PREG_CMD_FBSCP		(1<<22) /* FIS-based sw capable port */
#define  AHCI_PREG_CMD_APSTE		(1<<23) /* Auto Partial to Slumber */
#define  AHCI_PREG_CMD_ATAPI		(1<<24) /* Device is ATAPI */
#define  AHCI_PREG_CMD_DLAE		(1<<25) /* Drv LED on ATAPI Enable */
#define  AHCI_PREG_CMD_ALPE		(1<<26) /* Aggro Pwr Mgmt Enable */
#define  AHCI_PREG_CMD_ASP		(1<<27) /* Aggro Slumber/Partial */
#define  AHCI_PREG_CMD_ICC		0xf0000000 /* Interface Comm Ctrl */
#define  AHCI_PREG_CMD_ICC_SLUMBER	0x60000000
#define  AHCI_PREG_CMD_ICC_PARTIAL	0x20000000
#define  AHCI_PREG_CMD_ICC_ACTIVE	0x10000000
#define  AHCI_PREG_CMD_ICC_IDLE		0x00000000
#define  AHCI_PFMT_CMD		"\020" "\034ASP" "\033ALPE" "\032DLAE" \
				    "\031ATAPI" "\030APSTE" "\027FBSCP" \
				    "\026ESP" "\025CPD" "\024MPSP" \
				    "\023HPCP" "\022PMA" "\021CPS" "\020CR" \
				    "\017FR" "\016MPSS" "\005FRE" "\004CLO" \
				    "\003POD" "\002SUD" "\001ST"

#define AHCI_PREG_TFD		0x20 /* Task File Data*/
#define  AHCI_PREG_TFD_STS		0xff
#define  AHCI_PREG_TFD_STS_ERR		(1<<0)
#define  AHCI_PREG_TFD_STS_DRQ		(1<<3)
#define  AHCI_PREG_TFD_STS_BSY		(1<<7)
#define  AHCI_PREG_TFD_ERR		0xff00

#define AHCI_PFMT_TFD_STS	"\20" "\010BSY" "\004DRQ" "\001ERR"
#define AHCI_PREG_SIG		0x24 /* Signature */

#define AHCI_PREG_SSTS		0x28 /* SATA Status */
#define  AHCI_PREG_SSTS_DET		0xf /* Device Detection */
#define  AHCI_PREG_SSTS_DET_NONE	0x0
#define  AHCI_PREG_SSTS_DET_DEV_NE	0x1
#define  AHCI_PREG_SSTS_DET_DEV		0x3
#define  AHCI_PREG_SSTS_DET_PHYOFFLINE	0x4
#define  AHCI_PREG_SSTS_SPD		0xf0 /* Current Interface Speed */
#define  AHCI_PREG_SSTS_SPD_NONE	0x00
#define  AHCI_PREG_SSTS_SPD_GEN1	0x10
#define  AHCI_PREG_SSTS_SPD_GEN2	0x20
#define  AHCI_PREG_SSTS_SPD_GEN3	0x30
#define  AHCI_PREG_SSTS_IPM		0xf00 /* Interface Power Management */
#define  AHCI_PREG_SSTS_IPM_NONE	0x000
#define  AHCI_PREG_SSTS_IPM_ACTIVE	0x100
#define  AHCI_PREG_SSTS_IPM_PARTIAL	0x200
#define  AHCI_PREG_SSTS_IPM_SLUMBER	0x600

#define AHCI_PREG_SCTL		0x2c /* SATA Control */
#define  AHCI_PREG_SCTL_DET		0xf /* Device Detection */
#define  AHCI_PREG_SCTL_DET_NONE	0x0
#define  AHCI_PREG_SCTL_DET_INIT	0x1
#define  AHCI_PREG_SCTL_DET_DISABLE	0x4
#define  AHCI_PREG_SCTL_SPD		0xf0 /* Speed Allowed */
#define  AHCI_PREG_SCTL_SPD_ANY		0x00
#define  AHCI_PREG_SCTL_SPD_GEN1	0x10
#define  AHCI_PREG_SCTL_SPD_GEN2	0x20
#define  AHCI_PREG_SCTL_SPD_GEN3	0x30
#define  AHCI_PREG_SCTL_IPM		0xf00 /* Interface Power Management */
#define  AHCI_PREG_SCTL_IPM_NONE	0x000
#define  AHCI_PREG_SCTL_IPM_NOPARTIAL	0x100
#define  AHCI_PREG_SCTL_IPM_NOSLUMBER	0x200
#define  AHCI_PREG_SCTL_IPM_DISABLED	0x300
#define	 AHCI_PREG_SCTL_SPM		0xf000	/* Select Power Management */
#define	 AHCI_PREG_SCTL_SPM_NONE	0x0000
#define	 AHCI_PREG_SCTL_SPM_NOPARTIAL	0x1000
#define	 AHCI_PREG_SCTL_SPM_NOSLUMBER	0x2000
#define	 AHCI_PREG_SCTL_SPM_DISABLED	0x3000
#define  AHCI_PREG_SCTL_PMP		0xf0000	/* Set PM port for xmit FISes */
#define  AHCI_PREG_SCTL_PMP_SHIFT	16

#define AHCI_PREG_SERR		0x30 /* SATA Error */
#define  AHCI_PREG_SERR_ERR_I		(1<<0) /* Recovered Data Integrity */
#define  AHCI_PREG_SERR_ERR_M		(1<<1) /* Recovered Communications */
#define  AHCI_PREG_SERR_ERR_T		(1<<8) /* Transient Data Integrity */
#define  AHCI_PREG_SERR_ERR_C		(1<<9) /* Persistent Comm/Data */
#define  AHCI_PREG_SERR_ERR_P		(1<<10) /* Protocol */
#define  AHCI_PREG_SERR_ERR_E		(1<<11) /* Internal */
#define  AHCI_PREG_SERR_DIAG_N		(1<<16) /* PhyRdy Change */
#define  AHCI_PREG_SERR_DIAG_I		(1<<17) /* Phy Internal Error */
#define  AHCI_PREG_SERR_DIAG_W		(1<<18) /* Comm Wake */
#define  AHCI_PREG_SERR_DIAG_B		(1<<19) /* 10B to 8B Decode Error */
#define  AHCI_PREG_SERR_DIAG_D		(1<<20) /* Disparity Error */
#define  AHCI_PREG_SERR_DIAG_C		(1<<21) /* CRC Error */
#define  AHCI_PREG_SERR_DIAG_H		(1<<22) /* Handshake Error */
#define  AHCI_PREG_SERR_DIAG_S		(1<<23) /* Link Sequence Error */
#define  AHCI_PREG_SERR_DIAG_T		(1<<24) /* Transport State Trans Err */
#define  AHCI_PREG_SERR_DIAG_F		(1<<25) /* Unknown FIS Type */
#define  AHCI_PREG_SERR_DIAG_X		(1<<26) /* Exchanged */

#define  AHCI_PFMT_SERR	"\020" 	\
			"\033DIAG.X" "\032DIAG.F" "\031DIAG.T" "\030DIAG.S" \
			"\027DIAG.H" "\026DIAG.C" "\025DIAG.D" "\024DIAG.B" \
			"\023DIAG.W" "\022DIAG.I" "\021DIAG.N"		    \
			"\014ERR.E" "\013ERR.P" "\012ERR.C" "\011ERR.T"	    \
			"\002ERR.M" "\001ERR.I"

#define AHCI_PREG_SACT		0x34 /* SATA Active */
#define AHCI_PREG_CI		0x38 /* Command Issue */
#define  AHCI_PREG_CI_ALL_SLOTS	0xffffffff
#define AHCI_PREG_SNTF		0x3c /* SNotification */

/*
 * EN	- Enable FIS based switch, can only be changed when ST is clear
 *
 * DEC	- Device Error Clear, state machine.  Set to 1 by software only
 *	  for the EN+SDE case, then poll until hardware sets it back to 0.
 *	  Writing 0 has no effect.
 *
 * SDE	- Set by hardware indicating a single device error occurred.  If
 *	  not set and an error occurred then the error was whole-port.
 *
 * DEV	- Set by software to the PM target of the next command to issue
 *	  via the PREG_CI registers.  Software should not issue multiple
 *	  commands covering different targets in a single write.  This
 *	  basically causes writes to PREG_CI to index within the hardware.
 *
 * ADO	- (read only) Indicate how many concurrent devices commands may
 *	  be issued to at once.  Degredation may occur if commands are
 *	  issued to more devices but the case is allowed.
 *
 * DWE	- (read only) Only valid on SDE errors.  Hardware indicates which
 *	  PM target generated the error in this field.
 *
 */
#define AHCI_PREG_FBS		0x40 /* FIS-Based Switching Control */
#define  AHCI_PREG_FBS_EN		(1<<0) /* FIS-Based switching enable */
#define  AHCI_PREG_FBS_DEC		(1<<1) /* Device Error Clear */
#define  AHCI_PREG_FBS_SDE		(1<<2) /* Single-device Error */
#define  AHCI_PREG_FBS_DEV		0x00000F00 /* Device to Issue mask */
#define  AHCI_PREG_FBS_ADO		0x0000F000 /* Active Dev Optimize */
#define  AHCI_PREG_FBS_DWE		0x000F0000 /* Device With Error */
#define  AHCI_PREG_FBS_DEV_SHIFT	8
#define  AHCI_PREG_FBS_ADO_SHIFT	12
#define  AHCI_PREG_FBS_DWE_SHIFT	16

/*
 * AHCI mapped structures
 */
struct ahci_cmd_hdr {
	u_int16_t		flags;
#define AHCI_CMD_LIST_FLAG_CFL		0x001f /* Command FIS Length */
#define AHCI_CMD_LIST_FLAG_A		(1<<5) /* ATAPI */
#define AHCI_CMD_LIST_FLAG_W		(1<<6) /* Write */
#define AHCI_CMD_LIST_FLAG_P		(1<<7) /* Prefetchable */
#define AHCI_CMD_LIST_FLAG_R		(1<<8) /* Reset */
#define AHCI_CMD_LIST_FLAG_B		(1<<9) /* BIST */
#define AHCI_CMD_LIST_FLAG_C		(1<<10) /* Clear Busy upon R_OK */
#define AHCI_CMD_LIST_FLAG_PMP		0xf000 /* Port Multiplier Port */
#define AHCI_CMD_LIST_FLAG_PMP_SHIFT	12
	u_int16_t		prdtl; /* sgl len */

	u_int32_t		prdbc; /* transferred byte count */

	u_int32_t		ctba_lo;
	u_int32_t		ctba_hi;

	u_int32_t		reserved[4];
} __packed;

struct ahci_rfis {
	u_int8_t		dsfis[28];
	u_int8_t		reserved1[4];
	u_int8_t		psfis[24];
	u_int8_t		reserved2[8];
	u_int8_t		rfis[24];
	u_int8_t		reserved3[4];
	u_int8_t		sdbfis[4];
	u_int8_t		ufis[64];
	u_int8_t		reserved4[96];
} __packed;

struct ahci_prdt {
	u_int32_t		dba_lo;
	u_int32_t		dba_hi;
	u_int32_t		reserved;
	u_int32_t		flags;
#define AHCI_PRDT_FLAG_INTR		(1<<31) /* interrupt on completion */
} __packed;

/*
 * The base command table structure is 128 bytes.  Each prdt is 16 bytes.
 * We need to accomodate a 2MB maximum I/O transfer size, which is at least
 * 512 entries, plus one for page slop.
 *
 * Making the ahci_cmd_table 16384 bytes (a reasonable power of 2)
 * thus requires MAX_PRDT to be set to 1016.
 */
#define AHCI_MAX_PRDT		1016
#define AHCI_MAX_PMPORTS	16

#define AHCI_MAXPHYS		(2 * 1024 * 1024)	/* 2MB */
#if AHCI_MAXPHYS / PAGE_SIZE + 1 > AHCI_MAX_PRDT
#error "AHCI_MAX_PRDT is not big enough"
#endif

struct ahci_cmd_table {
	u_int8_t		cfis[64];	/* Command FIS */
	u_int8_t		acmd[16];	/* ATAPI Command */
	u_int8_t		reserved[48];

	struct ahci_prdt	prdt[AHCI_MAX_PRDT];
} __packed;

#define AHCI_MAX_PORTS		32

struct ahci_dmamem {
	bus_dma_tag_t		adm_tag;
	bus_dmamap_t		adm_map;
	bus_dma_segment_t	adm_seg;
	bus_addr_t		adm_busaddr;
	caddr_t			adm_kva;
};
#define AHCI_DMA_MAP(_adm)	((_adm)->adm_map)
#define AHCI_DMA_DVA(_adm)	((_adm)->adm_busaddr)
#define AHCI_DMA_KVA(_adm)	((void *)(_adm)->adm_kva)

struct ahci_softc;
struct ahci_port;
struct ahci_device;

struct ahci_ccb {
	/* ATA xfer associated with this CCB.  Must be 1st struct member. */
	struct ata_xfer		ccb_xa;
	struct callout          ccb_timeout;

	int			ccb_slot;
	struct ahci_port	*ccb_port;

	bus_dmamap_t		ccb_dmamap;
	struct ahci_cmd_hdr	*ccb_cmd_hdr;
	struct ahci_cmd_table	*ccb_cmd_table;

	void			(*ccb_done)(struct ahci_ccb *);

	TAILQ_ENTRY(ahci_ccb)	ccb_entry;
};

struct ahci_port {
	struct ahci_softc	*ap_sc;
	bus_space_handle_t	ap_ioh;

	int			ap_num;
	int			ap_pmcount;
	int			ap_flags;
#define AP_F_BUS_REGISTERED	0x0001
#define AP_F_CAM_ATTACHED	0x0002
#define AP_F_IN_RESET		0x0004
#define AP_F_SCAN_RUNNING	0x0008
#define AP_F_SCAN_REQUESTED	0x0010
#define AP_F_SCAN_COMPLETED	0x0020
#define AP_F_IGNORE_IFS		0x0040
#define AP_F_IFS_IGNORED	0x0080
#define AP_F_UNUSED_0100	0x0100
#define AP_F_EXCLUSIVE_ACCESS	0x0200
#define AP_F_ERR_CCB_RESERVED	0x0400
#define AP_F_HARSH_REINIT	0x0800
	int			ap_signal;	/* os per-port thread sig */
	thread_t		ap_thread;	/* os per-port thread */
	struct lock		ap_lock;	/* os per-port lock */
	struct lock		ap_sim_lock;	/* cam sim lock */
	struct lock		ap_sig_lock;	/* signal thread */
#define AP_SIGF_INIT		0x0001
#define AP_SIGF_TIMEOUT		0x0002
#define AP_SIGF_PORTINT		0x0004
#define AP_SIGF_THREAD_SYNC	0x0008
#define AP_SIGF_STOP		0x8000
	struct cam_sim		*ap_sim;

	struct ahci_rfis	*ap_rfis;
	struct ahci_dmamem	*ap_dmamem_rfis;

	struct ahci_dmamem	*ap_dmamem_cmd_list;
	struct ahci_dmamem	*ap_dmamem_cmd_table;

	u_int32_t		ap_active;	/* active CI command bmask */
	u_int32_t		ap_active_cnt;	/* active CI command count */
	u_int32_t		ap_sactive;	/* active SACT command bmask */
	u_int32_t		ap_expired;	/* deferred expired bmask */
	u_int32_t		ap_intmask;	/* interrupts we care about */
	struct ahci_ccb		*ap_ccbs;
	struct ahci_ccb		*ap_err_ccb;	/* always CCB SLOT 1 */
	int			ap_run_flags;	/* used to check excl mode */

	TAILQ_HEAD(, ahci_ccb)	ap_ccb_free;
	TAILQ_HEAD(, ahci_ccb)	ap_ccb_pending;
	struct lock		ap_ccb_lock;

	int			ap_type;	/* ATA_PORT_T_xxx */
	int			ap_probe;	/* ATA_PROBE_xxx */
	struct ata_port		*ap_ata[AHCI_MAX_PMPORTS];

	u_int32_t		ap_state;
#define AP_S_NORMAL			0
#define AP_S_FATAL_ERROR		1

	/* For error recovery. */
	u_int32_t		ap_err_saved_sactive;
	u_int32_t		ap_err_saved_active;
	u_int32_t		ap_err_saved_active_cnt;

	u_int8_t		*ap_err_scratch;

	int			link_pwr_mgmt;

	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;

	char			ap_name[16];
};

#define PORTNAME(_ap)		((_ap)->ap_name)
#define ATANAME(_ap, _at)	((_at) ? (_at)->at_name : (_ap)->ap_name)

struct ahci_softc {
	device_t		sc_dev;
	const struct ahci_device *sc_ad;	/* special casing */

	struct resource		*sc_irq;	/* bus resources */
	struct resource		*sc_regs;	/* bus resources */
	bus_space_tag_t		sc_iot;		/* split from sc_regs */
	bus_space_handle_t	sc_ioh;		/* split from sc_regs */

	int			sc_irq_type;
	int			sc_rid_irq;	/* saved bus RIDs */
	int			sc_rid_regs;
	u_int32_t		sc_cap;		/* capabilities */
	u_int32_t		sc_cap2;	/* capabilities */
	u_int32_t		sc_vers;	/* AHCI version */
	int			sc_numports;
	u_int32_t		sc_portmask;

	void			*sc_irq_handle;	/* installed irq vector */

	bus_dma_tag_t		sc_tag_rfis;	/* bus DMA tags */
	bus_dma_tag_t		sc_tag_cmdh;
	bus_dma_tag_t		sc_tag_cmdt;
	bus_dma_tag_t		sc_tag_data;

	int			sc_flags;
#define AHCI_F_NO_NCQ			0x00000001
#define AHCI_F_IGN_FR			0x00000002
#define AHCI_F_INT_GOOD			0x00000004
#define AHCI_F_FORCE_FBSS		0x00000008

	u_int			sc_ncmds;

	struct ahci_port	*sc_ports[AHCI_MAX_PORTS];

#ifdef AHCI_COALESCE
	u_int32_t		sc_ccc_mask;
	u_int32_t		sc_ccc_ports;
	u_int32_t		sc_ccc_ports_cur;
#endif

	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};
#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)

struct ahci_device {
	pci_vendor_id_t		ad_vendor;
	pci_product_id_t	ad_product;
	int			(*ad_attach)(device_t dev);
	int			(*ad_detach)(device_t dev);
	char			*name;
};

/* Wait for all bits in _b to be cleared */
#define ahci_pwait_clr(_ap, _r, _b) \
	ahci_pwait_eq((_ap), AHCI_PWAIT_TIMEOUT, (_r), (_b), 0)
#define ahci_pwait_clr_to(_ap, _to,  _r, _b) \
	ahci_pwait_eq((_ap), _to, (_r), (_b), 0)

/* Wait for all bits in _b to be set */
#define ahci_pwait_set(_ap, _r, _b) \
	ahci_pwait_eq((_ap), AHCI_PWAIT_TIMEOUT, (_r), (_b), (_b))
#define ahci_pwait_set_to(_ap, _to, _r, _b) \
	ahci_pwait_eq((_ap), _to, (_r), (_b), (_b))

#define AHCI_PWAIT_TIMEOUT      1000

const struct ahci_device *ahci_lookup_device(device_t dev);
int	ahci_init(struct ahci_softc *);
int	ahci_port_init(struct ahci_port *ap);
int	ahci_port_alloc(struct ahci_softc *, u_int);
void	ahci_port_state_machine(struct ahci_port *ap, int initial);
void	ahci_port_free(struct ahci_softc *, u_int);
int	ahci_port_reset(struct ahci_port *, struct ata_port *at, int);
void	ahci_port_link_pwr_mgmt(struct ahci_port *, int link_pwr_mgmt);
int	ahci_port_link_pwr_state(struct ahci_port *);

u_int32_t ahci_read(struct ahci_softc *, bus_size_t);
void	ahci_write(struct ahci_softc *, bus_size_t, u_int32_t);
int	ahci_wait_ne(struct ahci_softc *, bus_size_t, u_int32_t, u_int32_t);
u_int32_t ahci_pread(struct ahci_port *, bus_size_t);
void	ahci_pwrite(struct ahci_port *, bus_size_t, u_int32_t);
int	ahci_pwait_eq(struct ahci_port *, int, bus_size_t,
			u_int32_t, u_int32_t);
void	ahci_intr(void *);
void	ahci_port_intr(struct ahci_port *ap, int blockable);

int	ahci_port_start(struct ahci_port *ap);
int	ahci_port_stop(struct ahci_port *ap, int stop_fis_rx);
int	ahci_port_clo(struct ahci_port *ap);
void	ahci_flush_tfd(struct ahci_port *ap);
int	ahci_set_feature(struct ahci_port *ap, struct ata_port *atx,
			int feature, int enable);

int	ahci_cam_attach(struct ahci_port *ap);
void	ahci_cam_changed(struct ahci_port *ap, struct ata_port *at, int found);
void	ahci_cam_detach(struct ahci_port *ap);
int	ahci_cam_probe(struct ahci_port *ap, struct ata_port *at);

struct ata_xfer *ahci_ata_get_xfer(struct ahci_port *ap, struct ata_port *at);
void	ahci_ata_put_xfer(struct ata_xfer *xa);
int	ahci_ata_cmd(struct ata_xfer *xa);

int     ahci_pm_port_probe(struct ahci_port *ap, int);
int	ahci_pm_port_init(struct ahci_port *ap, struct ata_port *at);
int	ahci_pm_identify(struct ahci_port *ap);
int	ahci_pm_hardreset(struct ahci_port *ap, int target, int hard);
int	ahci_pm_softreset(struct ahci_port *ap, int target);
int	ahci_pm_phy_status(struct ahci_port *ap, int target, u_int32_t *datap);
int	ahci_pm_read(struct ahci_port *ap, int target,
			int which, u_int32_t *res);
int	ahci_pm_write(struct ahci_port *ap, int target,
			int which, u_int32_t data);
void	ahci_pm_check_good(struct ahci_port *ap, int target);
void	ahci_ata_cmd_timeout(struct ahci_ccb *ccb);
void	ahci_quick_timeout(struct ahci_ccb *ccb);
struct ahci_ccb *ahci_get_ccb(struct ahci_port *ap);
void	ahci_put_ccb(struct ahci_ccb *ccb);
struct ahci_ccb *ahci_get_err_ccb(struct ahci_port *);
void	ahci_put_err_ccb(struct ahci_ccb *);
int	ahci_poll(struct ahci_ccb *ccb, int timeout,
			void (*timeout_fn)(struct ahci_ccb *));

int     ahci_port_signature_detect(struct ahci_port *ap, struct ata_port *at);
void	ahci_port_thread_core(struct ahci_port *ap, int mask);

void	ahci_os_sleep(int ms);
void	ahci_os_hardsleep(int us);
int	ahci_os_softsleep(void);
void	ahci_os_start_port(struct ahci_port *ap);
void	ahci_os_stop_port(struct ahci_port *ap);
void	ahci_os_signal_port_thread(struct ahci_port *ap, int mask);
void	ahci_os_lock_port(struct ahci_port *ap);
int	ahci_os_lock_port_nb(struct ahci_port *ap);
void	ahci_os_unlock_port(struct ahci_port *ap);

extern u_int32_t AhciForceGen;
extern u_int32_t AhciNoFeatures;

enum {AHCI_LINK_PWR_MGMT_NONE, AHCI_LINK_PWR_MGMT_MEDIUM,
      AHCI_LINK_PWR_MGMT_AGGR};
