/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/imgact_aout.h>
#include <sys/imgact_elf.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/mplock2.h>
#include <sys/malloc.h>
#include <sys/ptrace.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <sys/exec.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/cpu.h>

#include "i386/linux.h"
#include "i386/linux_proto.h"
#include "linux_signal.h"
#include "linux_util.h"
#include "linux_emuldata.h"


struct lock emul_lock;

struct linux_emuldata *
emuldata_get(struct proc *p)
{
	struct linux_emuldata *em;

	EMUL_LOCK();

	em = p->p_emuldata;

	EMUL_UNLOCK();
	return (em);
}

void
emuldata_set_robust(struct proc *p, struct linux_robust_list_head *robust_ftx)
{
	struct linux_emuldata *em;

	EMUL_LOCK();

	em = emuldata_get(p);
	KKASSERT(em != NULL);

	em->robust_futexes = robust_ftx;
	EMUL_UNLOCK();
}

int
emuldata_init(struct proc *p, struct proc *pchild, int flags)
{
	struct linux_emuldata_shared *s;
	struct linux_emuldata *em, *ep;
	int error = 0;

	EMUL_LOCK();

	em = emuldata_get(p);

	if (pchild == NULL) {
		ep = NULL;
		/* This is the execv* case, where a process gets overwritten */
		KKASSERT(em != NULL);
		KKASSERT(em->s != NULL);
		if (atomic_fetchadd_int(&em->s->refs, -1) == 1) {
			kfree(em->s, M_LINUX); 
			em->s = NULL;
		}
		if (em->s)
			KKASSERT(em->s->refs >= 0);
		
		em->parent_tidptr = NULL;
		em->child_tidptr = NULL;
		em->clone_flags = 0;
		em->clear_tid = NULL;
		em->set_tls = NULL;
		em->proc = p;
	} else {
		ep = em;
		em = kmalloc(sizeof(*em), M_LINUX, M_WAITOK | M_ZERO);
	}

	if (flags & LINUX_CLONE_THREAD) {
		/*
		 * If CLONE_THREAD is set, the child is placed in the same
		 * thread group as the calling process.
		 */
		KKASSERT(ep != NULL);
		em->s = ep->s;
		s = em->s;
	} else {
		/* new thread group */
		s = kmalloc(sizeof(*s), M_LINUX, M_WAITOK | M_ZERO);
		LIST_INIT(&s->threads);
		if (pchild)
			s->group_pid = pchild->p_pid;
		else
			s->group_pid = p->p_pid;
	}

	if (ep != NULL) {
		em->parent_tidptr = ep->parent_tidptr;
		em->child_tidptr = ep->child_tidptr;
#if 0
		em->clone_flags = ep->clone_flags;
#endif
	}

	em->clone_flags = flags;

	atomic_add_int(&s->refs, 1);
	KKASSERT(s->refs >= 0);
	em->s = s;
	LIST_INSERT_HEAD(&s->threads, em, threads);

	
	if (pchild != NULL) {
		em->proc = pchild;
		pchild->p_emuldata = em;
	}

	EMUL_UNLOCK();
	return (error);
}

/* emuldata_exit is modelled after NetBSD's */
void
emuldata_exit(void *unused, struct proc *p)
{
	struct linux_sys_futex_args cup;
	struct linux_emuldata *em;
	int error = 0;

	if (__predict_true(p->p_sysent != &elf_linux_sysvec))
		return;

	release_futexes(p);
	EMUL_LOCK();

	em = emuldata_get(p);
	if (em == NULL) {
		EMUL_UNLOCK();
		return;
	}

	LIST_REMOVE(em, threads);
	p->p_emuldata = NULL;

	/*
	 * Members of the thread groups others than the leader should
	 * exit quietely: no zombie stage, no signal. We do that by
	 * reparenting to init. init will collect us and nobody will
	 * notice what happened.
	 */
	if ((em->s->group_pid != p->p_pid) &&
	    (em->clone_flags & LINUX_CLONE_THREAD)) {
		p->p_sigparent = SIGCHLD;

		proc_reparent(p, initproc);
		wakeup((caddr_t)initproc); /* kern_exit seems to do this */
	}

	if ((em->s->group_pid == p->p_pid) &&
	    (em->s->flags & LINUX_LES_INEXITGROUP)) {
		p->p_xstat = em->s->xstat;
	}

	if (atomic_fetchadd_int(&em->s->refs, -1) == 1) {
		kfree(em->s, M_LINUX);
		em->s = NULL;
	}
	if (em->s)
		KKASSERT(em->s->refs >= 0);

	EMUL_UNLOCK();

	if (em->clear_tid != NULL) {
		int tid = 0;
		copyout(&tid, em->clear_tid, sizeof(tid));
		cup.uaddr = em->clear_tid;
		cup.op = LINUX_FUTEX_WAKE;
		cup.val = 0x7fffffff;	/* Awake everyone */
		cup.timeout = NULL;
		cup.uaddr2 = NULL;
		cup.val3 = 0;
		error = sys_linux_sys_futex(&cup);
		if (error)
			kprintf("emuldata_exit futex stuff failed miserably\n");
	}

	kfree(em, M_LINUX);
}

void
linux_proc_transition(void *unused, struct image_params *imgp)
{
	struct proc *p;

	p = imgp->proc;
	if (__predict_false(imgp->proc->p_sysent == &elf_linux_sysvec &&
	    imgp->proc->p_emuldata == NULL)) {
#ifdef LINUX_DEBUG
		kprintf("timidly hello from proc_transition\n");
#endif
		emuldata_init(p, p, 0);
	}
}

static void
linux_proc_userret(void)
{
	struct proc *p = curproc;
	struct linux_emuldata *em;

	em = emuldata_get(p);
	KKASSERT(em != NULL);

	if (em->clone_flags & LINUX_CLONE_CHILD_SETTID) {
		copyout(&p->p_pid, (int *)em->child_tidptr,
 		    sizeof(p->p_pid));
	}

	return;
}

void
linux_proc_fork(struct proc *p, struct proc *parent, void *child_tidptr)
{
	struct linux_emuldata *em;

	em = emuldata_get(p);
	KKASSERT(em != NULL);

	if (child_tidptr != NULL)
		em->child_tidptr = child_tidptr;

	/* LINUX_CLONE_CHILD_CLEARTID: clear TID in child's memory on exit() */
	if (em->clone_flags & LINUX_CLONE_CHILD_CLEARTID)
		em->clear_tid = em->child_tidptr;

	if (em->clone_flags & LINUX_CLONE_CHILD_SETTID)
		p->p_userret = linux_proc_userret;

	return;
}

int
sys_linux_set_tid_address(struct linux_set_tid_address_args *args)
{
	struct linux_emuldata *em;

	EMUL_LOCK();

	em = emuldata_get(curproc);
	KKASSERT(em != NULL);

	em->clear_tid = args->tidptr;
	args->sysmsg_iresult = curproc->p_pid;

	EMUL_UNLOCK();
	return 0;
}
