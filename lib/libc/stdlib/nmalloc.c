/*
 * NMALLOC.C	- New Malloc (ported from kernel slab allocator)
 *
 * Copyright (c) 2003,2004,2009 The DragonFly Project.  All rights reserved.
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
/*
 * This module implements a slab allocator drop-in replacement for the
 * libc malloc().
 *
 * A slab allocator reserves a ZONE for each chunk size, then lays the
 * chunks out in an array within the zone.  Allocation and deallocation
 * is nearly instantanious, and overhead losses are limited to a fixed
 * worst-case amount.
 *
 * The slab allocator does not have to pre-initialize the list of
 * free chunks for each zone, and the underlying VM will not be
 * touched at all beyond the zone header until an actual allocation
 * needs it.
 *
 * Slab management and locking is done on a per-zone basis.
 *
 *	Alloc Size	Chunking        Number of zones
 *	0-127		8		16
 *	128-255		16		8
 *	256-511		32		8
 *	512-1023	64		8
 *	1024-2047	128		8
 *	2048-4095	256		8
 *	4096-8191	512		8
 *	8192-16383	1024		8
 *	16384-32767	2048		8
 *
 *	Allocations >= ZoneLimit (16K) go directly to mmap and a hash table
 *	is used to locate for free.  One and Two-page allocations use the
 *	zone mechanic to avoid excessive mmap()/munmap() calls.
 *
 *			   API FEATURES AND SIDE EFFECTS
 *
 *    + power-of-2 sized allocations up to a page will be power-of-2 aligned.
 *	Above that power-of-2 sized allocations are page-aligned.  Non
 *	power-of-2 sized allocations are aligned the same as the chunk
 *	size for their zone.
 *    + malloc(0) returns a special non-NULL value
 *    + ability to allocate arbitrarily large chunks of memory
 *    + realloc will reuse the passed pointer if possible, within the
 *	limitations of the zone chunking.
 */

#include "libc_private.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "spinlock.h"
#include "un-namespace.h"

/*
 * Linked list of large allocations
 */
typedef struct bigalloc {
	struct bigalloc *next;	/* hash link */
	void	*base;		/* base pointer */
	u_long	bytes;		/* bytes allocated */
	u_long	unused01;
} *bigalloc_t;

/*
 * Note that any allocations which are exact multiples of PAGE_SIZE, or
 * which are >= ZALLOC_ZONE_LIMIT, will fall through to the kmem subsystem.
 */
#define ZALLOC_ZONE_LIMIT	(16 * 1024)	/* max slab-managed alloc */
#define ZALLOC_MIN_ZONE_SIZE	(32 * 1024)	/* minimum zone size */
#define ZALLOC_MAX_ZONE_SIZE	(128 * 1024)	/* maximum zone size */
#define ZALLOC_ZONE_SIZE	(64 * 1024)
#define ZALLOC_SLAB_MAGIC	0x736c6162	/* magic sanity */
#define ZALLOC_SLAB_SLIDE	20		/* L1-cache skip */

#if ZALLOC_ZONE_LIMIT == 16384
#define NZONES			72
#elif ZALLOC_ZONE_LIMIT == 32768
#define NZONES			80
#else
#error "I couldn't figure out NZONES"
#endif

/*
 * Chunk structure for free elements
 */
typedef struct slchunk {
	struct slchunk *c_Next;
} *slchunk_t;

/*
 * The IN-BAND zone header is placed at the beginning of each zone.
 */
struct slglobaldata;

typedef struct slzone {
	__int32_t	z_Magic;	/* magic number for sanity check */
	int		z_NFree;	/* total free chunks / ualloc space */
	struct slzone *z_Next;		/* ZoneAry[] link if z_NFree non-zero */
	struct slglobaldata *z_GlobalData;
	int		z_NMax;		/* maximum free chunks */
	char		*z_BasePtr;	/* pointer to start of chunk array */
	int		z_UIndex;	/* current initial allocation index */
	int		z_UEndIndex;	/* last (first) allocation index */
	int		z_ChunkSize;	/* chunk size for validation */
	int		z_FirstFreePg;	/* chunk list on a page-by-page basis */
	int		z_ZoneIndex;
	int		z_Flags;
	struct slchunk *z_PageAry[ZALLOC_ZONE_SIZE / PAGE_SIZE];
#if defined(INVARIANTS)
	__uint32_t	z_Bitmap[];	/* bitmap of free chunks / sanity */
#endif
} *slzone_t;

