/*
 * SLABALLOC.C	- Userland SLAB memory allocator
 *
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libcaps/slaballoc.c,v 1.2 2003/12/04 22:06:19 dillon Exp $
 *
 * This module implements a thread-safe slab allocator for userland.
 *
 * A slab allocator reserves a ZONE for each chunk size, then lays the
 * chunks out in an array within the zone.  Allocation and deallocation
 * is nearly instantanious, and fragmentation/overhead losses are limited
 * to a fixed worst-case amount.
 *
 * The downside of this slab implementation is in the chunk size
 * multiplied by the number of zones.  ~80 zones * 128K = 10MB of VM per cpu.
 * To mitigate this we attempt to select a reasonable zone size based on
 * available system memory.  e.g. 32K instead of 128K.  Also since the
 * slab allocator is operating out of virtual memory in userland the actual
 * physical memory use is not as bad as it might otherwise be.
 *
 * The upside is that overhead is bounded... waste goes down as use goes up.
 *
 * Slab management is done on a per-cpu basis and no locking or mutexes
 * are required, only a critical section.  When one cpu frees memory
 * belonging to another cpu's slab manager an asynchronous IPI message
 * will be queued to execute the operation.   In addition, both the
 * high level slab allocator and the low level zone allocator optimize
 * M_ZERO requests, and the slab allocator does not have to pre initialize
 * the linked list of chunks.
 *
 * XXX Balancing is needed between cpus.  Balance will be handled through
 * asynchronous IPIs primarily by reassigning the z_Cpu ownership of chunks.
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
 *	(if PAGE_SIZE is 4K the maximum zone allocation is 16383)
 *
 *	Allocations >= ZoneLimit go directly to kmem.
 *
 *			API REQUIREMENTS AND SIDE EFFECTS
 *
 *    To operate as a drop-in replacement to the FreeBSD-4.x malloc() we
 *    have remained compatible with the following API requirements:
 *
 *    + small power-of-2 sized allocations are power-of-2 aligned (kern_tty)
 *    + all power-of-2 sized allocations are power-of-2 aligned (twe)
 *    + malloc(0) is allowed and returns non-NULL (ahc driver)
 *    + ability to allocate arbitrarily large chunks of memory
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stdint.h>
#include <sys/malloc.h>
#include "thread.h"
#include <sys/thread.h>
#include <sys/msgport.h>
#include <sys/errno.h>
#include "globaldata.h"
#include <sys/sysctl.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>

#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))
#define slab_min(a,b)	(((a)<(b)) ? (a) : (b))

/*
 * Fixed globals (not per-cpu)
 */
static int ZoneSize;
static int ZoneLimit;
static int ZonePageCount;
static int ZonePageLimit;
static int ZoneMask;
static struct malloc_type *kmemstatistics;
static int32_t weirdary[16];

/*
 * Misc constants.  Note that allocations that are exact multiples of 
 * PAGE_SIZE, or exceed the zone limit, fall through to the kmem module.
 * IN_SAME_PAGE_MASK is used to sanity-check the per-page free lists.
 */
#define MIN_CHUNK_SIZE		8		/* in bytes */
#define MIN_CHUNK_MASK		(MIN_CHUNK_SIZE - 1)
#define ZONE_RELS_THRESH	2		/* threshold number of zones */
#define IN_SAME_PAGE_MASK	(~(intptr_t)PAGE_MASK | MIN_CHUNK_MASK)

#define	SLOVERSZ_HSIZE		8192
#define	SLOVERSZ_HMASK		(SLOVERSZ_HSIZE - 1)

#define SLOVERSZ_HASH(ptr)	((((uintptr_t)ptr >> PAGE_SHIFT) ^ \
				((uintptr_t)ptr >> (PAGE_SHIFT * 2))) & \
				SLOVERSZ_HMASK)

SLOversized *SLOvHash[SLOVERSZ_HSIZE];

/*
 * The WEIRD_ADDR is used as known text to copy into free objects to
 * try to create deterministic failure cases if the data is accessed after
 * free.
 */    
#define WEIRD_ADDR      0xdeadc0de
#define MAX_COPY        sizeof(weirdary)
#define ZERO_LENGTH_PTR	((void *)-8)

/*
 * Misc global malloc buckets
 */
MALLOC_DEFINE(M_OVERSIZED, "overszinfo", "Oversized Info Blocks");

