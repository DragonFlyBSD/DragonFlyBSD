/*
 * Copyright (c) 2017-2019 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/hrtimer.h>
#include <linux/bug.h>

#include <sys/systimer.h>
#include <sys/taskqueue.h>

static void
__hrtimer_function(systimer_t info, int in_ipi, struct intrframe *frame)
{
	struct hrtimer *timer = info->data;
	struct taskqueue *tq = taskqueue_thread[mycpuid];

	BUG_ON(taskqueue_enqueue(tq, &timer->task) != 0);
}

static void
__hrtimer_task(void *arg, int pending)
{
	struct hrtimer *timer = arg;
	enum hrtimer_restart restart;

	lwkt_gettoken(&timer->timer_token);
	timer->running = true;
	if (timer->cancel) {
		timer->running = false;
		timer->active = false;
		goto done;
	}
	lwkt_reltoken(&timer->timer_token);
	KKASSERT(timer->function != NULL);
	restart = timer->function(timer);
	lwkt_gettoken(&timer->timer_token);
	timer->running = false;
	timer->active = false;
	if (!timer->cancel && restart == HRTIMER_RESTART) {
		KKASSERT(timer->is_rel);
		timer->active = true;
		systimer_init_oneshot(&timer->st, __hrtimer_function,
		    timer, timer->timeout_us);
	}
done:
	lwkt_reltoken(&timer->timer_token);
}

void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	BUG_ON(clock_id != CLOCK_MONOTONIC);

	memset(timer, 0, sizeof(struct hrtimer));
	timer->clock_id = clock_id;
	timer->ht_mode = mode;
	timer->active = false;
	timer->running = false;
	timer->cancel = false;
	timer->gd = NULL;
	TASK_INIT(&timer->task, 1, __hrtimer_task, timer);
	lwkt_token_init(&timer->timer_token, "timer token");
}

void
hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
		       u64 range_ns, const enum hrtimer_mode mode)
{
	if (mode == HRTIMER_MODE_ABS) {
		struct timespec ts;
		ktime_t curtime;

		timer->is_rel = false;
		nanouptime(&ts);
		curtime = ts.tv_sec * 1000000000 + ts.tv_nsec;
		if (curtime > tim)
			timer->timeout_us = 500;
		else
			timer->timeout_us = (tim - curtime) / 1000;
	} else {
		timer->timeout_us = tim / 1000;
		timer->is_rel = true;
	}
	/* Prevent arbitrarily short timeouts from being scheduled. */
	timer->timeout_us = max(500, timer->timeout_us);

	lwkt_gettoken(&timer->timer_token);

	timer->cancel = false;
	timer->running = false;
	timer->active = true;
	timer->gd = mycpu;
	systimer_init_oneshot(&timer->st, __hrtimer_function, timer,
	    timer->timeout_us);

	lwkt_reltoken(&timer->timer_token);
}

int
hrtimer_cancel(struct hrtimer *timer)
{
	int ret = 0;

	lwkt_gettoken(&timer->timer_token);
	if (timer->active) {
		struct taskqueue *tq = taskqueue_thread[timer->gd->gd_cpuid];
		bool running = timer->running;

		ret = 1;
		timer->cancel = true;
		lwkt_reltoken(&timer->timer_token);
		if (running) {
			/* Nothing */
		} else if (timer->gd == mycpu) {
			systimer_del(&timer->st);
		} else {
			struct globaldata *oldcpu = mycpu;
			lwkt_setcpu_self(timer->gd);
			systimer_del(&timer->st);
			lwkt_setcpu_self(oldcpu);
		}
		while (taskqueue_cancel(tq, &timer->task, NULL))
			taskqueue_drain(tq, &timer->task);
	} else {
		lwkt_reltoken(&timer->timer_token);
	}
	return ret;
}

/* Returns non-zero if the timer is queued or running */
bool
hrtimer_active(const struct hrtimer *const_timer)
{
	bool ret;
	struct hrtimer *timer = __DECONST(struct hrtimer *, const_timer);

	lwkt_gettoken(&timer->timer_token);
	ret = timer->active;
	lwkt_reltoken(&timer->timer_token);
	return ret;
}
