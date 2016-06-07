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

/*
 * Misc limits
 */
#define NVME_MAX_ADMIN_BUFFER	4096U

/*
 * NVME chipset register and structural definitions
 *
 * NOTE! Firmware related commands and responses are in nvme_fw.h
 */
#define NVME_REG_CAP		0x0000	/* Capabilities */
#define NVME_REG_VERS		0x0008	/* Version */
#define NVME_REG_INTSET		0x000C	/* Set interrupt mask bits */
#define NVME_REG_INTCLR		0x0010	/* Clr interrupt mask bits */
#define NVME_REG_CONFIG		0x0014	/* Configuration */
#define NVME_REG_RESERVED_18	0x0018
#define NVME_REG_STATUS		0x001C
#define NVME_REG_SUBRESET	0x0020
#define NVME_REG_ADM_ATTR	0x0024
#define NVME_REG_ADM_SUBADR	0x0028
#define NVME_REG_ADM_COMADR	0x0030
#define NVME_REG_MEMBUF		0x0038
#define NVME_REG_MEMSIZE	0x003C
#define NVME_REG_RESERVED_40	0x0040
#define NVME_REG_CSS		0x0F00	/* Command-set specific area */

/*
 * Doorbell area for queues.  Queue 0 is the admin queue.  The number of
 * queues supported is specified in the capabilities.
 */
#define NVME_REG_SUBQ_BELL(n, dstrd4)	(0x1000 + ((n) * 2 + 0) * (dstrd4))
#define NVME_REG_COMQ_BELL(n, dstrd4)	(0x1000 + ((n) * 2 + 1) * (dstrd4))

/*
 * NVME_REG_CAP		- Capabilities (64 bits)
 *
 * DSTRD- Doorbell stride (0=4 bytes, in multiples of 4 bytes)
 * AMS  - Arbitration mechanisms supported.  WRRUP means weighted round robin
 *	  with urgent priority feature.
 * CQR  - Indicates that submission and completion queues must be physically
 *	  contiguous.
 * MQES	- Maximum queue entries (0 means a maximum of 1, 1 is 2, etc)
 */
#define NVME_CAP_CSS_63		(0x8000000000000000LLU)
#define NVME_CAP_CSS_62		(0x4000000000000000LLU)
#define NVME_CAP_CSS_61		(0x2000000000000000LLU)
#define NVME_CAP_CSS_60		(0x1000000000000000LLU)
#define NVME_CAP_CSS_59		(0x0800000000000000LLU)
#define NVME_CAP_CSS_58		(0x0400000000000000LLU)
#define NVME_CAP_CSS_57		(0x0200000000000000LLU)
#define NVME_CAP_CSS_56		(0x0100000000000000LLU)
#define NVME_CAP_MEMPG_MAX	(0x00F0000000000000LLU)
#define NVME_CAP_MEMPG_MIN	(0x000F000000000000LLU)
#define NVME_CAP_RESERVED_47	(0x0000800000000000LLU)
#define NVME_CAP_RESERVED_46	(0x0000400000000000LLU)
#define NVME_CAP_RESERVED_45	(0x0000200000000000LLU)
#define NVME_CAP_CSS_44		(0x0000100000000000LLU)
#define NVME_CAP_CSS_43		(0x0000080000000000LLU)
#define NVME_CAP_CSS_42		(0x0000040000000000LLU)
#define NVME_CAP_CSS_41		(0x0000020000000000LLU)
#define NVME_CAP_CSS_40		(0x0000010000000000LLU)
#define NVME_CAP_CSS_39		(0x0000008000000000LLU)
#define NVME_CAP_CSS_38		(0x0000004000000000LLU)
#define NVME_CAP_CSS_NVM	(0x0000002000000000LLU)
#define NVME_CAP_SUBRESET	(0x0000001000000000LLU)
#define NVME_CAP_DSTRD_MASK	(0x0000000F00000000LLU)
#define NVME_CAP_TIMEOUT	(0x00000000FF000000LLU)
#define NVME_CAP_RESERVED_19	(0x0000000000F80000LLU)
#define NVME_CAP_AMS_VENDOR	(0x0000000000040000LLU)
#define NVME_CAP_AMS_WRRUP	(0x0000000000020000LLU)
#define NVME_CAP_CQR		(0x0000000000010000LLU)
#define NVME_CAP_MQES		(0x000000000000FFFFLLU)

#define NVME_CAP_MEMPG_MAX_GET(data)	\
		(1 << (12 + (((data) & NVME_CAP_MEMPG_MAX) >> 52)))
#define NVME_CAP_MEMPG_MIN_GET(data)	\
		(1 << (12 + (((data) & NVME_CAP_MEMPG_MIN) >> 48)))
#define NVME_CAP_DSTRD_GET(data)	\
		(4 << ((((data) & NVME_CAP_DSTRD_MASK) >> 32)))
#define NVME_CAP_TIMEOUT_GET(data)	\
		(((data) & NVME_CAP_TIMEOUT) >> 24)	/* 500ms incr */
#define NVME_CAP_MQES_GET(data)		\
		(((data) & NVME_CAP_MQES) + 1)		/* 0-based 0=1, min 2 */

/*
 * NVME_REG_VERS	- Version register (32 bits)
 *
 * Just extract and shift the fields, 1=1, e.g. '1.2' has MAJOR=1, MINOR=2
 */
#define NVME_VERS_MAJOR		(0xFFFF0000U)
#define NVME_VERS_MINOR		(0x0000FF00U)
#define NVME_VERS_RESERVED_00	(0x00000000U)

#define NVME_VERS_MAJOR_GET(data)	\
		(((data) & NVME_VERS_MAJOR) >> 16)
#define NVME_VERS_MINOR_GET(data)	\
		(((data) & NVME_VERS_MINOR) >> 8)

/*
 * NVME_REG_INTSET	(32 bits)
 * NVME_REG_INTCLR	(32 bits)
 *
 * Set and clear interrupt mask bits (up to 32 interrupt sources).
 * Writing a 1 to the appropriate bits in the appropriate register sets
 * or clears that interrupt source.  Writing a 0 has no effect.  Reading
 * either register returns the current interrupt mask.
 *
 * Setting an interrupt mask bit via INTSET masks the interrupt so it
 * cannot occur.
 */

/*
 * NVME_REG_CONFIG	(32 bits)
 *
 * Controller configuration, set by the host prior to enabling the
 * controller.
 *
 * IOCOM_ES	- I/O completion queue entry size, 1<<n
 * IOSUB_ES	- I/O submission queue entry size, 1<<n
 * SHUT*	- Set while controller enabled to indicate shutdown pending.
 *
 * ENABLE (EN):
 *	Works as expected.  On the 1->0 transition the controller state
 *	is reset except for PCI registers and the Admin Queue registers.
 *	When clearing to 0, poll the CSTS RDY bit until it becomes 0.
 *	Similarly, when enabling EN, poll CSTS RDY until it becomes 1.
 */
#define NVME_CONFIG_RESERVED_24	0xFF000000U
#define NVME_CONFIG_IOCOM_ES	0x00F00000U
#define NVME_CONFIG_IOSUB_ES	0x000F0000U

#define NVME_CONFIG_IOCOM_ES_SET(pwr)	((pwr) << 20)
#define NVME_CONFIG_IOSUB_ES_SET(pwr)	((pwr) << 16)

