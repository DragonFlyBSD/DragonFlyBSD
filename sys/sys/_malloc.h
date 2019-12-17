/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
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
#include <machine/stdint.h>	/* for __* types */
#include <machine/param.h>	/* for SMP_MAXCPU */

/*
 * The malloc tracking structure.  Note that per-cpu entries must be
 * aggregated for accurate statistics, they do not actually break the
 * stats down by cpu (e.g. the cpu freeing memory will subtract from
 * its slot, not the originating cpu's slot).
 *
 * SMP_MAXCPU is used so modules which use malloc remain compatible
 * between UP and SMP.
 *
 * Warning: __cachealign typically represents 64 byte alignment, so
 * this structure may be larger than expected.
 */
struct kmalloc_use {
	__size_t	memuse;
	__size_t	inuse;
	__int64_t	calls;	/* total packets of this type ever allocated */

	/*
	 * This value will be added to ks_loosememuse and resetted,
	 * once it goes above certain threshold (ZoneSize).  This
	 * is intended to reduce frequency of ks_loosememuse (global)
	 * updates.
	 */
	__size_t	loosememuse;
} __cachealign;

struct malloc_type {
	struct malloc_type *ks_next;	/* next in list */
	__size_t	ks_loosememuse;	/* (inaccurate) aggregate memuse */
	__size_t	ks_limit;	/* most that are allowed to exist */
	__uint64_t	ks_mtflags;	/* MTF_x flags */

	__uint32_t	ks_unused1;
	__uint32_t	ks_magic;	/* if it's not magic, don't touch it */
	const char	*ks_shortdesc;	/* short description */
	__uint64_t	ks_unused2;
	struct kmalloc_use *ks_use;
	struct kmalloc_use ks_use0;	/* dummy prior to SMP startup */
};

typedef	struct malloc_type	*malloc_type_t;

#define	MALLOC_DECLARE(type)		\
	extern struct malloc_type type[1]	/* ref as ptr */

#endif

#endif /* !_SYS__MALLOC_H_ */
