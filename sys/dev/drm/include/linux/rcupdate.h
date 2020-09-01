/*
 * Copyright (c) 2017-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_RCUPDATE_H_
#define _LINUX_RCUPDATE_H_

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>
#include <linux/lockdep.h>
#include <linux/completion.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/ktime.h>

#include <asm/barrier.h>

#include <linux/rcutree.h>

static inline void
rcu_read_lock(void)
{
	preempt_disable();
}

static inline void
rcu_read_unlock(void)
{
	preempt_enable();
}

#define rcu_dereference_protected(p, condition)	\
	((typeof(*p) *)(p))

#define rcu_dereference(p)					\
({								\
	typeof(*(p)) *__rcu_dereference_tmp = READ_ONCE(p);	\
	__rcu_dereference_tmp;					\
})

#define rcu_dereference_raw(p)			\
	((__typeof(*p) *)READ_ONCE(p))

#define rcu_assign_pointer(p, v)	\
do {					\
	cpu_mfence();			\
	WRITE_ONCE((p), (v));		\
} while (0)

#define RCU_INIT_POINTER(p, v)		\
do {					\
	p = v;				\
} while (0)

extern void kfree_rcu(void *ptr, void *rcu_head __unused);
extern void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *));

#define rcu_access_pointer(p)	((typeof(*p) *)READ_ONCE(p))

#define rcu_pointer_handoff(p)	(p)

#define synchronize_rcu()

#endif  /* _LINUX_RCUPDATE_H_ */
