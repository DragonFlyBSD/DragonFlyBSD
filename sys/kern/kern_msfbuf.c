/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
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
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
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
 * $DragonFly: src/sys/kern/kern_msfbuf.c,v 1.15 2005/07/30 01:12:22 hmp Exp $
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
#include <sys/xio.h>
#include <sys/msfbuf.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

#include <sys/thread2.h>
#include <vm/vm_page2.h>

MALLOC_DEFINE(M_MSFBUF, "MSFBUF", "direct-copy buffers");

/* lists and queues associated with msf_bufs */
LIST_HEAD(msf_buf_list, msf_buf);

TAILQ_HEAD(, msf_buf) msf_buf_freelist;

/* indicate shortage of available msf_bufs */
static u_int msf_buf_alloc_want;

/* base of the msf_buf map */
static vm_offset_t msf_base;
static struct msf_buf *msf_bufs;
static int msf_buf_hits;
static int msf_buf_misses;

static int msf_buf_count = 256; /* magic value */
SYSCTL_INT(_kern_ipc, OID_AUTO, msf_bufs, CTLFLAG_RD, &msf_buf_count,
	0, "number of direct-copy buffers available");
SYSCTL_INT(_kern_ipc, OID_AUTO, msf_hits, CTLFLAG_RD, &msf_buf_hits,
	0, "number of direct-copy buffers available");
SYSCTL_INT(_kern_ipc, OID_AUTO, msf_misses, CTLFLAG_RD, &msf_buf_misses,
	0, "number of direct-copy buffers available");

static void
msf_buf_init(void *__dummy)
{
	struct msf_buf *msf;
	int i;
	
	msf_buf_alloc_want = 0;
	TUNABLE_INT_FETCH("kern.ipc.msfbufs", &msf_buf_count);

	TAILQ_INIT(&msf_buf_freelist);

	msf_base = kmem_alloc_nofault(kernel_map,
					msf_buf_count * XIO_INTERNAL_SIZE);

	msf_bufs = malloc(msf_buf_count * sizeof(struct msf_buf), M_MSFBUF,
			M_WAITOK|M_ZERO);

	/* Initialize the free list with necessary information. */
	for (i = 0; i < msf_buf_count; i++) {
		msf = &msf_bufs[i];
		msf->ms_kva = msf_base + i * XIO_INTERNAL_SIZE;
		msf->ms_flags = MSF_ONFREEQ;
		msf->ms_type = MSF_TYPE_UNKNOWN;
		msf->ms_xio = &msf->ms_internal_xio;
		xio_init(&msf->ms_internal_xio);
		TAILQ_INSERT_TAIL(&msf_buf_freelist, &msf_bufs[i], free_list);
	}
}
SYSINIT(msf_buf, SI_SUB_MBUF, SI_ORDER_ANY, msf_buf_init, NULL);

/*
 * Get an msf_buf from the freelist; if none are available
 * than it will block.
 *
 * If SFB_CATCH was specified in 'flags' than the sleep is
 * block is interruptable by signals etc; this flag is normally
 * use for system calls.
 *
 */
static struct msf_buf *
msf_alloc(vm_page_t firstpage, int flags)
{
	struct msf_buf *msf;
	int pflags;
	int error;

	crit_enter();
	if (firstpage && (msf = firstpage->msf_hint) != NULL &&
		(msf->ms_flags & MSF_ONFREEQ)
	) {
		KKASSERT(msf->ms_refcnt == 0);
		msf->ms_flags &= ~MSF_ONFREEQ;
		msf->ms_refcnt = 1;
		TAILQ_REMOVE(&msf_buf_freelist, msf, free_list);
		--msf_buf_count;
		++msf_buf_hits;
	} else {
		/*
		 * Get a buffer off the freelist.  If the freelist is empty, we
		 * block until something becomes available; this happens quite
		 * quickly anyway because MSFBUFs are supposed to be temporary
		 * mappings.
		 *
		 * If the SFB_CATCH flag was provided, then we allow the sleep
		 * to be interruptible.
		 */
		for (;;) {
			if ((msf = TAILQ_FIRST(&msf_buf_freelist)) != NULL) {
				KKASSERT(msf->ms_refcnt == 0);
				--msf_buf_count;
				TAILQ_REMOVE(&msf_buf_freelist, msf, free_list);
				msf->ms_flags &= ~MSF_ONFREEQ;
				msf->ms_refcnt = 1;
				if (firstpage)
					firstpage->msf_hint = msf;
				break;
			}
			pflags = (flags & SFB_CATCH) ? PCATCH : 0;
			++msf_buf_alloc_want;
			error = tsleep(&msf_buf_freelist, pflags, "msfbuf", 0);
			--msf_buf_alloc_want;
			if (error)
					break;
		}
		++msf_buf_misses;
	}
	crit_exit();
	return (msf);
}

