/*
 * Copyright (c) 2003, 2004 Jeffrey Hsu
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
 * $DragonFly: src/sys/sys/socketops.h,v 1.2 2004/03/05 16:57:16 hsu Exp $
 */

#ifndef _SOCKETOPS_H_
#define _SOCKETOPS_H_

#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

static __inline int
so_pru_sosend(struct socket *so, struct sockaddr *addr, struct uio *uio,
    struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_sosend)(so, addr, uio, top,
	    control, flags, td));
}

static __inline int
so_pru_soreceive(struct socket *so, struct sockaddr **paddr, struct uio *uio,
    struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{
	return ((*so->so_proto->pr_usrreqs->pru_soreceive)(so, paddr, uio, mp0,
	    controlp, flagsp));
}

static __inline int
so_pru_abort(struct socket *so)
{
	return ((*so->so_proto->pr_usrreqs->pru_abort)(so));
}

static __inline int
so_pru_accept(struct socket *so,
    struct sockaddr **nam)
{
	return ((*so->so_proto->pr_usrreqs->pru_accept)(so, nam));
}

static __inline int
so_pru_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	return ((*so->so_proto->pr_usrreqs->pru_attach)(so, proto, ai));
}

static __inline int
so_pru_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_bind)(so, nam, td));
}

static __inline int
so_pru_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_connect)(so, nam, td));
}

static __inline int
so_pru_connect2(struct socket *so1, struct socket *so2)
{
	return ((*so1->so_proto->pr_usrreqs->pru_connect2)(so1, so2));
}

static __inline int
so_pru_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,
    struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_control)(so, cmd, data, ifp,
	    td));
}

static __inline int
so_pru_detach(struct socket *so)
{
	return ((*so->so_proto->pr_usrreqs->pru_detach)(so));
}

static __inline int
so_pru_disconnect(struct socket *so)
{
	return ((*so->so_proto->pr_usrreqs->pru_disconnect)(so));
}

static __inline int
so_pru_listen(struct socket *so, struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_listen)(so, td));
}

static __inline int
so_pru_peeraddr(struct socket *so,
    struct sockaddr **nam)
{
	return ((*so->so_proto->pr_usrreqs->pru_peeraddr)(so, nam));
}

static __inline int
so_pru_rcvd(struct socket *so, int flags)
{
	return ((*so->so_proto->pr_usrreqs->pru_rcvd)(so, flags));
}

static __inline int
so_pru_rcvoob(struct socket *so, struct mbuf *m, int flags)
{
	return ((*so->so_proto->pr_usrreqs->pru_rcvoob)(so, m, flags));
}

static __inline int
so_pru_send(struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_send)(so, flags, m, addr,
	    control, td));
}

static __inline int
so_pru_sense(struct socket *so, struct stat *sb)
{
	return ((*so->so_proto->pr_usrreqs->pru_sense)(so, sb));
}

static __inline int
so_pru_shutdown(struct socket *so)
{
	return ((*so->so_proto->pr_usrreqs->pru_shutdown)(so));
}

static __inline int
so_pru_sockaddr(struct socket *so, struct sockaddr **nam)
{
	return ((*so->so_proto->pr_usrreqs->pru_sockaddr)(so, nam));
}

static __inline int
so_pru_sopoll(struct socket *so, int events, struct ucred *cred,
    struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_sopoll)(so, events, cred, td));
}

static __inline int
so_pr_ctloutput(struct socket *so, struct sockopt *sopt)
{
	return ((*so->so_proto->pr_ctloutput)(so, sopt));
}

#endif
