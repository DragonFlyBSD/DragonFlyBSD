/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

typedef struct {
	/* TODO (Fig 91 - Identify Power State Descriptor Data Structure) */
	uint8_t		filler[32];
} __packed nvme_pwstate_data_t;

/*
 * NVME Identify Command data response structures
 */
typedef struct {
	/*
	 * Controller Capabilities and Features
	 */
	uint16_t	pci_vendor;
	uint16_t	pci_subsys;
	uint8_t		serialno[20];	/* ascii string */
	uint8_t		modelno[40];	/* ascii string */

	uint64_t	fwrev;		/* firmware revision */
	uint8_t		arb_burst;	/* recommended arbitration burst */
	uint8_t		ieee_oui[3];	/* IEEE OUI identifier */
	uint8_t		cmic_caps;	/* multi-path / ns sharing caps */
	uint8_t		mdts;		/* max data xfer size 2^N x mempgsiz */

	uint16_t	cntlid;		/* unique controller id */
	uint32_t	vers;		/* (copy of version register) */
	uint32_t	rtd3r;		/* typical resume latency uS */
	uint32_t	rtd3e;		/* typical entry latency uS */
	uint32_t	async_cap;	/* (opt) async capabilities */
	uint8_t		reserved96[144];

	char		mgmt_ifc[16];	/* NVMe Manage Interface spec */

	/*
	 * Admin Command Set Attributes and Optional Controller Capabilities
	 */
	uint16_t	admin_cap;
	uint8_t		abort_lim;	/* max concurrent aborts */
	uint8_t		async_lim;	/* max concurrent async event reqs */
	uint8_t		fwupd_caps;	/* firmware update capabilities */
	uint8_t		logpg_attr;
	uint8_t		logpg_error_entries; /* #of error log ent supported */
	uint8_t		num_power_states; /* number of power states supported*/
	uint8_t		avscc;		/* formatting convention for vendor */
					/* specific commands */
	uint8_t		apsta;		/* autonomous power states supported */
	uint16_t	warn_comp_temp;	/* warning comp temp threshold 0=unimp*/
	uint16_t	crit_comp_temp;	/* warning comp temp threshold 0=unimp*/
	uint16_t	fw_act_maxtime;	/* max act time N x 100ms, 0=unknown */
	uint32_t	hmpre;		/* host mem buf requested size */
	uint32_t	hmmin;		/* host mem buf minimum size */
	uint64_t	total_capacity[2]; /* 16-byte field, cap in bytes */
	uint64_t	unalloc_capacity[2]; /* 16-byte field, avail to alloc */
					     /* for namespace attachment */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	reply_flags;
	uint8_t		replay_tot_size; /* each RPMB in 128KB units, 0=128KB */
	uint8_t		replay_acc_size; /* each RPMB security send/recv */
					 /*  in 512B units, 0=512B */
#else
	uint8_t		replay_acc_size;
	uint8_t		replay_tot_size;
	uint16_t	reply_flags;
#endif
	uint8_t		reserved316[196];

	/*
	 * NVM Command Set Attributes (starting at offset 512)
	 */
	uint8_t		subq_entry_size;	/* see defines */
	uint8_t		comq_entry_size;	/* see defines */
	uint8_t		reserved514;
	uint8_t		reserved515;
	uint32_t	ns_count;		/* number of valid namespaces */
	uint16_t	nvm_opt_cap;		/* optional nvm I/O cmds */
	uint16_t	nvm_fuse_cap;		/* optional nvm fuse cmds */
	uint8_t		nvm_format_cap;
	uint8_t		vwc_flags;		/* volatile write cache caps */
	uint16_t	atomic_wun;		/* atomic write guarantee */
						/* log blocks, 0 = 1 block */
	uint16_t	atomic_wupf;		/* guarante during power fail*/
						/* log blocks, 0 = 1 block */
	uint8_t		nvscc;			/* convention for vendor */
						/* specific NVM commands */
	uint8_t		reserved531;
	uint16_t	atomic_cwu;		/* atomic comp+write guarantee*/
	uint16_t	reserved534;
	uint32_t	sgls;			/* SGL support */
	uint8_t		reserved540[164];

	/*
	 * I/O Command Set Attributes (offset 704)
	 */
	uint8_t		reserved704[1344];

	/*
	 * Power State Descriptors (offset 2048)
	 */
	nvme_pwstate_data_t pwrdescs[32];

	/*
	 * Vendor Specific (offset 3072)
	 */
	uint8_t		vendor3072[1024];
} __packed nvme_ident_ctlr_data_t;

