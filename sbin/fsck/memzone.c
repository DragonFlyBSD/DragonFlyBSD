/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fsutil.h"
#include "memzone.h"

/*
 * Efficiently allocate memory that will only be freed in bulk
 */
void *
mzalloc(struct memzone *zone, int bytes)
{
	struct memchunk *chunk;
	void *ptr;

	if ((chunk = zone->curr) != NULL) {
		if (bytes > chunk->bytes - zone->index) {
			chunk->next = zone->list;
			zone->list = chunk;
			zone->curr = NULL;
			chunk = NULL;
		}
	}
	if (chunk == NULL) {
		chunk = malloc(sizeof(*chunk));
		if (chunk == NULL)
			return(NULL);
		bzero(chunk, sizeof(*chunk));
		chunk->base = mmap(NULL, MEMZONE_CHUNK, PROT_READ|PROT_WRITE,
				   MAP_ANON|MAP_PRIVATE, -1, 0);
		if (chunk->base == MAP_FAILED) {
			free(chunk);
			return(NULL);
		}
		chunk->bytes = MEMZONE_CHUNK;
		zone->curr = chunk;
		zone->index = 0;
	}
	if (bytes > chunk->bytes)
		pfatal("allocation to large for mzalloc!");
	ptr = chunk->base + zone->index;
	zone->index += (bytes + 7) & ~7;
	return(ptr);
}

/*
 * Free memory in bulk
 */
void
mzpurge(struct memzone *zone)
{
	struct memchunk *chunk;

	if ((chunk = zone->curr) != NULL) {
		chunk->next = zone->list;
		zone->list = chunk;
		zone->curr = NULL;
	}
	while ((chunk = zone->list) != NULL) {
		zone->list = chunk->next;
		munmap(chunk->base, chunk->bytes);
		free(chunk);
	}
}

