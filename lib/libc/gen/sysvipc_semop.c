/*
 * $DragonFly: src/lib/libc/gen/semop.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "sysvipc_sem.h"

extern char use_userland_impl;
extern int __sys_semop(int, struct sembuf *, unsigned);

int semop(int semid, struct sembuf *sops, unsigned nsops)
{
	if (use_userland_impl) {
		return (sysvipc_semop(semid, sops, nsops));
	}
	return (__sys_semop(semid, sops, nsops));
}
