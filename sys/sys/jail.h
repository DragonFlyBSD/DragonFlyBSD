/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/sys/jail.h,v 1.8.2.2 2000/11/01 17:58:06 rwatson Exp $
 * $DragonFly: src/sys/sys/jail.h,v 1.5 2005/01/31 22:29:59 joerg Exp $
 *
 */

#ifndef _SYS_JAIL_H_
#define _SYS_JAIL_H_

struct jail {
	uint32_t	version;
	char		*path;
	char		*hostname;
	uint32_t	ip_number;
};

#ifndef _KERNEL

int jail(struct jail *);
int jail_attach(int);

#else /* _KERNEL */

#include <sys/varsym.h>

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_PRISON);
#endif

#define	JAIL_MAX	999999

/*
 * This structure describes a prison.  It is pointed to by all struct
 * proc's of the inmates.  pr_ref keeps track of them and is used to
 * delete the struture when the last inmate is dead.
 */

struct prison {
	LIST_ENTRY(prison) pr_list;			/* all prisons */
	int		pr_id;				/* prison id */
	int		pr_ref;				/* reference count */
	struct namecache *pr_root;			/* namecache entry of root */
	char 		pr_host[MAXHOSTNAMELEN];	/* host name */
	uint32_t	pr_ip;				/* IP address */
	void		*pr_linux;			/* Linux ABI emulation */
	int		 pr_securelevel;		/* jail securelevel */
	struct varsymset pr_varsymset;			/* jail varsyms */
};

/*
 * Sysctl-set variables that determine global jail policy
 */
extern int	jail_set_hostname_allowed;
extern int	jail_socket_unixiproute_only;
extern int	jail_sysvipc_allowed;

void	prison_hold(struct prison *);
void	prison_free(struct prison *);

/*
 * Return 1 if the passed credential is in a jail, otherwise 0.
 */
static __inline int
jailed(struct ucred *cred)
{
	return(cred->cr_prison != NULL);
}

#endif /* !_KERNEL */
#endif /* !_SYS_JAIL_H_ */
