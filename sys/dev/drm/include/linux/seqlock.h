/*
 * Copyright (c) 2017 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_SEQLOCK_H_
#define _LINUX_SEQLOCK_H_

#include <linux/spinlock.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>

typedef struct {
	unsigned sequence;
	struct spinlock lock;
} seqlock_t;


static inline void
seqlock_init(seqlock_t *sl)
{
	sl->sequence = 0;
	spin_init(&sl->lock, "lsql");
}

/*
 * Writers always use a spinlock. We still use store barriers
 * in order to quickly update the state of the sequence variable
 * for readers.
 */
static inline void
write_seqlock(seqlock_t *sl)
{
	spin_lock(&sl->lock);
	sl->sequence++;
	cpu_sfence();
}

static inline void
write_sequnlock(seqlock_t *sl)
{
	sl->sequence--;
	spin_unlock(&sl->lock);
	cpu_sfence();
}

/*
 * Read functions are fully unlocked.
 * We use load barriers to obtain a reasonably up-to-date state
 * for the sequence number.
 */
static inline unsigned
read_seqbegin(const seqlock_t *sl)
{
	return READ_ONCE(sl->sequence);
}

static inline unsigned
read_seqretry(const seqlock_t *sl, unsigned start)
{
	cpu_lfence();
	return (sl->sequence != start);
}

#endif	/* _LINUX_SEQLOCK_H_ */
