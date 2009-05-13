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
 * connection.c
 * $Id: connection.c,v 1.20 2005/02/07 06:39:59 cpressey Exp $
 * This code was derived in part from:
 * $_DragonFly: src/test/caps/client.c,v 1.3 2004/03/31 20:27:34 dillon Exp $
 * and is therefore also subject to the license conditions on that file.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <libaura/mem.h>
#include <libaura/buffer.h>

#include "system.h"
#define	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "dfui.h"
#undef	NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "encoding.h"
#include "dump.h"

#include "conn_caps.h"
#include "conn_npipe.h"
#include "conn_tcp.h"

struct dfui_connection *
dfui_connection_new(int transport, const char *rendezvous)
{
	struct dfui_connection *c = NULL;

	if (
#ifdef HAS_CAPS
	    transport == DFUI_TRANSPORT_CAPS ||
#endif
#ifdef HAS_NPIPE
	    transport == DFUI_TRANSPORT_NPIPE ||
#endif
#ifdef HAS_TCP
	    transport == DFUI_TRANSPORT_TCP ||
#endif
	    0) {
		/* We're OK. */
	} else {
		return(NULL);
	}

	if (dfui_debug_file == NULL) {
		dfui_debug_file = stderr;
	} else {
		setvbuf(dfui_debug_file, NULL, _IOLBF, 0);
	}

	AURA_MALLOC(c, dfui_connection);
	c->rendezvous = aura_strdup(rendezvous);
	c->transport = transport;
	c->ebuf = aura_buffer_new(16384);
	c->is_connected = 0;
	c->t_data = NULL;

	switch (transport) {
#ifdef HAS_CAPS
	case DFUI_TRANSPORT_CAPS:
		AURA_MALLOC(c->t_data, dfui_conn_caps);
		T_CAPS(c)->cid = 0;
		bzero(&T_CAPS(c)->msgid, sizeof(T_CAPS(c)->msgid));

		/*
		 * XXX Ideally, this value should grow as needed.
		 * However, CAPS currently has a size limit of
		 * 128K internally.
		 */
		T_CAPS(c)->size = 128 * 1024;
		if ((T_CAPS(c)->buf = aura_malloc(T_CAPS(c)->size, "CAPS buffer")) == NULL) {
			AURA_FREE(T_CAPS(c), dfui_conn_caps);
			aura_free(c->rendezvous, "rendezvous string");
			AURA_FREE(c, dfui_connection);
			return(NULL);
		}

		/*
		 * Set up dispatch functions.
		 */
		c->be_start = dfui_caps_be_start;
		c->be_stop = dfui_caps_be_stop;
		c->be_ll_exchange = dfui_caps_be_ll_exchange;

		c->fe_connect = dfui_caps_fe_connect;
		c->fe_disconnect = dfui_caps_fe_disconnect;
		c->fe_ll_request = dfui_caps_fe_ll_request;
		break;
#endif /* HAS_CAPS */

#ifdef HAS_NPIPE
	case DFUI_TRANSPORT_NPIPE:
		AURA_MALLOC(c->t_data, dfui_conn_npipe);
		T_NPIPE(c)->in_pipename = NULL;
		T_NPIPE(c)->out_pipename = NULL;
		T_NPIPE(c)->in = NULL;
		T_NPIPE(c)->out = NULL;

		/*
		 * Set up dispatch functions.
		 */
		c->be_start = dfui_npipe_be_start;
		c->be_stop = dfui_npipe_be_stop;
		c->be_ll_exchange = dfui_npipe_be_ll_exchange;

		c->fe_connect = dfui_npipe_fe_connect;
		c->fe_disconnect = dfui_npipe_fe_disconnect;
		c->fe_ll_request = dfui_npipe_fe_ll_request;
		break;
#endif /* HAS_NPIPE */

#ifdef HAS_TCP
	case DFUI_TRANSPORT_TCP:
                AURA_MALLOC(c->t_data, dfui_conn_tcp);
		T_TCP(c)->listen_sd = -1;
		T_TCP(c)->connected_sd = -1;
		T_TCP(c)->is_connected = 0;

                /*
                 * Set up dispatch functions.
                 */
                c->be_start = dfui_tcp_be_start;
                c->be_stop = dfui_tcp_be_stop;
		c->be_ll_exchange = dfui_tcp_be_ll_exchange;

                c->fe_connect = dfui_tcp_fe_connect;
                c->fe_disconnect = dfui_tcp_fe_disconnect;
		c->fe_ll_request = dfui_tcp_fe_ll_request;
		break;
#endif /* HAS_TCP */
	}

	return(c);
}

