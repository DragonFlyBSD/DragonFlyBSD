/*
 * $FreeBSD: src/lib/libc/gen/shmat.c,v 1.4 1999/08/27 23:58:56 peter Exp $
 * $DragonFly: src/lib/libc/gen/shmat.c,v 1.3 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

void *shmat(int shmid, void *shmaddr, int shmflg)
{
	return ((void *)shmsys(0, shmid, shmaddr, shmflg));
}
