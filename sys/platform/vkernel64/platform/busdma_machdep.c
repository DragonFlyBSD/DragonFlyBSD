/*
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/busdma_machdep.c,v 1.94 2008/08/15 20:51:31 kmacy Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/uio.h>
#include <sys/thread2.h>
#include <sys/bus_dma.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/spinlock2.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

/* XXX needed for to access pmap to convert per-proc virtual to physical */
#include <sys/proc.h>
#include <vm/vm_map.h>

#include <machine/md_var.h>

#define MAX_BPAGES	1024

struct bounce_zone;
struct bus_dmamap;

struct bus_dma_tag {
	bus_size_t	alignment;
	bus_size_t	boundary;
	bus_addr_t	lowaddr;
	bus_addr_t	highaddr;
	bus_size_t	maxsize;
	u_int		nsegments;
	bus_size_t	maxsegsz;
	int		flags;
	int		map_count;
	bus_dma_segment_t *segments;
	struct bounce_zone *bounce_zone;
};

/*
 * bus_dma_tag private flags
 */
#define BUS_DMA_BOUNCE_ALIGN	BUS_DMA_BUS2
#define BUS_DMA_BOUNCE_LOWADDR	BUS_DMA_BUS3
#define BUS_DMA_MIN_ALLOC_COMP	BUS_DMA_BUS4

#define BUS_DMA_COULD_BOUNCE	(BUS_DMA_BOUNCE_LOWADDR | BUS_DMA_BOUNCE_ALIGN)

#define BUS_DMAMEM_KMALLOC(dmat) \
	((dmat)->maxsize <= PAGE_SIZE && \
	 (dmat)->alignment <= PAGE_SIZE && \
	 (dmat)->lowaddr >= ptoa(Maxmem))

struct bounce_page {
	vm_offset_t	vaddr;		/* kva of bounce buffer */
	bus_addr_t	busaddr;	/* Physical address */
	vm_offset_t	datavaddr;	/* kva of client data */
	bus_size_t	datacount;	/* client data count */
	STAILQ_ENTRY(bounce_page) links;
};

struct bounce_zone {
	STAILQ_ENTRY(bounce_zone) links;
	STAILQ_HEAD(bp_list, bounce_page) bounce_page_list;
	STAILQ_HEAD(, bus_dmamap) bounce_map_waitinglist;
	struct spinlock	spin;
	int		total_bpages;
	int		free_bpages;
	int		reserved_bpages;
	int		active_bpages;
	int		total_bounced;
	int		total_deferred;
	int		reserve_failed;
	bus_size_t	alignment;
	bus_addr_t	lowaddr;
	char		zoneid[8];
	char		lowaddrid[20];
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
};

#define BZ_LOCK(bz)	spin_lock(&(bz)->spin)
#define BZ_UNLOCK(bz)	spin_unlock(&(bz)->spin)

static struct lwkt_token bounce_zone_tok =
	LWKT_TOKEN_INITIALIZER(bounce_zone_token);
static int busdma_zonecount;
static STAILQ_HEAD(, bounce_zone) bounce_zone_list =
	STAILQ_HEAD_INITIALIZER(bounce_zone_list);

int busdma_swi_pending;
static int total_bounce_pages;
static int max_bounce_pages = MAX_BPAGES;
static int bounce_alignment = 1; /* XXX temporary */

TUNABLE_INT("hw.busdma.max_bpages", &max_bounce_pages);
TUNABLE_INT("hw.busdma.bounce_alignment", &bounce_alignment);

struct bus_dmamap {
	struct bp_list	bpages;
	int		pagesneeded;
	int		pagesreserved;
	bus_dma_tag_t	dmat;
	void		*buf;		/* unmapped buffer pointer */
	bus_size_t	buflen;		/* unmapped buffer length */
	bus_dmamap_callback_t *callback;
	void		*callback_arg;
	STAILQ_ENTRY(bus_dmamap) links;
};

static STAILQ_HEAD(, bus_dmamap) bounce_map_callbacklist =
	STAILQ_HEAD_INITIALIZER(bounce_map_callbacklist);

static struct bus_dmamap nobounce_dmamap;

static int		alloc_bounce_zone(bus_dma_tag_t);
static int		alloc_bounce_pages(bus_dma_tag_t, u_int, int);
static int		reserve_bounce_pages(bus_dma_tag_t, bus_dmamap_t, int);
static void		return_bounce_pages(bus_dma_tag_t, bus_dmamap_t);
static bus_addr_t	add_bounce_page(bus_dma_tag_t, bus_dmamap_t,
			    vm_offset_t, bus_size_t);
static void		free_bounce_page(bus_dma_tag_t, struct bounce_page *);

static bus_dmamap_t	get_map_waiting(bus_dma_tag_t);
static void		add_map_callback(bus_dmamap_t);

