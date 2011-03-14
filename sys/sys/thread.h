/*
 * SYS/THREAD.H
 *
 *	Implements the architecture independant portion of the LWKT 
 *	subsystem.
 *
 * Types which must already be defined when this header is included by
 * userland:	struct md_thread
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
#ifndef _SYS_TIME_H_
#include <sys/time.h>   	/* struct timeval */
#endif
#ifndef _SYS_LOCK_H
#include <sys/lock.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_IOSCHED_H_
#include <sys/iosched.h>
#endif
#include <machine/thread.h>

struct globaldata;
struct lwp;
struct proc;
struct thread;
struct lwkt_queue;
struct lwkt_token;
struct lwkt_tokref;
struct lwkt_ipiq;
struct lwkt_cpu_msg;
struct lwkt_cpu_port;
struct lwkt_msg;
struct lwkt_port;
struct lwkt_cpusync;
union sysunion;

typedef struct lwkt_queue	*lwkt_queue_t;
typedef struct lwkt_token	*lwkt_token_t;
typedef struct lwkt_tokref	*lwkt_tokref_t;
typedef struct lwkt_cpu_msg	*lwkt_cpu_msg_t;
typedef struct lwkt_cpu_port	*lwkt_cpu_port_t;
typedef struct lwkt_ipiq	*lwkt_ipiq_t;
typedef struct lwkt_cpusync	*lwkt_cpusync_t;
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
 * Tokens are used to serialize access to information.  They are 'soft'
 * serialization entities that only stay in effect while a thread is
 * running.  If the thread blocks, other threads can run holding the same
 * token(s).  The tokens are reacquired when the original thread resumes.
 *
 * A thread can depend on its serialization remaining intact through a
 * preemption.  An interrupt which attempts to use the same token as the
 * thread being preempted will reschedule itself for non-preemptive
 * operation, so the new token code is capable of interlocking against
 * interrupts as well as other cpus.  This means that your token can only
 * be (temporarily) lost if you *explicitly* block.
 *
 * Tokens are managed through a helper reference structure, lwkt_tokref.  Each
 * thread has a stack of tokref's to keep track of acquired tokens.  Multiple
 * tokref's may reference the same token.
 */

typedef struct lwkt_token {
    struct lwkt_tokref	*t_ref;		/* Owning ref or NULL */
    intptr_t		t_flags;	/* MP lock required */
    long		t_collisions;	/* Collision counter */
    cpumask_t		t_collmask;	/* Collision cpu mask for resched */
    const char		*t_desc;	/* Descriptive name */
} lwkt_token;

/*
 * Static initialization for a lwkt_token.
 */
#define LWKT_TOKEN_INITIALIZER(name)	\
{					\
	.t_ref = NULL,			\
	.t_flags = 0,			\
	.t_collisions = 0,		\
	.t_collmask = 0,		\
	.t_desc = #name			\
}

/*
 * Assert that a particular token is held
 */
#define LWKT_TOKEN_HELD(tok)		_lwkt_token_held(tok, curthread)

#define ASSERT_LWKT_TOKEN_HELD(tok)	\
	KKASSERT(LWKT_TOKEN_HELD(tok))

#define ASSERT_NO_TOKENS_HELD(td)	\
	KKASSERT((td)->td_toks_stop == &td->td_toks_array[0])

/*
 * Assert that a particular token is held and we are in a hard
 * code execution section (interrupt, ipi, or hard code section).
 * Hard code sections are not allowed to block or potentially block.
 * e.g. lwkt_gettoken() would only be ok if the token were already
 * held.
 */
#define ASSERT_LWKT_TOKEN_HARD(tok)					\
	do {								\
		globaldata_t zgd __debugvar = mycpu;			\
		KKASSERT((tok)->t_ref &&				\
			 (tok)->t_ref->tr_owner == zgd->gd_curthread &&	\
			 zgd->gd_intr_nesting_level > 0);		\
	} while(0)

