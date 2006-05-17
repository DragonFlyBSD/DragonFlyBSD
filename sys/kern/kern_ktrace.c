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
 * $DragonFly: src/sys/kern/kern_ktrace.c,v 1.24 2006/05/17 20:20:49 dillon Exp $
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
static MALLOC_DEFINE(M_KTRACE, "KTRACE", "KTRACE");

#ifdef KTRACE
static struct ktr_header *ktrgetheader (int type);
static void ktrwrite (struct proc *, struct ktr_header *, struct uio *);
static int ktrcanset (struct proc *,struct proc *);
static int ktrsetchildren (struct proc *,struct proc *,int,int, ktrace_node_t);
static int ktrops (struct proc *,struct proc *,int,int, ktrace_node_t);

static struct ktr_header *
ktrgetheader(int type)
{
	struct ktr_header *kth;
	struct proc *p = curproc;	/* XXX */

	MALLOC(kth, struct ktr_header *, sizeof (struct ktr_header),
		M_KTRACE, M_WAITOK);
	kth->ktr_type = type;
	microtime(&kth->ktr_time);
	kth->ktr_pid = p->p_pid;
	bcopy(p->p_comm, kth->ktr_comm, MAXCOMLEN + 1);
	return (kth);
}

void
ktrsyscall(struct proc *p, int code, int narg, register_t args[])
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
	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_SYSCALL);
	MALLOC(ktp, struct ktr_syscall *, len, M_KTRACE, M_WAITOK);
	ktp->ktr_code = code;
	ktp->ktr_narg = narg;
	argp = &ktp->ktr_args[0];
	for (i = 0; i < narg; i++)
		*argp++ = args[i];
	kth->ktr_buf = (caddr_t)ktp;
	kth->ktr_len = len;
	ktrwrite(p, kth, NULL);
	FREE(ktp, M_KTRACE);
	FREE(kth, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrsysret(struct proc *p, int code, int error, register_t retval)
{
	struct ktr_header *kth;
	struct ktr_sysret ktp;

	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_SYSRET);
	ktp.ktr_code = code;
	ktp.ktr_error = error;
	ktp.ktr_retval = retval;		/* what about val2 ? */

	kth->ktr_buf = (caddr_t)&ktp;
	kth->ktr_len = sizeof(struct ktr_sysret);

	ktrwrite(p, kth, NULL);
	FREE(kth, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrnamei(struct proc *p, char *path)
{
	struct ktr_header *kth;

	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_NAMEI);
	kth->ktr_len = strlen(path);
	kth->ktr_buf = path;

	ktrwrite(p, kth, NULL);
	FREE(kth, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrgenio(struct proc *p, int fd, enum uio_rw rw, struct uio *uio, int error)
{
	struct ktr_header *kth;
	struct ktr_genio ktg;

	if (error)
		return;
	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_GENIO);
	ktg.ktr_fd = fd;
	ktg.ktr_rw = rw;
	kth->ktr_buf = (caddr_t)&ktg;
	kth->ktr_len = sizeof(struct ktr_genio);
	uio->uio_offset = 0;
	uio->uio_rw = UIO_WRITE;

	ktrwrite(p, kth, uio);
	FREE(kth, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrpsig(struct proc *p, int sig, sig_t action, sigset_t *mask, int code)
{
	struct ktr_header *kth;
	struct ktr_psig	kp;

	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_PSIG);
	kp.signo = (char)sig;
	kp.action = action;
	kp.mask = *mask;
	kp.code = code;
	kth->ktr_buf = (caddr_t)&kp;
	kth->ktr_len = sizeof (struct ktr_psig);

	ktrwrite(p, kth, NULL);
	FREE(kth, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;
}

void
ktrcsw(struct proc *p, int out, int user)
{
	struct ktr_header *kth;
	struct	ktr_csw kc;

	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_CSW);
	kc.out = out;
	kc.user = user;
	kth->ktr_buf = (caddr_t)&kc;
	kth->ktr_len = sizeof (struct ktr_csw);

	ktrwrite(p, kth, NULL);
	FREE(kth, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;
}
#endif

/* Interface and common routines */

/*
 * ktrace system call
 */
/* ARGSUSED */
int
ktrace(struct ktrace_args *uap)
{
#ifdef KTRACE
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
			return (error);
		}
		MALLOC(tracenode, ktrace_node_t, sizeof (struct ktrace_node),
		       M_KTRACE, M_WAITOK | M_ZERO);
		tracenode->kn_vp = nd.nl_open_vp;
		tracenode->kn_refs = 1;
		nd.nl_open_vp = NULL;
		nlookup_done(&nd);
		VOP_UNLOCK(tracenode->kn_vp, 0);
	}
	/*
	 * Clear all uses of the tracefile.  Not the most efficient operation
	 * in the world.
	 */
	if (ops == KTROP_CLEARFILE) {
again:
		FOREACH_PROC_IN_SYSTEM(p) {
			if (p->p_tracenode->kn_vp == tracenode->kn_vp) {
				if (ktrcanset(curp, p)) {
					ktrdestroy(&p->p_tracenode);
					p->p_traceflag = 0;
					goto again;
				} else {
					error = EPERM;
				}
			}
		}
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
		 * by process group
		 */
		pg = pgfind(-uap->pid);
		if (pg == NULL) {
			error = ESRCH;
			goto done;
		}
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			if (descend)
				ret |= ktrsetchildren(curp, p, ops, facs, tracenode);
			else
				ret |= ktrops(curp, p, ops, facs, tracenode);
		}
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
			ret |= ktrsetchildren(curp, p, ops, facs, tracenode);
		else
			ret |= ktrops(curp, p, ops, facs, tracenode);
	}
	if (!ret)
		error = EPERM;
done:
	if (tracenode)
		ktrdestroy(&tracenode);
	curp->p_traceflag &= ~KTRFAC_ACTIVE;
	return (error);
#else
	return ENOSYS;
#endif
}

/*
 * utrace system call
 */
/* ARGSUSED */
int
utrace(struct utrace_args *uap)
{
#ifdef KTRACE
	struct ktr_header *kth;
	struct thread *td = curthread;	/* XXX */
	struct proc *p = td->td_proc;
	caddr_t cp;

	if (!KTRPOINT(td, KTR_USER))
		return (0);
	if (uap->len > KTR_USER_MAXLEN)
		return (EINVAL);
	p->p_traceflag |= KTRFAC_ACTIVE;
	kth = ktrgetheader(KTR_USER);
	MALLOC(cp, caddr_t, uap->len, M_KTRACE, M_WAITOK);
	if (!copyin(uap->addr, cp, uap->len)) {
		kth->ktr_buf = cp;
		kth->ktr_len = uap->len;
		ktrwrite(p, kth, NULL);
	}
	FREE(kth, M_KTRACE);
	FREE(cp, M_KTRACE);
	p->p_traceflag &= ~KTRFAC_ACTIVE;

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
		/* XXX not MP safe yet */
		--tracenode->kn_refs;
		if (tracenode->kn_refs == 0) {
			printf("DESTROY %p\n", tracenode);
			vn_close(tracenode->kn_vp, FREAD|FWRITE);
			tracenode->kn_vp = NULL;
			FREE(tracenode, M_KTRACE);
		}
	}
}

