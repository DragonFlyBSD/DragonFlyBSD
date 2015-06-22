/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2014,2015 François Tigeot
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
#ifndef _LINUX_TIMER_H_
#define _LINUX_TIMER_H_

#include <linux/types.h>

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/thread.h>

#include <linux/ktime.h>

struct timer_list {
	struct callout	timer_callout;
	void		(*function)(unsigned long);
	unsigned long	data;
	unsigned long	expires;
	struct lwkt_token timer_token;
};

static inline void
_timer_fn(void *context)
{
	struct timer_list *timer;

	timer = context;
	timer->function(timer->data);
}

#define	setup_timer(timer, func, dat)					\
do {									\
	(timer)->function = (func);					\
	(timer)->data = (dat);						\
	lwkt_token_init(&(timer)->timer_token, "timer token");	\
	callout_init_mp(&(timer)->timer_callout);			\
} while (0)

#define	init_timer(timer)						\
do {									\
	(timer)->function = NULL;					\
	(timer)->data = 0;						\
	lwkt_token_init(&(timer)->timer_token, "timer token");	\
	callout_init_mp(&(timer)->timer_callout);			\
} while (0)

#define	mod_timer(timer, exp)						\
do {									\
	(timer)->expires = (exp);					\
	lwkt_gettoken(&(timer)->timer_token);				\
	callout_reset(&(timer)->timer_callout, (exp) - jiffies,		\
	    _timer_fn, (timer));					\
	lwkt_reltoken(&(timer)->timer_token);				\
} while (0)

#define mod_timer_pinned(timer, exp)	mod_timer(timer, exp)

#define	add_timer(timer)						\
	lwkt_gettoken(&(timer)->timer_token);				\
	callout_reset(&(timer)->timer_callout,				\
	    (timer)->expires - jiffies, _timer_fn, (timer));		\
	lwkt_reltoken(&(timer)->timer_token);

static inline void
del_timer(struct timer_list *timer)
{
	lwkt_gettoken(&(timer)->timer_token);
	callout_stop(&(timer)->timer_callout);
	lwkt_reltoken(&(timer)->timer_token);

	lwkt_token_uninit(&(timer)->timer_token);
}

#define	del_timer_sync(timer)	callout_drain(&(timer)->timer_callout)

#define	timer_pending(timer)	callout_pending(&(timer)->timer_callout)

static inline unsigned long
round_jiffies(unsigned long j)
{
	return roundup(j, hz);
}

static inline unsigned long
round_jiffies_up(unsigned long j)
{
	return roundup(j, hz);
}

static inline unsigned long
round_jiffies_up_relative(unsigned long j)
{
	return roundup(j, hz);
}

#define destroy_timer_on_stack(timer)

#endif /* _LINUX_TIMER_H_ */
