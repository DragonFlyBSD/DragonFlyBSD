/*
 * DMALLOC.C	- Dillon's malloc
 *
 * Copyright (c) 2011 The DragonFly Project. All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>.
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
 * This module implements a modified slab allocator drop-in replacement for
 * the libc malloc().  The slab algorithm has been adjusted to support dynamic
 * sizing of slabs which effectively allows slabs to be used for allocations of
 * any size.  Because of this we neither have a small-block allocator or a
 * big-block allocator and the code paths are simplified to the point where
 * allocations, caching, and freeing, is screaming fast.
 *
 * There is very little interaction between threads.  A global depot accessed
 * via atomic cmpxchg instructions (only! no spinlocks!) is used as a
 * catch-all and to deal with thread exits and such.
 *
 * To support dynamic slab sizing available user virtual memory is broken
 * down into ~1024 regions.  Each region has fixed slab size whos value is
 * set when the region is opened up for use.  The free() path simply applies
 * a mask based on the region to the pointer to acquire the base of the
 * governing slab structure.
 *
 * Regions[NREGIONS]	(1024)
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
 *	32768-65535	4096		8
 *	... continues unlimited ...	4 zones
 *
 *	For a 2^63 memory space each doubling >= 64K is broken down into
 *	4 chunking zones, so we support 88 + (48 * 4) = 280 zones.
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
 *				FUTURE FEATURES
 *
 *    + [better] garbage collection
 *    + better initial sizing.
 *
 * TUNING
 *
 * The value of the environment variable MALLOC_OPTIONS is a character string
 * containing various flags to tune nmalloc.  Upper case letters enabled
 * or increase the feature, lower case disables or decreases the feature.
 *
 * U		Enable UTRACE for all operations, observable with ktrace.
 *		Diasbled by default.
 *
 * Z		Zero out allocations, otherwise allocations (except for
 *		calloc) will contain garbage.
 *		Disabled by default.
 *
 * H		Pass a hint with madvise() about unused pages.
 *		Disabled by default.
 *		Not currently implemented.
 *
 * F		Disable local per-thread caching.
 *		Disabled by default.
 *
 * C		Increase (decrease) how much excess cache to retain.
 *		Set to 4 by default.
 */

/* cc -shared -fPIC -g -O -I/usr/src/lib/libc/include -o dmalloc.so dmalloc.c */

#ifndef STANDALONE_DEBUG
#include "libc_private.h"
#endif

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
#include <limits.h>

#include <machine/atomic.h>
#include <machine/cpufunc.h>

#ifdef STANDALONE_DEBUG
void _nmalloc_thr_init(void);
#else
#include "spinlock.h"
#include "un-namespace.h"
#endif

#ifndef MAP_SIZEALIGN
#define MAP_SIZEALIGN	0
#endif

#if SSIZE_MAX == 0x7FFFFFFF
#define ADDRBITS	32
#define UVM_BITS	32	/* worst case */
#else
#define ADDRBITS	64
#define UVM_BITS	48	/* worst case XXX */
#endif

#if LONG_MAX == 0x7FFFFFFF
#define LONG_BITS	32
#define LONG_BITS_SHIFT	5
#else
#define LONG_BITS	64
#define LONG_BITS_SHIFT	6
#endif

#define LOCKEDPTR	((void *)(intptr_t)-1)

/*
 * Regions[]
 */
#define NREGIONS_BITS	10
#define NREGIONS	(1 << NREGIONS_BITS)
#define NREGIONS_MASK	(NREGIONS - 1)
#define NREGIONS_SHIFT	(UVM_BITS - NREGIONS_BITS)
#define NREGIONS_SIZE	(1LU << NREGIONS_SHIFT)

typedef struct region *region_t;
typedef struct slglobaldata *slglobaldata_t;
typedef struct slab *slab_t;

struct region {
	uintptr_t	mask;
	slab_t		slab;	/* conditional out of band slab */
};

static struct region Regions[NREGIONS];

/*
 * Number of chunking zones available
 */
#define CHUNKFACTOR	8
#if ADDRBITS == 32
#define NZONES		(16 + 9 * CHUNKFACTOR + 16 * CHUNKFACTOR)
#else
#define NZONES		(16 + 9 * CHUNKFACTOR + 48 * CHUNKFACTOR)
#endif

static int MaxChunks[NZONES];

#define NDEPOTS		8		/* must be power of 2 */

/*
 * Maximum number of chunks per slab, governed by the allocation bitmap in
 * each slab.  The maximum is reduced for large chunk sizes.
 */
#define MAXCHUNKS	(LONG_BITS * LONG_BITS)
#define MAXCHUNKS_BITS	(LONG_BITS_SHIFT * LONG_BITS_SHIFT)
#define LITSLABSIZE	(32 * 1024)
#define NOMSLABSIZE	(2 * 1024 * 1024)
#define BIGSLABSIZE	(128 * 1024 * 1024)

#define ZALLOC_SLAB_MAGIC	0x736c6162	/* magic sanity */

TAILQ_HEAD(slab_list, slab);

/*
 * A slab structure
 */
