/*
 * SYS/RESIDENT.H
 *
 *	Userland system calls for resident executable support.
 *
 * $DragonFly: src/sys/sys/resident.h,v 1.3 2004/06/03 16:28:15 hmp Exp $
 */

#ifndef _SYS_RESIDENT_H_
#define _SYS_RESIDENT_H_

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

