/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 */
/*-
 * Copyright (c) 2006 Victor Balada Diaz <victor@bsdes.net>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * $FreeBSD: src/sys/kern/kern_jail.c,v 1.6.2.3 2001/08/17 01:00:26 rwatson Exp $
 */

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysmsg.h>
#include <sys/malloc.h>
#include <sys/nlookup.h>
#include <sys/namecache.h>
#include <sys/proc.h>
#include <sys/caps.h>
#include <sys/jail.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/kern_syscall.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>

static struct prison	*prison_find(int);
static void		prison_ipcache_init(struct prison *);

__read_mostly static prison_cap_t	prison_default_caps;

MALLOC_DEFINE(M_PRISON, "prison", "Prison structures");

SYSCTL_NODE(, OID_AUTO, jail, CTLFLAG_RW, 0,
    "All jails settings");

SYSCTL_NODE(_jail, OID_AUTO, defaults, CTLFLAG_RW, 0,
    "Default options for jails");

/*#define PRISON_DEBUG*/
#ifdef PRISON_DEBUG
__read_mostly static int prison_debug;
SYSCTL_INT(_jail, OID_AUTO, debug, CTLFLAG_RW, &prison_debug, 0,
    "Debug prison refs");
#endif

SYSCTL_BIT64(_jail_defaults, OID_AUTO, set_hostname_allowed, CTLFLAG_RW,
    &prison_default_caps, 1, PRISON_CAP_SYS_SET_HOSTNAME,
    "Processes in jail can set their hostnames");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, socket_unixiproute_only, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_NET_UNIXIPROUTE,
    "Processes in jail are limited to creating UNIX/IPv[46]/route sockets only");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, sysvipc_allowed, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_SYS_SYSVIPC,
    "Processes in jail can use System V IPC primitives");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, chflags_allowed, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_VFS_CHFLAGS,
    "Processes in jail can alter system file flags");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, allow_raw_sockets, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_NET_RAW_SOCKETS,
    "Process in jail can create raw sockets");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, allow_listen_override, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_NET_LISTEN_OVERRIDE,
    "Process in jail can override host wildcard listen");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, vfs_mount_nullfs, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_VFS_MOUNT_NULLFS,
    "Process in jail can mount nullfs(5) filesystems");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, vfs_mount_tmpfs, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_VFS_MOUNT_TMPFS,
    "Process in jail can mount tmpfs(5) filesystems");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, vfs_mount_devfs, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_VFS_MOUNT_DEVFS,
    "Process in jail can mount devfs(5) filesystems");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, vfs_mount_procfs, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_VFS_MOUNT_PROCFS,
    "Process in jail can mount procfs(5) filesystems");

SYSCTL_BIT64(_jail_defaults, OID_AUTO, vfs_mount_fusefs, CTLFLAG_RW,
    &prison_default_caps, 0, PRISON_CAP_VFS_MOUNT_FUSEFS,
    "Process in jail can mount fuse filesystems");

static int	lastprid = 0;
static int	prisoncount = 0;

static struct lock jail_lock =
       LOCK_INITIALIZER("jail", 0, LK_CANRECURSE);

LIST_HEAD(prisonlist, prison);
static struct prisonlist allprison = LIST_HEAD_INITIALIZER(&allprison);

static int
kern_jail_attach(int jid)
{
	struct proc *p = curthread->td_proc;
	struct prison *pr;
	struct ucred *cr;
	int error;

	pr = prison_find(jid);
	if (pr == NULL)
		return(EINVAL);

	error = kern_chroot(&pr->pr_root);
	if (error)
		return(error);

	prison_hold(pr);
	lwkt_gettoken(&p->p_token);
	cr = cratom_proc(p);
	cr->cr_prison = pr;
	p->p_flags |= P_JAILED;
	caps_set_locked(p, SYSCAP_RESTRICTEDROOT, __SYSCAP_ALL);
	lwkt_reltoken(&p->p_token);

	return(0);
}