typedef struct slglobaldata {
	spinlock_t	Spinlock;
	slzone_t	ZoneAry[NZONES];/* linked list of zones NFree > 0 */
	slzone_t	FreeZones;	/* whole zones that have become free */
	int		NFreeZones;	/* free zone count */
	int		JunkIndex;
} *slglobaldata_t;

#define SLZF_UNOTZEROD		0x0001

/*
 * Misc constants.  Note that allocations that are exact multiples of
 * PAGE_SIZE, or exceed the zone limit, fall through to the kmem module.
 * IN_SAME_PAGE_MASK is used to sanity-check the per-page free lists.
 */
#define MIN_CHUNK_SIZE		8		/* in bytes */
#define MIN_CHUNK_MASK		(MIN_CHUNK_SIZE - 1)
#define ZONE_RELS_THRESH	4		/* threshold number of zones */
#define IN_SAME_PAGE_MASK	(~(intptr_t)PAGE_MASK | MIN_CHUNK_MASK)

/*
 * The WEIRD_ADDR is used as known text to copy into free objects to
 * try to create deterministic failure cases if the data is accessed after
 * free.
 *
 * WARNING: A limited number of spinlocks are available, BIGXSIZE should
 *	    not be larger then 64.
 */
#define WEIRD_ADDR      0xdeadc0de
#define MAX_COPY        sizeof(weirdary)
#define ZERO_LENGTH_PTR	((void *)&malloc_dummy_pointer)

#define BIGHSHIFT	10			/* bigalloc hash table */
#define BIGHSIZE	(1 << BIGHSHIFT)
#define BIGHMASK	(BIGHSIZE - 1)
#define BIGXSIZE	(BIGHSIZE / 16)		/* bigalloc lock table */
#define BIGXMASK	(BIGXSIZE - 1)

#define SLGD_MAX	4			/* parallel allocations */

#define SAFLAG_ZERO	0x0001
#define SAFLAG_PASSIVE	0x0002

/*
 * Thread control
 */

#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

#define MASSERT(exp)	do { if (__predict_false(!(exp)))	\
				_mpanic("assertion: %s in %s",	\
				#exp, __func__);		\
			    } while (0)

/*
 * Fixed globals (not per-cpu)
 */
static const int ZoneSize = ZALLOC_ZONE_SIZE;
static const int ZoneLimit = ZALLOC_ZONE_LIMIT;
static const int ZonePageCount = ZALLOC_ZONE_SIZE / PAGE_SIZE;
static const int ZoneMask = ZALLOC_ZONE_SIZE - 1;

static struct slglobaldata	SLGlobalData[SLGD_MAX];
static bigalloc_t bigalloc_array[BIGHSIZE];
static spinlock_t bigspin_array[BIGXSIZE];
static int malloc_panic;
static int malloc_dummy_pointer;

static const int32_t weirdary[16] = {
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR,
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR,
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR,
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR
};

static __thread slglobaldata_t LastSLGD = &SLGlobalData[0];

static void *_slaballoc(size_t size, int flags);
static void *_slabrealloc(void *ptr, size_t size);
static void _slabfree(void *ptr);
static void *_vmem_alloc(size_t bytes, size_t align, int flags);
static void _vmem_free(void *ptr, size_t bytes);
static void _mpanic(const char *ctl, ...);
#if defined(INVARIANTS)
static void chunk_mark_allocated(slzone_t z, void *chunk);
static void chunk_mark_free(slzone_t z, void *chunk);
#endif

#ifdef INVARIANTS
/*
 * If enabled any memory allocated without M_ZERO is initialized to -1.
 */
static int  use_malloc_pattern;
#endif

/*
 * Thread locks.
 *
 * NOTE: slgd_trylock() returns 0 or EBUSY
 */
static __inline void
slgd_lock(slglobaldata_t slgd)
{
	if (__isthreaded)
		_SPINLOCK(&slgd->Spinlock);
}

static __inline int
slgd_trylock(slglobaldata_t slgd)
{
	if (__isthreaded)
		return(_SPINTRYLOCK(&slgd->Spinlock));
	return(0);
}

static __inline void
slgd_unlock(slglobaldata_t slgd)
{
	if (__isthreaded)
		_SPINUNLOCK(&slgd->Spinlock);
}

/*
 * bigalloc hashing and locking support.
 *
 * Return an unmasked hash code for the passed pointer.
 */