SYSCTL_NODE(_hw, OID_AUTO, busdma, CTLFLAG_RD, 0, "Busdma parameters");
SYSCTL_INT(_hw_busdma, OID_AUTO, total_bpages, CTLFLAG_RD, &total_bounce_pages,
	   0, "Total bounce pages");
SYSCTL_INT(_hw_busdma, OID_AUTO, max_bpages, CTLFLAG_RD, &max_bounce_pages,
	   0, "Max bounce pages per bounce zone");
SYSCTL_INT(_hw_busdma, OID_AUTO, bounce_alignment, CTLFLAG_RD,
	   &bounce_alignment, 0, "Obey alignment constraint");


/*
 * Returns true if the address falls within the tag's exclusion window, or
 * fails to meet its alignment requirements.
 */
static __inline int
addr_needs_bounce(bus_dma_tag_t dmat, bus_addr_t paddr)
{
    if ((paddr > dmat->lowaddr && paddr <= dmat->highaddr) ||
         (bounce_alignment && (paddr & (dmat->alignment - 1)) != 0))
        return (1);

    return (0);
}

/*
 * Allocate a device specific dma_tag.
 */
int
bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
		   bus_size_t boundary, bus_addr_t lowaddr,
		   bus_addr_t highaddr, bus_size_t maxsize, int nsegments,
		   bus_size_t maxsegsz, int flags, bus_dma_tag_t *dmat)
{
	bus_dma_tag_t newtag;
	int error = 0;

	/*
	 * Sanity checks
	 */

	if (alignment == 0)
		alignment = 1;
	if (alignment & (alignment - 1))
		panic("alignment must be power of 2");

	if (boundary != 0) {
		if (boundary & (boundary - 1))
			panic("boundary must be power of 2");
		if (boundary < maxsegsz) {
			kprintf("boundary < maxsegsz:\n");
			print_backtrace(-1);
			maxsegsz = boundary;
		}
	}

	/* Return a NULL tag on failure */
	*dmat = NULL;

	newtag = kmalloc(sizeof(*newtag), M_DEVBUF, M_INTWAIT);

	newtag->alignment = alignment;
	newtag->boundary = boundary;
	newtag->lowaddr = trunc_page((vm_paddr_t)lowaddr) + (PAGE_SIZE - 1);
	newtag->highaddr = trunc_page((vm_paddr_t)highaddr) + (PAGE_SIZE - 1);
	newtag->maxsize = maxsize;
	newtag->nsegments = nsegments;
	newtag->maxsegsz = maxsegsz;
	newtag->flags = flags;
	newtag->map_count = 0;
	newtag->segments = NULL;
	newtag->bounce_zone = NULL;

	/* Take into account any restrictions imposed by our parent tag */
	if (parent != NULL) {
		newtag->lowaddr = MIN(parent->lowaddr, newtag->lowaddr);
		newtag->highaddr = MAX(parent->highaddr, newtag->highaddr);

		if (newtag->boundary == 0) {
			newtag->boundary = parent->boundary;
		} else if (parent->boundary != 0) {
			newtag->boundary = MIN(parent->boundary,
					       newtag->boundary);
		}

#ifdef notyet
		newtag->alignment = MAX(parent->alignment, newtag->alignment);
#endif

	}

	if (newtag->lowaddr < ptoa(Maxmem))
		newtag->flags |= BUS_DMA_BOUNCE_LOWADDR;
	if (bounce_alignment && newtag->alignment > 1 &&
	    !(newtag->flags & BUS_DMA_ALIGNED))
		newtag->flags |= BUS_DMA_BOUNCE_ALIGN;

	if ((newtag->flags & BUS_DMA_COULD_BOUNCE) &&
	    (flags & BUS_DMA_ALLOCNOW) != 0) {
		struct bounce_zone *bz;

		/* Must bounce */

		error = alloc_bounce_zone(newtag);
		if (error)
			goto back;
		bz = newtag->bounce_zone;

		if (ptoa(bz->total_bpages) < maxsize) {
			int pages;

			if (flags & BUS_DMA_ONEBPAGE) {
				pages = 1;
			} else {
				pages = atop(round_page(maxsize)) -
					bz->total_bpages;
				pages = MAX(pages, 1);
			}

			/* Add pages to our bounce pool */
			if (alloc_bounce_pages(newtag, pages, flags) < pages)
				error = ENOMEM;

			/* Performed initial allocation */
			newtag->flags |= BUS_DMA_MIN_ALLOC_COMP;
		}
	}
back:
	if (error)
		kfree(newtag, M_DEVBUF);
	else
		*dmat = newtag;
	return error;
}

