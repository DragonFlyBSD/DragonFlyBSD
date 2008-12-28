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

union anynetmsg;

typedef void (*netisr_fn_t)(union anynetmsg *);

/*
 * Base netmsg
 */
typedef struct netmsg {
    struct lwkt_msg     nm_lmsg;
    netisr_fn_t		nm_dispatch;
} *netmsg_t;

#define MSGF_MPSAFE	MSGF_USER0

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * User protocol requests messages.
 */
struct netmsg_pru_abort {
    struct netmsg	nm_netmsg;
    pru_abort_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_accept {
    struct netmsg	nm_netmsg;
    pru_accept_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_attach {
    struct netmsg	nm_netmsg;
    pru_attach_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_proto;
    struct pru_attach_info *nm_ai;
};

struct netmsg_pru_bind {
    struct netmsg	nm_netmsg;
    pru_bind_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	*nm_nam;
    struct thread	*nm_td;
};

struct netmsg_pru_connect {
    struct netmsg	nm_netmsg;
    pru_connect_fn_t	nm_prufn;
    struct socket	*nm_so;
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
    struct socket	*nm_so;
    u_long		nm_cmd;
    caddr_t		nm_data;
    struct ifnet	*nm_ifp;
    struct thread	*nm_td;
};

struct netmsg_pru_detach {
    struct netmsg	nm_netmsg;
    pru_detach_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_disconnect {
    struct netmsg	nm_netmsg;
    pru_disconnect_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_listen {
    struct netmsg	nm_netmsg;
    pru_listen_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct thread	*nm_td;
};

struct netmsg_pru_peeraddr {
    struct netmsg	nm_netmsg;
    pru_peeraddr_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_rcvd {
    struct netmsg	nm_netmsg;
    pru_rcvd_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_flags;
};

struct netmsg_pru_rcvoob {
    struct netmsg	nm_netmsg;
    pru_rcvoob_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct mbuf		*nm_m;
    int			nm_flags;
};

struct netmsg_pru_send {
    struct netmsg	nm_netmsg;
    pru_send_fn_t	nm_prufn;
    struct socket	*nm_so;
    int 		nm_flags;
    struct mbuf		*nm_m;
    struct sockaddr	*nm_addr;
    struct mbuf		*nm_control;
    struct thread	*nm_td;
};

struct netmsg_pru_notify {
    struct netmsg	nm_netmsg;
    struct socket	*nm_so;
};

struct netmsg_pru_sense {
    struct netmsg	nm_netmsg;
    pru_sense_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct stat		*nm_stat;
};

struct netmsg_pru_shutdown {
    struct netmsg	nm_netmsg;
    pru_shutdown_fn_t	nm_prufn;
    struct socket	*nm_so;
};

struct netmsg_pru_sockaddr {
    struct netmsg	nm_netmsg;
    pru_sockaddr_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockaddr	**nm_nam;
};

struct netmsg_pru_poll {
    struct netmsg	nm_netmsg;
    pru_poll_fn_t	nm_prufn;
    struct socket	*nm_so;
    int			nm_events;
    struct ucred	*nm_cred;
    struct thread	*nm_td;
};

struct netmsg_pru_ctloutput {
    struct netmsg	nm_netmsg;
    pru_ctloutput_fn_t	nm_prufn;
    struct socket	*nm_so;
    struct sockopt	*nm_sopt;
};

struct netmsg_pru_ctlinput {
    struct netmsg	nm_netmsg;
    pru_ctlinput_fn_t	nm_prufn;
    int			nm_cmd;
    struct sockaddr	*nm_arg;
    void		*nm_extra;
};

#if 0

struct netmsg_pru_sosend {
    struct netmsg	nm_netmsg;
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
    struct netmsg	nm_netmsg;
    struct sockaddr	*nm_addr;
    struct socket	*nm_so;
    struct sockaddr	**nm_paddr;
    struct uio		*nm_uio;
    struct sockbuf	*nm_sio;
    struct mbuf		**nm_controlp;
    int			*nm_flagsp;
};

#endif

/*
 *  netisr messages
 */
TAILQ_HEAD(notifymsglist, netmsg_so_notify);

typedef __boolean_t (*msg_predicate_fn_t)(struct netmsg *);

/*
 * Base class.  All net messages must start with the same fields.
 */

struct netmsg_isr_packet {
     struct netmsg	nm_netmsg;
     struct mbuf	*nm_packet;
};

struct netmsg_pr_timeout {
    struct netmsg	nm_netmsg;
    int			(*nm_prfn) (void);
};

struct netmsg_so_op {
	struct netmsg	nm_netmsg;
	struct socket	*nm_so;
	void		*nm_val;
};

struct netmsg_so_notify {
    struct netmsg			nm_netmsg;
    msg_predicate_fn_t			nm_predicate;
    struct socket			*nm_so;
    int					nm_fflags; /* flags e.g. FNONBLOCK */
    int					nm_etype;  /* receive or send event */
    /*
     * waiting for that many bytes
     */
    int					nm_rbytes;
    TAILQ_ENTRY(netmsg_so_notify)	nm_list;
};

struct netmsg_so_notify_abort {
    struct netmsg			nm_netmsg;
    struct netmsg_so_notify 		*nm_notifymsg;
};

union anynetmsg {
	struct lwkt_msg    		lmsg;
	struct netmsg			netmsg;
	struct netmsg_pru_abort		pru_abort;
	struct netmsg_pru_accept	pru_accept;
	struct netmsg_pru_attach	pru_attach;
	struct netmsg_pru_bind		pru_bind;
	struct netmsg_pru_connect	pru_connect;
	struct netmsg_pru_connect2	pru_connect2;
	struct netmsg_pru_control	pru_control;
	struct netmsg_pru_detach	pru_detach;
	struct netmsg_pru_disconnect	pru_disconnect;
	struct netmsg_pru_listen	pru_listen;
	struct netmsg_pru_peeraddr	pru_peeraddr;
	struct netmsg_pru_rcvd		pru_rcvd;
	struct netmsg_pru_rcvoob	pru_rcvoob;
	struct netmsg_pru_send		pru_send;
	struct netmsg_pru_notify	pru_notify;
	struct netmsg_pru_sense		pru_sense;
	struct netmsg_pru_shutdown	pru_shutdown;
	struct netmsg_pru_sockaddr	pru_sockaddr;
	struct netmsg_pru_ctloutput	pru_ctloutput;
	struct netmsg_pru_ctlinput	pru_ctlinput;
	struct netmsg_pru_poll		pru_poll;

	struct netmsg_isr_packet	isr_packet;
	struct netmsg_pr_timeout	so_timeout;
	struct netmsg_so_op		so_op;
	struct netmsg_so_notify		so_notify;
	struct netmsg_so_notify_abort	so_notify_abort;
};

typedef union anynetmsg *anynetmsg_t;

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#endif	/* !_NET_NETMSG_H_ */
