/*
 * SYS/SYSMSG.H
 *
 * $DragonFly: src/sys/sys/sysmsg.h,v 1.1 2003/07/30 00:19:16 dillon Exp $
 */

#ifndef _SYS_SYSMSG_H_
#define _SYS_SYSMSG_H_

#ifndef _SYS_CALLOUT_H_
#include <sys/callout.h>
#endif

/*
 * The sysmsg structure holds out-of-band information for a system call
 * that userland is not aware of and does not have to supply.  It typically
 * preceeds the syscall arguments in sys/sysunion.h
 */
union sysmsg {
	struct lwkt_msg	sm_msg;
	struct {
	    struct lwkt_msg msg;
	    struct callout  timer;
	} sm_sleep;
};

#define sysmsg_result	sysmsg.sm_msg.u.ms_result
#define sysmsg_lresult	sysmsg.sm_msg.u.ms_lresult
#define sysmsg_resultp	sysmsg.sm_msg.u.ms_resultp
#define sysmsg_fds	sysmsg.sm_msg.u.ms_fds
#define sysmsg_offset	sysmsg.sm_msg.u.ms_offset
#define sysmsg_result32	sysmsg.sm_msg.u.ms_result32
#define sysmsg_result64	sysmsg.sm_msg.u.ms_result64

typedef union sysmsg *sysmsg_t;

#endif

