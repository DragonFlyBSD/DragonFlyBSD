/*
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * Copyright (c) 2015 Matthew Dillon <dillon@backplane.com>
 * Copyright (c) 2016 Matt Macy <mmacy@nextbsd.org>
 * Copyright (c) 2017-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef	_LINUX_SCATTERLIST_H_
#define	_LINUX_SCATTERLIST_H_

#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <asm/io.h>

/*
 * SG table design.
 *
 * If flags bit 0 is set, then the sg field contains a pointer to the next sg
 * table list. Otherwise the next entry is at sg + 1, can be determined using
 * the sg_is_chain() function.
 *
 * If flags bit 1 is set, then this sg entry is the last element in a list,
 * can be determined using the sg_is_last() function.
 *
 * See sg_next().
 *
 */

struct scatterlist {
	union {
		struct page		*page;
		struct scatterlist	*sg;
	} sl_un;
	unsigned long	offset;
	uint32_t	length;
	dma_addr_t	dma_address;
	uint32_t	flags;
};

struct sg_table {
	struct scatterlist *sgl;        /* the list */
	unsigned int nents;             /* number of mapped entries */
	unsigned int orig_nents;        /* original size of list */
};

struct sg_page_iter {
	struct scatterlist	*sg;
	unsigned int		sg_pgoffset;	/* page index */
	unsigned int		maxents;
	unsigned int		__nents;
	int			__pg_advance;
};


/*
 * Maximum number of entries that will be allocated in one piece, if
 * a list larger than this is required then chaining will be utilized.
 */
#define SG_MAX_SINGLE_ALLOC             (PAGE_SIZE / sizeof(struct scatterlist))

#define	sg_dma_address(sg)	(sg)->dma_address
#define	sg_dma_len(sg)		(sg)->length
#define	sg_page(sg)		(sg)->sl_un.page
#define	sg_scatternext(sg)	(sg)->sl_un.sg

#define	SG_END		0x01
#define	SG_CHAIN	0x02

static inline void
sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len,
    unsigned int offset)
{
	sg_page(sg) = page;
	sg_dma_len(sg) = len;
	sg->offset = offset;
	if (offset > PAGE_SIZE)
		panic("sg_set_page: Invalid offset %d\n", offset);
}

static inline void
sg_init_table(struct scatterlist *sg, unsigned int nents)
{
	bzero(sg, sizeof(*sg) * nents);
	sg[nents - 1].flags = SG_END;
}

static inline struct scatterlist *
sg_next(struct scatterlist *sg)
{
	if (sg->flags & SG_END)
		return (NULL);
	sg++;
	if (sg->flags & SG_CHAIN)
		sg = sg_scatternext(sg);
	return (sg);
}

static inline vm_paddr_t
sg_phys(struct scatterlist *sg)
{
	return ((struct vm_page *)sg_page(sg))->phys_addr + sg->offset;
}

/**
 * sg_chain - Chain two sglists together
 * @prv:        First scatterlist
 * @prv_nents:  Number of entries in prv
 * @sgl:        Second scatterlist
 *
 * Description:
 *   Links @prv@ and @sgl@ together, to form a longer scatterlist.
 *
 **/
static inline void
sg_chain(struct scatterlist *prv, unsigned int prv_nents,
					struct scatterlist *sgl)
{
/*
 * offset and length are unused for chain entry.  Clear them.
 */
	struct scatterlist *sg = &prv[prv_nents - 1];

	sg->offset = 0;
	sg->length = 0;

	/*
	 * Indicate a link pointer, and set the link to the second list.
	 */
	sg->flags = SG_CHAIN;
	sg->sl_un.sg = sgl;
}

/**
 * sg_mark_end - Mark the end of the scatterlist
 * @sg:          SG entryScatterlist
 *
 * Description:
 *   Marks the passed in sg entry as the termination point for the sg
 *   table. A call to sg_next() on this entry will return NULL.
 *
 **/
static inline void sg_mark_end(struct scatterlist *sg)
{
        sg->flags = SG_END;
}

/**
 * __sg_free_table - Free a previously mapped sg table
 * @table:      The sg table header to use
 * @max_ents:   The maximum number of entries per single scatterlist
 *
 *  Description:
 *    Free an sg table previously allocated and setup with
 *    __sg_alloc_table().  The @max_ents value must be identical to
 *    that previously used with __sg_alloc_table().
 *
 **/
void __sg_free_table(struct sg_table *table, unsigned int max_ents);

/**
 * sg_free_table - Free a previously allocated sg table
 * @table:      The mapped sg table header
 *
 **/
static inline void
sg_free_table(struct sg_table *table)
{
	__sg_free_table(table, SG_MAX_SINGLE_ALLOC);
}

int __sg_alloc_table(struct sg_table *table, unsigned int nents,
		unsigned int max_ents, gfp_t gfp_mask);

/**
 * sg_alloc_table - Allocate and initialize an sg table
 * @table:      The sg table header to use
 * @nents:      Number of entries in sg list
 * @gfp_mask:   GFP allocation mask
 *
 *  Description:
 *    Allocate and initialize an sg table. If @nents@ is larger than
 *    SG_MAX_SINGLE_ALLOC a chained sg table will be setup.
 *
 **/

