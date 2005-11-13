/*
 * $DragonFly: src/lib/libc/gen/semop.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int semop(int semid, struct sembuf *sops, unsigned nsops)
{
	return (semsys(2, semid, sops, nsops, 0));
}
