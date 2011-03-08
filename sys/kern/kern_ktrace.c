/*
 * Copyright (c) 1989, 1993
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
 *	@(#)kern_ktrace.c	8.2 (Berkeley) 9/23/93
 * $FreeBSD: src/sys/kern/kern_ktrace.c,v 1.35.2.6 2002/07/05 22:36:38 darrenr Exp $
 * $DragonFly: src/sys/kern/kern_ktrace.c,v 1.30 2008/04/14 12:01:50 dillon Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/ktrace.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/sysent.h>

#include <vm/vm_zone.h>

#include <sys/mplock2.h>

static MALLOC_DEFINE(M_KTRACE, "KTRACE", "KTRACE");

#ifdef KTRACE
static struct ktr_header *ktrgetheader (int type);
static void ktrwrite (struct lwp *, struct ktr_header *, struct uio *);
static int ktrcanset (struct thread *,struct proc *);
static int ktrsetchildren (struct thread *,struct proc *,int,int, ktrace_node_t);
static int ktrops (struct thread *,struct proc *,int,int, ktrace_node_t);

/*
 * MPSAFE
 */
static struct ktr_header *
ktrgetheader(int type)
{
	struct ktr_header *kth;
	struct proc *p = curproc;	/* XXX */
	struct lwp *lp = curthread->td_lwp;

	MALLOC(kth, struct ktr_header *, sizeof (struct ktr_header),
		M_KTRACE, M_WAITOK);
	kth->ktr_type = type;
	/* XXX threaded flag is a hack at the moment */
	kth->ktr_flags = (p->p_nthreads > 1) ? KTRH_THREADED : 0;
	microtime(&kth->ktr_time);
	kth->ktr_pid = p->p_pid;
	kth->ktr_tid = lp->lwp_tid;
	bcopy(p->p_comm, kth->ktr_comm, MAXCOMLEN + 1);
	return (kth);
}

void
ktrsyscall(struct lwp *lp, int code, int narg, register_t args[])
{
	struct	ktr_header *kth;
	struct	ktr_syscall *ktp;
	int len;
	register_t *argp;
	int i;

	len = offsetof(struct ktr_syscall, ktr_args) +
	      (narg * sizeof(register_t));

	/*
	 * Setting the active bit prevents a ktrace recursion from the
	 * ktracing op itself.
	 */
	lp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_SYSCALL);
	MALLOC(ktp, struct ktr_syscall *, len, M_KTRACE, M_WAITOK);
	ktp->ktr_code = code;
	ktp->ktr_narg = narg;
	argp = &ktp->ktr_args[0];
	for (i = 0; i < narg; i++)
		*argp++ = args[i];
	kth->ktr_buf = (caddr_t)ktp;
	kth->ktr_len = len;
	ktrwrite(lp, kth, NULL);
	FREE(ktp, M_KTRACE);
	FREE(kth, M_KTRACE);
	lp->lwp_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrsysret(struct lwp *lp, int code, int error, register_t retval)
{
	struct ktr_header *kth;
	struct ktr_sysret ktp;

	lp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_SYSRET);
	ktp.ktr_code = code;
	ktp.ktr_error = error;
	ktp.ktr_retval = retval;		/* what about val2 ? */

	kth->ktr_buf = (caddr_t)&ktp;
	kth->ktr_len = sizeof(struct ktr_sysret);

	ktrwrite(lp, kth, NULL);
	FREE(kth, M_KTRACE);
	lp->lwp_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrnamei(struct lwp *lp, char *path)
{
	struct ktr_header *kth;

	lp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_NAMEI);
	kth->ktr_len = strlen(path);
	kth->ktr_buf = path;

	ktrwrite(lp, kth, NULL);
	FREE(kth, M_KTRACE);
	lp->lwp_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrgenio(struct lwp *lp, int fd, enum uio_rw rw, struct uio *uio, int error)
{
	struct ktr_header *kth;
	struct ktr_genio ktg;

	if (error)
		return;
	lp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_GENIO);
	ktg.ktr_fd = fd;
	ktg.ktr_rw = rw;
	kth->ktr_buf = (caddr_t)&ktg;
	kth->ktr_len = sizeof(struct ktr_genio);
	uio->uio_offset = 0;
	uio->uio_rw = UIO_WRITE;

	ktrwrite(lp, kth, uio);
	FREE(kth, M_KTRACE);
	lp->lwp_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrpsig(struct lwp *lp, int sig, sig_t action, sigset_t *mask, int code)
{
	struct ktr_header *kth;
	struct ktr_psig	kp;

	lp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_PSIG);
	kp.signo = (char)sig;
	kp.action = action;
	kp.mask = *mask;
	kp.code = code;
	kth->ktr_buf = (caddr_t)&kp;
	kth->ktr_len = sizeof (struct ktr_psig);

	ktrwrite(lp, kth, NULL);
	FREE(kth, M_KTRACE);
	lp->lwp_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrcsw(struct lwp *lp, int out, int user)
{
	struct ktr_header *kth;
	struct	ktr_csw kc;

	lp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_CSW);
	kc.out = out;
	kc.user = user;
	kth->ktr_buf = (caddr_t)&kc;
	kth->ktr_len = sizeof (struct ktr_csw);

	ktrwrite(lp, kth, NULL);
	FREE(kth, M_KTRACE);
	lp->lwp_traceflag &= ~KTRFAC_ACTIVE;
}
#endif

