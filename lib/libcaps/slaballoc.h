/*
 * SLABALLOC.H	- Userland SLAB memory allocator
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
 * $DragonFly: src/lib/libcaps/slaballoc.h,v 1.3 2004/03/06 19:48:22 dillon Exp $
 */

#ifndef _LIBCAPS_SLABALLOC_H_
#define _LIBCAPS_SLABALLOC_H_

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

/*
 * The IN-BAND zone header is placed at the beginning of each zone.
 */
typedef struct SLZone {
    __int32_t	z_Magic;	/* magic number for sanity check */
    int		z_Cpu;		/* which cpu owns this zone? */
    struct globaldata *z_CpuGd;
    int		z_NFree;	/* total free chunks / ualloc space in zone */
    struct SLZone *z_Next;	/* ZoneAry[] link if z_NFree non-zero */
    int		z_NMax;		/* maximum free chunks */
    char	*z_BasePtr;	/* pointer to start of chunk array */
    int		z_UIndex;	/* current initial allocation index */
    int		z_UEndIndex;	/* last (first) allocation index */
    int		z_ChunkSize;	/* chunk size for validation */
    int		z_FirstFreePg;	/* chunk list on a page-by-page basis */
    int		z_ZoneIndex;
    int		z_Flags;
    SLChunk	*z_PageAry[ZALLOC_MAX_ZONE_SIZE / PAGE_SIZE];
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

typedef struct SLOversized {
    struct SLOversized	*ov_Next;
    void		*ov_Ptr;
    __uintptr_t		ov_Bytes;
} SLOversized;

void slab_init(void);
void slab_malloc_init(void *data);
void *slab_malloc(unsigned long size, struct malloc_type *type, int flags);
void slab_free(void *ptr, struct malloc_type *info);

#endif

