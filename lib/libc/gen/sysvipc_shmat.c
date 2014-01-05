#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "sysvipc_shm.h"

extern char sysvipc_userland;
extern void* __sys_shmat(int, const void *, int);

void *shmat(int shmid, const void *addr, int flag)
{
	if (sysvipc_userland)
		return (sysvipc_shmat(shmid, addr, flag));
	return (__sys_shmat(shmid, addr, flag));
}
