/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/kern/kern_jail.c,v 1.6.2.3 2001/08/17 01:00:26 rwatson Exp $
 * $DragonFly: src/sys/kern/kern_jail.c,v 1.7 2005/01/31 22:29:59 joerg Exp $
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/kinfo.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/nlookup.h>
#include <sys/namecache.h>
#include <sys/proc.h>
#include <sys/jail.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/kern_syscall.h>
#include <net/if.h>
#include <netinet/in.h>

static struct prison	*prison_find(int);

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
    "Processes in jail are limited to creating UNIX/IPv4/route sockets only");

int	jail_sysvipc_allowed = 0;
SYSCTL_INT(_jail, OID_AUTO, sysvipc_allowed, CTLFLAG_RW,
    &jail_sysvipc_allowed, 0,
    "Processes in jail can use System V IPC primitives");

int	lastprid = 0;
int	prisoncount = 0;

LIST_HEAD(prisonlist, prison);
struct	prisonlist allprison = LIST_HEAD_INITIALIZER(&allprison);

static int
kern_jail_attach(int jid)
{
	struct proc *p = curthread->td_proc;
	struct prison *pr;
	int error;

	pr = prison_find(jid);
	if (pr == NULL)
		return(EINVAL);

	error = kern_chroot(pr->pr_root);
	if (error)
		return(error);

	prison_hold(pr);
	cratom(&p->p_ucred);
	p->p_ucred->cr_prison = pr;
	p->p_flag |= P_JAILED;

	return(0);
}

/*
 * jail()
 *
 * jail_args(syscallarg(struct jail *) jail)
 */
int
jail(struct jail_args *uap) 
{
	struct prison *pr, *tpr;
	struct jail j;
	struct thread *td = curthread;
	int error, tryprid;
	struct nlookupdata nd;

	error = suser(td);
	if (error)
		return(error);
	error = copyin(uap->jail, &j, sizeof j);
	if (error)
		return(error);
	if (j.version != 0)
		return(EINVAL);
	MALLOC(pr, struct prison *, sizeof *pr , M_PRISON, M_WAITOK | M_ZERO);

	error = copyinstr(j.hostname, &pr->pr_host, sizeof pr->pr_host, 0);
	if (error) 
		goto bail;
	error = nlookup_init(&nd, j.path, UIO_USERSPACE, NLC_FOLLOW);
	if (error)
		goto nlookup_init_clean;
	error = nlookup(&nd);
	if (error)
		goto nlookup_init_clean;
	pr->pr_root = cache_hold(nd.nl_ncp);

	pr->pr_ip = j.ip_number;
	varsymset_init(&pr->pr_varsymset, NULL);

	tryprid = lastprid + 1;
	if (tryprid == JAIL_MAX)
		tryprid = 1;
next:
	LIST_FOREACH(tpr, &allprison, pr_list) {
		if (tpr->pr_id != tryprid)
			continue;
		tryprid++;
		if (tryprid == JAIL_MAX) {
			error = EAGAIN;
			goto varsym_clean;
		}
		goto next;
	}
	pr->pr_id = lastprid = tryprid;
	LIST_INSERT_HEAD(&allprison, pr, pr_list);
	prisoncount++;

	error = kern_jail_attach(pr->pr_id);
	if (error)
		goto jail_attach_clean;

	nlookup_done(&nd);
	return (0);

jail_attach_clean:
	LIST_REMOVE(pr, pr_list);
varsym_clean:
	varsymset_clean(&pr->pr_varsymset);
nlookup_init_clean:
	nlookup_done(&nd);
bail:
	FREE(pr, M_PRISON);
	return(error);
}

/*
 * int jail_attach(int jid);
 */
int
jail_attach(struct jail_attach_args *uap)
{
	struct thread *td = curthread;
	int error;

	error = suser(td);
	if (error)
		return(error);

	return(kern_jail_attach(uap->jid));
}

