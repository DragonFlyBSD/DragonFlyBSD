/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * $NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $
 * $FreeBSD: src/sys/i386/include/bus_dma.h,v 1.15.2.2 2002/11/21 23:36:01 sam Exp $
 */

#ifndef _SYS_BUS_DMA_H_
#define _SYS_BUS_DMA_H_

/*
 * Include machine-specific bus stuff
 */
#include <machine/bus_dma.h>	/* bus_addr_t */

/*
 * Flags used in various bus DMA methods.
 */
#define	BUS_DMA_WAITOK		0x0000	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x0001	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x0002	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x0004	/* map memory to not require sync */
#define	BUS_DMA_ZERO		0x0008	/* allocate zero'ed memory */
#define	BUS_DMA_BUS1		0x0010	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x0020
#define	BUS_DMA_BUS3		0x0040
#define	BUS_DMA_BUS4		0x0080
#define	BUS_DMA_ONEBPAGE	0x0100	/* allocate one bpage per map at most */
#define	BUS_DMA_ALIGNED		0x0200	/* no bpage should be allocated due to
					 * alignment requirement; all to-be-
					 * loaded memory is properly aligned */
#define BUS_DMA_PRIVBZONE	0x0400	/* need private bounce zone */
#define BUS_DMA_ALLOCALL	0x0800	/* allocate all needed resources */
#define BUS_DMA_PROTECTED	0x1000	/* all busdma functions are already
					 * protected */
#define BUS_DMA_KEEP_PG_OFFSET	0x2000	/* DMA tag hint that the page offset of the
					 * loaded kernel virtual address must be
					 * preserved in the first physical segment
					 * address, when the KVA is loaded into DMA. */
#define BUS_DMA_NOCACHE		0x4000	/* map memory uncached */

/* Forwards needed by prototypes below. */
struct mbuf;
struct uio;
union ccb;

/*
 *	bus_dmasync_op_t
 *
 *	Operations performed by bus_dmamap_sync().
 */
typedef int bus_dmasync_op_t;

#define	BUS_DMASYNC_PREREAD	0x01
#define	BUS_DMASYNC_POSTREAD	0x02
#define	BUS_DMASYNC_PREWRITE	0x04
#define	BUS_DMASYNC_POSTWRITE	0x08

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the characteristics
 *	of how to perform DMA mappings.  This structure encapsultes
 *	information concerning address and alignment restrictions, number
 *	of S/G	segments, amount of data per S/G segment, etc.
 */
typedef struct bus_dma_tag	*bus_dma_tag_t;

/*
 *	bus_dmamap_t
 *
 *	DMA mapping instance information.
 */
typedef struct bus_dmamap	*bus_dmamap_t;

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 */
typedef struct bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */
} bus_dma_segment_t;

typedef struct bus_dmamem {
	bus_dma_tag_t	dmem_tag;
	bus_dmamap_t	dmem_map;
	void		*dmem_addr;
	bus_addr_t	dmem_busaddr;
} bus_dmamem_t;

/*
 * Allocate a device specific dma_tag encapsulating the constraints of
 * the parent tag in addition to other restrictions specified:
 *
 *	alignment:	alignment for segments.
 *	boundary:	Boundary that segments cannot cross.
 *	lowaddr:	Low restricted address that cannot appear in a mapping.
 *	highaddr:	High restricted address that cannot appear in a mapping.
 *	maxsize:	Maximum mapping size supported by this tag.
 *	nsegments:	Number of discontinuities allowed in maps.
 *	maxsegsz:	Maximum size of a segment in the map.
 *	flags:		Bus DMA flags.
 *	dmat:		A pointer to set to a valid dma tag should the return
 *			value of this function indicate success.
 */
/* XXX Should probably allow specification of alignment */
int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
		       bus_size_t boundary, bus_addr_t lowaddr,
		       bus_addr_t highaddr, bus_size_t maxsize, int nsegments,
		       bus_size_t maxsegsz, int flags, bus_dma_tag_t *dmat);

int bus_dma_tag_destroy(bus_dma_tag_t dmat);
bus_size_t bus_dma_tag_getmaxsize(bus_dma_tag_t tag);

/*
 * Allocate a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp);

/*
 * Destroy  a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map);

/*
 * Allocate a piece of memory that can be efficiently mapped into
 * bus device space based on the constraints lited in the dma tag.
 * A dmamap to for use with dmamap_load is also allocated.
 */
int bus_dmamem_alloc(bus_dma_tag_t dmat, void** vaddr, int flags,
		     bus_dmamap_t *mapp);