/*
 * Assert that a particular token is held and we are in a normal
 * critical section.  Critical sections will not be preempted but
 * can explicitly block (tsleep, lwkt_gettoken, etc).
 */
#define ASSERT_LWKT_TOKEN_CRIT(tok)					\
	do {								\
		globaldata_t zgd __debugvar = mycpu;			\
		KKASSERT((tok)->t_ref &&				\
			 (tok)->t_ref->tr_owner == zgd->gd_curthread &&	\
			 zgd->gd_curthread->td_critcount > 0);		\
	} while(0)

struct lwkt_tokref {
    lwkt_token_t	tr_tok;		/* token in question */
    struct thread	*tr_owner;	/* me */
    intptr_t		tr_flags;	/* copy of t_flags */
    const void		*tr_stallpc;	/* stalled at pc */
};

#define MAXCPUFIFO      16	/* power of 2 */
#define MAXCPUFIFO_MASK	(MAXCPUFIFO - 1)
#define LWKT_MAXTOKENS	32	/* max tokens beneficially held by thread */

/*
 * Always cast to ipifunc_t when registering an ipi.  The actual ipi function
 * is called with both the data and an interrupt frame, but the ipi function
 * that is registered might only declare a data argument.
 */
typedef void (*ipifunc1_t)(void *arg);
typedef void (*ipifunc2_t)(void *arg, int arg2);
typedef void (*ipifunc3_t)(void *arg, int arg2, struct intrframe *frame);

typedef struct lwkt_ipiq {
    int		ip_rindex;      /* only written by target cpu */
    int		ip_xindex;      /* written by target, indicates completion */
    int		ip_windex;      /* only written by source cpu */
    ipifunc3_t	ip_func[MAXCPUFIFO];
    void	*ip_arg1[MAXCPUFIFO];
    int		ip_arg2[MAXCPUFIFO];
    u_int	ip_npoll;	/* synchronization to avoid excess IPIs */
} lwkt_ipiq;

/*
 * CPU Synchronization structure.  See lwkt_cpusync_start() and
 * lwkt_cpusync_finish() for more information.
 */
typedef void (*cpusync_func_t)(void *arg);

struct lwkt_cpusync {
    cpumask_t	cs_mask;		/* cpus running the sync */
    cpumask_t	cs_mack;		/* mask acknowledge */
    cpusync_func_t cs_func;		/* function to execute */
    void	*cs_data;		/* function data */
};

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
 * Thread structure.  Note that ownership of a thread structure is special
 * cased and there is no 'token'.  A thread is always owned by the cpu
 * represented by td_gd, any manipulation of the thread by some other cpu
 * must be done through cpu_*msg() functions.  e.g. you could request
 * ownership of a thread that way, or hand a thread off to another cpu.
 *
 * NOTE: td_ucred is synchronized from the p_ucred on user->kernel syscall,
 *	 trap, and AST/signal transitions to provide a stable ucred for
 *	 (primarily) system calls.  This field will be NULL for pure kernel
 *	 threads.
 */
struct md_intr_info;
struct caps_kinfo;

