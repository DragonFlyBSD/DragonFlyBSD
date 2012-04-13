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

#include "hammer2.h"

static void debug_recv(hammer2_iocom_t *iocom);
static void debug_send(hammer2_iocom_t *iocom);
static void debug_tty(hammer2_iocom_t *iocom);
static void hammer2_debug_parse(hammer2_iocom_t *iocom,
				hammer2_msg_t *msg, char *cmdbuf);

int
cmd_debug(void)
{
	struct sockaddr_in lsin;
	struct hammer2_iocom iocom;
	hammer2_msg_t *msg;
	int fd;

	/*
	 * Acquire socket and set options
	 */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "cmd_debug: socket(): %s\n",
			strerror(errno));
		return 1;
	}

	/*
	 * Connect to the target
	 */
	bzero(&lsin, sizeof(lsin));
	lsin.sin_family = AF_INET;
	lsin.sin_addr.s_addr = 0;
	lsin.sin_port = htons(HAMMER2_LISTEN_PORT);
	if (connect(fd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(fd);
		fprintf(stderr, "debug: connect failed: %s\n",
			strerror(errno));
		return 0;
	}

	/*
	 * Run the session.  The remote end transmits our prompt.
	 */
	hammer2_iocom_init(&iocom, fd, 0);
	printf("debug: connected\n");

	msg = hammer2_iocom_allocmsg(&iocom, HAMMER2_DBG_SHELL, 0);
	hammer2_ioq_write(&iocom, msg);

	hammer2_iocom_core(&iocom, debug_recv, debug_send, debug_tty);
	fprintf(stderr, "debug: disconnected\n");
	close(fd);
	return 0;
}

/*
 * Callback from hammer2_iocom_core() when messages might be present
 * on the socket.
 */
static
void
debug_recv(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;

	while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0 &&
	       (msg = hammer2_ioq_read(iocom)) != NULL) {
		switch(msg->any.head.cmd & HAMMER2_MSGF_CMDSWMASK) {
		case HAMMER2_LNK_ERROR:
			fprintf(stderr, "Link Error: %d\n",
				msg->any.head.error);
			break;
		case HAMMER2_DBG_SHELL:
			/*
			 * We send the commands, not accept them.
			 */
			hammer2_iocom_freemsg(iocom, msg);
			break;
		case HAMMER2_DBG_SHELL | HAMMER2_MSGF_REPLY:
			/*
			 * A reply from the remote is data we copy to stdout.
			 */
			if (msg->aux_size) {
				msg->aux_data[msg->aux_size - 1] = 0;
				write(1, msg->aux_data, strlen(msg->aux_data));
			}
			hammer2_iocom_freemsg(iocom, msg);
			break;
		default:
			assert((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0);
			fprintf(stderr, "Unknown message: %08x\n",
				msg->any.head.cmd);
			hammer2_ioq_reply_term(iocom, msg,
					       HAMMER2_MSG_ERR_UNKNOWN);
			break;
		}
	}
	if (iocom->ioq_rx.error) {
		fprintf(stderr, "node_master_recv: comm error %d\n",
			iocom->ioq_rx.error);
	}
}

/*
 * Callback from hammer2_iocom_core() when messages might be transmittable
 * to the socket.
 */
static
void
debug_send(hammer2_iocom_t *iocom)
{
	hammer2_ioq_write(iocom, NULL);
}

static
void
debug_tty(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;
	char buf[256];
	size_t len;

	if (fgets(buf, sizeof(buf), stdin) != NULL) {
		len = strlen(buf);
		if (len && buf[len - 1] == '\n')
			buf[--len] = 0;
		++len;
		msg = hammer2_iocom_allocmsg(iocom, HAMMER2_DBG_SHELL, len);
		bcopy(buf, msg->aux_data, len);
		hammer2_ioq_write(iocom, msg);
	} else {
		/*
		 * Set EOF flag without setting any error code for normal
		 * EOF.
		 */
		iocom->flags |= HAMMER2_IOCOMF_EOF;
	}
}

/*
 * This is called from the master node to process a received debug
 * shell command.  We process the command, outputting the results,
 * then finish up by outputting another prompt.
 */
void
hammer2_debug_remote(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	if (msg->aux_data)
		msg->aux_data[msg->aux_size - 1] = 0;
	if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
		/*
		 * A reply just prints out the string.  No newline is added
		 * (it is expected to be embedded if desired).
		 */
		if (msg->aux_data)
			write(2, msg->aux_data, strlen(msg->aux_data));
		hammer2_iocom_freemsg(iocom, msg);
	} else {
		/*
		 * Otherwise this is a command which we must process.
		 * When we are finished we generate a final reply.
		 */
		hammer2_debug_parse(iocom, msg, msg->aux_data);
		iocom_printf(iocom, msg, "debug> ");
		hammer2_iocom_freemsg(iocom, msg);
	}
}

static void
hammer2_debug_parse(hammer2_iocom_t *iocom, hammer2_msg_t *msg, char *cmdbuf)
{
	char *cmd = strsep(&cmdbuf, " \t");

	if (cmd == NULL || *cmd == 0) {
		;
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		iocom_printf(iocom, msg,
			     "help        Command help\n"
		);
	} else {
		iocom_printf(iocom, msg, "Unrecognized command: %s\n", cmd);
	}
}

void
iocom_printf(hammer2_iocom_t *iocom, hammer2_msg_t *msg, const char *ctl, ...)
{
	hammer2_msg_t *rmsg;
	va_list va;
	char buf[1024];
	size_t len;

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	va_end(va);
	len = strlen(buf) + 1;

	rmsg = hammer2_iocom_allocmsg(iocom, HAMMER2_DBG_SHELL, len);
	bcopy(buf, rmsg->aux_data, len);
	rmsg->any.head = msg->any.head;
	rmsg->any.head.aux_icrc = 0;

	hammer2_ioq_reply(iocom, rmsg);
}
