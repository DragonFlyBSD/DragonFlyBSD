/*
 * $FreeBSD: src/lib/libc/gen/shmat.c,v 1.4 1999/08/27 23:58:56 peter Exp $
 * $DragonFly: src/lib/libc/gen/shmat.c,v 1.2 2003/06/17 04:26:42 dillon Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if __STDC__
void *shmat(int shmid, void *shmaddr, int shmflg)
#else
void *shmat(shmid, shmaddr, shmflg)
	int shmid;
	void *shmaddr;
	int shmflg;
#endif
{
	return ((void *)shmsys(0, shmid, shmaddr, shmflg));
}
