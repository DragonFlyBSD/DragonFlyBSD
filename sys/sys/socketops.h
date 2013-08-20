/*
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $DragonFly: src/sys/sys/socketops.h,v 1.14 2008/10/27 02:56:30 sephe Exp $
 */

#ifndef _SOCKETOPS_H_
#define _SOCKETOPS_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_PROTOSW_H_
#include <sys/protosw.h>
#endif
#ifndef _SYS_SOCKET_H_
#include <sys/socket.h>
#endif
#ifndef _SYS_SOCKETVAR_H_
#include <sys/socketvar.h>
#endif

/*
 * sosend() and soreceive() can block and also calls other pru_usrreq functions.
 * They should not really be usrreq functions.  Always call them directly from
 * the process context rather than passing a message to the protocol thread.
 */
static __inline int
so_pru_sosend(struct socket *so, struct sockaddr *addr, struct uio *uio,
	      struct mbuf *top, struct mbuf *control, int flags,
	      struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_sosend)(so, addr, uio, top,
		control, flags, td));
}

static __inline int
so_pru_soreceive(struct socket *so, struct sockaddr **paddr, struct uio *uio,
		 struct sockbuf *sio, struct mbuf **controlp, int *flagsp)
{
	return ((*so->so_proto->pr_usrreqs->pru_soreceive)(so, paddr, uio, sio,
		controlp, flagsp));
}

void so_pru_abort (struct socket *so);
void so_pru_aborta (struct socket *so);
void so_pru_abort_oncpu (struct socket *so);
int so_pru_accept (struct socket *so, struct sockaddr **nam);
int so_pru_attach (struct socket *so, int proto, struct pru_attach_info *ai);
int so_pru_attach_direct(struct socket *so, int proto,
		struct pru_attach_info *ai);
int so_pru_bind (struct socket *so, struct sockaddr *nam, struct thread *td);
int so_pru_connect (struct socket *so, struct sockaddr *nam, struct thread *td);
int so_pru_connect_async (struct socket *so, struct sockaddr *nam,
		struct thread *td);
int so_pru_connect2 (struct socket *so1, struct socket *so2);
int so_pru_control_direct(struct socket *so, u_long cmd, caddr_t data,
		struct ifnet *ifp);
int so_pru_detach (struct socket *so);
void so_pru_detach_direct (struct socket *so);
int so_pru_disconnect (struct socket *so);
void so_pru_disconnect_direct (struct socket *so);
int so_pru_listen (struct socket *so, struct thread *td);
int so_pru_peeraddr (struct socket *so, struct sockaddr **nam);
int so_pru_rcvd (struct socket *so, int flags);
void so_pru_rcvd_async (struct socket *so);
void so_async_rcvd_reply (struct socket *so);
void so_async_rcvd_drop (struct socket *so);
int so_pru_rcvoob (struct socket *so, struct mbuf *m, int flags);
void so_pru_sync (struct socket *so);
int so_pru_send (struct socket *so, int flags, struct mbuf *m,
		struct sockaddr *addr, struct mbuf *control,
		struct thread *td);
void so_pru_send_async (struct socket *so, int flags, struct mbuf *m,
		struct sockaddr *addr, struct mbuf *control,
		struct thread *td);
int so_pru_sense (struct socket *so, struct stat *sb);
int so_pru_shutdown (struct socket *so);
int so_pru_sockaddr (struct socket *so, struct sockaddr **nam);
int so_pr_ctloutput(struct socket *so, struct sockopt *sopt);
void so_pru_ctlinput(struct protosw *pr, int cmd,
		struct sockaddr *arg, void *extra);

static __inline int
so_pru_senda(struct socket *so, int flags, struct mbuf *m,
	     struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	if (so->so_proto->pr_flags & PR_ASYNC_SEND) {
		so_pru_send_async(so, flags, m, addr, control, td);
		return 0;
	} else {
		return so_pru_send(so, flags, m, addr, control, td);
	}
}

#endif	/* _KERNEL */
#endif	/* _SYS_SOCKETOPS_H_ */
