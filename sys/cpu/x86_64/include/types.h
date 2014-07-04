/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2008 The DragonFly Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)types.h	8.3 (Berkeley) 1/5/94
 * $FreeBSD: src/sys/i386/include/types.h,v 1.19.2.1 2001/03/21 10:50:58 peter Exp $
 */

#ifndef _CPU_TYPES_H_
#define	_CPU_TYPES_H_

#include <machine/stdint.h>

#if defined(__x86_64__)
typedef	__int64_t	__segsz_t;	/* segment size */
typedef	__int64_t	register_t;
typedef	__uint64_t	u_register_t;
#elif defined(__i386__)
typedef	__int32_t	__segsz_t;	/* segment size */
typedef	__int32_t	register_t;
typedef	__uint32_t	u_register_t;
#endif

typedef unsigned long	vm_offset_t;    /* address space bounded offset */
typedef unsigned long	vm_size_t;      /* address space bounded size */

typedef __uint64_t	vm_pindex_t;    /* physical page index */
typedef	__int64_t	vm_ooffset_t;	/* VM object bounded offset */
typedef __uint64_t	vm_poff_t;	/* physical offset */
typedef __uint64_t	vm_paddr_t;	/* physical addr (same as vm_poff_t) */

#ifdef _KERNEL
typedef	__int64_t	intfptr_t;
typedef	__uint64_t	uintfptr_t;
#endif

/*
 * MMU page tables
 */
typedef __uint64_t	pml4_entry_t;
typedef __uint64_t	pdp_entry_t;
typedef __uint64_t	pd_entry_t;
typedef __uint64_t	pt_entry_t;
typedef __uint32_t      cpulock_t;      /* count and exclusive lock */

/*
 * cpumask_t - a mask representing a set of cpus and supporting routines.
 *
 * WARNING! It is recommended that this mask NOT be made variably-sized
 *	    because it affects a huge number of system structures.  However,
 *	    kernel code (non-module) can be optimized to not operate on the
 *	    whole mask.
 */

#define CPUMASK_ELEMENTS	4	/* tested by assembly for #error */

typedef struct {
	__uint64_t      ary[4];
} cpumask_t;

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#define CPUMASK_INITIALIZER_ALLONES	{ .ary = { (__uint64_t)-1, \
					  (__uint64_t)-1, \
					  (__uint64_t)-1, \
					  (__uint64_t)-1 } }
#define CPUMASK_INITIALIZER_ONLYONE	{ .ary = { 1, 0, 0, 0 } }

#define CPUMASK_SIMPLE(cpu)	((__uint64_t)1 << (cpu))

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

#define ATOMIC_CPUMASK_ORBIT(mask, i)					  \
			atomic_set_cpumask(&(mask).ary[((i) >> 6) & 3],	  \
					   CPUMASK_SIMPLE((i) & 63))

#define ATOMIC_CPUMASK_NANDBIT(mask, i)					  \
			atomic_clear_cpumask(&(mask).ary[((i) >> 6) & 3], \
					   CPUMASK_SIMPLE((i) & 63))

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

#endif

#define CPULOCK_EXCLBIT	0		/* exclusive lock bit number */
#define CPULOCK_EXCL	0x00000001	/* exclusive lock */
#define CPULOCK_INCR	0x00000002	/* auxillary counter add/sub */
#define CPULOCK_CNTMASK	0x7FFFFFFE

#define PML4SIZE	sizeof(pml4_entry_t) /* for assembly files */
#define PDPSIZE		sizeof(pdp_entry_t) /* for assembly files */
#define PDESIZE         sizeof(pd_entry_t) /* for assembly files */
#define PTESIZE         sizeof(pt_entry_t) /* for assembly files */

#endif /* !_CPU_TYPES_H_ */
