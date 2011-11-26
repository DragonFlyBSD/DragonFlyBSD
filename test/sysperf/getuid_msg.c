/*
 * getuid_msg.c
 *
 * $DragonFly: src/test/sysperf/getuid_msg.c,v 1.3 2004/10/31 20:19:23 eirikn Exp $
 */

#include "../sysmsg/syscall.h"

int
getuid_msg(void)
{
	struct getuid_args getuidmsg;
	int error;

	INITMSGSYSCALL(getuid, 0);
	DOMSGSYSCALL(getuid);
	FINISHMSGSYSCALL(getuid, error);
}

