/*
 * SYS/THREAD.H
 *
 *	Implements the architecture independant portion of the LWKT 
 *	subsystem.
 *
 * Types which must already be defined when this header is included by
 * userland:	struct md_thread
 * 
 * $DragonFly: src/sys/sys/thread.h,v 1.42 2004/02/12 20:43:57 drhodus Exp $
 */

#ifndef _SYS_THREAD_H_
#define _SYS_THREAD_H_

#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>		/* __int types */
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>		/* MAXCOMLEN */
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>		/* TAILQ_* macros */
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>	/* lwkt_port */
#endif

struct globaldata;
struct proc;
struct thread;
struct lwkt_queue;
struct lwkt_token;
struct lwkt_wait;
struct lwkt_ipiq;
struct lwkt_cpu_msg;
struct lwkt_cpu_port;
struct lwkt_rwlock;
struct lwkt_msg;
struct lwkt_port;
union sysunion;

typedef struct lwkt_queue	*lwkt_queue_t;
typedef struct lwkt_token	*lwkt_token_t;
typedef struct lwkt_wait	*lwkt_wait_t;
typedef struct lwkt_cpu_msg	*lwkt_cpu_msg_t;
typedef struct lwkt_cpu_port	*lwkt_cpu_port_t;
typedef struct lwkt_rwlock	*lwkt_rwlock_t;
typedef struct lwkt_ipiq	*lwkt_ipiq_t;
typedef struct thread 		*thread_t;

typedef TAILQ_HEAD(lwkt_queue, thread) lwkt_queue;

/*
 * Differentiation between kernel threads and user threads.  Userland
 * programs which want to access to kernel structures have to define
 * _KERNEL_STRUCTURES.  This is a kinda safety valve to prevent badly
 * written user programs from getting an LWKT thread that is neither the
 * kernel nor the user version.
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _MACHINE_THREAD_H_
#include <machine/thread.h>		/* md_thread */
#endif
#ifndef _MACHINE_FRAME_H_
#include <machine/frame.h>
#endif
#else
struct intrframe;
#endif

/*
 * Tokens arbitrate access to information.  They are 'soft' arbitrators
 * in that they are associated with cpus rather then threads, making the
 * optimal aquisition case very fast if your cpu already happens to own the
 * token you are requesting.
 */
