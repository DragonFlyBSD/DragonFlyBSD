/*
 * $FreeBSD: src/lib/libc/gen/shmctl.c,v 1.4 1999/08/27 23:58:57 peter Exp $
 * $DragonFly: src/lib/libc/gen/shmctl.c,v 1.3 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	return (shmsys(4, shmid, cmd, buf));
}
