/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@backplane.com>.
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
 * $DragonFly: src/sys/sys/bio.h,v 1.1 2005/08/08 01:25:31 hmp Exp $
 */

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

typedef void biodone_t(struct buf *);

/*
 * BIO encapsulation for storage drivers and systems that do not require
 * caching support for underlying blocks.
 */
struct bio {
	LIST_ENTRY(bio)	bio_chain;	/* Chaining. */
	struct buf	*bio_buf;   	/* High-level buffer back-pointer. */
	daddr_t	bio_lblkno;     	/* Logical block number. */
	daddr_t	bio_blkno;      	/* Underlying physical block number. */
	daddr_t	bio_pblkno;      	/* Physical block number. */
	dev_t 	bio_dev;        	/* Device associated to this I/O. */
	biodone_t	*bio_done;   	/* Completion function (optional). */
	long 	bio_resid;       	/* Remaining I/O. */
	int 	bio_flags;      	/* Operational flags. */
	int 	bio_error;      	/* Error value. */
	void 	*bio_driver_ctx; 	/* Private context for drivers. */
	void 	*bio_caller_ctx; 	/* Private context for callers. */
};
