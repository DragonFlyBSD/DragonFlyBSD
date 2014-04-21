/*-
 * Copyright (c) 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)proc.h	8.15 (Berkeley) 5/19/95
 * $FreeBSD: src/sys/sys/proc.h,v 1.99.2.9 2003/06/06 20:21:32 tegge Exp $
 */

#ifndef _SYS_PROC_H_
#define	_SYS_PROC_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)

#error "Userland must include sys/user.h instead of sys/proc.h"

#else

#include <sys/callout.h>		/* For struct callout_handle. */
#include <sys/filedesc.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/rtprio.h>			/* For struct rtprio. */
#include <sys/signal.h>
#include <sys/lock.h>
#ifndef _KERNEL
#include <sys/time.h>			/* For structs itimerval, timeval. */
#endif
#include <sys/ucred.h>
#include <sys/event.h>			/* For struct klist */
#include <sys/eventvar.h>
#include <sys/sysent.h>			/* For struct sysentvec */
#include <sys/thread.h>
#include <sys/varsym.h>
#include <sys/resourcevar.h>
#ifdef _KERNEL
#include <sys/globaldata.h>
#endif
#include <sys/systimer.h>
#include <sys/iosched.h>
#include <sys/usched.h>
#include <machine/proc.h>		/* Machine-dependent proc substruct. */
#include <machine/atomic.h>		/* Machine-dependent proc substruct. */
#include <sys/signalvar.h>

struct proc;
struct pgrp;
struct session;
struct lwp;

LIST_HEAD(proclist, proc);
LIST_HEAD(pgrplist, pgrp);
LIST_HEAD(sesslist, session);
LIST_HEAD(lwplist, lwp);

struct lwp_rb_tree;
RB_HEAD(lwp_rb_tree, lwp);
RB_PROTOTYPE2(lwp_rb_tree, lwp, u.lwp_rbnode, rb_lwp_compare, lwpid_t);

/*
 * One structure allocated per session.
 */
struct	session {
	LIST_ENTRY(session) s_list;	/* Hash chain. */
	int	s_count;		/* Ref cnt; pgrps in session. */
	struct	proc *s_leader;		/* Session leader. */
	struct	vnode *s_ttyvp;		/* Vnode of controlling terminal. */
	struct	tty *s_ttyp;		/* Controlling terminal. */
	pid_t	s_sid;			/* Session ID */
	char	s_login[roundup(MAXLOGNAME, sizeof(long))];	/* Setlogin() name. */
};

/*
 * One structure allocated per process group.
 */
struct	pgrp {
	LIST_ENTRY(pgrp) pg_list;	/* Hash chain. */
	struct proclist pg_members;	/* Pointer to pgrp members. */
	struct	session *pg_session;	/* Pointer to session. */
	struct  sigiolst pg_sigiolst;	/* List of sigio sources. */
	pid_t	pg_id;			/* Pgrp id. */
	int	pg_jobc;	/* # procs qualifying pgrp for job control */
	u_int	pg_refs;
	struct lwkt_token pg_token;
	struct lock pg_lock;
};

#define	PS_NOCLDWAIT	0x0001	/* No zombies if child dies */
#define	PS_NOCLDSTOP	0x0002	/* No SIGCHLD when children stop. */
#define	PS_CLDSIGIGN	0x0004	/* The SIGCHLD handler is SIG_IGN. */

/*
 * pargs, used to hold a copy of the command line, if it had a sane
 * length
 */
struct	pargs {
	u_int	ar_ref;		/* Reference count */
	u_int	ar_length;	/* Length */
	u_char	ar_args[0];	/* Arguments */
};

/*
 * Description of a process.
 *
 * This structure contains the information needed to manage a thread of
 * control, known in UN*X as a process; it has references to substructures
 * containing descriptions of things that the process uses, but may share
 * with related processes.  The process structure and the substructures
 * are always addressable except for those marked "(PROC ONLY)" below,
 * which might be addressable only on a processor on which the process
 * is running.
 *
 * NOTE!  The process start time is stored in the thread structure associated
 * with the process.  If the process is a Zombie, then this field will be
 * inaccessible due to the thread structure being free'd in kern_wait1().
 */

