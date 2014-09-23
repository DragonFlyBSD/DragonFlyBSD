/*
 * KERN_SLABALLOC.H	- Kernel SLAB memory allocator
 * 
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/sys/slaballoc.h,v 1.8 2005/06/20 20:49:12 dillon Exp $
 */

#ifndef _SYS_SLABALLOC_H_
#define _SYS_SLABALLOC_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>
#endif
#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif

/*
 * Note that any allocations which are exact multiples of PAGE_SIZE, or
 * which are >= ZALLOC_ZONE_LIMIT, will fall through to the kmem subsystem.
 */
#define ZALLOC_ZONE_LIMIT	(16 * 1024)	/* max slab-managed alloc */
#define ZALLOC_MIN_ZONE_SIZE	(32 * 1024)	/* minimum zone size */
#define ZALLOC_MAX_ZONE_SIZE	(128 * 1024)	/* maximum zone size */
#define ZALLOC_SLAB_MAGIC	0x736c6162	/* magic sanity */
#define ZALLOC_OVSZ_MAGIC	0x736c6163	/* magic sanity */
#define ZALLOC_SLAB_SLIDE	20


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
typedef struct SLChunk {
    struct SLChunk *c_Next;
} SLChunk;

#if defined(SLAB_DEBUG)
/*
 * Only used for kernels compiled w/SLAB_DEBUG
 */
struct ZSources {
    const char *file;
    int line;
};

#endif

/*
 * The IN-BAND zone header is placed at the beginning of each zone.
 *
 * NOTE! All fields are cpu-local except z_RChunks.  Remote cpus free
 *	 chunks using atomic ops to z_RChunks and then signal local
 *	 cpus as necessary.
 */
typedef struct SLZone {
    __int32_t	z_Magic;	/* magic number for sanity check */
    int		z_Cpu;		/* which cpu owns this zone? */
    struct globaldata *z_CpuGd;	/* which cpu owns this zone? */
    struct SLZone *z_Next;	/* ZoneAry[] link if z_NFree non-zero */
    int		z_NFree;	/* total free chunks / ualloc space in zone */
    int		z_NMax;		/* maximum free chunks */
    char	*z_BasePtr;	/* pointer to start of chunk array */
    int		z_UIndex;	/* current initial allocation index */
    int		z_UEndIndex;	/* last (first) allocation index */
    int		z_ChunkSize;	/* chunk size for validation */
    int		z_ZoneIndex;
    int		z_Flags;
    SLChunk	*z_LChunks;	/* linked list of chunks current cpu */
    SLChunk	**z_LChunksp;	/* tailp */
    SLChunk	*z_RChunks;	/* linked list of chunks remote cpu */
    int		z_RSignal;	/* signal interlock */
    int		z_RCount;	/* prevent local destruction w/inflight ipis */
#if defined(SLAB_DEBUG)
#define SLAB_DEBUG_ENTRIES	32	/* must be power of 2 */
    struct ZSources z_Sources[SLAB_DEBUG_ENTRIES];
    struct ZSources z_AltSources[SLAB_DEBUG_ENTRIES];
#endif
#if defined(INVARIANTS)
    __uint32_t	z_Bitmap[];	/* bitmap of free chunks for sanity check */
#endif
} SLZone;

#define SLZF_UNOTZEROD		0x0001

typedef struct SLGlobalData {
    SLZone	*ZoneAry[NZONES];	/* linked list of zones NFree > 0 */
    SLZone	*FreeZones;		/* whole zones that have become free */
    SLZone	*FreeOvZones;		/* oversized zones */
    int		NFreeZones;		/* free zone count */
    int		JunkIndex;
    struct malloc_type ZoneInfo;	/* stats on meta-zones allocated */
} SLGlobalData;

#endif	/* _KERNEL */

#endif	/* _SYS_SLABALLOC_H_ */