#define NVME_CONFIG_SHUT_MASK	0x0000C000U
#define NVME_CONFIG_SHUT_NONE	0x00000000U
#define NVME_CONFIG_SHUT_NORM	0x00004000U	/* normal shutdown */
#define NVME_CONFIG_SHUT_EMERG	0x00008000U	/* emergency shutdown */
#define NVME_CONFIG_SHUT_RES3	0x0000C000U

#define NVME_CONFIG_AMS_DEF	0x00000000U	/* default (round-robin) */
#define NVME_CONFIG_AMS_WRRUP	0x00000800U	/* weighted round-robin */
#define NVME_CONFIG_AMS_2	0x00001000U
#define NVME_CONFIG_AMS_3	0x00001800U
#define NVME_CONFIG_AMS_4	0x00002000U
#define NVME_CONFIG_AMS_5	0x00002800U
#define NVME_CONFIG_AMS_6	0x00003000U
#define NVME_CONFIG_AMS_VENDOR	0x00003800U

#define NVME_CONFIG_MEMPG	0x00000780U	/* MPS register */
#define NVME_CONFIG_CSS_MASK	0x00000070U
#define NVME_CONFIG_3		0x00000008U
#define NVME_CONFIG_2		0x00000004U
#define NVME_CONFIG_1		0x00000002U
#define NVME_CONFIG_EN		0x00000001U

#define NVME_CONFIG_CSS_NVM	(0U << 4)	/* NVM command set */
#define NVME_CONFIG_CSS_1	(1U << 4)	/* these are reserved */
#define NVME_CONFIG_CSS_2	(2U << 4)
#define NVME_CONFIG_CSS_3	(3U << 4)
#define NVME_CONFIG_CSS_4	(4U << 4)
#define NVME_CONFIG_CSS_5	(5U << 4)
#define NVME_CONFIG_CSS_6	(6U << 4)
#define NVME_CONFIG_CSS_7	(7U << 4)

#define NVME_CONFIG_MEMPG_SET(pwr)	\
		(uint32_t)(((pwr) - 12) << 7)


/*
 * NVME_REG_STATUS	(32 bits)
 *
 * PAUSED	- Set to 1 if the controller is paused, 0 if normal operation.
 * SUBRESET	- Set to 1 if a subsystem reset occurred (if available).
 * SHUT*	- Shutdown state for poller
 * FATAL	- Indicates a fatal controller error ocurred.
 * RDY		- Controller ready/disable response to ENABLE.
 */
#define NVME_STATUS_RESERVED	0xFFFFFFC0U
#define NVME_STATUS_PAUSED	0x00000020U
#define NVME_STATUS_SUBRESET	0x00000010U
#define NVME_STATUS_SHUT_MASK	0x0000000CU
#define NVME_STATUS_FATAL	0x00000002U
#define NVME_STATUS_RDY		0x00000001U

#define NVME_STATUS_SHUT_NORM	0x00000000U
#define NVME_STATUS_SHUT_INPROG	0x00000004U
#define NVME_STATUS_SHUT_DONE	0x00000008U
#define NVME_STATUS_SHUT_11	0x0000000CU

/*
 * NVME_REG_SUBRESET
 *
 * Allows for the initiation of a subsystem reset, if available (see caps).
 * Writing the value 0x4E565D65 ('NVMe') initiates the reset.
 */

/*
 * NVME_REG_ADM_ATTR
 *
 * Specifies the completion and submission queue size in #entries.  Values
 * between 2 and 4096 are valid.  COM and SUB are a 0's based value (0=1).
 */
#define NVME_ATTR_RESERVED_31	0x80000000U
#define NVME_ATTR_RESERVED_30	0x40000000U
#define NVME_ATTR_RESERVED_29	0x20000000U
#define NVME_ATTR_RESERVED_28	0x10000000U
#define NVME_ATTR_COM		0x0FFF0000U
#define NVME_ATTR_RESERVED_15	0x00008000U
#define NVME_ATTR_RESERVED_14	0x00004000U
#define NVME_ATTR_RESERVED_13	0x00002000U
#define NVME_ATTR_RESERVED_12	0x00001000U
#define NVME_ATTR_SUB		0x00000FFFU

#define NVME_ATTR_COM_SET(nqe)	(((nqe - 1) << 16) & NVME_ATTR_COM)
#define NVME_ATTR_SUB_SET(nqe)	((nqe - 1) & NVME_ATTR_SUB)

/*
 * NVME_REG_ADM_SUBADR (64 bits)
 * NVME_REG_ADM_COMADR (64 bits)
 *
 * Specify the admin submission and completion queue physical base
 * address.  These are 64-bit addresses, 4K aligned.  Bits 11:00 must
 * be 0.
 */

/*
 * NVME_REG_MEMBUF	(RO, 32 bits)
 * NVME_REG_MEMSIZE	(RO, 32 bits)
 *
 * These are optional registers which specify the location and extent
 * of the controller memory buffer.  The offset is specified in multipleps
 * of CMBSZ and must be 4K aligned.  The BIR tells us which BAR controls
 * MEMBUF/MEMSIZE.
 *
 * The SIZE field in MEMSIZE is in multiple of the UNITS field.
 *
 * WDS - If set to 1, data and meta-data for commands may be placed in
 *	 the memory buffer.
 * RDS - (same)
 * LISTS - PRP and SGL lists can be in controller memory.
 * CQS - completion queues can be in controller memory.
 * SQS - submission queues can be in controller memory.
 *
 * Implementation note: We can ignore this entire register.  We will always
 * use host memory for data and meta-data transfers.
 */
#define NVME_MEMBUF_OFFSET	0xFFFFF000U	/* 0, 2, 3, 4, or 5 only */
#define NVME_MEMBUF_BAR		0x00000007U	/* 0, 2, 3, 4, or 5 only */

#define NVME_MEMSIZE_SIZE	0xFFFFF000U
#define NVME_MEMSIZE_UNITS_MASK	0x00000F00U
#define NVME_MEMSIZE_UNITS_4K	0x00000000U
#define NVME_MEMSIZE_UNITS_64K	0x00000100U
#define NVME_MEMSIZE_UNITS_1M	0x00000200U
#define NVME_MEMSIZE_UNITS_16M	0x00000300U
#define NVME_MEMSIZE_UNITS_256M	0x00000400U
#define NVME_MEMSIZE_UNITS_4G	0x00000500U
#define NVME_MEMSIZE_UNITS_64G	0x00000600U
				/* 7-F are reserved */

#define NVME_MEMSIZE_WDS	0x00000010U
#define NVME_MEMSIZE_RDS	0x00000008U
#define NVME_MEMSIZE_LISTS	0x00000004U
#define NVME_MEMSIZE_CQS	0x00000002U
#define NVME_MEMSIZE_SQS	0x00000001U

/*
 * NVME_REG_SUBQ*_BELL		Submission queue doorbell register (WO)
 * NVME_REG_COMQ*_BELL		Completion queue doorbell register (WO)
 *
 * The low 16 bits specify the index of the submission queue tail or
 * completion queue head.  Only write to this register, do not read.
 * Writing to the register passes control of the related command block(s)
 * to/from the controller.  For example, if the submission queue is
 * at index 4 and empty the host can setup N command blocks and then
 * doorbell 4+N.  Each command block does not need to be independently
 * doorbelled.  The doorbell is managing a circular queue.
 *
 * NOTE: A full queue is N-1 entries.  The last entry cannot be in-play
 *	 to distinguish between an empty queue and a full queue.
 */
#define NVME_BELL_MASK		0x0000FFFFU