struct jail;
struct vkernel_proc;
struct vkernel_lwp;
struct vmspace_entry;
struct ktrace_node;
struct sem_undo;

enum lwpstat {
	LSRUN = 1,
	LSSTOP = 2,
	LSSLEEP = 3,
};

enum procstat {
	SIDL = 1,
	SACTIVE = 2,
	SSTOP = 3,
	SZOMB = 4,
};

struct lwp {
	TAILQ_ENTRY(lwp) lwp_procq;	/* run/sleep queue. */
	union {
	    RB_ENTRY(lwp)	lwp_rbnode;	/* RB tree node - lwp in proc */
	    LIST_ENTRY(lwp)	lwp_reap_entry;	/* reaper list */
	} u;

	struct proc	*lwp_proc;	/* Link to our proc. */
	struct vmspace	*lwp_vmspace;	/* Inherited from p_vmspace */
	struct vkernel_lwp *lwp_vkernel;/* VKernel support, lwp part */

	lwpid_t		lwp_tid;	/* Our thread id */

	u_int		lwp_flags;	/* LWP_*    flags */
	u_int		lwp_mpflags;	/* LWP_MP_* flags */
	enum lwpstat	lwp_stat;	/* LS* lwp status */
	int		lwp_lock;	/* lwp lock (prevent destruct) count */

	int		lwp_dupfd;	/* Sideways return value from fdopen */

	/*
	 * The following two fields are marked XXX since (at least) the
	 * 4.4BSD-Lite2 import.  I can only guess the reason:  It is ugly.
	 * These fields are used to pass the trap code from trapsignal() to
	 * postsig(), which gets called later from userret().
	 *
	 * The correct "fix" for these XXX is to convert our signal system
	 * to use signal queues, where each signal can carry its own meta
	 * data.
	 */
	int		lwp_sig;	/* for core dump/debugger XXX */
        u_long		lwp_code;	/* for core dump/debugger XXX */

	/*
	 * Scheduling.
	 */
	sysclock_t	lwp_cpticks;	/* cpu used in sched clock ticks */
	sysclock_t	lwp_cpbase;	/* Measurement base */
	fixpt_t		lwp_pctcpu;	/* %cpu for this process */
	u_int		lwp_slptime;	/* Time since last blocked. */
	u_int		lwp_rebal_ticks; /* Timestamp sched on current cpu */

	int		lwp_traceflag;	/* Kernel trace points. */

	struct rusage	lwp_ru;		/* stats for this lwp */

	union usched_data lwp_usdata;	/* User scheduler specific */

#define lwp_startcopy	lwp_cpumask
	cpumask_t	lwp_cpumask;
	sigset_t	lwp_siglist;	/* Signals arrived but not delivered. */
	sigset_t	lwp_oldsigmask;	/* saved mask from before sigpause */
	sigset_t	lwp_sigmask;	/* Current signal mask. */
	stack_t		lwp_sigstk;	/* sp & on stack state variable */

	struct rtprio	lwp_rtprio;	/* Realtime priority. */
#define	lwp_endcopy	lwp_md

	struct mdproc	lwp_md;		/* Any machine-dependent fields. */

	struct thread	*lwp_thread;	/* backpointer to proc's thread */
	void		*lwp_unused01;	/* for future fields */
	struct kqueue	lwp_kqueue;	/* for select/poll */
	u_int		lwp_kqueue_serial;
	struct lwkt_token lwp_token;	/* per-lwp token for signal/state */
	struct spinlock lwp_spin;	/* spinlock for signal handling */
	void		*lwp_reserveds1; /* reserved for lwp_saveusp */
	void		*lwp_reserveds2; /* reserved for lwp_saveupc */
};

struct	proc {
	LIST_ENTRY(proc) p_list;	/* List of all processes. */

