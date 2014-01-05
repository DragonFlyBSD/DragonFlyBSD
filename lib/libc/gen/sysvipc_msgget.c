#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "sysvipc_msg.h"

extern char sysvipc_userland;

extern int __sys_msgget(key_t, int);

int msgget(key_t key, int msgflg)
{
	if (sysvipc_userland)
		return (sysvipc_msgget(key, msgflg));

	return (__sys_msgget(key, msgflg));
}
