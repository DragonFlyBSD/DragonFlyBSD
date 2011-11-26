/*
 * $DragonFly: src/lib/libc/gen/msgrcv.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

int msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
	return (msgsys(3, msqid, msgp, msgsz, msgtyp, msgflg));
}