struct slab {
	struct slab	*next;		/* slabs with available space */
	TAILQ_ENTRY(slab) entry;
	int32_t		magic;		/* magic number for sanity check */
	u_int		navail;		/* number of free elements available */
	u_int		nmax;
	u_int		free_bit;	/* free hint bitno */
	u_int		free_index;	/* free hint index */
	u_long		bitmap[LONG_BITS]; /* free chunks */
	size_t		slab_size;	/* size of entire slab */
	size_t		chunk_size;	/* chunk size for validation */
	int		zone_index;
	enum { UNKNOWN, AVAIL, EMPTY, FULL } state;
	int		flags;
	region_t	region;		/* related region */
	char		*chunks;	/* chunk base */
	slglobaldata_t	slgd;
};

/*
 * per-thread data
 */
struct slglobaldata {
	struct zoneinfo {
		slab_t	avail_base;
		slab_t	empty_base;
		int	best_region;
		int	empty_count;
	} zone[NZONES];
	struct slab_list full_zones;		/* via entry */
	int		masked;
	int		biggest_index;
	size_t		nslabs;
	slglobaldata_t	sldepot;
};

#define SLAB_ZEROD		0x0001

/*
 * Misc constants.  Note that allocations that are exact multiples of
 * PAGE_SIZE, or exceed the zone limit, fall through to the kmem module.
 * IN_SAME_PAGE_MASK is used to sanity-check the per-page free lists.
 */
#define MIN_CHUNK_SIZE		8		/* in bytes */
#define MIN_CHUNK_MASK		(MIN_CHUNK_SIZE - 1)

#define SAFLAG_ZERO	0x00000001

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

#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

/*
 * Thread control
 */

#define MASSERT(exp)	do { if (__predict_false(!(exp)))	\
				_mpanic("assertion: %s in %s",	\
				#exp, __func__);		\
			    } while (0)

/* With this attribute set, do not require a function call for accessing
 * this variable when the code is compiled -fPIC */
#define TLS_ATTRIBUTE __attribute__ ((tls_model ("initial-exec")));

static __thread struct slglobaldata slglobal TLS_ATTRIBUTE;
static pthread_key_t thread_malloc_key;
static pthread_once_t thread_malloc_once = PTHREAD_ONCE_INIT;
static struct slglobaldata sldepots[NDEPOTS];

static int opt_madvise = 0;
static int opt_free = 0;
static int opt_cache = 4;
static int opt_utrace = 0;
static int g_malloc_flags = 0;
static int malloc_panic;
static int malloc_started = 0;

static const int32_t weirdary[16] = {
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR,
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR,
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR,
	WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR, WEIRD_ADDR
};

static void *memalloc(size_t size, int flags);
static void *memrealloc(void *ptr, size_t size);
static void memfree(void *ptr, int);
static slab_t slaballoc(int zi, size_t chunking, size_t chunk_size);
static void slabfree(slab_t slab);
static void slabterm(slglobaldata_t slgd, slab_t slab);
static void *_vmem_alloc(int ri, size_t slab_size);
static void _vmem_free(void *ptr, size_t slab_size);
static void _mpanic(const char *ctl, ...) __printflike(1, 2);
#ifndef STANDALONE_DEBUG
static void malloc_init(void) __constructor(101);
#else
static void malloc_init(void) __constructor(101);
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
	static spinlock_t malloc_init_lock;

	if (malloc_started)
		return;

	if (__isthreaded) {
		_SPINLOCK(&malloc_init_lock);
		if (malloc_started) {
			_SPINUNLOCK(&malloc_init_lock);
			return;
		}
	}

	Regions[0].mask = -1; /* disallow activity in lowest region */

	if (issetugid() == 0)
		p = getenv("MALLOC_OPTIONS");

	for (; p != NULL && *p != '\0'; p++) {
		switch(*p) {
		case 'u':
			opt_utrace = 0;
			break;
		case 'U':
			opt_utrace = 1;
			break;
		case 'h':
			opt_madvise = 0;
			break;
		case 'H':
			opt_madvise = 1;
			break;
		case 'c':
			if (opt_cache > 0)
				--opt_cache;
			break;
		case 'C':
			++opt_cache;
			break;
		case 'f':
			opt_free = 0;
			break;
		case 'F':
			opt_free = 1;
			break;
		case 'z':
			g_malloc_flags = 0;
			break;
		case 'Z':
			g_malloc_flags = SAFLAG_ZERO;
			break;
		default:
			break;
		}
	}

	UTRACE((void *) -1, 0, NULL);
	_nmalloc_thr_init();
	malloc_started = 1;

	if (__isthreaded)
		_SPINUNLOCK(&malloc_init_lock);
}

/*
 * We have to install a handler for nmalloc thread teardowns when
 * the thread is created.  We cannot delay this because destructors in
 * sophisticated userland programs can call malloc() for the first time
 * during their thread exit.
 *
 * This routine is called directly from pthreads.
 */
static void _nmalloc_thr_init_once(void);
static void _nmalloc_thr_destructor(void *thrp);

