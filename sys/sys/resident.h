/*
 * SYS/RESIDENT.H
 *
 *	Userland system calls for resident executable support.
 *
 * $DragonFly: src/sys/sys/resident.h,v 1.4 2006/05/20 02:42:13 dillon Exp $
 */

#ifndef _SYS_RESIDENT_H_
#define _SYS_RESIDENT_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_STAT_H_
#include <sys/stat.h>
#endif

#if !defined(_KERNEL)

int exec_sys_register(void *);
int exec_sys_unregister(int);

#endif

struct stat;
/* structure exported via sysctl 'vm.resident' for userland */
struct xresident {
	intptr_t	res_entry_addr;
	int     	res_id;
	char		res_file[MAXPATHLEN];
	struct stat	res_stat;
};

#endif