int
bus_dma_tag_destroy(bus_dma_tag_t dmat)
{
	if (dmat != NULL) {
		if (dmat->map_count != 0)
			return (EBUSY);

        if (dmat->segments != NULL)
            kfree(dmat->segments, M_DEVBUF);
        kfree(dmat, M_DEVBUF);
	}
	return (0);
}

/*
 * Allocate a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int
bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp)
{
	int error;

	error = 0;

	if (dmat->segments == NULL) {
		KKASSERT(dmat->nsegments && dmat->nsegments < 16384);
		dmat->segments = kmalloc(sizeof(bus_dma_segment_t) *
					dmat->nsegments, M_DEVBUF, M_INTWAIT);
	}

	if (dmat->flags & BUS_DMA_COULD_BOUNCE) {
		struct bounce_zone *bz;
		int maxpages;

		/* Must bounce */

		if (dmat->bounce_zone == NULL) {
			error = alloc_bounce_zone(dmat);
			if (error)
				return error;
		}
		bz = dmat->bounce_zone;

		*mapp = kmalloc(sizeof(**mapp), M_DEVBUF, M_INTWAIT | M_ZERO);

		/* Initialize the new map */
		STAILQ_INIT(&((*mapp)->bpages));

		/*
		 * Attempt to add pages to our pool on a per-instance
		 * basis up to a sane limit.
		 */
		if (dmat->flags & BUS_DMA_BOUNCE_ALIGN) {
			maxpages = max_bounce_pages;
		} else {
			maxpages = MIN(max_bounce_pages,
				       Maxmem - atop(dmat->lowaddr));
		}
		if ((dmat->flags & BUS_DMA_MIN_ALLOC_COMP) == 0
		 || (dmat->map_count > 0
		  && bz->total_bpages < maxpages)) {
			int pages;

			if (flags & BUS_DMA_ONEBPAGE) {
				pages = 1;
			} else {
				pages = atop(round_page(dmat->maxsize));
				pages = MIN(maxpages - bz->total_bpages, pages);
				pages = MAX(pages, 1);
			}
			if (alloc_bounce_pages(dmat, pages, flags) < pages)
				error = ENOMEM;

			if ((dmat->flags & BUS_DMA_MIN_ALLOC_COMP) == 0) {
				if (!error)
					dmat->flags |= BUS_DMA_MIN_ALLOC_COMP;
			} else {
				error = 0;
			}
		}
	} else {
		*mapp = NULL;
	}
	if (!error)
		dmat->map_count++;
	return error;
}

/*
 * Destroy a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int
bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map)
{
	if (map != NULL) {
		if (STAILQ_FIRST(&map->bpages) != NULL)
			return (EBUSY);
		kfree(map, M_DEVBUF);
	}
	dmat->map_count--;
	return (0);
}

static __inline bus_size_t
check_kmalloc(bus_dma_tag_t dmat, const void *vaddr0, int verify)
{
	bus_size_t maxsize = 0;
	uintptr_t vaddr = (uintptr_t)vaddr0;

	if ((vaddr ^ (vaddr + dmat->maxsize - 1)) & ~PAGE_MASK) {
		kprintf("boundary check failed\n");
		if (verify)
			print_backtrace(-1); /* XXX panic */
		maxsize = dmat->maxsize;
	}
	if (vaddr & (dmat->alignment - 1)) {
		kprintf("alignment check failed\n");
		if (verify)
			print_backtrace(-1); /* XXX panic */
		if (dmat->maxsize < dmat->alignment)
			maxsize = dmat->alignment;
		else
			maxsize = dmat->maxsize;
	}
	return maxsize;
}

/*
 * Allocate a piece of memory that can be efficiently mapped into
 * bus device space based on the constraints lited in the dma tag.
 *
 * mapp is degenerate.  By definition this allocation should not require
 * bounce buffers so do not allocate a dma map.
 */
int
bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags,
		 bus_dmamap_t *mapp)
{
	int mflags;

	/* If we succeed, no mapping/bouncing will be required */
	*mapp = NULL;

	if (dmat->segments == NULL) {
		KKASSERT(dmat->nsegments < 16384);
		dmat->segments = kmalloc(sizeof(bus_dma_segment_t) *
					dmat->nsegments, M_DEVBUF, M_INTWAIT);
	}

	if (flags & BUS_DMA_NOWAIT)
		mflags = M_NOWAIT;
	else
		mflags = M_WAITOK;
	if (flags & BUS_DMA_ZERO)
		mflags |= M_ZERO;

	if (BUS_DMAMEM_KMALLOC(dmat)) {
		bus_size_t maxsize;

		*vaddr = kmalloc(dmat->maxsize, M_DEVBUF, mflags);

		/*
		 * XXX
		 * Check whether the allocation
		 * - crossed a page boundary
		 * - was not aligned
		 * Retry with power-of-2 alignment in the above cases.
		 */
		maxsize = check_kmalloc(dmat, *vaddr, 0);
		if (maxsize) {
			size_t size;

			kfree(*vaddr, M_DEVBUF);
			/* XXX check for overflow? */
			for (size = 1; size <= maxsize; size <<= 1)
				;
			*vaddr = kmalloc(size, M_DEVBUF, mflags);
			check_kmalloc(dmat, *vaddr, 1);
		}
	} else {
		/*
		 * XXX Use Contigmalloc until it is merged into this facility
		 *     and handles multi-seg allocations.  Nobody is doing
		 *     multi-seg allocations yet though.
		 */
		*vaddr = contigmalloc(dmat->maxsize, M_DEVBUF, mflags,
		    0ul, dmat->lowaddr, dmat->alignment, dmat->boundary);
	}
	if (*vaddr == NULL)
		return (ENOMEM);
	return (0);
}

