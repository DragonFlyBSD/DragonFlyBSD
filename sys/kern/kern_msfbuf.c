/*
 * Copyright (c) 2004 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@backplane.com> and Matthew Dillon
 * <dillon@backplane.com>.
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
 * COPYRIGHT HOLDERS, CONTRIBUTORS OR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The MSF_BUF API was augmented from the SFBUF API:
 *	Copyright (c) 1998 David Greenman.  All rights reserved.
 * 	src/sys/kern/kern_sfbuf.c,v 1.7 2004/05/13 19:46:18 dillon
 *
 * $DragonFly: src/sys/kern/kern_msfbuf.c,v 1.4 2004/07/07 18:11:56 hmp Exp $
 */
/*
 * MSFBUFs cache linear multi-page ephermal mappings and operate similar
 * to SFBUFs.  MSFBUFs use XIO's internally to hold the page list and can
 * be considered to be a KVA wrapper around an XIO.
 *
 * Like the SFBUF subsystem, the locking and validation of the page array
 * is the responsibility of the caller.  Also like the SFBUF subsystem,
 * MSFBUFs are SMP-friendly, cache the mappings, and will avoid unnecessary
 * page invalidations when possible.
 *
 * MSFBUFs are primarily designed to be used in subsystems that manipulate
 * XIOs.  The DEV and BUF subsystems are a good example.
 *
 * TODO LIST:
 *	- Implement the FREEQ optimization that exists in the SFBUF code.
 *	- Allow allocation (aka mapping) based on an XIO instead of a pglist.
 *	- Overload XIOs representitive of smaller chunks of memory onto the
 *	  same KVA space to efficiently cache smaller mappings (filesystem
 *	  blocks / buffer cache related).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/globaldata.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sfbuf.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/xio.h>
#include <sys/msfbuf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

MALLOC_DECLARE(M_MSFBUF);
MALLOC_DEFINE(M_MSFBUF, "MSFBUF", "direct-copy buffers");

/* lists and queues associated with msf_bufs */
LIST_HEAD(msf_buf_list, msf_buf);

TAILQ_HEAD(, msf_buf) msf_buf_freelist;

/* hash table for tracking msf_bufs */
static struct msf_buf_list *msf_buf_hashtable;
static u_long msf_buf_hashmask;

/* indicate shortage of available msf_bufs */
static u_int msf_buf_alloc_want;

/* base of the msf_buf map */
static vm_offset_t msf_base;
static struct msf_buf *msf_bufs;

static int num_msf_bufs = 256; /* magic value */
SYSCTL_INT(_kern_ipc, OID_AUTO, msfbufs, CTLFLAG_RD, &num_msf_bufs,
	0, "number of direct-copy buffers available");

static void
msf_buf_init(void *__dummy)
{
	int i;
	
	msf_buf_alloc_want = 0;
	TUNABLE_INT_FETCH("kern.ipc.msfbufs", &num_msf_bufs);

	msf_buf_hashtable = hashinit(num_msf_bufs, M_TEMP, &msf_buf_hashmask);
	TAILQ_INIT(&msf_buf_freelist);

	msf_base = kmem_alloc_nofault(kernel_map,
					num_msf_bufs * XIO_INTERNAL_SIZE);

	/* 
	 * Use contig. memory for the maps, so it is quicker to access
	 * linear maps, and they can also be passed to device
	 * buffers (in the future).
	 */
	msf_bufs = malloc(num_msf_bufs * sizeof(struct msf_buf), M_MSFBUF,
			M_WAITOK|M_ZERO);

	/* Initialize the free list with necessary information. */
	for (i = 0; i < num_msf_bufs; i++) {
		msf_bufs[i].m_kva = msf_base + i * XIO_INTERNAL_SIZE;
		msf_bufs[i].m_flags |= SFBA_ONFREEQ;
		xio_init(&msf_bufs[i].m_xio);
		TAILQ_INSERT_TAIL(&msf_buf_freelist, &msf_bufs[i], free_list);
	}
}
SYSINIT(msf_buf, SI_SUB_MBUF, SI_ORDER_ANY, msf_buf_init, NULL);

/*
 * Hash the base page of an MSF's array of pages.
 */
static __inline
int
msf_buf_hash(vm_page_t base_m)
{
    int hv;

    hv = ((int)base_m / sizeof(vm_page_t)) + ((int)base_m >> 12);
    return(hv & msf_buf_hashmask);
}

/*
 * Get an msf_buf from the freelist; if none are available
 * than it will block.
 *
 * If SFBA_PCATCH was specified in 'flags' than the sleep is
 * block is interruptable by signals etc; this flag is normally
 * use for system calls.
 *
 */