struct thread {
    TAILQ_ENTRY(thread) td_threadq;
    TAILQ_ENTRY(thread) td_allq;
    TAILQ_ENTRY(thread) td_sleepq;
    lwkt_port	td_msgport;	/* built-in message port for replies */
    struct lwp	*td_lwp;	/* (optional) associated lwp */
    struct proc	*td_proc;	/* (optional) associated process */
    struct pcb	*td_pcb;	/* points to pcb and top of kstack */
    struct globaldata *td_gd;	/* associated with this cpu */
    const char	*td_wmesg;	/* string name for blockage */
    const volatile void	*td_wchan;	/* waiting on channel */
    int		td_pri;		/* 0-31, 31=highest priority (note 1) */
    int		td_critcount;	/* critical section priority */
    int		td_flags;	/* TDF flags */
    int		td_wdomain;	/* domain for wchan address (typ 0) */
    void	(*td_preemptable)(struct thread *td, int critcount);
    void	(*td_release)(struct thread *td);
    char	*td_kstack;	/* kernel stack */
    int		td_kstack_size;	/* size of kernel stack */
    char	*td_sp;		/* kernel stack pointer for LWKT restore */
    void	(*td_switch)(struct thread *ntd);
    __uint64_t	td_uticks;	/* Statclock hits in user mode (uS) */
    __uint64_t	td_sticks;      /* Statclock hits in system mode (uS) */
    __uint64_t	td_iticks;	/* Statclock hits processing intr (uS) */
    int		td_locks;	/* lockmgr lock debugging */
    void	*td_dsched_priv1;	/* priv data for I/O schedulers */
    int		td_refs;	/* hold position in gd_tdallq / hold free */
    int		td_nest_count;	/* prevent splz nesting */
    int		td_unused01[2];	/* for future fields */
#ifdef SMP
    int		td_cscount;	/* cpu synchronization master */
#else
    int		td_cscount_unused;
#endif
    int		td_unused02[4];	/* for future fields */
    int		td_unused03[4];	/* for future fields */
    struct iosched_data td_iosdata;	/* Dynamic I/O scheduling data */
    struct timeval td_start;	/* start time for a thread/process */
    char	td_comm[MAXCOMLEN+1]; /* typ 16+1 bytes */
    struct thread *td_preempted; /* we preempted this thread */
    struct ucred *td_ucred;		/* synchronized from p_ucred */
    struct caps_kinfo *td_caps;	/* list of client and server registrations */
    lwkt_tokref_t td_toks_stop;
    struct lwkt_tokref td_toks_array[LWKT_MAXTOKENS];
    int		td_fairq_lticks;	/* fairq wakeup accumulator reset */
    int		td_fairq_accum;		/* fairq priority accumulator */
    const void	*td_mplock_stallpc;	/* last mplock stall address */
#ifdef DEBUG_CRIT_SECTIONS
#define CRIT_DEBUG_ARRAY_SIZE   32
#define CRIT_DEBUG_ARRAY_MASK   (CRIT_DEBUG_ARRAY_SIZE - 1)
    const char	*td_crit_debug_array[CRIT_DEBUG_ARRAY_SIZE];
    int		td_crit_debug_index;
    int		td_in_crit_report;	
#endif
    struct md_thread td_mach;
#ifdef DEBUG_LOCKS
#define SPINLOCK_DEBUG_ARRAY_SIZE	32
   int 	td_spinlock_stack_id[SPINLOCK_DEBUG_ARRAY_SIZE];
   struct spinlock *td_spinlock_stack[SPINLOCK_DEBUG_ARRAY_SIZE];
   void 	*td_spinlock_caller_pc[SPINLOCK_DEBUG_ARRAY_SIZE];

    /*
     * Track lockmgr locks held; lk->lk_filename:lk->lk_lineno is the holder
     */
#define LOCKMGR_DEBUG_ARRAY_SIZE	8
    int		td_lockmgr_stack_id[LOCKMGR_DEBUG_ARRAY_SIZE];
    struct lock	*td_lockmgr_stack[LOCKMGR_DEBUG_ARRAY_SIZE];
#endif
};

#define td_toks_base		td_toks_array[0]
#define td_toks_end		td_toks_array[LWKT_MAXTOKENS]

#define TD_TOKS_HELD(td)	((td)->td_toks_stop != &(td)->td_toks_base)
#define TD_TOKS_NOT_HELD(td)	((td)->td_toks_stop == &(td)->td_toks_base)

/*
 * Thread flags.  Note that TDF_RUNNING is cleared on the old thread after
 * we switch to the new one, which is necessary because LWKTs don't need
 * to hold the BGL.  This flag is used by the exit code and the managed
 * thread migration code.  Note in addition that preemption will cause
 * TDF_RUNNING to be cleared temporarily, so any code checking TDF_RUNNING
 * must also check TDF_PREEMPT_LOCK.
 *
 * LWKT threads stay on their (per-cpu) run queue while running, not to
 * be confused with user processes which are removed from the user scheduling
 * run queue while actually running.
 *
 * td_threadq can represent the thread on one of three queues... the LWKT
 * run queue, a tsleep queue, or an lwkt blocking queue.  The LWKT subsystem
 * does not allow a thread to be scheduled if it already resides on some
 * queue.
 */
