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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/libkern.h>

#include <sys/tbridge.h>

MALLOC_DEFINE(M_SAFEMEM, "safemem", "safemem kernel port");

struct safe_mem_hdr {
	struct safe_mem_hdr	*prev;
	struct safe_mem_hdr	*next;
	struct safe_mem_tail	*tail;
	const char	*file;
	int		line;
	size_t		alloc_sz;
	char		sig[8]; /* SAFEMEM */
};

struct safe_mem_tail {
	char sig[8]; /* SAFEMEM */
};

static struct safe_mem_hdr *safe_mem_hdr_first = NULL;

static int safemem_error_count = 0;

void
safemem_reset_error_count(void)
{
	safemem_error_count = 0;
}

int
safemem_get_error_count(void)
{
	return safemem_error_count;
}

void *
_alloc_safe_mem(size_t req_sz, const char *file, int line)
{
	struct safe_mem_hdr *hdr, *hdrp;
	struct safe_mem_tail *tail;
	size_t alloc_sz;
	char *mem, *user_mem;

	/* only shift, to make sure things (TM) keep aligned */
	alloc_sz = req_sz;
	mem = kmalloc(alloc_sz << 2, M_SAFEMEM, M_WAITOK | M_ZERO);
	user_mem = mem + alloc_sz;
	hdr = (struct safe_mem_hdr *)(user_mem - sizeof(*hdr));
	tail = (struct safe_mem_tail *)(user_mem + alloc_sz);

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

void
_free_safe_mem(void *mem_ptr, const char *file, int line)
{
	struct safe_mem_hdr *hdr;
	struct safe_mem_tail *tail;
	size_t alloc_sz;
	char *mem = mem_ptr;
	char *user_mem = mem_ptr;

	hdr = (struct safe_mem_hdr *)(user_mem - sizeof(*hdr));
	tail = (struct safe_mem_tail *)(user_mem + hdr->alloc_sz);
	mem -= hdr->alloc_sz;

#ifdef DEBUG
	kprintf("freeing safe_mem (hdr): %#lx (%s:%d)\n",
		    (unsigned long)(void *)hdr, hdr->file, hdr->line);
#endif

	if (hdr->alloc_sz == 0) {
		tbridge_printf("SAFEMEM BUG: double-free at %s:%d !!!\n", file,
		    line);
		return;
	}

	/* Integrity checks */
	if ((memcmp(hdr->sig, "SAFEMEM\0", 8) != 0) ||
	    (memcmp(tail->sig, "SAFEMEM\0", 8) != 0)) {
		tbridge_printf("SAFEMEM BUG: safe_mem buffer under- or overflow "
		    "at %s:%d !!!\n", file, line);
		return;
	}

	if (safe_mem_hdr_first == NULL) {
		tbridge_printf("SAFEMEM BUG: safe_mem list should not be empty "
		    "at %s:%d !!!\n", file, line);
		return;
	}

	if (hdr->prev != NULL)
		hdr->prev->next = hdr->next;
	if (hdr->next != NULL)
		hdr->next->prev = hdr->prev;
	if (safe_mem_hdr_first == hdr)
		safe_mem_hdr_first = hdr->next;

	alloc_sz = hdr->alloc_sz;

	bzero(mem, alloc_sz << 2);
	kfree(mem, M_SAFEMEM);
}

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

		kprintf("SAFEMEM: un-freed safe_mem: %#lx (%s:%d) [integrity=%s]\n",
		    (unsigned long)(void *)hdr, hdr->file, hdr->line,
		    ok? "ok" : "failed");
#endif
		mem = (void *)hdr;
		mem += sizeof(*hdr);
		_free_safe_mem(mem, "check_and_purge_safe_mem", 0);
	}
}
