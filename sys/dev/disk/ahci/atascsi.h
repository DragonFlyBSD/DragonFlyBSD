/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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
 * $OpenBSD: atascsi.h,v 1.33 2009/02/16 21:19:06 miod Exp $
 */

struct atascsi;
struct scsi_link;

/*
 * ATA commands
 */

#define ATA_C_DATA_SET_MANAGEMENT 0x06 /* Data Set Management command */
#define ATA_C_SATA_FEATURE_ENA	0x10
#define ATA_C_READDMA_EXT	0x25
#define ATA_C_READ_LOG_EXT	0x2f
#define ATA_C_WRITEDMA_EXT	0x35
#define ATA_C_READ_FPDMA	0x60
#define ATA_C_WRITE_FPDMA	0x61
#define ATA_C_SATA_FEATURE_DIS	0x90
#define ATA_C_PACKET		0xa0
#define ATA_C_ATAPI_IDENTIFY	0xa1
#define ATA_C_READDMA		0xc8
#define ATA_C_WRITEDMA		0xca
#define ATA_C_READ_PM		0xe4
#define ATA_C_WRITE_PM		0xe8
#define ATA_C_FLUSH_CACHE	0xe7
#define ATA_C_FLUSH_CACHE_EXT	0xea /* lba48 */
#define ATA_C_IDENTIFY		0xec
#define ATA_C_SET_FEATURES	0xef
#define ATA_C_SEC_FREEZE_LOCK	0xf5

/*
 * ATA SATA FEATURES subcommands
 */
#define ATA_SATAFT_NONZDMA	0x01	/* DMA non-zero buffer offset */
#define ATA_SATAFT_DMAAAOPT	0x02	/* DMA AA optimization */
#define ATA_SATAFT_DEVIPS	0x03	/* Device-initiated pwr state*/
#define ATA_SATAFT_INORDER	0x04	/* in-order data delivery */
#define ATA_SATAFT_ASYNCNOTIFY	0x05	/* Async notification */

/*
 * ATA SET FEATURES subcommands
 */
#define ATA_SF_DSM_TRIM          0x01 /* TRIM DSM feature */
#define ATA_SF_WRITECACHE_EN	0x02
#define ATA_SF_SETXFER		0x03
#define ATA_SF_LOOKAHEAD_EN	0xaa

