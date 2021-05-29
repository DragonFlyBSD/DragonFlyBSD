/*
 * Copyright (c) 2019-2021 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#ifndef _SYS__MALLOC_H_
#define	_SYS__MALLOC_H_

/*
 * Do not include this header outside _KERNEL or _KERNEL_STRUCTURES scopes.
 * Used in <sys/user.h>.
 */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/cdefs.h>		/* for __cache_align */
#include <sys/spinlock.h>	/* for spinlock */
#include <sys/exislock.h>	/* for exislock_t */
#include <machine/stdint.h>	/* for __* types */
#include <machine/param.h>	/* for SMP_MAXCPU */

/*
 * A kmalloc slab (used with KSF_OBJSIZE) holds N fixed-size objects
 * in a fixed (typically 32KB) block of memory prefaced by the structure.
 */
#define KMALLOC_SLAB_SIZE	(__size_t)(128 * 1024)
#define KMALLOC_SLAB_MASK	((__size_t)(KMALLOC_SLAB_SIZE - 1))

#define KMALLOC_SLAB_MAXOBJS	(KMALLOC_SLAB_SIZE / __VM_CACHELINE_SIZE)
#define KMALLOC_LOOSE_SIZE	(KMALLOC_SLAB_SIZE * 4)

#define KMALLOC_SLAB_MAGIC	0x6b736c62
#define KMALLOC_MAXFREEMAGS	4

#define KMALLOC_CHECK_DOUBLE_FREE

struct kmalloc_slab {
	struct spinlock		spin;
	struct kmalloc_slab	*next;		/* next mag in list */
	struct malloc_type	*type;		/* who does this belong to */
	__uint32_t		magic;
	__uint32_t		orig_cpuid;	/* originally allocated on */
	__size_t		offset;		/* copied from kmalloc_mgt */
	__size_t		objsize;	/* copied from malloc_type */
	__size_t		ncount;
	/* copy of slab_count from kmalloc_mgt */
	__size_t		aindex;		/* start of allocations */
	__size_t		findex;		/* end of frees */
	__size_t		xindex;		/* synchronizer */
	exislock_t		exis;		/* existential lock state */
	void			*unused01;
	__uint64_t		bmap[(KMALLOC_SLAB_MAXOBJS + 63) / 64];
	void			*fobjs[1];
	/*
	 * Circular buffer listing free objects. All indices modulo ncount.
	 * Invariants:
	 * - aindex, findex and xindex never decrease
	 * - aindex <= xindex <= findex
	 *
	 * aindex <= i < xindex: fobjs[i % count] is a pointer to a free object.
	 * xindex <= i < findex:
	 *     Synchronization state: fobjs[i % count] is temporarily reserved.
	 */
} __cachealign;

/*
 * pcpu slab management structure for kmalloc zone.
 *
 * The intent is to try to improve cache characteristics and to reduce
 * fragmentation by keeping collections localized.  See the code for
 * more information.
 *
 * 'active' and 'alternate' are single slabs while 'partial', 'full', and
 * 'empty' are lists of slabs.
 *
 * The kmalloc zone also uses one of these as a global management structure
 * excess emptymags are regularly moved to the global structure.  This mgt
 * structure is used GLOBALLY (per-zone) as well as PCPU (per-zone).  The
 * fields are utilized differently (or not at all), depending on the use case.
 */
struct kmalloc_mgt {
	struct spinlock		spin;
	struct kmalloc_slab	*active;	/* pcpu */
	struct kmalloc_slab	*alternate;	/* pcpu */
	struct kmalloc_slab	*partial;	/* global */
	struct kmalloc_slab	*full;		/* global */
	struct kmalloc_slab	*empty;		/* global */
	struct kmalloc_slab	**empty_tailp;	/* global */
	__size_t		slab_offset;	/* first object in slab */
	__size_t		slab_count;	/* objects per slab */
	__size_t		npartial;	/* counts */
	__size_t		nfull;
	__size_t		nempty;
	__size_t		gcache_count;	/* #slabs returned to gcache */
	__size_t		unused01;
	__size_t		unused02;
} __cachealign;

/*
 * The malloc tracking structure.  Note that per-cpu entries must be
 * aggregated for accurate statistics, they do not actually break the
 * stats down by cpu (e.g. the cpu freeing memory will subtract from
 * its slot, not the originating cpu's slot).
 *
 * SMP_MAXCPU is used so modules which use malloc remain compatible
 * between UP and SMP.
 *
 * WARNING: __cachealign typically represents 64 byte alignment, so
 *	    this structure may be larger than expected.
 *
 * WARNING: loosememuse is transfered to ks_loosememuse and zerod
 *	    often (e.g. uses atomic_swap_long()).  It allows pcpu
 *	    updates to be taken into account without causing lots
 *	    of cache ping-pongs
 */
struct kmalloc_use {
	__size_t	memuse;
	__size_t	inuse;
	__int64_t	calls;		/* allocations counter (total) */
	__size_t	loosememuse;
	struct kmalloc_mgt mgt;		/* pcpu object store */
} __cachealign;

struct malloc_type {
	struct malloc_type *ks_next;	/* next in list */
	__size_t	ks_loosememuse;	/* (inaccurate) aggregate memuse */
	__size_t	ks_limit;	/* most that are allowed to exist */
	__uint64_t	ks_unused0;
	__uint32_t	ks_flags;	/* KSF_x flags */
	__uint32_t	ks_magic;	/* if it's not magic, don't touch it */
	const char	*ks_shortdesc;	/* short description */
	__size_t	ks_objsize;	/* single size if non-zero */
	struct kmalloc_use *ks_use;
	struct kmalloc_use ks_use0;	/* dummy prior to SMP startup */
	struct kmalloc_mgt ks_mgt;	/* rollup object store */
};

typedef	struct malloc_type	*malloc_type_t;

#define	MALLOC_DECLARE(type)		\
	extern struct malloc_type type[1]	/* ref as ptr */

#define KSF_OBJSIZE	0x00000001	/* zone used for one object type/size */
#define KSF_POLLING	0x00000002	/* poll in progress */

#define KMGD_MAXFREESLABS	128

typedef struct KMGlobalData {
	struct kmalloc_slab *free_slabs;
	struct kmalloc_slab *remote_free_slabs;
	__size_t	free_count;
	void		*reserved[5];
} KMGlobalData;

#endif

#endif /* !_SYS__MALLOC_H_ */
