/* $DragonFly: src/lib/libc/gen/semctl.c,v 1.3 2005/04/26 06:08:42 joerg Exp $
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2013/09/24 21:37:00 Lrisa Grigore <larisagrigore@gmail.com> Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdarg.h>
#include <stdlib.h>

#include "sysvipc_sem.h"

extern char use_userland_impl;

int
semctl(int semid, int semnum, int cmd, ...)
{
	va_list ap;
	union semun semun = {0};
	union semun *semun_ptr = NULL;
	va_start(ap, cmd);
	if (cmd == IPC_SET || cmd == IPC_STAT || cmd == GETALL
	    || cmd == SETVAL || cmd == SETALL) {
		semun = va_arg(ap, union semun);
		semun_ptr = &semun;
	}
	va_end(ap);

	if (use_userland_impl)
		return (sysvipc_semctl(semid, semnum, cmd, semun));
	return (semsys(0, semid, semnum, cmd, semun_ptr));
}