struct ata_identify {
	u_int16_t	config;		/*   0 */
	u_int16_t	ncyls;		/*   1 */
	u_int16_t	reserved1;	/*   2 */
	u_int16_t	nheads;		/*   3 */
	u_int16_t	track_size;	/*   4 */
	u_int16_t	sector_size;	/*   5 */
	u_int16_t	nsectors;	/*   6 */
	u_int16_t	reserved2[3];	/*   7 vendor unique */
	u_int8_t	serial[20];	/*  10 */
	u_int16_t	buffer_type;	/*  20 */
	u_int16_t	buffer_size;	/*  21 */
	u_int16_t	ecc;		/*  22 */
	u_int8_t	firmware[8];	/*  23 */
	u_int8_t	model[40];	/*  27 */
	u_int16_t	multi;		/*  47 */
	u_int16_t	dwcap;		/*  48 */
	u_int16_t	cap;		/*  49 */
	u_int16_t	reserved3;	/*  50 */
	u_int16_t	piomode;	/*  51 */
	u_int16_t	dmamode;	/*  52 */
	u_int16_t	validinfo;	/*  53 */
	u_int16_t	curcyls;	/*  54 */
	u_int16_t	curheads;	/*  55 */
	u_int16_t	cursectrk;	/*  56 */
	u_int16_t	curseccp[2];	/*  57 */
	u_int16_t	mult2;		/*  59 */
	u_int16_t	addrsec[2];	/*  60 */
	u_int16_t	worddma;	/*  62 */
	u_int16_t	dworddma;	/*  63 */
	u_int16_t	advpiomode;	/*  64 */
	u_int16_t	minmwdma;	/*  65 */
	u_int16_t	recmwdma;	/*  66 */
	u_int16_t	minpio;		/*  67 */
	u_int16_t	minpioflow;	/*  68 */
	u_int16_t       support3;	/*  69 */
#define ATA_SUPPORT_RZAT                0x0020
#define ATA_SUPPORT_DRAT                0x4000
	u_int16_t	reserved4;	/*  70 */
	u_int16_t	typtime[2];	/*  71 */
	u_int16_t	reserved5[2];	/*  73 */
	u_int16_t	qdepth;		/*  75 */
	u_int16_t	satacap;	/*  76 */
	u_int16_t	satacap2;	/*  77 */
#define SATA_CAP2_SNDRCV_FPDMA		(1 << 6)
	u_int16_t	satafsup;	/*  78 */
	u_int16_t	satafen;	/*  79 */
	u_int16_t	majver;		/*  80 */
	u_int16_t	minver;		/*  81 */
	u_int16_t	cmdset82;	/*  82 */
	u_int16_t	cmdset83;	/*  83 */
	u_int16_t	cmdset84;	/*  84 */
	u_int16_t	features85;	/*  85 */
	u_int16_t	features86;	/*  86 */
	u_int16_t	features87;	/*  87 */
#define ATA_ID_F87_WWN		(1<<8)
	u_int16_t	ultradma;	/*  88 */
	u_int16_t	erasetime;	/*  89 */
	u_int16_t	erasetimex;	/*  90 */
	u_int16_t	apm;		/*  91 */
	u_int16_t	masterpw;	/*  92 */
	u_int16_t	hwreset;	/*  93 */
	u_int16_t	acoustic;	/*  94 */
	u_int16_t	stream_min;	/*  95 */
	u_int16_t	stream_xfer_d;	/*  96 */
	u_int16_t	stream_lat;	/*  97 */
	u_int16_t	streamperf[2];	/*  98 */
	u_int16_t	addrsecxt[4];	/* 100 */
	u_int16_t	stream_xfer_p;	/* 104 */
	u_int16_t	max_dsm_blocks;	/* 105 */
	u_int16_t	phys_sect_sz;	/* 106 */
	u_int16_t	seek_delay;	/* 107 */
	u_int16_t	naa_ieee_oui;	/* 108 */
	u_int16_t	ieee_oui_uid;	/* 109 */
	u_int16_t	uid_mid;	/* 110 */
	u_int16_t	uid_low;	/* 111 */
	u_int16_t	resv_wwn[4];	/* 112 */
	u_int16_t	incits;		/* 116 */
	u_int16_t	words_lsec[2];	/* 117 */
	u_int16_t	cmdset119;	/* 119 */
	u_int16_t	features120;	/* 120 */
	u_int16_t	padding2[6];
	u_int16_t	rmsn;		/* 127 */
	u_int16_t	securestatus;	/* 128 */
#define ATA_SECURE_LOCKED		(1<<2)
#define ATA_SECURE_FROZEN		(1<<3)
	u_int16_t	vendor[31];	/* 129 */
	u_int16_t	padding3[9];	/* 160 */
	u_int16_t	support_dsm;	/* 169 */	
#define ATA_SUPPORT_DSM_TRIM            0x0001
	u_int16_t	padding5[6];	/* 170 */
	u_int16_t	curmedser[30];	/* 176 */
	u_int16_t	sctsupport;	/* 206 */
	u_int16_t	padding4[10];	/* 207 */
	u_int16_t	nomrota_rate;	/* 217 */
	u_int16_t	padding6[37];	/* 218 */
	u_int16_t	integrity;	/* 255 */
} __packed;

/*
 * IDENTIFY DEVICE data
 */
#define ATA_IDENTIFY_SECURITY		(1 << 1)
#define ATA_IDENTIFY_WRITECACHE		(1 << 5)
#define ATA_IDENTIFY_LOOKAHEAD		(1 << 6)

/*
 * Frame Information Structures
 */

#define ATA_FIS_LENGTH		20

struct ata_fis_h2d {
	u_int8_t		type;
#define ATA_FIS_TYPE_H2D		0x27
	u_int8_t		flags;
#define ATA_H2D_FLAGS_CMD		(1<<7)
	u_int8_t		command;
	u_int8_t		features;
#define ATA_H2D_FEATURES_DMA		(1<<0)
#define ATA_H2D_FEATURES_DIR		(1<<2)
#define ATA_H2D_FEATURES_DIR_READ	(1<<2)
#define ATA_H2D_FEATURES_DIR_WRITE	(0<<2)

	u_int8_t		lba_low;
	u_int8_t		lba_mid;
	u_int8_t		lba_high;
	u_int8_t		device;
#define ATA_H2D_DEVICE_LBA		0x40

	u_int8_t		lba_low_exp;
	u_int8_t		lba_mid_exp;
	u_int8_t		lba_high_exp;
	u_int8_t		features_exp;

	u_int8_t		sector_count;
	u_int8_t		sector_count_exp;
	u_int8_t		reserved0;
	u_int8_t		control;
#define ATA_FIS_CONTROL_SRST	0x04
#define ATA_FIS_CONTROL_4BIT	0x08

	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		reserved4;
} __packed;

struct ata_fis_d2h {
	u_int8_t		type;
#define ATA_FIS_TYPE_D2H		0x34
	u_int8_t		flags;
#define ATA_D2H_FLAGS_INTR		(1<<6)
	u_int8_t		status;
	u_int8_t		error;

	u_int8_t		lba_low;
	u_int8_t		lba_mid;
	u_int8_t		lba_high;
	u_int8_t		device;

	u_int8_t		lba_low_exp;
	u_int8_t		lba_mid_exp;
	u_int8_t		lba_high_exp;
	u_int8_t		reserved0;

