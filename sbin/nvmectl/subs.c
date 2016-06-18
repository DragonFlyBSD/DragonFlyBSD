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

#include "nvmectl.h"

const char *
format_number(uint64_t value)
{
	static char buf[8];

	humanize_number(buf, sizeof(buf), value, "",
			 HN_AUTOSCALE, HN_DIVISOR_1000 | HN_NOSPACE);

	return buf;
}

const char *
status_to_str(uint16_t status)
{
	static char buf[64];
	int code = NVME_COMQ_STATUS_CODE_GET(status);
	const char *msg = NULL;

	switch(NVME_COMQ_STATUS_TYPE_GET(status)) {
	case NVME_STATUS_TYPE_GENERIC:
		switch(code) {
		case NVME_CODE_SUCCESS:
			msg = "SUCCESS";
			break;
		case NVME_CODE_BADOP:
			msg = "Bad Op";
			break;
		case NVME_CODE_BADFIELD:
			msg = "Bad Field";
			break;
		case NVME_CODE_IDCONFLICT:
			msg = "ID Conflict";
			break;
		case NVME_CODE_BADXFER:
			msg = "Bad XFer";
			break;
		case NVME_CODE_ABORTED_PWRLOSS:
			msg = "Abort on Power Loss";
			break;
		case NVME_CODE_INTERNAL:
			msg = "Internal Error";
			break;
		case NVME_CODE_ABORTED_ONREQ:
			msg = "Abort on Request";
			break;
		case NVME_CODE_ABORTED_SQDEL:
			msg = "Abort on SubQ Deletion";
			break;
		case NVME_CODE_ABORTED_FUSEFAIL:
			msg = "Abort on Fuse Failed";
			break;
		case NVME_CODE_ABORTED_FUSEMISSING:
			msg = "Abort on Fuse Missing";
			break;
		case NVME_CODE_BADNAMESPACE:
			msg = "Bad Namespace";
			break;
		case NVME_CODE_SEQERROR:
			msg = "Seq Error";
			break;
		case NVME_CODE_BADSGLSEG:
			msg = "Bad SGL Segment";
			break;
		case NVME_CODE_BADSGLCNT:
			msg = "Bad SGL Count";
			break;
		case NVME_CODE_BADSGLLEN:
			msg = "Bad SGL Length";
			break;
		case NVME_CODE_BADSGLMLEN:
			msg = "Bad SGL MLength";
			break;
		case NVME_CODE_BADSGLTYPE:
			msg = "Bad SGL Type";
			break;
		case NVME_CODE_BADMEMBUFUSE:
			msg = "Bad Memory Buffer Usage";
			break;
		case NVME_CODE_BADPRPOFF:
			msg = "Bad PRP Offset";
			break;
		case NVME_CODE_ATOMICWUOVFL:
			msg = "Atomic Write Overflow";
			break;
		case NVME_CODE_LBA_RANGE:
			msg = "LBA Out of Range";
			break;
		case NVME_CODE_CAP_EXCEEDED:
			msg = "Capacity Exceeded";
			break;
		case NVME_CODE_NAM_NOT_READY:
			msg = "Namespace not Ready";
			break;
		case NVME_CODE_RSV_CONFLICT:
			msg = "Reservation Conflict";
			break;
		case NVME_CODE_FMT_IN_PROG:
			msg = "Format in Progress";
			break;
		default:
			snprintf(buf, sizeof(buf), "generic(0x%02x)", code);
			break;
		}
		break;
	case NVME_STATUS_TYPE_SPECIFIC:
		switch(code) {
		case NVME_CSSCODE_BADCOMQ:
			msg = "Bad Completion Queue";
			break;
		case NVME_CSSCODE_BADQID:
			msg = "Bad Queue ID";
			break;
		case NVME_CSSCODE_BADQSIZE:
			msg = "Bad Queue Size";
			break;
		case NVME_CSSCODE_ABORTLIM:
			msg = "Too many Aborts";
			break;
		case NVME_CSSCODE_RESERVED04:
			msg = "reserved04";
			break;
		case NVME_CSSCODE_ASYNCEVENTLIM:
			msg = "Too many Async Events";
			break;
		case NVME_CSSCODE_BADFWSLOT:
			msg = "Bad Firmware Slot";
			break;
		case NVME_CSSCODE_BADFWIMAGE:
			msg = "Bad Firmware Image";
			break;
		case NVME_CSSCODE_BADINTRVECT:
			msg = "Bad Interrupt Vector";
			break;
		case NVME_CSSCODE_BADLOGPAGE:
			msg = "Unsupported Log Page";
			break;
		case NVME_CSSCODE_BADFORMAT:
			msg = "Bad Format Command";
			break;
		case NVME_CSSCODE_FW_NEEDSCONVRESET:
			msg = "Firmware Activation Needs Conventional Reset";
			break;
		case NVME_CSSCODE_BADQDELETE:
			msg = "Bad Queue Delete";
			break;
		case NVME_CSSCODE_FEAT_NOT_SAVEABLE:
			msg = "Feature not Saveable";
			break;
		case NVME_CSSCODE_FEAT_NOT_CHGABLE:
			msg = "Feature not Changeable";
			break;
		case NVME_CSSCODE_FEAT_NOT_NSSPEC:
			msg = "Feature not Namespace-Specific";
			break;
		case NVME_CSSCODE_FW_NEEDSSUBRESET:
			msg = "Firmware Activation Needs Subsystem Reset";
			break;
		case NVME_CSSCODE_FW_NEEDSRESET:
			msg = "Firmware Activation Needs Reset";
			break;
		case NVME_CSSCODE_FW_NEEDSMAXTVIOLATE:
			msg = "Firmware Activation Requires "
				"Maximum Time Violation";
			break;
		case NVME_CSSCODE_FW_PROHIBITED:
			msg = "Firmware Activation Prohibited";
			break;
		case NVME_CSSCODE_RANGE_OVERLAP:
			msg = "Overlapping Range";
			break;
		case NVME_CSSCODE_NAM_INSUFF_CAP:
			msg = "Insufficient Capacity";
			break;
		case NVME_CSSCODE_NAM_ID_UNAVAIL:
			msg = "NSID is not Available";
			break;
		case NVME_CSSCODE_RESERVED17:
			msg = "reserved17";
			break;
		case NVME_CSSCODE_NAM_ALREADY_ATT:
			msg = "NSID Already Attached";
			break;
		case NVME_CSSCODE_NAM_IS_PRIVATE:
			msg = "NSID is Private";
			break;
		case NVME_CSSCODE_NAM_NOT_ATT:
			msg = "NSID Not Attached";
			break;
		case NVME_CSSCODE_NO_THIN_PROVISION:
			msg = "This Provisioning not Supported";
			break;
		case NVME_CSSCODE_CTLR_LIST_INVALID:
			msg = "Controller List Invalid";
			break;
		case NVME_CSSCODE_ATTR_CONFLICT:
			msg = "Conflicting Attributes";
			break;
		case NVME_CSSCODE_BADPROTINFO:
			msg = "Invalid Prortection Information";
			break;
		case NVME_CSSCODE_WRITE_TO_RDONLY:
			msg = "Attempted Write to Read Only Range";
			break;
		default:
			snprintf(buf, sizeof(buf), "specific(0x%02x)", code);
			break;
		}
		break;
	case NVME_STATUS_TYPE_MEDIA:
		switch(code) {
		case NVME_MEDCODE_WRITE_FAULT:
			msg = "Write Fault";
			break;
		case NVME_MEDCODE_UNRECOV_READ_ERROR:
			msg = "Unrecoverable Read Error";
			break;
		case NVME_MEDCODE_ETOE_GUARD_CHK:
			msg = "End-to-End Guard Check Fail";
			break;
		case NVME_MEDCODE_ETOE_APPTAG_CHK:
			msg = "End-to-End Application Tag Check Error";
			break;
		case NVME_MEDCODE_ETOE_REFTAG_CHK:
			msg = "End-to-End Reference Tag Check Error";
			break;
		case NVME_MEDCODE_COMPARE_FAILURE:
			msg = "Compare Failure";
			break;
		case NVME_MEDCODE_ACCESS_DENIED:
			msg = "Access Denied";
			break;
		case NVME_MEDCODE_UNALLOCATED:
			msg = "Deallocated or Unwritten Logical Block";
			break;
		default:
			snprintf(buf, sizeof(buf), "media(0x%02x)", code);
			break;
		}
		break;
	case NVME_STATUS_TYPE_3:
	case NVME_STATUS_TYPE_4:
	case NVME_STATUS_TYPE_5:
	case NVME_STATUS_TYPE_6:
		buf[0] = 0;
		break;
	case NVME_STATUS_TYPE_VENDOR:
		snprintf(buf, sizeof(buf), "vendor(0x%02x)", code);
		break;
	}
	if (msg)
		snprintf(buf, sizeof(buf), "%s", msg);

	return buf;
}
