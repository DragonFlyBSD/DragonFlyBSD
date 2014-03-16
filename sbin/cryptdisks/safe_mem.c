/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "safe_mem.h"

struct safe_mem_hdr {
	struct safe_mem_hdr	*prev;
	struct safe_mem_hdr	*next;
	struct safe_mem_tail	*tail;
	const char	*file;
	int 		line;
	size_t		alloc_sz;
	char		sig[8]; /* SAFEMEM */
};

struct safe_mem_tail {
	char sig[8]; /* SAFEMEM */
};

static struct safe_mem_hdr *safe_mem_hdr_first = NULL;

#pragma weak _alloc_safe_mem
void *
_alloc_safe_mem(size_t req_sz, const char *file, int line)
{
	struct safe_mem_hdr *hdr, *hdrp;
	struct safe_mem_tail *tail;
	size_t alloc_sz;
	char *mem, *user_mem;

	alloc_sz = req_sz + sizeof(*hdr) + sizeof(*tail);
	if ((mem = malloc(alloc_sz)) == NULL)
		return NULL;

	if (mlock(mem, alloc_sz) < 0) {
		free(mem);
		return NULL;
	}

	memset(mem, 0, alloc_sz);

	hdr = (struct safe_mem_hdr *)mem;
	tail = (struct safe_mem_tail *)(mem + alloc_sz - sizeof(*tail));
	user_mem = mem + sizeof(*hdr);

	strcpy(hdr->sig, "SAFEMEM");
	strcpy(tail->sig, "SAFEMEM");
	hdr->tail = tail;
	hdr->alloc_sz = alloc_sz;
	hdr->file = file;
	hdr->line = line;
	hdr->next = NULL;

	if (safe_mem_hdr_first == NULL) {
		safe_mem_hdr_first = hdr;
	} else {
		hdrp = safe_mem_hdr_first;
		while (hdrp->next != NULL)
			hdrp = hdrp->next;
		hdr->prev = hdrp;
		hdrp->next = hdr;
	}

	return user_mem;
}

#pragma weak _free_safe_mem
void
_free_safe_mem(void *mem_ptr, const char *file, int line)
{
	struct safe_mem_hdr *hdr;
	struct safe_mem_tail *tail;
	size_t alloc_sz;
	char *mem = mem_ptr;

	mem -= sizeof(*hdr);
	hdr = (struct safe_mem_hdr *)mem;
	tail = (struct safe_mem_tail *)(mem + hdr->alloc_sz - sizeof(*tail));

#ifdef DEBUG
	fprintf(stderr, "freeing safe_mem (hdr): %#lx (%s:%d)\n",
		    (unsigned long)(void *)hdr, hdr->file, hdr->line);
#endif

	if (hdr->alloc_sz == 0) {
		fprintf(stderr, "BUG: double-free at %s:%d !!!\n", file, line);
		return;
	}

	/* Integrity checks */
	if ((memcmp(hdr->sig, "SAFEMEM\0", 8) != 0) ||
	    (memcmp(tail->sig, "SAFEMEM\0", 8) != 0)) {
		fprintf(stderr, "BUG: safe_mem buffer under- or overflow at "
		    "%s:%d !!!\n", file, line);
		return;
	}

	if (safe_mem_hdr_first == NULL) {
		fprintf(stderr, "BUG: safe_mem list should not be empty at "
		    "%s:%d !!!\n", file, line);
		return;
	}

	if (hdr->prev != NULL)
		hdr->prev->next = hdr->next;
	if (hdr->next != NULL)
		hdr->next->prev = hdr->prev;
	if (safe_mem_hdr_first == hdr)
		safe_mem_hdr_first = hdr->next;

	alloc_sz = hdr->alloc_sz;
	memset(mem, 0xFF, alloc_sz);
	memset(mem, 0, alloc_sz);

	free(mem);
}

#pragma weak check_and_purge_safe_mem
void
check_and_purge_safe_mem(void)
{
	struct safe_mem_hdr *hdr;
	char *mem;
#ifdef DEBUG
	int ok;
#endif

	if (safe_mem_hdr_first == NULL)
		return;

	hdr = safe_mem_hdr_first;
	while ((hdr = safe_mem_hdr_first) != NULL) {
#ifdef DEBUG
		if ((hdr->alloc_sz > 0) &&
		    (memcmp(hdr->sig, "SAFEMEM\0", 8) == 0) &&
		    (memcmp(hdr->tail->sig, "SAFEMEM\0", 8) == 0))
			ok = 1;
		else
			ok = 0;

		fprintf(stderr, "un-freed safe_mem: %#lx (%s:%d) [integrity=%s]\n",
		    (unsigned long)(void *)hdr, hdr->file, hdr->line,
		    ok? "ok" : "failed");
#endif
		mem = (void *)hdr;
		mem += sizeof(*hdr);
		_free_safe_mem(mem, "check_and_purge_safe_mem", 0);
	}
}