static int
assign_prison_id(struct prison *pr)
{
	int tryprid;
	struct prison *tpr;

	tryprid = lastprid + 1;
	if (tryprid == JAIL_MAX)
		tryprid = 1;

	lockmgr(&jail_lock, LK_EXCLUSIVE);
next:
	LIST_FOREACH(tpr, &allprison, pr_list) {
		if (tpr->pr_id != tryprid)
			continue;
		tryprid++;
		if (tryprid == JAIL_MAX) {
			lockmgr(&jail_lock, LK_RELEASE);
			return (ERANGE);
		}
		goto next;
	}
	pr->pr_id = lastprid = tryprid;
	lockmgr(&jail_lock, LK_RELEASE);

	return (0);
}

static int
kern_jail(struct prison *pr, struct jail *j)
{
	int error;
	struct nlookupdata nd;

	error = nlookup_init(&nd, j->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}
	error = nlookup(&nd);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}
	cache_copy(&nd.nl_nch, &pr->pr_root);

	varsymset_init(&pr->pr_varsymset, NULL);
	prison_ipcache_init(pr);

	error = assign_prison_id(pr);
	if (error) {
		varsymset_clean(&pr->pr_varsymset);
		nlookup_done(&nd);
		return (error);
	}

	lockmgr(&jail_lock, LK_EXCLUSIVE);
	LIST_INSERT_HEAD(&allprison, pr, pr_list);
	++prisoncount;
	lockmgr(&jail_lock, LK_RELEASE);

	error = prison_sysctl_create(pr);
	if (error)
		goto out;

	error = kern_jail_attach(pr->pr_id);
	if (error)
		goto out2;

	nlookup_done(&nd);
	return 0;

out2:
	prison_sysctl_done(pr);

out:
	lockmgr(&jail_lock, LK_EXCLUSIVE);
	LIST_REMOVE(pr, pr_list);
	--prisoncount;
	lockmgr(&jail_lock, LK_RELEASE);
	varsymset_clean(&pr->pr_varsymset);
	nlookup_done(&nd);
	return (error);
}

/*
 * jail()
 *
 * jail_args(syscallarg(struct jail *) jail)
 *
 * MPALMOSTSAFE
 */
int
sys_jail(struct sysmsg *sysmsg, const struct jail_args *uap)
{
	struct prison *pr;
	struct jail_ip_storage *jip;
	struct jail j;
	int error;
	uint32_t jversion;

	sysmsg->sysmsg_result = -1;

	error = caps_priv_check_self(SYSCAP_NOJAIL_CREATE);
	if (error)
		return (error);

	error = copyin(uap->jail, &jversion, sizeof(jversion));
	if (error)
		return (error);

	pr = kmalloc(sizeof(*pr), M_PRISON, M_WAITOK | M_ZERO);
	SLIST_INIT(&pr->pr_ips);
	lockmgr(&jail_lock, LK_EXCLUSIVE);

	switch (jversion) {
	case 0:
		/* Single IPv4 jails. */
		{
		struct jail_v0 jv0;
		struct sockaddr_in ip4addr;

		error = copyin(uap->jail, &jv0, sizeof(jv0));
		if (error)
			goto out;

		j.path = jv0.path;
		j.hostname = jv0.hostname; 

		jip = kmalloc(sizeof(*jip),  M_PRISON, M_WAITOK | M_ZERO);
		ip4addr.sin_family = AF_INET;
		ip4addr.sin_addr.s_addr = htonl(jv0.ip_number);
		memcpy(&jip->ip, &ip4addr, sizeof(ip4addr));
		SLIST_INSERT_HEAD(&pr->pr_ips, jip, entries);
		break;
		}

	case 1:
		/*
		 * DragonFly multi noIP/IPv4/IPv6 jails
		 *
		 * NOTE: This version is unsupported by FreeBSD
		 * (which uses version 2 instead).
		 */

		error = copyin(uap->jail, &j, sizeof(j));
		if (error)
			goto out;

		for (int i = 0; i < j.n_ips; i++) {
			jip = kmalloc(sizeof(*jip), M_PRISON,
				      M_WAITOK | M_ZERO);
			SLIST_INSERT_HEAD(&pr->pr_ips, jip, entries);
			error = copyin(&j.ips[i], &jip->ip,
					sizeof(struct sockaddr_storage));
			if (error)
				goto out;
		}
		break;
	default:
		error = EINVAL;
		goto out;
	}

	error = copyinstr(j.hostname, &pr->pr_host, sizeof(pr->pr_host), 0);
	if (error)
		goto out;

	/* Use default capabilities as a template */
	pr->pr_caps = prison_default_caps;

	error = kern_jail(pr, &j);
	if (error)
		goto out;

	sysmsg->sysmsg_result = pr->pr_id;
	lockmgr(&jail_lock, LK_RELEASE);

	return (0);

out:
	/* Delete all ips */
	while (!SLIST_EMPTY(&pr->pr_ips)) {
		jip = SLIST_FIRST(&pr->pr_ips);
		SLIST_REMOVE_HEAD(&pr->pr_ips, entries);
		kfree(jip, M_PRISON);
	}
	lockmgr(&jail_lock, LK_RELEASE);
	kfree(pr, M_PRISON);

	return (error);
}

