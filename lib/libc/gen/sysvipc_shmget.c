/*
 * $DragonFly: src/lib/libc/gen/semget.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "sysvipc_shm.h"

extern char use_userland_impl;

extern int __sys_shmget(key_t, size_t, int);

int shmget(key_t key, size_t size, int shmflg)
{
	if (use_userland_impl)
		return (sysvipc_shmget(key, size, shmflg));
	return (__sys_shmget(key, size, shmflg));
}
