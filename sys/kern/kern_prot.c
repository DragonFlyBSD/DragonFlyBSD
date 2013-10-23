/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1991, 1993
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
 *	@(#)kern_prot.c	8.6 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_prot.c,v 1.53.2.9 2002/03/09 05:20:26 dd Exp $
 */

/*
 * System calls related to processes and protection
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/acct.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/malloc.h>
#include <sys/pioctl.h>
#include <sys/resourcevar.h>
#include <sys/jail.h>
#include <sys/lockf.h>
#include <sys/spinlock.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

static MALLOC_DEFINE(M_CRED, "cred", "credentials");

int
sys_getpid(struct getpid_args *uap)
{
	struct proc *p = curproc;

	uap->sysmsg_fds[0] = p->p_pid;
#if defined(COMPAT_43)
	lwkt_gettoken_shared(&p->p_token);
	uap->sysmsg_fds[1] = p->p_pptr->p_pid;
	lwkt_reltoken(&p->p_token);
#endif
	return (0);
}

int
sys_getppid(struct getppid_args *uap)
{
	struct proc *p = curproc;

	lwkt_gettoken_shared(&p->p_token);
	uap->sysmsg_result = p->p_pptr->p_pid;
	lwkt_reltoken(&p->p_token);

	return (0);
}

int
sys_lwp_gettid(struct lwp_gettid_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	uap->sysmsg_result = lp->lwp_tid;
	return (0);
}

/* 
 * Get process group ID; note that POSIX getpgrp takes no parameter 
 */
int
sys_getpgrp(struct getpgrp_args *uap)
{
	struct proc *p = curproc;

	lwkt_gettoken_shared(&p->p_token);
	uap->sysmsg_result = p->p_pgrp->pg_id;
	lwkt_reltoken(&p->p_token);

	return (0);
}

/*
 * Get an arbitrary pid's process group id 
 */
int
sys_getpgid(struct getpgid_args *uap)
{
	struct proc *p = curproc;
	struct proc *pt;
	int error;

	error = 0;

	if (uap->pid == 0) {
		pt = p;
		PHOLD(pt);
	} else {
		pt = pfind(uap->pid);
		if (pt == NULL)
			error = ESRCH;
	}
	if (error == 0) {
		lwkt_gettoken_shared(&pt->p_token);
		uap->sysmsg_result = pt->p_pgrp->pg_id;
		lwkt_reltoken(&pt->p_token);
	}
	if (pt)
		PRELE(pt);
	return (error);
}

/*
 * Get an arbitrary pid's session id.
 */
int
sys_getsid(struct getsid_args *uap)
{
	struct proc *p = curproc;
	struct proc *pt;
	int error;

	error = 0;

	if (uap->pid == 0) {
		pt = p;
		PHOLD(pt);
	} else {
		pt = pfind(uap->pid);
		if (pt == NULL)
			error = ESRCH;
	}
	if (error == 0)
		uap->sysmsg_result = pt->p_session->s_sid;
	if (pt)
		PRELE(pt);
	return (error);
}


/*
 * getuid()
 */
int
sys_getuid(struct getuid_args *uap)
{
	struct ucred *cred = curthread->td_ucred;

	uap->sysmsg_fds[0] = cred->cr_ruid;
#if defined(COMPAT_43)
	uap->sysmsg_fds[1] = cred->cr_uid;
#endif
	return (0);
}

/*
 * geteuid()
 */
int
sys_geteuid(struct geteuid_args *uap)
{
	struct ucred *cred = curthread->td_ucred;

	uap->sysmsg_result = cred->cr_uid;
	return (0);
}

/*
 * getgid()
 */
int
sys_getgid(struct getgid_args *uap)
{
	struct ucred *cred = curthread->td_ucred;

	uap->sysmsg_fds[0] = cred->cr_rgid;
#if defined(COMPAT_43)
	uap->sysmsg_fds[1] = cred->cr_groups[0];
#endif
	return (0);
}

/*
 * Get effective group ID.  The "egid" is groups[0], and could be obtained
 * via getgroups.  This syscall exists because it is somewhat painful to do
 * correctly in a library function.
 */
int
sys_getegid(struct getegid_args *uap)
{
	struct ucred *cred = curthread->td_ucred;

	uap->sysmsg_result = cred->cr_groups[0];
	return (0);
}

int
sys_getgroups(struct getgroups_args *uap)
{
	struct ucred *cr;
	u_int ngrp;
	int error;

	cr = curthread->td_ucred;
	if ((ngrp = uap->gidsetsize) == 0) {
		uap->sysmsg_result = cr->cr_ngroups;
		return (0);
	}
	if (ngrp < cr->cr_ngroups)
		return (EINVAL);
	ngrp = cr->cr_ngroups;
	error = copyout((caddr_t)cr->cr_groups,
			(caddr_t)uap->gidset, ngrp * sizeof(gid_t));
	if (error == 0)
		uap->sysmsg_result = ngrp;
	return (error);
}

int
sys_setsid(struct setsid_args *uap)
{
	struct proc *p = curproc;
	struct pgrp *pg = NULL;
	int error;

	lwkt_gettoken(&p->p_token);
	if (p->p_pgid == p->p_pid || (pg = pgfind(p->p_pid)) != NULL) {
		error = EPERM;
		if (pg)
			pgrel(pg);
	} else {
		enterpgrp(p, p->p_pid, 1);
		uap->sysmsg_result = p->p_pid;
		error = 0;
	}
	lwkt_reltoken(&p->p_token);
	return (error);
}

/*
 * set process group (setpgid/old setpgrp)
 *
 * caller does setpgid(targpid, targpgid)
 *
 * pid must be caller or child of caller (ESRCH)
 * if a child
 *	pid must be in same session (EPERM)
 *	pid can't have done an exec (EACCES)
 * if pgid != pid
 * 	there must exist some pid in same session having pgid (EPERM)
 * pid must not be session leader (EPERM)
 */
int
sys_setpgid(struct setpgid_args *uap)
{
	struct proc *curp = curproc;
	struct proc *targp;		/* target process */
	struct pgrp *pgrp = NULL;	/* target pgrp */
	int error;

	if (uap->pgid < 0)
		return (EINVAL);

	if (uap->pid != 0 && uap->pid != curp->p_pid) {
		if ((targp = pfind(uap->pid)) == NULL || !inferior(targp)) {
			if (targp)
				PRELE(targp);
			error = ESRCH;
			targp = NULL;
			goto done;
		}
		lwkt_gettoken(&targp->p_token);
		/* targp now referenced and its token is held */

		if (targp->p_pgrp == NULL ||
		    targp->p_session != curp->p_session) {
			error = EPERM;
			goto done;
		}
		if (targp->p_flags & P_EXEC) {
			error = EACCES;
			goto done;
		}
	} else {
		targp = curp;
		PHOLD(targp);
		lwkt_gettoken(&targp->p_token);
	}
	if (SESS_LEADER(targp)) {
		error = EPERM;
		goto done;
	}
	if (uap->pgid == 0) {
		uap->pgid = targp->p_pid;
	} else if (uap->pgid != targp->p_pid) {
		if ((pgrp = pgfind(uap->pgid)) == NULL ||
	            pgrp->pg_session != curp->p_session) {
			error = EPERM;
			goto done;
		}
	}
	error = enterpgrp(targp, uap->pgid, 0);
done:
	if (pgrp)
		pgrel(pgrp);
	if (targp) {
		lwkt_reltoken(&targp->p_token);
		PRELE(targp);
	}
	return (error);
}

/*
 * Use the clause in B.4.2.2 that allows setuid/setgid to be 4.2/4.3BSD
 * compatible.  It says that setting the uid/gid to euid/egid is a special
 * case of "appropriate privilege".  Once the rules are expanded out, this
 * basically means that setuid(nnn) sets all three id's, in all permitted
 * cases unless _POSIX_SAVED_IDS is enabled.  In that case, setuid(getuid())
 * does not set the saved id - this is dangerous for traditional BSD
 * programs.  For this reason, we *really* do not want to set
 * _POSIX_SAVED_IDS and do not want to clear POSIX_APPENDIX_B_4_2_2.
 */
#define POSIX_APPENDIX_B_4_2_2

int
sys_setuid(struct setuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t uid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setuid(geteuid()) is a special case of 
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatible with traditional BSD
	 * semantics.  Basically, it means that "setuid(xx)" sets all
	 * three id's (assuming you have privs).
	 *
	 * Notes on the logic.  We do things in three steps.
	 * 1: We determine if the euid is going to change, and do EPERM
	 *    right away.  We unconditionally change the euid later if this
	 *    test is satisfied, simplifying that part of the logic.
	 * 2: We determine if the real and/or saved uid's are going to
	 *    change.  Determined by compile options.
	 * 3: Change euid last. (after tests in #2 for "appropriate privs")
	 */
	uid = uap->uid;
	if (uid != cr->cr_ruid &&		/* allow setuid(getuid()) */
#ifdef _POSIX_SAVED_IDS
	    uid != crc->cr_svuid &&		/* allow setuid(saved gid) */
#endif
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use BSD-compat clause from B.4.2.2 */
	    uid != cr->cr_uid &&	/* allow setuid(geteuid()) */
#endif
	    (error = priv_check_cred(cr, PRIV_CRED_SETUID, 0)))
		goto done;

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or uid == euid)
	 * If so, we are changing the real uid and/or saved uid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use the clause from B.4.2.2 */
	    uid == cr->cr_uid ||
#endif
	    priv_check_cred(cr, PRIV_CRED_SETUID, 0) == 0) /* we are using privs */
#endif
	{
		/*
		 * Set the real uid and transfer proc count to new user.
		 */
		if (uid != cr->cr_ruid) {
			cr = change_ruid(uid);
			setsugid();
		}
		/*
		 * Set saved uid
		 *
		 * XXX always set saved uid even if not _POSIX_SAVED_IDS, as
		 * the security of seteuid() depends on it.  B.4.2.2 says it
		 * is important that we should do this.
		 */
		if (cr->cr_svuid != uid) {
			cr = cratom(&p->p_ucred);
			cr->cr_svuid = uid;
			setsugid();
		}
	}

	/*
	 * In all permitted cases, we are changing the euid.
	 * Copy credentials so other references do not see our changes.
	 */
	if (cr->cr_uid != uid) {
		change_euid(uid);
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
sys_seteuid(struct seteuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t euid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;
	euid = uap->euid;
	if (euid != cr->cr_ruid &&		/* allow seteuid(getuid()) */
	    euid != cr->cr_svuid &&		/* allow seteuid(saved uid) */
	    (error = priv_check_cred(cr, PRIV_CRED_SETEUID, 0))) {
		lwkt_reltoken(&p->p_token);
		return (error);
	}

	/*
	 * Everything's okay, do it.  Copy credentials so other references do
	 * not see our changes.
	 */
	if (cr->cr_uid != euid) {
		change_euid(euid);
		setsugid();
	}
	lwkt_reltoken(&p->p_token);
	return (0);
}

int
sys_setgid(struct setgid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t gid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setgid(getegid()) is a special case of
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatible with traditional BSD
	 * semantics.  Basically, it means that "setgid(xx)" sets all
	 * three id's (assuming you have privs).
	 *
	 * For notes on the logic here, see setuid() above.
	 */
	gid = uap->gid;
	if (gid != cr->cr_rgid &&		/* allow setgid(getgid()) */
#ifdef _POSIX_SAVED_IDS
	    gid != cr->cr_svgid &&		/* allow setgid(saved gid) */
#endif
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use BSD-compat clause from B.4.2.2 */
	    gid != cr->cr_groups[0] && /* allow setgid(getegid()) */
#endif
	    (error = priv_check_cred(cr, PRIV_CRED_SETGID, 0))) {
		goto done;
	}

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or gid == egid)
	 * If so, we are changing the real uid and saved gid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* use the clause from B.4.2.2 */
	    gid == cr->cr_groups[0] ||
#endif
	    priv_check_cred(cr, PRIV_CRED_SETGID, 0) == 0) /* we are using privs */
#endif
	{
		/*
		 * Set real gid
		 */
		if (cr->cr_rgid != gid) {
			cr = cratom(&p->p_ucred);
			cr->cr_rgid = gid;
			setsugid();
		}
		/*
		 * Set saved gid
		 *
		 * XXX always set saved gid even if not _POSIX_SAVED_IDS, as
		 * the security of setegid() depends on it.  B.4.2.2 says it
		 * is important that we should do this.
		 */
		if (cr->cr_svgid != gid) {
			cr = cratom(&p->p_ucred);
			cr->cr_svgid = gid;
			setsugid();
		}
	}
	/*
	 * In all cases permitted cases, we are changing the egid.
	 * Copy credentials so other references do not see our changes.
	 */
	if (cr->cr_groups[0] != gid) {
		cr = cratom(&p->p_ucred);
		cr->cr_groups[0] = gid;
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
sys_setegid(struct setegid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t egid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;
	egid = uap->egid;
	if (egid != cr->cr_rgid &&		/* allow setegid(getgid()) */
	    egid != cr->cr_svgid &&		/* allow setegid(saved gid) */
	    (error = priv_check_cred(cr, PRIV_CRED_SETEGID, 0))) {
		goto done;
	}
	if (cr->cr_groups[0] != egid) {
		cr = cratom(&p->p_ucred);
		cr->cr_groups[0] = egid;
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
sys_setgroups(struct setgroups_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	u_int ngrp;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;

	if ((error = priv_check_cred(cr, PRIV_CRED_SETGROUPS, 0)))
		goto done;
	ngrp = uap->gidsetsize;
	if (ngrp > NGROUPS) {
		error = EINVAL;
		goto done;
	}
	/*
	 * XXX A little bit lazy here.  We could test if anything has
	 * changed before cratom() and setting P_SUGID.
	 */
	cr = cratom(&p->p_ucred);
	if (ngrp < 1) {
		/*
		 * setgroups(0, NULL) is a legitimate way of clearing the
		 * groups vector on non-BSD systems (which generally do not
		 * have the egid in the groups[0]).  We risk security holes
		 * when running non-BSD software if we do not do the same.
		 */
		cr->cr_ngroups = 1;
	} else {
		error = copyin(uap->gidset, cr->cr_groups,
			       ngrp * sizeof(gid_t));
		if (error)
			goto done;
		cr->cr_ngroups = ngrp;
	}
	setsugid();
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
sys_setreuid(struct setreuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t ruid, euid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;

	ruid = uap->ruid;
	euid = uap->euid;
	if (((ruid != (uid_t)-1 && ruid != cr->cr_ruid && ruid != cr->cr_svuid) ||
	     (euid != (uid_t)-1 && euid != cr->cr_uid &&
	     euid != cr->cr_ruid && euid != cr->cr_svuid)) &&
	    (error = priv_check_cred(cr, PRIV_CRED_SETREUID, 0)) != 0) {
		goto done;
	}

	if (euid != (uid_t)-1 && cr->cr_uid != euid) {
		cr = change_euid(euid);
		setsugid();
	}
	if (ruid != (uid_t)-1 && cr->cr_ruid != ruid) {
		cr = change_ruid(ruid);
		setsugid();
	}
	if ((ruid != (uid_t)-1 || cr->cr_uid != cr->cr_ruid) &&
	    cr->cr_svuid != cr->cr_uid) {
		cr = cratom(&p->p_ucred);
		cr->cr_svuid = cr->cr_uid;
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
sys_setregid(struct setregid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t rgid, egid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;

	rgid = uap->rgid;
	egid = uap->egid;
	if (((rgid != (gid_t)-1 && rgid != cr->cr_rgid && rgid != cr->cr_svgid) ||
	     (egid != (gid_t)-1 && egid != cr->cr_groups[0] &&
	     egid != cr->cr_rgid && egid != cr->cr_svgid)) &&
	    (error = priv_check_cred(cr, PRIV_CRED_SETREGID, 0)) != 0) {
		goto done;
	}

	if (egid != (gid_t)-1 && cr->cr_groups[0] != egid) {
		cr = cratom(&p->p_ucred);
		cr->cr_groups[0] = egid;
		setsugid();
	}
	if (rgid != (gid_t)-1 && cr->cr_rgid != rgid) {
		cr = cratom(&p->p_ucred);
		cr->cr_rgid = rgid;
		setsugid();
	}
	if ((rgid != (gid_t)-1 || cr->cr_groups[0] != cr->cr_rgid) &&
	    cr->cr_svgid != cr->cr_groups[0]) {
		cr = cratom(&p->p_ucred);
		cr->cr_svgid = cr->cr_groups[0];
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

/*
 * setresuid(ruid, euid, suid) is like setreuid except control over the
 * saved uid is explicit.
 */
int
sys_setresuid(struct setresuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t ruid, euid, suid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;

	ruid = uap->ruid;
	euid = uap->euid;
	suid = uap->suid;
	if (((ruid != (uid_t)-1 && ruid != cr->cr_ruid && ruid != cr->cr_svuid &&
	      ruid != cr->cr_uid) ||
	     (euid != (uid_t)-1 && euid != cr->cr_ruid && euid != cr->cr_svuid &&
	      euid != cr->cr_uid) ||
	     (suid != (uid_t)-1 && suid != cr->cr_ruid && suid != cr->cr_svuid &&
	      suid != cr->cr_uid)) &&
	    (error = priv_check_cred(cr, PRIV_CRED_SETRESUID, 0)) != 0) {
		goto done;
	}
	if (euid != (uid_t)-1 && cr->cr_uid != euid) {
		cr = change_euid(euid);
		setsugid();
	}
	if (ruid != (uid_t)-1 && cr->cr_ruid != ruid) {
		cr = change_ruid(ruid);
		setsugid();
	}
	if (suid != (uid_t)-1 && cr->cr_svuid != suid) {
		cr = cratom(&p->p_ucred);
		cr->cr_svuid = suid;
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

/*
 * setresgid(rgid, egid, sgid) is like setregid except control over the
 * saved gid is explicit.
 */
int
sys_setresgid(struct setresgid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t rgid, egid, sgid;
	int error;

	lwkt_gettoken(&p->p_token);
	cr = p->p_ucred;
	rgid = uap->rgid;
	egid = uap->egid;
	sgid = uap->sgid;
	if (((rgid != (gid_t)-1 && rgid != cr->cr_rgid && rgid != cr->cr_svgid &&
	      rgid != cr->cr_groups[0]) ||
	     (egid != (gid_t)-1 && egid != cr->cr_rgid && egid != cr->cr_svgid &&
	      egid != cr->cr_groups[0]) ||
	     (sgid != (gid_t)-1 && sgid != cr->cr_rgid && sgid != cr->cr_svgid &&
	      sgid != cr->cr_groups[0])) &&
	    (error = priv_check_cred(cr, PRIV_CRED_SETRESGID, 0)) != 0) {
		goto done;
	}

	if (egid != (gid_t)-1 && cr->cr_groups[0] != egid) {
		cr = cratom(&p->p_ucred);
		cr->cr_groups[0] = egid;
		setsugid();
	}
	if (rgid != (gid_t)-1 && cr->cr_rgid != rgid) {
		cr = cratom(&p->p_ucred);
		cr->cr_rgid = rgid;
		setsugid();
	}
	if (sgid != (gid_t)-1 && cr->cr_svgid != sgid) {
		cr = cratom(&p->p_ucred);
		cr->cr_svgid = sgid;
		setsugid();
	}
	error = 0;
done:
	lwkt_reltoken(&p->p_token);
	return (error);
}

int
sys_getresuid(struct getresuid_args *uap)
{
	struct ucred *cr;
	int error1 = 0, error2 = 0, error3 = 0;

	/*
	 * copyout's can fault synchronously so we cannot use a shared
	 * token here.
	 */
	cr = curthread->td_ucred;
	if (uap->ruid)
		error1 = copyout((caddr_t)&cr->cr_ruid,
		    (caddr_t)uap->ruid, sizeof(cr->cr_ruid));
	if (uap->euid)
		error2 = copyout((caddr_t)&cr->cr_uid,
		    (caddr_t)uap->euid, sizeof(cr->cr_uid));
	if (uap->suid)
		error3 = copyout((caddr_t)&cr->cr_svuid,
		    (caddr_t)uap->suid, sizeof(cr->cr_svuid));
	return error1 ? error1 : (error2 ? error2 : error3);
}

int
sys_getresgid(struct getresgid_args *uap)
{
	struct ucred *cr;
	int error1 = 0, error2 = 0, error3 = 0;

	cr = curthread->td_ucred;
	if (uap->rgid)
		error1 = copyout(&cr->cr_rgid, uap->rgid,
				 sizeof(cr->cr_rgid));
	if (uap->egid)
		error2 = copyout(&cr->cr_groups[0], uap->egid,
				 sizeof(cr->cr_groups[0]));
	if (uap->sgid)
		error3 = copyout(&cr->cr_svgid, uap->sgid,
				 sizeof(cr->cr_svgid));
	return error1 ? error1 : (error2 ? error2 : error3);
}


/*
 * NOTE: OpenBSD sets a P_SUGIDEXEC flag set at execve() time,
 * we use P_SUGID because we consider changing the owners as
 * "tainting" as well.
 * This is significant for procs that start as root and "become"
 * a user without an exec - programs cannot know *everything*
 * that libc *might* have put in their data segment.
 */
int
sys_issetugid(struct issetugid_args *uap)
{
	uap->sysmsg_result = (curproc->p_flags & P_SUGID) ? 1 : 0;
	return (0);
}

/*
 * Check if gid is a member of the group set.
 */
int
groupmember(gid_t gid, struct ucred *cred)
{
	gid_t *gp;
	gid_t *egp;

	egp = &(cred->cr_groups[cred->cr_ngroups]);
	for (gp = cred->cr_groups; gp < egp; gp++) {
		if (*gp == gid)
			return (1);
	}
	return (0);
}

/*
 * Test whether the specified credentials have the privilege
 * in question.
 *
 * A kernel thread without a process context is assumed to have 
 * the privilege in question.  In situations where the caller always 
 * expect a cred to exist, the cred should be passed separately and 
 * priv_check_cred() should be used instead of priv_check().
 *
 * Returns 0 or error.
 */
int
priv_check(struct thread *td, int priv)
{
	if (td->td_lwp != NULL)
		return priv_check_cred(td->td_ucred, priv, 0);
	return (0);
}

/*
 * Check a credential for privilege.
 *
 * A non-null credential is expected unless NULL_CRED_OKAY is set.
 */
int
priv_check_cred(struct ucred *cred, int priv, int flags)
{
	int error;

	KASSERT(PRIV_VALID(priv), ("priv_check_cred: invalid privilege"));

	KASSERT(cred != NULL || (flags & NULL_CRED_OKAY),
		("priv_check_cred: NULL cred!"));

	if (cred == NULL) {
		if (flags & NULL_CRED_OKAY)
			return (0);
		else
			return (EPERM);
	}
	if (cred->cr_uid != 0) 
		return (EPERM);

	error = prison_priv_check(cred, priv);
	if (error)
		return (error);

	/* NOTE: accounting for suser access (p_acflag/ASU) removed */
	return (0);
}

/*
 * Return zero if p1 can fondle p2, return errno (EPERM/ESRCH) otherwise.
 */
int
p_trespass(struct ucred *cr1, struct ucred *cr2)
{
	if (cr1 == cr2)
		return (0);
	if (!PRISON_CHECK(cr1, cr2))
		return (ESRCH);
	if (cr1->cr_ruid == cr2->cr_ruid)
		return (0);
	if (cr1->cr_uid == cr2->cr_ruid)
		return (0);
	if (cr1->cr_ruid == cr2->cr_uid)
		return (0);
	if (cr1->cr_uid == cr2->cr_uid)
		return (0);
	if (priv_check_cred(cr1, PRIV_PROC_TRESPASS, 0) == 0)
		return (0);
	return (EPERM);
}

static __inline void
_crinit(struct ucred *cr)
{
	cr->cr_ref = 1;
}

void
crinit(struct ucred *cr)
{
	bzero(cr, sizeof(*cr));
	_crinit(cr);
}

/*
 * Allocate a zeroed cred structure.
 */
struct ucred *
crget(void)
{
	struct ucred *cr;

	cr = kmalloc(sizeof(*cr), M_CRED, M_WAITOK|M_ZERO);
	_crinit(cr);
	return (cr);
}

/*
 * Claim another reference to a ucred structure.  Can be used with special
 * creds.
 *
 * It must be possible to call this routine with spinlocks held, meaning
 * that this routine itself cannot obtain a spinlock.
 */
struct ucred *
crhold(struct ucred *cr)
{
	if (cr != NOCRED && cr != FSCRED)
		atomic_add_int(&cr->cr_ref, 1);
	return(cr);
}

/*
 * Drop a reference from the cred structure, free it if the reference count
 * reaches 0. 
 *
 * NOTE: because we used atomic_add_int() above, without a spinlock, we
 * must also use atomic_subtract_int() below.  A spinlock is required
 * in crfree() to handle multiple callers racing the refcount to 0.
 */
void
crfree(struct ucred *cr)
{
	if (cr->cr_ref <= 0)
		panic("Freeing already free credential! %p", cr);
	if (atomic_fetchadd_int(&cr->cr_ref, -1) == 1) {
		/*
		 * Some callers of crget(), such as nfs_statfs(),
		 * allocate a temporary credential, but don't
		 * allocate a uidinfo structure.
		 */
		if (cr->cr_uidinfo != NULL) {
			uidrop(cr->cr_uidinfo);
			cr->cr_uidinfo = NULL;
		}
		if (cr->cr_ruidinfo != NULL) {
			uidrop(cr->cr_ruidinfo);
			cr->cr_ruidinfo = NULL;
		}

		/*
		 * Destroy empty prisons
		 */
		if (jailed(cr))
			prison_free(cr->cr_prison);
		cr->cr_prison = NULL;	/* safety */

		kfree((caddr_t)cr, M_CRED);
	}
}

/*
 * Atomize a cred structure so it can be modified without polluting
 * other references to it.
 *
 * MPSAFE (however, *pcr must be stable)
 */
struct ucred *
cratom(struct ucred **pcr)
{
	struct ucred *oldcr;
	struct ucred *newcr;

	oldcr = *pcr;
	if (oldcr->cr_ref == 1)
		return (oldcr);
	newcr = crget();
	*newcr = *oldcr;
	if (newcr->cr_uidinfo)
		uihold(newcr->cr_uidinfo);
	if (newcr->cr_ruidinfo)
		uihold(newcr->cr_ruidinfo);
	if (jailed(newcr))
		prison_hold(newcr->cr_prison);
	newcr->cr_ref = 1;
	crfree(oldcr);
	*pcr = newcr;
	return (newcr);
}

/*
 * Dup cred struct to a new held one.
 */
struct ucred *
crdup(struct ucred *cr)
{
	struct ucred *newcr;

	newcr = crget();
	*newcr = *cr;
	if (newcr->cr_uidinfo)
		uihold(newcr->cr_uidinfo);
	if (newcr->cr_ruidinfo)
		uihold(newcr->cr_ruidinfo);
	if (jailed(newcr))
		prison_hold(newcr->cr_prison);
	newcr->cr_ref = 1;
	return (newcr);
}

/*
 * Fill in a struct xucred based on a struct ucred.
 */
void
cru2x(struct ucred *cr, struct xucred *xcr)
{

	bzero(xcr, sizeof(*xcr));
	xcr->cr_version = XUCRED_VERSION;
	xcr->cr_uid = cr->cr_uid;
	xcr->cr_ngroups = cr->cr_ngroups;
	bcopy(cr->cr_groups, xcr->cr_groups, sizeof(cr->cr_groups));
}

/*
 * Get login name, if available.
 */
int
sys_getlogin(struct getlogin_args *uap)
{
	struct proc *p = curproc;
	char buf[MAXLOGNAME];
	int error;

	if (uap->namelen > MAXLOGNAME)		/* namelen is unsigned */
		uap->namelen = MAXLOGNAME;
	bzero(buf, sizeof(buf));
	lwkt_gettoken_shared(&p->p_token);
	bcopy(p->p_pgrp->pg_session->s_login, buf, uap->namelen);
	lwkt_reltoken(&p->p_token);

	error = copyout(buf, uap->namebuf, uap->namelen);
	return (error);
}

/*
 * Set login name.
 */
int
sys_setlogin(struct setlogin_args *uap)
{
	struct thread *td = curthread;
	struct proc *p;
	struct ucred *cred;
	char buf[MAXLOGNAME];
	int error;

	cred = td->td_ucred;
	p = td->td_proc;

	if ((error = priv_check_cred(cred, PRIV_PROC_SETLOGIN, 0)))
		return (error);
	bzero(buf, sizeof(buf));
	error = copyinstr(uap->namebuf, buf, sizeof(buf), NULL);
	if (error == ENAMETOOLONG)
		error = EINVAL;
	if (error == 0) {
		lwkt_gettoken_shared(&p->p_token);
		memcpy(p->p_pgrp->pg_session->s_login, buf, sizeof(buf));
		lwkt_reltoken(&p->p_token);
	}
	return (error);
}

void
setsugid(void)
{
	struct proc *p = curproc;

	KKASSERT(p != NULL);
	lwkt_gettoken(&p->p_token);
	p->p_flags |= P_SUGID;
	if (!(p->p_pfsflags & PF_ISUGID))
		p->p_stops = 0;
	lwkt_reltoken(&p->p_token);
}

/*
 * Helper function to change the effective uid of a process
 */
struct ucred *
change_euid(uid_t euid)
{
	struct	proc *p = curproc;
	struct	ucred *cr;

	KKASSERT(p != NULL);
	lf_count_adjust(p, 0);
	cr = cratom(&p->p_ucred);
	cr->cr_uid = euid;
	uireplace(&cr->cr_uidinfo, uifind(euid));
	lf_count_adjust(p, 1);
	return (cr);
}

/*
 * Helper function to change the real uid of a process
 *
 * The per-uid process count for this process is transfered from
 * the old uid to the new uid.
 */
struct ucred *
change_ruid(uid_t ruid)
{
	struct	proc *p = curproc;
	struct	ucred *cr;

	KKASSERT(p != NULL);

	cr = cratom(&p->p_ucred);
	chgproccnt(cr->cr_ruidinfo, -1, 0);
	cr->cr_ruid = ruid;
	uireplace(&cr->cr_ruidinfo, uifind(ruid));
	chgproccnt(cr->cr_ruidinfo, 1, 0);
	return (cr);
}
