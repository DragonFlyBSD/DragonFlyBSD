/*
 * 43BSD_HOSTINFO.C	- 4.3BSD compatibility host info syscalls
 *
 * Copyright (c) 1982, 1986, 1989, 1993
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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_hostinfo.c,v 1.1 2003/11/14 01:53:54 daver Exp $
 *	from: DragonFly kern/kern_xxx.c,v 1.7
 *
 * These syscalls used to live in kern/kern_xxx.c.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

int
ogethostname(struct gethostname_args *uap)
{
	size_t len;
	char *hostname;
	int error, name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	len = MIN(uap->len, MAXHOSTNAMELEN);
	hostname = malloc(MAXHOSTNAMELEN, M_TEMP, M_WAITOK);

	error = kernel_sysctl(name, 2, hostname, &len, NULL, 0, NULL);

	if (error == 0)
		error = copyout(hostname, uap->hostname, len);

	free(hostname, M_TEMP);
	return (error);
}

int
osethostname(struct sethostname_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	size_t len;
	char *hostname;
	int name[2];
	int error;

	KKASSERT(p);
	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	error = suser_cred(p->p_ucred, PRISON_ROOT);
	if (error)
		return (error);
	len = MIN(uap->len, MAXHOSTNAMELEN);
	hostname = malloc(MAXHOSTNAMELEN, M_TEMP, M_WAITOK);

	error = copyin(uap->hostname, hostname, len);
	if (error) {
		free(hostname, M_TEMP);
		return (error);
	}

	error = kernel_sysctl(name, 2, NULL, 0, hostname, len, NULL);

	free(hostname, M_TEMP);
	return (error);
}

int
ogethostid(struct ogethostid_args *uap)
{
	uap->sysmsg_lresult = hostid;
	return (0);
}

int
osethostid(struct osethostid_args *uap)
{
	struct thread *td = curthread;
	int error;

	error = suser(td);
	if (error)
		return (error);
	hostid = uap->hostid;
	return (0);
}

int
oquota(struct oquota_args *uap)
{
	return (ENOSYS);
}
