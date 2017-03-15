/*
 * Copyright (c) 2017 François Tigeot
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

static inline void
__hrtimer_function(void *arg)
{
	struct hrtimer *timer = arg;
	enum hrtimer_restart restart = HRTIMER_RESTART;

	if (timer->function) {
		restart = timer->function(timer);
	}

	if (restart == HRTIMER_NORESTART)
		timer->function = NULL;
}

void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	BUG_ON(clock_id != CLOCK_MONOTONIC);

	memset(timer, 0, sizeof(struct hrtimer));
	timer->clock_id = clock_id;
	timer->ht_mode = mode;

	lwkt_token_init(&timer->timer_token, "timer token");
	callout_init_mp(&(timer)->timer_callout);
}

void
hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
		       u64 range_ns, const enum hrtimer_mode mode)
{
	int expire_ticks = tim.tv64 / (NSEC_PER_SEC / hz);

	if (mode == HRTIMER_MODE_ABS)
		expire_ticks -= ticks;

	if (expire_ticks <= 0)
		expire_ticks = 1;

	lwkt_gettoken(&timer->timer_token);

	timer->active = true;
	callout_reset(&timer->timer_callout,
		      expire_ticks, __hrtimer_function, timer);

	lwkt_reltoken(&timer->timer_token);
}

int
hrtimer_cancel(struct hrtimer *timer)
{
	return callout_drain(&timer->timer_callout) == 0;
}

/* Returns non-zero if the timer is already on the queue */
bool
hrtimer_active(const struct hrtimer *timer)
{
	return callout_pending(&timer->timer_callout);
}
