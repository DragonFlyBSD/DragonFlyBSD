/*
 * $DragonFly: src/lib/libc/gen/msgsnd.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

int msgsnd(int msqid, void *msgp, size_t msgsz, int msgflg)
{
	return (msgsys(2, msqid, msgp, msgsz, msgflg));
}