	/* substructures: */
	struct ucred	*p_ucred;	/* Process owner's identity. */
	struct filedesc	*p_fd;		/* Ptr to open files structure. */
	struct filedesc_to_leader *p_fdtol; /* Ptr to tracking node XXX lwp */
	struct plimit	*p_limit;	/* Process limits. */
	struct pstats	*p_stats;
	u_int		p_mqueue_cnt;	/* Count of open mqueues. */
	long		p_waitgen;	/* wait interlock allows late signal */
	struct sigacts	*p_sigacts;
#define p_sigignore	p_sigacts->ps_sigignore
#define p_sigcatch	p_sigacts->ps_sigcatch
#define	p_rlimit	p_limit->pl_rlimit

	int		p_flags;	/* P_* flags. */
	enum procstat	p_stat;		/* S* process status. */
	char		p_pad1[3];

	pid_t		p_pid;		/* Process identifier. */
	LIST_ENTRY(proc) p_pglist;	/* List of processes in pgrp. */
	struct proc	*p_pptr;	/* Pointer to parent process. */
	LIST_ENTRY(proc) p_sibling;	/* List of sibling processes. */
	struct proclist p_children;	/* Pointer to list of children. */
	struct callout	p_ithandle;	/* for scheduling p_realtimer */
	struct varsymset p_varsymset;
	struct iosched_data p_iosdata;	/* Dynamic I/O scheduling data */

	pid_t		p_oppid;	/* Save parent pid during ptrace. XXX */

	struct vmspace	*p_vmspace;	/* Current address space. */

	unsigned int	p_swtime;	/* Time swapped in or out */

	struct itimerval p_realtimer;	/* Alarm timer. */
	struct itimerval p_timer[3];	/* Virtual-time timers. */

	int		p_traceflag;	/* Kernel trace points. */
	struct ktrace_node *p_tracenode; /* Trace to vnode. */

	sigset_t	p_siglist;	/* Signals arrived but not delivered. */

	struct vnode	*p_textvp;	/* Vnode of executable. */
	struct nchandle	p_textnch;	/* namecache handle of executable. */

	unsigned int	p_stops;	/* procfs event bitmask */
	unsigned int	p_stype;	/* procfs stop event type */
	char		p_step;		/* procfs stop *once* flag */
	unsigned char	p_pfsflags;	/* procfs flags */
	char		p_pad2[2];
	struct		sigiolst p_sigiolst;	/* list of sigio sources */
	int		p_sigparent;	/* signal to parent on exit */
	struct klist	p_klist;	/* knotes attached to this process */

	struct timeval	p_start;	/* start time for a process */

	struct rusage	p_ru;		/* stats for this proc */
	struct rusage	p_cru;		/* sum of stats for reaped children */
	void		*p_dsched_priv1;

/* The following fields are all copied upon creation in fork. */
#define	p_startcopy	p_comm

	char		p_comm[MAXCOMLEN+1]; /* typ 16+1 bytes */
	char		p_pad3;
	char		p_nice;		/* Process "nice" value. */
	char		p_pad4;
	int		p_osrel;	/* release date for binary ELF note */

	struct pgrp	*p_pgrp;	/* Pointer to process group. */

	struct sysentvec *p_sysent;	/* System call dispatch information. */

	struct uprof	p_prof;		/* Profiling arguments. */
	struct rtprio	p_rtprio;	/* Realtime priority. */
	struct pargs	*p_args;
	u_short		p_xstat;	/* Exit status or last stop signal */

	int		p_ionice;
	void		*p_dsched_priv2;
/* End area that is copied on creation. */
#define	p_endcopy	p_dsched_priv2
	u_short		p_acflag;	/* Accounting flags. */

