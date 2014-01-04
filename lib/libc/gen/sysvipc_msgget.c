/*
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "sysvipc_msg.h"

extern char use_userland_impl;

extern int __sys_msgget(key_t, int);

int msgget(key_t key, int msgflg)
{
	if (use_userland_impl)
		return (sysvipc_msgget(key, msgflg));

	return (__sys_msgget(key, msgflg));
}