static __inline int
_bigalloc_hash(void *ptr)
{
	int hv;

	hv = ((int)(intptr_t)ptr >> PAGE_SHIFT) ^
	      ((int)(intptr_t)ptr >> (PAGE_SHIFT + BIGHSHIFT));

	return(hv);
}

/*
 * Lock the hash chain and return a pointer to its base for the specified
 * address.
 */
static __inline bigalloc_t *
bigalloc_lock(void *ptr)
{
	int hv = _bigalloc_hash(ptr);
	bigalloc_t *bigp;

	bigp = &bigalloc_array[hv & BIGHMASK];
	if (__isthreaded)
		_SPINLOCK(&bigspin_array[hv & BIGXMASK]);
	return(bigp);
}

/*
 * Lock the hash chain and return a pointer to its base for the specified
 * address.
 *
 * BUT, if the hash chain is empty, just return NULL and do not bother
 * to lock anything.
 */
static __inline bigalloc_t *
bigalloc_check_and_lock(void *ptr)
{
	int hv = _bigalloc_hash(ptr);
	bigalloc_t *bigp;

	bigp = &bigalloc_array[hv & BIGHMASK];
	if (*bigp == NULL)
		return(NULL);
	if (__isthreaded) {
		_SPINLOCK(&bigspin_array[hv & BIGXMASK]);
	}
	return(bigp);
}

static __inline void
bigalloc_unlock(void *ptr)
{
	int hv;

	if (__isthreaded) {
		hv = _bigalloc_hash(ptr);
		_SPINUNLOCK(&bigspin_array[hv & BIGXMASK]);
	}
}

/*
 * Calculate the zone index for the allocation request size and set the
 * allocation request size to that particular zone's chunk size.
 */
static __inline int
zoneindex(size_t *bytes, size_t *chunking)
{
	size_t n = (unsigned int)*bytes;	/* unsigned for shift opt */
	if (n < 128) {
		*bytes = n = (n + 7) & ~7;
		*chunking = 8;
		return(n / 8 - 1);		/* 8 byte chunks, 16 zones */
	}
	if (n < 256) {
		*bytes = n = (n + 15) & ~15;
		*chunking = 16;
		return(n / 16 + 7);
	}
	if (n < 8192) {
		if (n < 512) {
			*bytes = n = (n + 31) & ~31;
			*chunking = 32;
			return(n / 32 + 15);
		}
		if (n < 1024) {
			*bytes = n = (n + 63) & ~63;
			*chunking = 64;
			return(n / 64 + 23);
		}
		if (n < 2048) {
			*bytes = n = (n + 127) & ~127;
			*chunking = 128;
			return(n / 128 + 31);
		}
		if (n < 4096) {
			*bytes = n = (n + 255) & ~255;
			*chunking = 256;
			return(n / 256 + 39);
		}
		*bytes = n = (n + 511) & ~511;
		*chunking = 512;
		return(n / 512 + 47);
	}
#if ZALLOC_ZONE_LIMIT > 8192
	if (n < 16384) {
		*bytes = n = (n + 1023) & ~1023;
		*chunking = 1024;
		return(n / 1024 + 55);
	}
#endif
#if ZALLOC_ZONE_LIMIT > 16384
	if (n < 32768) {
		*bytes = n = (n + 2047) & ~2047;
		*chunking = 2048;
		return(n / 2048 + 63);
	}
#endif
	_mpanic("Unexpected byte count %d", n);
	return(0);
}

/*
 * malloc() - call internal slab allocator
 */
void *
malloc(size_t size)
{
	void *ptr;

	ptr = _slaballoc(size, 0);
	if (ptr == NULL)
		errno = ENOMEM;
	return(ptr);
}

/*
 * calloc() - call internal slab allocator
 */
void *
calloc(size_t number, size_t size)
{
	void *ptr;

	ptr = _slaballoc(number * size, SAFLAG_ZERO);
	if (ptr == NULL)
		errno = ENOMEM;
	return(ptr);
}

/*
 * realloc() (SLAB ALLOCATOR)
 *
 * We do not attempt to optimize this routine beyond reusing the same
 * pointer if the new size fits within the chunking of the old pointer's
 * zone.
 */
void *
realloc(void *ptr, size_t size)
{
	ptr = _slabrealloc(ptr, size);
	if (ptr == NULL)
		errno = ENOMEM;
	return(ptr);
}

/*
 * posix_memalign()
 *
 * Allocate (size) bytes with a alignment of (alignment), where (alignment)
 * is a power of 2 >= sizeof(void *).
 *
 * The slab allocator will allocate on power-of-2 boundaries up to
 * at least PAGE_SIZE.  We use the zoneindex mechanic to find a
 * zone matching the requirements, and _vmem_alloc() otherwise.
 */