typedef struct lwkt_token {
    struct globaldata *t_cpu;	/* the current owner of the token */
    struct globaldata *t_reqcpu;/* return ownership to this cpu on release */
    int		t_gen;		/* generation number */
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

#define MAXCPUFIFO      16	/* power of 2 */
#define MAXCPUFIFO_MASK	(MAXCPUFIFO - 1)

/*
 * Always cast to ipifunc_t when registering an ipi.  The actual ipi function
 * is called with both the data and an interrupt frame, but the ipi function
 * that is registered might only declare a data argument.
 */
typedef void (*ipifunc_t)(void *arg);
typedef void (*ipifunc2_t)(void *arg, struct intrframe *frame);

typedef struct lwkt_ipiq {
    int		ip_rindex;      /* only written by target cpu */
    int		ip_xindex;      /* writte by target, indicates completion */
    int		ip_windex;      /* only written by source cpu */
    ipifunc2_t	ip_func[MAXCPUFIFO];
    void	*ip_arg[MAXCPUFIFO];
} lwkt_ipiq;

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
 * cased and there is no 'token'.  A thread is always owned by the cpu
 * represented by td_gd, any manipulation of the thread by some other cpu
 * must be done through cpu_*msg() functions.  e.g. you could request
 * ownership of a thread that way, or hand a thread off to another cpu.
 *
 * NOTE: td_pri is bumped by TDPRI_CRIT when entering a critical section,
 * but this does not effect how the thread is scheduled by LWKT.
 */
struct md_intr_info;
struct caps_kinfo;

struct thread {
    TAILQ_ENTRY(thread) td_threadq;
    TAILQ_ENTRY(thread) td_allq;
    lwkt_port	td_msgport;	/* built-in message port for replies */
    struct proc	*td_proc;	/* (optional) associated process */
    struct pcb	*td_pcb;	/* points to pcb and top of kstack */
    struct globaldata *td_gd;	/* associated with this cpu */
    const char	*td_wmesg;	/* string name for blockage */
    void	*td_wchan;	/* waiting on channel */
    int		td_pri;		/* 0-31, 31=highest priority (note 1) */
    int		td_flags;	/* TDF flags */
    int		td_gen;		/* wait queue chasing generation number */
				/* maybe preempt */
    void	(*td_preemptable)(struct thread *td, int critpri);
    void	(*td_release)(struct thread *td);
    union {
	struct md_intr_info *intdata;
    } td_info;
    char	*td_kstack;	/* kernel stack */
    char	*td_sp;		/* kernel stack pointer for LWKT restore */
    void	(*td_switch)(struct thread *ntd);
    lwkt_wait_t td_wait;	/* thread sitting on wait structure */
    __uint64_t	td_uticks;	/* Statclock hits in user mode (uS) */
    __uint64_t	td_sticks;      /* Statclock hits in system mode (uS) */
    __uint64_t	td_iticks;	/* Statclock hits processing intr (uS) */
    int		td_locks;	/* lockmgr lock debugging YYY */
    int		td_refs;	/* hold position in gd_tdallq / hold free */
    int		td_nest_count;	/* prevent splz nesting */
#ifdef SMP
    int		td_mpcount;	/* MP lock held (count) */
#else
    int		td_unused001;
#endif
    char	td_comm[MAXCOMLEN+1]; /* typ 16+1 bytes */
    struct thread *td_preempted; /* we preempted this thread */
    struct caps_kinfo *td_caps;	/* list of client and server registrations */
    struct md_thread td_mach;
};

/*
 * Thread flags.  Note that TDF_RUNNING is cleared on the old thread after
 * we switch to the new one, which is necessary because LWKTs don't need
 * to hold the BGL.  This flag is used by the exit code and the managed
 * thread migration code.
 *
 * LWKT threads stay on their (per-cpu) run queue while running, not to
 * be confused with user processes which are removed from the user scheduling
 * run queue while actually running.
 */
#define TDF_RUNNING		0x0001	/* thread still active */
#define TDF_RUNQ		0x0002	/* on an LWKT run queue */
#define TDF_PREEMPT_LOCK	0x0004	/* I have been preempted */
#define TDF_PREEMPT_DONE	0x0008	/* acknowledge preemption complete */
#define TDF_IDLE_NOHLT		0x0010	/* we need to spin */

#define TDF_SYSTHREAD		0x0100	/* system thread */
#define TDF_ALLOCATED_THREAD	0x0200	/* zalloc allocated thread */
#define TDF_ALLOCATED_STACK	0x0400	/* zalloc allocated stack */
#define TDF_VERBOSE		0x0800	/* verbose on exit */
#define TDF_DEADLKTREAT		0x1000	/* special lockmgr deadlock treatment */
#define TDF_STOPREQ		0x2000	/* suspend_kproc */
#define TDF_WAKEREQ		0x4000	/* resume_kproc */
#define TDF_TIMEOUT		0x8000	/* tsleep timeout */
#define TDF_INTTHREAD		0x00010000	/* interrupt thread */

/*
 * Thread priorities.  Typically only one thread from any given
 * user process scheduling queue is on the LWKT run queue at a time.
 * Remember that there is one LWKT run queue per cpu.
 *
 * Critical sections are handled by bumping td_pri above TDPRI_MAX, which
 * causes interrupts to be masked as they occur.  When this occurs a
 * rollup flag will be set in mycpu->gd_reqflags.
 */
#define TDPRI_IDLE_THREAD	0	/* the idle thread */
#define TDPRI_USER_IDLE		4	/* user scheduler idle */
#define TDPRI_USER_NORM		6	/* user scheduler normal */
#define TDPRI_USER_REAL		8	/* user scheduler real time */
#define TDPRI_KERN_LPSCHED	9	/* scheduler helper for userland sch */
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

#define IN_CRITICAL_SECT(td)	((td)->td_pri >= TDPRI_CRIT)

#ifdef _KERNEL

extern struct vm_zone	*thread_zone;

#endif

/*
 * Applies both to the kernel and to liblwkt.
 */
extern struct thread *lwkt_alloc_thread(struct thread *template, int cpu);
extern void lwkt_init_thread(struct thread *td, void *stack, int flags,
	struct globaldata *gd);
extern void lwkt_set_comm(thread_t td, const char *ctl, ...);
extern void lwkt_wait_free(struct thread *td);
extern void lwkt_free_thread(struct thread *td);
extern void lwkt_init_wait(struct lwkt_wait *w);
extern void lwkt_gdinit(struct globaldata *gd);
extern void lwkt_switch(void);
extern void lwkt_maybe_switch(void);
extern void lwkt_preempt(thread_t ntd, int critpri);
extern void lwkt_schedule(thread_t td);
extern void lwkt_schedule_self(void);
extern void lwkt_deschedule(thread_t td);
extern void lwkt_deschedule_self(void);
extern void lwkt_acquire(thread_t td);
extern void lwkt_yield(void);
extern void lwkt_yield_quick(void);
extern void lwkt_hold(thread_t td);
extern void lwkt_rele(thread_t td);

extern void lwkt_block(lwkt_wait_t w, const char *wmesg, int *gen);
extern void lwkt_signal(lwkt_wait_t w, int count);
extern int lwkt_trytoken(lwkt_token_t tok);
extern int lwkt_gettoken(lwkt_token_t tok);
extern int lwkt_gentoken(lwkt_token_t tok, int *gen);
extern int lwkt_reltoken(lwkt_token_t tok);
extern void lwkt_inittoken(lwkt_token_t tok);
extern int  lwkt_regettoken(lwkt_token_t tok);
extern void lwkt_rwlock_init(lwkt_rwlock_t lock);
extern void lwkt_exlock(lwkt_rwlock_t lock, const char *wmesg);
extern void lwkt_shlock(lwkt_rwlock_t lock, const char *wmesg);
extern void lwkt_exunlock(lwkt_rwlock_t lock);
extern void lwkt_shunlock(lwkt_rwlock_t lock);
extern void lwkt_setpri(thread_t td, int pri);
extern void lwkt_setpri_self(int pri);
extern int  lwkt_send_ipiq(struct globaldata *targ, ipifunc_t func, void *arg);
extern int  lwkt_send_ipq_bycpu(int dcpu, ipifunc_t func, void *arg);
extern void lwkt_send_ipiq_mask(uint32_t mask, ipifunc_t func, void *arg);
extern void lwkt_wait_ipiq(struct globaldata *targ, int seq);
extern void lwkt_process_ipiq(void);
#ifdef _KERNEL
extern void lwkt_process_ipiq_frame(struct intrframe frame);
#endif
extern void crit_panic(void);
extern struct proc *lwkt_preempted_proc(void);

extern int  lwkt_create (void (*func)(void *), void *arg, struct thread **ptd,
			    struct thread *template, int tdflags, int cpu,
			    const char *ctl, ...);
extern void lwkt_exit (void) __dead2;

#endif

