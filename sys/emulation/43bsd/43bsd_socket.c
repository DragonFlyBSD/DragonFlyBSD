/*
 * 43BSD_SOCKET.C	- 4.3BSD compatibility socket syscalls
 *
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/emulation/43bsd/43bsd_socket.c,v 1.1 2003/09/12 00:43:30 daver Exp $
 *	from: DragonFly kern/uipc_syscalls.c,v 1.13
 *
 * The original versions of these syscalls used to live in
 * kern/uipc_syscalls.c.  These are heavily modified to use the
 * new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/kern_syscall.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include "43bsd_socket.h"

/*
 * System call interface to the socket abstraction.
 */

static int
compat_43_getsockaddr(struct sockaddr **namp, caddr_t uaddr, size_t len)
{
	struct sockaddr *sa;
	int error;

	*namp = NULL;
	if (len > SOCK_MAXADDRLEN)
		return ENAMETOOLONG;
	if (len < offsetof(struct sockaddr, sa_data[0]))
		return EDOM;
	MALLOC(sa, struct sockaddr *, len, M_SONAME, M_WAITOK);
	error = copyin(uaddr, sa, len);
	if (error) {
		FREE(sa, M_SONAME);
	} else {
		/*
		 * Convert to the 4.4BSD sockaddr structure.
		 */
		sa->sa_family = sa->sa_len;
		sa->sa_len = len;
		*namp = sa;
	}
	return error;
}

static int
compat_43_copyout_sockaddr(struct sockaddr *sa, caddr_t uaddr)
{
	int error, sa_len;

	/* Save the length of sa before we destroy it */
	sa_len = sa->sa_len;
	((struct osockaddr *)sa)->sa_family = sa->sa_family;

	error = copyout(sa, uaddr, sa_len);

	return (error);
}

int
oaccept(struct accept_args *uap)
{
	struct sockaddr *sa = NULL;
	int sa_len;
	int error;

	if (uap->name) {
		error = copyin(uap->anamelen, &sa_len, sizeof(sa_len));
		if (error)
			return (error);

		error = kern_accept(uap->s, &sa, &sa_len, &uap->sysmsg_result);

		if (error) {
			/*
			 * return a namelen of zero for older code which
			 * might ignore the return value from accept.
			 */
			sa_len = 0;
			copyout(&sa_len, uap->anamelen, sizeof(*uap->anamelen));
		} else {
			compat_43_copyout_sockaddr(sa, uap->name);
			if (error == 0) {
				error = copyout(&sa_len, uap->anamelen,
				    sizeof(*uap->anamelen));
			}
		}
		if (sa)
			FREE(sa, M_SONAME);
	} else {
		error = kern_accept(uap->s, NULL, 0, &uap->sysmsg_result);
	}
	return (error);
}

int
ogetsockname(struct getsockname_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getsockname(uap->fdes, &sa, &sa_len);

	if (error == 0)
		error = compat_43_copyout_sockaddr(sa, uap->asa);
	if (error == 0) {
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	}
	if (sa)
		FREE(sa, M_SONAME);
	return (error);
}

int
ogetpeername(struct ogetpeername_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getpeername(uap->fdes, &sa, &sa_len);

	if (error == 0) {
		error = compat_43_copyout_sockaddr(sa, uap->asa);
	}
	if (error == 0)
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	if (sa)
		FREE(sa, M_SONAME);
	return (error);
}