static __inline
SLOversized **
get_oversized(void *ptr)
{
    SLOversized **slovpp;
    SLOversized *slov;

    for (slovpp = &SLOvHash[SLOVERSZ_HASH(ptr)];
	(slov = *slovpp) != NULL;
	slovpp = &slov->ov_Next
    ) {
	if (slov->ov_Ptr == ptr)
	    return(slovpp);
    }
    return(NULL);
}
 
/*
 * Initialize the slab memory allocator.  We have to choose a zone size based
 * on available physical memory.  We choose a zone side which is approximately
 * 1/1024th of our memory, so if we have 128MB of ram we have a zone size of
 * 128K.  The zone size is limited to the bounds set in slaballoc.h
 * (typically 32K min, 128K max). 
 */
void
slab_init(void)
{
    int i;
    int error;
    int pagecnt;
    int pagecnt_size = sizeof(pagecnt);

    error = sysctlbyname("vm.stats.vm.v_page_count",
			&pagecnt, &pagecnt_size, NULL, 0);
    if (error == 0) {
	vm_poff_t limsize;
	int usesize;

	limsize = pagecnt * (vm_poff_t)PAGE_SIZE;
	usesize = (int)(limsize / 1024);	/* convert to KB */

	ZoneSize = ZALLOC_MIN_ZONE_SIZE;
	while (ZoneSize < ZALLOC_MAX_ZONE_SIZE && (ZoneSize << 1) < usesize)
	    ZoneSize <<= 1;
    } else {
	ZoneSize = ZALLOC_MIN_ZONE_SIZE;
    }
    ZoneLimit = ZoneSize / 4;
    if (ZoneLimit > ZALLOC_ZONE_LIMIT)
	ZoneLimit = ZALLOC_ZONE_LIMIT;
    ZoneMask = ZoneSize - 1;
    ZonePageLimit = PAGE_SIZE * 4;
    ZonePageCount = ZoneSize / PAGE_SIZE;

    for (i = 0; i < arysize(weirdary); ++i)
	weirdary[i] = WEIRD_ADDR;
    slab_malloc_init(M_OVERSIZED);
}

/*
 * Initialize a malloc type tracking structure.
 */
void
slab_malloc_init(void *data)
{
    struct malloc_type *type = data;
    vm_poff_t limsize;

    /*
     * Skip if already initialized
     */
    if (type->ks_limit != 0)
	return;

    type->ks_magic = M_MAGIC;
    limsize = (vm_poff_t)-1;	/* unlimited */
    type->ks_limit = limsize / 10;
    type->ks_next = kmemstatistics;
    kmemstatistics = type;
}

void
slab_malloc_uninit(void *data)
{
    struct malloc_type *type = data;
    struct malloc_type *t;
#ifdef INVARIANTS
    int i;
    long ttl;
#endif

    if (type->ks_magic != M_MAGIC)
	panic("malloc type lacks magic");

    if (type->ks_limit == 0)
	panic("malloc_uninit on uninitialized type");

#ifdef INVARIANTS
    /*
     * memuse is only correct in aggregation.  Due to memory being allocated
     * on one cpu and freed on another individual array entries may be 
     * negative or positive (canceling each other out).
     */
    for (i = ttl = 0; i < ncpus; ++i)
	ttl += type->ks_memuse[i];
    if (ttl) {
	printf("malloc_uninit: %ld bytes of '%s' still allocated on cpu %d\n",
	    ttl, type->ks_shortdesc, i);
    }
#endif
    if (type == kmemstatistics) {
	kmemstatistics = type->ks_next;
    } else {
	for (t = kmemstatistics; t->ks_next != NULL; t = t->ks_next) {
	    if (t->ks_next == type) {
		t->ks_next = type->ks_next;
		break;
	    }
	}
    }
    type->ks_next = NULL;
    type->ks_limit = 0;
}

/*
 * Calculate the zone index for the allocation request size and set the
 * allocation request size to that particular zone's chunk size.
 */