int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
	bigalloc_t *bigp;
	bigalloc_t big;
	size_t chunking;
	int zi;

	/*
	 * OpenGroup spec issue 6 checks
	 */
	if ((alignment | (alignment - 1)) + 1 != (alignment << 1)) {
		*memptr = NULL;
		return(EINVAL);
	}
	if (alignment < sizeof(void *)) {
		*memptr = NULL;
		return(EINVAL);
	}

	/*
	 * Our zone mechanism guarantees same-sized alignment for any
	 * power-of-2 allocation.  If size is a power-of-2 and reasonable
	 * we can just call _slaballoc() and be done.  We round size up
	 * to the nearest alignment boundary to improve our odds of
	 * it becoming a power-of-2 if it wasn't before.
	 */
	if (size <= alignment)
		size = alignment;
	else
		size = (size + alignment - 1) & ~(size_t)(alignment - 1);
	if (size < PAGE_SIZE && (size | (size - 1)) + 1 == (size << 1)) {
		*memptr = _slaballoc(size, 0);
		return(*memptr ? 0 : ENOMEM);
	}

	/*
	 * Otherwise locate a zone with a chunking that matches
	 * the requested alignment, within reason.   Consider two cases:
	 *
	 * (1) A 1K allocation on a 32-byte alignment.  The first zoneindex
	 *     we find will be the best fit because the chunking will be
	 *     greater or equal to the alignment.
	 *
	 * (2) A 513 allocation on a 256-byte alignment.  In this case
	 *     the first zoneindex we find will be for 576 byte allocations
	 *     with a chunking of 64, which is not sufficient.  To fix this
	 *     we simply find the nearest power-of-2 >= size and use the
	 *     same side-effect of _slaballoc() which guarantees
	 *     same-alignment on a power-of-2 allocation.
	 */
	if (size < PAGE_SIZE) {
		zi = zoneindex(&size, &chunking);
		if (chunking >= alignment) {
			*memptr = _slaballoc(size, 0);
			return(*memptr ? 0 : ENOMEM);
		}
		if (size >= 1024)
			alignment = 1024;
		if (size >= 16384)
			alignment = 16384;
		while (alignment < size)
			alignment <<= 1;
		*memptr = _slaballoc(alignment, 0);
		return(*memptr ? 0 : ENOMEM);
	}

	/*
	 * If the slab allocator cannot handle it use vmem_alloc().
	 *
	 * Alignment must be adjusted up to at least PAGE_SIZE in this case.
	 */
	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;
	if (size < alignment)
		size = alignment;
	size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
	*memptr = _vmem_alloc(size, alignment, 0);
	if (*memptr == NULL)
		return(ENOMEM);

	big = _slaballoc(sizeof(struct bigalloc), 0);
	if (big == NULL) {
		_vmem_free(*memptr, size);
		*memptr = NULL;
		return(ENOMEM);
	}
	bigp = bigalloc_lock(*memptr);
	big->base = *memptr;
	big->bytes = size;
	big->unused01 = 0;
	big->next = *bigp;
	*bigp = big;
	bigalloc_unlock(*memptr);

	return(0);
}

/*
 * free() (SLAB ALLOCATOR) - do the obvious
 */
void
free(void *ptr)
{
	_slabfree(ptr);
}

/*
 * _slaballoc()	(SLAB ALLOCATOR)
 *
 *	Allocate memory via the slab allocator.  If the request is too large,
 *	or if it page-aligned beyond a certain size, we fall back to the
 *	KMEM subsystem
 */