void
dfui_connection_free(struct dfui_connection *c)
{
	if (c == NULL)
		return;

	switch (c->transport) {
#ifdef HAS_CAPS
	case DFUI_TRANSPORT_CAPS:
		if (T_CAPS(c) != NULL) {
			if (T_CAPS(c)->buf != NULL)
				aura_free(T_CAPS(c)->buf, "CAPS buffer");
			AURA_FREE(T_CAPS(c), dfui_conn_caps);
		}
		break;
#endif
#ifdef HAS_NPIPE
	case DFUI_TRANSPORT_NPIPE:
		if (T_NPIPE(c) != NULL) {
			if (T_NPIPE(c)->in_pipename != NULL)
				aura_free(T_NPIPE(c)->in_pipename, "pipename");
			if (T_NPIPE(c)->out_pipename != NULL)
				aura_free(T_NPIPE(c)->out_pipename, "pipename");
			if (T_NPIPE(c)->in != NULL)
				fclose(T_NPIPE(c)->in);
			if (T_NPIPE(c)->out != NULL)
				fclose(T_NPIPE(c)->out);
			AURA_FREE(T_NPIPE(c), dfui_conn_npipe);
		}
		break;
#endif
#ifdef HAS_TCP
	case DFUI_TRANSPORT_TCP:
		if (T_TCP(c) != NULL) {
			/* XXX close sockets/files here */
			AURA_FREE(T_NPIPE(c), dfui_conn_tcp);
		}
		break;
#endif
	}

	if (c->rendezvous != NULL)
		free(c->rendezvous);
	AURA_FREE(c, dfui_connection);
}

/*
 * VERY HIGH LEVEL
 */

/*
 * Create and present a generic `dialog box'-type form for the user
 * and return their response.  actions is a pipe-seperated list of
 * actions to be put on the form (e.g. "OK|Cancel".)  The return
 * value is the ordinal position of the action that was selected,
 * starting at 1 for the first action.  A return value of 0 indicates
 * that an error occurred.  A return value of -1 indicates that the
 * front end aborted the communications.
 */
int
dfui_be_present_dialog(struct dfui_connection *c, const char *title,
			const char *actions, const char *fmt, ...)
{
	struct dfui_form *f;
	struct dfui_response *r;
	va_list args;
	char *message;
	char action_id[256], action_name[256];
	size_t start, end, counter, i;

	va_start(args, fmt);
	vasprintf(&message, fmt, args);
	va_end(args);

	f = dfui_form_create("dialog", title, message, "", NULL);

	free(message);

	start = end = 0;
	while (actions[end] != '\0') {
		end = start;
		while (actions[end] != '|' && actions[end] != '\0')
			end++;

		if ((end - start) >= 256)
			break;
		strncpy(action_name, &actions[start], end - start);
		action_name[end - start] = '\0';
		strcpy(action_id, action_name);
		for(i = 0; action_id[i] != '\0'; i++) {
			if (action_id[i] == ' ')
				action_id[i] = '_';
		}
		dfui_form_action_add(f, action_id,
		    dfui_info_new(action_name, "", ""));

		start = end + 1;
	}

	if (!dfui_be_present(c, f, &r)) {
		dfui_form_free(f);
		dfui_response_free(r);
		return(-1);
	}

	strlcpy(action_name, dfui_response_get_action_id(r), 256);
	for(i = 0; action_name[i] != '\0'; i++) {
		if (action_name[i] == '_')
			action_name[i] = ' ';
	}

	start = end = 0;
	counter = 1;
	while (actions[end] != '\0') {
		end = start;
		while (actions[end] != '|' && actions[end] != '\0')
			end++;

		if ((end - start) >= 256)
			break;
		if (strlen(action_name) == (end - start) &&
		    strncmp(action_name, &actions[start], end - start) == 0) {
			break;
		}
		counter++;

		start = end + 1;
	}

	dfui_form_free(f);
	dfui_response_free(r);

	return(counter);
}

