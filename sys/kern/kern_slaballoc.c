/*
 * (MPSAFE)
 *
 * KERN_SLABALLOC.C	- Kernel SLAB memory allocator
 * 
 * Copyright (c) 2003,2004,2010 The DragonFly Project.  All rights reserved.
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
 *
 * This module implements a slab allocator drop-in replacement for the
 * kernel malloc().
 *
 * A slab allocator reserves a ZONE for each chunk size, then lays the
 * chunks out in an array within the zone.  Allocation and deallocation
 * is nearly instantanious, and fragmentation/overhead losses are limited
 * to a fixed worst-case amount.
 *
 * The downside of this slab implementation is in the chunk size
 * multiplied by the number of zones.  ~80 zones * 128K = 10MB of VM per cpu.
 * In a kernel implementation all this memory will be physical so
 * the zone size is adjusted downward on machines with less physical
 * memory.  The upside is that overhead is bounded... this is the *worst*
 * case overhead.
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
 * XXX If we have to allocate a new zone and M_USE_RESERVE is set, use of
 * the new zone should be restricted to M_USE_RESERVE requests only.
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
 * Alignment properties:
 * - All power-of-2 sized allocations are power-of-2 aligned.
 * - Allocations with M_POWEROF2 are power-of-2 aligned on the nearest
 *   power-of-2 round up of 'size'.
 * - Non-power-of-2 sized allocations are zone chunk size aligned (see the
 *   above table 'Chunking' column).
 *
 *			API REQUIREMENTS AND SIDE EFFECTS
 *
 *    To operate as a drop-in replacement to the FreeBSD-4.x malloc() we
 *    have remained compatible with the following API requirements:
 *
 *    + malloc(0) is allowed and returns non-NULL (ahc driver)
 *    + ability to allocate arbitrarily large chunks of memory
 */

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/slaballoc.h>
#include <sys/mbuf.h>
#include <sys/vmmeter.h>
#include <sys/lock.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>

#include <machine/cpu.h>

#include <sys/thread2.h>
#include <vm/vm_page2.h>

#define btokup(z)	(&pmap_kvtom((vm_offset_t)(z))->ku_pagecnt)

#define MEMORY_STRING	"ptr=%p type=%p size=%lu flags=%04x"
#define MEMORY_ARGS	void *ptr, void *type, unsigned long size, int flags

#if !defined(KTR_MEMORY)
#define KTR_MEMORY	KTR_ALL
#endif
KTR_INFO_MASTER(memory);
KTR_INFO(KTR_MEMORY, memory, malloc_beg, 0, "malloc begin");
KTR_INFO(KTR_MEMORY, memory, malloc_end, 1, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_zero, 2, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_ovsz, 3, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_ovsz_delayed, 4, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_chunk, 5, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_request, 6, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_rem_beg, 7, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_rem_end, 8, MEMORY_STRING, MEMORY_ARGS);
KTR_INFO(KTR_MEMORY, memory, free_beg, 9, "free begin");
KTR_INFO(KTR_MEMORY, memory, free_end, 10, "free end");

