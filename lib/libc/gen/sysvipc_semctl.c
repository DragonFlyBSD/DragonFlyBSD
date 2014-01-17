#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdarg.h>
#include <stdlib.h>

#include "sysvipc_sem.h"

extern char sysvipc_userland;

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

	if (sysvipc_userland)
		return (sysvipc___semctl(semid, semnum, cmd, semun_ptr));
	return (semsys(0, semid, semnum, cmd, semun_ptr));
}
