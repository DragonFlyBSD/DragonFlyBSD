#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "sysvipc_msg.h"

extern char sysvipc_userland;

extern int __sys_msgrcv(int, void *, size_t, long, int);

int msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
	if (sysvipc_userland)
		return (sysvipc_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg));

	return (__sys_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg));
}