ktrace_node_t
ktrinherit(ktrace_node_t tracenode)
{
	KKASSERT(tracenode->kn_refs > 0);
	++tracenode->kn_refs;
	return(tracenode);
}

#ifdef KTRACE
static int
ktrops(struct proc *curp, struct proc *p, int ops, int facs,
       ktrace_node_t tracenode)
{
	ktrace_node_t oldnode;

	if (!ktrcanset(curp, p))
		return (0);
	if (ops == KTROP_SET) {
		if ((oldnode = p->p_tracenode) != NULL) {
			p->p_tracenode = ktrinherit(tracenode);
			ktrdestroy(&oldnode);
		}
		p->p_traceflag |= facs;
		if (curp->p_ucred->cr_uid == 0)
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
ktrsetchildren(struct proc *curp, struct proc *top, int ops, int facs,
	       ktrace_node_t tracenode)
{
	struct proc *p;
	int ret = 0;

	p = top;
	for (;;) {
		ret |= ktrops(curp, p, ops, facs, tracenode);
		/*
		 * If this process has children, descend to them next,
		 * otherwise do any siblings, and if done with this level,
		 * follow back up the tree (but not past top).
		 */
		if (!LIST_EMPTY(&p->p_children))
			p = LIST_FIRST(&p->p_children);
		else for (;;) {
			if (p == top)
				return (ret);
			if (LIST_NEXT(p, p_sibling)) {
				p = LIST_NEXT(p, p_sibling);
				break;
			}
			p = p->p_pptr;
		}
	}
	/*NOTREACHED*/
}

static void
ktrwrite(struct proc *p, struct ktr_header *kth, struct uio *uio)
{
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
	if (p->p_tracenode == NULL)
		return;
	tracenode = ktrinherit(p->p_tracenode);
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
			  IO_UNIT | IO_APPEND, p->p_ucred);
	if (error == 0 && uio != NULL) {
		error = VOP_WRITE(tracenode->kn_vp, uio,
			  	  IO_UNIT | IO_APPEND, p->p_ucred);
	}
	VOP_UNLOCK(tracenode->kn_vp, 0);
	if (!error)
		return;

	/*
	 * If an error occured, give up tracing on all processes using this
	 * tracenode.  This is not MP safe but is blocking-safe.
	 */
	log(LOG_NOTICE, "ktrace write failed, errno %d, tracing stopped\n",
	    error);
retry:
	FOREACH_PROC_IN_SYSTEM(p) {
		if (p->p_tracenode == tracenode) {
			ktrdestroy(&p->p_tracenode);
			p->p_traceflag = 0;
			goto retry;
		}
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
ktrcanset(struct proc *callp, struct proc *targetp)
{
	struct ucred *caller = callp->p_ucred;
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
