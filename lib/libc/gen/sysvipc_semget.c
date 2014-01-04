/*
 * $DragonFly: src/lib/libc/gen/semget.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "sysvipc_sem.h"

extern char use_userland_impl;

extern int __sys_semget(key_t, int, int);

int semget(key_t key, int nsems, int semflg)
{
	if (use_userland_impl)
		return (sysvipc_semget(key, nsems, semflg));

	return (__sys_semget(key, nsems, semflg));
}