static __inline int
zoneindex(unsigned long *bytes)
{
    unsigned int n = (unsigned int)*bytes;	/* unsigned for shift opt */
    if (n < 128) {
	*bytes = n = (n + 7) & ~7;
	return(n / 8 - 1);		/* 8 byte chunks, 16 zones */
    }
    if (n < 256) {
	*bytes = n = (n + 15) & ~15;
	return(n / 16 + 7);
    }
    if (n < 8192) {
	if (n < 512) {
	    *bytes = n = (n + 31) & ~31;
	    return(n / 32 + 15);
	}
	if (n < 1024) {
	    *bytes = n = (n + 63) & ~63;
	    return(n / 64 + 23);
	} 
	if (n < 2048) {
	    *bytes = n = (n + 127) & ~127;
	    return(n / 128 + 31);
	}
	if (n < 4096) {
	    *bytes = n = (n + 255) & ~255;
	    return(n / 256 + 39);
	}
	*bytes = n = (n + 511) & ~511;
	return(n / 512 + 47);
    }
#if ZALLOC_ZONE_LIMIT > 8192
    if (n < 16384) {
	*bytes = n = (n + 1023) & ~1023;
	return(n / 1024 + 55);
    }
#endif
#if ZALLOC_ZONE_LIMIT > 16384
    if (n < 32768) {
	*bytes = n = (n + 2047) & ~2047;
	return(n / 2048 + 63);
    }
#endif
    panic("Unexpected byte count %d", n);
    return(0);
}

/*
 * slab_malloc()
 *
 *	Allocate memory via the slab allocator.  If the request is too large,
 *	or if it page-aligned beyond a certain size, we fall back to the
 *	KMEM subsystem.  A SLAB tracking descriptor must be specified, use
 *	&SlabMisc if you don't care.
 *
 *	M_NOWAIT	- return NULL instead of blocking.
 *	M_ZERO		- zero the returned memory.
 */
