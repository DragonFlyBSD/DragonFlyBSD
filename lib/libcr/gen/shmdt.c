/*
 * $FreeBSD: src/lib/libc/gen/shmdt.c,v 1.4 1999/08/27 23:58:57 peter Exp $
 * $DragonFly: src/lib/libcr/gen/Attic/shmdt.c,v 1.2 2003/06/17 04:26:42 dillon Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if __STDC__
int shmdt(void *shmaddr)
#else
int shmdt(shmaddr)
	void *shmaddr;
#endif
{
	return (shmsys(2, shmaddr));
}