/*
 * Submission Queue Entry Header (40 bytes, full entry is 64 bytes)
 *
 * This is just the header for the entry, see the opcode section later
 * on for the full entry structure (which we will define as a union).
 *
 * NOTE: prp1/prp2 format depends on config and use cases also depend on
 *	 the command.
 *
 *	 PRP - Basically a 4-byte-aligned 64-bit address.  The first PRP
 *	       can offset into a page, subsequent PRPs must be page-aligned.
 *	       If pointing to a PRP list, must be 8-byte aligned and each
 *	       PRP in the PRP list must be page-aligned.
 *
 * NOTE: For optional admin and nvm vendor specific commands
 *
 * NOTE: PRP data target vs PRP to PRP list.  Typically prp1 represents
 *	 the starting point in the target data space and prp2, if needed,
 *	 is a PRP list entry.  In our driver implementation we limit the
 *	 data transfer size such that we do not have to chain PRP lists.
 *	 That is, 4096 / 8 = 512 x 4K pages (2MB).  So 2MB is the maximum
 *	 transfer size we will support.
 */
typedef struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t	opcode;
	uint8_t	flags;
	uint16_t cid;
#else
	uint16_t cid;
	uint8_t	flags;
	uint8_t	opcode;
#endif
	uint32_t nsid;		/* namespace id. 0=not used, -1=apply all */
	uint32_t dw2;
	uint32_t dw3;
	uint64_t mptr;
	uint64_t prp1;		/* prp or sgl depending on config */
	uint64_t prp2;		/* prp or sgl depending on config */
} __packed nvme_subq_head_t;

/*
 * NOTE: SGL1 - buffer can be byte aligned
 *	 SGL2 - buffer containing single SGL desc must be 8-byte aligned
 */
#define NVME_SUBQFLG_PRP	0x00
#define NVME_SUBQFLG_SGL1	0x40	/* MPTR -> single contig buffer */
#define NVME_SUBQFLG_SGL2	0x80	/* MPTR -> &SGL seg w/one SGL desc */
#define NVME_SUBQFLG_RESERVEDS	0xC0

#define NVME_SUBQFLG_NORM	0x00
#define NVME_SUBQFLG_FUSED0	0x01
#define NVME_SUBQFLG_FUSED1	0x02
#define NVME_SUBQFLG_RESERVEDF	0x03

/*
 * Submission queue full generic entry (64 bytes)
 *
 * NOTE: nvme_subq_item_t is used as part of the nvme_allcmd_t union later
 *	 on.  Do not use the generic item structure to construct nominal
 *	 commands.
 */
typedef struct {
	nvme_subq_head_t head;
	/*
	 * command specific fields
	 */
	union {
		uint32_t dw10;
		uint32_t ndt;	/* number of dwords in data xfer */
	};
	union {
		uint32_t dw11;
		uint32_t ndm;	/* number of dwords in meta-data xfer */
	};
	uint32_t dw12;
	uint32_t dw13;
	uint32_t dw14;
	uint32_t dw15;
} __packed nvme_subq_item_t;

/*
 * Completion Queue Entry Trailer (8 bytes, full entry is 16 bytes)
 */
typedef struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	subq_head_ptr;
	uint16_t	subq_id;
	uint16_t	cmd_id;
	uint16_t	status;
#else
	uint16_t	subq_id;
	uint16_t	subq_head_ptr;
	uint16_t	status;
	uint16_t	cmd_id;
#endif
} __packed nvme_comq_tail_t;

/*
 * Completion queue full generic entry (16 bytes)
 *
 * subq_head_ptr	- Current submission queue head pointer
 * subq_id		- Submission queue the command came from
 * cmd_id;		- Command identifier
 * status;		- Result status
 *
 * NOTE: nvme_comq_item_t is used as part of the nvme_allres_t union later
 *	 on.  Do not use the generic item structure to parse nominal
 *	 results.
 */
typedef struct {
	uint32_t	dw0;		/* command-specific status */
	uint32_t	dw1;		/* (typically reserved) */
	nvme_comq_tail_t tail;
} __packed nvme_comq_item_t;

#define NVME_COMQ_STATUS_DNR	0x8000U
#define NVME_COMQ_STATUS_MORE	0x4000U
#define NVME_COMQ_STATUS_29	0x2000U
#define NVME_COMQ_STATUS_28	0x1000U
#define NVME_COMQ_STATUS_TYPE	0x0E00U
#define NVME_COMQ_STATUS_CODE	0x01FEU
#define NVME_COMQ_STATUS_PHASE	0x0001U

#define NVME_COMQ_STATUS_TYPE_GET(data)	\
		(((data) & NVME_COMQ_STATUS_TYPE) >> 9)
#define NVME_COMQ_STATUS_CODE_GET(data)	\
		(((data) & NVME_COMQ_STATUS_CODE) >> 1)

#define NVME_STATUS_TYPE_GENERIC	0	/* generic status code */
#define NVME_STATUS_TYPE_SPECIFIC	1	/* opcode-specific code */
#define NVME_STATUS_TYPE_MEDIA		2	/* media & data errors */
#define NVME_STATUS_TYPE_3		3
#define NVME_STATUS_TYPE_4		4
#define NVME_STATUS_TYPE_5		5
#define NVME_STATUS_TYPE_6		6
#define NVME_STATUS_TYPE_VENDOR		7

/*
 * Generic status (NVME_STATUS_TYPE_GENERIC)
 *
 * Status codes 0x00-0x7F are applicable to the admin command set or
 * are generic across multiple command sets.
 *
 * Status codes 0x80-0xBF are applicable to the I/O command set.
 *
 * Status codes 0xC0-0xFF are vendor-specific
 */
#define NVME_CODE_SUCCESS		0x00
#define NVME_CODE_BADOP			0x01
#define NVME_CODE_BADFIELD		0x02
#define NVME_CODE_IDCONFLICT		0x03
#define NVME_CODE_BADXFER		0x04
#define NVME_CODE_ABORTED_PWRLOSS	0x05
#define NVME_CODE_INTERNAL		0x06
#define NVME_CODE_ABORTED_ONREQ		0x07
#define NVME_CODE_ABORTED_SQDEL		0x08
#define NVME_CODE_ABORTED_FUSEFAIL	0x09
#define NVME_CODE_ABORTED_FUSEMISSING	0x0A
#define NVME_CODE_BADNAMESPACE		0x0B
#define NVME_CODE_SEQERROR		0x0C
#define NVME_CODE_BADSGLSEG		0x0D
#define NVME_CODE_BADSGLCNT		0x0E
#define NVME_CODE_BADSGLLEN		0x0F
#define NVME_CODE_BADSGLMLEN		0x10
#define NVME_CODE_BADSGLTYPE		0x11
#define NVME_CODE_BADMEMBUFUSE		0x12
#define NVME_CODE_BADPRPOFF		0x13
#define NVME_CODE_ATOMICWUOVFL		0x14
					/* 15-7f reserved */

#define NVME_CODE_LBA_RANGE		0x80
#define NVME_CODE_CAP_EXCEEDED		0x81
#define NVME_CODE_NAM_NOT_READY		0x82
#define NVME_CODE_RSV_CONFLICT		0x83
#define NVME_CODE_FMT_IN_PROG		0x84
					/* 85-bf reserved */

/*
 * Command specific status codes (NVME_STATUS_TYPE_SPECIFIC)
 */