void *
slab_malloc(unsigned long size, struct malloc_type *type, int flags)
{
    SLZone *z;
    SLChunk *chunk;
    SLGlobalData *slgd;
    struct globaldata *gd;
    int zi;

    gd = mycpu;
    slgd = &gd->gd_slab;

    /*
     * XXX silly to have this in the critical path.
     */
    if (type->ks_limit == 0) {
	crit_enter();
	if (type->ks_limit == 0)
	    slab_malloc_init(type);
	crit_exit();
    }
    ++type->ks_calls;

    /*
     * Handle the case where the limit is reached.  Panic if can't return
     * NULL.  XXX the original malloc code looped, but this tended to
     * simply deadlock the computer.
     */
    while (type->ks_loosememuse >= type->ks_limit) {
	int i;
	long ttl;

	for (i = ttl = 0; i < ncpus; ++i)
	    ttl += type->ks_memuse[i];
	type->ks_loosememuse = ttl;
	if (ttl >= type->ks_limit) {
	    if (flags & (M_NOWAIT|M_NULLOK))
		return(NULL);
	    panic("%s: malloc limit exceeded", type->ks_shortdesc);
	}
    }

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
     * Handle hysteresis from prior frees here in malloc().  We cannot
     * safely manipulate the kernel_map in free() due to free() possibly
     * being called via an IPI message or from sensitive interrupt code.
     */
    while (slgd->NFreeZones > ZONE_RELS_THRESH && (flags & M_NOWAIT) == 0) {
	crit_enter();
	if (slgd->NFreeZones > ZONE_RELS_THRESH) {	/* crit sect race */
	    z = slgd->FreeZones;
	    slgd->FreeZones = z->z_Next;
	    --slgd->NFreeZones;
	    munmap(z, ZoneSize);
	}
	crit_exit();
    }
    /*
     * XXX handle oversized frees that were queued from free().
     */
    while (slgd->FreeOvZones && (flags & M_NOWAIT) == 0) {
	crit_enter();
	if ((z = slgd->FreeOvZones) != NULL) {
	    KKASSERT(z->z_Magic == ZALLOC_OVSZ_MAGIC);
	    slgd->FreeOvZones = z->z_Next;
	    munmap(z, z->z_ChunkSize);
	}
	crit_exit();
    }

    /*
     * Handle large allocations directly.  There should not be very many of
     * these so performance is not a big issue.
     *
     * Guarentee page alignment for allocations in multiples of PAGE_SIZE
     */
    if (size >= ZoneLimit || (size & PAGE_MASK) == 0) {
	SLOversized **slovpp;
	SLOversized *slov;

	slov = slab_malloc(sizeof(SLOversized), M_OVERSIZED, M_ZERO);
	if (slov == NULL)
	    return(NULL);

	size = round_page(size);
	chunk = mmap(NULL, size, PROT_READ|PROT_WRITE,
			MAP_ANON|MAP_PRIVATE, -1, 0);
	if (chunk == MAP_FAILED) {
	    slab_free(slov, M_OVERSIZED);
	    return(NULL);
	}
	flags &= ~M_ZERO;	/* result already zero'd if M_ZERO was set */
	flags |= M_PASSIVE_ZERO;

	slov->ov_Ptr = chunk;
	slov->ov_Bytes = size;
	slovpp = &SLOvHash[SLOVERSZ_HASH(chunk)];
	slov->ov_Next = *slovpp;
	*slovpp = slov;
	crit_enter();
	goto done;
    }

    /*
     * Attempt to allocate out of an existing zone.  First try the free list,
     * then allocate out of unallocated space.  If we find a good zone move
     * it to the head of the list so later allocations find it quickly
     * (we might have thousands of zones in the list).
     *
     * Note: zoneindex() will panic of size is too large.
     */
    zi = zoneindex(&size);
    KKASSERT(zi < NZONES);
    crit_enter();
    if ((z = slgd->ZoneAry[zi]) != NULL) {
	KKASSERT(z->z_NFree > 0);

	/*
	 * Remove us from the ZoneAry[] when we become empty
	 */
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
		KKASSERT(chunk->c_Next == NULL ||
			((intptr_t)chunk->c_Next & IN_SAME_PAGE_MASK) ==
			((intptr_t)chunk & IN_SAME_PAGE_MASK));
#endif
#ifdef INVARIANTS
		if ((uintptr_t)chunk < VM_MIN_KERNEL_ADDRESS)
			panic("chunk %p FFPG %d/%d", chunk, z->z_FirstFreePg, ZonePageCount);
		if (chunk->c_Next && (uintptr_t)chunk->c_Next < VM_MIN_KERNEL_ADDRESS)
			panic("chunkNEXT %p %p FFPG %d/%d", chunk, chunk->c_Next, z->z_FirstFreePg, ZonePageCount);
#endif
		z->z_PageAry[z->z_FirstFreePg] = chunk->c_Next;
		goto done;
	    }
	    ++z->z_FirstFreePg;
	}

	/*
	 * No chunks are available but NFree said we had some memory, so
	 * it must be available in the never-before-used-memory area
	 * governed by UIndex.  The consequences are very serious if our zone
	 * got corrupted so we use an explicit panic rather then a KASSERT.
	 */
	if (z->z_UIndex + 1 != z->z_NMax)
	    z->z_UIndex = z->z_UIndex + 1;
	else
	    z->z_UIndex = 0;
	if (z->z_UIndex == z->z_UEndIndex)
	    panic("slaballoc: corrupted zone");
	chunk = (SLChunk *)(z->z_BasePtr + z->z_UIndex * size);
	if ((z->z_Flags & SLZF_UNOTZEROD) == 0) {
	    flags &= ~M_ZERO;
	    flags |= M_PASSIVE_ZERO;
	}
	goto done;
    }

    /*
     * If all zones are exhausted we need to allocate a new zone for this
     * index.  Use M_ZERO to take advantage of pre-zerod pages.  Also see
     * UAlloc use above in regards to M_ZERO.  Note that when we are reusing
     * a zone from the FreeZones list UAlloc'd data will not be zero'd, and
     * we do not pre-zero it because we do not want to mess up the L1 cache.
     *
     * At least one subsystem, the tty code (see CROUND) expects power-of-2
     * allocations to be power-of-2 aligned.  We maintain compatibility by
     * adjusting the base offset below.
     */
    {
	int off;

	if ((z = slgd->FreeZones) != NULL) {
	    slgd->FreeZones = z->z_Next;
	    --slgd->NFreeZones;
	    bzero(z, sizeof(SLZone));
	    z->z_Flags |= SLZF_UNOTZEROD;
	} else {
	    z = mmap(NULL, ZoneSize, PROT_READ|PROT_WRITE,
		    MAP_ANON|MAP_PRIVATE, -1, 0);
	    if (z == MAP_FAILED)
		goto fail;
	}

	/*
	 * Guarentee power-of-2 alignment for power-of-2-sized chunks.
	 * Otherwise just 8-byte align the data.
	 */
	if ((size | (size - 1)) + 1 == (size << 1))
	    off = (sizeof(SLZone) + size - 1) & ~(size - 1);
	else
	    off = (sizeof(SLZone) + MIN_CHUNK_MASK) & ~MIN_CHUNK_MASK;
	z->z_Magic = ZALLOC_SLAB_MAGIC;
	z->z_ZoneIndex = zi;
	z->z_NMax = (ZoneSize - off) / size;
	z->z_NFree = z->z_NMax - 1;
	z->z_BasePtr = (char *)z + off;
	z->z_UIndex = z->z_UEndIndex = slgd->JunkIndex % z->z_NMax;
	z->z_ChunkSize = size;
	z->z_FirstFreePg = ZonePageCount;
	z->z_Cpu = gd->gd_cpuid;
	chunk = (SLChunk *)(z->z_BasePtr + z->z_UIndex * size);
	z->z_Next = slgd->ZoneAry[zi];
	slgd->ZoneAry[zi] = z;
	if ((z->z_Flags & SLZF_UNOTZEROD) == 0) {
	    flags &= ~M_ZERO;	/* already zero'd */
	    flags |= M_PASSIVE_ZERO;
	}

	/*
	 * Slide the base index for initial allocations out of the next
	 * zone we create so we do not over-weight the lower part of the
	 * cpu memory caches.
	 */
	slgd->JunkIndex = (slgd->JunkIndex + ZALLOC_SLAB_SLIDE)
				& (ZALLOC_MAX_ZONE_SIZE - 1);
    }