/******** BACKEND ********/

/*
 * Connect to the frontend.
 */
dfui_err_t
dfui_be_start(struct dfui_connection *c)
{
	if (c->is_connected) {
		return(DFUI_FAILURE);
	} else if (c->be_start(c)) {
		c->is_connected = 1;
		return(DFUI_SUCCESS);
	} else {
		return(DFUI_FAILURE);
	}
}

/*
 * Tell the frontend that we're done and disconnect from it.
 */
dfui_err_t
dfui_be_stop(struct dfui_connection *c)
{
	if (!c->is_connected) {
		return(DFUI_SUCCESS);
	} else if (c->be_stop(c)) {
		c->is_connected = 0;
		return(DFUI_SUCCESS);
	} else {
		return(DFUI_FAILURE);
	}
}

/*
 * Present a form to the user.  This call is synchronous;
 * it does not return until the user has selected an action.
 */
dfui_err_t
dfui_be_present(struct dfui_connection *c,
		struct dfui_form *f, struct dfui_response **r)
{
	struct aura_buffer *e;

	e = aura_buffer_new(16384);
	dfui_encode_form(e, f);

	c->be_ll_exchange(c, DFUI_BE_MSG_PRESENT, aura_buffer_buf(e));

	aura_buffer_free(e);

	/* check for ABORT reply */
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		return(DFUI_FAILURE);
	}

	/*
	 * Now we've got the response; so decode it.
	 */

	e = aura_buffer_new(16384);
	aura_buffer_set(e, aura_buffer_buf(c->ebuf) + 1, aura_buffer_len(c->ebuf) - 1);
	*r = dfui_decode_response(e);
	aura_buffer_free(e);

	return(DFUI_SUCCESS);
}

/*
 * Begin showing a progress bar to the user.
 * This function is asynchronous; it returns immediately.
 * The assumption is that the backend will make subsequent
 * calls to dfui_be_progress_update() frequently, and in
 * them, check to see if the user cancelled.
 */
dfui_err_t
dfui_be_progress_begin(struct dfui_connection *c, struct dfui_progress *pr)
{
	struct aura_buffer *e;

	e = aura_buffer_new(16384);
	dfui_encode_progress(e, pr);

	c->be_ll_exchange(c, DFUI_BE_MSG_PROG_BEGIN, aura_buffer_buf(e));
	aura_buffer_free(e);

	/* response might have been be READY or ABORT */
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		return(DFUI_FAILURE);
	} else {
		return(DFUI_SUCCESS);
	}
}

dfui_err_t
dfui_be_progress_update(struct dfui_connection *c,
			struct dfui_progress *pr, int *cancelled)
{
	struct aura_buffer *e;

	e = aura_buffer_new(16384);
	dfui_encode_progress(e, pr);

	c->be_ll_exchange(c, DFUI_BE_MSG_PROG_UPDATE, aura_buffer_buf(e));
	aura_buffer_free(e);

	/* response might have been READY, CANCEL, or ABORT */

	*cancelled = 0;
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_CANCEL) {
		*cancelled = 1;
	}
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		return(DFUI_FAILURE);
	} else {
		return(DFUI_SUCCESS);
	}
}

