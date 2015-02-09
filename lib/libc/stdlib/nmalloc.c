/*
 * NMALLOC.C	- New Malloc (ported from kernel slab allocator)
 *
 * Copyright (c) 2003,2004,2009,2010 The DragonFly Project. All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and by 
 * Venkatesh Srinivas <me@endeavour.zapto.org>.
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
 *
 * $Id: nmalloc.c,v 1.37 2010/07/23 08:20:35 vsrinivas Exp $
 */
/*
 * This module implements a slab allocator drop-in replacement for the
 * libc malloc().
 *
 * A slab allocator reserves a ZONE for each chunk size, then lays the
 * chunks out in an array within the zone.  Allocation and deallocation
 * is nearly instantaneous, and overhead losses are limited to a fixed
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
 *
 * Multithreaded enhancements for small allocations introduced August 2010.
 * These are in the spirit of 'libumem'. See:
 *	Bonwick, J.; Adams, J. (2001). "Magazines and Vmem: Extending the
 *	slab allocator to many CPUs and arbitrary resources". In Proc. 2001 
 * 	USENIX Technical Conference. USENIX Association.
 *
 * TUNING
 *
 * The value of the environment variable MALLOC_OPTIONS is a character string
 * containing various flags to tune nmalloc.
 *
 * 'U'   / ['u']	Generate / do not generate utrace entries for ktrace(1)
 *			This will generate utrace events for all malloc, 
 *			realloc, and free calls. There are tools (mtrplay) to
 *			replay and allocation pattern or to graph heap structure
 *			(mtrgraph) which can interpret these logs.
 * 'Z'   / ['z']	Zero out / do not zero all allocations.
 *			Each new byte of memory allocated by malloc, realloc, or
 *			reallocf will be initialized to 0. This is intended for
 *			debugging and will affect performance negatively.
 * 'H'	/  ['h']	Pass a hint to the kernel about pages unused by the
 *			allocation functions. 
 */

/* cc -shared -fPIC -g -O -I/usr/src/lib/libc/include -o nmalloc.so nmalloc.c */

#include "libc_private.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/ktrace.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "spinlock.h"
#include "un-namespace.h"

/*
 * Linked list of large allocations
 */
typedef struct bigalloc {
	struct bigalloc *next;	/* hash link */
	void	*base;		/* base pointer */
	u_long	bytes;		/* bytes allocated */
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
	int32_t		z_Magic;	/* magic number for sanity check */
	int		z_NFree;	/* total free chunks / ualloc space */
	struct slzone *z_Next;		/* ZoneAry[] link if z_NFree non-zero */
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
	int		JunkIndex;
} *slglobaldata_t;

#define SLZF_UNOTZEROD		0x0001

#define FASTSLABREALLOC		0x02

/*
 * Misc constants.  Note that allocations that are exact multiples of
 * PAGE_SIZE, or exceed the zone limit, fall through to the kmem module.
 * IN_SAME_PAGE_MASK is used to sanity-check the per-page free lists.
 */
#define MIN_CHUNK_SIZE		8		/* in bytes */
#define MIN_CHUNK_MASK		(MIN_CHUNK_SIZE - 1)
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
 * Magazines 
 */

#define M_MAX_ROUNDS	64
#define M_ZONE_ROUNDS	64
#define M_LOW_ROUNDS	32
#define M_INIT_ROUNDS	8
#define M_BURST_FACTOR  8
#define M_BURST_NSCALE	2

#define M_BURST		0x0001
#define M_BURST_EARLY	0x0002

struct magazine {
	SLIST_ENTRY(magazine) nextmagazine;

	int		flags;
	int 		capacity;	/* Max rounds in this magazine */
	int 		rounds;		/* Current number of free rounds */ 
	int		burst_factor;	/* Number of blocks to prefill with */
	int 		low_factor;	/* Free till low_factor from full mag */
	void		*objects[M_MAX_ROUNDS];
};

SLIST_HEAD(magazinelist, magazine);

static spinlock_t zone_mag_lock;
static struct magazine zone_magazine = {
	.flags = M_BURST | M_BURST_EARLY,
	.capacity = M_ZONE_ROUNDS,
	.rounds = 0,
	.burst_factor = M_BURST_FACTOR,
	.low_factor = M_LOW_ROUNDS
};

