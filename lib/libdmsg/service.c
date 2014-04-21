/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

static void master_auth_signal(dmsg_iocom_t *iocom);
static void master_auth_rxmsg(dmsg_msg_t *msg);
static void master_link_signal(dmsg_iocom_t *iocom);
static void master_link_rxmsg(dmsg_msg_t *msg);

/*
 * Service an accepted connection (runs as a pthread)
 *
 * (also called from a couple of other places)
 */
void *
dmsg_master_service(void *data)
{
	dmsg_master_service_info_t *info = data;
	dmsg_iocom_t iocom;

	if (info->detachme)
		pthread_detach(pthread_self());

	dmsg_iocom_init(&iocom,
			info->fd,
			(info->altmsg_callback ? info->altfd : -1),
			master_auth_signal,
			master_auth_rxmsg,
			info->usrmsg_callback,
			info->altmsg_callback);
	iocom.node_handler = info->node_handler;
	if (info->noclosealt)
		iocom.flags &= ~DMSG_IOCOMF_CLOSEALT;
	if (info->label) {
		dmsg_iocom_label(&iocom, "%s", info->label);
		free(info->label);
		info->label = NULL;
	}
	dmsg_iocom_core(&iocom);
	dmsg_iocom_done(&iocom);

	fprintf(stderr,
		"iocom on fd %d terminated error rx=%d, tx=%d\n",
		info->fd, iocom.ioq_rx.error, iocom.ioq_tx.error);
	close(info->fd);
	info->fd = -1;	/* safety */
	if (info->exit_callback)
		info->exit_callback(info->handle);
	free(info);

	return (NULL);
}

/************************************************************************
 *			    AUTHENTICATION				*
 ************************************************************************
 *
 * Callback via dmsg_iocom_core().
 *
 * Additional messaging-based authentication must occur before normal
 * message operation.  The connection has already been encrypted at
 * this point.
 */
static void master_auth_conn_rx(dmsg_msg_t *msg);

static
void
master_auth_signal(dmsg_iocom_t *iocom)
{
	dmsg_msg_t *msg;

	/*
	 * Transmit LNK_CONN, enabling the SPAN protocol if both sides
	 * agree.
	 *
	 * XXX put additional authentication states here?
	 */
	msg = dmsg_msg_alloc(&iocom->circuit0, 0,
			     DMSG_LNK_CONN | DMSGF_CREATE,
			     master_auth_conn_rx, NULL);
	msg->any.lnk_conn.peer_mask = (uint64_t)-1;
	msg->any.lnk_conn.peer_type = DMSG_PEER_CLUSTER;
	msg->any.lnk_conn.pfs_mask = (uint64_t)-1;

	dmsg_msg_write(msg);

	dmsg_iocom_restate(iocom, master_link_signal, master_link_rxmsg);
}

static
void
master_auth_conn_rx(dmsg_msg_t *msg)
{
	if (msg->any.head.cmd & DMSGF_DELETE)
		dmsg_msg_reply(msg, 0);
}

static
void
master_auth_rxmsg(dmsg_msg_t *msg __unused)
{
}

/************************************************************************
 *			POST-AUTHENTICATION SERVICE MSGS		*
 ************************************************************************
 *
 * Callback via dmsg_iocom_core().
 */
static
void
master_link_signal(dmsg_iocom_t *iocom)
{
	dmsg_msg_lnk_signal(iocom);
}

static
void
master_link_rxmsg(dmsg_msg_t *msg)
{
	dmsg_state_t *state;
	uint32_t cmd;

	/*
	 * If the message state has a function established we just
	 * call the function, otherwise we call the appropriate
	 * link-level protocol related to the original command and
	 * let it sort it out.
	 *
	 * Non-transactional one-off messages, on the otherhand,
	 * might have REPLY set.
	 */
	state = msg->state;
	cmd = state ? state->icmd : msg->any.head.cmd;

	if (state && state->func) {
		assert(state->func != NULL);
		state->func(msg);
	} else {
		switch(cmd & DMSGF_PROTOS) {
		case DMSG_PROTO_LNK:
			dmsg_msg_lnk(msg);
			break;
		case DMSG_PROTO_DBG:
			dmsg_msg_dbg(msg);
			break;
		default:
			msg->iocom->usrmsg_callback(msg, 1);
			break;
		}
	}
}

/*
 * This is called from the master node to process a received debug
 * shell command.  We process the command, outputting the results,
 * then finish up by outputting another prompt.
 */
void
dmsg_msg_dbg(dmsg_msg_t *msg)
{
	switch(msg->tcmd & DMSGF_CMDSWMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * This is a command which we must process.
		 * When we are finished we generate a final reply.
		 */
		if (msg->aux_data)
			msg->aux_data[msg->aux_size - 1] = 0;
		msg->iocom->usrmsg_callback(msg, 0);
		dmsg_msg_reply(msg, 0);	/* XXX send prompt instead */
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		/*
		 * A reply just prints out the string.  No newline is added
		 * (it is expected to be embedded if desired).
		 */
		if (msg->aux_data)
			msg->aux_data[msg->aux_size - 1] = 0;
		if (msg->aux_data)
			write(2, msg->aux_data, strlen(msg->aux_data));
		break;
	default:
		msg->iocom->usrmsg_callback(msg, 1);
		break;
	}
}

/*
 * Returns text debug output to the original defined by (msg).  (msg) is
 * not modified and stays intact.  We use a one-way message with REPLY set
 * to distinguish between a debug command and debug terminal output.
 *
 * To prevent loops circuit_printf() can filter the message (cmd) related
 * to the circuit_printf().  We filter out DBG messages.
 */
void
dmsg_circuit_printf(dmsg_circuit_t *circuit, const char *ctl, ...)
{
	dmsg_msg_t *rmsg;
	va_list va;
	char buf[1024];
	size_t len;

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	va_end(va);
	len = strlen(buf) + 1;

	rmsg = dmsg_msg_alloc(circuit, len,
			      DMSG_DBG_SHELL | DMSGF_REPLY,
			      NULL, NULL);
	bcopy(buf, rmsg->aux_data, len);

	dmsg_msg_write(rmsg);
}
