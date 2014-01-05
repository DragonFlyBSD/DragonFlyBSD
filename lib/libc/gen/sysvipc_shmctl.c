#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "sysvipc_shm.h"

extern char sysvipc_userland;
extern int __sys_shmctl(int, int, struct shmid_ds *);

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	if (sysvipc_userland)
		return (sysvipc_shmctl(shmid, cmd, buf));
	return (__sys_shmctl(shmid, cmd, buf));
}