/*
 * Free a piece of memory and it's allociated dmamap, that was allocated
 * via bus_dmamem_alloc.  Make the same choice for free/contigfree.
 */
void
bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map)
{
	/*
	 * dmamem does not need to be bounced, so the map should be
	 * NULL
	 */
	if (map != NULL)
		panic("bus_dmamem_free: Invalid map freed");
	if (BUS_DMAMEM_KMALLOC(dmat))
		kfree(vaddr, M_DEVBUF);
	else
		contigfree(vaddr, dmat->maxsize, M_DEVBUF);
}

static __inline vm_paddr_t
_bus_dma_extract(pmap_t pmap, vm_offset_t vaddr)
{
	if (pmap)
		return pmap_extract(pmap, vaddr, NULL);
	else
		return pmap_kextract(vaddr);
}

/*
 * Utility function to load a linear buffer.  lastaddrp holds state
 * between invocations (for multiple-buffer loads).  segp contains
 * the segment following the starting one on entrace, and the ending
 * segment on exit.  first indicates if this is the first invocation
 * of this function.
 */
static int
_bus_dmamap_load_buffer(bus_dma_tag_t dmat,
			bus_dmamap_t map,
			void *buf, bus_size_t buflen,
			bus_dma_segment_t *segments,
			int nsegments,
			pmap_t pmap,
			int flags,
			vm_paddr_t *lastpaddrp,
			int *segp,
			int first)
{
	vm_offset_t vaddr;
	vm_paddr_t paddr, nextpaddr;
	bus_dma_segment_t *sg;
	bus_addr_t bmask;
	int seg, error = 0;

	if (map == NULL)
		map = &nobounce_dmamap;

#ifdef INVARIANTS
	if (dmat->flags & BUS_DMA_ALIGNED)
		KKASSERT(((uintptr_t)buf & (dmat->alignment - 1)) == 0);
#endif

	/*
	 * If we are being called during a callback, pagesneeded will
	 * be non-zero, so we can avoid doing the work twice.
	 */
	if ((dmat->flags & BUS_DMA_COULD_BOUNCE) &&
	    map != &nobounce_dmamap && map->pagesneeded == 0) {
		vm_offset_t vendaddr;

		/*
		 * Count the number of bounce pages
		 * needed in order to complete this transfer
		 */
		vaddr = (vm_offset_t)buf;
		vendaddr = (vm_offset_t)buf + buflen;

		while (vaddr < vendaddr) {
			paddr = _bus_dma_extract(pmap, vaddr);
			if (addr_needs_bounce(dmat, paddr))
				map->pagesneeded++;
			vaddr += (PAGE_SIZE - (vaddr & PAGE_MASK));
		}
	}

	/* Reserve Necessary Bounce Pages */
	if (map->pagesneeded != 0) {
		struct bounce_zone *bz;

		bz = dmat->bounce_zone;
		BZ_LOCK(bz);
		if (flags & BUS_DMA_NOWAIT) {
			if (reserve_bounce_pages(dmat, map, 0) != 0) {
				BZ_UNLOCK(bz);
				error = ENOMEM;
				goto free_bounce;
			}
		} else {
			if (reserve_bounce_pages(dmat, map, 1) != 0) {
				/* Queue us for resources */
				map->dmat = dmat;
				map->buf = buf;
				map->buflen = buflen;

				STAILQ_INSERT_TAIL(
				    &dmat->bounce_zone->bounce_map_waitinglist,
				    map, links);
				BZ_UNLOCK(bz);

				return (EINPROGRESS);
			}
		}
		BZ_UNLOCK(bz);
	}

	KKASSERT(*segp >= 1 && *segp <= nsegments);
	seg = *segp;
	sg = &segments[seg - 1];

	vaddr = (vm_offset_t)buf;
	nextpaddr = *lastpaddrp;
	bmask = ~(dmat->boundary - 1);	/* note: will be 0 if boundary is 0 */

	/* force at least one segment */
	do {
		bus_size_t size;

		/*
		 * Per-page main loop
		 */
		paddr = _bus_dma_extract(pmap, vaddr);
		size = PAGE_SIZE - (paddr & PAGE_MASK);
		if (size > buflen)
			size = buflen;
		if (map->pagesneeded != 0 && addr_needs_bounce(dmat, paddr)) {
			/*
			 * note: this paddr has the same in-page offset
			 * as vaddr and thus the paddr above, so the
			 * size does not have to be recalculated
			 */
			paddr = add_bounce_page(dmat, map, vaddr, size);
		}

		/*
		 * Fill in the bus_dma_segment
		 */
		if (first) {
			sg->ds_addr = paddr;
			sg->ds_len = size;
			first = 0;
		} else if (paddr == nextpaddr) {
			sg->ds_len += size;
		} else {
			sg++;
			seg++;
			if (seg > nsegments)
				break;
			sg->ds_addr = paddr;
			sg->ds_len = size;
		}
		nextpaddr = paddr + size;

		/*
		 * Handle maxsegsz and boundary issues with a nested loop
		 */
		for (;;) {
			bus_size_t tmpsize;

			/*
			 * Limit to the boundary and maximum segment size
			 */
			if (((nextpaddr - 1) ^ sg->ds_addr) & bmask) {
				tmpsize = dmat->boundary -
					  (sg->ds_addr & ~bmask);
				if (tmpsize > dmat->maxsegsz)
					tmpsize = dmat->maxsegsz;
				KKASSERT(tmpsize < sg->ds_len);
			} else if (sg->ds_len > dmat->maxsegsz) {
				tmpsize = dmat->maxsegsz;
			} else {
				break;
			}

			/*
			 * Futz, split the data into a new segment.
			 */
			if (seg >= nsegments)
				goto fail;
			sg[1].ds_len = sg[0].ds_len - tmpsize;
			sg[1].ds_addr = sg[0].ds_addr + tmpsize;
			sg[0].ds_len = tmpsize;
			sg++;
			seg++;
		}

		/*
		 * Adjust for loop
		 */
		buflen -= size;
		vaddr += size;
	} while (buflen > 0);
fail:
	if (buflen != 0)
		error = EFBIG;

	*segp = seg;
	*lastpaddrp = nextpaddr;

free_bounce:
	if (error && (dmat->flags & BUS_DMA_COULD_BOUNCE) &&
	    map != &nobounce_dmamap) {
		_bus_dmamap_unload(dmat, map);
		return_bounce_pages(dmat, map);
	}
	return error;
}