/* Interface and common routines */

#ifdef KTRACE
/*
 * ktrace system call
 */
struct ktrace_clear_info {
	ktrace_node_t tracenode;
	int rootclear;
	int error;
};

static int ktrace_clear_callback(struct proc *p, void *data);

#endif

/*
 * MPALMOSTSAFE
 */
int
sys_ktrace(struct ktrace_args *uap)
{
#ifdef KTRACE
	struct ktrace_clear_info info;
	struct thread *td = curthread;
	struct proc *curp = td->td_proc;
	struct proc *p;
	struct pgrp *pg;
	int facs = uap->facs & ~KTRFAC_ROOT;
	int ops = KTROP(uap->ops);
	int descend = uap->ops & KTRFLAG_DESCEND;
	int ret = 0;
	int error = 0;
	struct nlookupdata nd;
	ktrace_node_t tracenode = NULL;

	get_mplock();
	curp->p_traceflag |= KTRFAC_ACTIVE;
	if (ops != KTROP_CLEAR) {
		/*
		 * an operation which requires a file argument.
		 */
		error = nlookup_init(&nd, uap->fname, 
					UIO_USERSPACE, NLC_LOCKVP);
		if (error == 0)
			error = vn_open(&nd, NULL, FREAD|FWRITE|O_NOFOLLOW, 0);
		if (error == 0 && nd.nl_open_vp->v_type != VREG)
			error = EACCES;
		if (error) {
			curp->p_traceflag &= ~KTRFAC_ACTIVE;
			nlookup_done(&nd);
			goto done;
		}
		MALLOC(tracenode, ktrace_node_t, sizeof (struct ktrace_node),
		       M_KTRACE, M_WAITOK | M_ZERO);
		tracenode->kn_vp = nd.nl_open_vp;
		tracenode->kn_refs = 1;
		nd.nl_open_vp = NULL;
		nlookup_done(&nd);
		vn_unlock(tracenode->kn_vp);
	}
	/*
	 * Clear all uses of the tracefile.  Not the most efficient operation
	 * in the world.
	 */
	if (ops == KTROP_CLEARFILE) {
		info.tracenode = tracenode;
		info.error = 0;
		info.rootclear = 0;
		allproc_scan(ktrace_clear_callback, &info);
		error = info.error;
		goto done;
	}
	/*
	 * need something to (un)trace (XXX - why is this here?)
	 */
	if (!facs) {
		error = EINVAL;
		goto done;
	}
	/*
	 * do it
	 */
	if (uap->pid < 0) {
		/*
		 * By process group.  Process group is referenced, preventing
		 * disposal.
		 */
		pg = pgfind(-uap->pid);
		if (pg == NULL) {
			error = ESRCH;
			goto done;
		}
		lwkt_gettoken(&pg->pg_token);
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			PHOLD(p);
			if (descend)
				ret |= ktrsetchildren(td, p, ops, facs, tracenode);
			else
				ret |= ktrops(td, p, ops, facs, tracenode);
			PRELE(p);
		}
		lwkt_reltoken(&pg->pg_token);
		pgrel(pg);
	} else {
		/*
		 * by pid
		 */
		p = pfind(uap->pid);
		if (p == NULL) {
			error = ESRCH;
			goto done;
		}
		if (descend)
			ret |= ktrsetchildren(td, p, ops, facs, tracenode);
		else
			ret |= ktrops(td, p, ops, facs, tracenode);
		PRELE(p);
	}
	if (!ret)
		error = EPERM;
