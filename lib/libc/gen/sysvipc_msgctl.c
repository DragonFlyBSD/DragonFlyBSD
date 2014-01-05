#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "sysvipc_msg.h"

extern char sysvipc_userland;

extern int __sys_msgctl(int, int, struct msqid_ds *);

int msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
	if (sysvipc_userland)
		return (sysvipc_msgctl(msqid, cmd, buf));

	return (__sys_msgctl(msqid, cmd, buf));
}