dfui_err_t
dfui_be_progress_end(struct dfui_connection *c)
{
	c->be_ll_exchange(c, DFUI_BE_MSG_PROG_END, "");

	/* response might have been be READY or ABORT */
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		return(DFUI_FAILURE);
	} else {
		return(DFUI_SUCCESS);
	}
}

dfui_err_t
dfui_be_set_global_setting(struct dfui_connection *c,
			   const char *key, const char *value,
			   int *cancelled)
{
	struct aura_buffer *e;
	struct dfui_property *p;

	e = aura_buffer_new(16384);
	p = dfui_property_new(key, value);
	dfui_encode_property(e, p);
	c->be_ll_exchange(c, DFUI_BE_MSG_SET_GLOBAL, aura_buffer_buf(e));
	aura_buffer_free(e);
	dfui_property_free(p);

	/* response might have been READY, CANCEL, or ABORT */

	*cancelled = 0;
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_CANCEL) {
		*cancelled = 1;
	}
	if (aura_buffer_buf(c->ebuf)[0] == DFUI_FE_MSG_ABORT) {
		return(DFUI_FAILURE);
	} else {
		return(DFUI_SUCCESS);
	}
}

/******** FRONTEND ********/

dfui_err_t
dfui_fe_connect(struct dfui_connection *c)
{
	return(c->fe_connect(c));
}

dfui_err_t
dfui_fe_disconnect(struct dfui_connection *c)
{
	dfui_debug("DISCONNECTING<<>>\n");
	return(c->fe_disconnect(c));
}

/*
 * Receive a message from the backend.  This call is synchronous;
 * it does not return until a message comes in from the backend.
 * After this call, the message type is available in *msgtype,
 * and the message itself (if any) is available in *payload, ready
 * to be casted to its real type (as per *msgtype).
 */
dfui_err_t
dfui_fe_receive(struct dfui_connection *c, char *msgtype, void **payload)
{
	struct aura_buffer *e;

	c->fe_ll_request(c, DFUI_FE_MSG_READY, "");
	*msgtype = aura_buffer_buf(c->ebuf)[0];
	switch (*msgtype) {
	case DFUI_BE_MSG_PRESENT:
		e = aura_buffer_new(16384);
		aura_buffer_set(e, aura_buffer_buf(c->ebuf) + 1, aura_buffer_len(c->ebuf) - 1);
		*payload = dfui_decode_form(e);
		aura_buffer_free(e);
		return(DFUI_SUCCESS);

	case DFUI_BE_MSG_PROG_BEGIN:
		e = aura_buffer_new(16384);
		aura_buffer_set(e, aura_buffer_buf(c->ebuf) + 1, aura_buffer_len(c->ebuf) - 1);
		*payload = dfui_decode_progress(e);
		aura_buffer_free(e);
		return(DFUI_SUCCESS);

	case DFUI_BE_MSG_PROG_UPDATE:
		e = aura_buffer_new(16384);
		aura_buffer_set(e, aura_buffer_buf(c->ebuf) + 1, aura_buffer_len(c->ebuf) - 1);
		*payload = dfui_decode_progress(e);
		aura_buffer_free(e);
		return(DFUI_SUCCESS);

	case DFUI_BE_MSG_PROG_END:
		*payload = NULL;
		return(DFUI_SUCCESS);

	case DFUI_BE_MSG_SET_GLOBAL:
		e = aura_buffer_new(16384);
		aura_buffer_set(e, aura_buffer_buf(c->ebuf) + 1, aura_buffer_len(c->ebuf) - 1);
		*payload = dfui_decode_property(e);
		aura_buffer_free(e);
		return(DFUI_SUCCESS);

	case DFUI_BE_MSG_STOP:
		*payload = NULL;
		return(DFUI_SUCCESS);

	default:
		/* XXX ??? */
		return(DFUI_FAILURE);
	}
}

