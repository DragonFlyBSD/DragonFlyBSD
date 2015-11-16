/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_KINFO_H_
#define _SYS_KINFO_H_

#ifndef _KERNEL_STRUCTURES
#define _KERNEL_STRUCTURES
#endif

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#include <sys/resource.h>
#include <sys/rtprio.h>
#include <sys/proc.h>

struct kinfo_file {
	size_t	 f_size;	/* size of struct kinfo_file */
	pid_t	 f_pid;		/* owning process */
	uid_t	 f_uid;		/* effective uid of owning process */
	int	 f_fd;		/* descriptor number */
	void	*f_file;	/* address of struct file */
	short	 f_type;	/* descriptor type */
	int	 f_count;	/* reference count */
	int	 f_msgcount;	/* references from message queue */
	off_t	 f_offset;	/* file offset */
	void	*f_data;	/* file descriptor specific data */
	u_int	 f_flag;	/* flags (see fcntl.h) */
};

/*
 * CPU time statistics
 */
struct kinfo_cputime {
	uint64_t	cp_user;
	uint64_t	cp_nice;
	uint64_t	cp_sys;
	uint64_t	cp_intr;
	uint64_t	cp_idle;
	uint64_t	cp_unused01;
	uint64_t	cp_unused02;
	uint64_t	cp_unused03;
	uint64_t	cp_stallpc;	/* code stall address */
	char		cp_msg[32];	/* code stall token or mplock */
};

/*
 * CPU system/interrupt program counter sampler
 */
#define PCTRACK_ARYSIZE	32	/* must be a power of 2 */
#define PCTRACK_ARYMASK	(PCTRACK_ARYSIZE - 1)

struct kinfo_pcheader {
	int		pc_ntrack;	/* number of tracks per cpu (2) */
	int		pc_arysize;	/* size of storage array (32) */
};

struct kinfo_pctrack {
	int		pc_index;
	void		*pc_array[PCTRACK_ARYSIZE];
};

#define PCTRACK_SYS	0
#define PCTRACK_INT	1
#define PCTRACK_SIZE	2

struct kinfo_clockinfo {
	int	ci_hz;		/* clock frequency */
	int	ci_tick;	/* micro-seconds per hz tick */
	int	ci_tickadj;	/* clock skew rate for adjtime() */
	int	ci_stathz;	/* statistics clock frequency */
	int	ci_profhz;	/* profiling clock frequency */
};

/*
 * Structure definition for the lwp-specific data in struct kinfo_proc.
 */
struct kinfo_lwp {
	pid_t		kl_pid;		/* PID of our associated proc */
	lwpid_t		kl_tid;		/* thread id */

	int		kl_flags;	/* LWP_ flags */
	enum lwpstat	kl_stat;	/* LS* lwp status */
	int		kl_lock;	/* lwp lock (prevent destruct) count */
	int		kl_tdflags;	/* thread flags */
	int		kl_mpcount;	/* MP lock held count */
	int		kl_prio;	/* scheduling priority */
	int		kl_tdprio;	/* lwkt sched priority */
	struct rtprio	kl_rtprio;	/* real-time scheduling prio */

	/* accounting */
	uint64_t	kl_uticks;	/* time accounting */
	uint64_t	kl_sticks;
	uint64_t	kl_iticks;
	uint64_t	kl_cpticks;	/* sched quantums used */
	u_int		kl_pctcpu;	/* percentage cputime */
	u_int		kl_slptime;	/* time since last blocked */
	int		kl_origcpu;	/* originally scheduled on cpu */
	int		kl_estcpu;
	int		kl_cpuid;	/* CPU this lwp was last scheduled on */

	struct rusage	kl_ru;		/* resource usage stats */

	sigset_t	kl_siglist;	/* pending signals */
	sigset_t	kl_sigmask;	/* masked signals */
#define WMESGLEN 8
	uintptr_t	kl_wchan;	/* waiting channel */
	char		kl_wmesg[WMESGLEN+1];	/* waiting message */
};

/*
 * KERN_PROC subtype ops return arrays of normalized proc structures:
 */
struct kinfo_proc {
	uintptr_t	kp_paddr;	/* address of this proc */

	/* proc information */
	int		kp_flags;
	enum procstat	kp_stat;
	int		kp_lock;
	int		kp_acflag;	/* accounting flags */
	int		kp_traceflag;

	uintptr_t	kp_fd;		/* address of the proc's files */

	sigset_t	kp_siglist;
	sigset_t	kp_sigignore;
	sigset_t	kp_sigcatch;
	int		kp_sigflag;	/* from ps_flag */
	struct timeval	kp_start;

	char		kp_comm[MAXCOMLEN+1];

	/* cred information */
	uid_t		kp_uid;
	short		kp_ngroups;
	gid_t		kp_groups[NGROUPS];
	uid_t		kp_ruid;
	uid_t		kp_svuid;
	gid_t		kp_rgid;
	gid_t		kp_svgid;

	pid_t		kp_pid;	/* process id */
	pid_t		kp_ppid;	/* parent process id */
	pid_t		kp_pgid;	/* process group id */
	int		kp_jobc;	/* job control counter */
	pid_t		kp_sid;	/* session id */
	char		kp_login[roundup(MAXLOGNAME, sizeof(long))];	/* setlogin() name */
	dev_t		kp_tdev;	/* controlling tty dev */
	pid_t		kp_tpgid;	/* tty process group id */
	pid_t		kp_tsid;	/* tty session id */

	u_short		kp_exitstat;	/* exit status information */
	int		kp_nthreads;
	int		kp_nice;
	unsigned int	kp_swtime;

	vm_size_t	kp_vm_map_size;	/* vmmap virtual size in bytes */
	segsz_t		kp_vm_rssize;		/* resident set size in pages */
	segsz_t		kp_vm_swrss;		/* rss before last swap in pages */
	segsz_t		kp_vm_tsize;		/* text size in pages */
	segsz_t		kp_vm_dsize;		/* data size in pages */
	segsz_t		kp_vm_ssize;		/* stack size in pages */
        u_int		kp_vm_prssize;		/* proportional rss in pages */

	int		kp_jailid;

	struct rusage	kp_ru;
	struct rusage	kp_cru;

	int		kp_auxflags;	/* generated flags */
#define KI_CTTY	1
#define KI_SLEADER	2

	struct kinfo_lwp kp_lwp;

	uintptr_t	kp_ktaddr;	/* address of this kernel thread */
	int		kp_spare[2];
};

struct proc;
struct lwp;
struct thread;

void fill_kinfo_proc(struct proc *, struct kinfo_proc *);
void fill_kinfo_lwp(struct lwp *, struct kinfo_lwp *);
void fill_kinfo_proc_kthread(struct thread *, struct kinfo_proc *);

#define KINFO_NEXT(kp)	((union kinfo *)((uintptr_t)kp + kp->gen.len))
#define KINFO_END(kp)	(kp->gen.type == KINFO_TYPE_END)

#if defined(_KERNEL)
#define cpu_time	cputime_percpu[mycpuid]
#endif

#if defined(_KERNEL)
extern struct kinfo_cputime cputime_percpu[MAXCPU];
#endif

#endif /* !_SYS_KINFO_H_ */
