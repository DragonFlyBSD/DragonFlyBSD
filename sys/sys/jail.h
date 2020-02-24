/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/sys/jail.h,v 1.8.2.2 2000/11/01 17:58:06 rwatson Exp $
 */

#ifndef _SYS_JAIL_H_
#define _SYS_JAIL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_UCRED_H_
#include <sys/ucred.h>
#endif
#ifndef _NET_IF_H_
#include <net/if.h>
#endif

struct jail {
	uint32_t	version;
	char		*path;
	char		*hostname;
	uint32_t	n_ips;     /* Number of ips */
	struct sockaddr_storage *ips;
};

struct jail_v0 {
	uint32_t	version;
	char		*path;
	char		*hostname;
	uint32_t	ip_number;
};

#ifndef _KERNEL

int jail(struct jail *);
int jail_attach(int);

#endif

#ifdef _KERNEL

#ifndef _SYS_NAMECACHE_H_
#include <sys/namecache.h>
#endif
#ifndef _SYS_VARSYM_H_
#include <sys/varsym.h>
#endif

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_PRISON);
#endif

#endif	/* _KERNEL */

/* Jail capabilities */
#define PRISON_CAP_ROOT			0    /* Catch-all during development */

/* System configuration capabilities */
#define PRISON_CAP_SYS_SET_HOSTNAME	1    /* Can set hostname */
#define PRISON_CAP_SYS_SYSVIPC		2    /* Can do SysV IPC calls */

/* Net specific capabiliites */
#define PRISON_CAP_NET_UNIXIPROUTE	20   /* Restrict to UNIX/IPv[46]/route
                                                sockets only */
#define PRISON_CAP_NET_RAW_SOCKETS	21   /* Can use raw sockets */
#define PRISON_CAP_NET_LISTEN_OVERRIDE	22   /* Can override wildcard on host */

/* VFS specific capabilities */
#define PRISON_CAP_VFS_CHFLAGS		40   /* Can manipulate system file
                                                flags */
#define PRISON_CAP_VFS_MOUNT_NULLFS	45   /* Can mount nullfs(5) */
#define PRISON_CAP_VFS_MOUNT_DEVFS	46   /* Can mount devfs(5) */
#define PRISON_CAP_VFS_MOUNT_TMPFS	47   /* Can mount tmpfs(5) */

typedef __uint64_t prison_cap_t;

#define PRISON_CAP_ISSET(mask, bit)	(mask & (1LU << bit))

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#define	JAIL_MAX	999999

/* Used to store the IPs of the jail */

struct jail_ip_storage {
	struct sockaddr_storage ip;
	SLIST_ENTRY(jail_ip_storage) entries;
};

/*
 * This structure describes a prison.  It is pointed to by all struct
 * ucred's of the inmates.  pr_ref keeps track of them and is used to
 * delete the struture when the last inmate is dead.
 */
struct sysctl_ctx_list;
struct sysctl_oid;

struct prison {
	LIST_ENTRY(prison) pr_list;			/* all prisons */
	int		pr_id;				/* prison id */
	int		pr_ref;				/* reference count */
	struct nchandle pr_root;			/* namecache entry of root */
	char 		pr_host[MAXHOSTNAMELEN];	/* host name */
	SLIST_HEAD(iplist, jail_ip_storage) pr_ips;	/* list of IP addresses */
	struct sockaddr_in	*local_ip4;		/* cache for a loopback ipv4 address */
	struct sockaddr_in	*nonlocal_ip4;		/* cache for a non loopback ipv4 address */
	struct sockaddr_in6	*local_ip6;		/* cache for a loopback ipv6 address */
	struct sockaddr_in6	*nonlocal_ip6;		/* cache for a non loopback ipv6 address */
	void		*pr_linux;			/* Linux ABI emulation */
	int		 pr_securelevel;		/* jail securelevel */
	struct varsymset pr_varsymset;			/* jail varsyms */

	struct sysctl_ctx_list *pr_sysctl_ctx;
	struct sysctl_oid *pr_sysctl_tree;

	prison_cap_t	pr_caps;			/* Prison capabilities */
};

/*
 * Kernel support functions for jail.
 */
int	jailed_ip(struct prison *, struct sockaddr *);
void	prison_free(struct prison *);
void	prison_hold(struct prison *);
int	prison_if(struct ucred *cred, struct sockaddr *sa);
struct sockaddr *
	prison_get_local(struct prison *pr, sa_family_t, struct sockaddr *);
struct sockaddr *
	prison_get_nonlocal(struct prison *pr, sa_family_t, struct sockaddr *);
int	prison_priv_check(struct ucred *cred, int priv);
int	prison_remote_ip(struct thread *td, struct sockaddr *ip);
int	prison_local_ip(struct thread *td, struct sockaddr *ip);
int	prison_replace_wildcards(struct thread *td, struct sockaddr *ip);
int	prison_sysctl_create(struct prison *);
int	prison_sysctl_done(struct prison *);

/*
 * Return 1 if the passed credential is in a jail, otherwise 0.
 *
 * MPSAFE
 */
static __inline int
jailed(struct ucred *cred)
{
	return(cred->cr_prison != NULL);
}

#endif /* _KERNEL || _KERNEL_STRUCTURES */
#endif /* !_SYS_JAIL_H_ */
