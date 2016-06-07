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
 * Get Log Page - Error Information (Log ID 01) (64 bytes)
 */
typedef struct {
	uint64_t	error_count;
	uint16_t	subq_id;
	uint16_t	cmd_id;
	uint16_t	status;
	uint16_t	param;
	uint64_t	lba;
	uint32_t	nsid;
	uint8_t		vendor;
	uint8_t		reserved29;
	uint8_t		reserved30;
	uint8_t		reserved31;
	uint64_t	csi;		/* command specific information */
	uint8_t		reserved40[24];
} __packed nvme_log_error_data_t;

/*
 * Get Log Page - Smart/ Health Information (Log ID 02) (512 bytes)
 */
typedef struct {
	uint8_t		crit_flags;
	uint8_t		comp_temp1;
	uint8_t		comp_temp2;
	uint8_t		spare_cap;	/* normalized spare capacity 0-100 */
	uint8_t		spare_thresh;	/* event when cap falls below value */
	uint8_t		rated_life;	/* 0-100, may exceed capped at 255 */
	uint8_t		reserved6[26];

					/* 16-byte fields lo, hi */
	uint64_t	read_count[2];	/* in 512000 byte units */
	uint64_t	write_count[2];	/* in 512000 byte units */
	uint64_t	read_cmds[2];
	uint64_t	write_cmds[2];
	uint64_t	busy_time[2];	/* in minutes */
	uint64_t	power_cycles[2];
	uint64_t	powon_hours[2];
	uint64_t	unsafe_shutdowns[2];
	uint64_t	unrecoverable_errors[2];
	uint64_t	error_log_entries[2];

	uint32_t	warn_comp_temp_time; /* minutes temp > warn level */
	uint32_t	crit_comp_temp_time; /* minutes temp > crit level */

	uint16_t	temp_sensors[8]; /* sensors 1-8 in kelvin 0=unimp */

	uint8_t		reserved216[296];
} __packed nvme_log_smart_data_t;

#define NVME_SMART_CRF_BELOW_THRESH	0x01
#define NVME_SMART_CRF_ABOVE_THRESH	0x02	/* or below under-temp thresh*/
#define NVME_SMART_CRF_UNRELIABLE	0x04
#define NVME_SMART_CRF_MEDIA_RO		0x08
#define NVME_SMART_CRF_VOLTL_BKUP_FAIL	0x10
#define NVME_SMART_CRF_RES20		0x20
#define NVME_SMART_CRF_RES40		0x40
#define NVME_SMART_CRF_RES80		0x80

/*
 * Firmware Slot Information (Log ID 03) (512 bytes)
 */
typedef struct {
	uint8_t		flags;
	uint8_t		reserved1[7];
	uint64_t	revision[7];	/* revision slot 1-7 */
	uint8_t		reserved64[448];
} __packed nvme_fw_slot_data_t;

#define NVME_FWSLOT_CRF_RESERVED_80	0x80
#define NVME_FWSLOT_CRF_FWNEXT_MASK	0x70
#define NVME_FWSLOT_CRF_RESERVED_08	0x08
#define NVME_FWSLOT_CRF_FWCURR_MASK	0x07

#define NVME_FWSLOT_CRF_FWNEXT_GET(data)	\
		(((data) & NVME_FWSLOT_CRF_FWNEXT_MASK) >> 4)
#define NVME_FWSLOT_CRF_FWCURR_GET(data)	\
		((data) & NVME_FWSLOT_CRF_FWCURR_MASK)

/*
 * Command Supported and Effects (Log ID 05) (4096 bytes)
 *
 * Iterates available admin and I/O commands, one command-effects data
 * structure for each command, indexed by command id.
 *
 * CSE - Indicates whether command must be serialized (i.e. no other
 *	 commands may be pending in the namespace or globally).
 *
 * CSUPP - Indicates that the command is supported by the controller
 *	   (use for iteration skip).
 */
typedef struct {
	uint32_t	admin_cmds[256];
	uint32_t	io_cmds[256];
} __packed nvme_cmdeff_data_t;

#define NVME_CMDEFF_RESERVED19		0xFFF80000U
#define NVME_CMDEFF_CSE_MASK		0x00007000U
#define NVME_CMDEFF_RESERVED05		0x00000FE0U
#define NVME_CMDEFF_CCC			0x00000010U
#define NVME_CMDEFF_NIC			0x00000008U
#define NVME_CMDEFF_NCC			0x00000004U
#define NVME_CMDEFF_LBCC		0x00000002U
#define NVME_CMDEFF_CSUPP		0x00000001U

#define NVME_CMDEFF_CSE_NORM		0x00000000U
#define NVME_CMDEFF_CSE_NS_SERIALIZE	0x00001000U
#define NVME_CMDEFF_CSE_GLOB_SERIALIZE	0x00002000U

/*
 * Reservation Notification (Log ID 0x80) (64 bytes)
 */
typedef struct {
	uint64_t	logpg_count;
	uint8_t		type;
	uint8_t		logpg_avail;
	uint8_t		reserved10[2];
	uint32_t	nsid;
	uint8_t		reserved16[48];
} __packed nvme_resnotify_data_t;

#define NVME_RESNOTIFY_EMPTY		0x00
#define NVME_RESNOTIFY_REG_PREEMPTED	0x01
#define NVME_RESNOTIFY_RES_RELEASED	0x02
#define NVME_RESNOTIFY_RES_PREEMPTED	0x03
					/* 04-FF reserved */

/* TYPE_CSS status */
#define NVME_RESNOTIFY_INVALID		0x09