#define NVME_CSSCODE_BADCOMQ			0x00
#define NVME_CSSCODE_BADQID			0x01
#define NVME_CSSCODE_BADQSIZE			0x02
#define NVME_CSSCODE_ABORTLIM			0x03
#define NVME_CSSCODE_RESERVED04			0x04
#define NVME_CSSCODE_ASYNCEVENTLIM		0x05
#define NVME_CSSCODE_BADFWSLOT			0x06
#define NVME_CSSCODE_BADFWIMAGE			0x07
#define NVME_CSSCODE_BADINTRVECT		0x08
#define NVME_CSSCODE_BADLOGPAGE			0x09
#define NVME_CSSCODE_BADFORMAT			0x0A
#define NVME_CSSCODE_FW_NEEDSCONVRESET		0x0B
#define NVME_CSSCODE_BADQDELETE			0x0C
#define NVME_CSSCODE_FEAT_NOT_SAVEABLE		0x0D
#define NVME_CSSCODE_FEAT_NOT_CHGABLE		0x0E
#define NVME_CSSCODE_FEAT_NOT_NSSPEC		0x0F
#define NVME_CSSCODE_FW_NEEDSSUBRESET		0x10
#define NVME_CSSCODE_FW_NEEDSRESET		0x11
#define NVME_CSSCODE_FW_NEEDSMAXTVIOLATE	0x12
#define NVME_CSSCODE_FW_PROHIBITED		0x13
#define NVME_CSSCODE_RANGE_OVERLAP		0x14
#define NVME_CSSCODE_NAM_INSUFF_CAP		0x15
#define NVME_CSSCODE_NAM_ID_UNAVAIL		0x16
#define NVME_CSSCODE_RESERVED17			0x17
#define NVME_CSSCODE_NAM_ALREADY_ATT		0x18
#define NVME_CSSCODE_NAM_IS_PRIVATE		0x19
#define NVME_CSSCODE_NAM_NOT_ATT		0x1A
#define NVME_CSSCODE_NO_THIN_PROVISION		0x1B
#define NVME_CSSCODE_CTLR_LIST_INVALID		0x1C
						/* 1D-7F reserved */

#define NVME_CSSCODE_ATTR_CONFLICT		0x80
#define NVME_CSSCODE_BADPROTINFO		0x81
#define NVME_CSSCODE_WRITE_TO_RDONLY		0x82
					/* 83-BF reserved */

/*
 * Media and Data Integrity (NVME_STATUS_TYPE_MEDIA)
 */
#define NVME_MEDCODE_WRITE_FAULT		0x80
#define NVME_MEDCODE_UNRECOV_READ_ERROR		0x81
#define NVME_MEDCODE_ETOE_GUARD_CHK		0x82
#define NVME_MEDCODE_ETOE_APPTAG_CHK		0x83
#define NVME_MEDCODE_ETOE_REFTAG_CHK		0x84
#define NVME_MEDCODE_COMPARE_FAILURE		0x85
#define NVME_MEDCODE_ACCESS_DENIED		0x86
#define NVME_MEDCODE_UNALLOCATED		0x87
					/* 88-BF reserved */

/*
 * OPCODES:
 *	7:	1=IO Command set or vendor specific
 *	6:2	Function
 *	1	Data XFer R
 *	0	Data XFer W	(note: both R and W cannot be specified)
 *
 * Admin commands
 */
#define NVME_OP_DELETE_SUBQ	0x00
#define NVME_OP_CREATE_SUBQ	0x01
#define NVME_OP_GET_LOG_PAGE	0x02	/* (needs namid) */
#define NVME_OP_DELETE_COMQ	0x04
#define NVME_OP_CREATE_COMQ	0x05
#define NVME_OP_IDENTIFY	0x06	/* (needs namid) */
#define NVME_OP_ABORT		0x08
#define NVME_OP_SET_FEATURES	0x09	/* (needs namid) */
#define NVME_OP_GET_FEATURES	0x0A	/* (needs namid) */
#define NVME_OP_ASY_EVENT_REQ	0x0C
#define NVME_OP_NAM_MANAGE	0x0D	/* (optional, needs namid) */
#define NVME_OP_FW_COMMIT	0x10	/* (optional) */
#define NVME_OP_FW_DOWNLOAD	0x11	/* (optional) */
#define NVME_OP_NAM_ATTACH	0x15	/* (optional, needs namid) */

#define NVME_OP_FORMATNVM	0x80	/* (optional, needs namid) */
#define NVME_OP_SEC_SEND	0x81	/* (optional, needs namid) */
#define NVME_OP_SEC_RECV	0x82	/* (optional, needs namid) */

/*
 * Abort command
 *
 * Error status possible: NVM_CSSCODE_ABORTLIM
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	subq_id;	/* subq to abort */
	uint16_t	cmd_id;		/* cmdid to abort */
#else
	uint16_t	cmd_id;
	uint16_t	subq_id;
#endif
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_abort_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_abort_res_t;

/*
 * Asynchronous Event Request Command
 *
 * Error status possible: NVM_CSSCODE_ASYNCEVENTLIM
 *
 * NOTE: Should be posted to an independent queue, with no timeout.  Async
 *	 events are returned when they occur and so might not be returned
 *	 for a very long time (like hours, or longer).
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	reserved10;
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_async_cmd_t;

typedef struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t		type;
	uint8_t		info;
	uint8_t		lid;
	uint8_t		reserved03;
#else
	uint8_t		reserved03;
	uint8_t		lid;
	uint8_t		info;
	uint8_t		type;
#endif
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_async_res_t;

#define NVME_ASYNC_TYPE_MASK		0x07
#define NVME_ASYNC_TYPE_ERROR		0x00	/* error status */
#define NVME_ASYNC_TYPE_SMART		0x01	/* smart status */
#define NVME_ASYNC_TYPE_NOTICE		0x02
#define NVME_ASYNC_TYPE_RESERVED3	0x03
#define NVME_ASYNC_TYPE_RESERVED4	0x04
#define NVME_ASYNC_TYPE_RESERVED5	0x05
#define NVME_ASYNC_TYPE_CSS		0x06	/* cmd-specific status */
#define NVME_ASYNC_TYPE_VENDOR		0x07

/* TYPE_ERROR */
#define NVME_ASYNC_INFO_INVDOORBELL	0x00
#define NVME_ASYNC_INFO_INVDOORVALUE	0x01
#define NVME_ASYNC_INFO_DIAGFAIL	0x02
#define NVME_ASYNC_INFO_INTRNL_FATAL	0x03	/* persistent internal error */
#define NVME_ASYNC_INFO_INTRNL_TRANS	0x04	/* transient internal error */
#define NVME_ASYNC_INFO_FIRMLOADERR	0x05
					/* 06-FF reserved */

/* TYPE_SMART */
#define NVME_ASYNC_INFO_UNRELIABLE	0x00
#define NVME_ASYNC_INFO_TEMP_THR	0x01
#define NVME_ASYNC_INFO_SPARE_BELOW_THR	0x02
					/* 03-FF reserved */

/* TYPE_NOTICE */
#define NVME_ASYNC_INFO_NAM_ATTR_CHG	0x00
#define NVME_ASYNC_INFO_FW_ACT_START	0x01
					/* 02-FF reserved */

/* TYPE_CSS */
#define NVME_ASYNC_INFO_RES_LOGPG_AVAIL	0x00
					/* 01-FF reserved */

