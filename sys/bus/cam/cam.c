/*
 * Generic utility routines for the Common Access Method layer.
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/cam/cam.c,v 1.4 2001/03/27 05:45:09 ken Exp $
 */
#include <sys/param.h>

#ifdef _KERNEL

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#else

#include <stdlib.h>
#include <stdio.h>
#include <camlib.h>

#define kbsearch	bsearch

#endif

#include "cam.h"
#include "cam_ccb.h"
#include "scsi/scsi_all.h"
#include <sys/sbuf.h>

#ifdef _KERNEL
#include <sys/libkern.h>
#include "cam_xpt.h"
#endif

static int	camstatusentrycomp(const void *key, const void *member);

const struct cam_status_entry cam_status_table[] = {
	{ CAM_REQ_INPROG,	 "CCB request is in progress"		     },
	{ CAM_REQ_CMP,		 "CCB request completed without error"	     },
	{ CAM_REQ_ABORTED,	 "CCB request aborted by the host"	     },
	{ CAM_UA_ABORT,		 "Unable to abort CCB request"		     },
	{ CAM_REQ_CMP_ERR,	 "CCB request completed with an error"	     },
	{ CAM_BUSY,		 "CAM subsystem is busy"		     },
	{ CAM_REQ_INVALID,	 "CCB request was invalid"		     },
	{ CAM_PATH_INVALID,	 "Supplied Path ID is invalid"		     },
	{ CAM_DEV_NOT_THERE,	 "Device Not Present"			     },
	{ CAM_UA_TERMIO,	 "Unable to terminate I/O CCB request"	     },
	{ CAM_SEL_TIMEOUT,	 "Selection Timeout"			     },
	{ CAM_CMD_TIMEOUT,	 "Command timeout"			     },
	{ CAM_SCSI_STATUS_ERROR, "SCSI Status Error"			     },
	{ CAM_MSG_REJECT_REC,	 "Message Reject Reveived"		     },
	{ CAM_SCSI_BUS_RESET,	 "SCSI Bus Reset Sent/Received"		     },
	{ CAM_UNCOR_PARITY,	 "Uncorrectable parity/CRC error"	     },
	{ CAM_AUTOSENSE_FAIL,	 "Auto-Sense Retrieval Failed"		     },
	{ CAM_NO_HBA,		 "No HBA Detected"			     },
	{ CAM_DATA_RUN_ERR,	 "Data Overrun error"			     },
	{ CAM_UNEXP_BUSFREE,	 "Unexpected Bus Free"			     },
	{ CAM_SEQUENCE_FAIL,	 "Target Bus Phase Sequence Failure"	     },
	{ CAM_CCB_LEN_ERR,	 "CCB length supplied is inadequate"	     },
	{ CAM_PROVIDE_FAIL,	 "Unable to provide requested capability"    },
	{ CAM_BDR_SENT,		 "SCSI BDR Message Sent"		     },
	{ CAM_REQ_TERMIO,	 "CCB request terminated by the host"	     },
	{ CAM_UNREC_HBA_ERROR,	 "Unrecoverable Host Bus Adapter Error"	     },
	{ CAM_REQ_TOO_BIG,	 "The request was too large for this host"   },
	{ CAM_REQUEUE_REQ,	 "Unconditionally Re-queue Request",	     },
	{ CAM_IDE,		 "Initiator Detected Error Message Received" },
	{ CAM_RESRC_UNAVAIL,	 "Resource Unavailable"			     },
	{ CAM_UNACKED_EVENT,	 "Unacknowledged Event by Host"		     },
	{ CAM_MESSAGE_RECV,	 "Message Received in Host Target Mode"	     },
	{ CAM_INVALID_CDB,	 "Invalid CDB received in Host Target Mode"  },
	{ CAM_LUN_INVALID,	 "Invalid Lun"				     },
	{ CAM_TID_INVALID,	 "Invalid Target ID"			     },
	{ CAM_FUNC_NOTAVAIL,	 "Function Not Available"		     },
	{ CAM_NO_NEXUS,		 "Nexus Not Established"		     },
	{ CAM_IID_INVALID,	 "Invalid Initiator ID"			     },
	{ CAM_CDB_RECVD,	 "CDB Received"				     },
	{ CAM_LUN_ALRDY_ENA,	 "LUN Already Enabled for Target Mode"	     },
	{ CAM_SCSI_BUSY,	 "SCSI Bus Busy"			     },
};

