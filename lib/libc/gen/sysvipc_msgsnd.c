#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "sysvipc_msg.h"

extern char sysvipc_userland;

extern int __sys_msgsnd(int, void *, size_t, int);

int msgsnd(int msqid, void *msgp, size_t msgsz, int msgflg)
{
	if (sysvipc_userland)
		return (sysvipc_msgsnd(msqid, msgp, msgsz, msgflg));

	return (__sys_msgsnd(msqid, msgp, msgsz, msgflg));
}