/*
 * Create I/O Completion Queue Command
 *
 * NOTE: PRP1 depends on the PC (physically contiguous) config bit.
 *	 If set (which we do), points to a single physically contiguous
 *	 array.
 *
 * NOTE: XXX IV specification associated with msix PCI space ?
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	comq_id;	/* create w/unique id */
	uint16_t	comq_size;	/* in entries */
	uint16_t	flags;
	uint16_t	ivect;		/* multiple MSI or MSI-X only */
#else
	uint16_t	comq_size;
	uint16_t	comq_id;
	uint16_t	ivect;
	uint16_t	flags;
#endif
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_createcomq_cmd_t;

#define NVME_CREATECOM_IEN	0x0002
#define NVME_CREATECOM_PC	0x0001

typedef struct {
	uint32_t	dw0;
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_createcomq_res_t;

/*
 * Create I/O Completion Queue Command
 *
 * NOTE: PRP1 depends on the PC (physically contiguous) config bit.
 *	 If set (which we do), points to a single physically contiguous
 *	 array.
 *
 * NOTE: XXX IV specification associated with msix PCI space ?
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	subq_id;	/* create w/unique id */
	uint16_t	subq_size;	/* in entries */
	uint16_t	flags;
	uint16_t	comq_id;	/* completion queue to use */
#else
	uint16_t	subq_size;
	uint16_t	subq_id;
	uint16_t	comq_id;
	uint16_t	flags;
#endif
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_createsubq_cmd_t;

#define NVME_CREATESUB_PRI	0x0006
#define NVME_CREATESUB_PRI_LOW	0x0006
#define NVME_CREATESUB_PRI_MED	0x0004
#define NVME_CREATESUB_PRI_HIG	0x0002
#define NVME_CREATESUB_PRI_URG	0x0000

#define NVME_CREATESUB_PC	0x0001

typedef struct {
	uint32_t	dw0;
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_createsubq_res_t;

/*
 * Delete I/O Completion Queue Command
 * Delete I/O Submission Queue Command
 *
 * Both commands use the same structures.
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	qid;		/* queue id to delete */
	uint16_t	reserved02;
#else
	uint16_t	reserved02;
	uint16_t	qid;		/* queue id to delete */
#endif
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_deleteq_cmd_t;

typedef struct {
	uint32_t	dw0;
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_deleteq_res_t;

/*
 * Get Features Command
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	flags;
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_getfeat_cmd_t;

#define NVME_GETFEAT_ID_MASK	0x000000FFU
#define NVME_GETFEAT_SEL_MASK	0x00000700U	/* NOTE: optional support */

#define NVME_GETFEAT_SEL_CUR	0x00000000U	/* current */
#define NVME_GETFEAT_SEL_DEF	0x00000100U	/* default */
#define NVME_GETFEAT_SEL_SAV	0x00000200U	/* saved */
#define NVME_GETFEAT_SEL_SUP	0x00000300U	/* supported */
#define NVME_GETFEAT_SEL_4	0x00000400U
#define NVME_GETFEAT_SEL_5	0x00000500U
#define NVME_GETFEAT_SEL_6	0x00000600U
#define NVME_GETFEAT_SEL_7	0x00000700U

typedef struct {
	uint32_t	cap;	/* SEL_SUP select only */
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_getfeat_res_t;

#define NVME_GETFEAT_CAP_SAVEABLE	0x00000001U
#define NVME_GETFEAT_CAP_NAM_SPECIFIC	0x00000002U
#define NVME_GETFEAT_CAP_CHANGEABLE	0x00000004U

/*
 * Get Log Page Command
 *
 * See nvme_log.h for returned data content
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t		lid;
	uint8_t		reserved01;
	uint16_t	numd;
#else
	uint16_t	numd;
	uint8_t		reserved01;
	uint8_t		lid;
#endif
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_getlog_cmd_t;

#define NVME_GETLOGPG_NUMD_MASK	0x0FFF

#define NVME_LID_00		0x00
#define NVME_LID_ERROR		0x01	/* error information */
#define NVME_LID_SMART		0x02	/* smart/health information */
#define NVME_LID_FWSLOT		0x03	/* firmware slot information */
#define NVME_LID_NAM_CHG_LIST	0x04	/* (optional) changed ns list */
#define NVME_LID_CMDEFF		0x05	/* (optional) command effects log */
				/* 06-7F reserved */
#define NVME_LID_RES_NOTIFY	0x80	/* (optional) Reservation notify */
				/* 81-BF I/O command set specific */
				/* C0-FF Vendor specific */

typedef struct {
	uint32_t	dw0;
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_getlog_res_t;

/*
 * Identify Command
 *
 * See nvme_ident.h for the returned data structure(s)
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t		cns;
	uint8_t		reserved01;
	uint16_t	cntid;
#else
	uint16_t	cntid;
	uint8_t		reserved01;
	uint8_t		cns;
#endif
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_identify_cmd_t;

#define NVME_CNS_ACT_NS		0x00	/* Identify Namespace Structure */
#define NVME_CNS_CTLR		0x01	/* Identify Controller Structure */
#define NVME_CNS_ACT_NSLIST	0x02	/* List of 1024 ACTIVE nsids > nsid */
				/* 03-0F reserved */

#define NVME_CNS_ALO_NSLIST	0x10	/* List of1024 ALLOCATED nsids >nsid*/
#define NVME_CNS_ALO_NS		0x11	/* Identify Namespace Structure */
#define NVME_CNS_ATT_CTLR_LIST	0x12	/* up to 2047 ctlr ids >= cntid */
					/* (that are attached to nsid) */
#define NVME_CNS_ANY_CTLR_LIST	0x13	/* same, but may/maynot be attached */
				/* 14-1F reserved */
				/* 20-FF reserved */

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_identify_res_t;

/*
 * Namespace Attachment Command
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	sel;
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_nsatt_cmd_t;

#define NVME_NSATT_SEL_MASK	0x0000000FU

#define NVME_NSATT_SEL_GET(data)	\
		((data) & NVME_NSATT_SEL_MASK)
#define NVME_NSATT_SEL_ATTACH	0
#define NVME_NSATT_SEL_DETACH	1
				/* 2-F reserved */

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_nsatt_res_t;

/*
 * Namespace Management Command
 *
 * See nvme_ns.h for transfered data structures
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	sel;
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_nsmgmt_cmd_t;

#define NVME_NSMGMT_SEL_MASK	0x0000000FU

#define NVME_NSMGMT_SEL_GET(data)	\
		((data) & NVME_NSMGMT_SEL_MASK)
#define NVME_NSMGMT_SEL_CREATE	0
#define NVME_NSMGMT_SEL_DELETE	1
				/* 2-F reserved */

typedef struct {
	uint32_t	nsid;	/* nsid created in a CREATE op only */
	uint32_t	dw1;
	nvme_comq_tail_t tail;
} __packed nvme_nsmgmt_res_t;

/*
 * NVME Set Features Command
 *
 * NOTE: PRP2 cannot point to a PRP list.  It exists in case the data area
 *	 crosses a page boundary and has a direct PRP.  Our driver
 *	 implementation page-aligns requests and will only use PRP1.
 *
 * NOTE: I decided to embed the sub-commands in the main structure, and
 *	 place the related #define's nearby.  This is the only place where
 *	 I try to embed #defines because doing so normally makes things hard
 *	 to read.
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t flags;		/* dw10 */
	union {
		/*
		 * (Generic)
		 */
		struct {
			uint32_t dw11;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		};

		/*
		 * NVME_FID_ARB
		 */
		struct {
			uint8_t	burst;	/* arb burst 2^n n=0-7 */
			uint8_t lpw;	/* N 0-255 (0=1) low pri weight */
			uint8_t mpw;	/* N 0-255 (0=1) med pri weight */
			uint8_t	hpw;	/* N 0-255 (0=1) high pri weight */
		} arb;
#define NVME_ARB_BURST_MASK	0x07
#define NVME_ARB_BURST_MAX	0x07

		/*
		 * NVME_FID_PWRMGMT
		 */
		struct {
			uint8_t xflags;
			uint8_t	reserved01;
			uint8_t	reserved02;
			uint8_t	reserved03;
		} pwrmgmt;
#define NVME_PWRMGMT_PS_MASK	0x1F;
#define NVME_PWRMGMT_WH_MASK	0xE0;
#define NVME_PWRMGMT_PS_SET(data)	((data) & NVME_PWRMGMT_PS_MASK)
#define NVME_PWRMGMT_WH_SET(data)	(((data) << 5) & NVME_PWRMGMT_WH_MASK)

		/*
		 * NVME_FID_LBARNGTYPE (requires Host Memory Buffer)
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} lbarng;
#define NVME_LBARNG_NUM_MASK	0x0000003FU

		/*
		 * NVME_FID_TEMPTHRESH
		 */
		struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			uint16_t tmpth;
			uint16_t xflags;
#else
			uint16_t xflags;
			uint16_t tmpth;
#endif
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} tempth;
#define NVME_TEMPTH_SEL_MASK	0x000FU
#define NVME_TEMPTH_TYPE_MASK	0x0030U
#define NVME_TEMPTH_TYPE_OVER	0x0000U
#define NVME_TEMPTH_TYPE_UNDER	0x0010U
#define NVME_TEMPTH_TYPE_2	0x0020U
#define NVME_TEMPTH_TYPE_3	0x0030U

		/*
		 * NVME_FID_ERRORRECOVERY
		 */
		struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			uint16_t tler;
			uint16_t xflags;
#else
			uint16_t xflags;
			uint16_t tler;
#endif
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} errrec;
#define NVME_ERRREC_DEALLOCERR	0x0001U	/* enable deallo/unwritten blk error */

		/*
		 * NVME_FID_VOLATILEWC
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} volatilewc;
#define NVME_VOLATILEWC_ENABLE	0x0001U

		/*
		 * NVME_FID_NUMQUEUES
		 *
		 * (dw0 in completion block contains the number of submission
		 *  and completion queues allocated).
		 */
		struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			uint16_t nsqr;		/* #submissions qus requested */
			uint16_t ncqr;		/* #completion qus requested */
#else
			uint16_t ncqr;
			uint16_t nsqr;
#endif
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} numqs;

		/*
		 * NVME_FID_INTCOALESCE
		 *
		 * NOTE: default upon reset is 0 (no coalescing)
		 */
		struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			uint8_t	thr;		/* 0's based value, 0=1 */
			uint8_t	time;		/* 0's based value, 0=1 */
			uint16_t reserved02;
#else
			uint16_t reserved02;
			uint8_t	time;
			uint8_t	thr;
#endif
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} intcoal;

		/*
		 * NVME_FID_INTVECTOR
		 */
		struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			uint16_t iv;
			uint16_t xflags;
#else
			uint16_t xflags;
			uint16_t iv;
#endif
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} intvect;
#define NVME_INTVECT_CD		0x0001U		/* disable coalescing */

		/*
		 * NVME_FID_WRATOMICYNRM
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} wratom;
#define NVME_WRATOM_DN		0x00000001U	/* disables AWUN/NAWUN */

		/*
		 * NVME_FID_ASYNCEVCFG
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} asyncev;
#define NVME_ASYNCEV_SMART_MASK	0x000000FFU	/* bits same as SMART bits */
#define NVME_ASYNCEV_NS_ATTR	0x00000100U	/* ns attr change */
#define NVME_ASYNCEV_FW_ACTVTE	0x00000200U	/* fw activation notice */

		/*
		 * NVME_FID_AUTOPS	(requires Host Memory Buffer)
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} autops;
#define NVME_AUTOPS_ENABLE	0x00000001U	/* enable autonomous ps trans */

		/*
		 * NVME_FID_HOSTMEMBUF
		 */
		struct {
			uint32_t xflags;
			uint32_t sizepgs;	/* buffer size in mps units */
			uint32_t hmdlla;	/* desclist lower address */
			uint32_t hmdlua;	/* desclist upper address */
			uint32_t count;		/* list entry count */
		} hostmem;
#define NVME_HOSTMEM_RETURN	0x00000002U	/* same memory after reset */
#define NVME_HOSTMEM_ENABLE	0x00000001U

		/*
		 * NVME_FID_SFTPROGRESS
		 */
		struct {
#if _BYTE_ORDER == _LITTLE_ENDIAN
			uint8_t pbslc;		/* pre-boot software load cnt */
			uint8_t reserved01;
			uint8_t reserved02;
			uint8_t reserved03;
#else
			uint8_t reserved03;
			uint8_t reserved02;
			uint8_t reserved01;
			uint8_t pbslc;
#endif
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} sftprog;

		/*
		 * NVME_FID_HOSTID
		 */
		struct {
			uint32_t dw11;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} hostid;

		/*
		 * NVME_FID_RESERVENOTMASK
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} resnotify;
#define NVME_RESNOTIFY_RESPRE	0x00000008U
#define NVME_RESNOTIFY_RESREL	0x00000004U
#define NVME_RESNOTIFY_REGPRE	0x00000002U

		/*
		 * NVME_FID_RESERVEPERSIST
		 */
		struct {
			uint32_t xflags;
			uint32_t dw12;
			uint32_t dw13;
			uint32_t dw14;
			uint32_t dw15;
		} respersist;
#define NVME_RESPERSIST_PTPL	0x00000001U	/* persist thru power loss */
	};
} __packed nvme_setfeat_cmd_t;

