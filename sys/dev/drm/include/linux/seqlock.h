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

#ifndef _LINUX_SEQLOCK_H_
#define _LINUX_SEQLOCK_H_

#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>
#include <asm/processor.h>

/*
 * Seqlock definition from Wikipedia
 *
 * A seqlock consists of storage for saving a sequence number in addition to a lock.
 * The lock is to support synchronization between two writers and the counter is for
 * indicating consistency in readers. In addition to updating the shared data, the
 * writer increments the sequence number, both after acquiring the lock and before
 * releasing the lock. Readers read the sequence number before and after reading the
 * shared data. If the sequence number is odd on either occasion, a writer had taken
 * the lock while the data was being read and it may have changed. If the sequence
 * numbers are different, a writer has changed the data while it was being read. In
 * either case readers simply retry (using a loop) until they read the same even
 * sequence number before and after.
 *
 */

typedef struct {
	unsigned sequence;
	struct lock lock;
} seqlock_t;

static inline void
seqlock_init(seqlock_t *sl)
{
	sl->sequence = 0;
	lockinit(&sl->lock, "lsql", 0, 0);
}

/*
 * Writers always use a spinlock. We still use store barriers
 * in order to quickly update the state of the sequence variable
 * for readers.
 */
static inline void
write_seqlock(seqlock_t *sl)
{
	lockmgr(&sl->lock, LK_EXCLUSIVE);
	sl->sequence++;
	cpu_sfence();
}

static inline void
write_sequnlock(seqlock_t *sl)
{
	sl->sequence--;
	lockmgr(&sl->lock, LK_RELEASE);
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

typedef struct seqcount {
	unsigned sequence;
} seqcount_t;

static inline void
__seqcount_init(seqcount_t *s, const char *name, struct lock_class_key *key)
{
	s->sequence = 0;
}

static inline unsigned int
__read_seqcount_begin(const seqcount_t *s)
{
	unsigned int ret;

	do {
		ret = READ_ONCE(s->sequence);
		/* If the sequence number is odd, a writer has taken the lock */
		if ((ret & 1) == 0)
			break;
		cpu_pause();
	} while (1);

	return ret;
}

static inline unsigned int
read_seqcount_begin(const seqcount_t *s)
{
	unsigned int ret = __read_seqcount_begin(s);

	cpu_lfence();

	return ret;
}

static inline int
__read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	return (s->sequence != start);
}

static inline int
read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	cpu_lfence();
	return __read_seqcount_retry(s, start);
}

static inline void write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	cpu_ccfence();
}

static inline void write_seqcount_end(seqcount_t *s)
{
	cpu_ccfence();
	s->sequence++;
}

static inline unsigned int
raw_read_seqcount(const seqcount_t *s)
{
	unsigned int value = READ_ONCE(s->sequence);

	cpu_ccfence();

	return value;
}

#endif	/* _LINUX_SEQLOCK_H_ */
