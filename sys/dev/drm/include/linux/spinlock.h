/*
 * Copyright (c) 2015-2017 François Tigeot
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

#include <sys/spinlock2.h>
#include <sys/lock.h>

#include <linux/irqflags.h>
#include <asm/barrier.h>

#define spin_is_locked(x)	spin_held(x)

#define assert_spin_locked(x)	KKASSERT(lockcountnb(x))

/*
 * The spin_lock_irq() family of functions stop hardware interrupts
 * from being delivered to the local CPU.
 * A crit_enter()/crit_exit() sequence does the same thing on the
 * DragonFly kernel
 */
static inline void spin_lock_irq(struct lock *lock)
{
	crit_enter();
	lockmgr(lock, LK_EXCLUSIVE);
}

static inline void spin_unlock_irq(struct lock *lock)
{
	lockmgr(lock, LK_RELEASE);
	crit_exit();
}

#define spin_lock_irqsave(lock, flags)		do { flags = 0; spin_lock_irq(lock); } while(0)
#define spin_unlock_irqrestore(lock, flags)	do { flags = 0; spin_unlock_irq(lock); } while(0)

static inline void
spin_lock_bh(struct lock *lock)
{
	spin_lock_irq(lock);
}

static inline void
spin_unlock_bh(struct lock *lock)
{
	spin_unlock_irq(lock);
}

#endif	/* _LINUX_SPINLOCK_H_ */
