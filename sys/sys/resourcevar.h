/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)resourcevar.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/resourcevar.h,v 1.16.2.1 2000/09/07 19:13:55 truckman Exp $
 * $DragonFly: src/sys/sys/resourcevar.h,v 1.16 2008/05/08 01:26:01 dillon Exp $
 */

#ifndef	_SYS_RESOURCEVAR_H_
#define	_SYS_RESOURCEVAR_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_RESOURCE_H_
#include <sys/resource.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_VARSYM_H_
#include <sys/varsym.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

struct uprof {			/* profile arguments */
	caddr_t	pr_base;	/* buffer base */
	u_long	pr_size;	/* buffer size */
	u_long	pr_off;		/* pc offset */
	u_long	pr_scale;	/* pc scaling */
	u_long	pr_addr;	/* temp storage for addr until AST */
	u_long	pr_ticks;	/* temp storage for ticks until AST */
};

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * For krateprintf()
 */
struct krate {
	int freq;
	int ticks;
	int count;
};

/*
 * Kernel shareable process resource limits.  Because this structure
 * is moderately large but changes infrequently, it is normally
 * shared copy-on-write after forks.
 *
 * Threaded programs force p_exclusive (set it to 2) to prevent the
 * proc->p_limit pointer from changing out from under threaded access.
 */
struct plimit {
	struct	rlimit pl_rlimit[RLIM_NLIMITS];
	int	p_refcnt;		/* number of references */
	int	p_exclusive;		/* exclusive to proc due to lwp's */
	rlim_t	p_cpulimit;		/* current cpu limit in usec */
	struct spinlock p_spin;
};

#define PLIMIT_TESTCPU_OK	0
#define PLIMIT_TESTCPU_XCPU	1
#define PLIMIT_TESTCPU_KILL	2

/*
 * Per uid resource consumption
 */
struct uidinfo {
	/*
	 * Protects access to ui_sbsize, ui_proccnt, ui_posixlocks
	 */
	struct spinlock ui_lock;
	LIST_ENTRY(uidinfo) ui_hash;
	rlim_t	ui_sbsize;		/* socket buffer space consumed */
	long	ui_proccnt;		/* number of processes */
	uid_t	ui_uid;			/* uid */
	int	ui_ref;			/* reference count */
	int	ui_posixlocks;		/* number of POSIX locks */
	int	ui_openfiles;		/* number of open files */
	struct varsymset ui_varsymset;	/* variant symlinks */
};

#endif

#ifdef _KERNEL

struct proc;
struct lwp;

void	addupc_intr (struct proc *p, u_long pc, u_int ticks);
void	addupc_task (struct proc *p, u_long pc, u_int ticks);
void	calcru (struct lwp *lp, struct timeval *up, struct timeval *sp);
void	calcru_proc (struct proc *p, struct rusage *ru);
int	chgproccnt (struct uidinfo *uip, int diff, int max);
int	chgsbsize (struct uidinfo *uip, u_long *hiwat, u_long to, rlim_t max);
void	ruadd (struct rusage *ru, struct rusage *ru2);
struct uidinfo *uifind (uid_t uid);
void	uihold (struct uidinfo *uip);
void	uidrop (struct uidinfo *uip);
void	uireplace (struct uidinfo **puip, struct uidinfo *nuip);
void	uihashinit (void);

void plimit_init0(struct plimit *);
struct plimit *plimit_fork(struct proc *);
void plimit_lwp_fork(struct proc *);
int plimit_testcpulimit(struct plimit *, u_int64_t);
void plimit_modify(struct proc *, int, struct rlimit *);
void plimit_free(struct proc *);

#endif

#endif	/* !_SYS_RESOURCEVAR_H_ */