static void *
_slaballoc(size_t size, int flags)
{
	slzone_t z;
	slchunk_t chunk;
	slglobaldata_t slgd;
	size_t chunking;
	int zi;
#ifdef INVARIANTS
	int i;
#endif
	int off;

	/*
	 * Handle the degenerate size == 0 case.  Yes, this does happen.
	 * Return a special pointer.  This is to maintain compatibility with
	 * the original malloc implementation.  Certain devices, such as the
	 * adaptec driver, not only allocate 0 bytes, they check for NULL and
	 * also realloc() later on.  Joy.
	 */
	if (size == 0)
		return(ZERO_LENGTH_PTR);

	/*
	 * Handle large allocations directly.  There should not be very many
	 * of these so performance is not a big issue.
	 *
	 * The backend allocator is pretty nasty on a SMP system.   Use the
	 * slab allocator for one and two page-sized chunks even though we
	 * lose some efficiency.
	 */
	if (size >= ZoneLimit ||
	    ((size & PAGE_MASK) == 0 && size > PAGE_SIZE*2)) {
		bigalloc_t big;
		bigalloc_t *bigp;

		size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
		chunk = _vmem_alloc(size, PAGE_SIZE, flags);
		if (chunk == NULL)
			return(NULL);

		big = _slaballoc(sizeof(struct bigalloc), 0);
		if (big == NULL) {
			_vmem_free(chunk, size);
			return(NULL);
		}
		bigp = bigalloc_lock(chunk);
		big->base = chunk;
		big->bytes = size;
		big->unused01 = 0;
		big->next = *bigp;
		*bigp = big;
		bigalloc_unlock(chunk);

		return(chunk);
	}

	/*
	 * Multi-threading support.  This needs work XXX.
	 *
	 * Choose a globaldata structure to allocate from.  If we cannot
	 * immediately get the lock try a different one.
	 *
	 * LastSLGD is a per-thread global.
	 */
	slgd = LastSLGD;
	if (slgd_trylock(slgd) != 0) {
		if (++slgd == &SLGlobalData[SLGD_MAX])
			slgd = &SLGlobalData[0];
		LastSLGD = slgd;
		slgd_lock(slgd);
	}

	/*
	 * Attempt to allocate out of an existing zone.  If all zones are
	 * exhausted pull one off the free list or allocate a new one.
	 *
	 * Note: zoneindex() will panic of size is too large.
	 */
	zi = zoneindex(&size, &chunking);
	MASSERT(zi < NZONES);

	if ((z = slgd->ZoneAry[zi]) == NULL) {
		/*
		 * Pull the zone off the free list.  If the zone on
		 * the free list happens to be correctly set up we
		 * do not have to reinitialize it.
		 */
		if ((z = slgd->FreeZones) != NULL) {
			slgd->FreeZones = z->z_Next;
			--slgd->NFreeZones;
			if (z->z_ChunkSize == size) {
				z->z_Magic = ZALLOC_SLAB_MAGIC;
				z->z_Next = slgd->ZoneAry[zi];
				slgd->ZoneAry[zi] = z;
				goto have_zone;
			}
			bzero(z, sizeof(struct slzone));
			z->z_Flags |= SLZF_UNOTZEROD;
		} else {
			z = _vmem_alloc(ZoneSize, ZoneSize, flags);
			if (z == NULL)
				goto fail;
		}

		/*
		 * How big is the base structure?
		 */
#if defined(INVARIANTS)
		/*
		 * Make room for z_Bitmap.  An exact calculation is
		 * somewhat more complicated so don't make an exact
		 * calculation.
		 */
		off = offsetof(struct slzone,
				z_Bitmap[(ZoneSize / size + 31) / 32]);
		bzero(z->z_Bitmap, (ZoneSize / size + 31) / 8);
#else
		off = sizeof(struct slzone);
#endif

		/*
		 * Align the storage in the zone based on the chunking.
		 *
		 * Guarentee power-of-2 alignment for power-of-2-sized
		 * chunks.  Otherwise align based on the chunking size
		 * (typically 8 or 16 bytes for small allocations).
		 *
		 * NOTE: Allocations >= ZoneLimit are governed by the
		 * bigalloc code and typically only guarantee page-alignment.
		 *
		 * Set initial conditions for UIndex near the zone header
		 * to reduce unecessary page faults, vs semi-randomization
		 * to improve L1 cache saturation.
		 */
		if ((size | (size - 1)) + 1 == (size << 1))
			off = (off + size - 1) & ~(size - 1);
		else
			off = (off + chunking - 1) & ~(chunking - 1);
		z->z_Magic = ZALLOC_SLAB_MAGIC;
		z->z_GlobalData = slgd;
		z->z_ZoneIndex = zi;
		z->z_NMax = (ZoneSize - off) / size;
		z->z_NFree = z->z_NMax;
		z->z_BasePtr = (char *)z + off;
		/*z->z_UIndex = z->z_UEndIndex = slgd->JunkIndex % z->z_NMax;*/
		z->z_UIndex = z->z_UEndIndex = 0;
		z->z_ChunkSize = size;
		z->z_FirstFreePg = ZonePageCount;
		z->z_Next = slgd->ZoneAry[zi];
		slgd->ZoneAry[zi] = z;
		if ((z->z_Flags & SLZF_UNOTZEROD) == 0) {
			flags &= ~SAFLAG_ZERO;	/* already zero'd */
			flags |= SAFLAG_PASSIVE;
		}

		/*
		 * Slide the base index for initial allocations out of the
		 * next zone we create so we do not over-weight the lower
		 * part of the cpu memory caches.
		 */
		slgd->JunkIndex = (slgd->JunkIndex + ZALLOC_SLAB_SLIDE)
					& (ZALLOC_MAX_ZONE_SIZE - 1);
	}

	/*
	 * Ok, we have a zone from which at least one chunk is available.
	 *
	 * Remove us from the ZoneAry[] when we become empty
	 */
have_zone:
	MASSERT(z->z_NFree > 0);

	if (--z->z_NFree == 0) {
		slgd->ZoneAry[zi] = z->z_Next;
		z->z_Next = NULL;
	}

	/*
	 * Locate a chunk in a free page.  This attempts to localize
	 * reallocations into earlier pages without us having to sort
	 * the chunk list.  A chunk may still overlap a page boundary.
	 */
	while (z->z_FirstFreePg < ZonePageCount) {
		if ((chunk = z->z_PageAry[z->z_FirstFreePg]) != NULL) {
#ifdef DIAGNOSTIC
			/*
			 * Diagnostic: c_Next is not total garbage.
			 */
			MASSERT(chunk->c_Next == NULL ||
			    ((intptr_t)chunk->c_Next & IN_SAME_PAGE_MASK) ==
			    ((intptr_t)chunk & IN_SAME_PAGE_MASK));
#endif
#ifdef INVARIANTS
			chunk_mark_allocated(z, chunk);
#endif
			MASSERT((uintptr_t)chunk & ZoneMask);
			z->z_PageAry[z->z_FirstFreePg] = chunk->c_Next;
			goto done;
		}
		++z->z_FirstFreePg;
	}

	/*
	 * No chunks are available but NFree said we had some memory,
	 * so it must be available in the never-before-used-memory
	 * area governed by UIndex.  The consequences are very
	 * serious if our zone got corrupted so we use an explicit
	 * panic rather then a KASSERT.
	 */
	chunk = (slchunk_t)(z->z_BasePtr + z->z_UIndex * size);

	if (++z->z_UIndex == z->z_NMax)
		z->z_UIndex = 0;
	if (z->z_UIndex == z->z_UEndIndex) {
		if (z->z_NFree != 0)
			_mpanic("slaballoc: corrupted zone");
	}

	if ((z->z_Flags & SLZF_UNOTZEROD) == 0) {
		flags &= ~SAFLAG_ZERO;
		flags |= SAFLAG_PASSIVE;
	}
#if defined(INVARIANTS)
	chunk_mark_allocated(z, chunk);
#endif

done:
	slgd_unlock(slgd);
	if (flags & SAFLAG_ZERO) {
		bzero(chunk, size);
#ifdef INVARIANTS
	} else if ((flags & (SAFLAG_ZERO|SAFLAG_PASSIVE)) == 0) {
		if (use_malloc_pattern) {
			for (i = 0; i < size; i += sizeof(int)) {
				*(int *)((char *)chunk + i) = -1;
			}
		}
		/* avoid accidental double-free check */
		chunk->c_Next = (void *)-1;
#endif
	}
	return(chunk);
fail:
	slgd_unlock(slgd);
	return(NULL);
}

