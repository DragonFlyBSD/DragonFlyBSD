/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/net/netmsg2.h,v 1.2 2007/09/08 11:20:22 sephe Exp $
 */

#ifndef _NET_NETMSG2_H_
#define _NET_NETMSG2_H_

#include <net/netmsg.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>

/*
 * Extended lwkt_initmsg() for netmsg's, also initializing the
 * dispatch function.
 */
static __inline void
netmsg_init(netmsg_base_t msg, struct socket *so, lwkt_port_t rport,
	    int flags, netisr_fn_t dispatch)
{
	lwkt_initmsg(&msg->lmsg, rport, flags);
	msg->nm_dispatch = dispatch;
	msg->nm_so = so;
}

static __inline void
netmsg_init_abortable(netmsg_base_t msg, struct socket *so, lwkt_port_t rport,
		      int flags, netisr_fn_t dispatch,
		      void (*abortfn)(lwkt_msg_t))
{
	lwkt_initmsg_abortable(&msg->lmsg, rport, flags, abortfn);
	msg->nm_dispatch = dispatch;
	msg->nm_so = so;
}

#endif /* _NET_NETMSG2_H_ */
