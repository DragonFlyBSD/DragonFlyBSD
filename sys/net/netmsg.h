/*
 * Copyright (c) 2003 Jeffrey Hsu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jeffrey M. Hsu.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $DragonFly: src/sys/net/netmsg.h,v 1.1 2004/03/06 02:36:25 hsu Exp $
 */

#ifndef _NETMSG_H_
#define _NETMSG_H_

/*
 * User protocol requests messages.
 */
struct netmsg_pru_abort {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_abort_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_accept {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_accept_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_attach {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_attach_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_proto;
    struct pru_attach_info *nm_ai;
};

struct netmsg_pru_bind {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_bind_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	*nm_nam;
    struct thread	*nm_td;
};

struct netmsg_pru_connect {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_connect_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	*nm_nam;
    struct thread	*nm_td;
};

struct netmsg_pru_connect2 {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_connect2_fn_t	nm_prufn;
    struct socket	*nm_so1;
    struct socket	*nm_so2;
};

struct netmsg_pru_control {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_control_fn_t	nm_prufn;
    struct socket	*nm_so;
    u_long		nm_cmd;
    caddr_t		nm_data;
    struct ifnet	*nm_ifp;
    struct thread	*nm_td;
};

struct netmsg_pru_detach {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_detach_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_disconnect {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_disconnect_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_listen {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_listen_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct thread	*nm_td;
};

struct netmsg_pru_peeraddr {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_peeraddr_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_rcvd {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_rcvd_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_flags;
};

struct netmsg_pru_rcvoob {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_rcvoob_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct mbuf		*nm_m;
    int			nm_flags;
};

struct netmsg_pru_send {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_send_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_flags;
    struct mbuf		*nm_m;
    struct sockaddr	*nm_addr;
    struct mbuf		*nm_control;
    struct thread	*nm_td;
};

struct netmsg_pru_sense {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_sense_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct stat		*nm_stat;
};

struct netmsg_pru_shutdown {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_shutdown_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_sockaddr {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_sockaddr_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_sosend {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_sosend_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	*nm_addr;
    struct uio		*nm_uio;
    struct mbuf		*nm_top;
    struct mbuf		*nm_control;
    int			nm_flags;
    struct thread	*nm_td;
};

struct netmsg_pru_soreceive {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_soreceive_fn_t	nm_prufn;
    struct sockaddr	*nm_addr;
    struct socket	*nm_so;
    struct sockaddr	**nm_paddr;
    struct uio		*nm_uio;
    struct mbuf		**nm_mp0;
    struct mbuf		**nm_controlp;
    int			*nm_flagsp;
};

struct netmsg_pru_sopoll {
    struct lwkt_msg	nm_lmsg;
    netisr_fn_t		nm_handler;
    pru_sopoll_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_events;
    struct ucred	*nm_cred;
    struct thread	*nm_td;
};

#endif