done:
	if (tracenode)
		ktrdestroy(&tracenode);
	curp->p_traceflag &= ~KTRFAC_ACTIVE;
	rel_mplock();
	return (error);
#else
	return ENOSYS;
#endif
}

#ifdef KTRACE

/*
 * NOTE: NOT MPSAFE (yet)
 */
static int
ktrace_clear_callback(struct proc *p, void *data)
{
	struct ktrace_clear_info *info = data;

	if (p->p_tracenode) {
		if (info->rootclear) {
			if (p->p_tracenode == info->tracenode) {
				ktrdestroy(&p->p_tracenode);
				p->p_traceflag = 0;
			}
		} else {
			if (p->p_tracenode->kn_vp == info->tracenode->kn_vp) {
				if (ktrcanset(curthread, p)) {
					ktrdestroy(&p->p_tracenode);
					p->p_traceflag = 0;
				} else {
					info->error = EPERM;
				}
			}
		}
	}
	return(0);
}

#endif

/*
 * utrace system call
 *
 * MPALMOSTSAFE
 */
int
sys_utrace(struct utrace_args *uap)
{
#ifdef KTRACE
	struct ktr_header *kth;
	struct thread *td = curthread;	/* XXX */
	caddr_t cp;

	if (!KTRPOINT(td, KTR_USER))
		return (0);
	if (uap->len > KTR_USER_MAXLEN)
		return (EINVAL);
	td->td_lwp->lwp_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_USER);
	MALLOC(cp, caddr_t, uap->len, M_KTRACE, M_WAITOK);
	if (!copyin(uap->addr, cp, uap->len)) {
		kth->ktr_buf = cp;
		kth->ktr_len = uap->len;
		ktrwrite(td->td_lwp, kth, NULL);
	}
	FREE(kth, M_KTRACE);
	FREE(cp, M_KTRACE);
	td->td_lwp->lwp_traceflag &= ~KTRFAC_ACTIVE;

	return (0);
#else
	return (ENOSYS);
#endif
}

void
ktrdestroy(struct ktrace_node **tracenodep)
{
	ktrace_node_t tracenode;

	if ((tracenode = *tracenodep) != NULL) {
		*tracenodep = NULL;
		KKASSERT(tracenode->kn_refs > 0);
		if (atomic_fetchadd_int(&tracenode->kn_refs, -1) == 1) {
			vn_close(tracenode->kn_vp, FREAD|FWRITE);
			tracenode->kn_vp = NULL;
			FREE(tracenode, M_KTRACE);
		}
	}
}

/*
 * This allows a process to inherit a ref on a tracenode and is also used
 * as a temporary ref to prevent a tracenode from being destroyed out from
 * under an active operation.
 */
ktrace_node_t
ktrinherit(ktrace_node_t tracenode)
{
	if (tracenode) {
		KKASSERT(tracenode->kn_refs > 0);
		atomic_add_int(&tracenode->kn_refs, 1);
	}
	return(tracenode);
}

#ifdef KTRACE
static int
ktrops(struct thread *td, struct proc *p, int ops, int facs,
       ktrace_node_t tracenode)
{
	ktrace_node_t oldnode;

	if (!ktrcanset(td, p))
		return (0);
	if (ops == KTROP_SET) {
		if ((oldnode = p->p_tracenode) != tracenode) {
			p->p_tracenode = ktrinherit(tracenode);
			ktrdestroy(&oldnode);
		}
		p->p_traceflag |= facs;
		if (td->td_ucred->cr_uid == 0)
			p->p_traceflag |= KTRFAC_ROOT;
	} else {
		/* KTROP_CLEAR */
		if (((p->p_traceflag &= ~facs) & KTRFAC_MASK) == 0) {
			/* no more tracing */
			p->p_traceflag = 0;
			ktrdestroy(&p->p_tracenode);
		}
	}

	return (1);
}

