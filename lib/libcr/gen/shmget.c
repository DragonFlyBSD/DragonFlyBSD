/*
 * $FreeBSD: src/lib/libc/gen/shmget.c,v 1.4 1999/08/27 23:58:57 peter Exp $
 * $DragonFly: src/lib/libcr/gen/Attic/shmget.c,v 1.2 2003/06/17 04:26:42 dillon Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if __STDC__
int shmget(key_t key, int size, int shmflg)
#else
int shmget(key, size, shmflg)
	key_t key;
	int size;
	int shmflg;
#endif
{
	return (shmsys(3, key, size, shmflg));
}