int
prison_ip(struct thread *td, int flag, u_int32_t *ip)
{
	u_int32_t tmp;
	struct prison *pr;

	if (td->td_proc == NULL)
		return (0);
	if ((pr = td->td_proc->p_ucred->cr_prison) == NULL)
		return (0);
	if (flag) 
		tmp = *ip;
	else
		tmp = ntohl(*ip);
	if (tmp == INADDR_ANY) {
		if (flag) 
			*ip = pr->pr_ip;
		else
			*ip = htonl(pr->pr_ip);
		return (0);
	}
	if (tmp == INADDR_LOOPBACK) {
		if (flag)
			*ip = pr->pr_ip;
		else
			*ip = htonl(pr->pr_ip);
		return (0);
	}
	if (pr->pr_ip != tmp)
		return (1);
	return (0);
}

void
prison_remote_ip(struct thread *td, int flag, u_int32_t *ip)
{
	u_int32_t tmp;
	struct prison *pr;

	if (td == NULL || td->td_proc == NULL)
		return;
	if ((pr = td->td_proc->p_ucred->cr_prison) == NULL)
		return;
	if (flag)
		tmp = *ip;
	else
		tmp = ntohl(*ip);
	if (tmp == INADDR_LOOPBACK) {
		if (flag)
			*ip = pr->pr_ip;
		else
			*ip = htonl(pr->pr_ip);
	}
	return;
}

int
prison_if(struct thread *td, struct sockaddr *sa)
{
	struct prison *pr;
	struct sockaddr_in *sai = (struct sockaddr_in*) sa;
	int ok;

	if (td->td_proc == NULL)
		return(0);
	pr = td->td_proc->p_ucred->cr_prison;

	if ((sai->sin_family != AF_INET) && jail_socket_unixiproute_only)
		ok = 1;
	else if (sai->sin_family != AF_INET)
		ok = 0;
	else if (pr->pr_ip != ntohl(sai->sin_addr.s_addr))
		ok = 1;
	else
		ok = 0;
	return (ok);
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
	struct proc *p;
	struct kinfo_prison *xp, *sxp;
	struct prison *pr;
	int count, error;

	p = curthread->td_proc;

	if (jailed(p->p_ucred))
		return (0);
retry:
	count = prisoncount;

	if (count == 0)
		return(0);

	sxp = xp = malloc(sizeof(*xp) * count, M_TEMP, M_WAITOK | M_ZERO);
	if (count < prisoncount) {
		free(sxp, M_TEMP);
		goto retry;
	}
	count = prisoncount;
	
	LIST_FOREACH(pr, &allprison, pr_list) {
		char *fullpath, *freepath;
		xp->pr_version = KINFO_PRISON_VERSION;
		xp->pr_id = pr->pr_id;
		error = cache_fullpath(p, pr->pr_root, &fullpath, &freepath);
		if (error == 0) {
			strlcpy(xp->pr_path, fullpath, sizeof(xp->pr_path));
			free(freepath, M_TEMP);
		} else {
			bzero(xp->pr_path, sizeof(xp->pr_path));
		}
		strlcpy(xp->pr_host, pr->pr_host, sizeof(xp->pr_host));
		xp->pr_ip = pr->pr_ip;
		xp++;
	}

	error = SYSCTL_OUT(req, sxp, sizeof(*sxp) * count);
	free(sxp, M_TEMP);
	return(error);
}

SYSCTL_OID(_jail, OID_AUTO, list, CTLTYPE_STRUCT | CTLFLAG_RD, NULL, 0,
	   sysctl_jail_list, "S", "List of active jails");

void
prison_hold(struct prison *pr)
{
	pr->pr_ref++;
}

void
prison_free(struct prison *pr)
{
	KKASSERT(pr->pr_ref >= 1);

	if (--pr->pr_ref > 0)
		return;

	LIST_REMOVE(pr, pr_list);
	prisoncount--;

	if (pr->pr_linux != NULL)
		free(pr->pr_linux, M_PRISON);
	varsymset_clean(&pr->pr_varsymset);
	cache_drop(pr->pr_root);
	free(pr, M_PRISON);
}
