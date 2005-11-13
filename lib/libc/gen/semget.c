/*
 * $DragonFly: src/lib/libc/gen/semget.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int semget(key_t key, int nsems, int semflg)
{
	return (semsys(1, key, nsems, semflg));
}