#define MAGAZINE_FULL(mp)	(mp->rounds == mp->capacity)
#define MAGAZINE_NOTFULL(mp)	(mp->rounds < mp->capacity)
#define MAGAZINE_EMPTY(mp)	(mp->rounds == 0)
#define MAGAZINE_NOTEMPTY(mp)	(mp->rounds != 0)

/* Each thread will have a pair of magazines per size-class (NZONES)
 * The loaded magazine will support immediate allocations, the previous
 * magazine will either be full or empty and can be swapped at need */
typedef struct magazine_pair {
	struct magazine	*loaded;
	struct magazine	*prev;
} magazine_pair;

/* A depot is a collection of magazines for a single zone. */
typedef struct magazine_depot {
	struct magazinelist full;
	struct magazinelist empty;
	spinlock_t	lock;
} magazine_depot;

typedef struct thr_mags {
	magazine_pair	mags[NZONES];
	struct magazine	*newmag;
	int		init;
} thr_mags;

/*
 * With this attribute set, do not require a function call for accessing
 * this variable when the code is compiled -fPIC. Empty for libc_rtld
 * (like __thread).
 */
#ifdef __LIBC_RTLD
#define TLS_ATTRIBUTE
#else
#define TLS_ATTRIBUTE __attribute__ ((tls_model ("initial-exec")))
#endif

static int mtmagazine_free_live;
static __thread thr_mags thread_mags TLS_ATTRIBUTE;
static pthread_key_t thread_mags_key;
static pthread_once_t thread_mags_once = PTHREAD_ONCE_INIT;
static magazine_depot depots[NZONES];

/*
 * Fixed globals (not per-cpu)
 */
static const int ZoneSize = ZALLOC_ZONE_SIZE;
static const int ZoneLimit = ZALLOC_ZONE_LIMIT;
static const int ZonePageCount = ZALLOC_ZONE_SIZE / PAGE_SIZE;
static const int ZoneMask = ZALLOC_ZONE_SIZE - 1;

static int opt_madvise = 0;
static int opt_utrace = 0;
static int g_malloc_flags = 0;
static struct slglobaldata	SLGlobalData;
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

static void *_slaballoc(size_t size, int flags);
static void *_slabrealloc(void *ptr, size_t size);
static void _slabfree(void *ptr, int, bigalloc_t *);
static void *_vmem_alloc(size_t bytes, size_t align, int flags);
static void _vmem_free(void *ptr, size_t bytes);
static void *magazine_alloc(struct magazine *, int *);
static int magazine_free(struct magazine *, void *);
static void *mtmagazine_alloc(int zi);
static int mtmagazine_free(int zi, void *);
static void mtmagazine_init(void);
static void mtmagazine_destructor(void *);
static slzone_t zone_alloc(int flags);
static void zone_free(void *z);
static void _mpanic(const char *ctl, ...) __printflike(1, 2);
static void malloc_init(void) __constructor(101);
#if defined(INVARIANTS)
static void chunk_mark_allocated(slzone_t z, void *chunk);
static void chunk_mark_free(slzone_t z, void *chunk);
#endif

struct nmalloc_utrace {
	void *p;
	size_t s;
	void *r;
};

#define UTRACE(a, b, c)						\
	if (opt_utrace) {					\
		struct nmalloc_utrace ut = {			\
			.p = (a),				\
			.s = (b),				\
			.r = (c)				\
		};						\
		utrace(&ut, sizeof(ut));			\
	}

#ifdef INVARIANTS
/*
 * If enabled any memory allocated without M_ZERO is initialized to -1.
 */
static int  use_malloc_pattern;
#endif

static void
malloc_init(void)
{
	const char *p = NULL;

	if (issetugid() == 0) 
		p = getenv("MALLOC_OPTIONS");

	for (; p != NULL && *p != '\0'; p++) {
		switch(*p) {
		case 'u':	opt_utrace = 0; break;
		case 'U':	opt_utrace = 1; break;
		case 'h':	opt_madvise = 0; break;
		case 'H':	opt_madvise = 1; break;
		case 'z':	g_malloc_flags = 0; break;
		case 'Z': 	g_malloc_flags = SAFLAG_ZERO; break;
		default:
			break;
		}
	}

	UTRACE((void *) -1, 0, NULL);
}

/*
 * We have to install a handler for nmalloc thread teardowns when
 * the thread is created.  We cannot delay this because destructors in
 * sophisticated userland programs can call malloc() for the first time
 * during their thread exit.
 *
 * This routine is called directly from pthreads.
 */
