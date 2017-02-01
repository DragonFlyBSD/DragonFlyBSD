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
 * $DragonFly: src/sys/sys/bio.h,v 1.6 2008/07/14 03:08:58 dillon Exp $
 */

#ifndef _SYS_BIO_H_
#define _SYS_BIO_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

struct bio;
struct bio_track;
struct disk;
typedef void biodone_t(struct bio *);

/*
 * BIO encapsulation for storage drivers and systems that do not require
 * caching support for underlying blocks.
 *
 * NOTE: bio_done and bio_caller_info belong to the caller, while
 * bio_driver_info belongs to the driver.
 *
 * bio_track is only non-NULL when an I/O is in progress.
 */
struct bio {
	TAILQ_ENTRY(bio) bio_act;	/* driver queue when active */
	TAILQ_ENTRY(bio) link;
	struct bio_track *bio_track;	/* BIO tracking structure */
	struct disk	*bio_disk;
	struct bio	*bio_prev;	/* BIO stack */
	struct bio	*bio_next;	/* BIO stack / cached translations */
	struct buf	*bio_buf;   	/* High-level buffer back-pointer. */
	biodone_t	*bio_done;   	/* MPSAFE caller completion function */
	off_t		bio_offset;	/* Logical offset relative to device */
	void		*bio_driver_info;
	uint32_t	bio_crc;	/* Caller-specific */
	int		bio_flags;
	union {
		void	*ptr;
		off_t	offset;
		int	index;
		u_int32_t uvalue32;
		struct buf *cluster_head;
		struct bio *cluster_parent;
	} bio_caller_info1;
	union {
		void	*ptr;
		off_t	offset;
		int	index;
		struct buf *cluster_tail;
	} bio_caller_info2;
	union {
		void	*ptr;
		int	value;
		long	lvalue;
		struct	timeval tv;
	} bio_caller_info3;
};

/*
 * BIO flags, used for strategy/biodone/biodone_sync interactions.
 */
#define BIO_SYNC	0x00000001
#define BIO_WANT	0x20000000
#define BIO_DONE	0x40000000

void bio_start_transaction(struct bio *, struct bio_track *);

#endif