static inline int
sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask)
{
	int ret;

	ret = __sg_alloc_table(table, nents, SG_MAX_SINGLE_ALLOC,
		gfp_mask);
	if (unlikely(ret))
		__sg_free_table(table, SG_MAX_SINGLE_ALLOC);

	return ret;
}

static inline int
sg_nents(struct scatterlist *sg)
{
	int nents;
	for (nents = 0; sg; sg = sg_next(sg))
		nents++;
	return nents;
}

static inline void
__sg_page_iter_start(struct sg_page_iter *piter,
			  struct scatterlist *sglist, unsigned int nents,
			  unsigned long pgoffset)
{
	piter->__pg_advance = 0;
	piter->__nents = nents;

	piter->sg = sglist;
	piter->sg_pgoffset = pgoffset;
}

/*
 * Iterate pages in sg list.
 */
static inline void
_sg_iter_next(struct sg_page_iter *iter)
{
	struct scatterlist *sg;
	unsigned int pgcount;

	sg = iter->sg;
	pgcount = (sg->offset + sg->length + PAGE_MASK) >> PAGE_SHIFT;

	++iter->sg_pgoffset;
	while (iter->sg_pgoffset >= pgcount) {
		iter->sg_pgoffset -= pgcount;
		sg = sg_next(sg);
		--iter->maxents;
		if (sg == NULL || iter->maxents == 0)
			break;
		pgcount = (sg->offset + sg->length + PAGE_MASK) >> PAGE_SHIFT;
	}
	iter->sg = sg;
}

static inline int
sg_page_count(struct scatterlist *sg)
{
	return PAGE_ALIGN(sg->offset + sg->length) >> PAGE_SHIFT;
}

static inline bool
__sg_page_iter_next(struct sg_page_iter *piter)
{
	if (piter->__nents == 0)
		return (false);
	if (piter->sg == NULL)
		return (false);

	piter->sg_pgoffset += piter->__pg_advance;
	piter->__pg_advance = 1;

	while (piter->sg_pgoffset >= sg_page_count(piter->sg)) {
		piter->sg_pgoffset -= sg_page_count(piter->sg);
		piter->sg = sg_next(piter->sg);
		if (--piter->__nents == 0)
			return (false);
		if (piter->sg == NULL)
			return (false);
	}
	return (true);
}

/*
 * NOTE: pgoffset is really a page index, not a byte offset.
 */
static inline void
_sg_iter_init(struct scatterlist *sgl, struct sg_page_iter *iter,
	      unsigned int nents, unsigned long pgoffset)
{
	if (nents) {
		/*
		 * Nominal case.  Note subtract 1 from starting page index
		 * for initial _sg_iter_next() call.
		 */
		iter->sg = sgl;
		iter->sg_pgoffset = pgoffset - 1;
		iter->maxents = nents;
		_sg_iter_next(iter);
	} else {
		/*
		 * Degenerate case
		 */
		iter->sg = NULL;
		iter->sg_pgoffset = 0;
		iter->maxents = 0;
	}
}

static inline struct page *
sg_page_iter_page(struct sg_page_iter *piter)
{
	return nth_page(sg_page(piter->sg), piter->sg_pgoffset);
}

static inline dma_addr_t
sg_page_iter_dma_address(struct sg_page_iter *spi)
{
	return spi->sg->dma_address + (spi->sg_pgoffset << PAGE_SHIFT);
}

#define for_each_sg_page(sgl, iter, nents, pgoffset)			\
	for (_sg_iter_init(sgl, iter, nents, pgoffset);			\
	     (iter)->sg; _sg_iter_next(iter))

#define	for_each_sg(sglist, sg, sgmax, _itr)				\
	for (_itr = 0, sg = (sglist); _itr < (sgmax); _itr++, sg = sg_next(sg))

/*
 *
 * XXX please review these
 */
size_t sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
		      const void *buf, size_t buflen, off_t skip);

static inline size_t
sg_copy_from_buffer(struct scatterlist *sgl, unsigned int nents,
		     const char *buf, size_t buflen)
{
	return (sg_pcopy_from_buffer(sgl, nents, buf, buflen, 0));
}

size_t sg_pcopy_to_buffer(struct scatterlist *sgl, unsigned int nents,
		   void *buf, size_t buflen, off_t skip);

static inline size_t
sg_copy_to_buffer(struct scatterlist *sgl, unsigned int nents,
		  char *buf, size_t buflen)
{

	return (sg_pcopy_to_buffer(sgl, nents, buf, buflen, 0));
}

static inline bool
sg_is_last(struct scatterlist *sg)
{
	return (sg->flags & SG_END);
}

static inline bool
sg_is_chain(struct scatterlist *sg)
{
	return (sg->flags & SG_CHAIN);
}

static inline struct scatterlist *
sg_chain_ptr(struct scatterlist *sg)
{
	return sg->sl_un.sg;
}

static inline int
sg_alloc_table_from_pages(struct sg_table *sgt,
	struct page **pages, unsigned int n_pages,
	unsigned long offset, unsigned long size, gfp_t gfp_mask)
{
	kprintf("sg_alloc_table_from_pages: Not implemented\n");
	return -EINVAL;
}

#endif	/* _LINUX_SCATTERLIST_H_ */