struct msf_buf *
msf_buf_alloc(vm_page_t *pg_ary, int npages, int flags)
{
	struct msf_buf_list *hash_chain;
	struct msf_buf *msf;
	globaldata_t gd;
	int error, pflags;
	int i;

	KKASSERT(npages != 0 && npages <= XIO_INTERNAL_SIZE);

	gd = mycpu;
	crit_enter();
	hash_chain = &msf_buf_hashtable[msf_buf_hash(*pg_ary)];
	LIST_FOREACH(msf, hash_chain, active_list) {
		if (msf->m_xio.xio_npages == npages) {
			for (i = npages - 1; i >= 0; --i) {
				if (msf->m_xio.xio_pages[i] != pg_ary[i])
					break;
			}
			if (i >= 0)
				continue;
			/*
			 * found existing mapping
			 */
			if (msf->m_flags & SFBA_ONFREEQ) {
			    TAILQ_REMOVE(&msf_buf_freelist, msf, free_list);
			    msf->m_flags &= ~SFBA_ONFREEQ;
			}

			goto done;
		}
	}

	/*
	 * Didn't find old mapping.  Get a buffer off the freelist.  We
	 * may have to remove and skip buffers with non-zero ref counts 
	 * that were lazily allocated.
	 *
	 * If the freelist is empty, we block until something becomes
	 * available.  This usually happens pretty quickly because sf_bufs
	 * and msf_bufs are supposed to be temporary mappings.
	 */
	while ((msf = TAILQ_FIRST(&msf_buf_freelist)) == NULL) {
		pflags = (flags & SFBA_PCATCH) ? PCATCH : 0;
		++msf_buf_alloc_want;
		error = tsleep(&msf_buf_freelist, pflags, "msfbuf", 0);
		--msf_buf_alloc_want;
		if (error)
			goto done2;
	}
	
	/*
	 * We are finished when we find an msf_buf with ref. count of
	 * 0.  Theoretically, we do not have to remove it from the
	 * freelist but it's a good idea to do so to preserve LRU
	 * operation for the (1) never seen before case and
	 * (2) accidently recycled due to prior cached uses not removing
	 * the buffer case.
	 */
	KKASSERT(msf->m_flags & SFBA_ONFREEQ);
	TAILQ_REMOVE(&msf_buf_freelist, msf, free_list);
	msf->m_flags &= ~SFBA_ONFREEQ;

	/* Remove previous mapping from hash table and overwrite new one */
	if (msf->m_xio.xio_pages[0] != NULL)
		LIST_REMOVE(msf, active_list);
	 
	LIST_INSERT_HEAD(hash_chain, msf, active_list);

	for (i = 0; i < npages; ++i)
		msf->m_xio.xio_pages[i] = pg_ary[i];

	msf->m_xio.xio_npages = npages;
	msf->m_xio.xio_bytes = npages * PAGE_SIZE;

	/*
	 * Successful MSF setup, bump the ref count and enter the pages.
	 */
done:
	++msf->m_refcnt;
	if ((flags & SFBA_QUICK)) {
		pmap_qenter2(msf->m_kva, msf->m_xio.xio_pages, 
			    msf->m_xio.xio_npages, &msf->m_cpumask);
	} else {
		pmap_qenter(msf->m_kva, msf->m_xio.xio_pages,
			    msf->m_xio.xio_npages);
		msf->m_cpumask = (cpumask_t)-1;
	}
done2:
	crit_exit();
	return (msf);
}

#if 0
/*
 * Add a reference to a buffer (currently unused)
 */
void
msf_buf_ref(struct msf_buf *msf)
{
	if (msf->m_refcnt == 0)
		panic("msf_buf_ref: referencing a free msf_buf");
	crit_enter();
	++msf->m_refcnt;
	crit_exit();
}
#endif

/*
 * Lose a reference to an msf_buf. When none left, detach mapped page
 * and release resources back to the system.  Note that the sfbuf's
 * removal from the freelist is delayed, so it may in fact already be
 * on the free list.  This is the optimal (and most likely) scenario.
 *
 * Must be called at splimp.
 */
void
msf_buf_free(struct msf_buf *msf)
{
	crit_enter();
	KKASSERT(msf->m_refcnt > 0);

	msf->m_refcnt--;
	if (msf->m_refcnt == 0) {
		KKASSERT((msf->m_flags & SFBA_ONFREEQ) == 0);
		TAILQ_INSERT_TAIL(&msf_buf_freelist, msf, free_list);
		msf->m_flags |= SFBA_ONFREEQ;
		if (msf_buf_alloc_want > 0)
			wakeup_one(&msf_buf_freelist);
	}
	crit_exit();
}
