/*
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_SPINLOCK_H_
#define _LINUX_SPINLOCK_H_

#include <linux/typecheck.h>
#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/stringify.h>
#include <linux/bottom_half.h>
#include <asm/barrier.h>

#include <linux/spinlock_types.h>

#include <linux/rwlock.h>

#include <sys/spinlock2.h>
#include <sys/lock.h>

#define spin_is_locked(x)	spin_held(x)

#define assert_spin_locked(x)	KKASSERT(lockinuse(x))

/*
 * The spin_lock_irq() family of functions stop hardware interrupts
 * from being delivered to the local CPU.
 */
static inline void spin_lock_irq(spinlock_t *lock)
{
	local_irq_disable();
	lockmgr(lock, LK_EXCLUSIVE);
}

static inline int spin_trylock_irq(spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	return lockmgr_try(lock, LK_EXCLUSIVE);
}

static inline void spin_unlock_irq(spinlock_t *lock)
{
	lockmgr(lock, LK_RELEASE);
	local_irq_enable();
}

#define spin_lock_irqsave(lock, flags)  \
({                                      \
        local_irq_save(flags);          \
        lockmgr(lock, LK_EXCLUSIVE);    \
})

static inline void
spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	lockmgr(lock, LK_RELEASE);
	local_irq_restore(flags);
}

/*
  XXX: the spin_lock_bh() and spin_unlock_bh() functions are possibly incorrect
  XXX: see also in_interrupt()
*/
static inline void
spin_lock_bh(struct lock *lock)
{
	crit_enter();
	lockmgr(lock, LK_EXCLUSIVE);
}

static inline void
spin_unlock_bh(struct lock *lock)
{
	lockmgr(lock, LK_RELEASE);
	crit_exit();
}

#define DEFINE_SPINLOCK(x)	struct lock x = LOCK_INITIALIZER("ds##x", 0, 0)

#define spin_lock_irqsave_nested(lock, flags, subclass)	\
	    spin_lock_irqsave(lock, flags)

#endif	/* _LINUX_SPINLOCK_H_ */
