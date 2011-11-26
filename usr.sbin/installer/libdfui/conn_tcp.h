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
 * conn_tcp.h
 * $Id: conn_tcp.h,v 1.7 2005/02/06 19:53:19 cpressey Exp $
 */

#ifndef __CONN_TCP_H_
#define __CONN_TCP_H_

#include "system.h"
#ifdef HAS_TCP

#include <stdio.h>

#include "dfui.h"

struct dfui_conn_tcp {
	int listen_sd;			/* listen socket descriptor (b/e) */
	int connected_sd;		/* connected socket descriptor */
	int is_connected;		/* is socket connected 0 = no */
	FILE *stream;			/* stream that wraps connected_sd */
};

#define T_TCP(c) ((struct dfui_conn_tcp *)c->t_data)

dfui_err_t	dfui_tcp_be_start(struct dfui_connection *);
dfui_err_t	dfui_tcp_be_stop(struct dfui_connection *);

dfui_err_t	dfui_tcp_be_ll_exchange(struct dfui_connection *,
		    char msgtype, const char *);
dfui_err_t	dfui_tcp_be_ll_receive(struct dfui_connection *);
dfui_err_t	dfui_tcp_be_ll_reply(struct dfui_connection *, const char *);

dfui_err_t	dfui_tcp_fe_connect(struct dfui_connection *);
dfui_err_t	dfui_tcp_fe_disconnect(struct dfui_connection *);

dfui_err_t	dfui_tcp_fe_ll_request(struct dfui_connection *, char, const char *);

int		read_data(FILE *, char *, int);
int		write_data(FILE *, const char *, int);

#endif /* HAS_TCP */
#endif /* !__CONN_TCP_H_ */