	int		p_lock;		/* Prevent proc destruction */
	int		p_nthreads;	/* Number of threads in this process. */
	int		p_nstopped;	/* Number of stopped threads. */
	int		p_lasttid;	/* Last tid used. */
	struct lwp_rb_tree p_lwp_tree;	/* RB tree of LWPs for this process */
	void		*p_aioinfo;	/* ASYNC I/O info */
	int		p_wakeup;	/* thread id XXX lwp */
	struct proc	*p_peers;	/* XXX lwp */
	struct proc	*p_leader;	/* XXX lwp */
	void		*p_emuldata;	/* process-specific emulator state */
	struct usched	*p_usched;	/* Userland scheduling control */
	struct vkernel_proc *p_vkernel; /* VKernel support, proc part */
	int		p_numposixlocks; /* number of POSIX locks */
	void		(*p_userret)(void);/* p: return-to-user hook */

	struct spinlock p_spin;		/* Spinlock for LWP access to proc */
	struct lwkt_token p_token;	/* Token for LWP access to proc */
	struct sem_undo	*p_sem_undo;	/* Fast semaphore tracking lookup */
	void		*p_drv_priv;	/* scp linkage (for syscons) */
	void		*p_vmm;
	cpumask_t	p_vmm_cpumask;
};

#define lwp_wchan	lwp_thread->td_wchan
#define lwp_wmesg	lwp_thread->td_wmesg
#define	p_session	p_pgrp->pg_session
#define	p_pgid		p_pgrp->pg_id

/* These flags are kept in p_flags. */
#define	P_ADVLOCK	0x00001	/* Process may hold a POSIX advisory lock */
#define	P_CONTROLT	0x00002	/* Has a controlling terminal */
#define	P_SWAPPEDOUT	0x00004	/* Swapped out of memory */
#define P_SYSVSEM	0x00008	/* Might have SysV semaphores */
#define	P_PPWAIT	0x00010	/* Parent is waiting for child to exec/exit */
#define	P_PROFIL	0x00020	/* Has started profiling */
#define P_POSTEXIT	0x00040 /* Prevent procfs from stepping after this pt */
#define	P_SCMOUSE	0x00080 /* Process is held by syscons ioctl */
				/* was: Sleep is interruptible */
#define	P_SUGID		0x00100	/* Had set id privileges since last exec */
#define	P_SYSTEM	0x00200	/* System proc: no sigs, stats or swapping */
#define	P_MAYBETHREADED	0x00400	/* might be threaded optimization */
#define	P_TRACED	0x00800	/* Debugged process being traced */
#define	P_WAITED	0x01000	/* SIGSTOP status was returned by wait3/4 */
#define	P_WEXIT		0x02000	/* Working on exiting (master exit) */
#define	P_EXEC		0x04000	/* Process called exec */
#define	P_CONTINUED	0x08000	/* Proc has continued from a stopped state */

#define P_UNUSED16	0x00010000
#define	P_UNUSED17	0x00020000

#define	P_SWAPWAIT	0x00040000 /* Waiting for a swapin */
#define	P_UNUSED19	0x00080000 /* was: Now in a zombied state */

/* Marked a kernel thread */
#define	P_UNUSED20	0x00100000 /* was: on a user scheduling run queue */
#define	P_KTHREADP	0x00200000 /* Process is really a kernel thread */
#define P_IDLESWAP	0x00400000 /* Swapout was due to idleswap, not load */

#define	P_JAILED	0x01000000 /* Process is in jail */
#define	P_SIGVTALRM	0x02000000 /* signal SIGVTALRM pending due to itimer */
#define	P_SIGPROF	0x04000000 /* signal SIGPROF pending due to itimer */
#define	P_INEXEC	0x08000000 /* Process is in execve(). */
#define P_UNUSED28	0x10000000
#define	P_UNUSED29	0x20000000
#define P_XCPU		0x40000000 /* SIGXCPU */

#define	LWP_ALTSTACK	0x0000001 /* have alternate signal stack */
#define	LWP_OLDMASK	0x0000002 /* need to restore mask before pause */
#define	LWP_SINTR	0x0000008 /* Sleep is interruptible. */
#define LWP_SELECT	0x0000010 /* Selecting; wakeup/waiting danger. */
#define LWP_UNUSED20	0x0000020
#define	LWP_UNUSED40	0x0000040
#define	LWP_UNUSED80	0x0000080
#define LWP_PASSIVE_ACQ	0x0000100 /* Passive acquire cpu (see kern_switch) */
#define LWP_PAGING	0x0000200 /* Currently in vm_fault */