/*
 * Reallocate memory within the chunk
 */
static void *
_slabrealloc(void *ptr, size_t size)
{
	bigalloc_t *bigp;
	void *nptr;
	slzone_t z;
	size_t chunking;

	if (ptr == NULL || ptr == ZERO_LENGTH_PTR)
		return(_slaballoc(size, 0));

	if (size == 0) {
	    free(ptr);
	    return(ZERO_LENGTH_PTR);
	}

	/*
	 * Handle oversized allocations.  XXX we really should require
	 * that a size be passed to free() instead of this nonsense.
	 */
	if ((bigp = bigalloc_check_and_lock(ptr)) != NULL) {
		bigalloc_t big;
		size_t bigbytes;

		while ((big = *bigp) != NULL) {
			if (big->base == ptr) {
				size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
				bigbytes = big->bytes;
				bigalloc_unlock(ptr);
				if (bigbytes == size)
					return(ptr);
				if ((nptr = _slaballoc(size, 0)) == NULL)
					return(NULL);
				if (size > bigbytes)
					size = bigbytes;
				bcopy(ptr, nptr, size);
				_slabfree(ptr);
				return(nptr);
			}
			bigp = &big->next;
		}
		bigalloc_unlock(ptr);
	}

	/*
	 * Get the original allocation's zone.  If the new request winds
	 * up using the same chunk size we do not have to do anything.
	 *
	 * NOTE: We don't have to lock the globaldata here, the fields we
	 * access here will not change at least as long as we have control
	 * over the allocation.
	 */
	z = (slzone_t)((uintptr_t)ptr & ~(uintptr_t)ZoneMask);
	MASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);

	/*
	 * Use zoneindex() to chunk-align the new size, as long as the
	 * new size is not too large.
	 */
	if (size < ZoneLimit) {
		zoneindex(&size, &chunking);
		if (z->z_ChunkSize == size)
			return(ptr);
	}

	/*
	 * Allocate memory for the new request size and copy as appropriate.
	 */
	if ((nptr = _slaballoc(size, 0)) != NULL) {
		if (size > z->z_ChunkSize)
			size = z->z_ChunkSize;
		bcopy(ptr, nptr, size);
		_slabfree(ptr);
	}

	return(nptr);
}

