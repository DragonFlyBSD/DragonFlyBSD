/*
 * $FreeBSD: src/lib/libc/gen/shmctl.c,v 1.4 1999/08/27 23:58:57 peter Exp $
 * $DragonFly: src/lib/libcr/gen/Attic/shmctl.c,v 1.2 2003/06/17 04:26:42 dillon Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if __STDC__
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
#else
int shmctl(shmid, cmd, buf)
	int shmid;
	int cmd;
	struct shmid_ds *buf;
#endif
{
	return (shmsys(4, shmid, cmd, buf));
}