#define TDF_RUNNING		0x0001	/* thread still active */
#define TDF_RUNQ		0x0002	/* on an LWKT run queue */
#define TDF_PREEMPT_LOCK	0x0004	/* I have been preempted */
#define TDF_PREEMPT_DONE	0x0008	/* acknowledge preemption complete */
#define TDF_UNUSED00000010	0x0010
#define TDF_MIGRATING		0x0020	/* thread is being migrated */
#define TDF_SINTR		0x0040	/* interruptability hint for 'ps' */
#define TDF_TSLEEPQ		0x0080	/* on a tsleep wait queue */

#define TDF_SYSTHREAD		0x0100	/* allocations may use reserve */
#define TDF_ALLOCATED_THREAD	0x0200	/* objcache allocated thread */
#define TDF_ALLOCATED_STACK	0x0400	/* objcache allocated stack */
#define TDF_VERBOSE		0x0800	/* verbose on exit */
#define TDF_DEADLKTREAT		0x1000	/* special lockmgr deadlock treatment */
#define TDF_STOPREQ		0x2000	/* suspend_kproc */
#define TDF_WAKEREQ		0x4000	/* resume_kproc */
#define TDF_TIMEOUT		0x8000	/* tsleep timeout */
#define TDF_INTTHREAD		0x00010000	/* interrupt thread */
#define TDF_TSLEEP_DESCHEDULED	0x00020000	/* tsleep core deschedule */
#define TDF_BLOCKED		0x00040000	/* Thread is blocked */
#define TDF_PANICWARN		0x00080000	/* panic warning in switch */
#define TDF_BLOCKQ		0x00100000	/* on block queue */
#define TDF_UNUSED00200000	0x00200000
#define TDF_EXITING		0x00400000	/* thread exiting */
#define TDF_USINGFP		0x00800000	/* thread using fp coproc */
#define TDF_KERNELFP		0x01000000	/* kernel using fp coproc */
#define TDF_UNUSED02000000	0x02000000
#define TDF_CRYPTO		0x04000000	/* crypto thread */
#define TDF_MARKER		0x80000000	/* fairq marker thread */

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
#define TDPRI_IDLE_WORK		1	/* idle work (page zero, etc) */
#define TDPRI_USER_SCHEDULER	2	/* user scheduler helper */
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

/*
 * Scale is the approximate number of ticks for which we desire the
 * entire gd_tdrunq to get service.  With hz = 100 a scale of 8 is 80ms.
 *
 * Setting this value too small will result in inefficient switching
 * rates.
 */
#define TDFAIRQ_SCALE		8
#define TDFAIRQ_MAX(gd)		((gd)->gd_fairq_total_pri * TDFAIRQ_SCALE)

#define LWKT_THREAD_STACK	(UPAGES * PAGE_SIZE)

#define IN_CRITICAL_SECT(td)	((td)->td_critcount)

#ifdef _KERNEL

/*
 * Global tokens
 */
extern struct lwkt_token mp_token;
extern struct lwkt_token pmap_token;
extern struct lwkt_token dev_token;
extern struct lwkt_token vm_token;
extern struct lwkt_token vmspace_token;
extern struct lwkt_token kvm_token;
extern struct lwkt_token proc_token;
extern struct lwkt_token tty_token;
extern struct lwkt_token vnode_token;
extern struct lwkt_token vmobj_token;

/*
 * Procedures
 */
extern void lwkt_init(void);
extern struct thread *lwkt_alloc_thread(struct thread *, int, int, int);
extern void lwkt_init_thread(struct thread *, void *, int, int,
			     struct globaldata *);