/*
 * int jail_attach(int jid);
 *
 * MPALMOSTSAFE
 */
int
sys_jail_attach(struct sysmsg *sysmsg, const struct jail_attach_args *uap)
{
	int error;

	error = caps_priv_check_self(SYSCAP_NOJAIL_ATTACH);
	if (error)
		return(error);
	lockmgr(&jail_lock, LK_EXCLUSIVE);
	error = kern_jail_attach(uap->jid);
	lockmgr(&jail_lock, LK_RELEASE);
	return (error);
}

static void
prison_ipcache_init(struct prison *pr)
{
	struct jail_ip_storage *jis;
	struct sockaddr_in *ip4;
	struct sockaddr_in6 *ip6;

	lockmgr(&jail_lock, LK_EXCLUSIVE);
	SLIST_FOREACH(jis, &pr->pr_ips, entries) {
		switch (jis->ip.ss_family) {
		case AF_INET:
			ip4 = (struct sockaddr_in *)&jis->ip;
			if ((ntohl(ip4->sin_addr.s_addr) >> IN_CLASSA_NSHIFT) ==
			    IN_LOOPBACKNET) {
				/* loopback address */
				if (pr->local_ip4 == NULL)
					pr->local_ip4 = ip4;
			} else {
				/* public address */
				if (pr->nonlocal_ip4 == NULL)
					pr->nonlocal_ip4 = ip4;
			}
			break;

		case AF_INET6:
			ip6 = (struct sockaddr_in6 *)&jis->ip;
			if (IN6_IS_ADDR_LOOPBACK(&ip6->sin6_addr)) {
				/* loopback address */
				if (pr->local_ip6 == NULL)
					pr->local_ip6 = ip6;
			} else {
				/* public address */
				if (pr->nonlocal_ip6 == NULL)
					pr->nonlocal_ip6 = ip6;
			}
			break;
		}
	}
	lockmgr(&jail_lock, LK_RELEASE);
}

/* 
 * Changes INADDR_LOOPBACK for a valid jail address.
 * ip is in network byte order.
 * Returns 1 if the ip is among jail valid ips.
 * Returns 0 if is not among jail valid ips or
 * if couldn't replace INADDR_LOOPBACK for a valid
 * IP.
 */
int
prison_replace_wildcards(struct thread *td, struct sockaddr *ip)
{
	struct sockaddr_in *ip4 = (struct sockaddr_in *)ip;
	struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)ip;
	struct prison *pr;

	if (td->td_proc == NULL || td->td_ucred == NULL)
		return (1);
	if ((pr = td->td_ucred->cr_prison) == NULL)
		return (1);

	if ((ip->sa_family == AF_INET &&
	    ip4->sin_addr.s_addr == htonl(INADDR_ANY)) ||
	    (ip->sa_family == AF_INET6 &&
	    IN6_IS_ADDR_UNSPECIFIED(&ip6->sin6_addr)))
		return (1);
	if ((ip->sa_family == AF_INET &&
	    ip4->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) ||
	    (ip->sa_family == AF_INET6 &&
	    IN6_IS_ADDR_LOOPBACK(&ip6->sin6_addr))) {
		if (!prison_get_local(pr, ip->sa_family, ip) &&
		    !prison_get_nonlocal(pr, ip->sa_family, ip))
			return(0);
		else
			return(1);
	}
	if (jailed_ip(pr, ip))
		return(1);
	return(0);
}