void
_nmalloc_thr_init(void)
{
	static int did_init;
	static int SLGI;
	int slgi;

	slgi = SLGI++;
	cpu_ccfence();
	TAILQ_INIT(&slglobal.full_zones);
	slglobal.sldepot = &sldepots[slgi & (NDEPOTS - 1)];

	if (slglobal.masked)
		return;

	slglobal.masked = 1;
	if (did_init == 0) {
		did_init = 1;
		pthread_once(&thread_malloc_once, _nmalloc_thr_init_once);
	}
	pthread_setspecific(thread_malloc_key, &slglobal);
	slglobal.masked = 0;
}

/*
 * Called just once
 */
static void
_nmalloc_thr_init_once(void)
{
	int error;

	error = pthread_key_create(&thread_malloc_key, _nmalloc_thr_destructor);
	if (error)
		abort();
}

/*
 * Called for each thread undergoing exit
 *
 * Move all of the thread's slabs into a depot.
 */
static void
_nmalloc_thr_destructor(void *thrp)
{
	slglobaldata_t slgd = thrp;
	slab_t slab;
	int i;

	slgd->masked = 1;

	for (i = 0; i <= slgd->biggest_index; i++) {
		while ((slab = slgd->zone[i].empty_base) != NULL) {
			slgd->zone[i].empty_base = slab->next;
			slabterm(slgd, slab);
		}

		while ((slab = slgd->zone[i].avail_base) != NULL) {
			slgd->zone[i].avail_base = slab->next;
			slabterm(slgd, slab);
		}

		while ((slab = TAILQ_FIRST(&slgd->full_zones)) != NULL) {
			TAILQ_REMOVE(&slgd->full_zones, slab, entry);
			slabterm(slgd, slab);
		}
	}
}

/*
 * Calculate the zone index for the allocation request size and set the
 * allocation request size to that particular zone's chunk size.
 */
static __inline int
zoneindex(size_t *bytes, size_t *chunking)
{
	size_t n = (size_t)*bytes;
	size_t x;
	size_t c;
	int i;

	if (n < 128) {
		*bytes = n = (n + 7) & ~7;
		*chunking = 8;
		return(n / 8);			/* 8 byte chunks, 16 zones */
	}
	if (n < 4096) {
		x = 256;
		c = x / (CHUNKFACTOR * 2);
		i = 16;
	} else {
		x = 8192;
		c = x / (CHUNKFACTOR * 2);
		i = 16 + CHUNKFACTOR * 5;  /* 256->512,1024,2048,4096,8192 */
	}
	while (n >= x) {
		x <<= 1;
		c <<= 1;
		i += CHUNKFACTOR;
		if (x == 0)
			_mpanic("slaballoc: byte value too high");
	}
	*bytes = n = (n + c - 1) & ~(c - 1);
	*chunking = c;
	return (i + n / c - CHUNKFACTOR);
#if 0
	*bytes = n = (n + c - 1) & ~(c - 1);
	*chunking = c;
	return (n / c + i);

	if (n < 256) {
		*bytes = n = (n + 15) & ~15;
		*chunking = 16;
		return(n / (CHUNKINGLO*2) + CHUNKINGLO*1 - 1);
	}
	if (n < 8192) {
		if (n < 512) {
			*bytes = n = (n + 31) & ~31;
			*chunking = 32;
			return(n / (CHUNKINGLO*4) + CHUNKINGLO*2 - 1);
		}
		if (n < 1024) {
			*bytes = n = (n + 63) & ~63;
			*chunking = 64;
			return(n / (CHUNKINGLO*8) + CHUNKINGLO*3 - 1);
		}
		if (n < 2048) {
			*bytes = n = (n + 127) & ~127;
			*chunking = 128;
			return(n / (CHUNKINGLO*16) + CHUNKINGLO*4 - 1);
		}
		if (n < 4096) {
			*bytes = n = (n + 255) & ~255;
			*chunking = 256;
			return(n / (CHUNKINGLO*32) + CHUNKINGLO*5 - 1);
		}
		*bytes = n = (n + 511) & ~511;
		*chunking = 512;
		return(n / (CHUNKINGLO*64) + CHUNKINGLO*6 - 1);
	}
	if (n < 16384) {
		*bytes = n = (n + 1023) & ~1023;
		*chunking = 1024;
		return(n / (CHUNKINGLO*128) + CHUNKINGLO*7 - 1);
	}
	if (n < 32768) {				/* 16384-32767 */
		*bytes = n = (n + 2047) & ~2047;
		*chunking = 2048;
		return(n / (CHUNKINGLO*256) + CHUNKINGLO*8 - 1);
	}
	if (n < 65536) {
		*bytes = n = (n + 4095) & ~4095;	/* 32768-65535 */
		*chunking = 4096;
		return(n / (CHUNKINGLO*512) + CHUNKINGLO*9 - 1);
	}

	x = 131072;
	c = 8192;
	i = CHUNKINGLO*10 - 1;

	while (n >= x) {
		x <<= 1;
		c <<= 1;
		i += CHUNKINGHI;
		if (x == 0)
			_mpanic("slaballoc: byte value too high");
	}
	*bytes = n = (n + c - 1) & ~(c - 1);
	*chunking = c;
	return (n / c + i);
#endif
}

