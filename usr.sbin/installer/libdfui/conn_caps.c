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
 * conn_caps.c
 * $Id: conn_caps.c,v 1.12 2005/02/06 19:53:19 cpressey Exp $
 * This code was derived in part from:
 * $_DragonFly: src/test/caps/client.c,v 1.3 2004/03/31 20:27:34 dillon Exp $
 * $_DragonFly: src/test/caps/server.c,v 1.4 2004/03/06 22:15:00 dillon Exp $
 * and is therefore also subject to the license conditions on those files.
 */

#include "system.h"
#ifdef HAS_CAPS

#include <sys/types.h>
#include <sys/time.h>
#include <sys/caps.h>
#include <sys/errno.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libaura/mem.h>
#include <libaura/buffer.h>

#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "encoding.h"
#include "dump.h"
#include "conn_caps.h"

/***** BACKEND ******/

/** High Level **/

/*
 * Connect to the frontend.
 */
dfui_err_t
dfui_caps_be_start(struct dfui_connection *c)
{
	T_CAPS(c)->cid = caps_sys_service(c->rendezvous, getuid(), getgid(),
				  0, CAPF_ANYCLIENT);
	return(T_CAPS(c)->cid < 0 ? DFUI_FAILURE : DFUI_SUCCESS);
}

/*
 * Tell the frontend that we're done and disconnect from it.
 */
dfui_err_t
dfui_caps_be_stop(struct dfui_connection *c)
{
	if (dfui_caps_be_ll_exchange(c, DFUI_BE_MSG_STOP, "")) {
		caps_sys_close(T_CAPS(c)->cid);
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
 * dfui_caps_be_ll_receive() and dfui_caps_be_ll_reply().
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
dfui_caps_be_ll_exchange(struct dfui_connection *c, char msgtype, const char *msg)
{
	char *fmsg;

	/*
	 * Construct our message to send.
	 */

	fmsg = aura_malloc(strlen(msg) + 2, "exchange message");
	fmsg[0] = msgtype;
	strcpy(fmsg + 1, msg);

	/*
	 * Get the frontend's message.
	 */

	dfui_caps_be_ll_receive(c);

	/*
	 * Frontend message should have been either READY or ABORT.
	 * If ABORT, we get out of here pronto.
	 */

	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		aura_free(fmsg, "exchange message");
		return(DFUI_FAILURE);
	}

	/* XXX if (!READY) ??? */

	do {
		dfui_caps_be_ll_reply(c, fmsg);

		/*
		 * Here, the frontend has picked up our request and is
		 * processing it.  We have to wait for the response.
		 */

		dfui_caps_be_ll_receive(c);

		/*
		 * Did we get READY from this?
		 * If so, loop!
		 */

	} while (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_READY);

	fmsg[0] = DFUI_BE_MSG_READY;
	fmsg[1] = '\0';
	dfui_caps_be_ll_reply(c, fmsg);

	aura_free(fmsg, "exchange message");
	return(DFUI_SUCCESS);
}

/*
 * Receive a message from the frontend.
 * This call is synchronous.
 * After this call, the message is available in c->ebuf.
 */
dfui_err_t
dfui_caps_be_ll_receive(struct dfui_connection *c)
{
	/*
	 * XXX Eventually, the message should be received directly
	 * into c->ebuf.  For now, receive into T_CAPS(c)->buf,
	 * and copy into c->ebuf.
	 */
	do {
		T_CAPS(c)->wresult = caps_sys_wait(T_CAPS(c)->cid, T_CAPS(c)->buf,
		    T_CAPS(c)->size, &T_CAPS(c)->msgid, NULL);
		if (T_CAPS(c)->wresult < 0)
			return(DFUI_FAILURE);
		/*
		 * This might have been a CAPMS_DISPOSE message from the kernel.
		 * If so, just accept it and try, try again.
		 */
		dfui_debug("DISPOSE?<<%s>>\n",
		    T_CAPS(c)->msgid.c_state == CAPMS_DISPOSE ? "Yes" : "No");
	} while (T_CAPS(c)->msgid.c_state == CAPMS_DISPOSE);

	aura_buffer_set(c->ebuf, T_CAPS(c)->buf, T_CAPS(c)->wresult);
	dfui_debug("RECV<<%s>>\n", aura_buffer_buf(c->ebuf));
	return(DFUI_SUCCESS);
}

/*
 * Send a NUL-terminated reply to the frontend.
 */
dfui_err_t
dfui_caps_be_ll_reply(struct dfui_connection *c, const char *fmsg)
{
	dfui_debug("SEND<<%s>>\n", fmsg);
	T_CAPS(c)->wresult = caps_sys_reply(T_CAPS(c)->cid,
	    __DECONST(char *, fmsg), strlen(fmsg), T_CAPS(c)->msgid.c_id);

	/*
	 * We may get a CAPMS_DISPOSE message after this, if the client
	 * process is still around.  If so, it'll be handled in the next
	 * call to dfui_caps_be_ll_receive().
	 */

	return(DFUI_SUCCESS);
}

/******** FRONTEND ********/

/** High Level **/

dfui_err_t
dfui_caps_fe_connect(struct dfui_connection *c)
{
	T_CAPS(c)->cid = caps_sys_client(c->rendezvous, getuid(), getgid(), 0,
	    CAPF_ANYCLIENT | CAPF_WAITSVC);
	return(T_CAPS(c)->cid > 0 ? DFUI_SUCCESS : DFUI_FAILURE);
}

dfui_err_t
dfui_caps_fe_disconnect(struct dfui_connection *c)
{
	caps_sys_close(T_CAPS(c)->cid);
	return(DFUI_SUCCESS);
}

/** Low Level **/

/*
 * Ask for, and subsequently receieve, a message from the backend.
 * msgtype should be one of the DFUI_FE_MSG_* constants.
 * This call is synchronous.
 * After this call, the encoded message is available in c->ebuf.
 */
dfui_err_t
dfui_caps_fe_ll_request(struct dfui_connection *c, char msgtype, const char *msg)
{
	char *fmsg;
	off_t msgcid = 0;

	/*
	 * First, assert that the connection is open.
	 */

	if (c == NULL || T_CAPS(c)->cid < 0)
		return(DFUI_FAILURE);

	/*
	 * Construct a message.
	 */

	fmsg = aura_malloc(strlen(msg) + 2, "exchange message");
	fmsg[0] = msgtype;
	strcpy(fmsg + 1, msg);
	dfui_debug("SEND<<%s>>\n", fmsg);

	/*
	 * Send a NUL-terminated message to the backend.
	 */

	errno = 0;
	msgcid = caps_sys_put(T_CAPS(c)->cid, fmsg, strlen(fmsg));
	if (msgcid < 0 && errno == ENOTCONN)
		return(DFUI_FAILURE);

	/*
	 * Receive a reply from the backend.
	 * If our message was a READY, this should be a message like PRESENT.
	 * Otherwise it should simply be a READY.
	 */

	dfui_debug("WAITING<<>>\n");

	T_CAPS(c)->wresult = caps_sys_wait(T_CAPS(c)->cid, T_CAPS(c)->buf,
	    T_CAPS(c)->size, &T_CAPS(c)->msgid, NULL);
	if (T_CAPS(c)->wresult < 0)
		return(DFUI_FAILURE);

	aura_buffer_set(c->ebuf, T_CAPS(c)->buf, T_CAPS(c)->wresult);
	dfui_debug("RECV<<%s>>\n", aura_buffer_buf(c->ebuf));

	aura_free(fmsg, "exchange message");

	return(DFUI_SUCCESS);
}

#endif /* HAS_CAPS */
