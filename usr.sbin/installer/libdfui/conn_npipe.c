/*
 * Copyright (c)2004 Cat's Eye Technologies.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Cat's Eye Technologies nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * conn_npipe.c
 * $Id: conn_npipe.c,v 1.13 2005/02/06 19:53:19 cpressey Exp $
 */

#include "system.h"
#ifdef HAS_NPIPE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/errno.h>

#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libaura/buffer.h>
#include <libaura/fspred.h>

#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "encoding.h"
#include "dump.h"
#include "conn_npipe.h"

/***** BACKEND ******/

/** High Level **/

/*
 * Connect to the frontend.
 */
dfui_err_t
dfui_npipe_be_start(struct dfui_connection *c)
{
	asprintf(&T_NPIPE(c)->out_pipename, "/tmp/dfui.%s.to_fe", c->rendezvous);
	asprintf(&T_NPIPE(c)->in_pipename, "/tmp/dfui.%s.from_fe", c->rendezvous);

	/*
	 * Create the named pipes.
	 */
	errno = 0;
	if (mkfifo(T_NPIPE(c)->in_pipename, 0600) < 0) {
		if (errno != EEXIST) {
			warn("mkfifo (to_be)");
			return(DFUI_FAILURE);
		}
	}
	errno = 0;
	if (mkfifo(T_NPIPE(c)->out_pipename, 0600) < 0) {
		if (errno != EEXIST) {
			warn("mkfifo (to_fe)");
			return(DFUI_FAILURE);
		}
	}
	dfui_debug("opening pipes...\n");
	if ((T_NPIPE(c)->out = fopen(T_NPIPE(c)->out_pipename, "w")) == NULL) {
		return(DFUI_FAILURE);
	}
	dfui_debug("opened to_fe pipe\n");
	setvbuf(T_NPIPE(c)->out, NULL, _IONBF, 0);
	if ((T_NPIPE(c)->in = fopen(T_NPIPE(c)->in_pipename, "r")) == NULL) {
		fclose(T_NPIPE(c)->out);
		return(DFUI_FAILURE);
	}
	dfui_debug("opened to_be pipe\n");
	return(DFUI_SUCCESS);
}

/*
 * Tell the frontend that we're done and disconnect from it.
 */
dfui_err_t
dfui_npipe_be_stop(struct dfui_connection *c)
{
	if (dfui_npipe_be_ll_exchange(c, DFUI_BE_MSG_STOP, "")) {
		fclose(T_NPIPE(c)->in);
		fclose(T_NPIPE(c)->out);
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
 * dfui_npipe_be_ll_receive() and dfui_npipe_be_ll_reply().
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
dfui_npipe_be_ll_exchange(struct dfui_connection *c, char msgtype, const char *msg)
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

	dfui_npipe_be_ll_receive(c);

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
		dfui_npipe_be_ll_reply(c, fmsg);

		/*
		 * Here, the frontend has picked up our request and is
		 * processing it.  We have to wait for the response.
		 */

		dfui_npipe_be_ll_receive(c);

		/*
		 * Did we get READY from this?
		 * If so, loop!
		 */

	} while (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_READY);

	fmsg[0] = DFUI_BE_MSG_READY;
	fmsg[1] = '\0';
	dfui_npipe_be_ll_reply(c, fmsg);

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
dfui_npipe_be_ll_receive(struct dfui_connection *c)
{
	int length;
	char *buf;

	dfui_debug("WAITING<<>>\n");

	fread(&length, 4, 1, T_NPIPE(c)->in);

	dfui_debug("LENGTH<<%d>>\n", length);

	buf = malloc(length + 1);
	fread(buf, length, 1, T_NPIPE(c)->in);
	aura_buffer_set(c->ebuf, buf, length);
	free(buf);

	dfui_debug("RECEIVED<<%s>>\n", aura_buffer_buf(c->ebuf));

	return(DFUI_SUCCESS);
}

/*
 * Send a NUL-terminated reply to the frontend.
 */
dfui_err_t
dfui_npipe_be_ll_reply(struct dfui_connection *c, const char *fmsg)
{
	int length;

	dfui_debug("SEND<<%s>>\n", fmsg);

	length = strlen(fmsg);

	fwrite(&length, 4, 1, T_NPIPE(c)->out);
	fwrite(fmsg, length, 1, T_NPIPE(c)->out);

	return(DFUI_SUCCESS);
}

/******** FRONTEND ********/

/** High Level **/

dfui_err_t
dfui_npipe_fe_connect(struct dfui_connection *c)
{
	asprintf(&T_NPIPE(c)->in_pipename, "/tmp/dfui.%s.to_fe", c->rendezvous);
	asprintf(&T_NPIPE(c)->out_pipename, "/tmp/dfui.%s.from_fe", c->rendezvous);

	dfui_debug("waiting for named pipes...\n");

	/*
	 * Wait for named pipes to be created.
	 */
	if (!is_named_pipe("%s", T_NPIPE(c)->in_pipename)) {
		while (!is_named_pipe("%s", T_NPIPE(c)->in_pipename)) {
			sleep(1);
		}
		sleep(1);
	}

	dfui_debug("opening inflow pipe...\n");

	if ((T_NPIPE(c)->in = fopen(T_NPIPE(c)->in_pipename, "r")) == NULL) {
		return(DFUI_FAILURE);
	}

	dfui_debug("opening outflow pipe...\n");

	if ((T_NPIPE(c)->out = fopen(T_NPIPE(c)->out_pipename, "w")) == NULL) {
		fclose(T_NPIPE(c)->in);
		return(DFUI_FAILURE);
	}

	dfui_debug("making outflow pipe raw...\n");

	setvbuf(T_NPIPE(c)->out, NULL, _IONBF, 0);
	return(DFUI_SUCCESS);
}

dfui_err_t
dfui_npipe_fe_disconnect(struct dfui_connection *c)
{
	fclose(T_NPIPE(c)->in);
	fclose(T_NPIPE(c)->out);
	return(DFUI_SUCCESS);
}

/** Low Level **/

/*
 * Ask for, and subsequently receieve, a message from the backend.
 * msgtype should be one of the DFUI_FE_MSG_* constants.
 * This call is synchronous.
 * After this call, the null-terminated, encoded message is
 * available in T_NPIPE(c)->buf.
 */
dfui_err_t
dfui_npipe_fe_ll_request(struct dfui_connection *c, char msgtype, const char *msg)
{
	char *fmsg, *buf;
	int length;

	/*
	 * First, assert that the connection is open.
	 */

	if (c == NULL || T_NPIPE(c)->in == NULL || T_NPIPE(c)->out == NULL)
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
	fwrite(&length, 4, 1, T_NPIPE(c)->out);
	fwrite(fmsg, length, 1, T_NPIPE(c)->out);

	/*
	 * Receive a reply from the backend.
	 * If our message was a READY, this should be a message like PRESENT.
	 * Otherwise it should simply be a READY.
	 */

	dfui_debug("WAITING<<>>\n");

	fread(&length, 4, 1, T_NPIPE(c)->in);
	buf = malloc(length + 1);
	fread(buf, length, 1, T_NPIPE(c)->in);
	aura_buffer_set(c->ebuf, buf, length);
	free(buf);

	dfui_debug("RECV<<%s>>\n", aura_buffer_buf(c->ebuf));

	free(fmsg);

	return(DFUI_SUCCESS);
}

#endif /* HAS_NPIPE */
