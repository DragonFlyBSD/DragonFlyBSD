#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdarg.h>

#include "sysvipc_sem.h"

extern char sysvipc_userland;
extern int __sys___semctl(int, int, int, union semun *);

int
semctl(int semid, int semnum, int cmd, ...)
{
	va_list ap;
	union semun semun = { 0 };
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
	return (__sys___semctl(semid, semnum, cmd, semun_ptr));
}
