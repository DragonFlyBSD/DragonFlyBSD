/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ARM64 Bus DMA implementation - stub for compile-only.
 * This needs proper implementation for working drivers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/uio.h>
#include <sys/bus_dma.h>
#include <sys/kernel.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <bus/cam/cam.h>
#include <bus/cam/cam_ccb.h>

/*
 * Stub bus_dma_tag structure.
 */
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
};

/*
 * Stub bus_dmamap structure.
 */
struct bus_dmamap {
	bus_dma_tag_t	dmat;
	void		*buf;
	bus_size_t	buflen;
	int		flags;
};

int
bus_dma_tag_create(bus_dma_tag_t parent __unused, bus_size_t alignment,
    bus_size_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_size_t maxsize, int nsegments, bus_size_t maxsegsz,
    int flags, bus_dma_tag_t *dmat)
{
	bus_dma_tag_t newtag;

	newtag = kmalloc(sizeof(*newtag), M_DEVBUF, M_INTWAIT | M_ZERO);
	newtag->alignment = alignment;
	newtag->boundary = boundary;
	newtag->lowaddr = lowaddr;
	newtag->highaddr = highaddr;
	newtag->maxsize = maxsize;
	newtag->nsegments = nsegments;
	newtag->maxsegsz = maxsegsz;
	newtag->flags = flags;
	newtag->map_count = 0;

	*dmat = newtag;
	return (0);
}

int
bus_dma_tag_destroy(bus_dma_tag_t dmat)
{
	if (dmat == NULL)
		return (0);

	if (dmat->map_count != 0)
		return (EBUSY);

	if (dmat->segments != NULL)
		kfree(dmat->segments, M_DEVBUF);
	kfree(dmat, M_DEVBUF);
	return (0);
}

bus_size_t
bus_dma_tag_getmaxsize(bus_dma_tag_t dmat)
{
	if (dmat == NULL)
		return (0);
	return (dmat->maxsize);
}

int
bus_dmamap_create(bus_dma_tag_t dmat, int flags __unused, bus_dmamap_t *mapp)
{
	bus_dmamap_t newmap;

	if (dmat == NULL) {
		*mapp = NULL;
		return (EINVAL);
	}

	newmap = kmalloc(sizeof(*newmap), M_DEVBUF, M_INTWAIT | M_ZERO);
	newmap->dmat = dmat;
	dmat->map_count++;

	*mapp = newmap;
	return (0);
}

int
bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map)
{
	if (map == NULL)
		return (0);

	if (dmat != NULL)
		dmat->map_count--;

	kfree(map, M_DEVBUF);
	return (0);
}

int
bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags,
    bus_dmamap_t *mapp)
{
	int mflags;

	if (dmat == NULL) {
		*vaddr = NULL;
		*mapp = NULL;
		return (EINVAL);
	}

	mflags = M_INTWAIT;
	if (flags & BUS_DMA_ZERO)
		mflags |= M_ZERO;
	if (flags & BUS_DMA_NOWAIT)
		mflags = (mflags & ~M_INTWAIT) | M_NOWAIT;

	*vaddr = kmalloc(dmat->maxsize, M_DEVBUF, mflags);
	if (*vaddr == NULL) {
		*mapp = NULL;
		return (ENOMEM);
	}

	return bus_dmamap_create(dmat, flags, mapp);
}

void
bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map)
{
	if (vaddr != NULL)
		kfree(vaddr, M_DEVBUF);
	bus_dmamap_destroy(dmat, map);
}

int
bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags __unused)
{
	bus_dma_segment_t seg;

	if (dmat == NULL || map == NULL) {
		if (callback != NULL)
			(*callback)(callback_arg, NULL, 0, EINVAL);
		return (EINVAL);
	}

	map->buf = buf;
	map->buflen = buflen;

	/* Simple identity mapping - physical == virtual for stub */
	seg.ds_addr = (bus_addr_t)(uintptr_t)buf;
	seg.ds_len = buflen;

	if (callback != NULL)
		(*callback)(callback_arg, &seg, 1, 0);

	return (0);
}

int
bus_dmamap_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct mbuf *mbuf, bus_dmamap_callback2_t *callback,
    void *callback_arg, int flags __unused)
{
	bus_dma_segment_t seg;

	if (dmat == NULL || map == NULL || mbuf == NULL) {
		if (callback != NULL)
			(*callback)(callback_arg, NULL, 0, 0, EINVAL);
		return (EINVAL);
	}

	/* Simple stub - just use first mbuf data */
	seg.ds_addr = (bus_addr_t)(uintptr_t)mtod(mbuf, void *);
	seg.ds_len = mbuf->m_len;

	if (callback != NULL)
		(*callback)(callback_arg, &seg, 1, mbuf->m_len, 0);

	return (0);
}