/*
 * free (SLAB ALLOCATOR)
 *
 * Free a memory block previously allocated by malloc.  Note that we do not
 * attempt to uplodate ks_loosememuse as MP races could prevent us from
 * checking memory limits in malloc.
 *
 * MPSAFE
 */
static void
_slabfree(void *ptr)
{
	slzone_t z;
	slchunk_t chunk;
	bigalloc_t big;
	bigalloc_t *bigp;
	slglobaldata_t slgd;
	size_t size;
	int pgno;

	/*
	 * Handle NULL frees and special 0-byte allocations
	 */
	if (ptr == NULL)
		return;
	if (ptr == ZERO_LENGTH_PTR)
		return;

	/*
	 * Handle oversized allocations.
	 */
	if ((bigp = bigalloc_check_and_lock(ptr)) != NULL) {
		while ((big = *bigp) != NULL) {
			if (big->base == ptr) {
				*bigp = big->next;
				bigalloc_unlock(ptr);
				size = big->bytes;
				_slabfree(big);
#ifdef INVARIANTS
				MASSERT(sizeof(weirdary) <= size);
				bcopy(weirdary, ptr, sizeof(weirdary));
#endif
				_vmem_free(ptr, size);
				return;
			}
			bigp = &big->next;
		}
		bigalloc_unlock(ptr);
	}

	/*
	 * Zone case.  Figure out the zone based on the fact that it is
	 * ZoneSize aligned.
	 */
	z = (slzone_t)((uintptr_t)ptr & ~(uintptr_t)ZoneMask);
	MASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);

	pgno = ((char *)ptr - (char *)z) >> PAGE_SHIFT;
	chunk = ptr;
	slgd = z->z_GlobalData;
	slgd_lock(slgd);

#ifdef INVARIANTS
	/*
	 * Attempt to detect a double-free.  To reduce overhead we only check
	 * if there appears to be link pointer at the base of the data.
	 */
	if (((intptr_t)chunk->c_Next - (intptr_t)z) >> PAGE_SHIFT == pgno) {
		slchunk_t scan;

		for (scan = z->z_PageAry[pgno]; scan; scan = scan->c_Next) {
			if (scan == chunk)
				_mpanic("Double free at %p", chunk);
		}
	}
	chunk_mark_free(z, chunk);
#endif

	/*
	 * Put weird data into the memory to detect modifications after
	 * freeing, illegal pointer use after freeing (we should fault on
	 * the odd address), and so forth.
	 */
#ifdef INVARIANTS
	if (z->z_ChunkSize < sizeof(weirdary))
		bcopy(weirdary, chunk, z->z_ChunkSize);
	else
		bcopy(weirdary, chunk, sizeof(weirdary));