#define NVME_SETFEAT_SAVE	0x80000000U
#define NVME_FID_MASK		0x000000FFU

#define NVME_FID_GET(data)	\
		((data) & NVME_FID_MASK)
#define NVME_FID_SET(fid)	\
		((fid) & NVME_FID_MASK)

#define NVME_FID_00		0x00
#define NVME_FID_ARB		0x01	/* Aribtration */
#define NVME_FID_PWRMGMT	0x02	/* Power Management */
#define NVME_FID_LBARNGTYPE	0x03	/* (opt) LBA Range Type */
#define NVME_FID_TEMPTHRESH	0x04	/* Temp Threshold */
#define NVME_FID_ERRORRECOVERY	0x05	/* Error Recovery */
#define NVME_FID_VOLATILEWC	0x06	/* (opt) Volatile Write Cache */
#define NVME_FID_NUMQUEUES	0x07	/* Number of Queues */
#define NVME_FID_INTCOALESCE	0x08	/* Interrupt Coalescing */
#define NVME_FID_INTVECTOR	0x09	/* Interrupt Vector Config */
#define NVME_FID_WRATOMICYNRM	0x0A	/* Write Atomicy Normal */
#define NVME_FID_ASYNCEVCFG	0x0B	/* Async Event Config */
#define NVME_FID_AUTOPS		0x0C	/* (opt) Autonomous pwr state */
#define NVME_FID_HOSTMEMBUF	0x0D	/* (opt) Host memory buffer */
				/* 0E-77 reserved */
				/* 78-7F see NVMe management ifc spec */
				/* 80-BF cmd set specific (reserved) */