int
bus_dmamap_load_mbuf_segment(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct mbuf *mbuf, bus_dma_segment_t *segs, int maxsegs,
    int *nsegs, int flags __unused)
{
	if (dmat == NULL || map == NULL || mbuf == NULL ||
	    segs == NULL || nsegs == NULL || maxsegs < 1) {
		if (nsegs != NULL)
			*nsegs = 0;
		return (EINVAL);
	}

	/* Simple stub - just use first mbuf data */
	segs[0].ds_addr = (bus_addr_t)(uintptr_t)mtod(mbuf, void *);
	segs[0].ds_len = mbuf->m_len;
	*nsegs = 1;

	return (0);
}

int
bus_dmamap_load_mbuf_defrag(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct mbuf **mbuf, bus_dma_segment_t *segs, int maxsegs,
    int *nsegs, int flags)
{
	if (mbuf == NULL || *mbuf == NULL)
		return (EINVAL);

	return bus_dmamap_load_mbuf_segment(dmat, map, *mbuf, segs,
	    maxsegs, nsegs, flags);
}

int
bus_dmamap_load_uio(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct uio *uio, bus_dmamap_callback2_t *callback,
    void *callback_arg, int flags __unused)
{
	bus_dma_segment_t seg;

	if (dmat == NULL || map == NULL || uio == NULL) {
		if (callback != NULL)
			(*callback)(callback_arg, NULL, 0, 0, EINVAL);
		return (EINVAL);
	}

	/* Simple stub - use first iovec */
	if (uio->uio_iovcnt > 0) {
		seg.ds_addr = (bus_addr_t)(uintptr_t)uio->uio_iov[0].iov_base;
		seg.ds_len = uio->uio_iov[0].iov_len;
		if (callback != NULL)
			(*callback)(callback_arg, &seg, 1, seg.ds_len, 0);
	} else {
		if (callback != NULL)
			(*callback)(callback_arg, NULL, 0, 0, 0);
	}

	return (0);
}

int
bus_dmamap_load_ccb(bus_dma_tag_t dmat, bus_dmamap_t map,
    union ccb *ccb, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags __unused)
{
	if (dmat == NULL || map == NULL || ccb == NULL) {
		if (callback != NULL)
			(*callback)(callback_arg, NULL, 0, EINVAL);
		return (EINVAL);
	}

	/* CCB loading requires CAM infrastructure - stub returns error */
	if (callback != NULL)
		(*callback)(callback_arg, NULL, 0, EOPNOTSUPP);

	return (EOPNOTSUPP);
}

void
_bus_dmamap_sync(bus_dma_tag_t dmat __unused, bus_dmamap_t map __unused,
    bus_dmasync_op_t op __unused)
{
	/* ARM64 cache coherency - stub for now */
	/* Real implementation needs DSB/DMB barriers */
}

void
_bus_dmamap_unload(bus_dma_tag_t dmat __unused, bus_dmamap_t map)
{
	if (map != NULL) {
		map->buf = NULL;
		map->buflen = 0;
	}
}

int
bus_dmamem_coherent(bus_dma_tag_t parent, bus_size_t alignment,
    bus_size_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_size_t maxsize, int flags, bus_dmamem_t *dmem)
{
	int error;

	if (dmem == NULL)
		return (EINVAL);

	error = bus_dma_tag_create(parent, alignment, boundary,
	    lowaddr, highaddr, maxsize, 1, maxsize, flags, &dmem->dmem_tag);
	if (error)
		return (error);

	error = bus_dmamem_alloc(dmem->dmem_tag, &dmem->dmem_addr,
	    flags | BUS_DMA_COHERENT, &dmem->dmem_map);
	if (error) {
		bus_dma_tag_destroy(dmem->dmem_tag);
		dmem->dmem_tag = NULL;
		return (error);
	}

	/* Identity mapping for stub */
	dmem->dmem_busaddr = (bus_addr_t)(uintptr_t)dmem->dmem_addr;

	return (0);
}

void *
bus_dmamem_coherent_any(bus_dma_tag_t parent, bus_size_t alignment,
    bus_size_t maxsize, int flags, bus_dma_tag_t *dtag,
    bus_dmamap_t *dmap, bus_addr_t *busaddr)
{
	bus_dmamem_t dmem;
	int error;

	error = bus_dmamem_coherent(parent, alignment, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, maxsize, flags, &dmem);
	if (error)
		return (NULL);

	if (dtag != NULL)
		*dtag = dmem.dmem_tag;
	if (dmap != NULL)
		*dmap = dmem.dmem_map;
	if (busaddr != NULL)
		*busaddr = dmem.dmem_busaddr;

	return (dmem.dmem_addr);
}
