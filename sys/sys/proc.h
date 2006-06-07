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
 * $DragonFly: src/sys/sys/proc.h,v 1.82 2006/06/07 03:02:11 dillon Exp $
 */

#ifndef _SYS_PROC_H_
#define	_SYS_PROC_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)

#error "Userland must include sys/user.h instead of sys/proc.h"

#else

#include <sys/callout.h>		/* For struct callout_handle. */
#include <sys/filedesc.h>
#include <sys/queue.h>
#include <sys/rtprio.h>			/* For struct rtprio. */
#include <sys/signal.h>
#ifndef _KERNEL
#include <sys/time.h>			/* For structs itimerval, timeval. */
#endif
#include <sys/ucred.h>
#include <sys/event.h>			/* For struct klist */
#include <sys/thread.h>
#include <sys/varsym.h>
#include <sys/upcall.h>
#include <sys/resourcevar.h>
#ifdef _KERNEL
#include <sys/globaldata.h>
#endif
#include <sys/systimer.h>
#include <sys/usched.h>
#include <machine/proc.h>		/* Machine-dependent proc substruct. */

/*
 * One structure allocated per session.
 */
struct	session {
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
	LIST_ENTRY(pgrp) pg_hash;	/* Hash chain. */
	LIST_HEAD(, proc) pg_members;	/* Pointer to pgrp members. */
	struct	session *pg_session;	/* Pointer to session. */
	struct  sigiolst pg_sigiolst;	/* List of sigio sources. */
	pid_t	pg_id;			/* Pgrp id. */
	int	pg_jobc;	/* # procs qualifying pgrp for job control */
};

struct	procsig {
	sigset_t ps_sigignore;	/* Signals being ignored. */
	sigset_t ps_sigcatch;	/* Signals being caught by user. */
	int      ps_flag;
	struct	 sigacts *ps_sigacts;
	int	 ps_refcnt;
};

#define	PS_NOCLDWAIT	0x0001	/* No zombies if child dies */
#define	PS_NOCLDSTOP	0x0002	/* No SIGCHLD when children stop. */

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
struct ktrace_node;

struct lwp {
	TAILQ_ENTRY(lwp) lwp_procq;	/* run/sleep queue. */
	LIST_ENTRY(lwp) lwp_list;	/* List of all threads in the proc. */

	struct proc	*lwp_proc;	/* Link to our proc. */

	int		lwp_tid;	/* Our thread id . */

	struct pstats	*lwp_stats;	/* Accounting/statistics (PROC ONLY). */
#ifdef notyet
	int		lwp_flag;	/* P_* flags. */
	char		lwp_stat;	/* S* process status. */
#endif

#define lwp_startzero	lwp_dupfd
	int		lwp_dupfd;	/* Sideways return value from fdopen. XXX */

	/*
	 * Scheduling.
	 */
	sysclock_t	lwp_cpticks;	/* cpu used in sched clock ticks */
	sysclock_t	lwp_cpbase;	/* Measurement base */
	fixpt_t		lwp_pctcpu;	/* %cpu for this process */
	u_int		lwp_swtime;	/* Time swapped in or out. */
	u_int		lwp_slptime;	/* Time since last blocked. */

	int		lwp_traceflag;	/* Kernel trace points. */

	union usched_data lwp_usdata;	/* User scheduler specific */
#define lwp_endzero	lwp_startcopy

#define lwp_startcopy	lwp_cpumask
	cpumask_t	lwp_cpumask;
	sigset_t	lwp_siglist;	/* Signals arrived but not delivered. */
	sigset_t	lwp_oldsigmask;	/* saved mask from before sigpause */
	sigset_t	lwp_sigmask;	/* Current signal mask. */
	stack_t		lwp_sigstk;	/* sp & on stack state variable */

	struct rtprio	lwp_rtprio;	/* Realtime priority. */
#define	lwp_endcopy	lwp_md

#ifdef notyet
	struct user	*lwp_addr;	/* XXX Really struct user? */
#endif
	struct mdproc	lwp_md;		/* Any machine-dependent fields. */

	struct thread	*lwp_thread;	/* backpointer to proc's thread */
	struct upcall	*lwp_upcall;	/* REGISTERED USERLAND POINTER! */
};

struct	proc {
	LIST_ENTRY(proc) p_list;	/* List of all processes. */