#endif

	/*
	 * Add this free non-zero'd chunk to a linked list for reuse, adjust
	 * z_FirstFreePg.
	 */
	chunk->c_Next = z->z_PageAry[pgno];
	z->z_PageAry[pgno] = chunk;
	if (z->z_FirstFreePg > pgno)
		z->z_FirstFreePg = pgno;

	/*
	 * Bump the number of free chunks.  If it becomes non-zero the zone
	 * must be added back onto the appropriate list.
	 */
	if (z->z_NFree++ == 0) {
		z->z_Next = slgd->ZoneAry[z->z_ZoneIndex];
		slgd->ZoneAry[z->z_ZoneIndex] = z;
	}

	/*
	 * If the zone becomes totally free then move this zone to
	 * the FreeZones list.
	 *
	 * Do not madvise here, avoiding the edge case where a malloc/free
	 * loop is sitting on the edge of a new zone.
	 *
	 * We could leave at least one zone in the ZoneAry for the index,
	 * using something like the below, but while this might be fine
	 * for the kernel (who cares about ~10MB of wasted memory), it
	 * probably isn't such a good idea for a user program.
	 *
	 * 	&& (z->z_Next || slgd->ZoneAry[z->z_ZoneIndex] != z)
	 */
	if (z->z_NFree == z->z_NMax) {
		slzone_t *pz;

		pz = &slgd->ZoneAry[z->z_ZoneIndex];
		while (z != *pz)
			pz = &(*pz)->z_Next;
		*pz = z->z_Next;
		z->z_Magic = -1;
		z->z_Next = slgd->FreeZones;
		slgd->FreeZones = z;
		++slgd->NFreeZones;
	}

	/*
	 * Limit the number of zones we keep cached.
	 */
	while (slgd->NFreeZones > ZONE_RELS_THRESH) {
		z = slgd->FreeZones;
		slgd->FreeZones = z->z_Next;
		--slgd->NFreeZones;
		slgd_unlock(slgd);
		_vmem_free(z, ZoneSize);
		slgd_lock(slgd);
	}
	slgd_unlock(slgd);
}

#if defined(INVARIANTS)
/*
 * Helper routines for sanity checks
 */
static
void
chunk_mark_allocated(slzone_t z, void *chunk)
{
	int bitdex = ((char *)chunk - (char *)z->z_BasePtr) / z->z_ChunkSize;
	__uint32_t *bitptr;

	MASSERT(bitdex >= 0 && bitdex < z->z_NMax);
	bitptr = &z->z_Bitmap[bitdex >> 5];
	bitdex &= 31;
	MASSERT((*bitptr & (1 << bitdex)) == 0);
	*bitptr |= 1 << bitdex;
}

static
void
chunk_mark_free(slzone_t z, void *chunk)
{
	int bitdex = ((char *)chunk - (char *)z->z_BasePtr) / z->z_ChunkSize;
	__uint32_t *bitptr;

	MASSERT(bitdex >= 0 && bitdex < z->z_NMax);
	bitptr = &z->z_Bitmap[bitdex >> 5];
	bitdex &= 31;
	MASSERT((*bitptr & (1 << bitdex)) != 0);
	*bitptr &= ~(1 << bitdex);
}

#endif

/*
 * _vmem_alloc()
 *
 *	Directly map memory in PAGE_SIZE'd chunks with the specified
 *	alignment.
 *
 *	Alignment must be a multiple of PAGE_SIZE.
 *
 *	Size must be >= alignment.
 */
static void *
_vmem_alloc(size_t size, size_t align, int flags)
{
	char *addr;
	char *save;
	size_t excess;

	/*
	 * Map anonymous private memory.
	 */
	addr = mmap(NULL, size, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE|MAP_ANON, -1, 0);
	if (addr == MAP_FAILED)
		return(NULL);

	/*
	 * Check alignment.  The misaligned offset is also the excess
	 * amount.  If misaligned unmap the excess so we have a chance of
	 * mapping at the next alignment point and recursively try again.
	 *
	 * BBBBBBBBBBB BBBBBBBBBBB BBBBBBBBBBB	block alignment
	 *   aaaaaaaaa aaaaaaaaaaa aa		mis-aligned allocation
	 *   xxxxxxxxx				final excess calculation
	 *   ^ returned address
	 */
	excess = (uintptr_t)addr & (align - 1);

	if (excess) {
		excess = align - excess;
		save = addr;

		munmap(save + excess, size - excess);
		addr = _vmem_alloc(size, align, flags);
		munmap(save, excess);
	}
	return((void *)addr);
}

/*
 * _vmem_free()
 *
 *	Free a chunk of memory allocated with _vmem_alloc()
 */
static void
_vmem_free(void *ptr, size_t size)
{
	munmap(ptr, size);
}

/*
 * Panic on fatal conditions
 */
static void
_mpanic(const char *ctl, ...)
{
	va_list va;

	if (malloc_panic == 0) {
		malloc_panic = 1;
		va_start(va, ctl);
		vfprintf(stderr, ctl, va);
		fprintf(stderr, "\n");
		fflush(stderr);
		va_end(va);
	}
	abort();
}
