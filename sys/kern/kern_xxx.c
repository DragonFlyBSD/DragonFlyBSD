/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)kern_xxx.c	8.2 (Berkeley) 11/14/93
 * $FreeBSD: src/sys/kern/kern_xxx.c,v 1.31 1999/08/28 00:46:15 peter Exp $
 * $DragonFly: src/sys/kern/kern_xxx.c,v 1.10 2006/06/05 07:26:10 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

struct lwkt_token domain_token = LWKT_TOKEN_INITIALIZER(domain_token);

int
sys_uname(struct uname_args *uap)
{
	int name[2], rtval;
	size_t len;
	char *s, *us;

	name[0] = CTL_KERN;
	name[1] = KERN_OSTYPE;
	len = sizeof uap->name->sysname;
	rtval = userland_sysctl(name, 2, uap->name->sysname, &len, 
				1, 0, 0, 0);
	if (rtval)
		goto done;
	subyte(uap->name->sysname + sizeof(uap->name->sysname) - 1, 0);

	name[1] = KERN_HOSTNAME;
	len = sizeof uap->name->nodename;
	rtval = userland_sysctl(name, 2, uap->name->nodename, &len, 
				1, 0, 0, 0);
	if (rtval)
		goto done;
	subyte(uap->name->nodename + sizeof(uap->name->nodename) - 1, 0);

	name[1] = KERN_OSRELEASE;
	len = sizeof uap->name->release;
	rtval = userland_sysctl(name, 2, uap->name->release, &len, 
				1, 0, 0, 0);
	if (rtval)
		goto done;
	subyte(uap->name->release + sizeof(uap->name->release) - 1, 0);

/*
	name = KERN_VERSION;
	len = sizeof uap->name->version;
	rtval = userland_sysctl(name, 2, uap->name->version, &len, 
				1, 0, 0, 0);
	if (rtval)
		goto done;
	subyte(uap->name->version + sizeof(uap->name->version) - 1, 0);
*/

/*
 * this stupid hackery to make the version field look like FreeBSD 1.1
 */
	for(s = version; *s && *s != '#'; s++);

	for(us = uap->name->version; *s && *s != ':'; s++) {
		rtval = subyte( us++, *s);
		if (rtval)
			goto done;
	}
	rtval = subyte( us++, 0);
	if (rtval)
		goto done;

	name[0] = CTL_HW;
	name[1] = HW_MACHINE;
	len = sizeof uap->name->machine;
	rtval = userland_sysctl(name, 2, uap->name->machine, &len, 
				1, 0, 0, 0);
	if (rtval)
		goto done;
	rtval = subyte(uap->name->machine + sizeof(uap->name->machine) - 1, 0);
done:
	return rtval;
}

int
sys_getdomainname(struct getdomainname_args *uap)
{
	int domainnamelen;
	int error;

	lwkt_gettoken_shared(&domain_token);
	domainnamelen = strlen(domainname) + 1;
	if ((u_int)uap->len > domainnamelen + 1)
		uap->len = domainnamelen + 1;
	error = copyout(domainname, uap->domainname, uap->len);
	lwkt_reltoken(&domain_token);
	return (error);
}

int
sys_setdomainname(struct setdomainname_args *uap)
{
	struct thread *td = curthread;
        int error, domainnamelen;

        if ((error = priv_check(td, PRIV_SETDOMAINNAME)))
                return (error);
        if ((u_int)uap->len > sizeof(domainname) - 1)
                return EINVAL;
	lwkt_gettoken(&domain_token);

        domainnamelen = uap->len;
        error = copyin(uap->domainname, domainname, uap->len);
        domainname[domainnamelen] = 0;

	lwkt_reltoken(&domain_token);
        return (error);
}