void
_nmalloc_thr_init(void)
{
	thr_mags *tp;

	/*
	 * Disallow mtmagazine operations until the mtmagazine is
	 * initialized.
	 */
	tp = &thread_mags;
	tp->init = -1;

	if (mtmagazine_free_live == 0) {
		mtmagazine_free_live = 1;
		pthread_once(&thread_mags_once, mtmagazine_init);
	}
	pthread_setspecific(thread_mags_key, tp);
	tp->init = 1;
}

/*
 * Thread locks.
 */
static __inline void
slgd_lock(slglobaldata_t slgd)
{
	if (__isthreaded)
		_SPINLOCK(&slgd->Spinlock);
}

static __inline void
slgd_unlock(slglobaldata_t slgd)
{
	if (__isthreaded)
		_SPINUNLOCK(&slgd->Spinlock);
}

static __inline void
depot_lock(magazine_depot *dp) 
{
	if (__isthreaded)
		_SPINLOCK(&dp->lock);
}

static __inline void
depot_unlock(magazine_depot *dp)
{
	if (__isthreaded)
		_SPINUNLOCK(&dp->lock);
}

static __inline void
zone_magazine_lock(void)
{
	if (__isthreaded)
		_SPINLOCK(&zone_mag_lock);
}

static __inline void
zone_magazine_unlock(void)
{
	if (__isthreaded)
		_SPINUNLOCK(&zone_mag_lock);
}

static __inline void
swap_mags(magazine_pair *mp)
{
	struct magazine *tmp;
	tmp = mp->loaded;
	mp->loaded = mp->prev;
	mp->prev = tmp;
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
	_mpanic("Unexpected byte count %zu", n);
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
	else
		UTRACE(0, size, ptr);
	return(ptr);
}

#define MUL_NO_OVERFLOW	(1UL << (sizeof(size_t) * 4))

/*
 * calloc() - call internal slab allocator
 */
