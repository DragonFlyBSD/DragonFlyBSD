/*
 * Copyright (c) 2006 Jeffrey M. Hsu.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/objcache.h>

struct acpicache {
	struct objcache *cache;
	struct objcache_malloc_args args;
};

/*
 * Add some magic numbers to catch double-frees earlier rather
 * then later.
 */
struct acpiobjhead {
	int state;
	void *cache;
	const char *func;
	int line;
	int unused;
};

#define TRACK_ALLOCATED	0x7AF45533
#define TRACK_FREED	0x7B056644

#define OBJHEADSIZE	sizeof(struct acpiobjhead)

#include "acpi.h"

ACPI_STATUS
AcpiOsCreateCache(char *CacheName, UINT16 ObjectSize, UINT16 MaxDepth,
    ACPI_CACHE_T **ReturnCache)
{
	ACPI_CACHE_T *cache;

	cache = kmalloc(sizeof(*cache), M_TEMP, M_WAITOK);
	cache->args.objsize = OBJHEADSIZE + ObjectSize;
	cache->args.mtype = M_CACHE;
	cache->cache = objcache_create(CacheName, 0, 0, NULL, NULL,
	    NULL, objcache_malloc_alloc, objcache_malloc_free, &cache->args);
	*ReturnCache = cache;
	return AE_OK;
}

ACPI_STATUS
AcpiOsDeleteCache(ACPI_CACHE_T *Cache)
{
	objcache_destroy(Cache->cache);
	kfree(Cache, M_TEMP);
	return AE_OK;
}


ACPI_STATUS
AcpiOsPurgeCache(ACPI_CACHE_T *Cache)
{
	struct objcache *reclaimlist[] = { Cache->cache };

	objcache_reclaimlist(reclaimlist, 1, M_WAITOK);
	return AE_OK;
}

void *
AcpiOsAcquireObject(ACPI_CACHE_T *Cache)
{
	struct acpiobjhead *head;

	head = objcache_get(Cache->cache, M_WAITOK);
	bzero(head, Cache->args.objsize);
	head->state = TRACK_ALLOCATED;
#ifdef ACPI_DEBUG_CACHE
	head->cache = Cache;
	head->func = "nowhere";
	head->line = 0;
#endif
	return (head + 1);
}

ACPI_STATUS
#ifdef ACPI_DEBUG_CACHE
_AcpiOsReleaseObject(ACPI_CACHE_T *Cache, void *Object,
    const char *func, int line)
#else
AcpiOsReleaseObject(ACPI_CACHE_T *Cache, void *Object)
#endif
{
	struct acpiobjhead *head = (void *)((char *)Object - OBJHEADSIZE);

#ifdef ACPI_DEBUG_CACHE
	if (head->cache != Cache) {
		kprintf("%s: object %p belongs to %p, not %p\n",
			__func__, Object, head->cache, Cache);
	}
#endif
	if (head->state != TRACK_ALLOCATED) {
		if (head->state == TRACK_FREED) {
#ifdef ACPI_DEBUG_CACHE
			kprintf("%s: Double Free %p, %s:%d, first %s:%d\n",
				__func__, Object, func, line, head->func,
				head->line);
#else
			kprintf("%s: Double Free %p\n", __func__, Object);
#endif
		} else
			kprintf("AcpiOsReleaseObject: Bad object %p (%08x)\n",
				Object, head->state);
		return AE_OK;
	}
	head->state = TRACK_FREED;
#ifdef ACPI_DEBUG_CACHE
	head->func = func;
	head->line = line;
#endif
	objcache_put(Cache->cache, head);
	return AE_OK;
}
