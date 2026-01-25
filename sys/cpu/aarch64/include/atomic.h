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

static __inline u_int
atomic_readandclear_int(volatile u_int *p)
{
	return (__atomic_exchange_n(p, 0, __ATOMIC_SEQ_CST));
}

static __inline u_long
atomic_readandclear_long(volatile u_long *p)
{
	return (__atomic_exchange_n(p, 0, __ATOMIC_SEQ_CST));
}

#endif /* !_CPU_ATOMIC_H_ */
