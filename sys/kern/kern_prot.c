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
 *	@(#)kern_prot.c	8.6 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_prot.c,v 1.53.2.9 2002/03/09 05:20:26 dd Exp $
 * $DragonFly: src/sys/kern/kern_prot.c,v 1.5 2003/06/26 02:17:45 dillon Exp $
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
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/pioctl.h>
#include <sys/resourcevar.h>
#include <sys/jail.h>

static MALLOC_DEFINE(M_CRED, "cred", "credentials");

#ifndef _SYS_SYSPROTO_H_
struct getpid_args {
	int	dummy;
};
#endif

/*
 * NOT MP SAFE due to p_pptr access
 */
/* ARGSUSED */
int
getpid(struct getpid_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_pid;
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	p->p_retval[1] = p->p_pptr->p_pid;
#endif
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getppid_args {
        int     dummy;
};
#endif
/* ARGSUSED */
int
getppid(struct getppid_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_pptr->p_pid;
	return (0);
}

/* 
 * Get process group ID; note that POSIX getpgrp takes no parameter 
 *
 * MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct getpgrp_args {
        int     dummy;
};
#endif

int
getpgrp(struct getpgrp_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_pgrp->pg_id;
	return (0);
}

/* Get an arbitary pid's process group id */
#ifndef _SYS_SYSPROTO_H_
struct getpgid_args {
	pid_t	pid;
};
#endif

int
getpgid(struct getpgid_args *uap)
{
	struct proc *p = curproc;
	struct proc *pt;

	pt = p;
	if (uap->pid == 0)
		goto found;

	if ((pt = pfind(uap->pid)) == 0)
		return ESRCH;
found:
	p->p_retval[0] = pt->p_pgrp->pg_id;
	return 0;
}

/*
 * Get an arbitary pid's session id.
 */
#ifndef _SYS_SYSPROTO_H_
struct getsid_args {
	pid_t	pid;
};
#endif

int
getsid(struct getsid_args *uap)
{
	struct proc *p = curproc;
	struct proc *pt;

	pt = p;
	if (uap->pid == 0)
		goto found;

	if ((pt = pfind(uap->pid)) == 0)
		return ESRCH;
found:
	p->p_retval[0] = pt->p_session->s_sid;
	return 0;
}


/*
 * getuid() - MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct getuid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
getuid(struct getuid_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_ucred->cr_ruid;
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	p->p_retval[1] = p->p_ucred->cr_uid;
#endif
	return (0);
}

/*
 * geteuid() - MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct geteuid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
geteuid(struct geteuid_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_ucred->cr_uid;
	return (0);
}

/*
 * getgid() - MP SAFE
 */
#ifndef _SYS_SYSPROTO_H_
struct getgid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
getgid(struct getgid_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_ucred->cr_rgid;
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
	p->p_retval[1] = p->p_ucred->cr_groups[0];
#endif
	return (0);
}

/*
 * Get effective group ID.  The "egid" is groups[0], and could be obtained
 * via getgroups.  This syscall exists because it is somewhat painful to do
 * correctly in a library function.
 */
#ifndef _SYS_SYSPROTO_H_
struct getegid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
getegid(struct getegid_args *uap)
{
	struct proc *p = curproc;

	p->p_retval[0] = p->p_ucred->cr_groups[0];
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getgroups_args {
	u_int	gidsetsize;
	gid_t	*gidset;
};
#endif
int
getgroups(struct getgroups_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	u_int ngrp;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	if ((ngrp = uap->gidsetsize) == 0) {
		p->p_retval[0] = cr->cr_ngroups;
		return (0);
	}
	if (ngrp < cr->cr_ngroups)
		return (EINVAL);
	ngrp = cr->cr_ngroups;
	if ((error = copyout((caddr_t)cr->cr_groups,
	    (caddr_t)uap->gidset, ngrp * sizeof(gid_t))))
		return (error);
	p->p_retval[0] = ngrp;
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setsid_args {
        int     dummy;
};
#endif

/* ARGSUSED */
int
setsid(struct setsid_args *uap)
{
	struct proc *p = curproc;

	if (p->p_pgid == p->p_pid || pgfind(p->p_pid)) {
		return (EPERM);
	} else {
		(void)enterpgrp(p, p->p_pid, 1);
		p->p_retval[0] = p->p_pid;
		return (0);
	}
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
#ifndef _SYS_SYSPROTO_H_
struct setpgid_args {
	int	pid;	/* target process id */
	int	pgid;	/* target pgrp id */
};
#endif
/* ARGSUSED */
int
setpgid(struct setpgid_args *uap)
{
	struct proc *curp = curproc;
	struct proc *targp;		/* target process */
	struct pgrp *pgrp;		/* target pgrp */

	if (uap->pgid < 0)
		return (EINVAL);
	if (uap->pid != 0 && uap->pid != curp->p_pid) {
		if ((targp = pfind(uap->pid)) == 0 || !inferior(targp))
			return (ESRCH);
		if (targp->p_pgrp == NULL ||  targp->p_session != curp->p_session)
			return (EPERM);
		if (targp->p_flag & P_EXEC)
			return (EACCES);
	} else
		targp = curp;
	if (SESS_LEADER(targp))
		return (EPERM);
	if (uap->pgid == 0)
		uap->pgid = targp->p_pid;
	else if (uap->pgid != targp->p_pid)
		if ((pgrp = pgfind(uap->pgid)) == 0 ||
	            pgrp->pg_session != curp->p_session)
			return (EPERM);
	return (enterpgrp(targp, uap->pgid, 0));
}

/*
 * Use the clause in B.4.2.2 that allows setuid/setgid to be 4.2/4.3BSD
 * compatable.  It says that setting the uid/gid to euid/egid is a special
 * case of "appropriate privilege".  Once the rules are expanded out, this
 * basically means that setuid(nnn) sets all three id's, in all permitted
 * cases unless _POSIX_SAVED_IDS is enabled.  In that case, setuid(getuid())
 * does not set the saved id - this is dangerous for traditional BSD
 * programs.  For this reason, we *really* do not want to set
 * _POSIX_SAVED_IDS and do not want to clear POSIX_APPENDIX_B_4_2_2.
 */
#define POSIX_APPENDIX_B_4_2_2

#ifndef _SYS_SYSPROTO_H_
struct setuid_args {
	uid_t	uid;
};
#endif
/* ARGSUSED */
int
setuid(struct setuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t uid;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setuid(geteuid()) is a special case of 
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatable with traditional BSD
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
	    (error = suser_cred(cr, PRISON_ROOT)))
		return (error);

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or uid == euid)
	 * If so, we are changing the real uid and/or saved uid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* Use the clause from B.4.2.2 */
	    uid == cr->cr_uid ||
#endif
	    suser_cred(cr, PRISON_ROOT) == 0) /* we are using privs */
#endif
	{
		/*
		 * Set the real uid and transfer proc count to new user.
		 */
		if (uid != cr->cr_ruid) {
			change_ruid(uid);
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
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct seteuid_args {
	uid_t	euid;
};
#endif
/* ARGSUSED */
int
seteuid(struct seteuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t euid;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);

	cr = p->p_ucred;
	euid = uap->euid;
	if (euid != cr->cr_ruid &&		/* allow seteuid(getuid()) */
	    euid != cr->cr_svuid &&		/* allow seteuid(saved uid) */
	    (error = suser_cred(cr, PRISON_ROOT)))
		return (error);
	/*
	 * Everything's okay, do it.  Copy credentials so other references do
	 * not see our changes.
	 */
	if (cr->cr_uid != euid) {
		change_euid(euid);
		setsugid();
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setgid_args {
	gid_t	gid;
};
#endif
/* ARGSUSED */
int
setgid(struct setgid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t gid;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	/*
	 * See if we have "permission" by POSIX 1003.1 rules.
	 *
	 * Note that setgid(getegid()) is a special case of
	 * "appropriate privileges" in appendix B.4.2.2.  We need
	 * to use this clause to be compatable with traditional BSD
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
	    (error = suser_cred(cr, PRISON_ROOT)))
		return (error);

#ifdef _POSIX_SAVED_IDS
	/*
	 * Do we have "appropriate privileges" (are we root or gid == egid)
	 * If so, we are changing the real uid and saved gid.
	 */
	if (
#ifdef POSIX_APPENDIX_B_4_2_2	/* use the clause from B.4.2.2 */
	    gid == cr->cr_groups[0] ||
#endif
	    suser_cred(cr, PRISON_ROOT) == 0) /* we are using privs */
#endif
	{
		/*
		 * Set real gid
		 */
		if (cr->cr_rgid != gid) {
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
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setegid_args {
	gid_t	egid;
};
#endif
/* ARGSUSED */
int
setegid(struct setegid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t egid;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	egid = uap->egid;
	if (egid != cr->cr_rgid &&		/* allow setegid(getgid()) */
	    egid != cr->cr_svgid &&		/* allow setegid(saved gid) */
	    (error = suser_cred(cr, PRISON_ROOT)))
		return (error);
	if (cr->cr_groups[0] != egid) {
		cr = cratom(&p->p_ucred);
		cr->cr_groups[0] = egid;
		setsugid();
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setgroups_args {
	u_int	gidsetsize;
	gid_t	*gidset;
};
#endif
/* ARGSUSED */
int
setgroups(struct setgroups_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	u_int ngrp;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	if ((error = suser_cred(cr, PRISON_ROOT)))
		return (error);
	ngrp = uap->gidsetsize;
	if (ngrp > NGROUPS)
		return (EINVAL);
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
		if ((error = copyin((caddr_t)uap->gidset,
		    (caddr_t)cr->cr_groups, ngrp * sizeof(gid_t))))
			return (error);
		cr->cr_ngroups = ngrp;
	}
	setsugid();
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setreuid_args {
	uid_t	ruid;
	uid_t	euid;
};
#endif
/* ARGSUSED */
int
setreuid(struct setreuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t ruid, euid;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	ruid = uap->ruid;
	euid = uap->euid;
	if (((ruid != (uid_t)-1 && ruid != cr->cr_ruid && ruid != cr->cr_svuid) ||
	     (euid != (uid_t)-1 && euid != cr->cr_uid &&
	     euid != cr->cr_ruid && euid != cr->cr_svuid)) &&
	    (error = suser_cred(cr, PRISON_ROOT)) != 0)
		return (error);

	if (euid != (uid_t)-1 && cr->cr_uid != euid) {
		change_euid(euid);
		setsugid();
	}
	if (ruid != (uid_t)-1 && cr->cr_ruid != ruid) {
		change_ruid(ruid);
		setsugid();
	}
	if ((ruid != (uid_t)-1 || cr->cr_uid != cr->cr_ruid) &&
	    cr->cr_svuid != cr->cr_uid) {
		cr = cratom(&p->p_ucred);
		cr->cr_svuid = cr->cr_uid;
		setsugid();
	}
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct setregid_args {
	gid_t	rgid;
	gid_t	egid;
};
#endif
/* ARGSUSED */
int
setregid(struct setregid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t rgid, egid;
	int error;

	if (p == NULL)				/* API enforcement */
		return(EPERM);
	cr = p->p_ucred;

	rgid = uap->rgid;
	egid = uap->egid;
	if (((rgid != (gid_t)-1 && rgid != cr->cr_rgid && rgid != cr->cr_svgid) ||
	     (egid != (gid_t)-1 && egid != cr->cr_groups[0] &&
	     egid != cr->cr_rgid && egid != cr->cr_svgid)) &&
	    (error = suser_cred(cr, PRISON_ROOT)) != 0)
		return (error);

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
	return (0);
}

/*
 * setresuid(ruid, euid, suid) is like setreuid except control over the
 * saved uid is explicit.
 */

#ifndef _SYS_SYSPROTO_H_
struct setresuid_args {
	uid_t	ruid;
	uid_t	euid;
	uid_t	suid;
};
#endif
/* ARGSUSED */
int
setresuid(struct setresuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	uid_t ruid, euid, suid;
	int error;

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
	    (error = suser_cred(cr, PRISON_ROOT)) != 0)
		return (error);
	if (euid != (uid_t)-1 && cr->cr_uid != euid) {
		change_euid(euid);
		setsugid();
	}
	if (ruid != (uid_t)-1 && cr->cr_ruid != ruid) {
		change_ruid(ruid);
		setsugid();
	}
	if (suid != (uid_t)-1 && cr->cr_svuid != suid) {
		cr = cratom(&p->p_ucred);
		cr->cr_svuid = suid;
		setsugid();
	}
	return (0);
}

/*
 * setresgid(rgid, egid, sgid) is like setregid except control over the
 * saved gid is explicit.
 */

#ifndef _SYS_SYSPROTO_H_
struct setresgid_args {
	gid_t	rgid;
	gid_t	egid;
	gid_t	sgid;
};
#endif
/* ARGSUSED */
int
setresgid(struct setresgid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr;
	gid_t rgid, egid, sgid;
	int error;

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
	    (error = suser_cred(cr, PRISON_ROOT)) != 0)
		return (error);

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
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getresuid_args {
	uid_t	*ruid;
	uid_t	*euid;
	uid_t	*suid;
};
#endif
/* ARGSUSED */
int
getresuid(struct getresuid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr = p->p_ucred;
	int error1 = 0, error2 = 0, error3 = 0;

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

#ifndef _SYS_SYSPROTO_H_
struct getresgid_args {
	gid_t	*rgid;
	gid_t	*egid;
	gid_t	*sgid;
};
#endif
/* ARGSUSED */
int
getresgid(struct getresgid_args *uap)
{
	struct proc *p = curproc;
	struct ucred *cr = p->p_ucred;
	int error1 = 0, error2 = 0, error3 = 0;

	if (uap->rgid)
		error1 = copyout((caddr_t)&cr->cr_rgid,
		    (caddr_t)uap->rgid, sizeof(cr->cr_rgid));
	if (uap->egid)
		error2 = copyout((caddr_t)&cr->cr_groups[0],
		    (caddr_t)uap->egid, sizeof(cr->cr_groups[0]));
	if (uap->sgid)
		error3 = copyout((caddr_t)&cr->cr_svgid,
		    (caddr_t)uap->sgid, sizeof(cr->cr_svgid));
	return error1 ? error1 : (error2 ? error2 : error3);
}


#ifndef _SYS_SYSPROTO_H_
struct issetugid_args {
	int dummy;
};
#endif
/* ARGSUSED */
int
issetugid(struct issetugid_args *uap)
{
	struct proc *p = curproc;
	/*
	 * Note: OpenBSD sets a P_SUGIDEXEC flag set at execve() time,
	 * we use P_SUGID because we consider changing the owners as
	 * "tainting" as well.
	 * This is significant for procs that start as root and "become"
	 * a user without an exec - programs cannot know *everything*
	 * that libc *might* have put in their data segment.
	 */
	p->p_retval[0] = (p->p_flag & P_SUGID) ? 1 : 0;
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
	for (gp = cred->cr_groups; gp < egp; gp++)
		if (*gp == gid)
			return (1);
	return (0);
}

/*
 * Test whether the specified credentials imply "super-user"
 * privilege; if so, and we have accounting info, set the flag
 * indicating use of super-powers.  A kernel thread without a process
 * context is assumed to have super user capabilities.  In situations
 * where the caller always expect a cred to exist, the cred should be
 * passed separately and suser_cred()should be used instead of suser().
 *
 * Returns 0 or error.
 */
int
suser(struct thread *td)
{
	struct proc *p = td->td_proc;

	if (p != NULL) {
		return suser_cred(p->p_ucred, 0);
	} else {
		return (0);
	}
}

int
suser_cred(struct ucred *cred, int flag)
{
	KASSERT(cred != NULL, ("suser_cred: NULL cred!"));

	if (cred->cr_uid != 0) 
		return (EPERM);
	if (cred->cr_prison && !(flag & PRISON_ROOT))
		return (EPERM);
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
	if (suser_cred(cr1, PRISON_ROOT) == 0)
		return (0);
	return (EPERM);
}

/*
 * Allocate a zeroed cred structure.
 */
struct ucred *
crget()
{
	register struct ucred *cr;

	MALLOC(cr, struct ucred *, sizeof(*cr), M_CRED, M_WAITOK);
	bzero((caddr_t)cr, sizeof(*cr));
	cr->cr_ref = 1;
	return (cr);
}

/*
 * Claim another reference to a ucred structure.  Can be used with special
 * creds.
 */
struct ucred *
crhold(struct ucred *cr)
{
	if (cr != NOCRED && cr != FSCRED)
		cr->cr_ref++;
	return(cr);
}

/*
 * Free a cred structure.
 * Throws away space when ref count gets to 0.
 */
void
crfree(struct ucred *cr)
{
	if (cr->cr_ref == 0)
		panic("Freeing already free credential! %p", cr);
	
	if (--cr->cr_ref == 0) {
		/*
		 * Some callers of crget(), such as nfs_statfs(),
		 * allocate a temporary credential, but don't
		 * allocate a uidinfo structure.
		 */
		if (cr->cr_uidinfo != NULL)
			uifree(cr->cr_uidinfo);
		if (cr->cr_ruidinfo != NULL)
			uifree(cr->cr_ruidinfo);

		/*
		 * Destroy empty prisons
		 */
		if (cr->cr_prison && !--cr->cr_prison->pr_ref) {
			if (cr->cr_prison->pr_linux != NULL)
				FREE(cr->cr_prison->pr_linux, M_PRISON);
			FREE(cr->cr_prison, M_PRISON);
		}
		cr->cr_prison = NULL;	/* safety */

		FREE((caddr_t)cr, M_CRED);
	}
}

/*
 * Atomize a cred structure so it can be modified without polluting
 * other references to it.
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
	if (newcr->cr_prison)
		++newcr->cr_prison->pr_ref;
	newcr->cr_ref = 1;
	crfree(oldcr);
	*pcr = newcr;
	return (newcr);
}

#if 0	/* no longer used but keep around for a little while */
/*
 * Copy cred structure to a new one and free the old one.
 */
struct ucred *
crcopy(struct ucred *cr)
{
	struct ucred *newcr;

	if (cr->cr_ref == 1)
		return (cr);
	newcr = crget();
	*newcr = *cr;
	if (newcr->cr_uidinfo)
		uihold(newcr->cr_uidinfo);
	if (newcr->cr_ruidinfo)
		uihold(newcr->cr_ruidinfo);
	if (newcr->cr_prison)
		++newcr->cr_prison->pr_ref;
	newcr->cr_ref = 1;
	crfree(cr);
	return (newcr);
}
#endif

/*
 * Dup cred struct to a new held one.
 */
struct ucred *
crdup(cr)
	struct ucred *cr;
{
	struct ucred *newcr;

	newcr = crget();
	*newcr = *cr;
	if (newcr->cr_uidinfo)
		uihold(newcr->cr_uidinfo);
	if (newcr->cr_ruidinfo)
		uihold(newcr->cr_ruidinfo);
	if (newcr->cr_prison)
		++newcr->cr_prison->pr_ref;
	newcr->cr_ref = 1;
	return (newcr);
}

/*
 * Fill in a struct xucred based on a struct ucred.
 */
void
cru2x(cr, xcr)
	struct ucred *cr;
	struct xucred *xcr;
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
#ifndef _SYS_SYSPROTO_H_
struct getlogin_args {
	char	*namebuf;
	u_int	namelen;
};
#endif
/* ARGSUSED */
int
getlogin(struct getlogin_args *uap)
{
	struct proc *p = curproc;

	if (uap->namelen > MAXLOGNAME)
		uap->namelen = MAXLOGNAME;
	return (copyout((caddr_t) p->p_pgrp->pg_session->s_login,
	    (caddr_t) uap->namebuf, uap->namelen));
}

/*
 * Set login name.
 */
#ifndef _SYS_SYSPROTO_H_
struct setlogin_args {
	char	*namebuf;
};
#endif
/* ARGSUSED */
int
setlogin(struct setlogin_args *uap)
{
	struct proc *p = curproc;
	int error;
	char logintmp[MAXLOGNAME];

	KKASSERT(p != NULL);
	if ((error = suser_cred(p->p_ucred, PRISON_ROOT)))
		return (error);
	error = copyinstr((caddr_t) uap->namebuf, (caddr_t) logintmp,
	    sizeof(logintmp), (size_t *)0);
	if (error == ENAMETOOLONG)
		error = EINVAL;
	else if (!error)
		(void) memcpy(p->p_pgrp->pg_session->s_login, logintmp,
		    sizeof(logintmp));
	return (error);
}

void
setsugid()
{
	struct proc *p = curproc;

	KKASSERT(p != NULL);
	p->p_flag |= P_SUGID;
	if (!(p->p_pfsflags & PF_ISUGID))
		p->p_stops = 0;
}

/*
 * Helper function to change the effective uid of a process
 */
void
change_euid(uid_t euid)
{
	struct	proc *p = curproc;
	struct	ucred *cr;
	struct	uidinfo *uip;

	KKASSERT(p != NULL);

	cr = cratom(&p->p_ucred);
	uip = cr->cr_uidinfo;
	cr->cr_uid = euid;
	cr->cr_uidinfo = uifind(euid);
	uifree(uip);
}

/*
 * Helper function to change the real uid of a process
 *
 * The per-uid process count for this process is transfered from
 * the old uid to the new uid.
 */
void
change_ruid(uid_t ruid)
{
	struct	proc *p = curproc;
	struct	ucred *cr;
	struct	uidinfo *uip;

	KKASSERT(p != NULL);

	cr = cratom(&p->p_ucred);
	(void)chgproccnt(cr->cr_ruidinfo, -1, 0);
	uip = cr->cr_ruidinfo;
	/* It is assumed that pcred is not shared between processes */
	cr->cr_ruid = ruid;
	cr->cr_ruidinfo = uifind(ruid);
	(void)chgproccnt(cr->cr_ruidinfo, 1, 0);
	uifree(uip);
}