/*
 * malloc() - call internal slab allocator
 */
void *
malloc(size_t size)
{
	void *ptr;

	ptr = memalloc(size, 0);
	if (ptr == NULL)
		errno = ENOMEM;
	else
		UTRACE(0, size, ptr);
	return(ptr);
}

/*
 * calloc() - call internal slab allocator
 */
void *
calloc(size_t number, size_t size)
{
	void *ptr;

	ptr = memalloc(number * size, SAFLAG_ZERO);
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

	if (ptr == NULL)
		ret = memalloc(size, 0);
	else
		ret = memrealloc(ptr, size);
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
 * The slab allocator will allocate on power-of-2 boundaries up to at least
 * PAGE_SIZE.  Otherwise we use the zoneindex mechanic to find a zone
 * matching the requirements.
 */
int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
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
	 * XXX for now just find the nearest power of 2 >= size and also
	 * >= alignment and allocate that.
	 */
	while (alignment < size) {
		alignment <<= 1;
		if (alignment == 0)
			_mpanic("posix_memalign: byte value too high");
	}
	*memptr = memalloc(alignment, 0);
	return(*memptr ? 0 : ENOMEM);
}

/*
 * free() (SLAB ALLOCATOR) - do the obvious
 */
void
free(void *ptr)
{
	if (ptr) {
		UTRACE(ptr, 0, 0);
		memfree(ptr, 0);
	}
}

/*
 * memalloc()	(SLAB ALLOCATOR)
 *
 *	Allocate memory via the slab allocator.
 */
static void *
memalloc(size_t size, int flags)
{
	slglobaldata_t slgd;
	struct zoneinfo *zinfo;
	slab_t slab;
	size_t chunking;
	int bmi;
	int bno;
	u_long *bmp;
	int zi;
#ifdef INVARIANTS
	int i;
#endif
	size_t off;
	char *obj;

	if (!malloc_started)
		malloc_init();

	/*
	 * If 0 bytes is requested we have to return a unique pointer, allocate
	 * at least one byte.
	 */
	if (size == 0)
		size = 1;

	/* Capture global flags */
	flags |= g_malloc_flags;

	/* Compute allocation zone; zoneindex will panic on excessive sizes */
	zi = zoneindex(&size, &chunking);
	MASSERT(zi < NZONES);
	if (size == 0)
		return(NULL);

	/*
	 * Locate a slab with available space.  If no slabs are available
	 * back-off to the empty list and if we still come up dry allocate
	 * a new slab (which will try the depot first).
	 */
retry:
	slgd = &slglobal;
	zinfo = &slgd->zone[zi];
	if ((slab = zinfo->avail_base) == NULL) {
		if ((slab = zinfo->empty_base) == NULL) {
			/*
			 * Still dry
			 */
			slab = slaballoc(zi, chunking, size);
			if (slab == NULL)
				return(NULL);
			slab->next = zinfo->avail_base;
			zinfo->avail_base = slab;
			slab->state = AVAIL;
			if (slgd->biggest_index < zi)
				slgd->biggest_index = zi;
			++slgd->nslabs;
		} else {
			/*
			 * Pulled from empty list
			 */
			zinfo->empty_base = slab->next;
			slab->next = zinfo->avail_base;
			zinfo->avail_base = slab;
			slab->state = AVAIL;
			--zinfo->empty_count;
		}
	}

	/*
	 * Allocate a chunk out of the slab.  HOT PATH
	 *
	 * Only the thread owning the slab can allocate out of it.
	 *
	 * NOTE: The last bit in the bitmap is always marked allocated so
	 *	 we cannot overflow here.
	 */
	bno = slab->free_bit;
	bmi = slab->free_index;
	bmp = &slab->bitmap[bmi];
	if (*bmp & (1LU << bno)) {
		atomic_clear_long(bmp, 1LU << bno);
		obj = slab->chunks + ((bmi << LONG_BITS_SHIFT) + bno) * size;
		slab->free_bit = (bno + 1) & (LONG_BITS - 1);
		atomic_add_int(&slab->navail, -1);
		if (flags & SAFLAG_ZERO)
			bzero(obj, size);
		return (obj);
	}

	/*
	 * Allocate a chunk out of a slab.  COLD PATH
	 */
	if (slab->navail == 0) {
		zinfo->avail_base = slab->next;
		slab->state = FULL;
		TAILQ_INSERT_TAIL(&slgd->full_zones, slab, entry);
		goto retry;
	}

	while (bmi < LONG_BITS) {
		bmp = &slab->bitmap[bmi];
		if (*bmp) {
			bno = bsflong(*bmp);
			atomic_clear_long(bmp, 1LU << bno);
			obj = slab->chunks + ((bmi << LONG_BITS_SHIFT) + bno) *
					     size;
			slab->free_index = bmi;
			slab->free_bit = (bno + 1) & (LONG_BITS - 1);
			atomic_add_int(&slab->navail, -1);
			if (flags & SAFLAG_ZERO)
				bzero(obj, size);
			return (obj);
		}
		++bmi;
	}
	bmi = 0;
	while (bmi < LONG_BITS) {
		bmp = &slab->bitmap[bmi];
		if (*bmp) {
			bno = bsflong(*bmp);
			atomic_clear_long(bmp, 1LU << bno);
			obj = slab->chunks + ((bmi << LONG_BITS_SHIFT) + bno) *
					     size;
			slab->free_index = bmi;
			slab->free_bit = (bno + 1) & (LONG_BITS - 1);
			atomic_add_int(&slab->navail, -1);
			if (flags & SAFLAG_ZERO)
				bzero(obj, size);
			return (obj);
		}
		++bmi;
	}
	_mpanic("slaballoc: corrupted zone: navail %d", slab->navail);
	/* not reached */
	return NULL;
}

