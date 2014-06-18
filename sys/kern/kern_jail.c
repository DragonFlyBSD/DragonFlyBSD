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
 * $DragonFly: src/sys/kern/kern_jail.c,v 1.19 2008/05/17 18:20:33 dillon Exp $
 */

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/nlookup.h>
#include <sys/namecache.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/jail.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/kern_syscall.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>

#include <sys/mplock2.h>

static struct prison	*prison_find(int);
static void		prison_ipcache_init(struct prison *);

MALLOC_DEFINE(M_PRISON, "prison", "Prison structures");

SYSCTL_NODE(, OID_AUTO, jail, CTLFLAG_RW, 0,
    "Jail rules");

int	jail_set_hostname_allowed = 1;
SYSCTL_INT(_jail, OID_AUTO, set_hostname_allowed, CTLFLAG_RW,
    &jail_set_hostname_allowed, 0,
    "Processes in jail can set their hostnames");

int	jail_socket_unixiproute_only = 1;
SYSCTL_INT(_jail, OID_AUTO, socket_unixiproute_only, CTLFLAG_RW,
    &jail_socket_unixiproute_only, 0,
    "Processes in jail are limited to creating UNIX/IPv[46]/route sockets only");

int	jail_sysvipc_allowed = 0;
SYSCTL_INT(_jail, OID_AUTO, sysvipc_allowed, CTLFLAG_RW,
    &jail_sysvipc_allowed, 0,
    "Processes in jail can use System V IPC primitives");

int    jail_chflags_allowed = 0;
SYSCTL_INT(_jail, OID_AUTO, chflags_allowed, CTLFLAG_RW,
    &jail_chflags_allowed, 0,
    "Process in jail can set chflags(1)");

int    jail_allow_raw_sockets = 0;
SYSCTL_INT(_jail, OID_AUTO, allow_raw_sockets, CTLFLAG_RW,
    &jail_allow_raw_sockets, 0,
    "Process in jail can create raw sockets");

int	lastprid = 0;
int	prisoncount = 0;

LIST_HEAD(prisonlist, prison);
struct	prisonlist allprison = LIST_HEAD_INITIALIZER(&allprison);

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
next:
	LIST_FOREACH(tpr, &allprison, pr_list) {
		if (tpr->pr_id != tryprid)
			continue;
		tryprid++;
		if (tryprid == JAIL_MAX) {
			return (ERANGE);
		}
		goto next;
	}
	pr->pr_id = lastprid = tryprid;

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
		
	LIST_INSERT_HEAD(&allprison, pr, pr_list);
	++prisoncount;

	error = kern_jail_attach(pr->pr_id);
	if (error) {
		LIST_REMOVE(pr, pr_list);
		--prisoncount;
		varsymset_clean(&pr->pr_varsymset);
	}
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
sys_jail(struct jail_args *uap)
{
	struct thread *td = curthread;
	struct prison *pr;
	struct jail_ip_storage *jip;
	struct jail j;
	int error;
	uint32_t jversion;

	uap->sysmsg_result = -1;

	error = priv_check(td, PRIV_JAIL_CREATE);
	if (error)
		return (error);

	error = copyin(uap->jail, &jversion, sizeof(jversion));
	if (error)
		return (error);

	pr = kmalloc(sizeof(*pr), M_PRISON, M_WAITOK | M_ZERO);
	SLIST_INIT(&pr->pr_ips);
	get_mplock();

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

	error = kern_jail(pr, &j);
	if (error)
		goto out;

	uap->sysmsg_result = pr->pr_id;
	rel_mplock();
	return (0);

out:
	/* Delete all ips */
	while (!SLIST_EMPTY(&pr->pr_ips)) {
		jip = SLIST_FIRST(&pr->pr_ips);
		SLIST_REMOVE_HEAD(&pr->pr_ips, entries);
		kfree(jip, M_PRISON);
	}
	rel_mplock();
	kfree(pr, M_PRISON);
	return (error);
}

/*
 * int jail_attach(int jid);
 *
 * MPALMOSTSAFE
 */
int
sys_jail_attach(struct jail_attach_args *uap)
{
	struct thread *td = curthread;
	int error;

	error = priv_check(td, PRIV_JAIL_ATTACH);
	if (error)
		return(error);
	get_mplock();
	error = kern_jail_attach(uap->jid);
	rel_mplock();
	return (error);
}

