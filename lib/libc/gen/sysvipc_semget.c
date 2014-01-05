#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "sysvipc_sem.h"

extern char sysvipc_userland;

extern int __sys_semget(key_t, int, int);

int semget(key_t key, int nsems, int semflg)
{
	if (sysvipc_userland)
		return (sysvipc_semget(key, nsems, semflg));

	return (__sys_semget(key, nsems, semflg));
}
