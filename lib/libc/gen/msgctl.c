/*
 * $DragonFly: src/lib/libc/gen/msgctl.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

int msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
	return (msgsys(0, msqid, cmd, buf));
}
