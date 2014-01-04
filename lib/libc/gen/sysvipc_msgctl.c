/*
 * $DragonFly: src/lib/libc/gen/msgctl.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "sysvipc_msg.h"

extern char use_userland_impl;

extern int __sys_msgctl(int, int, struct msqid_ds *);

int msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
	if (use_userland_impl)
		return (sysvipc_msgctl(msqid, cmd, buf));

	return (__sys_msgctl(msqid, cmd, buf));
}
