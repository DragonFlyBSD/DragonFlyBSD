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

#include "hammer_util.h"

static int CacheUse;
static int CacheMax = HAMMER_BUFSIZE * 1024;
static TAILQ_HEAD(, cache_info) CacheList = TAILQ_HEAD_INITIALIZER(CacheList);

int
hammer_parse_cache_size(const char *arg)
{
	char *ptr;
	int size = strtol(arg, &ptr, 0);

	switch(*ptr) {
	case 'm':
	case 'M':
		size *= 1024;
		/* fall through */
	case 'k':
	case 'K':
		size *= 1024;
		++ptr;
		break;
	case '\0':
	case ':':
		/* bytes if no suffix */
		break;
	default:
		return(-1);
	}

	if (*ptr == ':') {
		UseReadAhead = strtol(ptr + 1, NULL, 0);
		UseReadBehind = -UseReadAhead;
	}
	if (size < 1024 * 1024)
		size = 1024 * 1024;
	if (UseReadAhead < 0)
		return(-1);
	if (UseReadAhead * HAMMER_BUFSIZE / size / 16) {
		UseReadAhead = size / 16 / HAMMER_BUFSIZE;
		UseReadBehind = -UseReadAhead;
	}

	CacheMax = size;
	return(0);
}

void
hammer_cache_add(struct cache_info *cache)
{
	TAILQ_INSERT_HEAD(&CacheList, cache, entry);
	CacheUse += HAMMER_BUFSIZE;
}

void
hammer_cache_del(struct cache_info *cache)
{
	TAILQ_REMOVE(&CacheList, cache, entry);
	CacheUse -= HAMMER_BUFSIZE;
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
	struct cache_info *first = NULL;
	int count = 0;

	if (CacheUse >= CacheMax) {
		while ((cache = TAILQ_FIRST(&CacheList)) != NULL) {
			if (cache == first)
				break; /* seen this ref'd before */

			if (cache->refs) {
				if (first == NULL)
					first = cache;
				hammer_cache_used(cache);
				count++;
				continue;
			}
			if (count >= (CacheUse / HAMMER_BUFSIZE)) {
				CacheMax += HAMMER_BUFSIZE * 512;
				count = 0;
			}

			cache->refs = 1;
			cache->delete = 1;
			rel_buffer((buffer_info_t)cache);

			if (CacheUse < CacheMax / 2)
				break;
		}
	}
}

