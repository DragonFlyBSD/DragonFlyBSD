#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "sysvipc_shm.h"

extern char sysvipc_userland;
extern int __sys_shmdt(const void *);

int shmdt(const void *addr)
{
	if (sysvipc_userland)
		return (sysvipc_shmdt(addr));

	return (__sys_shmdt(addr));
}
