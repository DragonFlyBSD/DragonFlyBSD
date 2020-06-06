/*	$NetBSD: fence.h,v 1.15 2018/08/27 14:20:41 riastradh Exp $	*/

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
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

#ifndef _LINUX_DMA_FENCE_H_
#define _LINUX_DMA_FENCE_H_

#include <linux/err.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/kref.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>

#include <sys/types.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/queue.h>

struct dma_fence_cb;

struct dma_fence {
	struct kref		refcount;
	struct lock		*lock;
	volatile unsigned long	flags;
	u64			context;
	unsigned		seqno;
	const struct dma_fence_ops	*ops;

	TAILQ_HEAD(, dma_fence_cb)	f_callbacks;
	struct cv		f_cv;
	struct rcu_head		rcu;
	int error;
};

#define	DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT	0
#define	DMA_FENCE_FLAG_SIGNALED_BIT		1
#define	DMA_FENCE_FLAG_USER_BITS		2

struct dma_fence_ops {
	const char	*(*get_driver_name)(struct dma_fence *);
	const char	*(*get_timeline_name)(struct dma_fence *);
	bool		(*enable_signaling)(struct dma_fence *);
	bool		(*signaled)(struct dma_fence *);
	long		(*wait)(struct dma_fence *, bool, long);
	void		(*release)(struct dma_fence *);
	void	(*fence_value_str)(struct dma_fence *fence, char *str, int size);
	void	(*timeline_value_str)(struct dma_fence *fence, char *str, int size);
};

typedef void (*fence_func_t)(struct dma_fence *, struct dma_fence_cb *);

struct dma_fence_cb {
	struct list_head node;
	fence_func_t		func;
	TAILQ_ENTRY(dma_fence_cb) fcb_entry;
	bool			fcb_onqueue;
};

extern int	linux_fence_trace;

void dma_fence_init(struct dma_fence *, const struct dma_fence_ops *,
		struct lock*, u64 context, unsigned seqno);

void	dma_fence_destroy(struct dma_fence *);
void	dma_fence_free(struct dma_fence *);

u64 dma_fence_context_alloc(unsigned num);

bool	dma_fence_is_later(struct dma_fence *, struct dma_fence *);

struct dma_fence * dma_fence_get(struct dma_fence *);
struct dma_fence * dma_fence_get_rcu(struct dma_fence *);
void	dma_fence_put(struct dma_fence *);

int	dma_fence_add_callback(struct dma_fence *, struct dma_fence_cb *, fence_func_t);
bool	dma_fence_remove_callback(struct dma_fence *, struct dma_fence_cb *);
void	dma_fence_enable_sw_signaling(struct dma_fence *);

bool	dma_fence_is_signaled(struct dma_fence *);
bool	dma_fence_is_signaled_locked(struct dma_fence *);
int	dma_fence_signal(struct dma_fence *);
int	dma_fence_signal_locked(struct dma_fence *);
long	dma_fence_default_wait(struct dma_fence *, bool, long);
long	dma_fence_wait(struct dma_fence *, bool);
long	dma_fence_wait_any_timeout(struct dma_fence **, uint32_t, bool, long);
long	dma_fence_wait_timeout(struct dma_fence *, bool, long);

static inline void
DMA_FENCE_TRACE(struct dma_fence *f, const char *fmt, ...)
{
	va_list va;

	if (__predict_false(linux_fence_trace)) {
		va_start(va, fmt);
		kprintf("fence %llu@%u: ", f->context, f->seqno);
		kvprintf(fmt, va);
		va_end(va);
	}
}

static inline void
dma_fence_set_error(struct dma_fence *fence, int error)
{
	fence->error = error;
}

#endif	/* _LINUX_DMA_FENCE_H_ */
