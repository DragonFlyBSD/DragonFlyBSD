/*
 * $DragonFly: src/lib/libc/gen/semconfig.c,v 1.2 2005/11/13 00:07:42 swildner Exp $
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int semconfig(int cmd, int p1, int p2, int p3)
{
	return (semsys(3, cmd, p1, p2, p3));
}
