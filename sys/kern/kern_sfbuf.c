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
 * $DragonFly: src/sys/kern/kern_sfbuf.c,v 1.6 2004/05/13 01:34:03 dillon Exp $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sfbuf.h>
#include <sys/globaldata.h>
#include <sys/thread.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <sys/thread2.h>

static void sf_buf_init(void *arg);
SYSINIT(sock_sf, SI_SUB_MBUF, SI_ORDER_ANY, sf_buf_init, NULL)

LIST_HEAD(sf_buf_list, sf_buf);

SYSCTL_DECL(_kern_ipc);
SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufs, CTLFLAG_RD, &nsfbufs, 0,
	"Maximum number of sf_bufs available to the system");

/*
 * A hash table of active sendfile(2) buffers
 */
static struct sf_buf_list *sf_buf_hashtable;
static u_long sf_buf_hashmask;

static TAILQ_HEAD(, sf_buf) sf_buf_freelist;
static u_int sf_buf_alloc_want;

static vm_offset_t sf_base;
static struct sf_buf *sf_bufs;

static int sfbuf_quick = 1;
SYSCTL_INT(_debug, OID_AUTO, sfbuf_quick, CTLFLAG_RW, &sfbuf_quick, 0, "");

static __inline
int
sf_buf_hash(vm_page_t m)
{
    int hv;

    hv = ((int)m / sizeof(vm_page_t)) + ((int)m >> 12);
    return(hv & sf_buf_hashmask);
}

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
		sf_bufs[i].flags |= SFBA_ONFREEQ;
		TAILQ_INSERT_TAIL(&sf_buf_freelist, &sf_bufs[i], free_entry);
	}
}

/*
 * Get an sf_buf from the freelist. Will block if none are available.
 */
struct sf_buf *
sf_buf_alloc(struct vm_page *m, int flags)
{
	struct sf_buf_list *hash_chain;
	struct sf_buf *sf;
	globaldata_t gd;
	int error;
	int pflags;

	gd = mycpu;
	crit_enter();
	hash_chain = &sf_buf_hashtable[sf_buf_hash(m)];
	LIST_FOREACH(sf, hash_chain, list_entry) {
		if (sf->m == m) {
			/*
			 * cache hit
			 *
			 * We must invalidate the TLB entry based on whether
			 * it need only be valid on the local cpu (SFBA_QUICK),
			 * or on all cpus.  This is conditionalized and in
			 * most cases no system-wide invalidation should be
			 * needed.
			 *
			 * Note: we do not remove the entry from the freelist
			 * on the 0->1 transition. 
			 */
			++sf->refcnt;
			if ((flags & SFBA_QUICK) && sfbuf_quick) {
				if ((sf->cpumask & gd->gd_cpumask) == 0) {
					pmap_kenter_sync_quick(sf->kva);
					sf->cpumask |= gd->gd_cpumask;
				}
			} else {
				if (sf->cpumask != (cpumask_t)-1) {
					pmap_kenter_sync(sf->kva);
					sf->cpumask = (cpumask_t)-1;
				}
			}
			goto done;	/* found existing mapping */
		}
	}

	/*
	 * Didn't find old mapping.  Get a buffer off the freelist.  We
	 * may have to remove and skip buffers with non-zero ref counts 
	 * that were lazily allocated.
	 */
	for (;;) {
		if ((sf = TAILQ_FIRST(&sf_buf_freelist)) == NULL) {
			pflags = (flags & SFBA_PCATCH) ? PCATCH : 0;
			++sf_buf_alloc_want;
			error = tsleep(&sf_buf_freelist, pflags, "sfbufa", 0);
			--sf_buf_alloc_want;
			if (error)
				goto done;
		} else {
			/*
			 * We may have to do delayed removals for referenced
			 * sf_buf's here in addition to locating a sf_buf
			 * to reuse.  The sf_bufs must be removed.
			 *
			 * We are finished when we find an sf_buf with a
			 * refcnt of 0.  We theoretically do not have to
			 * remove it from the freelist but it's a good idea
			 * to do so to preserve LRU operation for the
			 * (1) never before seen before case and (2) 
			 * accidently recycled due to prior cached uses not
			 * removing the buffer case.
			 */
			KKASSERT(sf->flags & SFBA_ONFREEQ);
			TAILQ_REMOVE(&sf_buf_freelist, sf, free_entry);
			sf->flags &= ~SFBA_ONFREEQ;
			if (sf->refcnt == 0)
				break;
		}
	}
	if (sf->m != NULL)	/* remove previous mapping from hash table */
		LIST_REMOVE(sf, list_entry);
	LIST_INSERT_HEAD(hash_chain, sf, list_entry);
	sf->refcnt = 1;
	sf->m = m;
	if ((flags & SFBA_QUICK) && sfbuf_quick) {
		pmap_kenter_quick(sf->kva, sf->m->phys_addr);
		sf->cpumask = gd->gd_cpumask;
	} else {
		pmap_kenter(sf->kva, sf->m->phys_addr);
		sf->cpumask = (cpumask_t)-1;
	}
done:
	crit_exit();
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
 * and release resources back to the system.  Note that the sfbuf's
 * removal from the freelist is delayed, so it may in fact already be
 * on the free list.  This is the optimal (and most likely) scenario.
 *
 * Must be called at splimp.
 */
void
sf_buf_free(struct sf_buf *sf)
{
	if (sf->refcnt == 0)
		panic("sf_buf_free: freeing free sf_buf");
	crit_enter();
	sf->refcnt--;
	if (sf->refcnt == 0 && (sf->flags & SFBA_ONFREEQ) == 0) {
		KKASSERT(sf->aux1 == 0 && sf->aux2 == 0);
		TAILQ_INSERT_TAIL(&sf_buf_freelist, sf, free_entry);
		sf->flags |= SFBA_ONFREEQ;
		if (sf_buf_alloc_want > 0)
			wakeup_one(&sf_buf_freelist);
	}
	crit_exit();
}