static
void
msf_map_msf(struct msf_buf *msf, int flags)
{
#ifdef SMP
	if (flags & SFB_CPUPRIVATE) {
		pmap_qenter2(msf->ms_kva, msf->ms_xio->xio_pages, 
			    msf->ms_xio->xio_npages, &msf->ms_cpumask);
	} else {
		pmap_qenter(msf->ms_kva, msf->ms_xio->xio_pages,
			    msf->ms_xio->xio_npages);
		msf->ms_cpumask = (cpumask_t)-1;
	}
#else
	pmap_qenter2(msf->ms_kva, msf->ms_xio->xio_pages, 
			msf->ms_xio->xio_npages, &msf->ms_cpumask);
#endif
}

int
msf_map_pagelist(struct msf_buf **msfp, vm_page_t *list, int npages, int flags)
{
	struct msf_buf *msf;
	int i;

	KKASSERT(npages != 0 && npages <= XIO_INTERNAL_PAGES);

	if ((msf = msf_alloc(list[0], flags)) != NULL) {
		KKASSERT(msf->ms_xio == &msf->ms_internal_xio);
		for (i = 0; i < npages; ++i)
			msf->ms_internal_xio.xio_pages[i] = list[i];
		msf->ms_internal_xio.xio_offset = 0;
		msf->ms_internal_xio.xio_npages = npages;
		msf->ms_internal_xio.xio_bytes = npages << PAGE_SHIFT;
		msf->ms_type = MSF_TYPE_PGLIST;
		msf_map_msf(msf, flags);
		*msfp = msf;
		return (0);
	} else {
		*msfp = NULL;
		return (ENOMEM);
	}
}

int
msf_map_xio(struct msf_buf **msfp, struct xio *xio, int flags)
{
	struct msf_buf *msf;

	KKASSERT(xio != NULL && xio->xio_npages > 0);
	KKASSERT(xio->xio_npages <= XIO_INTERNAL_PAGES);

	if ((msf = msf_alloc(xio->xio_pages[0], flags)) != NULL) {
		msf->ms_type = MSF_TYPE_XIO;
		msf->ms_xio = xio;
		msf_map_msf(msf, flags);
		*msfp = msf;
		return(0);
	} else {
		*msfp = NULL;
		return(ENOMEM);
	}
}

int
msf_map_ubuf(struct msf_buf **msfp, void *base, size_t nbytes, int flags)
{
	struct msf_buf *msf;
	vm_paddr_t paddr;
	int error;

	if (((int)(intptr_t)base & PAGE_MASK) + nbytes > XIO_INTERNAL_SIZE) {
		*msfp = NULL;
		return (ERANGE);
	}

	if ((paddr = pmap_kextract((vm_offset_t)base)) != 0)
		msf = msf_alloc(PHYS_TO_VM_PAGE(paddr), flags);
	else
		msf = msf_alloc(NULL, flags);

	if (msf == NULL) {
		error = ENOENT;
	} else {
		error = xio_init_ubuf(&msf->ms_internal_xio, base, nbytes, 0);
		if (error == 0) {
			KKASSERT(msf->ms_xio == &msf->ms_internal_xio);
			msf_map_msf(msf, flags);
			msf->ms_type = MSF_TYPE_UBUF;
		} else {
			msf_buf_free(msf);
			msf = NULL;
		}
	}
	*msfp = msf;
	return (error);
}