/*
 * Reallocate memory within the chunk
 */
static void *
memrealloc(void *ptr, size_t nsize)
{
	region_t region;
	slab_t slab;
	size_t osize;
	char *obj;
	int flags = 0;

	/*
	 * If 0 bytes is requested we have to return a unique pointer, allocate
	 * at least one byte.
	 */
	if (nsize == 0)
		nsize = 1;

	/* Capture global flags */
	flags |= g_malloc_flags;

	/*
	 * Locate the zone by looking up the dynamic slab size mask based
	 * on the memory region the allocation resides in.
	 */
	region = &Regions[((uintptr_t)ptr >> NREGIONS_SHIFT) & NREGIONS_MASK];
	if ((slab = region->slab) == NULL)
		slab = (void *)((uintptr_t)ptr & region->mask);
	MASSERT(slab->magic == ZALLOC_SLAB_MAGIC);
	osize = slab->chunk_size;
	if (nsize <= osize) {
		if (osize < 32 || nsize >= osize / 2) {
			obj = ptr;
			if ((flags & SAFLAG_ZERO) && nsize < osize)
				bzero(obj + nsize, osize - nsize);
			return(obj);
		}
	}

	/*
	 * Otherwise resize the object
	 */
	obj = memalloc(nsize, 0);
	if (obj) {
		if (nsize > osize)
			nsize = osize;
		bcopy(ptr, obj, nsize);
		memfree(ptr, 0);
	}
	return (obj);
}

/*
 * free (SLAB ALLOCATOR)
 *
 * Free a memory block previously allocated by malloc.
 *
 * MPSAFE
 */
static void
memfree(void *ptr, int flags)
{
	region_t region;
	slglobaldata_t slgd;
	slab_t slab;
	slab_t stmp;
	slab_t *slabp;
	char *obj;
	int bmi;
	int bno;
	u_long *bmp;

	/*
	 * Locate the zone by looking up the dynamic slab size mask based
	 * on the memory region the allocation resides in.
	 *
	 * WARNING!  The slab may be owned by another thread!
	 */
	region = &Regions[((uintptr_t)ptr >> NREGIONS_SHIFT) & NREGIONS_MASK];
	if ((slab = region->slab) == NULL)
		slab = (void *)((uintptr_t)ptr & region->mask);
	MASSERT(slab != NULL);
	MASSERT(slab->magic == ZALLOC_SLAB_MAGIC);

#ifdef INVARIANTS
	/*
	 * Put weird data into the memory to detect modifications after
	 * freeing, illegal pointer use after freeing (we should fault on
	 * the odd address), and so forth.
	 */
	if (slab->chunk_size < sizeof(weirdary))
		bcopy(weirdary, ptr, slab->chunk_size);
	else
		bcopy(weirdary, ptr, sizeof(weirdary));
#endif

	bno = ((uintptr_t)ptr - (uintptr_t)slab->chunks) / slab->chunk_size;
	bmi = bno >> LONG_BITS_SHIFT;
	bno &= (LONG_BITS - 1);
	bmp = &slab->bitmap[bmi];

	MASSERT(bmi >= 0 && bmi < slab->nmax);
	MASSERT((*bmp & (1LU << bno)) == 0);
	atomic_set_long(bmp, 1LU << bno);
	atomic_add_int(&slab->navail, 1);

	/*
	 * We can only do the following if we own the slab
	 */
	slgd = &slglobal;
	if (slab->slgd == slgd) {
		struct zoneinfo *zinfo;

		if (slab->free_index > bmi) {
			slab->free_index = bmi;
			slab->free_bit = bno;
		} else if (slab->free_index == bmi &&
			   slab->free_bit > bno) {
			slab->free_bit = bno;
		}
		zinfo = &slgd->zone[slab->zone_index];

		/*
		 * Freeing an object from a full slab will move it to the
		 * available list.  If the available list already has a
		 * slab we terminate the full slab instead, moving it to
		 * the depot.
		 */
		if (slab->state == FULL) {
			TAILQ_REMOVE(&slgd->full_zones, slab, entry);
			if (zinfo->avail_base == NULL) {
				slab->state = AVAIL;
				stmp = zinfo->avail_base;
				slab->next = stmp;
				zinfo->avail_base = slab;
			} else {
				slabterm(slgd, slab);
				goto done;
			}
		}

		/*
		 * If the slab becomes completely empty dispose of it in
		 * some manner.  By default each thread caches up to 4
		 * empty slabs.  Only small slabs are cached.
		 */
		if (slab->navail == slab->nmax && slab->state == AVAIL) {
			/*
			 * Remove slab from available queue
			 */
			slabp = &zinfo->avail_base;
			while ((stmp = *slabp) != slab)
				slabp = &stmp->next;
			*slabp = slab->next;

			if (opt_free || opt_cache == 0) {
				/*
				 * If local caching is disabled cache the
				 * slab in the depot (or free it).
				 */
				slabterm(slgd, slab);
			} else if (slab->slab_size > BIGSLABSIZE) {
				/*
				 * We do not try to retain large slabs
				 * in per-thread caches.
				 */
				slabterm(slgd, slab);
			} else if (zinfo->empty_count > opt_cache) {
				/*
				 * We have too many slabs cached, but
				 * instead of freeing this one free
				 * an empty slab that's been idle longer.
				 *
				 * (empty_count does not change)
				 */
				stmp = zinfo->empty_base;
				slab->state = EMPTY;
				slab->next = stmp->next;
				zinfo->empty_base = slab;
				slabterm(slgd, stmp);
			} else {
				/*
				 * Cache the empty slab in our thread local
				 * empty list.
				 */
				++zinfo->empty_count;
				slab->state = EMPTY;
				slab->next = zinfo->empty_base;
				zinfo->empty_base = slab;
			}
		}
	}
done:
	;
}

