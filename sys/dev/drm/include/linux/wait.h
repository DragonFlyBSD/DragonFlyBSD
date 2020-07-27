/*
 * Copyright (c) 2014 Imre Vadász
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_WAIT_H_
#define _LINUX_WAIT_H_

#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <asm/current.h>

typedef struct __wait_queue wait_queue_t;

typedef int (*wait_queue_func_t)(wait_queue_t *wait, unsigned mode, int flags, void *key);

int default_wake_function(wait_queue_t *wait, unsigned mode, int flags, void *key);
int autoremove_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key);

struct __wait_queue {
	unsigned int flags;
	void *private;
	wait_queue_func_t func;
	struct list_head task_list;
};

typedef struct {
	struct lock		lock;
	struct list_head	task_list;
} wait_queue_head_t;

void __init_waitqueue_head(wait_queue_head_t *q, const char *name, struct lock_class_key *);

static inline void
init_waitqueue_head(wait_queue_head_t *q)
{
	__init_waitqueue_head(q, "", NULL);
}

void __wake_up_core(wait_queue_head_t *q, int num_to_wake_up);

static inline void
wake_up(wait_queue_head_t *q)
{
	lockmgr(&q->lock, LK_EXCLUSIVE);
	__wake_up_core(q, 1);
	lockmgr(&q->lock, LK_RELEASE);
	wakeup_one(q);
}

static inline void
wake_up_all(wait_queue_head_t *q)
{
	lockmgr(&q->lock, LK_EXCLUSIVE);
	__wake_up_core(q, 0);
	lockmgr(&q->lock, LK_RELEASE);
	wakeup(q);
}

void wake_up_bit(void *, int);

#define wake_up_all_locked(eq)		__wake_up_core(eq, 0)

#define wake_up_interruptible(eq)	wake_up(eq)
#define wake_up_interruptible_all(eq)	wake_up_all(eq)

void __wait_event_prefix(wait_queue_head_t *wq, int flags);
void prepare_to_wait(wait_queue_head_t *q, wait_queue_t *wait, int state);
void finish_wait(wait_queue_head_t *q, wait_queue_t *wait);

/*
 * wait_event_interruptible_timeout:
 * - The process is put to sleep until the condition evaluates to true.
 * - The condition is checked each time the waitqueue wq is woken up.
 * - wake_up has to be called after changing any variable that could change
 * the result of the wait condition.
 *
 * returns:
 *   - 0 if the timeout elapsed
 *   - the remaining jiffies if the condition evaluated to true before
 *   the timeout elapsed.
 *   - remaining jiffies are always at least 1
 *   - -ERESTARTSYS if interrupted by a signal (when PCATCH is set in flags)
*/
#define __wait_event_common(wq, condition, timeout_jiffies, flags,	\
			    locked)					\
({									\
	int start_jiffies, elapsed_jiffies, remaining_jiffies, ret;	\
	bool timeout_expired = false;					\
	bool interrupted = false;					\
	long retval;							\
	int state;							\
	DEFINE_WAIT(tmp_wq);						\
									\
	start_jiffies = ticks;						\
	state = (flags & PCATCH) ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE; \
	prepare_to_wait(&wq, &tmp_wq, state);				\
									\
	while (1) {							\
		__wait_event_prefix(&wq, flags);			\
									\
		if (condition)						\
			break;						\
									\
		tsleep_interlock(current, flags);			\
									\
		if ((timeout_jiffies) != 0) {				\
			ret = tsleep(current, PINTERLOCKED|flags, "lwe", timeout_jiffies);	\
		} else {						\
			ret = tsleep(current, PINTERLOCKED|flags, "lwe", hz);\
			if (ret == EWOULDBLOCK) {			\
				kprintf("F");				\
				print_backtrace(-1);			\
				ret = 0;				\
			}						\
		}							\
									\
		if (ret == EINTR || ret == ERESTART) {			\
			interrupted = true;				\
			break;						\
		}							\
		if (ret == EWOULDBLOCK) {				\
			timeout_expired = true;				\
			break;						\
		}							\
	}								\
									\
	elapsed_jiffies = ticks - start_jiffies;			\
	remaining_jiffies = timeout_jiffies - elapsed_jiffies;		\
	if (remaining_jiffies <= 0)					\
		remaining_jiffies = 1;					\
									\
	if (timeout_expired)						\
		retval = 0;						\
	else if (interrupted)						\
		retval = -ERESTARTSYS;					\
	else if (timeout_jiffies > 0)					\
		retval = remaining_jiffies;				\
	else								\
		retval = 1;						\
									\
	finish_wait(&wq, &tmp_wq);					\
	retval;								\
})

#define wait_event(wq, condition)					\
		__wait_event_common(wq, condition, 0, 0, false)

#define wait_event_timeout(wq, condition, timeout)			\
		__wait_event_common(wq, condition, timeout, 0, false)

#define wait_event_interruptible(wq, condition)				\
({									\
	long retval;							\
									\
	retval = __wait_event_common(wq, condition, 0, PCATCH, false);	\
	if (retval != -ERESTARTSYS)					\
		retval = 0;						\
	retval;								\
})

#define wait_event_interruptible_locked(wq, condition)			\
({									\
	long retval;							\
									\
	retval = __wait_event_common(wq, condition, 0, PCATCH, true);	\
	if (retval != -ERESTARTSYS)					\
		retval = 0;						\
	retval;								\
})

#define wait_event_interruptible_timeout(wq, condition, timeout)	\
		__wait_event_common(wq, condition, timeout, PCATCH, false)

static inline int
waitqueue_active(wait_queue_head_t *q)
{
	return !list_empty(&q->task_list);
}

#define DEFINE_WAIT(name)					\
	wait_queue_t name = {					\
		.private = current,				\
		.task_list = LIST_HEAD_INIT((name).task_list),	\
		.func = autoremove_wake_function,		\
	}

#define DEFINE_WAIT_FUNC(name, _function)			\
	wait_queue_t name = {					\
		.private = current,				\
		.task_list = LIST_HEAD_INIT((name).task_list),	\
		.func = _function,				\
	}

static inline void
__add_wait_queue(wait_queue_head_t *head, wait_queue_t *new)
{
	list_add(&new->task_list, &head->task_list);
}

static inline void
add_wait_queue(wait_queue_head_t *head, wait_queue_t *wq)
{
	lockmgr(&head->lock, LK_EXCLUSIVE);
	__add_wait_queue(head, wq);
	lockmgr(&head->lock, LK_RELEASE);
}

#define DECLARE_WAIT_QUEUE_HEAD(name)					\
	wait_queue_head_t name = {					\
		.lock = LOCK_INITIALIZER("name", 0, LK_CANRECURSE),	\
		.task_list = { &(name).task_list, &(name).task_list }	\
	}

static inline void
__remove_wait_queue(wait_queue_head_t *head, wait_queue_t *old)
{
	list_del(&old->task_list);
}

static inline void
remove_wait_queue(wait_queue_head_t *head, wait_queue_t *wq)
{
	lockmgr(&head->lock, LK_EXCLUSIVE);
	__remove_wait_queue(head, wq);
	lockmgr(&head->lock, LK_RELEASE);
}

static inline void
__add_wait_queue_tail(wait_queue_head_t *wqh, wait_queue_t *wq)
{
	list_add_tail(&wq->task_list, &wqh->task_list);
}

int wait_on_bit_timeout(unsigned long *word, int bit,
			unsigned mode, unsigned long timeout);

#endif	/* _LINUX_WAIT_H_ */
