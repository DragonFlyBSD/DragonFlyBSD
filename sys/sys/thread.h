/*
 * SYS/THREAD.H
 *
 *	Implements the architecture independant portion of the LWKT 
 *	subsystem.
 * 
 * $DragonFly: src/sys/sys/thread.h,v 1.3 2003/06/20 02:09:59 dillon Exp $
 */

#ifndef _SYS_THREAD_H_
#define _SYS_THREAD_H_

struct proc;
struct thread;
struct globaldata;

typedef TAILQ_HEAD(, thread) thread_list_t;

struct thread {
    TAILQ_ENTRY(thread) td_threadq;
    struct proc	*td_proc;	/* (optional) associated process */
    struct pcb	*td_pcb;	/* points to pcb and top of kstack */
    int		td_pri;		/* 0-31, 0=highest priority */
    int		td_flags;	/* THF flags */
    char	*td_kstack;	/* kernel stack */
    char	*td_sp;		/* kernel stack pointer for LWKT restore */
    void	(*td_switch)(struct thread *ntd);
    thread_list_t *td_waitq;
#if 0
    int		td_bglcount;	/* big giant lock count */
#endif
};

typedef struct thread *thread_t;

/*
 * Thread states.  Note that the RUNNING state is independant from the
 * RUNQ/WAITQ state.  That is, a thread's queueing state can be manipulated
 * while it is running.  If a thread is preempted it will always be moved
 * back to the RUNQ if it isn't on it.
 */

#define TDF_RUNNING		0x0001	/* currently running */
#define TDF_RUNQ		0x0002	/* on run queue */
#define TDF_WAITQ		0x0004	/* on wait queue */

/*
 * Thread priorities.  Typically only one thread from any given
 * user process scheduling queue is on the LWKT run queue at a time.
 * Remember that there is one LWKT run queue per cpu.
 */

#define THPRI_INT_HIGH		2	/* high priority interrupt */
#define THPRI_INT_MED		4	/* medium priority interrupt */
#define THPRI_INT_LOW		6	/* low priority interrupt */
#define THPRI_INT_SUPPORT	10	/* kernel / high priority support */
#define THPRI_SOFT_TIMER	12	/* kernel / timer */
#define THPRI_SOFT_NORM		15	/* kernel / normal */
#define THPRI_KERN_USER		20	/* kernel / block in syscall */
#define THPRI_USER_REAL		25	/* user scheduler real time */
#define THPRI_USER_NORM		27	/* user scheduler normal */
#define THPRI_USER_IDLE		29	/* user scheduler idle */
#define THPRI_IDLE_THREAD	31	/* the idle thread */

#define CACHE_NTHREADS		4

#ifdef _KERNEL

extern struct vm_zone	*thread_zone;

extern void lwkt_gdinit(struct globaldata *gd);
extern void lwkt_switch(void);
extern void lwkt_preempt(void);
extern void lwkt_schedule(thread_t td);
extern void lwkt_deschedule(thread_t td);

#endif

#endif

