/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/cache.c,v 1.5 2008/05/16 18:39:03 dillon Exp $
 */

#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include "hammer_util.h"

static int CacheUse;
static int CacheMax = 16 * 1024 * 1024;
static int NCache;
static TAILQ_HEAD(, cache_info) CacheList = TAILQ_HEAD_INITIALIZER(CacheList);

void
hammer_cache_set(int bytes)
{
	CacheMax = bytes;
}

void
hammer_cache_add(struct cache_info *cache, enum cache_type type)
{
	TAILQ_INSERT_HEAD(&CacheList, cache, entry);
	cache->type = type;
	CacheUse += HAMMER_BUFSIZE;
	++NCache;
}

void
hammer_cache_del(struct cache_info *cache)
{
	TAILQ_REMOVE(&CacheList, cache, entry);
	CacheUse -= HAMMER_BUFSIZE;
	--NCache;
}

void
hammer_cache_used(struct cache_info *cache)
{
	TAILQ_REMOVE(&CacheList, cache, entry);
	TAILQ_INSERT_TAIL(&CacheList, cache, entry);
}

void
hammer_cache_flush(void)
{
	struct cache_info *cache;
	struct cache_info *p = NULL;
	int target;
	int count = 0;
	int ncache = NCache;

	if (CacheUse >= CacheMax) {
		target = CacheMax / 2;
		while ((cache = TAILQ_FIRST(&CacheList)) != NULL) {
			if (cache == p)
				break;  /* seen this before */
			++count;
			if (cache->refs) {
				if (p == NULL)
					p = cache;
				hammer_cache_used(cache);
				continue;
			}
			if (count >= NCache) {
				CacheMax += 8 * 1024 * 1024;
				target = CacheMax / 2;
				count = 0;
			}
			cache->refs = 1;
			cache->delete = 1;
			--count;

			switch(cache->type) {
			case ISVOLUME:
				rel_volume(cache->u.volume);
				break;
			case ISBUFFER:
				rel_buffer(cache->u.buffer);
				break;
			default:
				errx(1, "hammer_cache_flush: unknown type: %d",
				     (int)cache->type);
				/* not reached */
				break;
			}
			/* structure was freed */
			if (CacheUse < target)
				break;
		}
		if (DebugOpt)
			fprintf(stderr, "hammer_cache_flush: free %d/%d cache\n",
				ncache - NCache, ncache);
	}
}

