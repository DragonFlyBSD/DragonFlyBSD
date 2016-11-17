/*
 * Copyright (c) 2013-2016 François Tigeot
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

#ifndef _LINUX_IDR_H_
#define _LINUX_IDR_H_

#include <sys/idr.h>

MALLOC_DECLARE(M_IDR);

#define	IDA_CHUNK_SIZE		128	/* 128 bytes per chunk */
#define	IDA_BITMAP_LONGS	(IDA_CHUNK_SIZE / sizeof(long) - 1)
#define	IDA_BITMAP_BITS 	(IDA_BITMAP_LONGS * sizeof(long) * 8)

struct ida_bitmap {
	long			nr_busy;
	unsigned long		bitmap[IDA_BITMAP_LONGS];
};

struct ida {
	struct idr		idr;
	struct ida_bitmap	*free_bitmap;
};

static inline void
ida_init(struct ida *ida)
{
	idr_init(&ida->idr);
}

static inline void
ida_simple_remove(struct ida *ida, unsigned int id)
{
	idr_remove(&ida->idr, id);
}

static inline void
ida_remove(struct ida *ida, int id)
{
	idr_remove(&ida->idr, id);
}

static inline void
ida_destroy(struct ida *ida)
{
	idr_destroy(&ida->idr);
	if (ida->free_bitmap != NULL) {
		/* kfree() is a linux macro! Work around the cpp pass */
		(kfree)(ida->free_bitmap, M_IDR);
	}
}

static inline int
ida_simple_get(struct ida *ida, unsigned int start, unsigned int end, gfp_t gfp_mask)
{
	int id;
	unsigned int lim;

	if ((end == 0) || (end > 0x80000000))
		lim = 0x80000000;
	else
		lim = end - 1;

	idr_preload(gfp_mask);
	id = idr_alloc(&ida->idr, NULL, start, lim, gfp_mask);
	idr_preload_end();

	return id;
}

#endif	/* _LINUX_IDR_H_ */
