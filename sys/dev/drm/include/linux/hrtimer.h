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
#ifndef _LINUX_HRTIMER_H_
#define _LINUX_HRTIMER_H_

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/timer.h>

enum hrtimer_mode {
	HRTIMER_MODE_ABS = 0x0,
	HRTIMER_MODE_REL = 0x1,
};

enum hrtimer_restart {
	HRTIMER_NORESTART,	/* Timer is not restarted */
	HRTIMER_RESTART,	/* Timer must be restarted */
};

struct hrtimer {
	struct callout timer_callout;
	clockid_t 		clock_id;
	enum hrtimer_mode	ht_mode;
	bool active;
	enum hrtimer_restart	(*function)(struct hrtimer *);
	struct lwkt_token timer_token;
};

extern void hrtimer_init(struct hrtimer *timer, clockid_t which_clock,
			 enum hrtimer_mode mode);

extern void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
				   u64 range_ns, const enum hrtimer_mode mode);

extern int hrtimer_cancel(struct hrtimer *timer);

extern bool hrtimer_active(const struct hrtimer *timer);

#endif /* _LINUX_HRTIMER_H_ */
