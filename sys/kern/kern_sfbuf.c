/*
 * Copyright (c) 1998 David Greenman.  All rights reserved. 
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
 * $DragonFly: src/sys/kern/kern_sfbuf.c,v 1.2 2004/03/29 15:46:18 dillon Exp $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sfbuf.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

static void sf_buf_init(void *arg);
SYSINIT(sock_sf, SI_SUB_MBUF, SI_ORDER_ANY, sf_buf_init, NULL)

LIST_HEAD(sf_buf_list, sf_buf);

/*
 * A hash table of active sendfile(2) buffers
 */
static struct sf_buf_list *sf_buf_hashtable;
static u_long sf_buf_hashmask;

#define	SF_BUF_HASH(m)	(((m) - vm_page_array) & sf_buf_hashmask)

static TAILQ_HEAD(, sf_buf) sf_buf_freelist;
static u_int sf_buf_alloc_want;

static vm_offset_t sf_base;
static struct sf_buf *sf_bufs;

/*
 * Allocate a pool of sf_bufs (sendfile(2) or "super-fast" if you prefer. :-))
 */
static void
sf_buf_init(void *arg)
{
	int i;

	sf_buf_hashtable = hashinit(nsfbufs, M_TEMP, &sf_buf_hashmask);
	TAILQ_INIT(&sf_buf_freelist);
	sf_base = kmem_alloc_nofault(kernel_map, nsfbufs * PAGE_SIZE);
	sf_bufs = malloc(nsfbufs * sizeof(struct sf_buf), M_TEMP,
	    M_NOWAIT | M_ZERO);
	for (i = 0; i < nsfbufs; i++) {
		sf_bufs[i].kva = sf_base + i * PAGE_SIZE;
		TAILQ_INSERT_TAIL(&sf_buf_freelist, &sf_bufs[i], free_entry);
	}
}

/*
 * Get an sf_buf from the freelist. Will block if none are available.
 */
struct sf_buf *
sf_buf_alloc(struct vm_page *m)
{
	struct sf_buf_list *hash_chain;
	struct sf_buf *sf;
	int s;
	int error;

	s = splimp();
	hash_chain = &sf_buf_hashtable[SF_BUF_HASH(m)];
	LIST_FOREACH(sf, hash_chain, list_entry) {
		if (sf->m == m) {
			if (sf->refcnt == 0) {
				/* reclaim cached entry off freelist */
				TAILQ_REMOVE(&sf_buf_freelist, sf, free_entry);
			}
			++sf->refcnt;
			goto done;	/* found existing mapping */
		}
	}

	/*
	 * Didn't find old mapping.  Get a buffer off the freelist.
	 */
	while ((sf = TAILQ_FIRST(&sf_buf_freelist)) == NULL) {
		++sf_buf_alloc_want;
		error = tsleep(&sf_buf_freelist, PCATCH, "sfbufa", 0);
		--sf_buf_alloc_want;

		/* If we got a signal, don't risk going back to sleep. */
		if (error)
			goto done;
	}
	TAILQ_REMOVE(&sf_buf_freelist, sf, free_entry);

	if (sf->m != NULL)	/* remove previous mapping from hash table */
		LIST_REMOVE(sf, list_entry);
	LIST_INSERT_HEAD(hash_chain, sf, list_entry);
	sf->refcnt = 1;
	sf->m = m;
	pmap_qenter(sf->kva, &sf->m, 1);
done:
	splx(s);
	return (sf);
}

#define dtosf(x)	(&sf_bufs[((uintptr_t)(x) - (uintptr_t)sf_base) >> PAGE_SHIFT])

struct sf_buf *
sf_buf_tosf(caddr_t addr)
{
	return(dtosf(addr));
}

void
sf_buf_ref(struct sf_buf *sf)
{
	if (sf->refcnt == 0)
		panic("sf_buf_ref: referencing a free sf_buf");
	sf->refcnt++;
}

/*
 * Lose a reference to an sf_buf. When none left, detach mapped page
 * and release resources back to the system.
 *
 * Must be called at splimp.
 */
void
sf_buf_free(struct sf_buf *sf)
{
	if (sf->refcnt == 0)
		panic("sf_buf_free: freeing free sf_buf");
	sf->refcnt--;
	if (sf->refcnt == 0) {
		KKASSERT(sf->aux1 == 0 && sf->aux2 == 0);
		TAILQ_INSERT_TAIL(&sf_buf_freelist, sf, free_entry);
		if (sf_buf_alloc_want > 0)
			wakeup_one(&sf_buf_freelist);
	}
}