/*
 * Allocate a new slab holding objects of size chunk_size.
 */
static slab_t
slaballoc(int zi, size_t chunking, size_t chunk_size)
{
	slglobaldata_t slgd;
	slglobaldata_t sldepot;
	struct zoneinfo *zinfo;
	region_t region;
	void *save;
	slab_t slab;
	slab_t stmp;
	size_t slab_desire;
	size_t slab_size;
	size_t region_mask;
	uintptr_t chunk_offset;
	ssize_t maxchunks;
	ssize_t tmpchunks;
	int ispower2;
	int power;
	int ri;
	int rx;
	int nswath;
	int j;

	/*
	 * First look in the depot.  Any given zone in the depot may be
	 * locked by being set to -1.  We have to do this instead of simply
	 * removing the entire chain because removing the entire chain can
	 * cause racing threads to allocate local slabs for large objects,
	 * resulting in a large VSZ.
	 */
	slgd = &slglobal;
	sldepot = slgd->sldepot;
	zinfo = &sldepot->zone[zi];

	while ((slab = zinfo->avail_base) != NULL) {
		if ((void *)slab == LOCKEDPTR) {
			cpu_pause();
			continue;
		}
		if (atomic_cmpset_ptr(&zinfo->avail_base, slab, LOCKEDPTR)) {
			MASSERT(slab->slgd == NULL);
			slab->slgd = slgd;
			zinfo->avail_base = slab->next;
			return(slab);
		}
	}

	/*
	 * Nothing in the depot, allocate a new slab by locating or assigning
	 * a region and then using the system virtual memory allocator.
	 */
	slab = NULL;

	/*
	 * Calculate the start of the data chunks relative to the start
	 * of the slab.
	 */
	if ((chunk_size ^ (chunk_size - 1)) == (chunk_size << 1) - 1) {
		ispower2 = 1;
		chunk_offset = (sizeof(*slab) + (chunk_size - 1)) &
			       ~(chunk_size - 1);
	} else {
		ispower2 = 0;
		chunk_offset = sizeof(*slab) + chunking - 1;
		chunk_offset -= chunk_offset % chunking;
	}

	/*
	 * Calculate a reasonable number of chunks for the slab.
	 *
	 * Once initialized the MaxChunks[] array can only ever be
	 * reinitialized to the same value.
	 */
	maxchunks = MaxChunks[zi];
	if (maxchunks == 0) {
		/*
		 * First calculate how many chunks would fit in 1/1024
		 * available memory.  This is around 2MB on a 32 bit
		 * system and 128G on a 64-bit (48-bits available) system.
		 */
		maxchunks = (ssize_t)(NREGIONS_SIZE - chunk_offset) /
			    (ssize_t)chunk_size;
		if (maxchunks <= 0)
			maxchunks = 1;

		/*
		 * A slab cannot handle more than MAXCHUNKS chunks, but
		 * limit us to approximately MAXCHUNKS / 2 here because
		 * we may have to expand maxchunks when we calculate the
		 * actual power-of-2 slab.
		 */
		if (maxchunks > MAXCHUNKS / 2)
			maxchunks = MAXCHUNKS / 2;

		/*
		 * Try to limit the slabs to BIGSLABSIZE (~128MB).  Larger
		 * slabs will be created if the allocation does not fit.
		 */
		if (chunk_offset + chunk_size * maxchunks > BIGSLABSIZE) {
			tmpchunks = (ssize_t)(BIGSLABSIZE - chunk_offset) /
				    (ssize_t)chunk_size;
			if (tmpchunks <= 0)
				tmpchunks = 1;
			if (maxchunks > tmpchunks)
				maxchunks = tmpchunks;
		}

		/*
		 * If the slab calculates to greater than 2MB see if we
		 * can cut it down to ~2MB.  This controls VSZ but has
		 * no effect on run-time size or performance.
		 *
		 * This is very important in case you core dump and also
		 * important to reduce unnecessary region allocations.
		 */
		if (chunk_offset + chunk_size * maxchunks > NOMSLABSIZE) {
			tmpchunks = (ssize_t)(NOMSLABSIZE - chunk_offset) /
				    (ssize_t)chunk_size;
			if (tmpchunks < 1)
				tmpchunks = 1;
			if (maxchunks > tmpchunks)
				maxchunks = tmpchunks;
		}

		/*
		 * If the slab calculates to greater than 128K see if we
		 * can cut it down to ~128K while still maintaining a
		 * reasonably large number of chunks in each slab.  This
		 * controls VSZ but has no effect on run-time size or
		 * performance.
		 *
		 * This is very important in case you core dump and also
		 * important to reduce unnecessary region allocations.
		 */
		if (chunk_offset + chunk_size * maxchunks > LITSLABSIZE) {
			tmpchunks = (ssize_t)(LITSLABSIZE - chunk_offset) /
				    (ssize_t)chunk_size;
			if (tmpchunks < 32)
				tmpchunks = 32;
			if (maxchunks > tmpchunks)
				maxchunks = tmpchunks;
		}

		MaxChunks[zi] = maxchunks;
	}
	MASSERT(maxchunks > 0 && maxchunks <= MAXCHUNKS);

	/*
	 * Calculate the actual slab size.  maxchunks will be recalculated
	 * a little later.
	 */
	slab_desire = chunk_offset + chunk_size * maxchunks;
	slab_size = 8 * MAXCHUNKS;
	power = 3 + MAXCHUNKS_BITS;
	while (slab_size < slab_desire) {
		slab_size <<= 1;
		++power;
	}

	/*
	 * Do a quick recalculation based on the actual slab size but not
	 * yet dealing with whether the slab header is in-band or out-of-band.
	 * The purpose here is to see if we can reasonably reduce slab_size
	 * to a power of 4 to allow more chunk sizes to use the same slab
	 * size.
	 */
	if ((power & 1) && slab_size > 32768) {
		maxchunks = (slab_size - chunk_offset) / chunk_size;
		if (maxchunks >= MAXCHUNKS / 8) {
			slab_size >>= 1;
			--power;
		}
	}
	if ((power & 2) && slab_size > 32768 * 4) {
		maxchunks = (slab_size - chunk_offset) / chunk_size;
		if (maxchunks >= MAXCHUNKS / 4) {
			slab_size >>= 2;
			power -= 2;
		}
	}
	/*
	 * This case occurs when the slab_size is larger than 1/1024 available
	 * UVM.
	 */
	nswath = slab_size / NREGIONS_SIZE;
	if (nswath > NREGIONS)
		return (NULL);


	/*
	 * Try to allocate from our current best region for this zi
	 */
	region_mask = ~(slab_size - 1);
	ri = slgd->zone[zi].best_region;
	if (Regions[ri].mask == region_mask) {
		if ((slab = _vmem_alloc(ri, slab_size)) != NULL)
			goto found;
	}

	/*
	 * Try to find an existing region to allocate from.  The normal
	 * case will be for allocations that are less than 1/1024 available
	 * UVM, which fit into a single Regions[] entry.
	 */
	while (slab_size <= NREGIONS_SIZE) {
		rx = -1;
		for (ri = 0; ri < NREGIONS; ++ri) {
			if (rx < 0 && Regions[ri].mask == 0)
				rx = ri;
			if (Regions[ri].mask == region_mask) {
				slab = _vmem_alloc(ri, slab_size);
				if (slab) {
					slgd->zone[zi].best_region = ri;
					goto found;
				}
			}
		}

		if (rx < 0)
			return(NULL);

		/*
		 * This can fail, retry either way
		 */
		atomic_cmpset_ptr((void **)&Regions[rx].mask,
				  NULL,
				  (void *)region_mask);
	}

	for (;;) {
		rx = -1;
		for (ri = 0; ri < NREGIONS; ri += nswath) {
			if (Regions[ri].mask == region_mask) {
				slab = _vmem_alloc(ri, slab_size);
				if (slab) {
					slgd->zone[zi].best_region = ri;
					goto found;
				}
			}
			if (rx < 0) {
				for (j = nswath - 1; j >= 0; --j) {
					if (Regions[ri+j].mask != 0)
						break;
				}
				if (j < 0)
					rx = ri;
			}
		}

		/*
		 * We found a candidate, try to allocate it backwards so
		 * another thread racing a slaballoc() does not see the
		 * mask in the base index position until we are done.
		 *
		 * We can safely zero-out any partial allocations because
		 * the mask is only accessed from the base index.  Any other
		 * threads racing us will fail prior to us clearing the mask.
		 */
		if (rx < 0)
			return(NULL);
		for (j = nswath - 1; j >= 0; --j) {
			if (!atomic_cmpset_ptr((void **)&Regions[rx+j].mask,
					       NULL, (void *)region_mask)) {
				while (++j < nswath)
					Regions[rx+j].mask = 0;
				break;
			}
		}
		/* retry */
	}

	/*
	 * Fill in the new slab in region ri.  If the slab_size completely
	 * fills one or more region slots we move the slab structure out of
	 * band which should optimize the chunking (particularly for a power
	 * of 2).
	 */
found:
	region = &Regions[ri];
	MASSERT(region->slab == NULL);
	if (slab_size >= NREGIONS_SIZE) {
		save = slab;
		slab = memalloc(sizeof(*slab), 0);
		bzero(slab, sizeof(*slab));
		slab->chunks = save;
		for (j = 0; j < nswath; ++j)
			region[j].slab = slab;
		chunk_offset = 0;
	} else {
		bzero(slab, sizeof(*slab));
		slab->chunks = (char *)slab + chunk_offset;
	}

	/*
	 * Calculate the start of the chunks memory and recalculate the
	 * actual number of chunks the slab can hold.
	 */
	maxchunks = (slab_size - chunk_offset) / chunk_size;
	if (maxchunks > MAXCHUNKS)
		maxchunks = MAXCHUNKS;

	/*
	 * And fill in the rest
	 */
	slab->magic = ZALLOC_SLAB_MAGIC;
	slab->navail = maxchunks;
	slab->nmax = maxchunks;
	slab->slab_size = slab_size;
	slab->chunk_size = chunk_size;
	slab->zone_index = zi;
	slab->slgd = slgd;
	slab->state = UNKNOWN;
	slab->region = region;

	for (ri = 0; ri < maxchunks; ri += LONG_BITS) {
		if (ri + LONG_BITS <= maxchunks)
			slab->bitmap[ri >> LONG_BITS_SHIFT] = ULONG_MAX;
		else
			slab->bitmap[ri >> LONG_BITS_SHIFT] =
						(1LU << (maxchunks - ri)) - 1;
	}
	return (slab);
}

