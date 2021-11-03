/*
 * Copyright (c) 2016 Mellanox Technologies, Ltd.
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

#ifndef _ASM_ATOMIC_LONG_H_
#define _ASM_ATOMIC_LONG_H_

typedef struct {
        volatile long counter;
} atomic_long_t;

static inline void
atomic_long_set(atomic_long_t *v, long i)
{
        WRITE_ONCE(v->counter, i);
}

static inline long
atomic_long_xchg(atomic_long_t *v, long val)
{
        return atomic_swap_long(&v->counter, val);
}

static inline long
atomic_long_cmpxchg(atomic_long_t *v, long old, long new)
{
        long ret = old;

        for (;;) {
                if (atomic_fcmpset_long(&v->counter, &ret, new))
                        break;
                if (ret != old)
                        break;
        }
        return (ret);
}

static inline int
atomic_long_add_unless(atomic64_t *v, long a, long u)
{
	long c = atomic64_read(v);

	for (;;) {
		if (unlikely(c == u))
			break;
		if (likely(atomic_fcmpset_long(&v->counter, &c, c + a)))
			break;
	}
	return (c != u);
}

#define atomic_long_inc_not_zero(v)	atomic_long_add_unless((v), 1, 0)

#endif	/* _ASM_ATOMIC_LONG_H_ */