/*
 * Wrapper function for dfui_fe_receive for binding generators which
 * seem to (understandably) have problems wrapping void *'s themselves.
 */
struct dfui_payload *
dfui_fe_receive_payload(struct dfui_connection *c)
{
	char msgtype;
	void *v;
	struct dfui_payload *payload;

	if (!dfui_fe_receive(c, &msgtype, &v)) {
		return(NULL);
	}

	AURA_MALLOC(payload, dfui_payload);

	payload->msgtype = msgtype;
	payload->form = NULL;
	payload->progress = NULL;

	switch (msgtype) {
	case DFUI_BE_MSG_PRESENT:
		payload->form = v;
		break;

	case DFUI_BE_MSG_PROG_BEGIN:
	case DFUI_BE_MSG_PROG_UPDATE:
		payload->progress = v;
		break;

	case DFUI_BE_MSG_SET_GLOBAL:
		payload->global_setting = v;
		break;

	case DFUI_BE_MSG_PROG_END:
	case DFUI_BE_MSG_STOP:
		break;
	}

	return(payload);
}

char
dfui_payload_get_msg_type(const struct dfui_payload *p)
{
	if (p == NULL)
		return(' ');
	return(p->msgtype);
}

struct dfui_form *
dfui_payload_get_form(const struct dfui_payload *p)
{
	if (p == NULL)
		return(NULL);
	return(p->form);
}

struct dfui_progress *
dfui_payload_get_progress(const struct dfui_payload *p)
{
	if (p == NULL)
		return(NULL);
	return(p->progress);
}

void
dfui_payload_free(struct dfui_payload *p)
{
	if (p == NULL)
		return;
	if (p->form != NULL)
		dfui_form_free(p->form);
	if (p->progress != NULL)
		dfui_progress_free(p->progress);
	AURA_FREE(p, dfui_payload);
}

/*
 * Submit the result of a form to the backend.
 */
dfui_err_t
dfui_fe_submit(struct dfui_connection *c, struct dfui_response *r)
{
	struct aura_buffer *e;
	dfui_err_t request_error;

	e = aura_buffer_new(16384);
	dfui_encode_response(e, r);

	dfui_debug("ENCODE<<%s>>\n", aura_buffer_buf(e));
	request_error = c->fe_ll_request(c, DFUI_FE_MSG_SUBMIT,
	    aura_buffer_buf(e));
	/* XXX we should check for READY from the backend? */
	aura_buffer_free(e);

	return(request_error);
}

dfui_err_t
dfui_fe_progress_continue(struct dfui_connection *c)
{
	c->fe_ll_request(c, DFUI_FE_MSG_CONTINUE, "");
	return(DFUI_SUCCESS);
}

dfui_err_t
dfui_fe_progress_cancel(struct dfui_connection *c)
{
	c->fe_ll_request(c, DFUI_FE_MSG_CANCEL, "");
	return(DFUI_SUCCESS);
}

dfui_err_t
dfui_fe_confirm_set_global(struct dfui_connection *c)
{
	c->fe_ll_request(c, DFUI_FE_MSG_CONTINUE, "");
	return(DFUI_SUCCESS);
}

dfui_err_t
dfui_fe_cancel_set_global(struct dfui_connection *c)
{
	c->fe_ll_request(c, DFUI_FE_MSG_CANCEL, "");
	return(DFUI_SUCCESS);
}

dfui_err_t
dfui_fe_confirm_stop(struct dfui_connection *c)
{
	c->fe_ll_request(c, DFUI_FE_MSG_CONTINUE, "");
	return(DFUI_SUCCESS);
}

/*
 * Abort the backend.
 * Note that you still must call dfui_fe_disconnect after this.
 */
dfui_err_t
dfui_fe_abort(struct dfui_connection *c)
{
	c->fe_ll_request(c, DFUI_FE_MSG_ABORT, "");
	return(DFUI_SUCCESS);
}
