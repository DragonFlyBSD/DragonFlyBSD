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
 *
 * $DragonFly: src/sys/dev/acpica5/Osd/OsdCache.c,v 1.2 2007/01/19 23:58:53 y0netan1 Exp $
 */

#include <sys/objcache.h>

struct acpicache {
	struct objcache *cache;
	struct objcache_malloc_args args;
};

#include "acpi.h"

#ifndef ACPI_USE_LOCAL_CACHE

ACPI_STATUS
AcpiOsCreateCache(char *CacheName, UINT16 ObjectSize, UINT16 MaxDepth,
    ACPI_CACHE_T **ReturnCache)
{
	ACPI_CACHE_T *cache;

	cache = kmalloc(sizeof(*cache), M_TEMP, M_WAITOK);
	cache->args.objsize = ObjectSize;
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
	void *Object;

	Object = objcache_get(Cache->cache, M_WAITOK);
	bzero(Object, Cache->args.objsize);
	return Object;
}

ACPI_STATUS
AcpiOsReleaseObject(ACPI_CACHE_T *Cache, void *Object)
{
	objcache_put(Cache->cache, Object);
	return AE_OK;
}

#endif
