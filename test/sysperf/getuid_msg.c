/*
 * getuid_msg.c
 *
 * $DragonFly: src/test/sysperf/getuid_msg.c,v 1.2 2004/07/06 15:32:05 eirikn Exp $
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/msgport.h>
#include <sys/syscall.h>
#include <sys/sysproto.h>
#include <sys/sysunion.h>

#include "../sysmsg/sendsys.h"

int
getuid_msg(void)
{
    static struct getuid_args uidmsg;
    int error;

    /*
     * In real life use a properly pre-initialized message, e.g. stowed in 
     * the thread structure or cached in a linked list somewhere.
     * bzero(&sysmsg.lmsg, sizeof(sysmsg.lmsg))
     */
    uidmsg.usrmsg.umsg.ms_cmd.cm_op = SYS_getuid;	/* XXX lwkt_init_msg() */
    uidmsg.usrmsg.umsg.ms_flags = MSGF_DONE;

    error = sendsys(NULL, &uidmsg, sizeof(uidmsg));
    if (error) {
	printf("error %d\n", error);
	exit(1);
    }
    return(uidmsg.usrmsg.umsg.u.ms_result32);
}

