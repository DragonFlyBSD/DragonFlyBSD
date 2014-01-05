#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "sysvipc_sem.h"

extern char sysvipc_userland;
extern int __sys_semop(int, struct sembuf *, unsigned);

int semop(int semid, struct sembuf *sops, unsigned nsops)
{
	if (sysvipc_userland) {
		return (sysvipc_semop(semid, sops, nsops));
	}
	return (__sys_semop(semid, sops, nsops));
}