/*
 * Map the buffer buf into bus space using the dmamap map.
 */
int
bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
		bus_size_t buflen, bus_dmamap_callback_t *callback,
		void *callback_arg, int flags)
{
	vm_paddr_t lastaddr = 0;
	int error, nsegs = 1;

	if (map != NULL) {
		/*
		 * XXX
		 * Follow old semantics.  Once all of the callers are fixed,
		 * we should get rid of these internal flag "adjustment".
		 */
		flags &= ~BUS_DMA_NOWAIT;
		flags |= BUS_DMA_WAITOK;

		map->callback = callback;
		map->callback_arg = callback_arg;
	}

	error = _bus_dmamap_load_buffer(dmat, map, buf, buflen,
			dmat->segments, dmat->nsegments,
			NULL, flags, &lastaddr, &nsegs, 1);
	if (error == EINPROGRESS)
		return error;

	callback(callback_arg, dmat->segments, nsegs, error);
	return 0;
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
bus_dmamap_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map,
		     struct mbuf *m0,
		     bus_dmamap_callback2_t *callback, void *callback_arg,
		     int flags)
{
	int nsegs, error;

	/*
	 * XXX
	 * Follow old semantics.  Once all of the callers are fixed,
	 * we should get rid of these internal flag "adjustment".
	 */
	flags &= ~BUS_DMA_WAITOK;
	flags |= BUS_DMA_NOWAIT;

	error = bus_dmamap_load_mbuf_segment(dmat, map, m0,
			dmat->segments, dmat->nsegments, &nsegs, flags);
	if (error) {
		/* force "no valid mappings" in callback */
		callback(callback_arg, dmat->segments, 0, 0, error);
	} else {
		callback(callback_arg, dmat->segments, nsegs,
			 m0->m_pkthdr.len, error);
	}
	return error;
}

