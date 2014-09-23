/*-
 * Copyright (c) 2001  The FreeBSD Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/linux/linux_uid16.c,v 1.4.2.1 2001/10/21 03:57:35 marcel Exp $
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kern_syscall.h>
#include <sys/nlookup.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sysproto.h>
#include <sys/thread.h>

#include <sys/mplock2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_util.h"

DUMMY(setfsuid16);
DUMMY(setfsgid16);
DUMMY(getresuid16);
DUMMY(getresgid16);

#define	CAST_NOCHG(x)	((x == 0xFFFF) ? -1 : x)

/*
 * MPALMOSTSAFE
 */
int
sys_linux_chown16(struct linux_chown16_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chown16))
		kprintf(ARGS(chown16, "%s, %d, %d"), path, args->uid,
		    args->gid);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_chown(&nd, CAST_NOCHG(args->uid),
				    CAST_NOCHG(args->gid));
	}
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_lchown16(struct linux_lchown16_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(lchown16))
		kprintf(ARGS(lchown16, "%s, %d, %d"), path, args->uid,
		    args->gid);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0) {
		error = kern_chown(&nd, CAST_NOCHG(args->uid),
				    CAST_NOCHG(args->gid));
	}
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_setgroups16(struct linux_setgroups16_args *args)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct ucred *newcred, *oldcred;
	l_gid16_t linux_gidset[NGROUPS];
	gid_t *bsd_gidset;
	int ngrp, error;

#ifdef DEBUG
	if (ldebug(setgroups16))
		kprintf(ARGS(setgroups16, "%d, *"), args->gidsetsize);
#endif

	ngrp = args->gidsetsize;
	oldcred = td->td_ucred;

	/*
	 * cr_groups[0] holds egid. Setting the whole set from
	 * the supplied set will cause egid to be changed too.
	 * Keep cr_groups[0] unchanged to prevent that.
	 */

	if ((error = priv_check_cred(oldcred, PRIV_CRED_SETGROUPS, 0)) != 0)
		return (error);

	if ((u_int)ngrp >= NGROUPS)
		return (EINVAL);

	get_mplock();
	newcred = crdup(oldcred);
	if (ngrp > 0) {
		error = copyin((caddr_t)args->gidset, linux_gidset,
			       ngrp * sizeof(l_gid16_t));
		if (error) {
			crfree(newcred);
			goto done;
		}

		newcred->cr_ngroups = ngrp + 1;

		bsd_gidset = newcred->cr_groups;
		ngrp--;
		while (ngrp >= 0) {
			bsd_gidset[ngrp + 1] = linux_gidset[ngrp];
			ngrp--;
		}
	} else {
		newcred->cr_ngroups = 1;
	}

	setsugid();
	oldcred = p->p_ucred;	/* deal with threads race */
	p->p_ucred = newcred;
	crfree(oldcred);
	error = 0;
done:
	rel_mplock();
	return (error);
}

/*
 * MPSAFE
 */
int
sys_linux_getgroups16(struct linux_getgroups16_args *args)
{
	struct proc *p = curproc;
	struct ucred *cred;
	l_gid16_t linux_gidset[NGROUPS];
	gid_t *bsd_gidset;
	int bsd_gidsetsz, ngrp, error;

#ifdef DEBUG
	if (ldebug(getgroups16))
		kprintf(ARGS(getgroups16, "%d, *"), args->gidsetsize);
#endif

	cred = p->p_ucred;
	bsd_gidset = cred->cr_groups;
	bsd_gidsetsz = cred->cr_ngroups - 1;

	/*
	 * cr_groups[0] holds egid. Returning the whole set
	 * here will cause a duplicate. Exclude cr_groups[0]
	 * to prevent that.
	 */

	if ((ngrp = args->gidsetsize) == 0) {
		args->sysmsg_result = bsd_gidsetsz;
		return (0);
	}

	if ((u_int)ngrp < (u_int)bsd_gidsetsz)
		return (EINVAL);

	ngrp = 0;
	while (ngrp < bsd_gidsetsz) {
		linux_gidset[ngrp] = bsd_gidset[ngrp + 1];
		ngrp++;
	}

	error = copyout(linux_gidset, (caddr_t)args->gidset,
			ngrp * sizeof(l_gid16_t));
	if (error)
		return (error);

	args->sysmsg_result = ngrp;
	return (0);
}

/*
 * The FreeBSD native getgid(2) and getuid(2) also modify p->p_retval[1]
 * when COMPAT_43 is defined. This globbers registers that
 * are assumed to be preserved. The following lightweight syscalls fixes
 * this. See also linux_getpid(2), linux_getgid(2) and linux_getuid(2) in
 * linux_misc.c
 *
 * linux_getgid16() - MP SAFE
 * linux_getuid16() - MP SAFE
 */

/*
 * MPSAFE
 */
int
sys_linux_getgid16(struct linux_getgid16_args *args)
{
	struct proc *p = curproc;

	args->sysmsg_result = p->p_ucred->cr_rgid;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_getuid16(struct linux_getuid16_args *args)
{
	struct proc *p = curproc;

	args->sysmsg_result = p->p_ucred->cr_ruid;
	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_getegid16(struct linux_getegid16_args *args)
{
	struct getegid_args bsd;
	int error;

	bsd.sysmsg_result = 0;

	error = sys_getegid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_geteuid16(struct linux_geteuid16_args *args)
{
	struct geteuid_args bsd;
	int error;

	bsd.sysmsg_result = 0;

	error = sys_geteuid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_setgid16(struct linux_setgid16_args *args)
{
	struct setgid_args bsd;
	int error;

	bsd.gid = args->gid;
	bsd.sysmsg_result = 0;

	error = sys_setgid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_setuid16(struct linux_setuid16_args *args)
{
	struct setuid_args bsd;
	int error;

	bsd.uid = args->uid;
	bsd.sysmsg_result = 0;

	error = sys_setuid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_setregid16(struct linux_setregid16_args *args)
{
	struct setregid_args bsd;
	int error;

	bsd.rgid = CAST_NOCHG(args->rgid);
	bsd.egid = CAST_NOCHG(args->egid);
	bsd.sysmsg_result = 0;

	error = sys_setregid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_setreuid16(struct linux_setreuid16_args *args)
{
	struct setreuid_args bsd;
	int error;

	bsd.ruid = CAST_NOCHG(args->ruid);
	bsd.euid = CAST_NOCHG(args->euid);
	bsd.sysmsg_result = 0;

	error = sys_setreuid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_setresgid16(struct linux_setresgid16_args *args)
{
	struct setresgid_args bsd;
	int error;

	bsd.rgid = CAST_NOCHG(args->rgid);
	bsd.egid = CAST_NOCHG(args->egid);
	bsd.sgid = CAST_NOCHG(args->sgid);
	bsd.sysmsg_result = 0;

	error = sys_setresgid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_setresuid16(struct linux_setresuid16_args *args)
{
	struct setresuid_args bsd;
	int error;

	bsd.ruid = CAST_NOCHG(args->ruid);
	bsd.euid = CAST_NOCHG(args->euid);
	bsd.suid = CAST_NOCHG(args->suid);
	bsd.sysmsg_result = 0;

	error = sys_setresuid(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}