/*
 * Controller list format
 */
typedef struct {
	uint16_t	idcount;	/* 2047 max */
	uint16_t	ctlrids[2047];	/* N controller ids */
} __packed nvme_ident_ctlr_list_t;

typedef struct {
	uint32_t	nsids[1024];	/* N namespace ids */
} __packed nvme_ident_ns_list_t;

#define NVME_CMIC_80		0x80
#define NVME_CMIC_40		0x40
#define NVME_CMIC_20		0x20
#define NVME_CMIC_10		0x10
#define NVME_CMIC_08		0x08
#define NVME_CMIC_CTLR_VIRTUAL	0x04	/* controller assoc w/virtual funct */
#define NVME_CMIC_MULTI_CTLR	0x02	/* subsystem has 2+ controllers */
#define NVME_CMIC_MULTI_PCI	0x01	/* subsystem has 2+ PCIe ports */

#define NVME_ASYNC_NSATTRCHG	0x00000100U /* supports ns attr chg event */

#define NVME_ADMIN_NSMANAGE	0x0008U	/* admin_cap */
#define NVME_ADMIN_FWIMG	0x0004U
#define NVME_ADMIN_FORMAT	0x0002U
#define NVME_ADMIN_SECURITY	0x0001U

#define NVME_FWUPD_FWACT_NORST	0x10	/* fw activation without reset */
#define NVME_FWUPD_SLTCNT_MASK	0x0E	/* fw slots */
#define NVME_FWUPD_SLOT1RO	0x01	/* slot 1 (first slot) is RO */

#define NVME_FWUPD_SLTCNT_GET(data)	\
		(((data) & NVME_FWUPD_SLTCNT_MASK) >> 1)

#define NVME_LOGPG_ATTR_CMDEFF	0x02	/* supports command effects log pg */
#define NVME_LOGPG_ATTR_SMART	0x01	/* supports SMART log pg */

#define NVME_AVSCC_ALLUSEFIG13	0x01	/* all use standard format (fig13) */

#define NVME_APSTA_SUPPORTED	0x01	/* supports autonomous transitions */

#define NVME_REPLAY_AUTH_MASK	0x0038
#define NVME_REPLAY_RPMB_MASK	0x0007

#define NVME_REPLAY_RPMB_GET(data)	\
		((data) & NVME_REPLAY_RPMB_MASK)
#define NVME_REPLAY_AUTH_GET(data)	\
		(((data) & NVME_REPLAY_AUTH_MASK) >> 3)

#define NVME_REPLY_AUTH_HMAC_SHA256	0
					/* 1-7 reserved */

/*
 * Defines for NVM Command set Attributes
 */
#define NVME_QENTRY_MAX_MASK	0xF0	/* subq_entry_size & comq_entry_size */
#define NVME_QENTRY_REQ_MASK	0x0F	/* subq_entry_size & comq_entry_size */

#define NVME_QENTRY_MAX_GET(data)	\
		(1 << (((data) & NVME_QENTRY_MAX_MASK) >> 4))
#define NVME_QENTRY_REQ_GET(data)	\
		(1 << ((data) & NVME_QENTRY_REQ_MASK))

#define NVME_NVMCAP_RESERVATIONS	0x0020
#define NVME_NVMCAP_SETFEATSAVE		0x0010
#define NVME_NVMCAP_WRITEZEROS		0x0008
#define NVME_NVMCAP_DSETMGMT		0x0004
#define NVME_NVMCAP_WRITEUNCORR		0x0002
#define NVME_NVMCAP_COMPARE		0x0001

#define NVME_FUSECAP_CMPWRITE		0x0001

#define NVME_FORMATCAP_CRYPTO		0x04	/* supported as part of secure*/
						/* erase functionality */
#define NVME_FORMATCAP_CRYPTOALLNS	0x02	/* crypto erase to all ns's */
#define NVME_FORMATCAP_FMTALLNS		0x01	/* fmt applies to all ns's */

#define NVME_VWC_PRESENT		0x01

#define NVME_SGLS_EXCESS_SUPP		0x00040000U
#define NVME_SGLS_META_BYTE_CONTIG_SUPP	0x00020000U
#define NVME_SGLS_BITBUCKET_SUPP	0x00010000U
#define NVME_SGLS_NVM_SUPP		0x00000001U

