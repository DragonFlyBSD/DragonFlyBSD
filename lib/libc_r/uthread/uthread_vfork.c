/*
 * $FreeBSD: src/lib/libc_r/uthread/uthread_vfork.c,v 1.1.8.1 2002/10/22 14:44:03 fjoe Exp $
 * $DragonFly: src/lib/libc_r/uthread/uthread_vfork.c,v 1.2 2003/06/17 04:26:48 dillon Exp $
 */
#include <unistd.h>

int
vfork(void)
{
	return (fork());
}