	u_int8_t		sector_count;
	u_int8_t		sector_count_exp;
	u_int8_t		reserved1;
	u_int8_t		reserved2;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		reserved5;
	u_int8_t		reserved6;
} __packed;

/*
 * SATA log page 10h -
 * looks like a D2H FIS, with errored tag number in first byte.
 */
struct ata_log_page_10h {
	struct ata_fis_d2h	err_regs;
#define ATA_LOG_10H_TYPE_NOTQUEUED	0x80
#define ATA_LOG_10H_TYPE_TAG_MASK	0x1f
	u_int8_t		reserved[256 - sizeof(struct ata_fis_d2h)];
	u_int8_t		vendor_specific[255];
	u_int8_t		checksum;
} __packed;

/*
 * SATA registers
 */

#define SATA_SStatus_DET		0x00f
#define SATA_SStatus_DET_NODEV		0x000
#define SATA_SStatus_DET_NOPHY		0x001
#define SATA_SStatus_DET_DEV		0x003
#define SATA_SStatus_DET_OFFLINE	0x008

#define SATA_SStatus_SPD		0x0f0
#define SATA_SStatus_SPD_NONE		0x000
#define SATA_SStatus_SPD_1_5		0x010
#define SATA_SStatus_SPD_3_0		0x020

#define SATA_SStatus_IPM		0xf00
#define SATA_SStatus_IPM_NODEV		0x000
#define SATA_SStatus_IPM_ACTIVE		0x100
#define SATA_SStatus_IPM_PARTIAL	0x200
#define SATA_SStatus_IPM_SLUMBER	0x600

#define SATA_SIGNATURE_PORT_MULTIPLIER	0x96690101
#define SATA_SIGNATURE_ATAPI		0xeb140101
#define SATA_SIGNATURE_DISK		0x00000101

/*
 * ATA interface
 */

struct ahci_port;

struct ata_port {
	struct ata_identify	at_identify;	/* only if ATA_PORT_T_DISK */
	struct ahci_port	*at_ahci_port;
	int			at_type;
#define ATA_PORT_T_NONE			0
#define ATA_PORT_T_DISK			1
#define ATA_PORT_T_ATAPI		2
#define ATA_PORT_T_PM			3
	int			at_features;
#define ATA_PORT_F_WCACHE		(1 << 0)
#define ATA_PORT_F_RAHEAD		(1 << 1)
#define ATA_PORT_F_FRZLCK		(1 << 2)
#define ATA_PORT_F_RESCAN		(1 << 3) /* re-check on bus scan */
	int			at_probe;
#define ATA_PROBE_NEED_INIT		0
#define ATA_PROBE_NEED_HARD_RESET	1
#define ATA_PROBE_NEED_SOFT_RESET	2
#define ATA_PROBE_NEED_IDENT		3
#define ATA_PROBE_GOOD			4
#define ATA_PROBE_FAILED		7
	int			at_ncqdepth;
	u_int64_t		at_capacity;	/* only if ATA_PORT_T_DISK */
	int			at_target;	/* port multiplier port */
	char			at_name[16];
};

struct ata_xfer {
	struct ata_fis_h2d	*fis;
	struct ata_fis_d2h	rfis;
	u_int8_t		*packetcmd;
	u_int8_t		tag;

	void			*data;
	size_t			datalen;
	size_t			resid;

	void			(*complete)(struct ata_xfer *);
	u_int			timeout;
	int			serial;		/* detect timeout races */

	int			flags;
#define ATA_F_READ			(1<<0)
#define ATA_F_WRITE			(1<<1)
#define ATA_F_NOWAIT			(1<<2)
#define ATA_F_POLL			(1<<3)
#define ATA_F_PIO			(1<<4)
#define ATA_F_PACKET			(1<<5)
#define ATA_F_NCQ			(1<<6)
#define ATA_F_TIMEOUT_RUNNING		(1<<7)
#define ATA_F_TIMEOUT_DESIRED		(1<<8)
#define ATA_F_TIMEOUT_EXPIRED		(1<<9)
#define ATA_F_AUTOSENSE			(1<<10)
#define ATA_F_EXCLUSIVE			(1<<11)
#define ATA_F_SILENT			(1<<12)
#define ATA_FMT_FLAGS			"\020" 				\
					"\015SILENT"			\
					"\014EXCLUSIVE"			\
					"\013AUTOSENSE"			\
					"\012EXPIRED"			\
					"\011DESIRED" "\010TRUNNING"	\
					"\007NCQ" "\006PACKET"		\
					"\005PIO" "\004POLL" "\003NOWAIT" \
					"\002WRITE" "\001READ"

	volatile int		state;
#define ATA_S_SETUP			0
#define ATA_S_PENDING			1
#define ATA_S_COMPLETE			2
#define ATA_S_ERROR			3
#define ATA_S_TIMEOUT			4
#define ATA_S_ONCHIP			5
#define ATA_S_PUT			6

	void			*atascsi_private;
	struct ata_port         *at;	/* NULL if direct-attached */
};