const int num_cam_status_entries = NELEM(cam_status_table);

#ifdef _KERNEL
SYSCTL_NODE(_kern, OID_AUTO, cam, CTLFLAG_RD, 0, "CAM Subsystem");
#endif

void
cam_strvis(u_int8_t *dst, const u_int8_t *src, int srclen, int dstlen)
{

	/* Trim leading/trailing spaces, nulls. */
	while (srclen > 0 && src[0] == ' ')
		src++, srclen--;
	while (srclen > 0
	    && (src[srclen-1] == ' ' || src[srclen-1] == '\0'))
		srclen--;

	while (srclen > 0 && dstlen > 1) {
		u_int8_t *cur_pos = dst;

		if (*src < 0x20 || *src >= 0x80) {
			/* SCSI-II Specifies that these should never occur. */
			/* non-printable character */
			if (dstlen > 4) {
				*cur_pos++ = '\\';
				*cur_pos++ = ((*src & 0300) >> 6) + '0';
				*cur_pos++ = ((*src & 0070) >> 3) + '0';
				*cur_pos++ = ((*src & 0007) >> 0) + '0';
			} else {
				*cur_pos++ = '?';
			}
		} else {
			/* normal character */
			*cur_pos++ = *src;
		}
		src++;
		srclen--;
		dstlen -= cur_pos - dst;
		dst = cur_pos;
	}
	*dst = '\0';
}

/*
 * Compare string with pattern, returning 0 on match.
 * Short pattern matches trailing blanks in name,
 * wildcard '*' in pattern matches rest of name,
 * wildcard '?' matches a single non-space character.
 */
int
cam_strmatch(const u_int8_t *str, const u_int8_t *pattern, int str_len)
{

	while (*pattern != '\0'&& str_len > 0) {  

		if (*pattern == '*') {
			return (0);
		}
		if ((*pattern != *str)
		 && (*pattern != '?' || *str == ' ')) {
			return (1);
		}
		pattern++;
		str++;
		str_len--;
	}
	while (str_len > 0 && *str++ == ' ')
		str_len--;

	return (str_len);
}

caddr_t
cam_quirkmatch(caddr_t target, caddr_t quirk_table, int num_entries,
	       int entry_size, cam_quirkmatch_t *comp_func)
{
	for (; num_entries > 0; num_entries--, quirk_table += entry_size) {
		if ((*comp_func)(target, quirk_table) == 0)
			return (quirk_table);
	}
	return (NULL);
}

const struct cam_status_entry*
cam_fetch_status_entry(cam_status status)
{
	status &= CAM_STATUS_MASK;
	return (kbsearch(&status, &cam_status_table,
			num_cam_status_entries,
			sizeof(*cam_status_table),
			camstatusentrycomp));
}

static int
camstatusentrycomp(const void *key, const void *member)
{
	cam_status status;
	const struct cam_status_entry *table_entry;

	status = *(const cam_status *)key;
	table_entry = (const struct cam_status_entry *)member;

	return (status - table_entry->status_code);
}


#ifdef _KERNEL
char *
cam_error_string(union ccb *ccb, char *str, int str_len,
		 cam_error_string_flags flags,
		 cam_error_proto_flags proto_flags)
#else /* !_KERNEL */
char *
cam_error_string(struct cam_device *device, union ccb *ccb, char *str,
		 int str_len, cam_error_string_flags flags,
		 cam_error_proto_flags proto_flags)