/*
 * Convert the localhost IP to the actual jail IP
 */
int
prison_remote_ip(struct thread *td, struct sockaddr *ip)
{
	struct sockaddr_in *ip4 = (struct sockaddr_in *)ip;
	struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)ip;
	struct prison *pr;

	if (td == NULL || td->td_proc == NULL || td->td_ucred == NULL)
		return(1);
	if ((pr = td->td_ucred->cr_prison) == NULL)
		return(1);
	if ((ip->sa_family == AF_INET &&
	    ip4->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) ||
	    (ip->sa_family == AF_INET6 &&
	    IN6_IS_ADDR_LOOPBACK(&ip6->sin6_addr))) {
		if (!prison_get_local(pr, ip->sa_family, ip) &&
		    !prison_get_nonlocal(pr, ip->sa_family, ip))
			return(0);
		else
			return(1);
	}
	return(1);
}

/*
 * Convert the jail IP back to localhost
 *
 * Used by getsockname() and getpeername() to convert the in-jail loopback
 * address back to LOCALHOST.  For example, 127.0.0.2 -> 127.0.0.1.  The
 * idea is that programs running inside the jail should be unaware that they
 * are using a different loopback IP than the host.
 */
__read_mostly static struct in6_addr sin6_localhost = IN6ADDR_LOOPBACK_INIT;

int
prison_local_ip(struct thread *td, struct sockaddr *ip)
{
	struct sockaddr_in *ip4 = (struct sockaddr_in *)ip;
	struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)ip;
	struct prison *pr;

	if (td == NULL || td->td_proc == NULL || td->td_ucred == NULL)
		return(1);
	if ((pr = td->td_ucred->cr_prison) == NULL)
		return(1);
	if (ip->sa_family == AF_INET && pr->local_ip4 &&
	    pr->local_ip4->sin_addr.s_addr == ip4->sin_addr.s_addr &&
	    pr->local_ip4->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
		ip4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return(0);
	}
	if (ip->sa_family == AF_INET6 && pr->local_ip6 &&
	    bcmp(&pr->local_ip6->sin6_addr, &ip6->sin6_addr,
		 sizeof(ip6->sin6_addr)) == 0) {
		bcopy(&sin6_localhost, &ip6->sin6_addr, sizeof(ip6->sin6_addr));
		return(0);
	}
	return(1);
}

/*
 * Prison get non loopback ip:
 * - af is the address family of the ip we want (AF_INET|AF_INET6).
 * - If ip != NULL, put the first IP address that is not a loopback address
 *   into *ip.
 *
 * ip is in network by order and we don't touch it unless we find a valid ip.
 * No matter if ip == NULL or not, we return either a valid struct sockaddr *,
 * or NULL.  This struct may not be modified.
 */
struct sockaddr *
prison_get_nonlocal(struct prison *pr, sa_family_t af, struct sockaddr *ip)
{
	struct sockaddr_in *ip4 = (struct sockaddr_in *)ip;
	struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)ip;

	/* Check if it is cached */
	switch(af) {
	case AF_INET:
		if (ip4 != NULL && pr->nonlocal_ip4 != NULL)
			ip4->sin_addr.s_addr = pr->nonlocal_ip4->sin_addr.s_addr;
		return (struct sockaddr *)pr->nonlocal_ip4;

	case AF_INET6:
		if (ip6 != NULL && pr->nonlocal_ip6 != NULL)
			ip6->sin6_addr = pr->nonlocal_ip6->sin6_addr;
		return (struct sockaddr *)pr->nonlocal_ip6;
	}

	/* NOTREACHED */
	return NULL;
}

/*
 * Prison get loopback ip.
 * - af is the address family of the ip we want (AF_INET|AF_INET6).
 * - If ip != NULL, put the first IP address that is not a loopback address
 *   into *ip.
 *
 * ip is in network by order and we don't touch it unless we find a valid ip.
 * No matter if ip == NULL or not, we return either a valid struct sockaddr *,
 * or NULL.  This struct may not be modified.
 */
