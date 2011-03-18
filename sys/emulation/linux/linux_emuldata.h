/*	$NetBSD: linux_emuldata.h,v 1.16 2008/10/26 16:38:22 christos Exp $	*/

/*-
 * Copyright (c) 1998,2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Eric Haszlakiewicz.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "linux_futex.h"

#ifndef _SYS_LOCK_H
#include <sys/lock.h>
#endif

#ifndef _COMMON_LINUX_EMULDATA_H
#define _COMMON_LINUX_EMULDATA_H

/*
 * This is auxillary data the linux compat code
 * needs to do its work.  A pointer to it is
 * stored in the emuldata field of the proc
 * structure.
 */
struct linux_emuldata_shared {
	void *	p_break;	/* Processes' idea of break */
	int refs;
	pid_t group_pid;	/* PID of Linux process (group of threads) */
	/* List of Linux threads (NetBSD processes) in the Linux process */
	LIST_HEAD(, linux_emuldata) threads;
	int flags;		/* See below */
	int xstat;		/* Thread group exit code, for exit_group */
};

#define LINUX_LES_INEXITGROUP	0x1	/* thread group doing exit_group() */
#define LINUX_LES_USE_NPTL	0x2	/* Need to emulate NPTL threads */

struct linux_emuldata {
#if 0 /* notyet */
	sigset_t ps_siginfo;	/* Which signals have a RT handler */
#endif
	int	debugreg[8];	/* GDB information for ptrace - for use, */
				/* see ../arch/i386/linux_ptrace.c */
	struct linux_emuldata_shared *s;

	void *parent_tidptr;	/* Used during clone() */
	void *child_tidptr;	/* Used during clone() */
	int clone_flags;	/* Used during clone() */
	int flags;
	void *clear_tid;	/* Own TID to clear on exit */
	void *set_tls;		/* Pointer to child TLS desc in user space */

	struct linux_robust_list_head *robust_futexes;

	/* List of Linux threads (NetBSD processes) in the Linux process */
	LIST_ENTRY(linux_emuldata) threads;
	struct proc *proc;	/* backpointer to struct proc */
};

#define	EMUL_DIDKILL	0x01

#define LINUX_CHILD_QUIETEXIT	0x1	/* Child will have quietexit set */
#define LINUX_QUIETEXIT		0x2	/* Quiet exit (no zombie state) */

#define	EMUL_LOCKINIT(x)	lockinit(&emul_lock, "tux_emul", 0, LK_CANRECURSE)
#define	EMUL_LOCKUNINIT(x)	lockuninit(&emul_lock)

#define EMUL_LOCK(x)	lockmgr(&emul_lock, LK_EXCLUSIVE)
#define	EMUL_UNLOCK(x)	lockmgr(&emul_lock, LK_RELEASE)

extern struct lock emul_lock;

struct linux_emuldata *emuldata_get(struct proc *p);
void	emuldata_set_robust(struct proc *p, struct linux_robust_list_head *robust_ftx);
int	emuldata_init(struct proc *p, struct proc *pchild, int flags);
void	emuldata_exit(void *unused, struct proc *p);
void	linux_proc_transition(void *unused, struct image_params *imgp);
void	linux_proc_fork(struct proc *p, struct proc *parent, void *child_tidptr);
#endif /* !_COMMON_LINUX_EMULDATA_H */