done:
    ++type->ks_inuse[gd->gd_cpuid];
    type->ks_memuse[gd->gd_cpuid] += size;
    type->ks_loosememuse += size;
    crit_exit();
    if (flags & M_ZERO)
	bzero(chunk, size);
#ifdef INVARIANTS
    else if ((flags & (M_ZERO|M_PASSIVE_ZERO)) == 0)
	chunk->c_Next = (void *)-1; /* avoid accidental double-free check */
#endif
    return(chunk);
fail:
    crit_exit();
    return(NULL);
}

void *
slab_realloc(void *ptr, unsigned long size, struct malloc_type *type, int flags)
{
    SLZone *z;
    SLOversized **slovpp;
    SLOversized *slov;
    void *nptr;
    unsigned long osize;

    if (ptr == NULL || ptr == ZERO_LENGTH_PTR)
	return(slab_malloc(size, type, flags));
    if (size == 0) {
	slab_free(ptr, type);
	return(NULL);
    }

    /*
     * Handle oversized allocations. 
     */
    if ((slovpp = get_oversized(ptr)) != NULL) {
	slov = *slovpp;
	osize = slov->ov_Bytes;
	if (osize == round_page(size))
	    return(ptr);
	if ((nptr = slab_malloc(size, type, flags)) == NULL)
	    return(NULL);
	bcopy(ptr, nptr, slab_min(size, osize));
	slab_free(ptr, type);
	return(nptr);
    }

    /*
     * Get the original allocation's zone.  If the new request winds up
     * using the same chunk size we do not have to do anything.
     */
    z = (SLZone *)((uintptr_t)ptr & ~(uintptr_t)ZoneMask);
    KKASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);

    zoneindex(&size);
    if (z->z_ChunkSize == size)
	return(ptr);

    /*
     * Allocate memory for the new request size.  Note that zoneindex has
     * already adjusted the request size to the appropriate chunk size, which
     * should optimize our bcopy().  Then copy and return the new pointer.
     */
    if ((nptr = slab_malloc(size, type, flags)) == NULL)
	return(NULL);
    bcopy(ptr, nptr, slab_min(size, z->z_ChunkSize));
    slab_free(ptr, type);
    return(nptr);
}

#ifdef SMP
/*
 * slab_free()	(SLAB ALLOCATOR)
 *
 *	Free the specified chunk of memory.
 */
static
void
slab_free_remote(void *ptr)
{
    slab_free(ptr, *(struct malloc_type **)ptr);
}

#endif

