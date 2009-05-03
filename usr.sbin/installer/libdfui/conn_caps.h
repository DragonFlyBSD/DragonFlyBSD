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
 * conn_caps.h
 * $Id: conn_caps.h,v 1.7 2005/02/06 19:53:19 cpressey Exp $
 */

#ifndef __CONN_CAPS_H_
#define __CONN_CAPS_H_

#include "system.h"
#ifdef HAS_CAPS

#include <sys/caps.h>

#include "dfui.h"

struct dfui_conn_caps {
	int cid;			/* the caps id for the connection */
	int wresult;			/* result of last caps_sys_wait/get */
	struct caps_msgid msgid;	/* msgid of the last msg recvd */
	char *buf;			/* XXX last message recvd */
	size_t size;			/* XXX size of msg recv buffer */
};

#define T_CAPS(c) ((struct dfui_conn_caps *)c->t_data)

dfui_err_t	dfui_caps_be_start(struct dfui_connection *);
dfui_err_t	dfui_caps_be_stop(struct dfui_connection *);

dfui_err_t	dfui_caps_be_ll_exchange(struct dfui_connection *,
		    char, const char *);
dfui_err_t	dfui_caps_be_ll_receive(struct dfui_connection *);
dfui_err_t	dfui_caps_be_ll_reply(struct dfui_connection *, const char *);

dfui_err_t	dfui_caps_fe_connect(struct dfui_connection *);
dfui_err_t	dfui_caps_fe_disconnect(struct dfui_connection *);

dfui_err_t	dfui_caps_fe_ll_request(struct dfui_connection *, char, const char *);

#endif /* HAS_CAPS */
#endif /* !__CONN_CAPS_H_ */