#define logmemory(name, ptr, type, size, flags)				\
	KTR_LOG(memory_ ## name, ptr, type, size, flags)
#define logmemory_quick(name)						\
	KTR_LOG(memory_ ## name)

/*
 * Fixed globals (not per-cpu)
 */
static int ZoneSize;
static int ZoneLimit;
static int ZonePageCount;
static uintptr_t ZoneMask;
static int ZoneBigAlloc;		/* in KB */
static int ZoneGenAlloc;		/* in KB */
struct malloc_type *kmemstatistics;	/* exported to vmstat */
static int32_t weirdary[16];

static void *kmem_slab_alloc(vm_size_t bytes, vm_offset_t align, int flags);
static void kmem_slab_free(void *ptr, vm_size_t bytes);

#if defined(INVARIANTS)
static void chunk_mark_allocated(SLZone *z, void *chunk);
static void chunk_mark_free(SLZone *z, void *chunk);
#else
#define chunk_mark_allocated(z, chunk)
#define chunk_mark_free(z, chunk)
#endif

/*
 * Misc constants.  Note that allocations that are exact multiples of 
 * PAGE_SIZE, or exceed the zone limit, fall through to the kmem module.
 */
#define ZONE_RELS_THRESH	32		/* threshold number of zones */

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

MALLOC_DEFINE(M_CACHE, "cache", "Various Dynamically allocated caches");
MALLOC_DEFINE(M_DEVBUF, "devbuf", "device driver memory");
MALLOC_DEFINE(M_TEMP, "temp", "misc temporary data buffers");
 
MALLOC_DEFINE(M_IP6OPT, "ip6opt", "IPv6 options");
MALLOC_DEFINE(M_IP6NDP, "ip6ndp", "IPv6 Neighbor Discovery");

/*
 * Initialize the slab memory allocator.  We have to choose a zone size based
 * on available physical memory.  We choose a zone side which is approximately
 * 1/1024th of our memory, so if we have 128MB of ram we have a zone size of
 * 128K.  The zone size is limited to the bounds set in slaballoc.h
 * (typically 32K min, 128K max). 
 */
static void kmeminit(void *dummy);

char *ZeroPage;

SYSINIT(kmem, SI_BOOT1_ALLOCATOR, SI_ORDER_FIRST, kmeminit, NULL);

#ifdef INVARIANTS
/*
 * If enabled any memory allocated without M_ZERO is initialized to -1.
 */
static int  use_malloc_pattern;
SYSCTL_INT(_debug, OID_AUTO, use_malloc_pattern, CTLFLAG_RW,
    &use_malloc_pattern, 0,
    "Initialize memory to -1 if M_ZERO not specified");
#endif

static int ZoneRelsThresh = ZONE_RELS_THRESH;
SYSCTL_INT(_kern, OID_AUTO, zone_big_alloc, CTLFLAG_RD, &ZoneBigAlloc, 0, "");
SYSCTL_INT(_kern, OID_AUTO, zone_gen_alloc, CTLFLAG_RD, &ZoneGenAlloc, 0, "");
SYSCTL_INT(_kern, OID_AUTO, zone_cache, CTLFLAG_RW, &ZoneRelsThresh, 0, "");
static long SlabsAllocated;
static long SlabsFreed;
SYSCTL_LONG(_kern, OID_AUTO, slabs_allocated, CTLFLAG_RD, &SlabsAllocated, 0, "");
SYSCTL_LONG(_kern, OID_AUTO, slabs_freed, CTLFLAG_RD, &SlabsFreed, 0, "");

/*
 * Returns the kernel memory size limit for the purposes of initializing
 * various subsystem caches.  The smaller of available memory and the KVM
 * memory space is returned.
 *
 * The size in megabytes is returned.
 */
size_t
kmem_lim_size(void)
{
    size_t limsize;

    limsize = (size_t)vmstats.v_page_count * PAGE_SIZE;
    if (limsize > KvaSize)
	limsize = KvaSize;
    return (limsize / (1024 * 1024));
}

static void
kmeminit(void *dummy)
{
    size_t limsize;
    int usesize;
    int i;

    limsize = kmem_lim_size();
    usesize = (int)(limsize * 1024);	/* convert to KB */

    /*
     * If the machine has a large KVM space and more than 8G of ram,
     * double the zone release threshold to reduce SMP invalidations.
     * If more than 16G of ram, do it again.
     *
     * The BIOS eats a little ram so add some slop.  We want 8G worth of
     * memory sticks to trigger the first adjustment.
     */
    if (ZoneRelsThresh == ZONE_RELS_THRESH) {
	    if (limsize >= 7 * 1024)
		    ZoneRelsThresh *= 2;
	    if (limsize >= 15 * 1024)
		    ZoneRelsThresh *= 2;
    }

    /*
     * Calculate the zone size.  This typically calculates to
     * ZALLOC_MAX_ZONE_SIZE
     */
    ZoneSize = ZALLOC_MIN_ZONE_SIZE;
    while (ZoneSize < ZALLOC_MAX_ZONE_SIZE && (ZoneSize << 1) < usesize)
	ZoneSize <<= 1;
    ZoneLimit = ZoneSize / 4;
    if (ZoneLimit > ZALLOC_ZONE_LIMIT)
	ZoneLimit = ZALLOC_ZONE_LIMIT;
    ZoneMask = ~(uintptr_t)(ZoneSize - 1);
    ZonePageCount = ZoneSize / PAGE_SIZE;

    for (i = 0; i < NELEM(weirdary); ++i)
	weirdary[i] = WEIRD_ADDR;

    ZeroPage = kmem_slab_alloc(PAGE_SIZE, PAGE_SIZE, M_WAITOK|M_ZERO);

    if (bootverbose)
	kprintf("Slab ZoneSize set to %dKB\n", ZoneSize / 1024);
}

/*
 * Initialize a malloc type tracking structure.
 */
void
malloc_init(void *data)
{
    struct malloc_type *type = data;
    size_t limsize;

    if (type->ks_magic != M_MAGIC)
	panic("malloc type lacks magic");
					   
    if (type->ks_limit != 0)
	return;

    if (vmstats.v_page_count == 0)
	panic("malloc_init not allowed before vm init");

    limsize = kmem_lim_size() * (1024 * 1024);
    type->ks_limit = limsize / 10;

    type->ks_next = kmemstatistics;
    kmemstatistics = type;
}

void
malloc_uninit(void *data)
{
    struct malloc_type *type = data;
    struct malloc_type *t;
#ifdef INVARIANTS
    int i;
    long ttl;
#endif

    if (type->ks_magic != M_MAGIC)
	panic("malloc type lacks magic");

    if (vmstats.v_page_count == 0)
	panic("malloc_uninit not allowed before vm init");

    if (type->ks_limit == 0)
	panic("malloc_uninit on uninitialized type");

    /* Make sure that all pending kfree()s are finished. */
    lwkt_synchronize_ipiqs("muninit");

#ifdef INVARIANTS
    /*
     * memuse is only correct in aggregation.  Due to memory being allocated
     * on one cpu and freed on another individual array entries may be 
     * negative or positive (canceling each other out).
     */
    for (i = ttl = 0; i < ncpus; ++i)
	ttl += type->ks_memuse[i];
    if (ttl) {
	kprintf("malloc_uninit: %ld bytes of '%s' still allocated on cpu %d\n",
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
 * Increase the kmalloc pool limit for the specified pool.  No changes
 * are the made if the pool would shrink.
 */
void
kmalloc_raise_limit(struct malloc_type *type, size_t bytes)
{
    if (type->ks_limit == 0)
	malloc_init(type);
    if (bytes == 0)
	bytes = KvaSize;
    if (type->ks_limit < bytes)
	type->ks_limit = bytes;
}

/*
 * Dynamically create a malloc pool.  This function is a NOP if *typep is
 * already non-NULL.
 */
void
kmalloc_create(struct malloc_type **typep, const char *descr)
{
	struct malloc_type *type;

	if (*typep == NULL) {
		type = kmalloc(sizeof(*type), M_TEMP, M_WAITOK | M_ZERO);
		type->ks_magic = M_MAGIC;
		type->ks_shortdesc = descr;
		malloc_init(type);
		*typep = type;
	}
}

/*
 * Destroy a dynamically created malloc pool.  This function is a NOP if
 * the pool has already been destroyed.
 */
void
kmalloc_destroy(struct malloc_type **typep)
{
	if (*typep != NULL) {
		malloc_uninit(*typep);
		kfree(*typep, M_TEMP);
		*typep = NULL;
	}
}

/*
 * Calculate the zone index for the allocation request size and set the
 * allocation request size to that particular zone's chunk size.
 */
static __inline int
zoneindex(unsigned long *bytes, unsigned long *align)
{
    unsigned int n = (unsigned int)*bytes;	/* unsigned for shift opt */
    if (n < 128) {
	*bytes = n = (n + 7) & ~7;
	*align = 8;
	return(n / 8 - 1);		/* 8 byte chunks, 16 zones */
    }
    if (n < 256) {
	*bytes = n = (n + 15) & ~15;
	*align = 16;
	return(n / 16 + 7);
    }
    if (n < 8192) {
	if (n < 512) {
	    *bytes = n = (n + 31) & ~31;
	    *align = 32;
	    return(n / 32 + 15);
	}
	if (n < 1024) {
	    *bytes = n = (n + 63) & ~63;
	    *align = 64;
	    return(n / 64 + 23);
	} 
	if (n < 2048) {
	    *bytes = n = (n + 127) & ~127;
	    *align = 128;
	    return(n / 128 + 31);
	}
	if (n < 4096) {
	    *bytes = n = (n + 255) & ~255;
	    *align = 256;
	    return(n / 256 + 39);
	}
	*bytes = n = (n + 511) & ~511;
	*align = 512;
	return(n / 512 + 47);
    }
#if ZALLOC_ZONE_LIMIT > 8192
    if (n < 16384) {
	*bytes = n = (n + 1023) & ~1023;
	*align = 1024;
	return(n / 1024 + 55);
    }
#endif
#if ZALLOC_ZONE_LIMIT > 16384
    if (n < 32768) {
	*bytes = n = (n + 2047) & ~2047;
	*align = 2048;
	return(n / 2048 + 63);
    }
#endif
    panic("Unexpected byte count %d", n);
    return(0);
}

static __inline
void
clean_zone_rchunks(SLZone *z)
{
    SLChunk *bchunk;

    while ((bchunk = z->z_RChunks) != NULL) {
	cpu_ccfence();
	if (atomic_cmpset_ptr(&z->z_RChunks, bchunk, NULL)) {
	    *z->z_LChunksp = bchunk;
	    while (bchunk) {
		chunk_mark_free(z, bchunk);
		z->z_LChunksp = &bchunk->c_Next;
		bchunk = bchunk->c_Next;
		++z->z_NFree;
	    }
	    break;
	}
	/* retry */
    }
}

/*
 * If the zone becomes totally free, and there are other zones we
 * can allocate from, move this zone to the FreeZones list.  Since
 * this code can be called from an IPI callback, do *NOT* try to mess
 * with kernel_map here.  Hysteresis will be performed at malloc() time.
 */
static __inline
SLZone *
check_zone_free(SLGlobalData *slgd, SLZone *z)
{
    if (z->z_NFree == z->z_NMax &&
	(z->z_Next || LIST_FIRST(&slgd->ZoneAry[z->z_ZoneIndex]) != z) &&
	z->z_RCount == 0
    ) {
	SLZone *znext;
	int *kup;

	znext = LIST_NEXT(z, z_Entry);
	LIST_REMOVE(z, z_Entry);

	z->z_Magic = -1;
	LIST_INSERT_HEAD(&slgd->FreeZones, z, z_Entry);
	++slgd->NFreeZones;
	kup = btokup(z);
	*kup = 0;
	z = znext;
    } else {
	z = LIST_NEXT(z, z_Entry);
    }
    return z;
}

#ifdef SLAB_DEBUG
/*
 * Used to debug memory corruption issues.  Record up to (typically 32)
 * allocation sources for this zone (for a particular chunk size).
 */

static void
slab_record_source(SLZone *z, const char *file, int line)
{
    int i;
    int b = line & (SLAB_DEBUG_ENTRIES - 1);

    i = b;
    do {
	if (z->z_Sources[i].file == file && z->z_Sources[i].line == line)
		return;
	if (z->z_Sources[i].file == NULL)
		break;
	i = (i + 1) & (SLAB_DEBUG_ENTRIES - 1);
    } while (i != b);
    z->z_Sources[i].file = file;
    z->z_Sources[i].line = line;
}

#endif

static __inline unsigned long
powerof2_size(unsigned long size)
{
	int i;

	if (size == 0 || powerof2(size))
		return size;

	i = flsl(size);
	return (1UL << i);
}

/*
 * kmalloc()	(SLAB ALLOCATOR)
 *
 *	Allocate memory via the slab allocator.  If the request is too large,
 *	or if it page-aligned beyond a certain size, we fall back to the
 *	KMEM subsystem.  A SLAB tracking descriptor must be specified, use
 *	&SlabMisc if you don't care.
 *
 *	M_RNOWAIT	- don't block.
 *	M_NULLOK	- return NULL instead of blocking.
 *	M_ZERO		- zero the returned memory.
 *	M_USE_RESERVE	- allow greater drawdown of the free list
 *	M_USE_INTERRUPT_RESERVE - allow the freelist to be exhausted
 *	M_POWEROF2	- roundup size to the nearest power of 2
 *
 * MPSAFE
 */

#ifdef SLAB_DEBUG
void *
kmalloc_debug(unsigned long size, struct malloc_type *type, int flags,
	      const char *file, int line)
#else
void *
kmalloc(unsigned long size, struct malloc_type *type, int flags)
#endif
{
    SLZone *z;
    SLChunk *chunk;
    SLGlobalData *slgd;
    struct globaldata *gd;
    unsigned long align;
    int zi;
#ifdef INVARIANTS
    int i;
#endif

    logmemory_quick(malloc_beg);
    gd = mycpu;
    slgd = &gd->gd_slab;

    /*
     * XXX silly to have this in the critical path.
     */
    if (type->ks_limit == 0) {
	crit_enter();
	malloc_init(type);
	crit_exit();
    }
    ++type->ks_calls;

    if (flags & M_POWEROF2)
	size = powerof2_size(size);

    /*
     * Handle the case where the limit is reached.  Panic if we can't return
     * NULL.  The original malloc code looped, but this tended to
     * simply deadlock the computer.
     *
     * ks_loosememuse is an up-only limit that is NOT MP-synchronized, used
     * to determine if a more complete limit check should be done.  The
     * actual memory use is tracked via ks_memuse[cpu].
     */
    while (type->ks_loosememuse >= type->ks_limit) {
	int i;
	long ttl;

	for (i = ttl = 0; i < ncpus; ++i)
	    ttl += type->ks_memuse[i];
	type->ks_loosememuse = ttl;	/* not MP synchronized */
	if ((ssize_t)ttl < 0)		/* deal with occassional race */
		ttl = 0;
	if (ttl >= type->ks_limit) {
	    if (flags & M_NULLOK) {
		logmemory(malloc_end, NULL, type, size, flags);
		return(NULL);
	    }
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
    if (size == 0) {
	logmemory(malloc_end, ZERO_LENGTH_PTR, type, size, flags);
	return(ZERO_LENGTH_PTR);
    }

    /*
     * Handle hysteresis from prior frees here in malloc().  We cannot
     * safely manipulate the kernel_map in free() due to free() possibly
     * being called via an IPI message or from sensitive interrupt code.
     *
     * NOTE: ku_pagecnt must be cleared before we free the slab or we
     *	     might race another cpu allocating the kva and setting
     *	     ku_pagecnt.
     */
    while (slgd->NFreeZones > ZoneRelsThresh && (flags & M_RNOWAIT) == 0) {
	crit_enter();
	if (slgd->NFreeZones > ZoneRelsThresh) {	/* crit sect race */
	    int *kup;

	    z = LIST_FIRST(&slgd->FreeZones);
	    LIST_REMOVE(z, z_Entry);
	    --slgd->NFreeZones;
	    kup = btokup(z);
	    *kup = 0;
	    kmem_slab_free(z, ZoneSize);	/* may block */
	    atomic_add_int(&ZoneGenAlloc, -ZoneSize / 1024);
	}
	crit_exit();
    }

    /*
     * XXX handle oversized frees that were queued from kfree().
     */
    while (LIST_FIRST(&slgd->FreeOvZones) && (flags & M_RNOWAIT) == 0) {
	crit_enter();
	if ((z = LIST_FIRST(&slgd->FreeOvZones)) != NULL) {
	    vm_size_t tsize;

	    KKASSERT(z->z_Magic == ZALLOC_OVSZ_MAGIC);
	    LIST_REMOVE(z, z_Entry);
	    tsize = z->z_ChunkSize;
	    kmem_slab_free(z, tsize);	/* may block */
	    atomic_add_int(&ZoneBigAlloc, -(int)tsize / 1024);
	}
	crit_exit();
    }

    /*
     * Handle large allocations directly.  There should not be very many of
     * these so performance is not a big issue.
     *
     * The backend allocator is pretty nasty on a SMP system.   Use the
     * slab allocator for one and two page-sized chunks even though we lose
     * some efficiency.  XXX maybe fix mmio and the elf loader instead.
     */
    if (size >= ZoneLimit || ((size & PAGE_MASK) == 0 && size > PAGE_SIZE*2)) {
	int *kup;

	size = round_page(size);
	chunk = kmem_slab_alloc(size, PAGE_SIZE, flags);
	if (chunk == NULL) {
	    logmemory(malloc_end, NULL, type, size, flags);
	    return(NULL);
	}
	atomic_add_int(&ZoneBigAlloc, (int)size / 1024);
	flags &= ~M_ZERO;	/* result already zero'd if M_ZERO was set */
	flags |= M_PASSIVE_ZERO;
	kup = btokup(chunk);
	*kup = size / PAGE_SIZE;
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
    zi = zoneindex(&size, &align);
    KKASSERT(zi < NZONES);
    crit_enter();

    if ((z = LIST_FIRST(&slgd->ZoneAry[zi])) != NULL) {
	/*
	 * Locate a chunk - we have to have at least one.  If this is the
	 * last chunk go ahead and do the work to retrieve chunks freed
	 * from remote cpus, and if the zone is still empty move it off
	 * the ZoneAry.
	 */
	if (--z->z_NFree <= 0) {
	    KKASSERT(z->z_NFree == 0);

	    /*
	     * WARNING! This code competes with other cpus.  It is ok
	     * for us to not drain RChunks here but we might as well, and
	     * it is ok if more accumulate after we're done.
	     *
	     * Set RSignal before pulling rchunks off, indicating that we
	     * will be moving ourselves off of the ZoneAry.  Remote ends will
	     * read RSignal before putting rchunks on thus interlocking
	     * their IPI signaling.
	     */
	    if (z->z_RChunks == NULL)
		atomic_swap_int(&z->z_RSignal, 1);

	    clean_zone_rchunks(z);

	    /*
	     * Remove from the zone list if no free chunks remain.
	     * Clear RSignal
	     */
	    if (z->z_NFree == 0) {
		LIST_REMOVE(z, z_Entry);
	    } else {
		z->z_RSignal = 0;
	    }
	}

	/*
	 * Fast path, we have chunks available in z_LChunks.
	 */
	chunk = z->z_LChunks;
	if (chunk) {
		chunk_mark_allocated(z, chunk);
		z->z_LChunks = chunk->c_Next;
		if (z->z_LChunks == NULL)
			z->z_LChunksp = &z->z_LChunks;
#ifdef SLAB_DEBUG
		slab_record_source(z, file, line);
#endif
		goto done;
	}

	/*
	 * No chunks are available in LChunks, the free chunk MUST be
	 * in the never-before-used memory area, controlled by UIndex.
	 *
	 * The consequences are very serious if our zone got corrupted so
	 * we use an explicit panic rather than a KASSERT.
	 */
	if (z->z_UIndex + 1 != z->z_NMax)
	    ++z->z_UIndex;
	else
	    z->z_UIndex = 0;

	if (z->z_UIndex == z->z_UEndIndex)
	    panic("slaballoc: corrupted zone");

	chunk = (SLChunk *)(z->z_BasePtr + z->z_UIndex * size);
	if ((z->z_Flags & SLZF_UNOTZEROD) == 0) {
	    flags &= ~M_ZERO;
	    flags |= M_PASSIVE_ZERO;
	}
	chunk_mark_allocated(z, chunk);
#ifdef SLAB_DEBUG
	slab_record_source(z, file, line);
#endif
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
	int *kup;

	if ((z = LIST_FIRST(&slgd->FreeZones)) != NULL) {
	    LIST_REMOVE(z, z_Entry);
	    --slgd->NFreeZones;
	    bzero(z, sizeof(SLZone));
	    z->z_Flags |= SLZF_UNOTZEROD;
	} else {
	    z = kmem_slab_alloc(ZoneSize, ZoneSize, flags|M_ZERO);
	    if (z == NULL)
		goto fail;
	    atomic_add_int(&ZoneGenAlloc, ZoneSize / 1024);
	}

	/*
	 * How big is the base structure?
	 */
#if defined(INVARIANTS)
	/*
	 * Make room for z_Bitmap.  An exact calculation is somewhat more
	 * complicated so don't make an exact calculation.
	 */
	off = offsetof(SLZone, z_Bitmap[(ZoneSize / size + 31) / 32]);
	bzero(z->z_Bitmap, (ZoneSize / size + 31) / 8);
#else
	off = sizeof(SLZone);
#endif

	/*
	 * Guarentee power-of-2 alignment for power-of-2-sized chunks.
	 * Otherwise properly align the data according to the chunk size.
	 */
	if (powerof2(size))
	    align = size;
	off = (off + align - 1) & ~(align - 1);

	z->z_Magic = ZALLOC_SLAB_MAGIC;
	z->z_ZoneIndex = zi;
	z->z_NMax = (ZoneSize - off) / size;
	z->z_NFree = z->z_NMax - 1;
	z->z_BasePtr = (char *)z + off;
	z->z_UIndex = z->z_UEndIndex = slgd->JunkIndex % z->z_NMax;
	z->z_ChunkSize = size;
	z->z_CpuGd = gd;
	z->z_Cpu = gd->gd_cpuid;
	z->z_LChunksp = &z->z_LChunks;
#ifdef SLAB_DEBUG
	bcopy(z->z_Sources, z->z_AltSources, sizeof(z->z_Sources));
	bzero(z->z_Sources, sizeof(z->z_Sources));
#endif
	chunk = (SLChunk *)(z->z_BasePtr + z->z_UIndex * size);
	LIST_INSERT_HEAD(&slgd->ZoneAry[zi], z, z_Entry);
	if ((z->z_Flags & SLZF_UNOTZEROD) == 0) {
	    flags &= ~M_ZERO;	/* already zero'd */
	    flags |= M_PASSIVE_ZERO;
	}
	kup = btokup(z);
	*kup = -(z->z_Cpu + 1);	/* -1 to -(N+1) */
	chunk_mark_allocated(z, chunk);
#ifdef SLAB_DEBUG
	slab_record_source(z, file, line);
#endif

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
    type->ks_loosememuse += size;	/* not MP synchronized */
    crit_exit();

    if (flags & M_ZERO)
	bzero(chunk, size);
#ifdef INVARIANTS
    else if ((flags & (M_ZERO|M_PASSIVE_ZERO)) == 0) {
	if (use_malloc_pattern) {
	    for (i = 0; i < size; i += sizeof(int)) {
		*(int *)((char *)chunk + i) = -1;
	    }
	}
	chunk->c_Next = (void *)-1; /* avoid accidental double-free check */
    }
#endif
    logmemory(malloc_end, chunk, type, size, flags);
    return(chunk);
fail:
    crit_exit();
    logmemory(malloc_end, NULL, type, size, flags);
    return(NULL);
}

/*
 * kernel realloc.  (SLAB ALLOCATOR) (MP SAFE)
 *
 * Generally speaking this routine is not called very often and we do
 * not attempt to optimize it beyond reusing the same pointer if the
 * new size fits within the chunking of the old pointer's zone.
 */
#ifdef SLAB_DEBUG
void *
krealloc_debug(void *ptr, unsigned long size,
	       struct malloc_type *type, int flags,
	       const char *file, int line)
#else
void *
krealloc(void *ptr, unsigned long size, struct malloc_type *type, int flags)
#endif
{
    unsigned long osize;
    unsigned long align;
    SLZone *z;
    void *nptr;
    int *kup;

    KKASSERT((flags & M_ZERO) == 0);	/* not supported */

    if (ptr == NULL || ptr == ZERO_LENGTH_PTR)
	return(kmalloc_debug(size, type, flags, file, line));
    if (size == 0) {
	kfree(ptr, type);
	return(NULL);
    }

    /*
     * Handle oversized allocations.  XXX we really should require that a
     * size be passed to free() instead of this nonsense.
     */
    kup = btokup(ptr);
    if (*kup > 0) {
	osize = *kup << PAGE_SHIFT;
	if (osize == round_page(size))
	    return(ptr);
	if ((nptr = kmalloc_debug(size, type, flags, file, line)) == NULL)
	    return(NULL);
	bcopy(ptr, nptr, min(size, osize));
	kfree(ptr, type);
	return(nptr);
    }

    /*
     * Get the original allocation's zone.  If the new request winds up
     * using the same chunk size we do not have to do anything.
     */
    z = (SLZone *)((uintptr_t)ptr & ZoneMask);
    kup = btokup(z);
    KKASSERT(*kup < 0);
    KKASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);

    /*
     * Allocate memory for the new request size.  Note that zoneindex has
     * already adjusted the request size to the appropriate chunk size, which
     * should optimize our bcopy().  Then copy and return the new pointer.
     *
     * Resizing a non-power-of-2 allocation to a power-of-2 size does not
     * necessary align the result.
     *
     * We can only zoneindex (to align size to the chunk size) if the new
     * size is not too large.
     */
    if (size < ZoneLimit) {
	zoneindex(&size, &align);
	if (z->z_ChunkSize == size)
	    return(ptr);
    }
    if ((nptr = kmalloc_debug(size, type, flags, file, line)) == NULL)
	return(NULL);
    bcopy(ptr, nptr, min(size, z->z_ChunkSize));
    kfree(ptr, type);
    return(nptr);
}

/*
 * Return the kmalloc limit for this type, in bytes.
 */
long
kmalloc_limit(struct malloc_type *type)
{
    if (type->ks_limit == 0) {
	crit_enter();
	if (type->ks_limit == 0)
	    malloc_init(type);
	crit_exit();
    }
    return(type->ks_limit);
}

/*
 * Allocate a copy of the specified string.
 *
 * (MP SAFE) (MAY BLOCK)
 */
#ifdef SLAB_DEBUG
char *
kstrdup_debug(const char *str, struct malloc_type *type,
	      const char *file, int line)
#else
char *
kstrdup(const char *str, struct malloc_type *type)
#endif
{
    int zlen;	/* length inclusive of terminating NUL */
    char *nstr;

    if (str == NULL)
	return(NULL);
    zlen = strlen(str) + 1;
    nstr = kmalloc_debug(zlen, type, M_WAITOK, file, line);
    bcopy(str, nstr, zlen);
    return(nstr);
}

/*
 * Notify our cpu that a remote cpu has freed some chunks in a zone that
 * we own.  RCount will be bumped so the memory should be good, but validate
 * that it really is.
 */
static
void
kfree_remote(void *ptr)
{
    SLGlobalData *slgd;
    SLZone *z;
    int nfree;
    int *kup;

    slgd = &mycpu->gd_slab;
    z = ptr;
    kup = btokup(z);
    KKASSERT(*kup == -((int)mycpuid + 1));
    KKASSERT(z->z_RCount > 0);
    atomic_subtract_int(&z->z_RCount, 1);

    logmemory(free_rem_beg, z, NULL, 0L, 0);
    KKASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);
    KKASSERT(z->z_Cpu  == mycpu->gd_cpuid);
    nfree = z->z_NFree;

    /*
     * Indicate that we will no longer be off of the ZoneAry by
     * clearing RSignal.
     */
    if (z->z_RChunks)
	z->z_RSignal = 0;

    /*
     * Atomically extract the bchunks list and then process it back
     * into the lchunks list.  We want to append our bchunks to the
     * lchunks list and not prepend since we likely do not have
     * cache mastership of the related data (not that it helps since
     * we are using c_Next).
     */
    clean_zone_rchunks(z);
    if (z->z_NFree && nfree == 0) {
	LIST_INSERT_HEAD(&slgd->ZoneAry[z->z_ZoneIndex], z, z_Entry);
    }

    /*
     * If the zone becomes totally free, and there are other zones we
     * can allocate from, move this zone to the FreeZones list.  Since
     * this code can be called from an IPI callback, do *NOT* try to mess
     * with kernel_map here.  Hysteresis will be performed at malloc() time.
     *
     * Do not move the zone if there is an IPI inflight, otherwise MP
     * races can result in our free_remote code accessing a destroyed
     * zone.
     */
    if (z->z_NFree == z->z_NMax &&
	(z->z_Next || LIST_FIRST(&slgd->ZoneAry[z->z_ZoneIndex]) != z) &&
	z->z_RCount == 0
    ) {
	int *kup;

	LIST_REMOVE(z, z_Entry);
	z->z_Magic = -1;
	LIST_INSERT_HEAD(&slgd->FreeZones, z, z_Entry);
	++slgd->NFreeZones;
	kup = btokup(z);
	*kup = 0;
    }
    logmemory(free_rem_end, z, NULL, 0L, 0);
}

/*
 * free (SLAB ALLOCATOR)
 *
 * Free a memory block previously allocated by malloc.  Note that we do not
 * attempt to update ks_loosememuse as MP races could prevent us from
 * checking memory limits in malloc.
 *
 * MPSAFE
 */
void
kfree(void *ptr, struct malloc_type *type)
{
    SLZone *z;
    SLChunk *chunk;
    SLGlobalData *slgd;
    struct globaldata *gd;
    int *kup;
    unsigned long size;
    SLChunk *bchunk;
    int rsignal;

    logmemory_quick(free_beg);
    gd = mycpu;
    slgd = &gd->gd_slab;

    if (ptr == NULL)
	panic("trying to free NULL pointer");

    /*
     * Handle special 0-byte allocations
     */
    if (ptr == ZERO_LENGTH_PTR) {
	logmemory(free_zero, ptr, type, -1UL, 0);
	logmemory_quick(free_end);
	return;
    }

    /*
     * Panic on bad malloc type
     */
    if (type->ks_magic != M_MAGIC)
	panic("free: malloc type lacks magic");

    /*
     * Handle oversized allocations.  XXX we really should require that a
     * size be passed to free() instead of this nonsense.
     *
     * This code is never called via an ipi.
     */
    kup = btokup(ptr);
    if (*kup > 0) {
	size = *kup << PAGE_SHIFT;
	*kup = 0;
#ifdef INVARIANTS
	KKASSERT(sizeof(weirdary) <= size);
	bcopy(weirdary, ptr, sizeof(weirdary));
#endif
	/*
	 * NOTE: For oversized allocations we do not record the
	 *	     originating cpu.  It gets freed on the cpu calling
	 *	     kfree().  The statistics are in aggregate.
	 *
	 * note: XXX we have still inherited the interrupts-can't-block
	 * assumption.  An interrupt thread does not bump
	 * gd_intr_nesting_level so check TDF_INTTHREAD.  This is
	 * primarily until we can fix softupdate's assumptions about free().
	 */
	crit_enter();
	--type->ks_inuse[gd->gd_cpuid];
	type->ks_memuse[gd->gd_cpuid] -= size;
	if (mycpu->gd_intr_nesting_level ||
	    (gd->gd_curthread->td_flags & TDF_INTTHREAD))
	{
	    logmemory(free_ovsz_delayed, ptr, type, size, 0);
	    z = (SLZone *)ptr;
	    z->z_Magic = ZALLOC_OVSZ_MAGIC;
	    z->z_ChunkSize = size;

	    LIST_INSERT_HEAD(&slgd->FreeOvZones, z, z_Entry);
	    crit_exit();
	} else {
	    crit_exit();
	    logmemory(free_ovsz, ptr, type, size, 0);
	    kmem_slab_free(ptr, size);	/* may block */
	    atomic_add_int(&ZoneBigAlloc, -(int)size / 1024);
	}
	logmemory_quick(free_end);
	return;
    }

    /*
     * Zone case.  Figure out the zone based on the fact that it is
     * ZoneSize aligned. 
     */
    z = (SLZone *)((uintptr_t)ptr & ZoneMask);
    kup = btokup(z);
    KKASSERT(*kup < 0);
    KKASSERT(z->z_Magic == ZALLOC_SLAB_MAGIC);

    /*
     * If we do not own the zone then use atomic ops to free to the
     * remote cpu linked list and notify the target zone using a
     * passive message.
     *
     * The target zone cannot be deallocated while we own a chunk of it,
     * so the zone header's storage is stable until the very moment
     * we adjust z_RChunks.  After that we cannot safely dereference (z).
     *
     * (no critical section needed)
     */
    if (z->z_CpuGd != gd) {
	/*
	 * Making these adjustments now allow us to avoid passing (type)
	 * to the remote cpu.  Note that ks_inuse/ks_memuse is being
	 * adjusted on OUR cpu, not the zone cpu, but it should all still
	 * sum up properly and cancel out.
	 */
	crit_enter();
	--type->ks_inuse[gd->gd_cpuid];
	type->ks_memuse[gd->gd_cpuid] -= z->z_ChunkSize;
	crit_exit();

	/*
	 * WARNING! This code competes with other cpus.  Once we
	 *	    successfully link the chunk to RChunks the remote
	 *	    cpu can rip z's storage out from under us.
	 *
	 *	    Bumping RCount prevents z's storage from getting
	 *	    ripped out.
	 */
	rsignal = z->z_RSignal;
	cpu_lfence();
	if (rsignal)
		atomic_add_int(&z->z_RCount, 1);

	chunk = ptr;
	for (;;) {
	    bchunk = z->z_RChunks;
	    cpu_ccfence();
	    chunk->c_Next = bchunk;
	    cpu_sfence();

	    if (atomic_cmpset_ptr(&z->z_RChunks, bchunk, chunk))
		break;
	}

	/*
	 * We have to signal the remote cpu if our actions will cause
	 * the remote zone to be placed back on ZoneAry so it can
	 * move the zone back on.
	 *
	 * We only need to deal with NULL->non-NULL RChunk transitions
	 * and only if z_RSignal is set.  We interlock by reading rsignal
	 * before adding our chunk to RChunks.  This should result in
	 * virtually no IPI traffic.
	 *
	 * We can use a passive IPI to reduce overhead even further.
	 */
	if (bchunk == NULL && rsignal) {
		logmemory(free_request, ptr, type,
			  (unsigned long)z->z_ChunkSize, 0);
	    lwkt_send_ipiq_passive(z->z_CpuGd, kfree_remote, z);
	    /* z can get ripped out from under us from this point on */
	} else if (rsignal) {
	    atomic_subtract_int(&z->z_RCount, 1);
	    /* z can get ripped out from under us from this point on */
	}
	logmemory_quick(free_end);
	return;
    }

    /*
     * kfree locally
     */
    logmemory(free_chunk, ptr, type, (unsigned long)z->z_ChunkSize, 0);

    crit_enter();
    chunk = ptr;
    chunk_mark_free(z, chunk);

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
     * Add this free non-zero'd chunk to a linked list for reuse.  Add
     * to the front of the linked list so it is more likely to be
     * reallocated, since it is already in our L1 cache.
     */
#ifdef INVARIANTS
    if ((vm_offset_t)chunk < KvaStart || (vm_offset_t)chunk >= KvaEnd)
	panic("BADFREE %p", chunk);
#endif
    chunk->c_Next = z->z_LChunks;
    z->z_LChunks = chunk;
    if (chunk->c_Next == NULL)
	    z->z_LChunksp = &chunk->c_Next;

#ifdef INVARIANTS
    if (chunk->c_Next && (vm_offset_t)chunk->c_Next < KvaStart)
	panic("BADFREE2");
#endif

    /*
     * Bump the number of free chunks.  If it becomes non-zero the zone
     * must be added back onto the appropriate list.
     */
    if (z->z_NFree++ == 0) {
	LIST_INSERT_HEAD(&slgd->ZoneAry[z->z_ZoneIndex], z, z_Entry);
    }

    --type->ks_inuse[z->z_Cpu];
    type->ks_memuse[z->z_Cpu] -= z->z_ChunkSize;

    check_zone_free(slgd, z);
    logmemory_quick(free_end);
    crit_exit();
}

/*
 * Cleanup slabs which are hanging around due to RChunks.  Called once every
 * 10 seconds on all cpus.
 */
void
slab_cleanup(void)
{
    SLGlobalData *slgd = &mycpu->gd_slab;
    SLZone *z;
    int i;

    crit_enter();
    for (i = 0; i < NZONES; ++i) {
	if ((z = LIST_FIRST(&slgd->ZoneAry[i])) == NULL)
		continue;
	z = LIST_NEXT(z, z_Entry);

	/*
	 * Scan zones starting with the second zone in each list.
	 */
	while (z) {
	    /*
	     * Shift all RChunks to the end of the LChunks list.  This is
	     * an O(1) operation.
	     *
	     * Then free the zone if possible.
	     */
	    clean_zone_rchunks(z);
	    z = check_zone_free(slgd, z);
	}
    }
    crit_exit();
}

#if defined(INVARIANTS)

/*
 * Helper routines for sanity checks
 */
static
void
chunk_mark_allocated(SLZone *z, void *chunk)
{
    int bitdex = ((char *)chunk - (char *)z->z_BasePtr) / z->z_ChunkSize;
    __uint32_t *bitptr;

    KKASSERT((((intptr_t)chunk ^ (intptr_t)z) & ZoneMask) == 0);
    KASSERT(bitdex >= 0 && bitdex < z->z_NMax,
	    ("memory chunk %p bit index %d is illegal", chunk, bitdex));
    bitptr = &z->z_Bitmap[bitdex >> 5];
    bitdex &= 31;
    KASSERT((*bitptr & (1 << bitdex)) == 0,
	    ("memory chunk %p is already allocated!", chunk));
    *bitptr |= 1 << bitdex;
}

static
void
chunk_mark_free(SLZone *z, void *chunk)
{
    int bitdex = ((char *)chunk - (char *)z->z_BasePtr) / z->z_ChunkSize;
    __uint32_t *bitptr;

    KKASSERT((((intptr_t)chunk ^ (intptr_t)z) & ZoneMask) == 0);
    KASSERT(bitdex >= 0 && bitdex < z->z_NMax,
	    ("memory chunk %p bit index %d is illegal!", chunk, bitdex));
    bitptr = &z->z_Bitmap[bitdex >> 5];
    bitdex &= 31;
    KASSERT((*bitptr & (1 << bitdex)) != 0,
	    ("memory chunk %p is already free!", chunk));
    *bitptr &= ~(1 << bitdex);
}

#endif

/*
 * kmem_slab_alloc()
 *
 *	Directly allocate and wire kernel memory in PAGE_SIZE chunks with the
 *	specified alignment.  M_* flags are expected in the flags field.
 *
 *	Alignment must be a multiple of PAGE_SIZE.
 *
 *	NOTE! XXX For the moment we use vm_map_entry_reserve/release(),
 *	but when we move zalloc() over to use this function as its backend
 *	we will have to switch to kreserve/krelease and call reserve(0)
 *	after the new space is made available.
 *
 *	Interrupt code which has preempted other code is not allowed to
 *	use PQ_CACHE pages.  However, if an interrupt thread is run
 *	non-preemptively or blocks and then runs non-preemptively, then
 *	it is free to use PQ_CACHE pages.  <--- may not apply any longer XXX
 */
static void *
kmem_slab_alloc(vm_size_t size, vm_offset_t align, int flags)
{
    vm_size_t i;
    vm_offset_t addr;
    int count, vmflags, base_vmflags;
    vm_page_t mbase = NULL;
    vm_page_t m;
    thread_t td;

    size = round_page(size);
    addr = vm_map_min(&kernel_map);

    count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
    crit_enter();
    vm_map_lock(&kernel_map);
    if (vm_map_findspace(&kernel_map, addr, size, align, 0, &addr)) {
	vm_map_unlock(&kernel_map);
	if ((flags & M_NULLOK) == 0)
	    panic("kmem_slab_alloc(): kernel_map ran out of space!");
	vm_map_entry_release(count);
	crit_exit();
	return(NULL);
    }

    /*
     * kernel_object maps 1:1 to kernel_map.
     */
    vm_object_hold(&kernel_object);
    vm_object_reference_locked(&kernel_object);
    vm_map_insert(&kernel_map, &count,
		  &kernel_object, NULL,
		  addr, addr, addr + size,
		  VM_MAPTYPE_NORMAL,
		  VM_PROT_ALL, VM_PROT_ALL,
		  0);
    vm_object_drop(&kernel_object);
    vm_map_set_wired_quick(&kernel_map, addr, size, &count);
    vm_map_unlock(&kernel_map);

    td = curthread;

    base_vmflags = 0;
    if (flags & M_ZERO)
        base_vmflags |= VM_ALLOC_ZERO;
    if (flags & M_USE_RESERVE)
	base_vmflags |= VM_ALLOC_SYSTEM;
    if (flags & M_USE_INTERRUPT_RESERVE)
        base_vmflags |= VM_ALLOC_INTERRUPT;
    if ((flags & (M_RNOWAIT|M_WAITOK)) == 0) {
	panic("kmem_slab_alloc: bad flags %08x (%p)",
	      flags, ((int **)&size)[-1]);
    }

    /*
     * Allocate the pages.  Do not mess with the PG_ZERO flag or map
     * them yet.  VM_ALLOC_NORMAL can only be set if we are not preempting.
     *
     * VM_ALLOC_SYSTEM is automatically set if we are preempting and
     * M_WAITOK was specified as an alternative (i.e. M_USE_RESERVE is
     * implied in this case), though I'm not sure if we really need to
     * do that.
     */
    vmflags = base_vmflags;
    if (flags & M_WAITOK) {
	if (td->td_preempted)
	    vmflags |= VM_ALLOC_SYSTEM;
	else
	    vmflags |= VM_ALLOC_NORMAL;
    }

    vm_object_hold(&kernel_object);
    for (i = 0; i < size; i += PAGE_SIZE) {
	m = vm_page_alloc(&kernel_object, OFF_TO_IDX(addr + i), vmflags);
	if (i == 0)
		mbase = m;

	/*
	 * If the allocation failed we either return NULL or we retry.
	 *
	 * If M_WAITOK is specified we wait for more memory and retry.
	 * If M_WAITOK is specified from a preemption we yield instead of
	 * wait.  Livelock will not occur because the interrupt thread
	 * will not be preempting anyone the second time around after the
	 * yield.
	 */
	if (m == NULL) {
	    if (flags & M_WAITOK) {
		if (td->td_preempted) {
		    lwkt_switch();
		} else {
		    vm_wait(0);
		}
		i -= PAGE_SIZE;	/* retry */
		continue;
	    }
	    break;
	}
    }

    /*
     * Check and deal with an allocation failure
     */
    if (i != size) {
	while (i != 0) {
	    i -= PAGE_SIZE;
	    m = vm_page_lookup(&kernel_object, OFF_TO_IDX(addr + i));
	    /* page should already be busy */
	    vm_page_free(m);
	}
	vm_map_lock(&kernel_map);
	vm_map_delete(&kernel_map, addr, addr + size, &count);
	vm_map_unlock(&kernel_map);
	vm_object_drop(&kernel_object);

	vm_map_entry_release(count);
	crit_exit();
	return(NULL);
    }

    /*
     * Success!
     *
     * NOTE: The VM pages are still busied.  mbase points to the first one
     *	     but we have to iterate via vm_page_next()
     */
    vm_object_drop(&kernel_object);
    crit_exit();

    /*
     * Enter the pages into the pmap and deal with PG_ZERO and M_ZERO.
     */
    m = mbase;
    i = 0;

    while (i < size) {
	/*
	 * page should already be busy
	 */
	m->valid = VM_PAGE_BITS_ALL;
	vm_page_wire(m);
	pmap_enter(&kernel_pmap, addr + i, m, VM_PROT_ALL | VM_PROT_NOSYNC,
		   1, NULL);
	if ((m->flags & PG_ZERO) == 0 && (flags & M_ZERO))
	    bzero((char *)addr + i, PAGE_SIZE);
	vm_page_flag_clear(m, PG_ZERO);
	KKASSERT(m->flags & (PG_WRITEABLE | PG_MAPPED));
	vm_page_flag_set(m, PG_REFERENCED);
	vm_page_wakeup(m);

	i += PAGE_SIZE;
	vm_object_hold(&kernel_object);
	m = vm_page_next(m);
	vm_object_drop(&kernel_object);
    }
    smp_invltlb();
    vm_map_entry_release(count);
    atomic_add_long(&SlabsAllocated, 1);
    return((void *)addr);
}

/*
 * kmem_slab_free()
 */
static void
kmem_slab_free(void *ptr, vm_size_t size)
{
    crit_enter();
    vm_map_remove(&kernel_map, (vm_offset_t)ptr, (vm_offset_t)ptr + size);
    atomic_add_long(&SlabsFreed, 1);
    crit_exit();
}

void *
kmalloc_cachealign(unsigned long size_alloc, struct malloc_type *type,
    int flags)
{
#if (__VM_CACHELINE_SIZE == 32)
#define CAN_CACHEALIGN(sz)	((sz) >= 256)
#elif (__VM_CACHELINE_SIZE == 64)
#define CAN_CACHEALIGN(sz)	((sz) >= 512)
#elif (__VM_CACHELINE_SIZE == 128)
#define CAN_CACHEALIGN(sz)	((sz) >= 1024)
#else
#error "unsupported cacheline size"
#endif

	void *ret;

	if (size_alloc < __VM_CACHELINE_SIZE)
		size_alloc = __VM_CACHELINE_SIZE;
	else if (!CAN_CACHEALIGN(size_alloc))
		flags |= M_POWEROF2;

	ret = kmalloc(size_alloc, type, flags);
	KASSERT(((uintptr_t)ret & (__VM_CACHELINE_SIZE - 1)) == 0,
	    ("%p(%lu) not cacheline %d aligned",
	     ret, size_alloc, __VM_CACHELINE_SIZE));
	return ret;

#undef CAN_CACHEALIGN
}