void
slab_free(void *ptr, struct malloc_type *type)
{
    SLZone *z;
    SLOversized **slovpp;
    SLOversized *slov;
    SLChunk *chunk;
    SLGlobalData *slgd;
    struct globaldata *gd;
    int pgno;

    gd = mycpu;
    slgd = &gd->gd_slab;

    /*
     * Handle special 0-byte allocations
     */
    if (ptr == ZERO_LENGTH_PTR)
	return;

    /*
     * Handle oversized allocations.  XXX we really should require that a
     * size be passed to slab_free() instead of this nonsense.
     *
     * This code is never called via an ipi.
     */
    if ((slovpp = get_oversized(ptr)) != NULL) {
	slov = *slovpp;
	*slovpp = slov->ov_Next;

#ifdef INVARIANTS
	KKASSERT(sizeof(weirdary) <= slov->ov_Bytes);
	bcopy(weirdary, ptr, sizeof(weirdary));
#endif
	/*
	 * note: we always adjust our cpu's slot, not the originating
	 * cpu (kup->ku_cpuid).  The statistics are in aggregate.
	 *
	 * note: XXX we have still inherited the interrupts-can't-block
	 * assumption.  An interrupt thread does not bump
	 * gd_intr_nesting_level so check TDF_INTTHREAD.  This is
	 * primarily until we can fix softupdate's assumptions about 
	 * slab_free().
	 */
	crit_enter();
	--type->ks_inuse[gd->gd_cpuid];
	type->ks_memuse[gd->gd_cpuid] -= slov->ov_Bytes;
	if (mycpu->gd_intr_nesting_level || (gd->gd_curthread->td_flags & TDF_INTTHREAD)) {
	    z = (SLZone *)ptr;
	    z->z_Magic = ZALLOC_OVSZ_MAGIC;
	    z->z_Next = slgd->FreeOvZones;
	    z->z_ChunkSize = slov->ov_Bytes;
	    slgd->FreeOvZones = z;
	    crit_exit();
	} else {
	    crit_exit();
	    munmap(ptr, slov->ov_Bytes);
	}
	slab_free(slov, M_OVERSIZED);
	return;
    }

    /*
     * Zone case.  Figure out the zone based on the fact that it is
     * ZoneSize aligned. 
     */
    z = (SLZone *)((uintptr_t)ptr & ~(uintptr_t)ZoneMask);
    KKASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);

    /*
     * If we do not own the zone then forward the request to the
     * cpu that does.  The freeing code does not need the byte count
     * unless DIAGNOSTIC is set.
     */
    if (z->z_Cpu != gd->gd_cpuid) {
	*(struct malloc_type **)ptr = type;
#ifdef SMP
	lwkt_send_ipiq(z->z_Cpu, slab_free_remote, ptr);
#else
	panic("Corrupt SLZone");
#endif
	return;
    }

    if (type->ks_magic != M_MAGIC)
	panic("slab_free: malloc type lacks magic");

    crit_enter();
    pgno = ((char *)ptr - (char *)z) >> PAGE_SHIFT;
    chunk = ptr;

#ifdef INVARIANTS
    /*
     * Attempt to detect a double-free.  To reduce overhead we only check
     * if there appears to be link pointer at the base of the data.
     */
    if (((intptr_t)chunk->c_Next - (intptr_t)z) >> PAGE_SHIFT == pgno) {
	SLChunk *scan;
	for (scan = z->z_PageAry[pgno]; scan; scan = scan->c_Next) {
	    if (scan == chunk)
		panic("Double free at %p", chunk);
	}
    }
#endif

    /*
     * Put weird data into the memory to detect modifications after freeing,
     * illegal pointer use after freeing (we should fault on the odd address),
     * and so forth.  XXX needs more work, see the old malloc code.
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
#ifdef INVARIANTS
    if ((uintptr_t)chunk < VM_MIN_KERNEL_ADDRESS)
	panic("BADFREE %p\n", chunk);
#endif
    chunk->c_Next = z->z_PageAry[pgno];
    z->z_PageAry[pgno] = chunk;
#ifdef INVARIANTS
    if (chunk->c_Next && (uintptr_t)chunk->c_Next < VM_MIN_KERNEL_ADDRESS)
	panic("BADFREE2");
#endif
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

    --type->ks_inuse[z->z_Cpu];
    type->ks_memuse[z->z_Cpu] -= z->z_ChunkSize;

    /*
     * If the zone becomes totally free, and there are other zones we
     * can allocate from, move this zone to the FreeZones list.  Since
     * this code can be called from an IPI callback, do *NOT* try to mess
     * with kernel_map here.  Hysteresis will be performed at malloc() time.
     */
    if (z->z_NFree == z->z_NMax && 
	(z->z_Next || slgd->ZoneAry[z->z_ZoneIndex] != z)
    ) {
	SLZone **pz;

	for (pz = &slgd->ZoneAry[z->z_ZoneIndex]; z != *pz; pz = &(*pz)->z_Next)
	    ;
	*pz = z->z_Next;
	z->z_Magic = -1;
	z->z_Next = slgd->FreeZones;
	slgd->FreeZones = z;
	++slgd->NFreeZones;
    }
    crit_exit();
}

