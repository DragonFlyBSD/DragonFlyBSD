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

static void *master_accept(void *data);
static void *master_service(void *data);
static void master_auth_rx(hammer2_iocom_t *iocom);
static void master_auth_tx(hammer2_iocom_t *iocom);
static void master_link_rx(hammer2_iocom_t *iocom);
static void master_link_tx(hammer2_iocom_t *iocom);

/*
 * Start-up the master listener daemon for the machine.
 *
 * The master listener serves as a rendezvous point in the cluster, accepting
 * connections, performing registrations and authentications, maintaining
 * the spanning tree, and keeping track of message state so disconnects can
 * be handled properly.
 *
 * Once authenticated only low-level messaging protocols (which includes
 * tracking persistent messages) are handled by this daemon.  This daemon
 * does not run the higher level quorum or locking protocols.
 *
 * This daemon can also be told to maintain connections to other nodes,
 * forming a messaging backbone, which in turn allows PFS's (if desired) to
 * simply connect to the master daemon via localhost if desired.
 * Backbones are specified via /etc/hammer2.conf.
 */
int
cmd_service(void)
{
	struct sockaddr_in lsin;
	int on;
	int lfd;

	/*
	 * Acquire socket and set options
	 */
	if ((lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "master_listen: socket(): %s\n",
			strerror(errno));
		return 1;
	}
	on = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	/*
	 * Setup listen port and try to bind.  If the bind fails we assume
	 * that a master listener process is already running and silently
	 * fail.
	 */
	bzero(&lsin, sizeof(lsin));
	lsin.sin_family = AF_INET;
	lsin.sin_addr.s_addr = INADDR_ANY;
	lsin.sin_port = htons(HAMMER2_LISTEN_PORT);
	if (bind(lfd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(lfd);
		if (QuietOpt == 0) {
			fprintf(stderr,
				"master listen: daemon already running\n");
		}
		return 0;
	}
	if (QuietOpt == 0)
		fprintf(stderr, "master listen: startup\n");
	listen(lfd, 50);

	/*
	 * Fork and disconnect the controlling terminal and parent process,
	 * executing the specified function as a pthread.
	 *
	 * Returns to the original process which can then continue running.
	 * In debug mode this call will create the pthread without forking
	 * and set NormalExit to 0, instead of fork.
	 */
	hammer2_demon(master_accept, (void *)(intptr_t)lfd);
	if (NormalExit)
		close(lfd);
	return 0;
}

/*
 * Master listen/accept thread.  Accept connections on the master socket,
 * starting a pthread for each one.
 */
static
void *
master_accept(void *data)
{
	struct sockaddr_in asin;
	socklen_t alen;
	pthread_t thread;
	int lfd = (int)(intptr_t)data;
	int fd;

	/*
	 * Nobody waits for us
	 */
	setproctitle("hammer2 master listen");
	pthread_detach(pthread_self());

	/*
	 * Accept connections and create pthreads to handle them after
	 * validating the IP.
	 */
	for (;;) {
		alen = sizeof(asin);
		fd = accept(lfd, (struct sockaddr *)&asin, &alen);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		thread = NULL;
		fprintf(stderr, "master_accept: accept fd %d\n", fd);
		pthread_create(&thread, NULL,
			       master_service, (void *)(intptr_t)fd);
	}
	return (NULL);
}

/*
 * Service an accepted connection (runs as a pthread)
 */
static
void *
master_service(void *data)
{
	hammer2_iocom_t iocom;
	int fd;

	fd = (int)(intptr_t)data;
	hammer2_iocom_init(&iocom, fd, -1);
	hammer2_iocom_core(&iocom, master_auth_rx, master_auth_tx, NULL);

	fprintf(stderr,
		"iocom on fd %d terminated error rx=%d, tx=%d\n",
		fd, iocom.ioq_rx.error, iocom.ioq_tx.error);
	close(fd);

	return (NULL);
}

/************************************************************************
 *			    AUTHENTICATION				*
 ************************************************************************
 *
 * Additional messaging-based authentication must occur before normal
 * message operation.  The connection has already been encrypted at
 * this point.
 */
static
void
master_auth_rx(hammer2_iocom_t *iocom __unused)
{
	printf("AUTHRX\n");
	iocom->recvmsg_callback = master_link_rx;
	iocom->sendmsg_callback = master_link_tx;
}

static
void
master_auth_tx(hammer2_iocom_t *iocom __unused)
{
	printf("AUTHTX\n");
	iocom->recvmsg_callback = master_link_rx;
	iocom->sendmsg_callback = master_link_tx;
}

/*
 * Callback from hammer2_iocom_core() when messages might be present
 * on the socket.
 */
static
void
master_link_rx(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;

	while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0 &&
	       (msg = hammer2_ioq_read(iocom)) != NULL) {
		fprintf(stderr, "MSG RECEIVED: %08x error %d\n",
			msg->any.head.cmd, msg->any.head.error);
		switch(msg->any.head.cmd & HAMMER2_MSGF_CMDSWMASK) {
		case HAMMER2_LNK_ERROR:
			break;
		case HAMMER2_DBG_SHELL:
		case HAMMER2_DBG_SHELL | HAMMER2_MSGF_REPLY:
			hammer2_shell_remote(iocom, msg);
			break;
		default:
			hammer2_msg_reply(iocom, msg, HAMMER2_MSG_ERR_UNKNOWN);
			break;
		}
		hammer2_state_cleanuprx(iocom, msg);
	}
	if (iocom->ioq_rx.error) {
		fprintf(stderr,
			"master_recv: comm error %d\n",
			iocom->ioq_rx.error);
	}
}

/*
 * Callback from hammer2_iocom_core() when messages might be transmittable
 * to the socket.
 */
static
void
master_link_tx(hammer2_iocom_t *iocom)
{
	hammer2_iocom_flush(iocom);
}