	/* substructures: */
	struct ucred	*p_ucred;	/* Process owner's identity. */
	struct filedesc	*p_fd;		/* Ptr to open files structure. */
	struct filedesc_to_leader *p_fdtol; /* Ptr to tracking node XXX lwp */
#define p_stats p_lwp.lwp_stats
	struct plimit	*p_limit;	/* Process limits. */
	void		*p_pad0;
	struct	procsig	*p_procsig;
#define p_sigacts	p_procsig->ps_sigacts
#define p_sigignore	p_procsig->ps_sigignore
#define p_sigcatch	p_procsig->ps_sigcatch
#define	p_rlimit	p_limit->pl_rlimit

	int		p_flag;		/* P_* flags. */
	char		p_stat;		/* S* process status. */
	char		p_pad1[3];

	pid_t		p_pid;		/* Process identifier. */
	LIST_ENTRY(proc) p_hash;	/* Hash chain. */
	LIST_ENTRY(proc) p_pglist;	/* List of processes in pgrp. */
	struct proc	*p_pptr;	/* Pointer to parent process. */
	LIST_ENTRY(proc) p_sibling;	/* List of sibling processes. */
	LIST_HEAD(, proc) p_children;	/* Pointer to list of children. */
	struct callout	p_ithandle;	/* for scheduling p_realtimer */
	struct varsymset p_varsymset;

/* The following fields are all zeroed upon creation in fork. */
#define	p_startzero	p_oppid

	pid_t		p_oppid;	/* Save parent pid during ptrace. XXX */

	struct vmspace	*p_vmspace;	/* Address space. */

#define p_cpticks p_lwp.lwp_cpticks
#define p_cpbase p_lwp.lwp_cpbase
#define p_pctcpu p_lwp.lwp_pctcpu
#define p_swtime p_lwp.lwp_swtime
#define p_slptime p_lwp.lwp_slptime

	struct itimerval p_realtimer;	/* Alarm timer. */
	struct itimerval p_timer[3];	/* Virtual-time timers. */

	int		p_traceflag;	/* Kernel trace points. */
	struct ktrace_node *p_tracenode; /* Trace to vnode. */

	sigset_t	p_siglist;	/* Signals arrived but not delivered. */

	struct vnode	*p_textvp;	/* Vnode of executable. */

#define p_usdata p_lwp.lwp_usdata
	
	unsigned int	p_stops;	/* procfs event bitmask */
	unsigned int	p_stype;	/* procfs stop event type */
	char		p_step;		/* procfs stop *once* flag */
	unsigned char	p_pfsflags;	/* procfs flags */
	char		p_pad2[2];	/* padding for alignment */
	struct		sigiolst p_sigiolst;	/* list of sigio sources */
	int		p_sigparent;	/* signal to parent on exit */
#define p_oldsigmask p_lwp.lwp_oldsigmask
	int		p_sig;		/* for core dump/debugger XXX */
        u_long		p_code;		/* for core dump/debugger XXX */
	struct klist	p_klist;	/* knotes attached to this process */

	struct timeval	p_start;	/* start time for a process */

/* End area that is zeroed on creation. */
#define	p_endzero	p_startcopy

/* The following fields are all copied upon creation in fork. */
#define	p_startcopy	p_comm

#define p_sigmask p_lwp.lwp_sigmask
#define p_sigstk p_lwp.lwp_sigstk

	char		p_comm[MAXCOMLEN+1]; /* typ 16+1 bytes */
	char		p_lock;		/* Process lock (prevent swap) count. */
	char		p_nice;		/* Process "nice" value. */
	char		p_pad3;

	struct pgrp	*p_pgrp;	/* Pointer to process group. */

	struct sysentvec *p_sysent;	/* System call dispatch information. */

	struct uprof	p_prof;		/* Profiling arguments. */
	struct rtprio	p_rtprio;	/* Realtime priority. */
	struct pargs	*p_args;
/* End area that is copied on creation. */
#define	p_endcopy	p_addr
	struct user	*p_addr;	/* Kernel virtual addr of u-area (PROC ONLY) XXX lwp */
#define p_md p_lwp.lwp_md

	u_short		p_xstat;	/* Exit status or last stop signal */
	u_short		p_acflag;	/* Accounting flags. */
	struct		rusage *p_ru;	/* Exit information. XXX */

