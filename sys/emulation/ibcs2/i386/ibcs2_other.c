/*
 * Copyright (c) 1995 Steven Wallace
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * $FreeBSD: src/sys/i386/ibcs2/ibcs2_other.c,v 1.10 1999/08/28 00:43:59 peter Exp $
 * $DragonFly: src/sys/emulation/ibcs2/i386/Attic/ibcs2_other.c,v 1.4 2003/07/21 07:57:44 dillon Exp $
 */

/*
 * IBCS2 compatibility module.
 */

#include "opt_spx_hack.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>

#include <i386/ibcs2/ibcs2_types.h>
#include <i386/ibcs2/ibcs2_signal.h>
#include <i386/ibcs2/ibcs2_util.h>
#include <i386/ibcs2/ibcs2_proto.h>

#define IBCS2_SECURE_GETLUID 1
#define IBCS2_SECURE_SETLUID 2

int
ibcs2_secure(struct ibcs2_secure_args *uap)
{
	struct proc *p = curproc;

	switch (uap->cmd) {
	case IBCS2_SECURE_GETLUID:		/* get login uid */
		p->p_retval[0] = p->p_ucred->cr_uid;
		return 0;
	case IBCS2_SECURE_SETLUID:		/* set login uid */
		return EPERM;
	default:
		printf("IBCS2: 'secure' cmd=%d not implemented\n", uap->cmd);
	}

	return EINVAL;
}

int
ibcs2_lseek(register struct ibcs2_lseek_args *uap)
{
	struct lseek_args largs;
	int error;

	largs.fd = uap->fd;
	largs.offset = uap->offset;
	largs.whence = uap->whence;
	error = lseek(&largs);
	return (error);
}

#ifdef SPX_HACK
#include <sys/socket.h>
#include <sys/un.h>     

int
spx_open(void *uap)
{
	struct socket_args sock;
	struct connect_args conn;
	struct sockaddr_un *Xaddr;
	int fd, error;
	caddr_t sg = stackgap_init();
	struct proc *p = curproc;

	/* obtain a socket. */
	DPRINTF(("SPX: open socket\n"));
	sock.domain = AF_UNIX;
	sock.type = SOCK_STREAM;
	sock.protocol = 0;
	error = socket(&sock);
	if (error)
		return error;

	/* connect the socket to standard X socket */
	DPRINTF(("SPX: connect to /tmp/X11-unix/X0\n"));
	Xaddr = stackgap_alloc(&sg, sizeof(struct sockaddr_un));
	Xaddr->sun_family = AF_UNIX;
	Xaddr->sun_len = sizeof(struct sockaddr_un) - sizeof(Xaddr->sun_path) +
	  strlen(Xaddr->sun_path) + 1;
	copyout("/tmp/.X11-unix/X0", Xaddr->sun_path, 18);

	conn.s = fd = p->p_retval[0];
	conn.name = (caddr_t)Xaddr;
	conn.namelen = sizeof(struct sockaddr_un);
	error = connect(&conn);
	if (error) {
		struct close_args cl;
		cl.fd = fd;
		close(&cl);
		return error;
	}
	p->p_retval[0] = fd;
	return 0;
}
#endif /* SPX_HACK */