struct sockaddr *
prison_get_local(struct prison *pr, sa_family_t af, struct sockaddr *ip)
{
	struct sockaddr_in *ip4 = (struct sockaddr_in *)ip;
	struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)ip;

	/* Check if it is cached */
	switch(af) {
	case AF_INET:
		if (ip4 != NULL && pr->local_ip4 != NULL)
			ip4->sin_addr.s_addr = pr->local_ip4->sin_addr.s_addr;
		return (struct sockaddr *)pr->local_ip4;

	case AF_INET6:
		if (ip6 != NULL && pr->local_ip6 != NULL)
			ip6->sin6_addr = pr->local_ip6->sin6_addr;
		return (struct sockaddr *)pr->local_ip6;
	}

	/* NOTREACHED */
	return NULL;
}

/* Check if the IP is among ours, if it is return 1, else 0 */
int
jailed_ip(struct prison *pr, const struct sockaddr *ip)
{
	const struct jail_ip_storage *jis;
	const struct sockaddr_in *jip4, *ip4;
	const struct sockaddr_in6 *jip6, *ip6;

	if (pr == NULL)
		return(0);
	ip4 = (const struct sockaddr_in *)ip;
	ip6 = (const struct sockaddr_in6 *)ip;

	lockmgr(&jail_lock, LK_EXCLUSIVE);
	SLIST_FOREACH(jis, &pr->pr_ips, entries) {
		switch (ip->sa_family) {
		case AF_INET:
			jip4 = (const struct sockaddr_in *) &jis->ip;
			if (jip4->sin_family == AF_INET &&
			    ip4->sin_addr.s_addr == jip4->sin_addr.s_addr) {
				lockmgr(&jail_lock, LK_RELEASE);
				return(1);
			}
			break;
		case AF_INET6:
			jip6 = (const struct sockaddr_in6 *) &jis->ip;
			if (jip6->sin6_family == AF_INET6 &&
			    IN6_ARE_ADDR_EQUAL(&ip6->sin6_addr,
				&jip6->sin6_addr)) {
				lockmgr(&jail_lock, LK_RELEASE);
				return(1);
			}
			break;
		}
	}
	lockmgr(&jail_lock, LK_RELEASE);
	/* Ip not in list */
	return(0);
}

int
prison_if(struct ucred *cred, struct sockaddr *sa)
{
	struct prison *pr;
	struct sockaddr_in *sai = (struct sockaddr_in*) sa;

	pr = cred->cr_prison;

	if (((sai->sin_family != AF_INET) && (sai->sin_family != AF_INET6))
	    && PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_NET_UNIXIPROUTE))
		return(1);
	else if ((sai->sin_family != AF_INET) && (sai->sin_family != AF_INET6))
		return(0);
	else if (jailed_ip(pr, sa))
		return(0);
	return(1);
}

/*
 * Returns a prison instance, or NULL on failure.
 */
static struct prison *
prison_find(int prid)
{
	struct prison *pr;

	lockmgr(&jail_lock, LK_EXCLUSIVE);
	LIST_FOREACH(pr, &allprison, pr_list) {
		if (pr->pr_id == prid)
			break;
	}
	lockmgr(&jail_lock, LK_RELEASE);

	return(pr);
}

