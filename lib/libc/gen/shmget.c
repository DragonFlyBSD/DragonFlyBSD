/*
 * $FreeBSD: src/lib/libc/gen/shmget.c,v 1.4 1999/08/27 23:58:57 peter Exp $
 * $DragonFly: src/lib/libc/gen/shmget.c,v 1.3 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int shmget(key_t key, int size, int shmflg)
{
	return (shmsys(3, key, size, shmflg));
}
