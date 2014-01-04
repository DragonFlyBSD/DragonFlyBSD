/*
 * $DragonFly: src/lib/libc/gen/semget.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "sysvipc_shm.h"

extern char use_userland_impl;
extern void* __sys_shmat(int, const void *, int);

void *shmat(int shmid, const void *addr, int flag)
{
	if (use_userland_impl)
		return (sysvipc_shmat(shmid, addr, flag));
	return (__sys_shmat(shmid, addr, flag));
}