static int
sysctl_jail_list(SYSCTL_HANDLER_ARGS)
{
	struct thread *td = curthread;
	struct jail_ip_storage *jip;
#ifdef INET6
	struct sockaddr_in6 *jsin6;
#endif
	struct sockaddr_in *jsin;
	struct lwp *lp;
	struct prison *pr;
	unsigned int jlssize, jlsused;
	int count, error;
	char *jls; /* Jail list */
	char *oip; /* Output ip */
	char *fullpath, *freepath;

	jlsused = 0;

	if (jailed(td->td_ucred))
		return (0);
	lp = td->td_lwp;
retry:
	count = prisoncount;

	if (count == 0)
		return(0);

	jlssize = (count * 1024);
	jls = kmalloc(jlssize + 1, M_TEMP, M_WAITOK | M_ZERO);
	if (count < prisoncount) {
		kfree(jls, M_TEMP);
		goto retry;
	}
	count = prisoncount;

	lockmgr(&jail_lock, LK_EXCLUSIVE);
	LIST_FOREACH(pr, &allprison, pr_list) {
		error = cache_fullpath(lp->lwp_proc, &pr->pr_root, NULL,
					&fullpath, &freepath, 0);
		if (error)
			continue;
		if (jlsused && jlsused < jlssize)
			jls[jlsused++] = '\n';
		count = ksnprintf(jls + jlsused, (jlssize - jlsused),
				 "%d %s %s",
				 pr->pr_id, pr->pr_host, fullpath);
		kfree(freepath, M_TEMP);
		if (count < 0)
			goto end;
		jlsused += count;

		/* Copy the IPS */
		SLIST_FOREACH(jip, &pr->pr_ips, entries) {
			char buf[INET_ADDRSTRLEN];

			jsin = (struct sockaddr_in *)&jip->ip;

			switch(jsin->sin_family) {
			case AF_INET:
				oip = kinet_ntoa(jsin->sin_addr, buf);
				break;
#ifdef INET6
			case AF_INET6:
				jsin6 = (struct sockaddr_in6 *)&jip->ip;
				oip = ip6_sprintf(&jsin6->sin6_addr);
				break;
#endif
			default:
				oip = "?family?";
				break;
			}

			if ((jlssize - jlsused) < (strlen(oip) + 1)) {
				error = ERANGE;
				goto end;
			}
			count = ksnprintf(jls + jlsused, (jlssize - jlsused),
					  " %s", oip);
			if (count < 0)
				goto end;
			jlsused += count;
		}
	}

	/* 
	 * The format is:
	 * pr_id <SPC> hostname1 <SPC> PATH1 <SPC> IP1 <SPC> IP2\npr_id...
	 */
	error = SYSCTL_OUT(req, jls, jlsused);
end:
	lockmgr(&jail_lock, LK_RELEASE);
	kfree(jls, M_TEMP);

	return(error);
}

SYSCTL_OID(_jail, OID_AUTO, list, CTLTYPE_STRING | CTLFLAG_RD, NULL, 0,
	   sysctl_jail_list, "A", "List of active jails");

static int
sysctl_jail_jailed(SYSCTL_HANDLER_ARGS)
{
	int error, injail;

	injail = jailed(req->td->td_ucred);
	error = SYSCTL_OUT(req, &injail, sizeof(injail));

	return (error);
}

SYSCTL_PROC(_jail, OID_AUTO, jailed,
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_NOLOCK, NULL, 0,
	    sysctl_jail_jailed, "I", "Process in jail?");

/*
 * MPSAFE
 */
void
prison_hold(struct prison *pr)
{
	atomic_add_int(&pr->pr_ref, 1);
#ifdef PRISON_DEBUG
	if (prison_debug > 0) {
		--prison_debug;
		print_backtrace(-1);
	}
#endif
}

/*
 * MPALMOSTSAFE
 */
void
prison_free(struct prison *pr)
{
	struct jail_ip_storage *jls;

#ifdef PRISON_DEBUG
	if (prison_debug > 0) {
		--prison_debug;
		print_backtrace(-1);
	}
#endif
	KKASSERT(pr->pr_ref > 0);
	if (atomic_fetchadd_int(&pr->pr_ref, -1) != 1)
		return;

	/*
	 * The global jail lock is needed on the last ref to adjust
	 * the list.
	 */
	lockmgr(&jail_lock, LK_EXCLUSIVE);
	if (pr->pr_ref) {
		lockmgr(&jail_lock, LK_RELEASE);
		return;
	}
	LIST_REMOVE(pr, pr_list);
	--prisoncount;

	/*
	 * Clean up
	 */
	while (!SLIST_EMPTY(&pr->pr_ips)) {
		jls = SLIST_FIRST(&pr->pr_ips);
		SLIST_REMOVE_HEAD(&pr->pr_ips, entries);
		kfree(jls, M_PRISON);
	}
	lockmgr(&jail_lock, LK_RELEASE);

	if (pr->pr_linux != NULL)
		kfree(pr->pr_linux, M_PRISON);
	varsymset_clean(&pr->pr_varsymset);

	/* Release the sysctl tree */
	prison_sysctl_done(pr);

	cache_drop(&pr->pr_root);
	kfree(pr, M_PRISON);
}

/*
 * Check if permisson for a specific privilege is granted within jail.
 *
 * MPSAFE
 */