void *
calloc(size_t number, size_t size)
{
	void *ptr;

	if ((number >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
	     number > 0 && SIZE_MAX / number < size) {
		errno = ENOMEM;
		return(NULL);
	}

	ptr = _slaballoc(number * size, SAFLAG_ZERO);
	if (ptr == NULL)
		errno = ENOMEM;
	else
		UTRACE(0, number * size, ptr);
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
	void *ret;
	ret = _slabrealloc(ptr, size);
	if (ret == NULL)
		errno = ENOMEM;
	else
		UTRACE(ptr, size, ret);
	return(ret);
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
	int zi __unused;

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
	UTRACE(ptr, 0, 0);
	_slabfree(ptr, 0, NULL);
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
	void *obj;

	/*
	 * Handle the degenerate size == 0 case.  Yes, this does happen.
	 * Return a special pointer.  This is to maintain compatibility with
	 * the original malloc implementation.  Certain devices, such as the
	 * adaptec driver, not only allocate 0 bytes, they check for NULL and
	 * also realloc() later on.  Joy.
	 */
	if (size == 0)
		return(ZERO_LENGTH_PTR);

	/* Capture global flags */
	flags |= g_malloc_flags;

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

		/*
		 * Page-align and cache-color in case of virtually indexed
		 * physically tagged L1 caches (aka SandyBridge).  No sweat
		 * otherwise, so just do it.
		 */
		size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
		if ((size & 8191) == 0)
			size += 4096;

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
		big->next = *bigp;
		*bigp = big;
		bigalloc_unlock(chunk);

		return(chunk);
	}

	/* Compute allocation zone; zoneindex will panic on excessive sizes */
	zi = zoneindex(&size, &chunking);
	MASSERT(zi < NZONES);

	obj = mtmagazine_alloc(zi);
	if (obj != NULL) {
		if (flags & SAFLAG_ZERO)
			bzero(obj, size);
		return (obj);
	}

	slgd = &SLGlobalData;
	slgd_lock(slgd);

	/*
	 * Attempt to allocate out of an existing zone.  If all zones are
	 * exhausted pull one off the free list or allocate a new one.
	 */
	if ((z = slgd->ZoneAry[zi]) == NULL) {
		z = zone_alloc(flags);
		if (z == NULL)
			goto fail;

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
		 * Guarantee power-of-2 alignment for power-of-2-sized
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
		z->z_ZoneIndex = zi;
		z->z_NMax = (ZoneSize - off) / size;
		z->z_NFree = z->z_NMax;
		z->z_BasePtr = (char *)z + off;
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

	if (ptr == NULL || ptr == ZERO_LENGTH_PTR) {
		return(_slaballoc(size, 0));
	}

	if (size == 0) {
		free(ptr);
		return(ZERO_LENGTH_PTR);
	}

	/*
	 * Handle oversized allocations. 
	 */
	if ((bigp = bigalloc_check_and_lock(ptr)) != NULL) {
		bigalloc_t big;
		size_t bigbytes;

		while ((big = *bigp) != NULL) {
			if (big->base == ptr) {
				size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
				bigbytes = big->bytes;
				if (bigbytes == size) {
					bigalloc_unlock(ptr);
					return(ptr);
				}
				*bigp = big->next;
				bigalloc_unlock(ptr);
				if ((nptr = _slaballoc(size, 0)) == NULL) {
					/* Relink block */
					bigp = bigalloc_lock(ptr);
					big->next = *bigp;
					*bigp = big;
					bigalloc_unlock(ptr);
					return(NULL);
				}
				if (size > bigbytes)
					size = bigbytes;
				bcopy(ptr, nptr, size);
				_slabfree(ptr, FASTSLABREALLOC, &big);
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
		if (z->z_ChunkSize == size) {
			return(ptr);
		}
	}

	/*
	 * Allocate memory for the new request size and copy as appropriate.
	 */
	if ((nptr = _slaballoc(size, 0)) != NULL) {
		if (size > z->z_ChunkSize)
			size = z->z_ChunkSize;
		bcopy(ptr, nptr, size);
		_slabfree(ptr, 0, NULL);
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
 * flags:
 *	FASTSLABREALLOC		Fast call from realloc, *rbigp already
 *				unlinked.
 *
 * MPSAFE
 */
static void
_slabfree(void *ptr, int flags, bigalloc_t *rbigp)
{
	slzone_t z;
	slchunk_t chunk;
	bigalloc_t big;
	bigalloc_t *bigp;
	slglobaldata_t slgd;
	size_t size;
	int zi;
	int pgno;

	/* Fast realloc path for big allocations */
	if (flags & FASTSLABREALLOC) {
		big = *rbigp;
		goto fastslabrealloc;
	}

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
fastslabrealloc:
				size = big->bytes;
				_slabfree(big, 0, NULL);
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

	size = z->z_ChunkSize;
	zi = z->z_ZoneIndex;

	if (g_malloc_flags & SAFLAG_ZERO)
		bzero(ptr, size);

	if (mtmagazine_free(zi, ptr) == 0)
		return;

	pgno = ((char *)ptr - (char *)z) >> PAGE_SHIFT;
	chunk = ptr;
	slgd = &SLGlobalData;
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
	 * If the zone becomes totally free then release it.
	 */
	if (z->z_NFree == z->z_NMax) {
		slzone_t *pz;

		pz = &slgd->ZoneAry[z->z_ZoneIndex];
		while (z != *pz)
			pz = &(*pz)->z_Next;
		*pz = z->z_Next;
		z->z_Magic = -1;
		z->z_Next = NULL;
		zone_free(z);
		/* slgd lock released */
		return;
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
 * Allocate and return a magazine.  NULL is returned and *burst is adjusted
 * if the magazine is empty.
 */
static __inline void *
magazine_alloc(struct magazine *mp, int *burst)
{
	void *obj;

	if (mp == NULL)
		return(NULL);
	if (MAGAZINE_NOTEMPTY(mp)) {
		obj = mp->objects[--mp->rounds];
		return(obj);
	}

	/*
	 * Return burst factor to caller along with NULL
	 */
	if ((mp->flags & M_BURST) && (burst != NULL)) {
		*burst = mp->burst_factor;
	}
	/* Reduce burst factor by NSCALE; if it hits 1, disable BURST */
	if ((mp->flags & M_BURST) && (mp->flags & M_BURST_EARLY) &&
	    (burst != NULL)) {
		mp->burst_factor -= M_BURST_NSCALE;
		if (mp->burst_factor <= 1) {
			mp->burst_factor = 1;
			mp->flags &= ~(M_BURST);
			mp->flags &= ~(M_BURST_EARLY);
		}
	}
	return (NULL);
}

static __inline int
magazine_free(struct magazine *mp, void *p)
{
	if (mp != NULL && MAGAZINE_NOTFULL(mp)) {
		mp->objects[mp->rounds++] = p;
		return 0;
	}

	return -1;
}

static void *
mtmagazine_alloc(int zi)
{
	thr_mags *tp;
	struct magazine *mp, *emptymag;
	magazine_depot *d;
	void *obj;

	/*
	 * Do not try to access per-thread magazines while the mtmagazine
	 * is being initialized or destroyed.
	 */
	tp = &thread_mags;
	if (tp->init < 0)
		return(NULL);

	/*
	 * Primary per-thread allocation loop
	 */
	for (;;) {
		/*
		 * If the loaded magazine has rounds, allocate and return
		 */
		mp = tp->mags[zi].loaded;
		obj = magazine_alloc(mp, NULL);
		if (obj)
			break;

		/*
		 * If the prev magazine is full, swap with the loaded
		 * magazine and retry.
		 */
		mp = tp->mags[zi].prev;
		if (mp && MAGAZINE_FULL(mp)) {
			MASSERT(mp->rounds != 0);
			swap_mags(&tp->mags[zi]);	/* prev now empty */
			continue;
		}

		/*
		 * Try to get a full magazine from the depot.  Cycle
		 * through depot(full)->loaded->prev->depot(empty).
		 * Retry if a full magazine was available from the depot.
		 *
		 * Return NULL (caller will fall through) if no magazines
		 * can be found anywhere.
		 */
		d = &depots[zi];
		depot_lock(d);
		emptymag = tp->mags[zi].prev;
		if (emptymag)
			SLIST_INSERT_HEAD(&d->empty, emptymag, nextmagazine);
		tp->mags[zi].prev = tp->mags[zi].loaded;
		mp = SLIST_FIRST(&d->full);	/* loaded magazine */
		tp->mags[zi].loaded = mp;
		if (mp) {
			SLIST_REMOVE_HEAD(&d->full, nextmagazine);
			MASSERT(MAGAZINE_NOTEMPTY(mp));
			depot_unlock(d);
			continue;
		}
		depot_unlock(d);
		break;
	} 

	return (obj);
}

static int
mtmagazine_free(int zi, void *ptr)
{
	thr_mags *tp;
	struct magazine *mp, *loadedmag;
	magazine_depot *d;
	int rc = -1;

	/*
	 * Do not try to access per-thread magazines while the mtmagazine
	 * is being initialized or destroyed.
	 */
	tp = &thread_mags;
	if (tp->init < 0)
		return(-1);

	/*
	 * Primary per-thread freeing loop
	 */
	for (;;) {
		/*
		 * Make sure a new magazine is available in case we have
		 * to use it.  Staging the newmag allows us to avoid
		 * some locking/reentrancy complexity.
		 *
		 * Temporarily disable the per-thread caches for this
		 * allocation to avoid reentrancy and/or to avoid a
		 * stack overflow if the [zi] happens to be the same that
		 * would be used to allocate the new magazine.
		 */
		if (tp->newmag == NULL) {
			tp->init = -1;
			tp->newmag = _slaballoc(sizeof(struct magazine),
						SAFLAG_ZERO);
			tp->init = 1;
			if (tp->newmag == NULL) {
				rc = -1;
				break;
			}
		}

		/*
		 * If the loaded magazine has space, free directly to it
		 */
		rc = magazine_free(tp->mags[zi].loaded, ptr);
		if (rc == 0)
			break;

		/*
		 * If the prev magazine is empty, swap with the loaded
		 * magazine and retry.
		 */
		mp = tp->mags[zi].prev;
		if (mp && MAGAZINE_EMPTY(mp)) {
			MASSERT(mp->rounds == 0);
			swap_mags(&tp->mags[zi]);	/* prev now full */
			continue;
		}

		/*
		 * Try to get an empty magazine from the depot.  Cycle
		 * through depot(empty)->loaded->prev->depot(full).
		 * Retry if an empty magazine was available from the depot.
		 */
		d = &depots[zi];
		depot_lock(d);

		if ((loadedmag = tp->mags[zi].prev) != NULL)
			SLIST_INSERT_HEAD(&d->full, loadedmag, nextmagazine);
		tp->mags[zi].prev = tp->mags[zi].loaded;
		mp = SLIST_FIRST(&d->empty);
		if (mp) {
			tp->mags[zi].loaded = mp;
			SLIST_REMOVE_HEAD(&d->empty, nextmagazine);
			MASSERT(MAGAZINE_NOTFULL(mp));
		} else {
			mp = tp->newmag;
			tp->newmag = NULL;
			mp->capacity = M_MAX_ROUNDS;
			mp->rounds = 0;
			mp->flags = 0;
			tp->mags[zi].loaded = mp;
		}
		depot_unlock(d);
	} 

	return rc;
}

static void 
mtmagazine_init(void)
{
	int error;

	error = pthread_key_create(&thread_mags_key, mtmagazine_destructor);
	if (error)
		abort();
}

/*
 * This function is only used by the thread exit destructor
 */
static void
mtmagazine_drain(struct magazine *mp)
{
	void *obj;

	while (MAGAZINE_NOTEMPTY(mp)) {
		obj = magazine_alloc(mp, NULL);
		_slabfree(obj, 0, NULL);
	}
}

/* 
 * mtmagazine_destructor()
 *
 * When a thread exits, we reclaim all its resources; all its magazines are
 * drained and the structures are freed. 
 *
 * WARNING!  The destructor can be called multiple times if the larger user
 *	     program has its own destructors which run after ours which
 *	     allocate or free memory.
 */
static void
mtmagazine_destructor(void *thrp)
{
	thr_mags *tp = thrp;
	struct magazine *mp;
	int i;

	/*
	 * Prevent further use of mtmagazines while we are destructing
	 * them, as well as for any destructors which are run after us
	 * prior to the thread actually being destroyed.
	 */
	tp->init = -1;

	for (i = 0; i < NZONES; i++) {
		mp = tp->mags[i].loaded;
		tp->mags[i].loaded = NULL;
		if (mp) {
			if (MAGAZINE_NOTEMPTY(mp))
				mtmagazine_drain(mp);
			_slabfree(mp, 0, NULL);
		}

		mp = tp->mags[i].prev;
		tp->mags[i].prev = NULL;
		if (mp) {
			if (MAGAZINE_NOTEMPTY(mp))
				mtmagazine_drain(mp);
			_slabfree(mp, 0, NULL);
		}
	}

	if (tp->newmag) {
		mp = tp->newmag;
		tp->newmag = NULL;
		_slabfree(mp, 0, NULL);
	}
}

/*
 * zone_alloc()
 *
 * Attempt to allocate a zone from the zone magazine; the zone magazine has
 * M_BURST_EARLY enabled, so honor the burst request from the magazine.
 */
static slzone_t
zone_alloc(int flags) 
{
	slglobaldata_t slgd = &SLGlobalData;
	int burst = 1;
	int i, j;
	slzone_t z;

	zone_magazine_lock();
	slgd_unlock(slgd);

	z = magazine_alloc(&zone_magazine, &burst);
	if (z == NULL && burst == 1) {
		zone_magazine_unlock();
		z = _vmem_alloc(ZoneSize * burst, ZoneSize, flags);
	} else if (z == NULL) {
		z = _vmem_alloc(ZoneSize * burst, ZoneSize, flags);
		if (z) {
			for (i = 1; i < burst; i++) {
				j = magazine_free(&zone_magazine,
						  (char *) z + (ZoneSize * i));
				MASSERT(j == 0);
			}
		}
		zone_magazine_unlock();
	} else {
		z->z_Flags |= SLZF_UNOTZEROD;
		zone_magazine_unlock();
	}
	slgd_lock(slgd);
	return z;
}

/*
 * zone_free()
 *
 * Release a zone and unlock the slgd lock.
 */
static void
zone_free(void *z)
{
	slglobaldata_t slgd = &SLGlobalData;
	void *excess[M_ZONE_ROUNDS - M_LOW_ROUNDS] = {};
	int i, j;

	zone_magazine_lock();
	slgd_unlock(slgd);
	
	bzero(z, sizeof(struct slzone));

	if (opt_madvise)
		madvise(z, ZoneSize, MADV_FREE);

	i = magazine_free(&zone_magazine, z);

	/*
	 * If we failed to free, collect excess magazines; release the zone
	 * magazine lock, and then free to the system via _vmem_free. Re-enable
	 * BURST mode for the magazine.
	 */
	if (i == -1) {
		j = zone_magazine.rounds - zone_magazine.low_factor;
		for (i = 0; i < j; i++) {
			excess[i] = magazine_alloc(&zone_magazine, NULL);
			MASSERT(excess[i] !=  NULL);
		}

		zone_magazine_unlock();

		for (i = 0; i < j; i++) 
			_vmem_free(excess[i], ZoneSize);

		_vmem_free(z, ZoneSize);
	} else {
		zone_magazine_unlock();
	}
}

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