int
bus_dmamap_load_mbuf_segment(bus_dma_tag_t dmat, bus_dmamap_t map,
			     struct mbuf *m0,
			     bus_dma_segment_t *segs, int maxsegs,
			     int *nsegs, int flags)
{
	int error;

	M_ASSERTPKTHDR(m0);

	KASSERT(maxsegs >= 1, ("invalid maxsegs %d", maxsegs));
	KASSERT(maxsegs <= dmat->nsegments,
		("%d too many segments, dmat only supports %d segments",
		 maxsegs, dmat->nsegments));
	KASSERT(flags & BUS_DMA_NOWAIT,
		("only BUS_DMA_NOWAIT is supported"));

	if (m0->m_pkthdr.len <= dmat->maxsize) {
		int first = 1;
		vm_paddr_t lastaddr = 0;
		struct mbuf *m;

		*nsegs = 1;
		error = 0;
		for (m = m0; m != NULL && error == 0; m = m->m_next) {
			if (m->m_len == 0)
				continue;

			error = _bus_dmamap_load_buffer(dmat, map,
					m->m_data, m->m_len,
					segs, maxsegs,
					NULL, flags, &lastaddr,
					nsegs, first);
			if (error == ENOMEM && !first) {
				/*
				 * Out of bounce pages due to too many
				 * fragments in the mbuf chain; return
				 * EFBIG instead.
				 */
				error = EFBIG;
			}
			first = 0;
		}
#ifdef INVARIANTS
		if (!error)
			KKASSERT(*nsegs <= maxsegs && *nsegs >= 1);
#endif
	} else {
		*nsegs = 0;
		error = EINVAL;
	}
	KKASSERT(error != EINPROGRESS);
	return error;
}

/*
 * Like _bus_dmamap_load(), but for uios.
 */
int
bus_dmamap_load_uio(bus_dma_tag_t dmat, bus_dmamap_t map,
		    struct uio *uio,
		    bus_dmamap_callback2_t *callback, void *callback_arg,
		    int flags)
{
	vm_paddr_t lastaddr;
	int nsegs, error, first, i;
	bus_size_t resid;
	struct iovec *iov;
	pmap_t pmap;

	/*
	 * XXX
	 * Follow old semantics.  Once all of the callers are fixed,
	 * we should get rid of these internal flag "adjustment".
	 */
	flags &= ~BUS_DMA_WAITOK;
	flags |= BUS_DMA_NOWAIT;

	resid = (bus_size_t)uio->uio_resid;
	iov = uio->uio_iov;

	if (uio->uio_segflg == UIO_USERSPACE) {
		struct thread *td;

		td = uio->uio_td;
		KASSERT(td != NULL && td->td_proc != NULL,
			("bus_dmamap_load_uio: USERSPACE but no proc"));
		pmap = vmspace_pmap(td->td_proc->p_vmspace);
	} else {
		pmap = NULL;
	}

	error = 0;
	nsegs = 1;
	first = 1;
	lastaddr = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && !error; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		bus_size_t minlen =
			resid < iov[i].iov_len ? resid : iov[i].iov_len;
		caddr_t addr = (caddr_t) iov[i].iov_base;

		error = _bus_dmamap_load_buffer(dmat, map, addr, minlen,
				dmat->segments, dmat->nsegments,
				pmap, flags, &lastaddr, &nsegs, first);
		first = 0;

		resid -= minlen;
	}

	if (error) {
		/* force "no valid mappings" in callback */
		callback(callback_arg, dmat->segments, 0, 0, error);
	} else {
		callback(callback_arg, dmat->segments, nsegs,
			 (bus_size_t)uio->uio_resid, error);
	}
	return error;
}

/*
 * Release the mapping held by map.
 */
void
_bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map)
{
	struct bounce_page *bpage;

	while ((bpage = STAILQ_FIRST(&map->bpages)) != NULL) {
		STAILQ_REMOVE_HEAD(&map->bpages, links);
		free_bounce_page(dmat, bpage);
	}
}

void
_bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op)
{
	struct bounce_page *bpage;

	if ((bpage = STAILQ_FIRST(&map->bpages)) != NULL) {
		/*
		 * Handle data bouncing.  We might also
		 * want to add support for invalidating
		 * the caches on broken hardware
		 */
		switch (op) {
		case BUS_DMASYNC_PREWRITE:
			while (bpage != NULL) {
				bcopy((void *)bpage->datavaddr,
				      (void *)bpage->vaddr,
				      bpage->datacount);
				bpage = STAILQ_NEXT(bpage, links);
			}
			dmat->bounce_zone->total_bounced++;
			break;

		case BUS_DMASYNC_POSTREAD:
			while (bpage != NULL) {
				bcopy((void *)bpage->vaddr,
				      (void *)bpage->datavaddr,
				      bpage->datacount);
				bpage = STAILQ_NEXT(bpage, links);
			}
			dmat->bounce_zone->total_bounced++;
			break;

		case BUS_DMASYNC_PREREAD:
		case BUS_DMASYNC_POSTWRITE:
			/* No-ops */
			break;
		}
	}
}