/*
 * Free a piece of memory and it's allociated dmamap, that was allocated
 * via bus_dmamem_alloc.
 */
void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);

/*
 * A function that processes a successfully loaded dma map or an error
 * from a delayed load map.
 */
typedef void bus_dmamap_callback_t(void *, bus_dma_segment_t *, int, int);

/*
 * Map the buffer buf into bus space using the dmamap map.
 */
int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
		    bus_size_t buflen, bus_dmamap_callback_t *callback,
		    void *callback_arg, int flags);

/*
 * Like bus_dmamap_callback but includes map size in bytes.  This is
 * defined as a separate interface to maintain compatiiblity for users
 * of bus_dmamap_callback_t--at some point these interfaces should be merged.
 */
typedef void bus_dmamap_callback2_t(void *, bus_dma_segment_t *, int, bus_size_t, int);
/*
 * Like bus_dmamap_load but for mbufs.  Note the use of the
 * bus_dmamap_callback2_t interface.
 */
int bus_dmamap_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map,
			 struct mbuf *mbuf,
			 bus_dmamap_callback2_t *callback, void *callback_arg,
			 int flags);
/*
 * Like bus_dmamap_load but for uios.  Note the use of the
 * bus_dmamap_callback2_t interface.
 */
int bus_dmamap_load_uio(bus_dma_tag_t dmat, bus_dmamap_t map,
			struct uio *ui,
			bus_dmamap_callback2_t *callback, void *callback_arg,
			int flags);
/*
 * Like bus_dmamap_load but for ccb.
 */
int bus_dmamap_load_ccb(bus_dma_tag_t dmat, bus_dmamap_t map,
			union ccb *ccb,
			bus_dmamap_callback_t *callback, void *callback_arg,
			int flags);

/*
 * Like bus_dmamap_load_mbuf without callback.
 * Segmentation information are saved in 'segs' and 'nsegs' if
 * the loading is successful.  'maxsegs' must be set by caller
 * and must be at least 1 but less than 'dmat' nsegment.  It
 * indicates the number of elements in 'segs'.  'flags' must
 * have BUS_DMA_NOWAIT turned on.
 */
int
bus_dmamap_load_mbuf_segment(bus_dma_tag_t dmat, bus_dmamap_t map,
			     struct mbuf *mbuf,
			     bus_dma_segment_t *segs, int maxsegs,
			     int *nsegs, int flags);

/*
 * Like bus_dmamap_load_mbuf_segment, but it will call m_defrag()
 * and try reloading if low level code indicates too many fragments
 * in the '*mbuf'; 'mbuf' will be updated under this situation.
 */
int
bus_dmamap_load_mbuf_defrag(bus_dma_tag_t dmat, bus_dmamap_t map,
			    struct mbuf **mbuf,
			    bus_dma_segment_t *segs, int maxsegs,
			    int *nsegs, int flags);

/*
 * Convenient function to create coherent busdma memory
 */
int
bus_dmamem_coherent(bus_dma_tag_t parent,
		    bus_size_t alignment, bus_size_t boundary,
		    bus_addr_t lowaddr, bus_addr_t highaddr,
		    bus_size_t maxsize, int flags,
		    bus_dmamem_t *dmem);

/*
 * Simplified version of bus_dmamem_coherent() with:
 * boundary == 0
 * lowaddr  == BUS_SPACE_MAXADDR
 * highaddr == BUS_SPACE_MAXADDR
 *
 * 'parent' usually should not be NULL, so we could inherit
 * boundary, lowaddr and highaddr from it.
 */
void *
bus_dmamem_coherent_any(bus_dma_tag_t parent, bus_size_t alignment,
			bus_size_t maxsize, int flags,
			bus_dma_tag_t *dtag, bus_dmamap_t *dmap,
			bus_addr_t *busaddr);

/*
 * Perform a syncronization operation on the given map.
 */
void _bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_dmasync_op_t);
#define bus_dmamap_sync(dmat, dmamap, op) 		\
	if ((dmamap) != NULL && (dmamap) != (void *)-1)	\
		_bus_dmamap_sync(dmat, dmamap, op)

/*
 * Release the mapping held by map.
 */
void _bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map);
#define bus_dmamap_unload(dmat, dmamap) 		\
	if ((dmamap) != NULL && (dmamap) != (void *)-1)	\
		_bus_dmamap_unload(dmat, dmamap)

#endif /* _SYS_BUS_DMA_H_ */