	int		p_nthreads;	/* Number of threads in this process. */
	int		p_nstopped;	/* Number of stopped threads. */
	int		p_lasttid;	/* Last tid used. */
	LIST_HEAD(, lwp) p_lwps;	/* List of threads in this process. */
	void		*p_aioinfo;	/* ASYNC I/O info */
	int		p_wakeup;	/* thread id XXX lwp */
	struct proc	*p_peers;	/* XXX lwp */
	struct proc	*p_leader;	/* XXX lwp */
	void		*p_emuldata;	/* process-specific emulator state */
#define p_thread p_lwp.lwp_thread
	struct usched	*p_usched;	/* Userland scheduling control */
	int		p_numposixlocks; /* number of POSIX locks */

	struct lwp	p_lwp;		/* Embedded lwp XXX */
	struct spinlock p_spin;		/* Spinlock for LWP access to proc */
};

#if defined(_KERNEL)
#define p_wchan		p_thread->td_wchan
#define p_wmesg		p_thread->td_wmesg
#define	p_session	p_pgrp->pg_session
#define	p_pgid		p_pgrp->pg_id
#endif

/*
 * Status values.   Please note that p_stat doesn't actually take on
 * synthesized states.
 */
#define	SIDL	1		/* Process being created by fork. */
#define	SRUN	2		/* Currently runnable. */
#define	SSLEEP	3		/* Sleeping on an address. */
#define	SSTOP	4		/* Synthesized from SSLEEP + P_STOPPED */
#define	SZOMB	5		/* Synthesized from P_ZOMBIE for eproc only */
#define STHREAD	6		/* Synthesized for eproc only */

/* These flags are kept in p_flags. */
#define	P_ADVLOCK	0x00001	/* Process may hold a POSIX advisory lock. */
#define	P_CONTROLT	0x00002	/* Has a controlling terminal. */
#define	P_SWAPPEDOUT	0x00004	/* Swapped out of memory */
#define P_BREAKTSLEEP	0x00008	/* Event pending, break tsleep on sigcont */
#define	P_PPWAIT	0x00010	/* Parent is waiting for child to exec/exit. */
#define	P_PROFIL	0x00020	/* Has started profiling. */
#define P_SELECT	0x00040 /* Selecting; wakeup/waiting danger. */
#define	P_SINTR		0x00080	/* Sleep is interruptible. */
#define	P_SUGID		0x00100	/* Had set id privileges since last exec. */
#define	P_SYSTEM	0x00200	/* System proc: no sigs, stats or swapping. */
#define	P_STOPPED	0x00400	/* SIGSTOP status */
#define	P_TRACED	0x00800	/* Debugged process being traced. */
#define	P_WAITED	0x01000	/* SIGSTOP status was returned by wait3/4 */
#define	P_WEXIT		0x02000	/* Working on exiting. */
#define	P_EXEC		0x04000	/* Process called exec. */

/* Should probably be changed into a hold count. */
/* was	P_NOSWAP	0x08000	was: Do not swap upages; p->p_hold */
/* was	P_PHYSIO	0x10000	was: Doing physical I/O; use p->p_hold */

#define	P_UPCALLPEND	0x20000	/* an upcall is pending */

#define	P_SWAPWAIT	0x40000	/* Waiting for a swapin */
#define	P_ZOMBIE	0x80000	/* Now in a zombied state */

/* Marked a kernel thread */
#define	P_ONRUNQ	0x100000 /* on a user scheduling run queue */
#define	P_KTHREADP	0x200000 /* Process is really a kernel thread */
#define P_IDLESWAP	0x400000 /* Swapout was due to idleswap, not load */
#define	P_DEADLKTREAT   0x800000 /* lock aquisition - deadlock treatment */

#define	P_JAILED	0x1000000 /* Process is in jail */
#define	P_OLDMASK	0x2000000 /* need to restore mask before pause */
#define	P_ALTSTACK	0x4000000 /* have alternate signal stack */
#define	P_INEXEC	0x8000000 /* Process is in execve(). */
#define P_PASSIVE_ACQ	0x10000000 /* Passive acquire cpu (see kern_switch) */
#define	P_UPCALLWAIT	0x20000000 /* Wait for upcall or signal */
#define P_XCPU		0x40000000 /* SIGXCPU */

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_SESSION);
MALLOC_DECLARE(M_SUBPROC);
MALLOC_DECLARE(M_ZOMBIE);
MALLOC_DECLARE(M_PARGS);
#endif

