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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_hostinfo.c,v 1.4 2006/09/05 00:55:44 dillon Exp $
 *	from: DragonFly kern/kern_xxx.c,v 1.7
 *	from: DragonFly kern/kern_sysctl.c,v 1.12
 *
 * These syscalls used to live in kern/kern_xxx.c and kern/kern_sysctl.c.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <vm/vm_param.h>

int
sys_ogethostname(struct gethostname_args *uap)
{
	size_t len;
	char *hostname;
	int error, name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	len = MIN(uap->len, MAXHOSTNAMELEN);
	hostname = kmalloc(MAXHOSTNAMELEN, M_TEMP, M_WAITOK);

	error = kernel_sysctl(name, 2, hostname, &len, NULL, 0, NULL);

	if (error == 0)
		error = copyout(hostname, uap->hostname, len);

	kfree(hostname, M_TEMP);
	return (error);
}

int
sys_osethostname(struct sethostname_args *uap)
{
	struct thread *td = curthread;
	size_t len;
	char *hostname;
	int name[2];
	int error;

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	error = priv_check_cred(td->td_ucred, PRIV_SETHOSTNAME, 0);
	if (error)
		return (error);
	len = MIN(uap->len, MAXHOSTNAMELEN);
	hostname = kmalloc(MAXHOSTNAMELEN, M_TEMP, M_WAITOK);

	error = copyin(uap->hostname, hostname, len);
	if (error) {
		kfree(hostname, M_TEMP);
		return (error);
	}

	error = kernel_sysctl(name, 2, NULL, 0, hostname, len, NULL);

	kfree(hostname, M_TEMP);
	return (error);
}

/*
 * MPSAFE
 */
int
sys_ogethostid(struct ogethostid_args *uap)
{
	uap->sysmsg_lresult = hostid;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_osethostid(struct osethostid_args *uap)
{
	struct thread *td = curthread;
	int error;

	error = priv_check(td, PRIV_ROOT);
	if (error)
		return (error);
	hostid = uap->hostid;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_oquota(struct oquota_args *uap)
{
	return (ENOSYS);
}

#define	KINFO_PROC		(0<<8)
#define	KINFO_RT		(1<<8)
#define	KINFO_VNODE		(2<<8)
#define	KINFO_FILE		(3<<8)
#define	KINFO_METER		(4<<8)
#define	KINFO_LOADAVG		(5<<8)
#define	KINFO_CLOCKRATE		(6<<8)

/* Non-standard BSDI extension - only present on their 4.3 net-2 releases */
#define	KINFO_BSDI_SYSINFO	(101<<8)

/*
 * XXX this is bloat, but I hope it's better here than on the potentially
 * limited kernel stack...  -Peter
 */

static struct {
	int	bsdi_machine;		/* "i386" on BSD/386 */
/*      ^^^ this is an offset to the string, relative to the struct start */
	char	*pad0;
	long	pad1;
	long	pad2;
	long	pad3;
	u_long	pad4;
	u_long	pad5;
	u_long	pad6;

	int	bsdi_ostype;		/* "BSD/386" on BSD/386 */
	int	bsdi_osrelease;		/* "1.1" on BSD/386 */
	long	pad7;
	long	pad8;
	char	*pad9;

	long	pad10;
	long	pad11;
	int	pad12;
	long	pad13;
	quad_t	pad14;
	long	pad15;

	struct	timeval pad16;
	/* we dont set this, because BSDI's uname used gethostname() instead */
	int	bsdi_hostname;		/* hostname on BSD/386 */

	/* the actual string data is appended here */

} bsdi_si;
/*
 * this data is appended to the end of the bsdi_si structure during copyout.
 * The "char *" offsets are relative to the base of the bsdi_si struct.
 * This contains "FreeBSD\02.0-BUILT-nnnnnn\0i386\0", and these strings
 * should not exceed the length of the buffer here... (or else!! :-)
 */
static char bsdi_strings[80];	/* It had better be less than this! */

/*
 * MPALMOSTSAFE
 */
int
sys_ogetkerninfo(struct getkerninfo_args *uap)
{
	int error, name[6];
	size_t size;
	u_int needed = 0;

	switch (uap->op & 0xff00) {
	case KINFO_RT:
		name[0] = CTL_NET;
		name[1] = PF_ROUTE;
		name[2] = 0;
		name[3] = (uap->op & 0xff0000) >> 16;
		name[4] = uap->op & 0xff;
		name[5] = uap->arg;
		error = userland_sysctl(name, 6, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_VNODE:
		name[0] = CTL_KERN;
		name[1] = KERN_VNODE;
		error = userland_sysctl(name, 2, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_PROC:
		name[0] = CTL_KERN;
		name[1] = KERN_PROC;
		name[2] = uap->op & 0xff;
		name[3] = uap->arg;
		error = userland_sysctl(name, 4, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_FILE:
		name[0] = CTL_KERN;
		name[1] = KERN_FILE;
		error = userland_sysctl(name, 2, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_METER:
		name[0] = CTL_VM;
		name[1] = VM_METER;
		error = userland_sysctl(name, 2, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_LOADAVG:
		name[0] = CTL_VM;
		name[1] = VM_LOADAVG;
		error = userland_sysctl(name, 2, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_CLOCKRATE:
		name[0] = CTL_KERN;
		name[1] = KERN_CLOCKRATE;
		error = userland_sysctl(name, 2, uap->where, uap->size,
			0, 0, 0, &size);
		break;

	case KINFO_BSDI_SYSINFO: {
		/*
		 * this is pretty crude, but it's just enough for uname()
		 * from BSDI's 1.x libc to work.
		 * *size gives the size of the buffer before the call, and
		 * the amount of data copied after a successful call.
		 * If successful, the return value is the amount of data
		 * available, which can be larger than *size.
		 *
		 * BSDI's 2.x product apparently fails with ENOMEM if
		 * *size is too small.
		 */

		u_int left;
		char *s;

		bzero((char *)&bsdi_si, sizeof(bsdi_si));
		bzero(bsdi_strings, sizeof(bsdi_strings));

		s = bsdi_strings;

		bsdi_si.bsdi_ostype = (s - bsdi_strings) + sizeof(bsdi_si);
		strcpy(s, ostype);
		s += strlen(s) + 1;

		bsdi_si.bsdi_osrelease = (s - bsdi_strings) + sizeof(bsdi_si);
		strcpy(s, osrelease);
		s += strlen(s) + 1;

		bsdi_si.bsdi_machine = (s - bsdi_strings) + sizeof(bsdi_si);
		strcpy(s, machine);
		s += strlen(s) + 1;

		needed = sizeof(bsdi_si) + (s - bsdi_strings);

		if (uap->where == NULL || (uap->size == NULL)) {
			/* process is asking how much buffer to supply.. */
			size = needed;
			error = 0;
			break;
		}

		if ((error = copyin(uap->size, &size, sizeof(size))) != 0)
				break;

		/* if too much buffer supplied, trim it down */
		if (size > needed)
			size = needed;

		/* how much of the buffer is remaining */
		left = size;

		if ((error = copyout((char *)&bsdi_si, uap->where, left)) != 0)
			break;

		/* is there any point in continuing? */
		if (left > sizeof(bsdi_si)) {
			left -= sizeof(bsdi_si);
			error = copyout(&bsdi_strings,
					uap->where + sizeof(bsdi_si), left);
		}
		break;
	}
	default:
		error = EOPNOTSUPP;
		break;
	}

	if (error)
		return (error);
	uap->sysmsg_iresult = (int)size;
	if (uap->size)
		error = copyout(&size, uap->size, sizeof(size));
	return (error);
}
