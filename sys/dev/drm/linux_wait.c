/*
 * Copyright (c) 2019-2020 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/wait.h>
#include <linux/sched.h>

int
default_wake_function(wait_queue_t *q, unsigned mode, int wake_flags, void *key)
{
	return wake_up_process(q->private);
}

int
autoremove_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	int ret = default_wake_function(wait, mode, sync, key);

	/* Was the process woken up ? */
	if (ret)
		list_del_init(&wait->task_list);

	return ret;
}

void
__wake_up_core(wait_queue_head_t *q, int num_to_wake_up)
{
	wait_queue_t *curr, *next;
	int mode = TASK_NORMAL;

	list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
		if (curr->func(curr, mode, 0, NULL))
			num_to_wake_up--;

		if (num_to_wake_up == 0)
			break;
	}
}

void
__wait_event_prefix(wait_queue_head_t *wq, int flags)
{
	lockmgr(&wq->lock, LK_EXCLUSIVE);
	if (flags & PCATCH) {
		set_current_state(TASK_INTERRUPTIBLE);
	} else {
		set_current_state(TASK_UNINTERRUPTIBLE);
	}
	lockmgr(&wq->lock, LK_RELEASE);
}

void
prepare_to_wait(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	lockmgr(&q->lock, LK_EXCLUSIVE);
	if (list_empty(&wait->task_list))
		__add_wait_queue(q, wait);
	set_current_state(state);
	lockmgr(&q->lock, LK_RELEASE);
}

void
finish_wait(wait_queue_head_t *q, wait_queue_t *wait)
{
	set_current_state(TASK_RUNNING);

	lockmgr(&q->lock, LK_EXCLUSIVE);
	if (!list_empty(&wait->task_list))
		list_del_init(&wait->task_list);
	lockmgr(&q->lock, LK_RELEASE);
}

void
wake_up_bit(void *addr, int bit)
{
	wakeup_one(addr);
}

/* Wait for a bit to be cleared or a timeout to expire */
int
wait_on_bit_timeout(unsigned long *word, int bit, unsigned mode,
		    unsigned long timeout)
{
	int rv, awakened = 0, timeout_expired = 0;
	long start_time;

	if (!test_bit(bit, word))
		return 0;

	start_time = ticks;
	set_current_state(mode);

	do {
		rv = tsleep(word, mode, "lwobt", timeout);
		if (rv == 0)
			awakened = 1;
		if (time_after_eq(start_time, timeout))
			timeout_expired = 1;
	} while (test_bit(bit, word) && !timeout_expired);

	set_current_state(TASK_RUNNING);

	if (awakened)
		return 0;

	return 1;
}

void __init_waitqueue_head(wait_queue_head_t *q,
			   const char *name, struct lock_class_key *key)
{
	lockinit(&q->lock, "lwq", 0, 0);
	INIT_LIST_HEAD(&q->task_list);
}