static int
alloc_bounce_zone(bus_dma_tag_t dmat)
{
	struct bounce_zone *bz, *new_bz;

	KASSERT(dmat->bounce_zone == NULL,
		("bounce zone was already assigned"));

	new_bz = kmalloc(sizeof(*new_bz), M_DEVBUF, M_INTWAIT | M_ZERO);

	lwkt_gettoken(&bounce_zone_tok);

	/* Check to see if we already have a suitable zone */
	STAILQ_FOREACH(bz, &bounce_zone_list, links) {
		if (dmat->alignment <= bz->alignment &&
		    dmat->lowaddr >= bz->lowaddr) {
			lwkt_reltoken(&bounce_zone_tok);

			dmat->bounce_zone = bz;
			kfree(new_bz, M_DEVBUF);
			return 0;
		}
	}
	bz = new_bz;

	spin_init(&bz->spin, "allocbouncezone");
	STAILQ_INIT(&bz->bounce_page_list);
	STAILQ_INIT(&bz->bounce_map_waitinglist);
	bz->free_bpages = 0;
	bz->reserved_bpages = 0;
	bz->active_bpages = 0;
	bz->lowaddr = dmat->lowaddr;
	bz->alignment = round_page(dmat->alignment);
	ksnprintf(bz->zoneid, 8, "zone%d", busdma_zonecount);
	busdma_zonecount++;
	ksnprintf(bz->lowaddrid, 18, "%#jx", (uintmax_t)bz->lowaddr);
	STAILQ_INSERT_TAIL(&bounce_zone_list, bz, links);

	lwkt_reltoken(&bounce_zone_tok);

	dmat->bounce_zone = bz;

	sysctl_ctx_init(&bz->sysctl_ctx);
	bz->sysctl_tree = SYSCTL_ADD_NODE(&bz->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw_busdma), OID_AUTO, bz->zoneid,
	    CTLFLAG_RD, 0, "");
	if (bz->sysctl_tree == NULL) {
		sysctl_ctx_free(&bz->sysctl_ctx);
		return 0;	/* XXX error code? */
	}

	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "total_bpages", CTLFLAG_RD, &bz->total_bpages, 0,
	    "Total bounce pages");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "free_bpages", CTLFLAG_RD, &bz->free_bpages, 0,
	    "Free bounce pages");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "reserved_bpages", CTLFLAG_RD, &bz->reserved_bpages, 0,
	    "Reserved bounce pages");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "active_bpages", CTLFLAG_RD, &bz->active_bpages, 0,
	    "Active bounce pages");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "total_bounced", CTLFLAG_RD, &bz->total_bounced, 0,
	    "Total bounce requests");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "total_deferred", CTLFLAG_RD, &bz->total_deferred, 0,
	    "Total bounce requests that were deferred");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "reserve_failed", CTLFLAG_RD, &bz->reserve_failed, 0,
	    "Total bounce page reservations that were failed");
	SYSCTL_ADD_STRING(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "lowaddr", CTLFLAG_RD, bz->lowaddrid, 0, "");
	SYSCTL_ADD_INT(&bz->sysctl_ctx,
	    SYSCTL_CHILDREN(bz->sysctl_tree), OID_AUTO,
	    "alignment", CTLFLAG_RD, &bz->alignment, 0, "");

	return 0;
}

static int
alloc_bounce_pages(bus_dma_tag_t dmat, u_int numpages, int flags)
{
	struct bounce_zone *bz = dmat->bounce_zone;
	int count = 0, mflags;

	if (flags & BUS_DMA_NOWAIT)
		mflags = M_NOWAIT;
	else
		mflags = M_WAITOK;

	while (numpages > 0) {
		struct bounce_page *bpage;

		bpage = kmalloc(sizeof(*bpage), M_DEVBUF, M_INTWAIT | M_ZERO);

		bpage->vaddr = (vm_offset_t)contigmalloc(PAGE_SIZE, M_DEVBUF,
							 mflags, 0ul,
							 bz->lowaddr,
							 bz->alignment, 0);
		if (bpage->vaddr == 0) {
			kfree(bpage, M_DEVBUF);
			break;
		}
		bpage->busaddr = pmap_kextract(bpage->vaddr);

		BZ_LOCK(bz);
		STAILQ_INSERT_TAIL(&bz->bounce_page_list, bpage, links);
		total_bounce_pages++;
		bz->total_bpages++;
		bz->free_bpages++;
		BZ_UNLOCK(bz);

		count++;
		numpages--;
	}
	return count;
}