/* flags for suser_xxx() */
#define PRISON_ROOT	0x1
#define	NULL_CRED_OKAY	0x2

/* Handy macro to determine if p1 can mangle p2 */

#define PRISON_CHECK(cr1, cr2) \
	((!(cr1)->cr_prison) || (cr1)->cr_prison == (cr2)->cr_prison)

/*
 * We use process IDs <= PID_MAX; PID_MAX + 1 must also fit in a pid_t,
 * as it is used to represent "no process group".
 */
#define	PID_MAX		99999
#define	NO_PID		100000

#define SESS_LEADER(p)	((p)->p_session->s_leader == (p))

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

/* hold process U-area in memory, normally for ptrace/procfs work */
#define PHOLD(p)	(++(p)->p_lock)
#define PRELE(p)	(--(p)->p_lock)

#define	PIDHASH(pid)	(&pidhashtbl[(pid) & pidhash])
extern LIST_HEAD(pidhashhead, proc) *pidhashtbl;
extern u_long pidhash;

#define	PGRPHASH(pgid)	(&pgrphashtbl[(pgid) & pgrphash])
extern LIST_HEAD(pgrphashhead, pgrp) *pgrphashtbl;
extern u_long pgrphash;

#if 0 
#ifndef SET_CURPROC
#define SET_CURPROC(p)	(curproc = (p))
#endif
#endif

extern struct proc proc0;		/* Process slot for swapper. */
extern struct thread thread0;		/* Thread slot for swapper. */
extern int hogticks;			/* Limit on kernel cpu hogs. */
extern int nprocs, maxproc;		/* Current and max number of procs. */
extern int maxprocperuid;		/* Max procs per uid. */
extern int sched_quantum;		/* Scheduling quantum in ticks */

LIST_HEAD(proclist, proc);
extern struct proclist allproc;		/* List of all processes. */
extern struct proclist zombproc;	/* List of zombie processes. */
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

struct proc *pfind (pid_t);	/* Find process by id. */
struct pgrp *pgfind (pid_t);	/* Find process group by id. */
struct proc *zpfind (pid_t);	/* Find zombie process by id. */

struct vm_zone;
struct globaldata;
extern struct vm_zone *proc_zone;

int	enterpgrp (struct proc *p, pid_t pgid, int mksess);
void	proc_add_allproc(struct proc *p);
void	proc_move_allproc_zombie(struct proc *);
void	proc_remove_zombie(struct proc *);
void	allproc_scan(int (*callback)(struct proc *, void *), void *data);
void	zombproc_scan(int (*callback)(struct proc *, void *), void *data);
void	fixjobc (struct proc *p, struct pgrp *pgrp, int entering);
void	updatepcpu(struct lwp *, int, int);
int	inferior (struct proc *p);
int	leavepgrp (struct proc *p);
void	sess_hold(struct session *sp);
void	sess_rele(struct session *sp);
void	mi_switch (struct proc *p);
void	procinit (void);
void	relscurproc(struct proc *curp);
int	p_trespass (struct ucred *cr1, struct ucred *cr2);
void	setrunnable (struct proc *);
void	clrrunnable (struct proc *);
void	sleep_gdinit (struct globaldata *);
int	suser (struct thread *td);
int	suser_proc (struct proc *p);
int	suser_cred (struct ucred *cred, int flag);
void	cpu_heavy_switch (struct thread *);
void	cpu_lwkt_switch (struct thread *);

void	cpu_proc_exit (void) __dead2;
void	cpu_thread_exit (void) __dead2;
void	exit1 (int) __dead2;
void	cpu_fork (struct proc *, struct proc *, int);
void	cpu_set_fork_handler (struct proc *, void (*)(void *), void *);
void	cpu_set_thread_handler(struct thread *td, void (*retfunc)(void), void *func, void *arg);
int	fork1 (struct lwp *, int, struct proc **);
void	start_forked_proc (struct lwp *, struct proc *);
int	trace_req (struct proc *);
void	cpu_proc_wait (struct proc *);
void	cpu_thread_wait (struct thread *);
int	cpu_coredump (struct thread *, struct vnode *, struct ucred *);
void	setsugid (void);
void	faultin (struct proc *p);
void	swapin_request (void);

u_int32_t	procrunnable (void);

#endif	/* _KERNEL */

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_PROC_H_ */