/*
 * Free a slab.
 */
static void
slabfree(slab_t slab)
{
	int nswath;
	int j;

	if (slab->region->slab == slab) {
		/*
		 * Out-of-band slab.
		 */
		nswath = slab->slab_size / NREGIONS_SIZE;
		for (j = 0; j < nswath; ++j)
			slab->region[j].slab = NULL;
		slab->magic = 0;
		_vmem_free(slab->chunks, slab->slab_size);
		memfree(slab, 0);
	} else {
		/*
		 * In-band slab.
		 */
		slab->magic = 0;
		_vmem_free(slab, slab->slab_size);
	}
}

/*
 * Terminate a slab's use in the current thread.  The slab may still have
 * outstanding allocations and thus not be deallocatable.
 */
static void
slabterm(slglobaldata_t slgd, slab_t slab)
{
	slglobaldata_t sldepot = slgd->sldepot;
	struct zoneinfo *zinfo;
	slab_t dnext;
	int zi = slab->zone_index;

	slab->slgd = NULL;
	--slgd->nslabs;
	zinfo = &sldepot->zone[zi];

	/*
	 * If the slab can be freed and the depot is either locked or not
	 * empty, then free the slab.
	 */
	if (slab->navail == slab->nmax && zinfo->avail_base) {
		slab->state = UNKNOWN;
		slabfree(slab);
		return;
	}
	slab->state = AVAIL;

	/*
	 * Link the slab into the depot
	 */
	for (;;) {
		dnext = zinfo->avail_base;
		cpu_ccfence();
		if ((void *)dnext == LOCKEDPTR) {
			cpu_pause();
			continue;
		}
		slab->next = dnext;
		if (atomic_cmpset_ptr(&zinfo->avail_base, dnext, slab))
			break;
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
_vmem_alloc(int ri, size_t slab_size)
{
	char *baddr = (void *)((uintptr_t)ri << NREGIONS_SHIFT);
	char *eaddr;
	char *addr;
	char *save;
	uintptr_t excess;

	if (slab_size < NREGIONS_SIZE)
		eaddr = baddr + NREGIONS_SIZE;
	else
		eaddr = baddr + slab_size;

	/*
	 * This usually just works but might not.
	 */
	addr = mmap(baddr, slab_size, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON | MAP_SIZEALIGN, -1, 0);
	if (addr == MAP_FAILED) {
		return (NULL);
	}
	if (addr < baddr || addr + slab_size > eaddr) {
		munmap(addr, slab_size);
		return (NULL);
	}

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
	excess = (uintptr_t)addr & (slab_size - 1);
	while (excess) {
		excess = slab_size - excess;
		save = addr;

		munmap(save + excess, slab_size - excess);
		addr = _vmem_alloc(ri, slab_size);
		munmap(save, excess);
		if (addr == NULL)
			return (NULL);
		if (addr < baddr || addr + slab_size > eaddr) {
			munmap(addr, slab_size);
			return (NULL);
		}
		excess = (uintptr_t)addr & (slab_size - 1);
	}
	return (addr);
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