static void
prison_ipcache_init(struct prison *pr)
{
	struct jail_ip_storage *jis;
	struct sockaddr_in *ip4;
	struct sockaddr_in6 *ip6;

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
jailed_ip(struct prison *pr, struct sockaddr *ip)
{
	struct jail_ip_storage *jis;
	struct sockaddr_in *jip4, *ip4;
	struct sockaddr_in6 *jip6, *ip6;

	if (pr == NULL)
		return(0);
	ip4 = (struct sockaddr_in *)ip;
	ip6 = (struct sockaddr_in6 *)ip;
	SLIST_FOREACH(jis, &pr->pr_ips, entries) {
		switch (ip->sa_family) {
		case AF_INET:
			jip4 = (struct sockaddr_in *) &jis->ip;
			if (jip4->sin_family == AF_INET &&
			    ip4->sin_addr.s_addr == jip4->sin_addr.s_addr)
				return(1);
			break;
		case AF_INET6:
			jip6 = (struct sockaddr_in6 *) &jis->ip;
			if (jip6->sin6_family == AF_INET6 &&
			    IN6_ARE_ADDR_EQUAL(&ip6->sin6_addr,
					       &jip6->sin6_addr))
				return(1);
			break;
		}
	}
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
	    && jail_socket_unixiproute_only)
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

	LIST_FOREACH(pr, &allprison, pr_list) {
		if (pr->pr_id == prid)
			break;
	}
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
			jsin = (struct sockaddr_in *)&jip->ip;

			switch(jsin->sin_family) {
			case AF_INET:
				oip = inet_ntoa(jsin->sin_addr);
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
	kfree(jls, M_TEMP);
	return(error);
}

SYSCTL_OID(_jail, OID_AUTO, list, CTLTYPE_STRING | CTLFLAG_RD, NULL, 0,
	   sysctl_jail_list, "A", "List of active jails");

/*
 * MPSAFE
 */
void
prison_hold(struct prison *pr)
{
	atomic_add_int(&pr->pr_ref, 1);
}

/*
 * MPALMOSTSAFE
 */
void
prison_free(struct prison *pr)
{
	struct jail_ip_storage *jls;

	KKASSERT(pr->pr_ref > 0);
	if (atomic_fetchadd_int(&pr->pr_ref, -1) != 1)
		return;

	/*
	 * The MP lock is needed on the last ref to adjust
	 * the list.
	 */
	get_mplock();
	if (pr->pr_ref) {
		rel_mplock();
		return;
	}
	LIST_REMOVE(pr, pr_list);
	--prisoncount;
	rel_mplock();

	/*
	 * Clean up
	 */
	while (!SLIST_EMPTY(&pr->pr_ips)) {
		jls = SLIST_FIRST(&pr->pr_ips);
		SLIST_REMOVE_HEAD(&pr->pr_ips, entries);
		kfree(jls, M_PRISON);
	}

	if (pr->pr_linux != NULL)
		kfree(pr->pr_linux, M_PRISON);
	varsymset_clean(&pr->pr_varsymset);
	cache_drop(&pr->pr_root);
	kfree(pr, M_PRISON);
}

/*
 * Check if permisson for a specific privilege is granted within jail.
 *
 * MPSAFE
 */
int
prison_priv_check(struct ucred *cred, int priv)
{
	if (!jailed(cred))
		return (0);

	switch (priv) {
	case PRIV_CRED_SETUID:
	case PRIV_CRED_SETEUID:
	case PRIV_CRED_SETGID:
	case PRIV_CRED_SETEGID:
	case PRIV_CRED_SETGROUPS:
	case PRIV_CRED_SETREUID:
	case PRIV_CRED_SETREGID:
	case PRIV_CRED_SETRESUID:
	case PRIV_CRED_SETRESGID:

	case PRIV_VFS_SYSFLAGS:
	case PRIV_VFS_CHOWN:
	case PRIV_VFS_CHMOD:
	case PRIV_VFS_CHROOT:
	case PRIV_VFS_LINK:
	case PRIV_VFS_CHFLAGS_DEV:
	case PRIV_VFS_REVOKE:
	case PRIV_VFS_MKNOD_BAD:
	case PRIV_VFS_MKNOD_WHT:
	case PRIV_VFS_MKNOD_DIR:
	case PRIV_VFS_SETATTR:
	case PRIV_VFS_SETGID:

	case PRIV_PROC_SETRLIMIT:
	case PRIV_PROC_SETLOGIN:

	case PRIV_SYSCTL_WRITEJAIL:

	case PRIV_VARSYM_SYS:

	case PRIV_SETHOSTNAME:

	case PRIV_PROC_TRESPASS:

		return (0);

	case PRIV_UFS_QUOTAON:
	case PRIV_UFS_QUOTAOFF:
	case PRIV_VFS_SETQUOTA:
	case PRIV_UFS_SETUSE:
	case PRIV_VFS_GETQUOTA:
		return (0);


	case PRIV_DEBUG_UNPRIV:
		return (0);


		/*
		 * Allow jailed root to bind reserved ports.
		 */
	case PRIV_NETINET_RESERVEDPORT:
		return (0);


		/*
		 * Conditionally allow creating raw sockets in jail.
		 */
	case PRIV_NETINET_RAW:
		if (jail_allow_raw_sockets)
			return (0);
		else
			return (EPERM);

	case PRIV_HAMMER_IOCTL:
		return (0);

	default:

		return (EPERM);
	}
}