#define NVME_FID_SFTPROGRESS	0x80	/* (opt) Software Progress Marker */
#define NVME_FID_HOSTID		0x81	/* (opt) Host Identifier */
#define NVME_FID_RESERVENOTMASK	0x82	/* (opt) Reservation Notify Marker */
#define NVME_FID_RESERVEPERSIST	0x83	/* (opt) Reservation Persistance */

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_setfeat_res_t;

/*
 * Format NVM Command
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	flags;
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_format_cmd_t;

#define NVME_FORMAT_SES_MASK		0x00000E00U
#define NVME_FORMAT_SES_NONE		0x00000000U
#define NVME_FORMAT_SES_NORM		0x00000200U
#define NVME_FORMAT_SES_CRYPTO		0x00000400U
					/* remainint ids reserved */

#define NVME_FORMAT_PROT_FIRST		0x00000100U	/* first-8 of meta */
							/* (else last-8) */

#define NVME_FORMAT_PROT_MASK		0x000000E0U
#define NVME_FORMAT_PROT_NONE		0x00000000U
#define NVME_FORMAT_PROT_TYPE1		0x00000020U
#define NVME_FORMAT_PROT_TYPE2		0x00000040U
#define NVME_FORMAT_PROT_TYPE3		0x00000060U
					/* remaining ids reserved */

#define NVME_FORMAT_MS			0x00000010U	/* metadata 1=inline */
#define NVME_FORMAT_LBA_FMT_MASK	0x0000000FU
#define NVME_FORMAT_LBA_FMT_SET(data)	\
	((data) & NVME_FORMAT_LBA_FMT_MASK)

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_format_res_t;

/*
 * Security Receive Command
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t		nssf;
	uint8_t		spsp0;
	uint8_t		spsp1;
	uint8_t		secp;
#else
	uint8_t		secp;
	uint8_t		spsp1;
	uint8_t		spsp0;
	uint8_t		nssf;
#endif
	uint32_t	alloc_len;	/* allocation length */
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_secrecv_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_secrecv_res_t;

/*
 * Security Send Command
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t		nssf;
	uint8_t		spsp0;
	uint8_t		spsp1;
	uint8_t		secp;
#else
	uint8_t		secp;
	uint8_t		spsp1;
	uint8_t		spsp0;
	uint8_t		nssf;
#endif
	uint32_t	xfer_len;	/* xfer length */
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_secsend_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_secsend_res_t;


/************************************************************************
 * NVM I/O COMMANDS - Core I/O Commands, NVM command set		*
 ************************************************************************
 *
 * The nsid field is required for all of these commands.
 */

#define NVME_IOCMD_FLUSH	0x00
#define NVME_IOCMD_WRITE	0x01
#define NVME_IOCMD_READ		0x02
#define NVME_IOCMD_WRITEUC	0x04
#define NVME_IOCMD_COMPARE	0x05
#define NVME_IOCMD_WRITEZ	0x08
#define NVME_IOCMD_DATAMGMT	0x09
#define NVME_IOCMD_RESREG	0x0D
#define NVME_IOCMD_RESREP	0x0E
#define NVME_IOCMD_RESACQ	0x11
#define NVME_IOCMD_RESREL	0x15

/*
 * ioflags (16 bits) is similar across many NVM commands, make
 * those definitions generic.
 */
#define NVME_IOFLG_LR		0x8000U	/* limited retry */
#define NVME_IOFLG_FUA		0x4000U	/* force unit access */
#define NVME_IOFLG_PRINFO_MASK	0x3C00U	/* prot info mask */
#define NVME_IOFLG_RESV_MASK	0x03FFU

/*
 * dsm (32 bits) exists in the read and write commands.
 */
#define NVME_DSM_INCOMPRESSIBLE	0x00000080U
#define NVME_DSM_SEQREQ		0x00000040U

#define NVME_DSM_ACCLAT_MASK	0x00000030U
#define NVME_DSM_ACCLAT_UNSPEC	0x00000000U
#define NVME_DSM_ACCLAT_IDLE	0x00000010U
#define NVME_DSM_ACCLAT_NORM	0x00000020U
#define NVME_DSM_ACCLAT_LOW	0x00000030U

#define NVME_DSM_ACCFREQ_MASK	0x0000000FU
#define NVME_DSM_ACCFREQ_UNSPEC	0x00000000U	/* unspecified */
#define NVME_DSM_ACCFREQ_WRTYP	0x00000001U	/* typical reads & writes */
#define NVME_DSM_ACCFREQ_WRLOW	0x00000002U	/* few writes, few reads */
#define NVME_DSM_ACCFREQ_WLORHI	0x00000003U	/* few writes, many reads */
#define NVME_DSM_ACCFREQ_WHIRLO	0x00000004U	/* many writes, few reads */
#define NVME_DSM_ACCFREQ_WHIRHI	0x00000005U	/* many writes, many reads */
#define NVME_DSM_ACCFREQ_RONETM	0x00000006U	/* one-time read */
#define NVME_DSM_ACCFREQ_RSPECU	0x00000007U	/* speculative read */
#define NVME_DSM_ACCFREQ_OVERWR	0x00000008U	/* will be overwritten soon */
				/* 9-F reserved */


/*
 * NVM Flush Command			NVME_IOCMD_FLUSH
 *
 * For entire nsid, dw10-15 are reserved and should be zerod.
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	reserved10;
	uint32_t	reserved11;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_flush_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_flush_res_t;

/*
 * NVM Write Command			NVME_IOCMD_WRITE
 */
typedef struct {
	nvme_subq_head_t head;
	uint64_t	start_lba;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	count_lba;
	uint16_t	ioflags;
#else
	uint16_t	ioflags;
	uint16_t	count_lba;
#endif
	uint32_t	dsm;
	uint32_t	iilbrt;		/* expected initial logblk ref tag */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	lbat;		/* expected log blk app tag */
	uint16_t	lbatm;		/* expected log blk app tag mask */
#else
	uint16_t	lbatm;
	uint16_t	lbat;
#endif
} __packed nvme_write_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_write_res_t;

/*
 * NVM Read Command			NVME_IOCMD_READ
 */
typedef struct {
	nvme_subq_head_t head;
	uint64_t	start_lba;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	count_lba;
	uint16_t	ioflags;
#else
	uint16_t	ioflags;
	uint16_t	count_lba;
#endif
	uint32_t	dsm;
	uint32_t	eilbrt;		/* expected initial logblk ref tag */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	elbat;		/* expected log blk app tag */
	uint16_t	elbatm;		/* expected log blk app tag mask */
#else
	uint16_t	elbatm;
	uint16_t	elbat;
#endif
} __packed nvme_read_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_read_res_t;

/*
 * NVM Write Uncorrectable Command	NVME_IOCMD_WRITEUC
 */
