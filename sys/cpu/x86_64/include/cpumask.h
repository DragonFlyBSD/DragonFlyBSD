/*
 * Copyright (c) 2008-2016 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CPU_CPUMASK_H_
#define	_CPU_CPUMASK_H_

#include <cpu/types.h>
#ifdef _KERNEL
#include <cpu/atomic.h>
#endif

#if _CPUMASK_ELEMENTS != 4
#error "CPUMASK macros incompatible with cpumask_t"
#endif

#define CPUMASK_ELEMENTS	_CPUMASK_ELEMENTS

#define CPUMASK_INITIALIZER_ALLONES	{ .ary = { (__uint64_t)-1, \
					  (__uint64_t)-1, \
					  (__uint64_t)-1, \
					  (__uint64_t)-1 } }
#define CPUMASK_INITIALIZER_ONLYONE	{ .ary = { 1, 0, 0, 0 } }

#define CPUMASK_SIMPLE(cpu)	((__uint64_t)1 << (cpu))

#define CPUMASK_ADDR(mask, cpu)	(&(mask).ary[((cpu) >> 6) & 3])

#define BSRCPUMASK(val)		((val).ary[3] ? 192 + bsrq((val).ary[3]) : \
				((val).ary[2] ? 128 + bsrq((val).ary[2]) : \
				((val).ary[1] ? 64 + bsrq((val).ary[1]) : \
						bsrq((val).ary[0]))))

#define BSFCPUMASK(val)		((val).ary[0] ? bsfq((val).ary[0]) : \
				((val).ary[1] ? 64 + bsfq((val).ary[1]) : \
				((val).ary[2] ? 128 + bsfq((val).ary[2]) : \
						192 + bsfq((val).ary[3]))))

#define CPUMASK_CMPMASKEQ(val1, val2)	((val1).ary[0] == (val2).ary[0] && \
					 (val1).ary[1] == (val2).ary[1] && \
					 (val1).ary[2] == (val2).ary[2] && \
					 (val1).ary[3] == (val2).ary[3])

#define CPUMASK_CMPMASKNEQ(val1, val2)	((val1).ary[0] != (val2).ary[0] || \
					 (val1).ary[1] != (val2).ary[1] || \
					 (val1).ary[2] != (val2).ary[2] || \
					 (val1).ary[3] != (val2).ary[3])

#define CPUMASK_ISUP(val)		((val).ary[0] == 1 && \
					 (val).ary[1] == 0 && \
					 (val).ary[2] == 0 && \
					 (val).ary[3] == 0)

#define CPUMASK_TESTZERO(val)		((val).ary[0] == 0 && \
					 (val).ary[1] == 0 && \
					 (val).ary[2] == 0 && \
					 (val).ary[3] == 0)

#define CPUMASK_TESTNZERO(val)		((val).ary[0] != 0 || \
					 (val).ary[1] != 0 || \
					 (val).ary[2] != 0 || \
					 (val).ary[3] != 0)

#define CPUMASK_TESTBIT(val, i)		((val).ary[((i) >> 6) & 3] & \
					 CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_TESTMASK(val1, val2)	(((val1).ary[0] & (val2.ary[0])) || \
					 ((val1).ary[1] & (val2.ary[1])) || \
					 ((val1).ary[2] & (val2.ary[2])) || \
					 ((val1).ary[3] & (val2.ary[3])))

#define CPUMASK_LOWMASK(val)		((val).ary[0])

#define CPUMASK_ORBIT(mask, i)		((mask).ary[((i) >> 6) & 3] |= \
					 CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_ANDBIT(mask, i)		((mask).ary[((i) >> 6) & 3] &= \
					 CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_NANDBIT(mask, i)	((mask).ary[((i) >> 6) & 3] &= \
					 ~CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_ASSZERO(mask)		do {				\
					(mask).ary[0] = 0;		\
					(mask).ary[1] = 0;		\
					(mask).ary[2] = 0;		\
					(mask).ary[3] = 0;		\
					} while(0)

#define CPUMASK_ASSALLONES(mask)	do {				\
					(mask).ary[0] = (__uint64_t)-1;	\
					(mask).ary[1] = (__uint64_t)-1;	\
					(mask).ary[2] = (__uint64_t)-1;	\
					(mask).ary[3] = (__uint64_t)-1;	\
					} while(0)

#define CPUMASK_ASSBIT(mask, i)		do {				\
						CPUMASK_ASSZERO(mask);	\
						CPUMASK_ORBIT(mask, i); \
					} while(0)

#define CPUMASK_ASSBMASK(mask, i)	do {				\
		if (i < 64) {						\
			(mask).ary[0] = CPUMASK_SIMPLE(i) - 1;		\
			(mask).ary[1] = 0;				\
			(mask).ary[2] = 0;				\
			(mask).ary[3] = 0;				\
		} else if (i < 128) {					\
			(mask).ary[0] = (__uint64_t)-1;			\
			(mask).ary[1] = CPUMASK_SIMPLE((i) - 64) - 1;	\
			(mask).ary[2] = 0;				\
			(mask).ary[3] = 0;				\
		} else if (i < 192) {					\
			(mask).ary[0] = (__uint64_t)-1;			\
			(mask).ary[1] = (__uint64_t)-1;			\
			(mask).ary[2] = CPUMASK_SIMPLE((i) - 128) - 1;	\
			(mask).ary[3] = 0;				\
		} else {						\
			(mask).ary[0] = (__uint64_t)-1;			\
			(mask).ary[1] = (__uint64_t)-1;			\
			(mask).ary[2] = (__uint64_t)-1;			\
			(mask).ary[3] = CPUMASK_SIMPLE((i) - 192) - 1;	\
		}							\
					} while(0)

#define CPUMASK_ASSNBMASK(mask, i)	do {				\
		if (i < 64) {						\
			(mask).ary[0] = ~(CPUMASK_SIMPLE(i) - 1);	\
			(mask).ary[1] = (__uint64_t)-1;			\
			(mask).ary[2] = (__uint64_t)-1;			\
			(mask).ary[3] = (__uint64_t)-1;			\
		} else if (i < 128) {					\
			(mask).ary[0] = 0;				\
			(mask).ary[1] = ~(CPUMASK_SIMPLE((i) - 64) - 1);\
			(mask).ary[2] = (__uint64_t)-1;			\
			(mask).ary[3] = (__uint64_t)-1;			\
		} else if (i < 192) {					\
			(mask).ary[0] = 0;				\
			(mask).ary[1] = 0;				\
			(mask).ary[2] = ~(CPUMASK_SIMPLE((i) - 128) - 1);\
			(mask).ary[3] = (__uint64_t)-1;			\
		} else {						\
			(mask).ary[0] = 0;				\
			(mask).ary[1] = 0;				\
			(mask).ary[2] = 0;				\
			(mask).ary[3] = ~(CPUMASK_SIMPLE((i) - 192) - 1);\
		}							\
					} while(0)

#define CPUMASK_ANDMASK(mask, val)	do {				\
					(mask).ary[0] &= (val).ary[0];	\
					(mask).ary[1] &= (val).ary[1];	\
					(mask).ary[2] &= (val).ary[2];	\
					(mask).ary[3] &= (val).ary[3];	\
					} while(0)

#define CPUMASK_NANDMASK(mask, val)	do {				\
					(mask).ary[0] &= ~(val).ary[0];	\
					(mask).ary[1] &= ~(val).ary[1];	\
					(mask).ary[2] &= ~(val).ary[2];	\
					(mask).ary[3] &= ~(val).ary[3];	\
					} while(0)

#define CPUMASK_ORMASK(mask, val)	do {				\
					(mask).ary[0] |= (val).ary[0];	\
					(mask).ary[1] |= (val).ary[1];	\
					(mask).ary[2] |= (val).ary[2];	\
					(mask).ary[3] |= (val).ary[3];	\
					} while(0)

#define CPUMASK_XORMASK(mask, val)	do {				\
					(mask).ary[0] ^= (val).ary[0];	\
					(mask).ary[1] ^= (val).ary[1];	\
					(mask).ary[2] ^= (val).ary[2];	\
					(mask).ary[3] ^= (val).ary[3];	\
					} while(0)

#define CPUMASK_INVMASK(mask)		do {				\
					(mask).ary[0] ^= -1L;		\
					(mask).ary[1] ^= -1L;		\
					(mask).ary[2] ^= -1L;		\
					(mask).ary[3] ^= -1L;		\
					} while(0)

#ifdef _KERNEL
#define ATOMIC_CPUMASK_ORBIT(mask, i)					  \
			atomic_set_cpumask(&(mask).ary[((i) >> 6) & 3],	  \
					   CPUMASK_SIMPLE((i) & 63))

#define ATOMIC_CPUMASK_NANDBIT(mask, i)					  \
			atomic_clear_cpumask(&(mask).ary[((i) >> 6) & 3], \
					   CPUMASK_SIMPLE((i) & 63))

#define ATOMIC_CPUMASK_TESTANDSET(mask, i)				  \
		atomic_testandset_long(&(mask).ary[((i) >> 6) & 3], (i))

#define ATOMIC_CPUMASK_TESTANDCLR(mask, i)				  \
		atomic_testandclear_long(&(mask).ary[((i) >> 6) & 3], (i))

#define ATOMIC_CPUMASK_ORMASK(mask, val) do {				  \
			atomic_set_cpumask(&(mask).ary[0], (val).ary[0]); \
			atomic_set_cpumask(&(mask).ary[1], (val).ary[1]); \
			atomic_set_cpumask(&(mask).ary[2], (val).ary[2]); \
			atomic_set_cpumask(&(mask).ary[3], (val).ary[3]); \
					 } while(0)

#define ATOMIC_CPUMASK_NANDMASK(mask, val) do {				    \
			atomic_clear_cpumask(&(mask).ary[0], (val).ary[0]); \
			atomic_clear_cpumask(&(mask).ary[1], (val).ary[1]); \
			atomic_clear_cpumask(&(mask).ary[2], (val).ary[2]); \
			atomic_clear_cpumask(&(mask).ary[3], (val).ary[3]); \
					 } while(0)

#define ATOMIC_CPUMASK_COPY(mask, val) do {				    \
			atomic_store_rel_cpumask(&(mask).ary[0], (val).ary[0]);\
			atomic_store_rel_cpumask(&(mask).ary[1], (val).ary[1]);\
			atomic_store_rel_cpumask(&(mask).ary[2], (val).ary[2]);\
			atomic_store_rel_cpumask(&(mask).ary[3], (val).ary[3]);\
					 } while(0)
#endif

#endif /* !_CPU_CPUMASK_H_ */