/*
 * LWP_MP_WSTOP: When set the thread will stop prior to return to userland
 *	      and has been counted in the process stop-threads-count, but
 *	      may still be running in kernel-land.
 *
 * LWP_MP_WEXIT: When set the thread has been asked to exit and will not return
 *	      to userland.  p_nthreads will not be decremented until the
 *	      thread has actually exited.
 */
#define	LWP_MP_ONRUNQ	0x0000001 /* on a user scheduling run queue */
#define LWP_MP_WEXIT	0x0000002 /* working on exiting (lwpuserret()) */
#define	LWP_MP_WSTOP	0x0000004 /* working on stopping */
#define	LWP_MP_ULOAD	0x0000008 /* uload accounting for current cpu */
#define	LWP_MP_RRFORCE	0x0000010 /* forced resched due to rrcount */
#define LWP_MP_VNLRU	0x0000020 /* check excessive vnode allocations (lwpuserret()) */

#define LWP_MP_URETMASK	(LWP_MP_WEXIT | LWP_MP_VNLRU)

#define	FIRST_LWP_IN_PROC(p)		RB_FIRST(lwp_rb_tree, &(p)->p_lwp_tree)
#define	FOREACH_LWP_IN_PROC(lp, p)	\
	RB_FOREACH(lp, lwp_rb_tree, &(p)->p_lwp_tree)
#define	ONLY_LWP_IN_PROC(p)		_only_lwp_in_proc(p, __func__)

#ifdef _KERNEL
static inline struct lwp *
_only_lwp_in_proc(struct proc *p, const char *caller)
{
	if (p->p_nthreads != 1) {
		panic("%s: proc %p (pid %d cmd %s) has more than one thread",
		    caller, p, p->p_pid, p->p_comm);
	}
	return RB_ROOT(&p->p_lwp_tree);
}
#endif

/*
 * We use process IDs <= PID_MAX.  NO_PID must also fit in a pid_t and
 * is used to represent "no process group".
 */
#define	PID_MAX		999999
#define	NO_PID		0x7FFFFFFF

#define SESS_LEADER(p)	((p)->p_session->s_leader == (p))

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_SESSION);
MALLOC_DECLARE(M_PROC);
MALLOC_DECLARE(M_LWP);
MALLOC_DECLARE(M_SUBPROC);
MALLOC_DECLARE(M_PARGS);
#endif

/* for priv_check_cred() */
#define	NULL_CRED_OKAY	0x2

/* Handy macro to determine if p1 can mangle p2 */

#define PRISON_CHECK(cr1, cr2) \
	((!(cr1)->cr_prison) || (cr1)->cr_prison == (cr2)->cr_prison)

/*
 * STOPEVENT
 */
extern void stopevent(struct proc*, unsigned int, unsigned int);
#define	STOPEVENT(p,e,v)			\
	do {					\
		if ((p)->p_stops & (e)) {	\
			stopevent(p,e,v);	\
		}				\
	} while (0)

/*
 * Hold process in memory, don't destruct, used by ktrace, procfs, sigio,
 * and signaling code (e.g. ksignal()).
 *
 * MPSAFE
 */
#define PHOLD(p)	phold((p))
#define PRELE(p)	prele((p))
#define PHOLDZOMB(p)	pholdzomb((p))
#define PRELEZOMB(p)	prelezomb((p))
#define PSTALL(p, msg, n) \
	do { if ((p)->p_lock > (n)) pstall((p), (msg), (n)); } while (0)

/*
 * Hold lwp in memory, don't destruct, normally for ptrace/procfs work
 * atomic ops because they can occur from an IPI.
 * MPSAFE
 */
#define LWPHOLD(lp)	atomic_add_int(&(lp)->lwp_lock, 1)
#define LWPRELE(lp)	atomic_add_int(&(lp)->lwp_lock, -1)