int
prison_priv_check(struct ucred *cred, int cap)
{
	struct prison *pr = cred->cr_prison;

	if (!jailed(cred))
		return (0);

	switch (cap & ~__SYSCAP_XFLAGS) {
	case SYSCAP_RESTRICTEDROOT:		/* meta group 1 */
		/* RESTRICTEDROOT fallbacks disallowed in jails */
		return EPERM;
	case SYSCAP_SENSITIVEROOT:		/* meta group 2 */
	case SYSCAP_NOEXEC:			/* meta group 3 */
	case SYSCAP_NOCRED:			/* meta group 4 */
		return 0;
	case SYSCAP_NOJAIL:			/* meta group 5 */
		/* all jail ops disallowed in jails */
		return EPERM;
	case SYSCAP_NONET:			/* meta group 6 */
		return 0;
	case SYSCAP_NONET_SENSITIVE:		/* meta group 7 */
		/* all sensitive network ops disallowed in jails */
		return EPERM;
	case SYSCAP_NOVFS:			/* meta group 8 */
	case SYSCAP_NOVFS_SENSITIVE:		/* meta group 9 */
	case SYSCAP_NOMOUNT:			/* meta group 10 */
	case SYSCAP_NO11:			/* meta group 11 */
	case SYSCAP_NO12:			/* meta group 12 */
	case SYSCAP_NO13:			/* meta group 13 */
	case SYSCAP_NO14:			/* meta group 14 */
	case SYSCAP_NO15:			/* meta group 15 */
		return (0);

	/* ----- */				/* group 1 - disallowed */

	case SYSCAP_NOPROC_TRESPASS:		/* group 2 allowed */
	case SYSCAP_NOPROC_SETLOGIN:
	case SYSCAP_NOPROC_SETRLIMIT:
	case SYSCAP_NOSYSCTL_WR:
	case SYSCAP_NOVARSYM_SYS:
	case SYSCAP_NOSETHOSTNAME:
	case SYSCAP_NOQUOTA_WR:
	case SYSCAP_NODEBUG_UNPRIV:
	case SYSCAP_NOSCHED:
	case SYSCAP_NOSCHED_CPUSET:
	case SYSCAP_NOSETTIME:
		return (0);

	case SYSCAP_NOEXEC_SUID:		/* group 3 allowed */
	case SYSCAP_NOEXEC_SGID:
		return (0);

	case SYSCAP_NOCRED_SETUID:		/* group 4 allowed */
	case SYSCAP_NOCRED_SETGID:
	case SYSCAP_NOCRED_SETEUID:
	case SYSCAP_NOCRED_SETEGID:
	case SYSCAP_NOCRED_SETREUID:
	case SYSCAP_NOCRED_SETREGID:
	case SYSCAP_NOCRED_SETRESUID:
	case SYSCAP_NOCRED_SETRESGID:
	case SYSCAP_NOCRED_SETGROUPS:
		return (0);

	case SYSCAP_NOJAIL_CREATE:		/* group 5 disallowed */
	case SYSCAP_NOJAIL_ATTACH:
		return EPERM;

	case SYSCAP_NONET_RESPORT:		/* group 6 mostly allowed */
		/*
		 * Allow reserved ports
		 */
		return 0;
	case SYSCAP_NONET_RAW:
		/*
		 * Conditionally allow creating raw sockets in jail.
		 */
		if (PRISON_CAP_ISSET(pr->pr_caps,
			PRISON_CAP_NET_RAW_SOCKETS))
			return (0);
		else
			return (EPERM);

	/* ----- */				/* group 7 - disallowed */

	case SYSCAP_NOVFS_SYSFLAGS:		/* group 8 - allowed */
	case SYSCAP_NOVFS_CHOWN:
	case SYSCAP_NOVFS_CHMOD:
	case SYSCAP_NOVFS_LINK:
	case SYSCAP_NOVFS_CHFLAGS_DEV:
	case SYSCAP_NOVFS_SETATTR:
	case SYSCAP_NOVFS_SETGID:
	case SYSCAP_NOVFS_GENERATION:
	case SYSCAP_NOVFS_RETAINSUGID:
		return (0);

	case SYSCAP_NOVFS_MKNOD_BAD:		/* group 9 - allowed */
	case SYSCAP_NOVFS_MKNOD_WHT:
	case SYSCAP_NOVFS_MKNOD_DIR:
	case SYSCAP_NOVFS_MKNOD_DEV:
	case SYSCAP_NOVFS_IOCTL:
	case SYSCAP_NOVFS_CHROOT:
	case SYSCAP_NOVFS_REVOKE:
		return (0);

	case SYSCAP_NOMOUNT_NULLFS:		/* group 10 - conditional */
		if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_VFS_MOUNT_NULLFS))
			return (0);
		else
			return (EPERM);
	case SYSCAP_NOMOUNT_DEVFS:
		if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_VFS_MOUNT_DEVFS))
			return (0);
		else
			return (EPERM);
	case SYSCAP_NOMOUNT_TMPFS:
		if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_VFS_MOUNT_TMPFS))
			return (0);
		else
			return (EPERM);
	case SYSCAP_NOMOUNT_PROCFS:
		if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_VFS_MOUNT_PROCFS))
			return (0);
		else
			return (EPERM);
	case SYSCAP_NOMOUNT_FUSE:
		if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_VFS_MOUNT_FUSEFS))
			return (0);
		else
			return (EPERM);
	case SYSCAP_NOMOUNT_UMOUNT:
		return (0);

	default:
		/* otherwise disallow */
		return (EPERM);
	}
}


