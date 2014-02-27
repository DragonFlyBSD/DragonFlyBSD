/* $FreeBSD: src/sys/kern/sysv_ipc.c,v 1.13.2.2 2000/07/01 14:33:49 bsd Exp $ */
/*	$NetBSD: sysv_ipc.c,v 1.7 1994/06/29 06:33:11 cgd Exp $	*/

/*
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@novatel.ca>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_sysvipc.h"

#include <sys/param.h>
#include <sys/ipc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/ucred.h>

#define SYSCALL_NOT_PRESENT_GEN(SC, str) \
int sys_##SC (struct SC##_args *uap) \
{ \
	sysv_nosys(str); \
	return sys_nosys((struct nosys_args *)uap); \
}

#if defined(SYSVSEM) || defined(SYSVSHM) || defined(SYSVMSG)

/*
 * Check for ipc permission
 */

int
ipcperm(struct proc *p, struct ipc_perm *perm, int mode)
{
	struct ucred *cred = p->p_ucred;

	/* Check for user match. */
	if (cred->cr_uid != perm->cuid && cred->cr_uid != perm->uid) {
		if (mode & IPC_M)
			return (priv_check_cred(cred, PRIV_ROOT, 0) == 0 ? 0 : EPERM);
		/* Check for group match. */
		mode >>= 3;
		if (!groupmember(perm->gid, cred) &&
		    !groupmember(perm->cgid, cred))
			/* Check for `other' match. */
			mode >>= 3;
	}

	if (mode & IPC_M)
		return (0);
	return ((mode & perm->mode) == mode || 
		priv_check_cred(cred, PRIV_ROOT, 0) == 0 ? 0 : EACCES);
}

#endif /* defined(SYSVSEM) || defined(SYSVSHM) || defined(SYSVMSG) */


#if !defined(SYSVSEM) || !defined(SYSVSHM) || !defined(SYSVMSG)

#include <sys/proc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/syslog.h>
#include <sys/sysproto.h>
#include <sys/systm.h>

static void sysv_nosys (char *s);

static void 
sysv_nosys(char *s)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;

	log(LOG_ERR, "cmd %s pid %d tried to use non-present %s\n",
			(p ? p->p_comm : td->td_comm),
			(p ? p->p_pid : -1), s);
}

#if !defined(SYSVSEM)

/*
 * SYSVSEM stubs
 */

SYSCALL_NOT_PRESENT_GEN(__semctl, "SYSVSEM");
SYSCALL_NOT_PRESENT_GEN(semget, "SYSVSEM");
SYSCALL_NOT_PRESENT_GEN(semop, "SYSVSEM");

/* called from kern_exit.c */
void
semexit(struct proc *p)
{
	/* empty */
}

#endif /* !defined(SYSVSEM) */


#if !defined(SYSVMSG)

/*
 * SYSVMSG stubs
 */

SYSCALL_NOT_PRESENT_GEN(msgctl, "SYSVMSG");
SYSCALL_NOT_PRESENT_GEN(msgget, "SYSVMSG");
SYSCALL_NOT_PRESENT_GEN(msgsnd, "SYSVMSG");
SYSCALL_NOT_PRESENT_GEN(msgrcv, "SYSVMSG");

#endif /* !defined(SYSVMSG) */


#if !defined(SYSVSHM)

/*
 * SYSVSHM stubs
 */

SYSCALL_NOT_PRESENT_GEN(shmdt, "SYSVSHM");
SYSCALL_NOT_PRESENT_GEN(shmat, "SYSVSHM");
SYSCALL_NOT_PRESENT_GEN(shmctl, "SYSVSHM");
SYSCALL_NOT_PRESENT_GEN(shmget, "SYSVSHM");

/* called from kern_fork.c */
void
shmfork(struct proc *p1, struct proc *p2)
{
	/* empty */
}

/* called from kern_exit.c */
void
shmexit(struct vmspace *vm)
{
	/* empty */
}

#endif /* !defined(SYSVSHM) */

#endif /* !defined(SYSVSEM) || !defined(SYSVSHM) || !defined(SYSVMSG) */
