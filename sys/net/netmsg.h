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
 * $DragonFly: src/sys/net/netmsg.h,v 1.10 2008/10/27 02:56:30 sephe Exp $
 */

#ifndef _NET_NETMSG_H_
#define _NET_NETMSG_H_

#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_PROTOSW_H_
#include <sys/protosw.h>
#endif

struct netmsg;

typedef void (*netisr_fn_t)(struct netmsg *);

/*
 * Base netmsg
 */
typedef struct netmsg {
    struct lwkt_msg     nm_lmsg;
    netisr_fn_t		nm_dispatch;
    struct socket	*nm_so;
} *netmsg_t;

#define MSGF_MPSAFE	MSGF_USER0

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * User protocol requests messages.
 */
struct netmsg_pru_abort {
    struct netmsg	nm_netmsg;
    pru_abort_fn_t	nm_prufn;
};

struct netmsg_pru_accept {
    struct netmsg	nm_netmsg;
    pru_accept_fn_t	nm_prufn;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_attach {
    struct netmsg	nm_netmsg;
    pru_attach_fn_t	nm_prufn;
    int			nm_proto;
    struct pru_attach_info *nm_ai;
};

struct netmsg_pru_bind {
    struct netmsg	nm_netmsg;
    pru_bind_fn_t	nm_prufn;
    struct sockaddr	*nm_nam;
    struct thread	*nm_td;
};

struct netmsg_pru_connect {
    struct netmsg	nm_netmsg;
    pru_connect_fn_t	nm_prufn;
    struct sockaddr	*nm_nam;
    struct thread	*nm_td;
};

struct netmsg_pru_connect2 {
    struct netmsg	nm_netmsg;
    pru_connect2_fn_t	nm_prufn;
    struct socket	*nm_so1;
    struct socket	*nm_so2;
};

struct netmsg_pru_control {
    struct netmsg	nm_netmsg;
    pru_control_fn_t	nm_prufn;
    u_long		nm_cmd;
    caddr_t		nm_data;
    struct ifnet	*nm_ifp;
    struct thread	*nm_td;
};

struct netmsg_pru_detach {
    struct netmsg	nm_netmsg;
    pru_detach_fn_t	nm_prufn;
};

struct netmsg_pru_disconnect {
    struct netmsg	nm_netmsg;
    pru_disconnect_fn_t	nm_prufn;
};

struct netmsg_pru_listen {
    struct netmsg	nm_netmsg;
    pru_listen_fn_t	nm_prufn;
    struct thread	*nm_td;
};

struct netmsg_pru_peeraddr {
    struct netmsg	nm_netmsg;
    pru_peeraddr_fn_t	nm_prufn;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_rcvd {
    struct netmsg	nm_netmsg;
    pru_rcvd_fn_t	nm_prufn;
    int			nm_flags;
};

struct netmsg_pru_rcvoob {
    struct netmsg	nm_netmsg;
    pru_rcvoob_fn_t	nm_prufn;
    struct mbuf		*nm_m;
    int			nm_flags;
};

struct netmsg_pru_send {
    struct netmsg	nm_netmsg;
    pru_send_fn_t	nm_prufn;
    int			nm_flags;
    struct mbuf		*nm_m;
    struct sockaddr	*nm_addr;
    struct mbuf		*nm_control;
    struct thread	*nm_td;
};

struct netmsg_pru_sense {
    struct netmsg	nm_netmsg;
    pru_sense_fn_t	nm_prufn;
    struct stat		*nm_stat;
};

struct netmsg_pru_shutdown {
    struct netmsg	nm_netmsg;
    pru_shutdown_fn_t	nm_prufn;
};

struct netmsg_pru_sockaddr {
    struct netmsg	nm_netmsg;
    pru_sockaddr_fn_t	nm_prufn;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_sosend {
    struct netmsg	nm_netmsg;
    pru_sosend_fn_t	nm_prufn;
    struct sockaddr	*nm_addr;
    struct uio		*nm_uio;
    struct mbuf		*nm_top;
    struct mbuf		*nm_control;
    int			nm_flags;
    struct thread	*nm_td;
};

struct netmsg_pru_soreceive {
    struct netmsg	nm_netmsg;
    struct sockaddr	*nm_addr;
    struct sockaddr	**nm_paddr;
    struct uio		*nm_uio;
    struct sockbuf	*nm_sio;
    struct mbuf		**nm_controlp;
    int			*nm_flagsp;
};

struct netmsg_pru_ctloutput {
    struct netmsg	nm_netmsg;
    pru_ctloutput_fn_t	nm_prufn;
    struct sockopt	*nm_sopt;
};

struct netmsg_pru_ctlinput {
    struct netmsg	nm_netmsg;
    pru_ctlinput_fn_t	nm_prufn;
    int			nm_cmd;
    struct sockaddr	*nm_arg;
    void		*nm_extra;
};

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#endif	/* !_NET_NETMSG_H_ */
