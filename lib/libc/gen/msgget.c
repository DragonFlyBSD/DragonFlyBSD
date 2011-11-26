/*
 * $DragonFly: src/lib/libc/gen/msgget.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

int msgget(key_t key, int msgflg)
{
	return (msgsys(1, key, msgflg));
}