/*
 * For lba_fmt[] field below.
 *
 * NOTE: If protection is enabled, the first or last 8 bytes of the meta-data
 *	 holds the protection information (in-band with the meta-data).
 */
typedef struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	ms;		/* meta-dta bytes per lba */
	uint8_t		sect_size;	/* sector size 1 << n */
	uint8_t		flags;
#else
	uint8_t		flags;
	uint8_t		lbads;		/* sector size 1 << n */
	uint16_t	ms;		/* meta-dta bytes per lba */
#endif
} __packed nvme_lba_fmt_data_t;

/* flags field */
#define NVME_LBAFMT_PERF_MASK		0x03
#define NVME_LBAFMT_PERF_BEST		0x00
#define NVME_LBAFMT_PERF_BETTER		0x01
#define NVME_LBAFMT_PERF_GOOD		0x02
#define NVME_LBAFMT_PERF_DEGRADED	0x03

/*
 * NVME Identify Namespace data response structures (4096 bytes)
 */
typedef struct {
	uint64_t	size;		/* in logical blocks */
	uint64_t	capacity;	/* in logical blocks (for thin prov) */
	uint64_t	util;		/* in logical blocks */
	uint8_t		features;
	uint8_t		nlbaf;		/* #lba formats avail */
	uint8_t		flbas;		/* lba fmt used by current ns */
	uint8_t		mc;		/* meta-data capabilities */
	uint8_t		dpc;		/* data-protection caps */
	uint8_t		dps;		/* data-protection type settings */
	uint8_t		nmic;		/* (opt) mpath I/O / ns sharing */
	uint8_t		res_cap;	/* (opt) reservation capabilities */
	uint8_t		fmt_progress;	/* (opt) format progress */
	uint8_t		reserved33;
	uint16_t	natomic_wun;	/* (opt) atomic overrides for ns */
	uint16_t	natomic_wupf;	/* (opt) atomic overrides for ns */
	uint16_t	natomic_cwu;	/* (opt) atomic overrides for ns */
	uint16_t	natomic_bsn;	/* (opt) atomic overrides for ns */
	uint16_t	natomic_bo;	/* (opt) atomic overrides for ns */
	uint16_t	natomic_bspf;	/* (opt) atomic overrides for ns */
	uint16_t	reserved46;
	uint64_t	nvm_capacity[2];/* (opt) total size in bytes of the */
					/* nvm allocated to this namespace */
	uint8_t		reserved64[40];
	uint64_t	nguid[2];	/* ns global uuid */
	uint64_t	eui64;		/* ieee ext unique id */
	nvme_lba_fmt_data_t lba_fmt[16];/* format 0 [0] is mandatory */
	uint8_t		reserved192[192];
	uint8_t		reserved384[3712];
} __packed nvme_ident_ns_data_t;

#define NVME_NSFEAT_DEALLOC		0x04	/* support deallocation */
#define NVME_NSFEAT_NATOMICS		0x02	/* use NAWUN, NAWUPF, NACWU */
#define NVME_NSFEAT_THIN		0x01	/* thin provisioning avail */

#define NVME_FLBAS_META_INLINE		0x10	/* inline w/data, 0=separate */
#define NVME_FLBAS_SEL_MASK		0x0F
#define NVME_FLBAS_SEL_GET(data)	\
		((data) & NVME_FLBAS_SEL_MASK)

#define NVME_MC_INLINE			0x02	/* can xfer meta inline */
#define NVME_MC_EXTLBA			0x01	/* can xfer meta separately */


#define NVME_DPC_META_L8		0x10
#define NVME_DPC_META_F8		0x08
#define NVME_DPC_TYPE3			0x04
#define NVME_DPC_TYPE2			0x02
#define NVME_DPC_TYPE1			0x01

#define NVME_DPS_PROT_MASK		0x07

#define NVME_DPS_PROT_GET(data)		\
		((data) & NVME_DPS_PROT_MASK)

#define NVME_DPS_PROT_NONE		0
#define NVME_DPS_PROT_TYPE1		1
#define NVME_DPS_PROT_TYPE2		2
#define NVME_DPS_PROT_TYPE3		3
					/* 4-7 reserved */

#define NVME_NMIC_SHARED		0x01
