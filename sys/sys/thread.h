/*
 * SYS/THREAD.H
 *
 *	Implements the architecture independant portion of the LWKT 
 *	subsystem.
 * 
 * $DragonFly: src/sys/sys/thread.h,v 1.16 2003/06/30 19:50:32 dillon Exp $
 */

#ifndef _SYS_THREAD_H_
#define _SYS_THREAD_H_

struct globaldata;
struct proc;
struct thread;
struct lwkt_queue;
struct lwkt_token;
struct lwkt_wait;
struct lwkt_msg;
struct lwkt_port;
struct lwkt_cpu_msg;
struct lwkt_cpu_port;
struct lwkt_rwlock;

typedef struct lwkt_queue	*lwkt_queue_t;
typedef struct lwkt_token	*lwkt_token_t;
typedef struct lwkt_wait	*lwkt_wait_t;
typedef struct lwkt_msg		*lwkt_msg_t;
typedef struct lwkt_port	*lwkt_port_t;
typedef struct lwkt_cpu_msg	*lwkt_cpu_msg_t;
typedef struct lwkt_cpu_port	*lwkt_cpu_port_t;
typedef struct lwkt_rwlock	*lwkt_rwlock_t;
typedef struct thread 		*thread_t;

typedef TAILQ_HEAD(lwkt_queue, thread) lwkt_queue;
typedef TAILQ_HEAD(lwkt_msg_queue, lwkt_msg) lwkt_msg_queue;

#ifndef _MACHINE_THREAD_H_
#include <machine/thread.h>		/* md_thread */
#endif

/*
 * Tokens arbitrate access to information.  They are 'soft' arbitrators
 * in that they are associated with cpus rather then threads, making the
 * optimal aquisition case very fast if your cpu already happens to own the
 * token you are requesting.
 */
typedef struct lwkt_token {
    int		t_cpu;		/* the current owner of the token */
    int		t_reqcpu;	/* return ownership to this cpu on release */
#if 0
    int		t_pri;		/* raise thread priority to hold token */
#endif
} lwkt_token;

/*
 * Wait structures deal with blocked threads.  Due to the way remote cpus
 * interact with these structures stable storage must be used.
 */
typedef struct lwkt_wait {
    lwkt_queue	wa_waitq;	/* list of waiting threads */
    lwkt_token	wa_token;	/* who currently owns the list */
    int		wa_gen;
    int		wa_count;
} lwkt_wait;

/*
 * The standarding message and port structure for communications between
 * threads.
 */
typedef struct lwkt_msg {
    TAILQ_ENTRY(lwkt_msg) ms_node;
    lwkt_port_t	ms_replyport;
    int		ms_cmd;
    int		ms_flags;
    int		ms_error;
} lwkt_msg;

#define MSGF_DONE	0x0001
#define MSGF_REPLY	0x0002
#define MSGF_QUEUED	0x0004

typedef struct lwkt_port {
    lwkt_msg_queue	mp_msgq;
    lwkt_wait		mp_wait;
} lwkt_port;

#define mp_token	mp_wait.wa_token

/*
 * The standard message and queue structure used for communications between
 * cpus.  Messages are typically queued via a machine-specific non-linked
 * FIFO matrix allowing any cpu to send a message to any other cpu without
 * blocking.
 */
typedef struct lwkt_cpu_msg {
    void	(*cm_func)(lwkt_cpu_msg_t msg);	/* primary dispatch function */
    int		cm_code;		/* request code if applicable */
    int		cm_cpu;			/* reply to cpu */
    thread_t	cm_originator;		/* originating thread for wakeup */
} lwkt_cpu_msg;

/*
 * reader/writer lock
 */
typedef struct lwkt_rwlock {
    lwkt_wait	rw_wait;
    thread_t	rw_owner;
    int		rw_count;
    int		rw_requests;
} lwkt_rwlock;

#define rw_token	rw_wait.wa_token

/*
 * Thread structure.  Note that ownership of a thread structure is special
 * cased and there is no 'token'.  A thread is always owned by td_cpu and
 * any manipulation of the thread by some other cpu must be done through
 * cpu_*msg() functions.  e.g. you could request ownership of a thread that
 * way, or hand a thread off to another cpu by changing td_cpu and sending
 * a schedule request to the other cpu.
 *
 * NOTE: td_pri is bumped by TDPRI_CRIT when entering a critical section,
 * but this does not effect how the thread is scheduled by LWKT.
 */
struct thread {
    TAILQ_ENTRY(thread) td_threadq;
    struct proc	*td_proc;	/* (optional) associated process */
    struct pcb	*td_pcb;	/* points to pcb and top of kstack */
    struct globaldata *td_gd;	/* associated with this cpu */
    const char	*td_wmesg;	/* string name for blockage */
    void	*td_wchan;	/* waiting on channel */
    int		td_cpu;		/* cpu owning the thread */
    int		td_pri;		/* 0-31, 31=highest priority (note 1) */
    int		td_flags;	/* THF flags */
    int		td_gen;		/* wait queue chasing generation number */
    char	*td_kstack;	/* kernel stack */
    char	*td_sp;		/* kernel stack pointer for LWKT restore */
    void	(*td_switch)(struct thread *ntd);
    lwkt_wait_t td_wait;	/* thread sitting on wait structure */
    u_int64_t	td_uticks;	/* Statclock hits in user mode (uS) */
    u_int64_t	td_sticks;      /* Statclock hits in system mode (uS) */
    u_int64_t	td_iticks;	/* Statclock hits processing intr (uS) */
    int		td_locks;	/* lockmgr lock debugging YYY */
    char	td_comm[MAXCOMLEN+1]; /* typ 16+1 bytes */
    struct thread *td_preempted; /* we preempted this thread */
    struct md_thread td_mach;
};

