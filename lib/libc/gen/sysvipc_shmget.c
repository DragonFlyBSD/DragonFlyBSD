#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "sysvipc_shm.h"

extern char sysvipc_userland;

extern int __sys_shmget(key_t, size_t, int);

int shmget(key_t key, size_t size, int shmflg)
{
	if (sysvipc_userland)
		return (sysvipc_shmget(key, size, shmflg));
	return (__sys_shmget(key, size, shmflg));
}
