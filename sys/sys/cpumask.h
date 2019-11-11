/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef _SYS_CPUMASK_H_
#define _SYS_CPUMASK_H_

#include <machine/cpumask.h>
#include <machine/stdint.h>

typedef	__cpumask_t	cpumask_t;

#ifndef _KERNEL
#define	CPU_SETSIZE		((int)(sizeof(cpumask_t) * 8))
#define	CPU_ZERO(set)			__CPU_ZERO(set)
#define	CPU_SET(cpu, set)		__CPU_SET(cpu, set)
#define	CPU_CLR(cpu, set)		__CPU_CLR(cpu, set)
#define	CPU_ISSET(cpu, set)		__CPU_ISSET(cpu, set)
#define	CPU_COUNT(set)			__CPU_COUNT(set)
#define	CPU_AND(dst, set1, set2)	__CPU_AND(dst, set1, set2)
#define	CPU_OR(dst, set1, set2)		__CPU_OR(dst, set1, set2)
#define	CPU_XOR(dst, set1, set2)	__CPU_XOR(dst, set1, set2)
#define	CPU_EQUAL(set1, set2)		__CPU_EQUAL(set1, set2)
#endif

/*
 * It is convenient to place this type here due to its proximity to the
 * cpumask_t use cases in structs.  Keep public for easier access to
 * struct proc for now.
 */
typedef	__uint32_t	cpulock_t;	/* count and exclusive lock */

#define	CPULOCK_EXCLBIT	0		/* exclusive lock bit number */
#define	CPULOCK_EXCL	0x00000001	/* exclusive lock */
#define	CPULOCK_INCR	0x00000002	/* auxillary counter add/sub */
#define	CPULOCK_CNTMASK	0x7FFFFFFE

#endif /* !_SYS_CPUMASK_H_ */