/* Assume caller holds bounce zone spinlock */
static int
reserve_bounce_pages(bus_dma_tag_t dmat, bus_dmamap_t map, int commit)
{
	struct bounce_zone *bz = dmat->bounce_zone;
	int pages;

	pages = MIN(bz->free_bpages, map->pagesneeded - map->pagesreserved);
	if (!commit && map->pagesneeded > (map->pagesreserved + pages)) {
		bz->reserve_failed++;
		return (map->pagesneeded - (map->pagesreserved + pages));
	}

	bz->free_bpages -= pages;

	bz->reserved_bpages += pages;
	KKASSERT(bz->reserved_bpages <= bz->total_bpages);

	map->pagesreserved += pages;
	pages = map->pagesneeded - map->pagesreserved;

	return pages;
}

static void
return_bounce_pages(bus_dma_tag_t dmat, bus_dmamap_t map)
{
	struct bounce_zone *bz = dmat->bounce_zone;
	int reserved = map->pagesreserved;
	bus_dmamap_t wait_map;

	map->pagesreserved = 0;
	map->pagesneeded = 0;

	if (reserved == 0)
		return;

	BZ_LOCK(bz);

	bz->free_bpages += reserved;
	KKASSERT(bz->free_bpages <= bz->total_bpages);

	KKASSERT(bz->reserved_bpages >= reserved);
	bz->reserved_bpages -= reserved;

	wait_map = get_map_waiting(dmat);

	BZ_UNLOCK(bz);

	if (wait_map != NULL)
		add_map_callback(map);
}

static bus_addr_t
add_bounce_page(bus_dma_tag_t dmat, bus_dmamap_t map, vm_offset_t vaddr,
		bus_size_t size)
{
	struct bounce_zone *bz = dmat->bounce_zone;
	struct bounce_page *bpage;

	KASSERT(map->pagesneeded > 0, ("map doesn't need any pages"));
	map->pagesneeded--;

	KASSERT(map->pagesreserved > 0, ("map doesn't reserve any pages"));
	map->pagesreserved--;

	BZ_LOCK(bz);

	bpage = STAILQ_FIRST(&bz->bounce_page_list);
	KASSERT(bpage != NULL, ("free page list is empty"));
	STAILQ_REMOVE_HEAD(&bz->bounce_page_list, links);

	KKASSERT(bz->reserved_bpages > 0);
	bz->reserved_bpages--;

	bz->active_bpages++;
	KKASSERT(bz->active_bpages <= bz->total_bpages);

	BZ_UNLOCK(bz);

	bpage->datavaddr = vaddr;
	bpage->datacount = size;
	STAILQ_INSERT_TAIL(&map->bpages, bpage, links);
	return bpage->busaddr;
}

static void
free_bounce_page(bus_dma_tag_t dmat, struct bounce_page *bpage)
{
	struct bounce_zone *bz = dmat->bounce_zone;
	bus_dmamap_t map;

	bpage->datavaddr = 0;
	bpage->datacount = 0;

	BZ_LOCK(bz);

	STAILQ_INSERT_HEAD(&bz->bounce_page_list, bpage, links);

	bz->free_bpages++;
	KKASSERT(bz->free_bpages <= bz->total_bpages);

	KKASSERT(bz->active_bpages > 0);
	bz->active_bpages--;

	map = get_map_waiting(dmat);

	BZ_UNLOCK(bz);

	if (map != NULL)
		add_map_callback(map);
}

/* Assume caller holds bounce zone spinlock */
static bus_dmamap_t
get_map_waiting(bus_dma_tag_t dmat)
{
	struct bounce_zone *bz = dmat->bounce_zone;
	bus_dmamap_t map;

	map = STAILQ_FIRST(&bz->bounce_map_waitinglist);
	if (map != NULL) {
		if (reserve_bounce_pages(map->dmat, map, 1) == 0) {
			STAILQ_REMOVE_HEAD(&bz->bounce_map_waitinglist, links);
			bz->total_deferred++;
		} else {
			map = NULL;
		}
	}
	return map;
}

static void
add_map_callback(bus_dmamap_t map)
{
#ifdef notyet
	/* XXX callbacklist is not MPSAFE */
	crit_enter();
	get_mplock();
	STAILQ_INSERT_TAIL(&bounce_map_callbacklist, map, links);
	busdma_swi_pending = 1;
	setsoftvm();
	rel_mplock();
	crit_exit();
#else
	panic("%s uncoded", __func__);
#endif
}

#ifdef notyet
void
busdma_swi(void)
{
	bus_dmamap_t map;

	crit_enter();
	while ((map = STAILQ_FIRST(&bounce_map_callbacklist)) != NULL) {
		STAILQ_REMOVE_HEAD(&bounce_map_callbacklist, links);
		crit_exit();
		bus_dmamap_load(map->dmat, map, map->buf, map->buflen,
				map->callback, map->callback_arg, /*flags*/0);
		crit_enter();
	}
	crit_exit();
}
#endif