/*
 * Create a per-jail sysctl tree to control the prison
 */
int
prison_sysctl_create(struct prison *pr)
{
	char id_str[7];

	ksnprintf(id_str, 6, "%d", pr->pr_id);

	pr->pr_sysctl_ctx = (struct sysctl_ctx_list *) kmalloc(
		sizeof(struct sysctl_ctx_list), M_PRISON, M_WAITOK | M_ZERO);

	sysctl_ctx_init(pr->pr_sysctl_ctx);

	/* Main jail node */
	pr->pr_sysctl_tree = SYSCTL_ADD_NODE(pr->pr_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_jail),
	    OID_AUTO, id_str, CTLFLAG_RD, 0,
	    "Jail specific settings");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "sys_set_hostname", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_SYS_SET_HOSTNAME,
	    "Processes in jail can set their hostnames");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "sys_sysvipc", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_SYS_SYSVIPC,
	    "Processes in jail can use System V IPC primitives");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "net_unixiproute", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_NET_UNIXIPROUTE,
	    "Processes in jail are limited to creating UNIX/IPv[46]/route sockets only");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "net_raw_sockets", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_NET_RAW_SOCKETS,
	    "Process in jail can create raw sockets");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "allow_listen_override", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_NET_LISTEN_OVERRIDE,
	    "Process in jail can create raw sockets");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "vfs_chflags", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_VFS_CHFLAGS,
	    "Process in jail can override host wildcard listen");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "vfs_mount_nullfs", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_VFS_MOUNT_NULLFS,
	    "Processes in jail can mount nullfs(5) filesystems");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "vfs_mount_tmpfs", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_VFS_MOUNT_TMPFS,
	    "Processes in jail can mount tmpfs(5) filesystems");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "vfs_mount_devfs", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_VFS_MOUNT_DEVFS,
	    "Processes in jail can mount devfs(5) filesystems");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "vfs_mount_procfs", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_VFS_MOUNT_PROCFS,
	    "Processes in jail can mount procfs(5) filesystems");

	SYSCTL_ADD_BIT64(pr->pr_sysctl_ctx, SYSCTL_CHILDREN(pr->pr_sysctl_tree),
	    OID_AUTO, "vfs_mount_fusefs", CTLFLAG_RW,
	    &pr->pr_caps, 0, PRISON_CAP_VFS_MOUNT_FUSEFS,
	    "Processes in jail can mount fuse filesystems");

	return 0;
}

int
prison_sysctl_done(struct prison *pr)
{
	if (pr->pr_sysctl_tree) {
		sysctl_ctx_free(pr->pr_sysctl_ctx);
		kfree(pr->pr_sysctl_ctx, M_PRISON);
		pr->pr_sysctl_tree = NULL;
	}

	return 0;
}