#endif /* _KERNEL/!_KERNEL */
{
	char path_str[64];
	struct sbuf sb;

	if ((ccb == NULL)
	 || (str == NULL)
	 || (str_len <= 0))
		return(NULL);

	if (flags == CAM_ESF_NONE)
		return(NULL);

	switch (ccb->ccb_h.func_code) {
		case XPT_SCSI_IO:
			switch (proto_flags & CAM_EPF_LEVEL_MASK) {
			case CAM_EPF_NONE:
				break;
			case CAM_EPF_ALL:
			case CAM_EPF_NORMAL:
				proto_flags |= CAM_ESF_PRINT_SENSE;
				/* FALLTHROUGH */
			case CAM_EPF_MINIMAL:
				proto_flags |= CAM_ESF_PRINT_STATUS;
			default:
				break;
			}
			break;
		default:
			break;
	}
#ifdef _KERNEL
	xpt_path_string(ccb->csio.ccb_h.path, path_str, sizeof(path_str));
#else /* !_KERNEL */
	cam_path_string(device, path_str, sizeof(path_str));
#endif /* _KERNEL/!_KERNEL */

	sbuf_new(&sb, str, str_len, 0);

	if (flags & CAM_ESF_COMMAND) {

		sbuf_cat(&sb, path_str);

		switch (ccb->ccb_h.func_code) {
		case XPT_SCSI_IO:
#ifdef _KERNEL
			scsi_command_string(&ccb->csio, &sb);
#else /* !_KERNEL */
			scsi_command_string(device, &ccb->csio, &sb);
#endif /* _KERNEL/!_KERNEL */
			sbuf_printf(&sb, "\n");

			break;
		default:
			break;
		}
	}

	if (flags & CAM_ESF_CAM_STATUS) {
		cam_status status;
		const struct cam_status_entry *entry;

		sbuf_cat(&sb, path_str);

		status = ccb->ccb_h.status & CAM_STATUS_MASK;

		entry = cam_fetch_status_entry(status);

		if (entry == NULL)
			sbuf_printf(&sb, "CAM Status: Unknown (%#x)\n",
				    ccb->ccb_h.status);
		else
			sbuf_printf(&sb, "CAM Status: %s\n",
				    entry->status_text);
	}

	if (flags & CAM_ESF_PROTO_STATUS) {

		switch (ccb->ccb_h.func_code) {
		case XPT_SCSI_IO:
			if ((ccb->ccb_h.status & CAM_STATUS_MASK) !=
			     CAM_SCSI_STATUS_ERROR)
				break;

			if (proto_flags & CAM_ESF_PRINT_STATUS) {
				sbuf_cat(&sb, path_str);
				/*
				 * Print out the SCSI status byte as long as
				 * the user wants some protocol output.
				 */
				sbuf_printf(&sb, "SCSI Status: %s\n",
					    scsi_status_string(&ccb->csio));
			}

			if ((proto_flags & CAM_ESF_PRINT_SENSE)
			 && (ccb->csio.scsi_status == SCSI_STATUS_CHECK_COND)
			 && (ccb->ccb_h.status & CAM_AUTOSNS_VALID)) {

#ifdef _KERNEL
				scsi_sense_sbuf(&ccb->csio, &sb,
						SSS_FLAG_NONE);
#else /* !_KERNEL */
				scsi_sense_sbuf(device, &ccb->csio, &sb,
						SSS_FLAG_NONE);
#endif /* _KERNEL/!_KERNEL */
			}
			break;
		default:
			break;
		}
	}

	sbuf_finish(&sb);

	return(sbuf_data(&sb));
}

#ifdef _KERNEL

void
cam_error_print(union ccb *ccb, cam_error_string_flags flags,
		cam_error_proto_flags proto_flags)
{
	char str[512];

	kprintf("%s", cam_error_string(ccb, str, sizeof(str), flags,
	       proto_flags));
}

#else /* !_KERNEL */

void
cam_error_print(struct cam_device *device, union ccb *ccb,
		cam_error_string_flags flags, cam_error_proto_flags proto_flags,
		FILE *ofile)
{
	char str[512];

	if ((device == NULL) || (ccb == NULL) || (ofile == NULL))
		return;

	fprintf(ofile, "%s", cam_error_string(device, ccb, str, sizeof(str),
		flags, proto_flags));
}

#endif /* _KERNEL/!_KERNEL */

/*
 * Common calculate geometry fuction
 *
 * Caller should set ccg->volume_size and block_size.
 * The extended parameter should be zero if extended translation
 * should not be used.
 */
void
cam_calc_geometry(struct ccb_calc_geometry *ccg, int extended)
{
	uint32_t size_mb, secs_per_cylinder;

	if (ccg->block_size == 0) {
		ccg->ccb_h.status = CAM_REQ_CMP_ERR;
		return;
	}
	size_mb = (1024L * 1024L) / ccg->block_size;
	if (size_mb == 0) {
		ccg->ccb_h.status = CAM_REQ_CMP_ERR;
		return;
	}
	size_mb = ccg->volume_size / size_mb;
	if (size_mb > 1024 && extended) {
		ccg->heads = 255;
		ccg->secs_per_track = 63;
	} else {
		ccg->heads = 64;
		ccg->secs_per_track = 32;
	}
	secs_per_cylinder = ccg->heads * ccg->secs_per_track;
	ccg->cylinders = ccg->volume_size / secs_per_cylinder;
	ccg->ccb_h.status = CAM_REQ_CMP;
}
