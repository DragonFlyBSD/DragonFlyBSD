/*
 * SYS/SYSMSG.H
 *
 * $DragonFly: src/sys/sys/sysmsg.h,v 1.6 2004/08/12 19:59:30 eirikn Exp $
 */

#ifndef _SYS_SYSMSG_H_
#define _SYS_SYSMSG_H_

#ifdef _KERNEL

#ifndef _SYS_CALLOUT_H_
#include <sys/callout.h>	/* for struct callout */
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>		/* for struct timespec */
#endif

/*
 * The sysmsg holds the kernelland version of a system call.
 * It typically preceeds the usrmsg and syscall arguments in sysunion
 * (see sys/sysunion.h).  Note that msgq field is used by the governing
 * process to record a system call that returns EASYNC in order to be able
 * to properly abort and/or wait on it in exit1().  This is different from
 * any localized queueing of the LWKT message that the syscall itself might
 * do.
 */
union sysunion;

struct sysmsg {
	struct lwkt_msg	lmsg;
	void 		(*copyout)(union sysunion *sysun);
	TAILQ_ENTRY(sysmsg)	msgq;
	union {
	    struct sysmsg_sleep {
		struct lwkt_msg lmsg;
		struct timespec rmt;
		struct timespec rqt;
		struct callout  timer;
	    } sleep;
	} sm;
};

struct proc;
union sysunion;

struct sysmsg *sysmsg_wait(struct proc *p, struct sysmsg *sysmsg, int nonblock);
void sysmsg_rundown(struct proc *p, int doabort);

#endif

/*
 * The usrmsg holds the userland version of the system call message which
 * typically preceeds the original user arguments.  This message structure
 * is typically loaded by the copyin() and adjusted prior to copyout(), but
 * not used in the nominal running of the system call.
 */
union usrmsg {
	struct lwkt_msg umsg;
};

#ifdef _KERNEL
typedef struct sysmsg *sysmsg_t;
#define sysmsg_copyout	sysmsg.copyout
#define sysmsg_lmsg	sysmsg.lmsg
#define sysmsg_result	sysmsg.lmsg.u.ms_result
#define sysmsg_lresult	sysmsg.lmsg.u.ms_lresult
#define sysmsg_resultp	sysmsg.lmsg.u.ms_resultp
#define sysmsg_fds	sysmsg.lmsg.u.ms_fds
#define sysmsg_offset	sysmsg.lmsg.u.ms_offset
#define sysmsg_result32	sysmsg.lmsg.u.ms_result32
#define sysmsg_result64	sysmsg.lmsg.u.ms_result64
#endif

typedef union usrmsg *usrmsg_t;
#define usrmsg_result	usrmsg.umsg.u.ms_result
#define usrmsg_lresult	usrmsg.umsg.u.ms_lresult
#define usrmsg_resultp	usrmsg.umsg.u.ms_resultp
#define usrmsg_fds	usrmsg.umsg.u.ms_fds
#define usrmsg_offset	usrmsg.umsg.u.ms_offset
#define usrmsg_result32	usrmsg.umsg.u.ms_result32
#define usrmsg_result64	usrmsg.umsg.u.ms_result64

#endif

