/*
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

#include "dmsg_local.h"

const char *
dmsg_basecmd_str(uint32_t cmd)
{
	static char buf[64];
	char protobuf[32];
	char cmdbuf[32];
	const char *protostr;
	const char *cmdstr;

	switch(cmd & DMSGF_PROTOS) {
	case DMSG_PROTO_LNK:
		protostr = "LNK_";
		break;
	case DMSG_PROTO_DBG:
		protostr = "DBG_";
		break;
	case DMSG_PROTO_HM2:
		protostr = "HM2_";
		break;
	case DMSG_PROTO_BLK:
		protostr = "BLK_";
		break;
	case DMSG_PROTO_VOP:
		protostr = "VOP_";
		break;
	default:
		snprintf(protobuf, sizeof(protobuf), "%x_",
			(cmd & DMSGF_PROTOS) >> 20);
		protostr = protobuf;
		break;
	}

	switch(cmd & (DMSGF_PROTOS |
		      DMSGF_CMDS |
		      DMSGF_SIZE)) {
	case DMSG_LNK_PAD:
		cmdstr = "PAD";
		break;
	case DMSG_LNK_PING:
		cmdstr = "PING";
		break;
	case DMSG_LNK_AUTH:
		cmdstr = "AUTH";
		break;
	case DMSG_LNK_CONN:
		cmdstr = "CONN";
		break;
	case DMSG_LNK_SPAN:
		cmdstr = "SPAN";
		break;
	case DMSG_LNK_ERROR:
		if (cmd & DMSGF_DELETE)
			cmdstr = "RETURN";
		else
			cmdstr = "RESULT";
		break;
	case DMSG_DBG_SHELL:
		cmdstr = "SHELL";
		break;
	default:
		snprintf(cmdbuf, sizeof(cmdbuf),
			 "%06x", (cmd & (DMSGF_PROTOS |
					 DMSGF_CMDS |
					 DMSGF_SIZE)));
		cmdstr = cmdbuf;
		break;
	}
	snprintf(buf, sizeof(buf), "%s%s", protostr, cmdstr);
	return (buf);
}

const char *
dmsg_msg_str(dmsg_msg_t *msg)
{
	dmsg_state_t *state;
	static char buf[256];
	char errbuf[16];
	char statebuf[64];
	char flagbuf[64];
	const char *statestr;
	const char *errstr;
	uint32_t basecmd;
	int i;

	/*
	 * Parse the state
	 */
	if ((state = msg->state) != NULL) {
		basecmd = (state->rxcmd & DMSGF_REPLY) ?
			  state->txcmd : state->rxcmd;
		snprintf(statebuf, sizeof(statebuf),
			 " %s=%s,L=%s%s,R=%s%s",
			 ((state->txcmd & DMSGF_REPLY) ?
				"rcvcmd" : "sndcmd"),
			 dmsg_basecmd_str(basecmd),
			 ((state->txcmd & DMSGF_CREATE) ? "C" : ""),
			 ((state->txcmd & DMSGF_DELETE) ? "D" : ""),
			 ((state->rxcmd & DMSGF_CREATE) ? "C" : ""),
			 ((state->rxcmd & DMSGF_DELETE) ? "D" : "")
		);
		statestr = statebuf;
	} else {
		statestr = "";
	}

	/*
	 * Parse the error
	 */
	switch(msg->any.head.error) {
	case 0:
		errstr = "";
		break;
	case DMSG_IOQ_ERROR_SYNC:
		errstr = "err=IOQ:NOSYNC";
		break;
	case DMSG_IOQ_ERROR_EOF:
		errstr = "err=IOQ:STREAMEOF";
		break;
	case DMSG_IOQ_ERROR_SOCK:
		errstr = "err=IOQ:SOCKERR";
		break;
	case DMSG_IOQ_ERROR_FIELD:
		errstr = "err=IOQ:BADFIELD";
		break;
	case DMSG_IOQ_ERROR_HCRC:
		errstr = "err=IOQ:BADHCRC";
		break;
	case DMSG_IOQ_ERROR_XCRC:
		errstr = "err=IOQ:BADXCRC";
		break;
	case DMSG_IOQ_ERROR_ACRC:
		errstr = "err=IOQ:BADACRC";
		break;
	case DMSG_IOQ_ERROR_STATE:
		errstr = "err=IOQ:BADSTATE";
		break;
	case DMSG_IOQ_ERROR_NOPEER:
		errstr = "err=IOQ:PEERCONFIG";
		break;
	case DMSG_IOQ_ERROR_NORKEY:
		errstr = "err=IOQ:BADRKEY";
		break;
	case DMSG_IOQ_ERROR_NOLKEY:
		errstr = "err=IOQ:BADLKEY";
		break;
	case DMSG_IOQ_ERROR_KEYXCHGFAIL:
		errstr = "err=IOQ:BADKEYXCHG";
		break;
	case DMSG_IOQ_ERROR_KEYFMT:
		errstr = "err=IOQ:BADFMT";
		break;
	case DMSG_IOQ_ERROR_BADURANDOM:
		errstr = "err=IOQ:BADRANDOM";
		break;
	case DMSG_IOQ_ERROR_MSGSEQ:
		errstr = "err=IOQ:BADSEQ";
		break;
	case DMSG_IOQ_ERROR_EALREADY:
		errstr = "err=IOQ:DUPMSG";
		break;
	case DMSG_IOQ_ERROR_TRANS:
		errstr = "err=IOQ:BADTRANS";
		break;
	case DMSG_IOQ_ERROR_IVWRAP:
		errstr = "err=IOQ:IVWRAP";
		break;
	case DMSG_IOQ_ERROR_MACFAIL:
		errstr = "err=IOQ:MACFAIL";
		break;
	case DMSG_IOQ_ERROR_ALGO:
		errstr = "err=IOQ:ALGOFAIL";
		break;
	case DMSG_ERR_NOSUPP:
		errstr = "err=NOSUPPORT";
		break;
	default:
		snprintf(errbuf, sizeof(errbuf),
			 " err=%d", msg->any.head.error);
		errstr = errbuf;
		break;
	}

	/*
	 * Message flags
	 */
	i = 0;
	if (msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				 DMSGF_ABORT | DMSGF_REPLY)) {
		flagbuf[i++] = '|';
		if (msg->any.head.cmd & DMSGF_CREATE)
			flagbuf[i++] = 'C';
		if (msg->any.head.cmd & DMSGF_DELETE)
			flagbuf[i++] = 'D';
		if (msg->any.head.cmd & DMSGF_REPLY)
			flagbuf[i++] = 'R';
		if (msg->any.head.cmd & DMSGF_ABORT)
			flagbuf[i++] = 'A';
	}
	flagbuf[i] = 0;

	/*
	 * Generate the buf
	 */
	snprintf(buf, sizeof(buf),
		"msg=%s%s %s %s hcrc=%08x id=%016jx",
		 dmsg_basecmd_str(msg->any.head.cmd),
		 flagbuf,
		 errstr,
		 statestr,
		 msg->any.head.hdr_crc,
		 msg->any.head.msgid);

	return(buf);
}
