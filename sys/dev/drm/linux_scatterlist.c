/*
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * Copyright (c) 2015 Matthew Dillon <dillon@backplane.com>
 * Copyright (c) 2016 Matt Macy <mmacy@nextbsd.org>
 * Copyright (c) 2017-2018 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>

/**
 * __sg_alloc_table - Allocate and initialize an sg table with given allocator
 * @table:      The sg table header to use
 * @nents:      Number of entries in sg list
 * @max_ents:   The maximum number of entries the allocator returns per call
 * @gfp_mask:   GFP allocation mask
 *
 * Description:
 *   This function returns a @table @nents long. The allocator is
 *   defined to return scatterlist chunks of maximum size @max_ents.
 *   Thus if @nents is bigger than @max_ents, the scatterlists will be
 *   chained in units of @max_ents.
 *
 * Notes:
 *   If this function returns non-0 (eg failure), the caller must call
 *   __sg_free_table() to cleanup any leftover allocations.
 *
 **/
int
__sg_alloc_table(struct sg_table *table, unsigned int nents,
		unsigned int max_ents, gfp_t gfp_mask)
{
	struct scatterlist *sg, *prv;
	unsigned int left;

	memset(table, 0, sizeof(*table));

	if (nents == 0)
		return -EINVAL;
	left = nents;
	prv = NULL;
	do {
		unsigned int sg_size, alloc_size = left;

		if (alloc_size > max_ents) {
			alloc_size = max_ents;
			sg_size = alloc_size - 1;
		} else
			sg_size = alloc_size;

		left -= sg_size;

		sg = kmalloc(alloc_size * sizeof(struct scatterlist), M_DRM, gfp_mask);
		if (unlikely(!sg)) {
		/*
		 * Adjust entry count to reflect that the last
		 * entry of the previous table won't be used for
		 * linkage.  Without this, sg_kfree() may get
		 * confused.
		 */
			if (prv)
				table->nents = ++table->orig_nents;

			return -ENOMEM;
		}

		sg_init_table(sg, alloc_size);
		table->nents = table->orig_nents += sg_size;

		/*
		 * If this is the first mapping, assign the sg table header.
		 * If this is not the first mapping, chain previous part.
		 */
		if (prv)
			sg_chain(prv, max_ents, sg);
		else
			table->sgl = sg;

		/*
		* If no more entries after this one, mark the end
		*/
		if (!left)
			sg_mark_end(&sg[sg_size - 1]);

		prv = sg;
	} while (left);

	return 0;
}

void
__sg_free_table(struct sg_table *table, unsigned int max_ents)
{
	struct scatterlist *sgl, *next;

	if (unlikely(!table->sgl))
		return;

	sgl = table->sgl;
	while (table->orig_nents) {
		unsigned int alloc_size = table->orig_nents;
		unsigned int sg_size;

		/*
		 * If we have more than max_ents segments left,
		 * then assign 'next' to the sg table after the current one.
		 * sg_size is then one less than alloc size, since the last
		 * element is the chain pointer.
		 */
		if (alloc_size > max_ents) {
			next = sgl[max_ents - 1].sl_un.sg;
			alloc_size = max_ents;
			sg_size = alloc_size - 1;
		} else {
			sg_size = alloc_size;
			next = NULL;
		}

		table->orig_nents -= sg_size;
		kfree(sgl);
		sgl = next;
	}

	table->sgl = NULL;
}

size_t
sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
		     const void *buf, size_t buflen, off_t skip)
{
	off_t off;
	int len, curlen, curoff;
	struct sg_page_iter iter;
	struct scatterlist *sg;
	struct page *page;
	char *vaddr;

	off = 0;
	for_each_sg_page(sgl, &iter, nents, 0) {
		sg = iter.sg;
		curlen = sg->length;
		curoff = sg->offset;
		if (skip && curlen >= skip) {
			skip -= curlen;
			continue;
		}
		if (skip) {
			curlen -= skip;
			curoff += skip;
			skip = 0;
		}
		len = min(curlen, buflen - off);
		page = sg_page_iter_page(&iter);
		vaddr = (char *)kmap(page) + sg->offset;
		memcpy(vaddr, (const char *)buf + off, len);
		off += len;
		kunmap(page);
	}

	return (off);
}

size_t
sg_pcopy_to_buffer(struct scatterlist *sgl, unsigned int nents,
		   void *buf, size_t buflen, off_t skip)
{
	off_t off;
	int len, curlen, curoff;
	struct sg_page_iter iter;
	struct scatterlist *sg;
	struct page *page;
	char *vaddr;

	off = 0;
	for_each_sg_page(sgl, &iter, nents, 0) {
		sg = iter.sg;
		curlen = sg->length;
		curoff = sg->offset;
		if (skip && curlen >= skip) {
			skip -= curlen;
			continue;
		}
		if (skip) {
			curlen -= skip;
			curoff += skip;
			skip = 0;
		}
		len = min(curlen, buflen - off);
		page = sg_page_iter_page(&iter);
		vaddr = (char *)kmap(page) + sg->offset;
		memcpy((char *)buf + off, vaddr, len);
		off += len;
		kunmap(page);
	}

	return (off);
}
