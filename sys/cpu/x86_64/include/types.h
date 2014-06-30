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
typedef __uint64_t      cpumask_t;      /* mask representing a set of cpus */
typedef __uint32_t      cpulock_t;      /* count and exclusive lock */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define CPUMASK(cpu)	((cpumask_t)1 << (cpu))
#define BSRCPUMASK(mask)	bsrq(mask)
#define BSFCPUMASK(mask)	bsfq(mask)
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
