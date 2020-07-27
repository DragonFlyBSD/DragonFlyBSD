/*
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
 * Copyright (c) 2019 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef	_LINUX_SCHED_H_
#define	_LINUX_SCHED_H_

#include <linux/capability.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/rbtree.h>
#include <linux/thread_info.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/preempt.h>

#include <asm/page.h>

#include <linux/smp.h>
#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>

#include <linux/time.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/llist.h>
#include <linux/gfp.h>

#include <asm/processor.h>

#include <linux/spinlock.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal2.h>

#include <machine/cpu.h>

struct seq_file;

#define	TASK_RUNNING		0
#define	TASK_INTERRUPTIBLE	1
#define	TASK_UNINTERRUPTIBLE	2

#define TASK_NORMAL		(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

#define MAX_SCHEDULE_TIMEOUT    LONG_MAX

#define TASK_COMM_LEN	MAXCOMLEN

struct task_struct {
	struct thread *dfly_td;
	volatile long state;
	struct mm_struct *mm;	/* mirror copy in p->p_linux_mm */
	int prio;

	/* kthread-specific data */
	unsigned long		kt_flags;
	int			(*kt_fn)(void *data);
	void			*kt_fndata;
	int			kt_exitvalue;

	/* executable name without path */
	char			comm[TASK_COMM_LEN];
};

#define __set_current_state(state_value)	current->state = (state_value);

#define set_current_state(state_value)		\
do {						\
	__set_current_state(state_value);	\
	mb();					\
} while (0)

/*
 * schedule_timeout: puts the current thread to sleep until timeout
 * if its state allows it to.
 */
static inline long
schedule_timeout(signed long timeout)
{
	unsigned long time_before, time_after;
	long slept, ret = 0;
	int timo;

	if (timeout < 0) {
		kprintf("schedule_timeout(): timeout cannot be negative\n");
		goto done;
	}

	/*
	 * Indefinite wait if timeout is MAX_SCHEDULE_TIMEOUT, but we are
	 * also translating to an integer.  The first conditional will
	 * cover both but to code defensively test both.
	 */
	if (timeout >= INT_MAX || timeout == MAX_SCHEDULE_TIMEOUT)
		timo = 0;
	else
		timo = timeout;

	switch (current->state) {
	case TASK_INTERRUPTIBLE:
		time_before = ticks;
		tsleep(current, PCATCH, "lstim", timo);
		time_after = ticks;
		slept = time_after - time_before;
		ret = timeout - slept;
		if (ret < 0)
			ret = 0;
		break;
	case TASK_UNINTERRUPTIBLE:
		tsleep(current, 0, "lstim", timo);
		break;
	default:
		break;
	}

done:
	if (timeout == MAX_SCHEDULE_TIMEOUT)
		ret = MAX_SCHEDULE_TIMEOUT;

	current->state = TASK_RUNNING;
	return ret;
}

static inline void
schedule(void)
{
	(void)schedule_timeout(MAX_SCHEDULE_TIMEOUT);
}

static inline signed long
schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}

static inline long
io_schedule_timeout(signed long timeout)
{
	return schedule_timeout(timeout);
}

/*
 * local_clock: fast time source, monotonic on the same cpu
 */
static inline uint64_t
local_clock(void)
{
	struct timespec ts;

	getnanouptime(&ts);
	return (ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
}

static inline void
yield(void)
{
	lwkt_yield();
}

static inline int
wake_up_process(struct task_struct *tsk)
{
	/* Among other things, this function is supposed to act as
	 * a barrier */
	smp_wmb();
	wakeup(tsk);

	return 1;	/* Always indicate the process was woken up */
}

static inline int
signal_pending(struct task_struct *p)
{
	struct thread *t = p->dfly_td;

	/* Some kernel threads do not have lwp, t->td_lwp can be NULL */
	if (t->td_lwp == NULL)
		return 0;

	return CURSIG(t->td_lwp);
}

static inline int
fatal_signal_pending(struct task_struct *p)
{
	struct thread *t = p->dfly_td;
	sigset_t pending_set;

	/* Some kernel threads do not have lwp, t->td_lwp can be NULL */
	if (t->td_lwp == NULL)
		return 0;

	pending_set = lwp_sigpend(t->td_lwp);
	return SIGISMEMBER(pending_set, SIGKILL);
}

static inline int
signal_pending_state(long state, struct task_struct *p)
{
	if (state & TASK_INTERRUPTIBLE)
		return (signal_pending(p));
	else
		return (fatal_signal_pending(p));
}

/* Explicit rescheduling in order to reduce latency */
static inline int
cond_resched(void)
{
	lwkt_yield();
	return 0;
}

static inline int
send_sig(int sig, struct proc *p, int priv)
{
	ksignal(p, sig);
	return 0;
}

static inline void
set_need_resched(void)
{
	/* do nothing for now */
	/* used on ttm_bo_reserve failures */
}

static inline bool
need_resched(void)
{
	return any_resched_wanted();
}

static inline int
sched_setscheduler_nocheck(struct task_struct *ts,
			   int policy, const struct sched_param *param)
{
	/* We do not allow different thread scheduling policies */
	return 0;
}

static inline int
pagefault_disabled(void)
{
	return (curthread->td_flags & TDF_NOFAULT);
}

static inline void
mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}

#endif	/* _LINUX_SCHED_H_ */
