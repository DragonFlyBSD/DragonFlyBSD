/*
 * SYS/USCHED.H
 *
 *	Userland scheduler API
 * 
 * $DragonFly: src/sys/sys/usched.h,v 1.15 2008/04/21 15:24:47 dillon Exp $
 */

#ifndef _SYS_USCHED_H_
#define _SYS_USCHED_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_SYSTIMER_H_
#include <sys/systimer.h>
#endif

#define NAME_LENGTH 32

struct lwp;
struct proc;
struct globaldata;

struct usched {
    TAILQ_ENTRY(usched) entry;
    const char *name;
    const char *desc;
    void (*usched_register)(void);
    void (*usched_unregister)(void);
    void (*acquire_curproc)(struct lwp *);
    void (*release_curproc)(struct lwp *);
    void (*setrunqueue)(struct lwp *);
    void (*schedulerclock)(struct lwp *, sysclock_t, sysclock_t);
    void (*recalculate)(struct lwp *);
    void (*resetpriority)(struct lwp *);
    void (*heuristic_forking)(struct lwp *, struct lwp *);
    void (*heuristic_exiting)(struct lwp *, struct proc *);
    void (*uload_update)(struct lwp *);
    void (*setcpumask)(struct usched *, cpumask_t);
    void (*yield)(struct lwp *);
    void (*changedcpu)(struct lwp *);
};

union usched_data {
    /*
     * BSD4 scheduler. 
     */
    struct {
	short	priority;	/* lower is better */
	char	unused01;	/* (currently not used) */
	char	rqindex;
	int	batch;		/* batch mode heuristic */
	int	estcpu;		/* dynamic priority modification */
	u_short rqtype;		/* protected copy of rtprio type */
	u_short	unused02;
    } bsd4;
    struct {
	short	priority;	/* lower is better */
	char	forked;		/* lock cpu during fork */
	char	rqindex;
	short	estfast;	/* fast estcpu collapse mode */
	short	uload;		/* for delta uload adjustments */
	int	estcpu;		/* dynamic priority modification */
	u_short rqtype;		/* protected copy of rtprio type */
	u_short	qcpu;		/* which cpu are we enqueued on? */
	u_short rrcount;	/* reset when moved to runq tail */
	u_short unused01;
	u_short unused02;
	u_short unused03;
    } dfly;

    int		pad[6];		/* PAD for future expansion */
};

/*
 * Flags for usched_ctl()
 */
#define        USCH_ADD        0x00000001
#define        USCH_REM        0x00000010

#endif	/* _KERNEL || _KERNEL_STRUCTURES */

#define USCHED_SET_SCHEDULER	0
#define USCHED_SET_CPU		1
#define USCHED_ADD_CPU		2
#define USCHED_DEL_CPU		3
#define USCHED_GET_CPU		4

/*
 * Kernel variables and procedures, or user system calls.
 */
#ifdef _KERNEL

extern struct usched	usched_bsd4;
extern struct usched	usched_dfly;
extern struct usched	usched_dummy;
extern cpumask_t usched_mastermask;
extern int sched_ticks; /* From sys/kern/kern_clock.c */

int usched_ctl(struct usched *, int);
struct usched *usched_init(void);
void usched_schedulerclock(struct lwp *, sysclock_t, sysclock_t);

#endif

#if !defined(_KERNEL) || defined(_KERNEL_VIRTUAL)

int usched_set(pid_t, int, void *, int);

#endif

#endif
