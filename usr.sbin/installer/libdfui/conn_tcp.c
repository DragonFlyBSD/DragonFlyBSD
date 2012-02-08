/*
 *
 * Copyright (c) 2004 Scott Ullrich <GeekGod@GeekGod.com>
 * Portions Copyright (c) 2004 Chris Pressey <cpressey@catseye.mine.nu>
 *
 * Copyright (c) 2004 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Scott Ullrich and Chris Pressey (see above for e-mail addresses).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS, CONTRIBUTORS OR VOICES IN THE AUTHOR'S HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
 * conn_tcp.c
 * $Id: conn_tcp.c,v 1.16 2005/02/06 19:53:19 cpressey Exp $
 */

#include "system.h"
#ifdef HAS_TCP

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libaura/buffer.h>

#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "encoding.h"
#include "conn_tcp.h"
#include "dump.h"

/***** BACKEND ******/

/** High Level **/

/*
 * Connect to the frontend.
 */
dfui_err_t
dfui_tcp_be_start(struct dfui_connection *c)
{
	struct sockaddr_in servaddr;
	int server_port;
	int tru = 1;

	server_port = atoi(c->rendezvous);

	/*
	 * Create the tcp socket
	 */
	errno = 0;
	if ((T_TCP(c)->listen_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return(DFUI_FAILURE);
	dfui_debug("LISTEN_SOCKET<<%d>>\n", T_TCP(c)->listen_sd);

	if (setsockopt(T_TCP(c)->listen_sd, SOL_SOCKET, SO_REUSEADDR,
	    &tru, sizeof(tru)) == -1) {
		return(DFUI_FAILURE);
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(server_port);
	switch(inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr)) {
	case 0:
		warnx("inet_pton(): address not parseable");
		return(DFUI_FAILURE);
	case 1:
		break;
	default:
		warn("inet_pton()");
		return(DFUI_FAILURE);
	}

	if (bind(T_TCP(c)->listen_sd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		warn("bind()");
		return(DFUI_FAILURE);
	}
	dfui_debug("BOUND_ON<<%d>>\n", T_TCP(c)->listen_sd);
	if (listen(T_TCP(c)->listen_sd, 0) == -1)
		return(DFUI_FAILURE);
	dfui_debug("LISTENING_ON<<%d>>\n", T_TCP(c)->listen_sd);
	/* at this point we should be listening on the rendezvous port */
	return(DFUI_SUCCESS);
}

/*
 * Tell the frontend that we're done and disconnect from it.
 */
dfui_err_t
dfui_tcp_be_stop(struct dfui_connection *c)
{
	if (dfui_tcp_be_ll_exchange(c, DFUI_BE_MSG_STOP, "")) {
		close(T_TCP(c)->listen_sd);
		close(T_TCP(c)->connected_sd);
		fclose(T_TCP(c)->stream);
		return(DFUI_SUCCESS);
	} else
		return(DFUI_FAILURE);
}

/** Low Level **/

/*
 * Exchange a message with the frontend.  This involves two receive()/reply()
 * cycles: one to provide our message, one to get a reply from the frontend.
 *
 * Note that this does not immediately send the message to the frontend -
 * it can't, because we're a service and it's a client.  What it does is
 * keep the message handy and wait for a frontend request to come in.  It
 * then replies to that request with our message.
 *
 * The protocol looks something like the following, using the PRESENT and
 * SUBMIT exchange as an example:
 *
 * frontend (client) | backend (service)
 * ------------------+------------------
 *
 *                                     [stage 1]
 * READY            -->                ll_receive()
 *                 <--  PRESENT(form)  ll_reply()
 *
 *                                     [stage 2]
 * SUBMIT(form)     -->                ll_receive()
 *                 <--  READY          ll_reply()
 *
 * Each of those exchanges is a pair of calls, on our end, to
 * dfui_tcp_be_ll_receive() and dfui_npipe_be_ll_reply().
 *
 * The set of messages that the client can pass us is determined by
 * the conversation state:
 *
 *   o  In stage 1, only READY and ABORT are meaningful.
 *   o  After a PRESENT, the messages SUBMIT and ABORT are meaningul
 *      in stage 2.
 *   o  During a PROG_*, the messages CONTINUE, CANCEL, and ABORT
 *      are meaningful in stage 2.
 *
 * If the frontend sends us with READY in stage 2, we assume it has
 * fallen out of sync, so we send the same initial reply again, going
 * back to stage 1 as it were.
 *
 * After this call, the message is available in c->ebuf.
 */
dfui_err_t
dfui_tcp_be_ll_exchange(struct dfui_connection *c, char msgtype, const char *msg)
{
	char *fmsg;

	/*
	 * Construct our message to send.
	 */

	fmsg = malloc(strlen(msg) + 2);
	fmsg[0] = msgtype;
	strcpy(fmsg + 1, msg);

	/*
	 * Get the frontend's message.
	 */

	dfui_tcp_be_ll_receive(c);

	/*
	 * Frontend message should have been either READY or ABORT.
	 * If ABORT, we get out of here pronto.
	 */

	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		free(fmsg);
		return(DFUI_FAILURE);
	}

	/* XXX if (!READY) ??? */

	do {
		dfui_tcp_be_ll_reply(c, fmsg);

		/*
		 * Here, the frontend has picked up our request and is
		 * processing it.  We have to wait for the response.
		 */

		dfui_tcp_be_ll_receive(c);

		/*
		 * Did we get READY from this?
		 * If so, loop!
		 */

	} while (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_READY);

	fmsg[0] = DFUI_BE_MSG_READY;
	fmsg[1] = '\0';
	dfui_tcp_be_ll_reply(c, fmsg);

	free(fmsg);
	return(DFUI_SUCCESS);
}

/*
 * Receive a message from the frontend.
 * This call is synchronous.
 * After this call, the NUL-terminated message is available in
 * c->ebuf.
 */
dfui_err_t
dfui_tcp_be_ll_receive(struct dfui_connection *c)
{
	int length;
	char *buf;

	top:

	if (!T_TCP(c)->is_connected) {
	dfui_debug("NOT_CONNECTED,ACCEPTING_ON<<%d>>\n", T_TCP(c)->listen_sd);
		T_TCP(c)->connected_sd = accept(T_TCP(c)->listen_sd, NULL, NULL);
		dfui_debug("ACCEPTED<<%d>>\n", T_TCP(c)->connected_sd);
		T_TCP(c)->stream = fdopen(T_TCP(c)->connected_sd, "r+");
		T_TCP(c)->is_connected = 1;
	} else {
		dfui_debug("ALREADY_CONNECTED<<>>\n");
	}

	dfui_debug("WAITING<<>>\n");

	if (read_data(T_TCP(c)->stream, (char *)&length, sizeof(length)) == -1) {
		dfui_debug("LOST_THEM<<>>\n");
		fclose(T_TCP(c)->stream);
		T_TCP(c)->is_connected = 0;
		goto top;
	}

	buf = malloc(length + 1);
	if (read_data(T_TCP(c)->stream, buf, length) == -1) {
		dfui_debug("LOST_THEM<<>>\n");
		fclose(T_TCP(c)->stream);
		T_TCP(c)->is_connected = 0;
		goto top;
	}

	aura_buffer_set(c->ebuf, buf, length);
	free(buf);

	dfui_debug("RECEIVED<<%s>>\n", aura_buffer_buf(c->ebuf));

	return(DFUI_SUCCESS);
}

/*
 * Send a NUL-terminated reply to the frontend.
 */
dfui_err_t
dfui_tcp_be_ll_reply(struct dfui_connection *c, const char *fmsg)
{
	int length;

	dfui_debug("SEND<<%s>>\n", fmsg);
	length = strlen(fmsg);
	write_data(T_TCP(c)->stream, (char *)&length, sizeof(length));
	write_data(T_TCP(c)->stream, fmsg, length);

	return(DFUI_SUCCESS);
}

/******** FRONTEND ********/

/** High Level **/

dfui_err_t
dfui_tcp_fe_connect(struct dfui_connection *c)
{
        struct sockaddr_in servaddr;
        int server_port;
	int connected = 0;

        server_port = atoi(c->rendezvous);

        /*
         * Create the tcp socket
         */
	while (!connected) {
		errno = 0;
		if ((T_TCP(c)->connected_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			return(DFUI_FAILURE);
		}

		dfui_debug("CLIENT_SOCKET<<%d>>\n", T_TCP(c)->connected_sd);
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(server_port);
		inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);

		if (connect(T_TCP(c)->connected_sd, (struct sockaddr *)&servaddr,
		    sizeof(servaddr)) == 0) {
			dfui_debug("CONNECTED<<>>\n");
			connected = 1;
		} else {
			dfui_debug("NO_CONNECT<<>>\n");
			close(T_TCP(c)->connected_sd);
			sleep(1);
		}
	}

        /* at this point we should be connected */

	T_TCP(c)->stream = fdopen(T_TCP(c)->connected_sd, "r+");

        return(DFUI_SUCCESS);
}

dfui_err_t
dfui_tcp_fe_disconnect(struct dfui_connection *c)
{
	close(T_TCP(c)->connected_sd);
	return(DFUI_SUCCESS);
}

/** Low Level **/

/*
 * Ask for, and subsequently receieve, a message from the backend.
 * msgtype should be one of the DFUI_FE_MSG_* constants.
 * This call is synchronous.
 * After this call, the null-terminated, encoded message is
 * available in T_TCP(c)->buf.
 */
dfui_err_t
dfui_tcp_fe_ll_request(struct dfui_connection *c, char msgtype, const char *msg)
{
	char *fmsg, *buf;
	int length, result;

	/*
	 * First, assert that the connection is open.
	 */

	if (c == NULL || T_TCP(c)->connected_sd == -1)
		return(DFUI_FAILURE);

	/*
	 * Construct a message.
	 */

	fmsg = malloc(strlen(msg) + 2);
	fmsg[0] = msgtype;
	strcpy(fmsg + 1, msg);
	dfui_debug("SEND<<%s>>\n", fmsg);

	/*
	 * Send a NUL-terminated message to the backend.
	 */

        length = strlen(fmsg);
        result = write_data(T_TCP(c)->stream, (char *)&length, sizeof(length));
	dfui_debug("result<<%d>>\n", result);
	result = write_data(T_TCP(c)->stream, (char *)fmsg, length);
	dfui_debug("result<<%d>>\n", result);

	/*
	 * Receive a reply from the backend.
	 * If our message was a READY, this should be a message like PRESENT.
	 * Otherwise it should simply be a READY.
	 */

	dfui_debug("WAITING<<>>\n");
        result = read_data(T_TCP(c)->stream, (char *)&length, sizeof(length));
	dfui_debug("result<<%d>>\n", result);
        buf = malloc(length + 1);
        result = read_data(T_TCP(c)->stream, buf, length);
	dfui_debug("result<<%d>>\n", result);
        aura_buffer_set(c->ebuf, buf, length);
        free(buf);

	dfui_debug("RECV<<%s>>\n", aura_buffer_buf(c->ebuf));

	free(fmsg);

	return(DFUI_SUCCESS);
}

int
read_data(FILE *f, char *buf, int n)
{
	int bcount;	/* counts bytes read */
	int br;		/* bytes read this pass */

	bcount = 0;
	br = 0;
	while (bcount < n) {
		if ((br = fread(buf, 1, n - bcount, f)) > 0) {
			dfui_debug("READ_BYTES<<%d>>\n", br);
			bcount += br;
			buf += br;
		} else if (br <= 0) {
			dfui_debug("read_data_error<<%d>>\n", br);
			return(-1);
		}
	}
	return(bcount);
}

int
write_data(FILE *f, const char *buf, int n)
{
        int bcount;	/* counts bytes written */
        int bw;		/* bytes written this pass */

        bcount = 0;
        bw = 0;
        while (bcount < n) {
                if ((bw = fwrite(buf, 1, n - bcount, f)) > 0) {
			dfui_debug("WROTE_BYTES<<%d>>\n", bw);
                        bcount += bw;
                        buf += bw;
                } else if (bw <= 0) {
			dfui_debug("write_data_error<<%d>>\n", bw);
			return(-1);
		}
        }
        return(bcount);
}

#endif /* HAS_TCP */
