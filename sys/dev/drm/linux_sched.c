/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
#include <sys/cdefs.h>

#include <sys/condvar.h>
#include <sys/queue.h>
#include <sys/lock.h>

#include <linux/compiler.h>

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/kref.h>
#include <linux/dma-fence.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/*
 * Called when curthread->td_linux_task is NULL.  We must allocated, initialize,
 * and install a task_struct in td (the current thread).
 *
 * All threads belonging to the same process have a common mm_struct which
 * is stored as p->p_linux_mm.  This must be allocated, initialized, and
 * and installed if necessary.
 */
struct task_struct *
linux_task_alloc(struct thread *td)
{
	struct task_struct *task;
	struct mm_struct *mm;
	struct proc *p;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	task->dfly_td = td;

	if ((p = td->td_proc) != NULL) {
		if ((mm = p->p_linux_mm) == NULL) {
			mm = kzalloc(sizeof(*mm), GFP_KERNEL);
			mm->refs = 1;
			lockinit(&mm->mmap_sem, "drmmms", 0, LK_CANRECURSE);
			lwkt_gettoken(&p->p_token);
			if (p->p_linux_mm == NULL) {
				p->p_linux_mm = mm;
			} else {
				linux_mm_drop(mm);
				mm = p->p_linux_mm;
			}
			lwkt_reltoken(&p->p_token);
		}
		task->mm = mm;
		atomic_add_long(&mm->refs, 1);
	}
	td->td_linux_task = task;

	return task;
}

/*
 * Called at thread exit
 */
void
linux_task_drop(struct thread *td)
{
	struct task_struct *task;
	struct mm_struct *mm;

	task = td->td_linux_task;
	td->td_linux_task = NULL;
	if ((mm = task->mm) != NULL) {
		atomic_add_long(&mm->refs, -1);	/* proc ref always remains */
		task->mm = NULL;
	}
	kfree(task);
}

void
linux_proc_drop(struct proc *p)
{
	struct mm_struct *mm;

	if ((mm = p->p_linux_mm) != NULL) {
		p->p_linux_mm = NULL;
		linux_mm_drop(mm);
	}
}

void
linux_mm_drop(struct mm_struct *mm)
{
	long refs;

	refs = atomic_fetchadd_long(&mm->refs, -1);
	KKASSERT(refs > 0);
	if (refs == 1) {
		lockuninit(&mm->mmap_sem);
		kfree(mm);
	}
}