typedef struct {
	nvme_subq_head_t head;
	uint64_t	start_lba;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	count_lba;
	uint16_t	reserved12l;
#else
	uint16_t	reserved12l;
	uint16_t	count_lba;
#endif
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_writeuc_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_writeuc_res_t;

/*
 * NVM Compare Command			NVME_IOCMD_COMPARE
 */
typedef struct {
	nvme_subq_head_t head;
	uint64_t	start_lba;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	count_lba;
	uint16_t	ioflags;
#else
	uint16_t	ioflags;
	uint16_t	count_lba;
#endif
	uint32_t	reserved13;
	uint32_t	eilbrt;		/* expected initial logblk ref tag */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	elbat;		/* expected log blk app tag */
	uint16_t	elbatm;		/* expected log blk app tag mask */
#else
	uint16_t	elbatm;
	uint16_t	elbat;
#endif
} __packed nvme_cmp_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_cmp_res_t;

/*
 * NVM Write Zeros Command		NVME_IOCMD_WRITEZ
 */
typedef struct {
	nvme_subq_head_t head;
	uint64_t	start_lba;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	count_lba;
	uint16_t	ioflags;
#else
	uint16_t	ioflags;
	uint16_t	count_lba;
#endif
	uint32_t	dsm;
	uint32_t	iilbrt;		/* expected initial logblk ref tag */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint16_t	lbat;		/* expected log blk app tag */
	uint16_t	lbatm;		/* expected log blk app tag mask */
#else
	uint16_t	lbatm;
	uint16_t	lbat;
#endif
} __packed nvme_writez_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_writez_res_t;

/*
 * NVM Dataset Management Command	NVME_IOCMD_DATAMGMT
 *
 * See nvme_datamgmt.h for range and context attributes
 */
typedef struct {
	nvme_subq_head_t head;
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t		nr;	/* number of 16-byte ranges 0's based (0=1) */
	uint8_t		reserved01;
	uint8_t		reserved02;
	uint8_t		reserved03;
#else
	uint8_t		reserved03;
	uint8_t		reserved02;
	uint8_t		reserved01;
	uint8_t		nr;	/* number of 16-byte ranges 0's based (0=1) */
#endif
	uint32_t	flags;
	uint32_t	reserved12;
	uint32_t	reserved13;
	uint32_t	reserved14;
	uint32_t	reserved15;
} __packed nvme_datamgmt_cmd_t;

/* flags field */
#define NVME_DATAMGT_AD		0x00000004U	/* 1=deallocate ranges */
#define NVME_DATAMGT_IDW	0x00000002U	/* 1=hint for write acc */
#define NVME_DATAMGT_IDR	0x00000001U	/* 1=hint for read acc */

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_datamgmt_res_t;

/*
 * NVM Reservation Register Command	NVME_IOCMD_RESREG (TOD)
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	dw10;
	uint32_t	dw11;
	uint32_t	dw12;
	uint32_t	dw13;
	uint32_t	dw14;
	uint32_t	dw15;
} __packed nvme_resreg_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_resreg_res_t;

/*
 * NVM Reservation Report Command	NVME_IOCMD_RESREP (TODO)
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	dw10;
	uint32_t	dw11;
	uint32_t	dw12;
	uint32_t	dw13;
	uint32_t	dw14;
	uint32_t	dw15;
} __packed nvme_resrep_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_resrep_res_t;

/*
 * NVM Reservation Acquire Command	NVME_IOCMD_RESACQ (TODO)
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	dw10;
	uint32_t	dw11;
	uint32_t	dw12;
	uint32_t	dw13;
	uint32_t	dw14;
	uint32_t	dw15;
} __packed nvme_resacq_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_resacq_res_t;

/*
 * NVM Reservation Release Command	NVME_IOCMD_RESREL (TODO)
 */
typedef struct {
	nvme_subq_head_t head;
	uint32_t	dw10;
	uint32_t	dw11;
	uint32_t	dw12;
	uint32_t	dw13;
	uint32_t	dw14;
	uint32_t	dw15;
} __packed nvme_resrel_cmd_t;

typedef struct {
	uint32_t dw0;
	uint32_t dw1;
	nvme_comq_tail_t tail;
} __packed nvme_resrel_res_t;


/*
 * SUBMISSION AND COMPLETION QUEUE ALL-COMMAND UNIONS (primary API)
 *
 * Union of all submission queue commands (64 bytes)
 */
typedef union {
	struct {		/* convenient accessors */
		nvme_subq_head_t head;
		uint32_t dw10;
		uint32_t dw11;
		uint32_t dw12;
		uint32_t dw13;
		uint32_t dw14;
		uint32_t dw15;
	};
	nvme_subq_item_t	item;
	nvme_abort_cmd_t	abort;
	nvme_async_cmd_t	async;
	nvme_createcomq_cmd_t	crcom;
	nvme_createsubq_cmd_t	crsub;
	nvme_deleteq_cmd_t	delete;
	nvme_getfeat_cmd_t	getfeat;
	nvme_getlog_cmd_t	getlog;
	nvme_identify_cmd_t	identify;
	nvme_nsatt_cmd_t	nsatt;
	nvme_nsmgmt_cmd_t	nsmgmt;
	nvme_setfeat_cmd_t	setfeat;
	nvme_format_cmd_t	format;
	nvme_secrecv_cmd_t	secrecv;
	nvme_secsend_cmd_t	secsend;
	nvme_flush_cmd_t	flush;
	nvme_write_cmd_t	write;
	nvme_read_cmd_t		read;
	nvme_writeuc_cmd_t	writeuc;
	nvme_cmp_cmd_t		cmp;
	nvme_writez_cmd_t	writez;
	nvme_datamgmt_cmd_t	datamgmt;
	nvme_resreg_cmd_t	resreg;
	nvme_resrep_cmd_t	resrep;
	nvme_resacq_cmd_t	resacq;
	nvme_resrel_cmd_t	resrel;
} __packed nvme_allcmd_t;

/*
 * Union of all completion queue responses (16 bytes)
 */
typedef union {
	struct {		/* convenient accessors */
		uint32_t dw0;
		uint32_t dw1;
		nvme_comq_tail_t tail;
	};
	nvme_comq_item_t	item;
	nvme_async_res_t	async;
	nvme_createcomq_res_t	crcom;
	nvme_createsubq_res_t	crsub;
	nvme_deleteq_res_t	delete;
	nvme_getfeat_res_t	getfeat;
	nvme_getlog_res_t	getlog;
	nvme_identify_res_t	identify;
	nvme_nsatt_res_t	nsatt;
	nvme_nsmgmt_res_t	nsmgmt;
	nvme_setfeat_res_t	setfeat;
	nvme_format_res_t	format;
	nvme_secrecv_res_t	secrecv;
	nvme_secsend_res_t	secsend;
	nvme_flush_res_t	flush;
	nvme_write_res_t	write;
	nvme_read_res_t		read;
	nvme_writeuc_res_t	writeuc;
	nvme_cmp_res_t		cmp;
	nvme_writez_res_t	writez;
	nvme_datamgmt_res_t	datamgmt;
	nvme_resreg_res_t	resreg;
	nvme_resrep_res_t	resrep;
	nvme_resacq_res_t	resacq;
	nvme_resrel_res_t	resrel;
} __packed nvme_allres_t;

/*
 * Union of all administrative data buffers (does not exceed 4KB)
 */
typedef union {
	nvme_pwstate_data_t	pwstate;
	nvme_lba_fmt_data_t	lbafmt;
	nvme_ident_ctlr_data_t	idctlr;
	nvme_ident_ns_data_t	idns;
	nvme_ident_ns_list_t	nslist;
	nvme_ident_ctlr_list_t	ctlrlist;
	nvme_log_error_data_t	logerr;
	nvme_log_smart_data_t	logsmart;
	nvme_fw_slot_data_t	fwslot;
	nvme_nsmgmt_create_data_t nsmgmt;
	nvme_cmdeff_data_t	cmdeff;
	nvme_resnotify_data_t	resnotify;
} __packed nvme_admin_data_t;

/*
 * MISC STRUCTURES SENT OR RECEIVED AS DATA
 */