int
msf_map_kbuf(struct msf_buf **msfp, void *base, size_t nbytes, int flags)
{
	struct msf_buf *msf;
	vm_paddr_t paddr;
	int error;

	if (((int)(intptr_t)base & PAGE_MASK) + nbytes > XIO_INTERNAL_SIZE) {
		*msfp = NULL;
		return (ERANGE);
	}

	if ((paddr = pmap_kextract((vm_offset_t)base)) != 0)
		msf = msf_alloc(PHYS_TO_VM_PAGE(paddr), flags);
	else
		msf = msf_alloc(NULL, flags);

	if (msf == NULL) {
		error = ENOENT;
	} else {
		error = xio_init_kbuf(&msf->ms_internal_xio, base, nbytes);
		if (error == 0) {
			KKASSERT(msf->ms_xio == &msf->ms_internal_xio);
			msf_map_msf(msf, flags);
			msf->ms_type = MSF_TYPE_KBUF;
		} else {
			msf_buf_free(msf);
			msf = NULL;
		}
	}
	*msfp = msf;
	return (error);
}

/*
 * Iterate through the specified uio calling the function with a kernel buffer
 * containing the data until the uio has been exhausted.  If the uio 
 * represents system space no mapping occurs.  If the uio represents user 
 * space the data is mapped into system space in chunks.  This function does
 * not guarentee any particular alignment or minimum chunk size, it will 
 * depend on the limitations of MSF buffers and the breakdown of the UIO's
 * elements.
 */
int
msf_uio_iterate(struct uio *uio, 
		int (*callback)(void *info, char *buf, int bytes), void *info)
{
	struct msf_buf *msf;
	struct iovec *iov;
	size_t offset;
	size_t bytes;
	size_t pgoff;
	int error;
	int i;

	switch (uio->uio_segflg) {
	case UIO_USERSPACE:
		error = 0;
		for (i = 0; i < uio->uio_iovcnt && error == 0; ++i) {
			iov = &uio->uio_iov[i];
			offset = 0;
			pgoff = (int)(intptr_t)iov->iov_base & PAGE_MASK;
			while (offset < iov->iov_len) {
				bytes = iov->iov_len - offset;
				if (bytes + pgoff > XIO_INTERNAL_SIZE)
					bytes = XIO_INTERNAL_SIZE - pgoff;
				error = msf_map_ubuf(&msf, iov->iov_base + offset, bytes, 0);
				if (error)
					break;
				error = callback(info, msf_buf_kva(msf), bytes);
				msf_buf_free(msf);
				if (error)
					break;
				pgoff = 0;
				offset += bytes;
			}
		}
		break;
	case UIO_SYSSPACE:
		error = 0;
		for (i = 0; i < uio->uio_iovcnt; ++i) {
			iov = &uio->uio_iov[i];
			if (iov->iov_len == 0)
				continue;
			error = callback(info, iov->iov_base, iov->iov_len);
			if (error)
				break;
		}
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

#if 0
/*
 * Add a reference to a buffer (currently unused)
 */
void
msf_buf_ref(struct msf_buf *msf)
{
	if (msf->ms_refcnt == 0)
		panic("msf_buf_ref: referencing a free msf_buf");
	crit_enter();
	++msf->ms_refcnt;
	crit_exit();
}
#endif

/*
 * Lose a reference to an msf_buf. When none left, detach mapped page
 * and release resources back to the system.  Note that the sfbuf's
 * removal from the freelist is delayed, so it may in fact already be
 * on the free list.  This is the optimal (and most likely) scenario.
 */
void
msf_buf_free(struct msf_buf *msf)
{
	KKASSERT(msf->ms_refcnt > 0);

	crit_enter();
	if (--msf->ms_refcnt == 0) {
		KKASSERT((msf->ms_flags & MSF_ONFREEQ) == 0);

		if (msf->ms_type == MSF_TYPE_UBUF || msf->ms_type == MSF_TYPE_KBUF)
			xio_release(msf->ms_xio);

		msf->ms_type = MSF_TYPE_UNKNOWN;
		msf->ms_flags |= MSF_ONFREEQ;
		msf->ms_xio = &msf->ms_internal_xio;
		TAILQ_INSERT_TAIL(&msf_buf_freelist, msf, free_list);
		++msf_buf_count;
		if (msf_buf_alloc_want > 0)
			wakeup_one(&msf_buf_freelist);
	}
	crit_exit();
}

