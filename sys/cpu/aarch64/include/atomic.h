/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CPU_ATOMIC_H_
#define _CPU_ATOMIC_H_

#include <sys/types.h>
#include <sys/atomic_common.h>

#define ATOMIC_LOAD_ACQ(type, p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ATOMIC_STORE_REL(type, p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

static __inline void
atomic_set_int(volatile u_int *p, u_int v)
{
	__atomic_fetch_or(p, v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_clear_int(volatile u_int *p, u_int v)
{
	__atomic_fetch_and(p, ~v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_add_int(volatile u_int *p, u_int v)
{
	__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_subtract_int(volatile u_int *p, u_int v)
{
	__atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_intr_init(__atomic_intr_t *p)
{
	*p = 0;
}

static __inline void
atomic_intr_cond_enter(__atomic_intr_t *p, void (*func)(void *), void *arg)
{
}

static __inline int
atomic_intr_cond_try(__atomic_intr_t *p)
{
	return (0);
}

static __inline int
atomic_intr_cond_test(__atomic_intr_t *p)
{
	return (0);
}

static __inline void
atomic_intr_cond_exit(__atomic_intr_t *p, void (*func)(void *), void *arg)
{
}

static __inline void
atomic_intr_cond_inc(__atomic_intr_t *p)
{
}

static __inline void
atomic_intr_cond_dec(__atomic_intr_t *p)
{
}

static __inline int
atomic_intr_handler_disable(__atomic_intr_t *p)
{
	return (0);
}

static __inline void
atomic_intr_handler_enable(__atomic_intr_t *p)
{
}

static __inline int
atomic_intr_handler_is_enabled(__atomic_intr_t *p)
{
	return (0);
}

static __inline void
atomic_set_short(volatile u_short *p, u_short v)
{
	__atomic_fetch_or(p, v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_clear_short(volatile u_short *p, u_short v)
{
	__atomic_fetch_and(p, (u_short)~v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_set_long(volatile u_long *p, u_long v)
{
	__atomic_fetch_or(p, v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_clear_long(volatile u_long *p, u_long v)
{
	__atomic_fetch_and(p, ~v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_add_long(volatile u_long *p, u_long v)
{
	__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}

static __inline void
atomic_subtract_long(volatile u_long *p, u_long v)
{
	__atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST);
}

#define	atomic_set_acq_int		atomic_set_int
#define	atomic_set_rel_int		atomic_set_int
#define	atomic_clear_acq_int		atomic_clear_int
#define	atomic_clear_rel_int		atomic_clear_int
#define	atomic_add_acq_int		atomic_add_int
#define	atomic_add_rel_int		atomic_add_int
#define	atomic_subtract_acq_int		atomic_subtract_int
#define	atomic_subtract_rel_int		atomic_subtract_int

#define	atomic_set_acq_long		atomic_set_long
#define	atomic_set_rel_long		atomic_set_long
#define	atomic_clear_acq_long		atomic_clear_long
#define	atomic_clear_rel_long		atomic_clear_long
#define	atomic_add_acq_long		atomic_add_long
#define	atomic_add_rel_long		atomic_add_long
#define	atomic_subtract_acq_long	atomic_subtract_long
#define	atomic_subtract_rel_long	atomic_subtract_long

#define	atomic_load_acq_64		atomic_load_acq_long
#define	atomic_store_rel_64		atomic_store_rel_long
#define	atomic_swap_64			atomic_swap_long
#define	atomic_fetchadd_64		atomic_fetchadd_long
#define	atomic_add_64			atomic_add_long
#define	atomic_subtract_64		atomic_subtract_long
#define	atomic_cmpset_64		atomic_cmpset_long
#define	atomic_fcmpset_64		atomic_fcmpset_long
#define	atomic_set_64			atomic_set_long
#define	atomic_clear_64			atomic_clear_long

static __inline u_int
atomic_readandclear_int(volatile u_int *p)
{
	return (__atomic_exchange_n(p, 0, __ATOMIC_SEQ_CST));
}

static __inline u_int
atomic_load_acq_int(volatile u_int *p)
{
	return (__atomic_load_n(p, __ATOMIC_ACQUIRE));
}

static __inline u_long
atomic_readandclear_long(volatile u_long *p)
{
	return (__atomic_exchange_n(p, 0, __ATOMIC_SEQ_CST));
}

static __inline int
atomic_swap_int(volatile int *p, int v)
{
	return (__atomic_exchange_n(p, v, __ATOMIC_SEQ_CST));
}

static __inline u_int
atomic_fetchadd_int(volatile u_int *p, u_int v)
{
	return (__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST));
}

static __inline u_long
atomic_fetchadd_long(volatile u_long *p, u_long v)
{
	return (__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST));
}

static __inline int
atomic_cmpset_int(volatile u_int *p, u_int cmpv, u_int newv)
{
	return (__atomic_compare_exchange_n(p, &cmpv, newv, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

static __inline int
atomic_fcmpset_int(volatile u_int *p, u_int *cmpv, u_int newv)
{
	return (__atomic_compare_exchange_n(p, cmpv, newv, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

static __inline int
atomic_fcmpset_long(volatile u_long *p, u_long *cmpv, u_long newv)
{
	return (__atomic_compare_exchange_n(p, cmpv, newv, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

static __inline int
atomic_cmpset_ptr(volatile void *p, void *cmpv, void *newv)
{
	return (__atomic_compare_exchange_n((void * volatile *)p, &cmpv, newv, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
}

#define atomic_fcmpset_ptr(dst, old, new) \
	atomic_fcmpset_long((volatile u_long *)(dst), (u_long *)(old), \
	    (u_long)(new))

static __inline void *
atomic_swap_ptr(volatile void **addr, void *value)
{
	return ((void *)(uintptr_t)__atomic_exchange_n(
	    (volatile uintptr_t *)addr, (uintptr_t)value,
	    __ATOMIC_SEQ_CST));
}

static __inline void
atomic_add_int_nonlocked(volatile u_int *p, u_int v)
{
	__atomic_fetch_add(p, v, __ATOMIC_RELAXED);
}

#endif /* !_CPU_ATOMIC_H_ */