/*
 * Thread flags.  Note that TDF_EXITED is set by the appropriate switchout
 * code when a thread exits, after it has switched to another stack and
 * cleaned up the MMU state.
 */
#define TDF_EXITED		0x0001	/* thread finished exiting */
#define TDF_RUNQ		0x0002	/* on run queue */
#define TDF_PREEMPT_LOCK	0x0004	/* I have been preempted */
#define TDF_PREEMPT_DONE	0x0008	/* acknowledge preemption complete */

#define TDF_ALLOCATED_THREAD	0x0200	/* zalloc allocated thread */
#define TDF_ALLOCATED_STACK	0x0400	/* zalloc allocated stack */
#define TDF_VERBOSE		0x0800	/* verbose on exit */
#define TDF_DEADLKTREAT		0x1000	/* special lockmgr deadlock treatment */
#define TDF_STOPREQ		0x2000	/* suspend_kproc */
#define TDF_WAKEREQ		0x4000	/* resume_kproc */
#define TDF_TIMEOUT		0x8000	/* tsleep timeout */

/*
 * Thread priorities.  Typically only one thread from any given
 * user process scheduling queue is on the LWKT run queue at a time.
 * Remember that there is one LWKT run queue per cpu.
 *
 * Critical sections are handled by bumping td_pri above TDPRI_MAX, which
 * causes interrupts to be masked as they occur.  When this occurs
 * mycpu->gd_reqpri will be raised (possibly just set to TDPRI_CRIT for
 * interrupt masking).
 */
#define TDPRI_IDLE_THREAD	0	/* the idle thread */
#define TDPRI_USER_IDLE		4	/* user scheduler idle */
#define TDPRI_USER_NORM		6	/* user scheduler normal */
#define TDPRI_USER_REAL		8	/* user scheduler real time */
#define TDPRI_KERN_USER		10	/* kernel / block in syscall */
#define TDPRI_KERN_DAEMON	12	/* kernel daemon (pageout, etc) */
#define TDPRI_SOFT_NORM		14	/* kernel / normal */
#define TDPRI_SOFT_TIMER	16	/* kernel / timer */
#define TDPRI_EXITING		19	/* exiting thread */
#define TDPRI_INT_SUPPORT	20	/* kernel / high priority support */
#define TDPRI_INT_LOW		27	/* low priority interrupt */
#define TDPRI_INT_MED		28	/* medium priority interrupt */
#define TDPRI_INT_HIGH		29	/* high priority interrupt */
#define TDPRI_MAX		31

#define TDPRI_MASK		31
#define TDPRI_CRIT		32	/* high bits of td_pri used for crit */

#define CACHE_NTHREADS		6

#ifdef _KERNEL

extern struct vm_zone	*thread_zone;

extern struct thread *lwkt_alloc_thread(struct thread *template);
extern void lwkt_init_thread(struct thread *td, void *stack, int flags,
	struct globaldata *gd);
extern void lwkt_free_thread(struct thread *td);
extern void lwkt_init_wait(struct lwkt_wait *w);
extern void lwkt_gdinit(struct globaldata *gd);
extern void lwkt_switch(void);
extern void lwkt_preempt(thread_t ntd, int id);
extern void lwkt_schedule(thread_t td);
extern void lwkt_schedule_self(void);
extern void lwkt_deschedule(thread_t td);
extern void lwkt_deschedule_self(void);
extern void lwkt_yield(void);
extern void lwkt_yield_quick(void);

extern void lwkt_block(lwkt_wait_t w, const char *wmesg, int *gen);
extern void lwkt_signal(lwkt_wait_t w);
extern void lwkt_gettoken(lwkt_token_t tok);
extern void lwkt_reltoken(lwkt_token_t tok);
extern int  lwkt_regettoken(lwkt_token_t tok);
extern void lwkt_rwlock_init(lwkt_rwlock_t lock);
extern void lwkt_exlock(lwkt_rwlock_t lock, const char *wmesg);
extern void lwkt_shlock(lwkt_rwlock_t lock, const char *wmesg);
extern void lwkt_exunlock(lwkt_rwlock_t lock);
extern void lwkt_shunlock(lwkt_rwlock_t lock);
extern void lwkt_setpri(thread_t td, int pri);
extern void lwkt_setpri_self(int pri);
extern void crit_panic(void);
extern struct proc *lwkt_preempted_proc(void);


extern int  lwkt_create (void (*func)(void *), void *arg, struct thread **ptd,
			    struct thread *template, int tdflags,
			    const char *ctl, ...);
extern void lwkt_exit __P((void)) __dead2;

#endif

#endif