static int
ktrsetchildren(struct thread *td, struct proc *top, int ops, int facs,
	       ktrace_node_t tracenode)
{
	struct proc *p;
	struct proc *np;
	int ret = 0;

	p = top;
	PHOLD(p);
	lwkt_gettoken(&p->p_token);

	for (;;) {
		ret |= ktrops(td, p, ops, facs, tracenode);

		/*
		 * If this process has children, descend to them next,
		 * otherwise do any siblings, and if done with this level,
		 * follow back up the tree (but not past top).
		 */
		if ((np = LIST_FIRST(&p->p_children)) != NULL) {
			PHOLD(np);
		}
		while (np == NULL) {
			if (p == top)
				break;
			if ((np = LIST_NEXT(p, p_sibling)) != NULL) {
				PHOLD(np);
				break;
			}

			/*
			 * recurse up to parent, set p in our inner
			 * loop when doing this.  np can be NULL if
			 * we race a reparenting to init (thus 'top'
			 * is skipped past and never encountered).
			 */
			np = p->p_pptr;
			if (np == NULL)
				break;
			PHOLD(np);
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			p = np;
			lwkt_gettoken(&p->p_token);
			np = NULL;
		}
		lwkt_reltoken(&p->p_token);
		PRELE(p);
		p = np;
		if (p == NULL)
			break;
		/* Already held, but we need the token too */
		lwkt_gettoken(&p->p_token);
	}
	return (ret);
}

static void
ktrwrite(struct lwp *lp, struct ktr_header *kth, struct uio *uio)
{
	struct ktrace_clear_info info;
	struct uio auio;
	struct iovec aiov[2];
	int error;
	ktrace_node_t tracenode;

	/*
	 * We have to ref our tracenode to prevent it from being ripped out
	 * from under us while we are trying to use it.   p_tracenode can
	 * go away at any time if another process gets a write error.
	 *
	 * XXX not MP safe
	 */
	if (lp->lwp_proc->p_tracenode == NULL)
		return;
	tracenode = ktrinherit(lp->lwp_proc->p_tracenode);
	auio.uio_iov = &aiov[0];
	auio.uio_offset = 0;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	aiov[0].iov_base = (caddr_t)kth;
	aiov[0].iov_len = sizeof(struct ktr_header);
	auio.uio_resid = sizeof(struct ktr_header);
	auio.uio_iovcnt = 1;
	auio.uio_td = curthread;
	if (kth->ktr_len > 0) {
		auio.uio_iovcnt++;
		aiov[1].iov_base = kth->ktr_buf;
		aiov[1].iov_len = kth->ktr_len;
		auio.uio_resid += kth->ktr_len;
		if (uio != NULL)
			kth->ktr_len += uio->uio_resid;
	}
	vn_lock(tracenode->kn_vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_WRITE(tracenode->kn_vp, &auio,
			  IO_UNIT | IO_APPEND, lp->lwp_thread->td_ucred);
	if (error == 0 && uio != NULL) {
		error = VOP_WRITE(tracenode->kn_vp, uio,
			      IO_UNIT | IO_APPEND, lp->lwp_thread->td_ucred);
	}
	vn_unlock(tracenode->kn_vp);
	if (error) {
		/*
		 * If an error occured, give up tracing on all processes
		 * using this tracenode.  This is not MP safe but is
		 * blocking-safe.
		 */
		log(LOG_NOTICE,
		    "ktrace write failed, errno %d, tracing stopped\n", error);
		info.tracenode = tracenode;
		info.error = 0;
		info.rootclear = 1;
		allproc_scan(ktrace_clear_callback, &info);
	}
	ktrdestroy(&tracenode);
}

/*
 * Return true if caller has permission to set the ktracing state
 * of target.  Essentially, the target can't possess any
 * more permissions than the caller.  KTRFAC_ROOT signifies that
 * root previously set the tracing status on the target process, and
 * so, only root may further change it.
 *
 * TODO: check groups.  use caller effective gid.
 */
static int
ktrcanset(struct thread *calltd, struct proc *targetp)
{
	struct ucred *caller = calltd->td_ucred;
	struct ucred *target = targetp->p_ucred;

	if (!PRISON_CHECK(caller, target))
		return (0);
	if ((caller->cr_uid == target->cr_ruid &&
	     target->cr_ruid == target->cr_svuid &&
	     caller->cr_rgid == target->cr_rgid &&	/* XXX */
	     target->cr_rgid == target->cr_svgid &&
	     (targetp->p_traceflag & KTRFAC_ROOT) == 0 &&
	     (targetp->p_flag & P_SUGID) == 0) ||
	     caller->cr_uid == 0)
		return (1);

	return (0);
}

#endif /* KTRACE */