extern struct proc proc0;		/* Process slot for swapper. */
extern struct lwp lwp0;			/* LWP slot for swapper. */
extern struct thread thread0;		/* Thread slot for swapper. */
extern int hogticks;			/* Limit on kernel cpu hogs. */
extern int nprocs, maxproc;		/* Current and max number of procs. */
extern int maxprocperuid;		/* Max procs per uid. */
extern int sched_quantum;		/* Scheduling quantum in ticks */

extern struct proc *initproc;		/* Process slot for init */
extern struct thread *pagethread, *updatethread;

/*
 * Scheduler independant variables.  The primary scheduler polling frequency,
 * the maximum ESTCPU value, and the weighting factor for nice values.  A
 * cpu bound program's estcpu will increase to ESTCPUMAX - 1.
 */
#define ESTCPUFREQ	50

extern	u_long ps_arg_cache_limit;
extern	int ps_argsopen;
extern	int ps_showallprocs;

struct proc *pfind (pid_t);	/* Find process by id w/ref */
struct proc *pfindn (pid_t);	/* Find process by id wo/ref */
struct pgrp *pgfind (pid_t);	/* Find process group by id w/ref */
struct proc *zpfind (pid_t);	/* Find zombie process by id w/ref */
void pgref (struct pgrp *);	/* Ref pgrp preventing disposal */
void pgrel (struct pgrp *);	/* Deref pgrp & dispose on 1->0 trans */

struct globaldata;
struct lwp_params;

int	enterpgrp (struct proc *p, pid_t pgid, int mksess);
void	proc_add_allproc(struct proc *p);
void	proc_move_allproc_zombie(struct proc *);
void	proc_remove_zombie(struct proc *);
void	allproc_scan(int (*callback)(struct proc *, void *), void *data);
void	alllwp_scan(int (*callback)(struct lwp *, void *), void *data);
void	zombproc_scan(int (*callback)(struct proc *, void *), void *data);
void	fixjobc (struct proc *p, struct pgrp *pgrp, int entering);
void	updatepcpu(struct lwp *, int, int);
int	inferior (struct proc *p);
int	leavepgrp (struct proc *p);
void	sess_hold(struct session *sp);
void	sess_rele(struct session *sp);
void	procinit (void);
void	procinsertinit(struct proc *p);
void	sessinsertinit(struct session *sess);
void	pgrpinsertinit(struct pgrp *pg);
void	relscurproc(struct proc *curp);
int	p_trespass (struct ucred *cr1, struct ucred *cr2);
void	setrunnable (struct lwp *);
void	proc_stop (struct proc *);
void	proc_unstop (struct proc *);
void	sleep_gdinit (struct globaldata *);
thread_t cpu_heavy_switch (struct thread *);
thread_t cpu_lwkt_switch (struct thread *);

void	cpu_lwp_exit (void) __dead2;
void	cpu_thread_exit (void) __dead2;
void	lwp_exit (int masterexit, void *waddr) __dead2;
void	lwp_dispose (struct lwp *);
int	killalllwps (int);
void	exit1 (int) __dead2;
void	cpu_fork (struct lwp *, struct lwp *, int);
int	cpu_prepare_lwp(struct lwp *, struct lwp_params *);
void	cpu_set_fork_handler (struct lwp *, void (*)(void *, struct trapframe *), void *);
void	cpu_set_thread_handler(struct thread *td, void (*retfunc)(void), void *func, void *arg);
int	fork1 (struct lwp *, int, struct proc **);
void	start_forked_proc (struct lwp *, struct proc *);
int	trace_req (struct proc *);
void	cpu_thread_wait (struct thread *);
void	setsugid (void);
void	faultin (struct proc *p);
void	swapin_request (void);
void	phold (struct proc *);
void	prele (struct proc *);
int	pholdzomb (struct proc *);
void	prelezomb (struct proc *);
void	pstall (struct proc *, const char *, int);
void	lwpuserret(struct lwp *);
void	lwpkthreaddeferred(void);

u_int32_t	procrunnable (void);

#endif	/* _KERNEL */

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_PROC_H_ */