extern void lwkt_set_comm(thread_t, const char *, ...) __printflike(2, 3);
extern void lwkt_wait_free(struct thread *);
extern void lwkt_free_thread(struct thread *);
extern void lwkt_gdinit(struct globaldata *);
extern void lwkt_switch(void);
extern void lwkt_preempt(thread_t, int);
extern void lwkt_schedule(thread_t);
extern void lwkt_schedule_noresched(thread_t);
extern void lwkt_schedule_self(thread_t);
extern void lwkt_deschedule(thread_t);
extern void lwkt_deschedule_self(thread_t);
extern void lwkt_yield(void);
extern void lwkt_user_yield(void);
extern void lwkt_token_wait(void);
extern void lwkt_hold(thread_t);
extern void lwkt_rele(thread_t);
extern void lwkt_passive_release(thread_t);
extern void lwkt_maybe_splz(thread_t);

extern void lwkt_gettoken(lwkt_token_t);
extern void lwkt_gettoken_hard(lwkt_token_t);
extern int  lwkt_trytoken(lwkt_token_t);
extern void lwkt_reltoken(lwkt_token_t);
extern void lwkt_reltoken_hard(lwkt_token_t);
extern int  lwkt_cnttoken(lwkt_token_t, thread_t);
extern int  lwkt_getalltokens(thread_t);
extern void lwkt_relalltokens(thread_t);
extern void lwkt_drain_token_requests(void);
extern void lwkt_token_init(lwkt_token_t, const char *);
extern void lwkt_token_uninit(lwkt_token_t);

extern void lwkt_token_pool_init(void);
extern lwkt_token_t lwkt_token_pool_lookup(void *);
extern lwkt_token_t lwkt_getpooltoken(void *);
extern void lwkt_relpooltoken(void *);

extern void lwkt_setpri(thread_t, int);
extern void lwkt_setpri_initial(thread_t, int);
extern void lwkt_setpri_self(int);
extern void lwkt_fairq_schedulerclock(thread_t td);
extern void lwkt_fairq_setpri_self(int pri);
extern int lwkt_fairq_push(int pri);
extern void lwkt_fairq_pop(int pri);
extern void lwkt_fairq_yield(void);
extern void lwkt_setcpu_self(struct globaldata *);
extern void lwkt_migratecpu(int);

#ifdef SMP

extern void lwkt_giveaway(struct thread *);
extern void lwkt_acquire(struct thread *);
extern int  lwkt_send_ipiq3(struct globaldata *, ipifunc3_t, void *, int);
extern int  lwkt_send_ipiq3_passive(struct globaldata *, ipifunc3_t,
				    void *, int);
extern int  lwkt_send_ipiq3_nowait(struct globaldata *, ipifunc3_t,
				   void *, int);
extern int  lwkt_send_ipiq3_bycpu(int, ipifunc3_t, void *, int);
extern int  lwkt_send_ipiq3_mask(cpumask_t, ipifunc3_t, void *, int);
extern void lwkt_wait_ipiq(struct globaldata *, int);
extern int  lwkt_seq_ipiq(struct globaldata *);
extern void lwkt_process_ipiq(void);
extern void lwkt_process_ipiq_frame(struct intrframe *);
extern void lwkt_smp_stopped(void);
extern void lwkt_synchronize_ipiqs(const char *);

#endif /* SMP */

/* lwkt_cpusync_init() - inline function in sys/thread2.h */
extern void lwkt_cpusync_simple(cpumask_t, cpusync_func_t, void *);
extern void lwkt_cpusync_interlock(lwkt_cpusync_t);
extern void lwkt_cpusync_deinterlock(lwkt_cpusync_t);

extern void crit_panic(void) __dead2;
extern struct lwp *lwkt_preempted_proc(void);

extern int  lwkt_create (void (*func)(void *), void *, struct thread **,
		struct thread *, int, int,
		const char *, ...) __printflike(7, 8);
extern void lwkt_exit (void) __dead2;
extern void lwkt_remove_tdallq (struct thread *);

#endif

#endif

