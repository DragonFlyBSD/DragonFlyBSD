/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sbin/hammer/ondisk.c,v 1.10 2008/02/08 08:30:56 dillon Exp $
 */

#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include "hammer_util.h"

static void init_fifo_head(hammer_fifo_head_t head, u_int16_t hdr_type);
static hammer_off_t hammer_alloc_fifo(int32_t base_bytes, int32_t ext_bytes,
			struct buffer_info **bufp, u_int16_t hdr_type);
#if 0
static void readhammerbuf(struct volume_info *vol, void *data,
			int64_t offset);
#endif
static void writehammerbuf(struct volume_info *vol, const void *data,
			int64_t offset);


uuid_t Hammer_FSType;
uuid_t Hammer_FSId;
int64_t BootAreaSize;
int64_t MemAreaSize;
int     UsingSuperClusters;
int     NumVolumes;
int	RootVolNo = -1;
struct volume_list VolList = TAILQ_HEAD_INITIALIZER(VolList);

/*
 * Lookup the requested information structure and related on-disk buffer.
 * Missing structures are created.
 */
struct volume_info *
setup_volume(int32_t vol_no, const char *filename, int isnew, int oflags)
{
	struct volume_info *vol;
	struct volume_info *scan;
	struct hammer_volume_ondisk *ondisk;
	int n;

	/*
	 * Allocate the volume structure
	 */
	vol = malloc(sizeof(*vol));
	bzero(vol, sizeof(*vol));
	TAILQ_INIT(&vol->buffer_list);
	vol->name = strdup(filename);
	vol->fd = open(filename, oflags);
	if (vol->fd < 0) {
		free(vol->name);
		free(vol);
		err(1, "setup_volume: %s: Open failed", filename);
	}

	/*
	 * Read or initialize the volume header
	 */
	vol->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
	if (isnew) {
		bzero(ondisk, HAMMER_BUFSIZE);
	} else {
		n = pread(vol->fd, ondisk, HAMMER_BUFSIZE, 0);
		if (n != HAMMER_BUFSIZE) {
			err(1, "setup_volume: %s: Read failed at offset 0",
			    filename);
		}
		vol_no = ondisk->vol_no;
		if (RootVolNo < 0) {
			RootVolNo = ondisk->vol_rootvol;
		} else if (RootVolNo != (int)ondisk->vol_rootvol) {
			errx(1, "setup_volume: %s: root volume disagreement: "
				"%d vs %d",
				vol->name, RootVolNo, ondisk->vol_rootvol);
		}

		if (bcmp(&Hammer_FSType, &ondisk->vol_fstype, sizeof(Hammer_FSType)) != 0) {
			errx(1, "setup_volume: %s: Header does not indicate "
				"that this is a hammer volume", vol->name);
		}
		if (TAILQ_EMPTY(&VolList)) {
			Hammer_FSId = vol->ondisk->vol_fsid;
		} else if (bcmp(&Hammer_FSId, &ondisk->vol_fsid, sizeof(Hammer_FSId)) != 0) {
			errx(1, "setup_volume: %s: FSId does match other "
				"volumes!", vol->name);
		}
	}
	vol->vol_no = vol_no;

	if (isnew) {
		init_fifo_head(&ondisk->head, HAMMER_HEAD_TYPE_VOL);
		vol->cache.modified = 1;
        }

	/*
	 * Link the volume structure in
	 */
	TAILQ_FOREACH(scan, &VolList, entry) {
		if (scan->vol_no == vol_no) {
			errx(1, "setup_volume %s: Duplicate volume number %d "
				"against %s", filename, vol_no, scan->name);
		}
	}
	TAILQ_INSERT_TAIL(&VolList, vol, entry);
	return(vol);
}

struct volume_info *
get_volume(int32_t vol_no)
{
	struct volume_info *vol;

	TAILQ_FOREACH(vol, &VolList, entry) {
		if (vol->vol_no == vol_no)
			break;
	}
	if (vol == NULL)
		errx(1, "get_volume: Volume %d does not exist!", vol_no);
	++vol->cache.refs;
	/* not added to or removed from hammer cache */
	return(vol);
}

void
rel_volume(struct volume_info *volume)
{
	/* not added to or removed from hammer cache */
	--volume->cache.refs;
}

/*
 * Acquire the specified buffer.
 */
struct buffer_info *
get_buffer(hammer_off_t buf_offset, int isnew)
{
	void *ondisk;
	struct buffer_info *buf;
	struct volume_info *volume;
	int n;
	int vol_no;

	assert((buf_offset & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_RAW_BUFFER);

	vol_no = HAMMER_VOL_DECODE(buf_offset);
	volume = get_volume(vol_no);
	buf_offset &= ~HAMMER_BUFMASK64;

	TAILQ_FOREACH(buf, &volume->buffer_list, entry) {
		if (buf->buf_offset == buf_offset)
			break;
	}
	if (buf == NULL) {
		buf = malloc(sizeof(*buf));
		bzero(buf, sizeof(*buf));
		buf->buf_offset = buf_offset;
		buf->buf_disk_offset = volume->ondisk->vol_buf_beg +
					(buf_offset & HAMMER_OFF_SHORT_MASK);
		buf->volume = volume;
		TAILQ_INSERT_TAIL(&volume->buffer_list, buf, entry);
		++volume->cache.refs;
		buf->cache.u.buffer = buf;
		hammer_cache_add(&buf->cache, ISBUFFER);
	}
	++buf->cache.refs;
	hammer_cache_flush();
	if ((ondisk = buf->ondisk) == NULL) {
		buf->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		if (isnew == 0) {
			n = pread(volume->fd, ondisk, HAMMER_BUFSIZE,
				  buf->buf_disk_offset);
			if (n != HAMMER_BUFSIZE) {
				err(1, "get_buffer: %s:%016llx Read failed at "
				       "offset %lld",
				    volume->name, buf->buf_offset,
				    buf->buf_disk_offset);
			}
		}
	}
	if (isnew) {
		bzero(ondisk, HAMMER_BUFSIZE);
		buf->cache.modified = 1;
	}
	return(buf);
}

void
rel_buffer(struct buffer_info *buffer)
{
	struct volume_info *volume;

	assert(buffer->cache.refs > 0);
	if (--buffer->cache.refs == 0) {
		if (buffer->cache.delete) {
			volume = buffer->volume;
			if (buffer->cache.modified)
				flush_buffer(buffer);
			TAILQ_REMOVE(&volume->buffer_list, buffer, entry);
			hammer_cache_del(&buffer->cache);
			free(buffer->ondisk);
			free(buffer);
			rel_volume(volume);
		}
	}
}

/*
 * Retrieve a pointer to a B-Tree node given a cluster offset.  The underlying
 * bufp is freed if non-NULL and a referenced buffer is loaded into it.
 */
hammer_node_ondisk_t
get_node(hammer_off_t node_offset, struct buffer_info **bufp)
{
	struct buffer_info *buf;

	if (*bufp)
		rel_buffer(*bufp);
	*bufp = buf = get_buffer(node_offset, 0);
	return((void *)((char *)buf->ondisk +
			(int32_t)(node_offset & HAMMER_BUFMASK)));
}

/*
 * Allocate HAMMER elements - btree nodes, data storage, and record elements
 *
 * NOTE: hammer_alloc_fifo() initializes the fifo header for the returned
 * item and zero's out the remainder, so don't bzero() it.
 */
void *
alloc_btree_element(hammer_off_t *offp)
{
	struct buffer_info *buf;
	void *item;

	*offp = hammer_alloc_fifo(sizeof(struct hammer_node_ondisk), 0,
				  &buf, HAMMER_HEAD_TYPE_BTREE);
	item = (char *)buf->ondisk + ((int32_t)*offp & HAMMER_BUFMASK);
	/* XXX buf not released, ptr remains valid */
	return(item);
}

hammer_record_ondisk_t
alloc_record_element(hammer_off_t *offp, u_int8_t rec_type,
		     int32_t rec_len, int32_t data_len, void **datap)
{
	struct buffer_info *buf;
	hammer_record_ondisk_t rec;
	int32_t aligned_rec_len;

	aligned_rec_len = (rec_len + HAMMER_HEAD_ALIGN_MASK) &
			  ~HAMMER_HEAD_ALIGN_MASK;

	*offp = hammer_alloc_fifo(aligned_rec_len, data_len, &buf,
				  HAMMER_HEAD_TYPE_RECORD);
	rec = (void *)((char *)buf->ondisk + ((int32_t)*offp & HAMMER_BUFMASK));
	rec->base.base.rec_type = rec_type;
	if (data_len) {
		rec->base.data_off = *offp + aligned_rec_len;
		rec->base.data_len = data_len;
		*datap = (char *)rec + aligned_rec_len;
	} else {
		*datap = NULL;
	}
	/* XXX buf not released, ptr remains valid */
	return(rec);
}

/*
 * Reserve space from the FIFO.  Make sure that bytes does not cross a 
 * record boundary.
 *
 * Initialize the fifo header, keep track of the previous entry's size
 * so the reverse poitner can be initialized (using lastBlk), and also
 * store a terminator (used by the recovery code) which will be overwritten
 * by the next allocation.
 */
static
hammer_off_t
hammer_alloc_fifo(int32_t base_bytes, int32_t ext_bytes,
		  struct buffer_info **bufp, u_int16_t hdr_type)
{
	struct buffer_info *buf;
	struct volume_info *volume;
	hammer_fifo_head_t head;
	hammer_off_t off;
	int32_t aligned_bytes;
	static u_int32_t lastBlk;

	aligned_bytes = (base_bytes + ext_bytes + HAMMER_HEAD_ALIGN_MASK) &
			~HAMMER_HEAD_ALIGN_MASK;

	volume = get_volume(RootVolNo);
	off = volume->ondisk->vol0_fifo_end;

	/*
	 * For now don't deal with transitions across buffer boundaries,
	 * only newfs_hammer uses this function.
	 */
	assert((off & ~HAMMER_BUFMASK64) ==
		 ((off + aligned_bytes + sizeof(*head)) & ~HAMMER_BUFMASK));

	*bufp = buf = get_buffer(off, 0);

	buf->cache.modified = 1;
	volume->cache.modified = 1;

	head = (void *)((char *)buf->ondisk + ((int32_t)off & HAMMER_BUFMASK));
	bzero(head, base_bytes);

	head->hdr_type = hdr_type;
	head->hdr_rev_link = lastBlk;
	head->hdr_fwd_link = aligned_bytes;
	head->hdr_seq = volume->ondisk->vol0_next_seq++;
	lastBlk = head->hdr_fwd_link;

	volume->ondisk->vol0_fifo_end += aligned_bytes;
	volume->cache.modified = 1;
	head = (void *)((char *)head + aligned_bytes);
	head->hdr_signature = HAMMER_HEAD_SIGNATURE;
	head->hdr_type = HAMMER_HEAD_TYPE_TERM;
	head->hdr_rev_link = lastBlk;
	head->hdr_fwd_link = 0;
	head->hdr_crc = 0;
	head->hdr_seq = volume->ondisk->vol0_next_seq;

	rel_volume(volume);

	return(off);
}

/*
 * Flush various tracking structures to disk
 */

/*
 * Flush various tracking structures to disk
 */
void
flush_all_volumes(void)
{
	struct volume_info *vol;

	TAILQ_FOREACH(vol, &VolList, entry)
		flush_volume(vol);
}

void
flush_volume(struct volume_info *volume)
{
	struct buffer_info *buffer;

	TAILQ_FOREACH(buffer, &volume->buffer_list, entry)
		flush_buffer(buffer);
	writehammerbuf(volume, volume->ondisk, 0);
	volume->cache.modified = 0;
}

void
flush_buffer(struct buffer_info *buffer)
{
	writehammerbuf(buffer->volume, buffer->ondisk, buffer->buf_disk_offset);
	buffer->cache.modified = 0;
}

/*
 * Generic buffer initialization
 */
static void
init_fifo_head(hammer_fifo_head_t head, u_int16_t hdr_type)
{
	head->hdr_signature = HAMMER_HEAD_SIGNATURE;
	head->hdr_type = hdr_type;
	head->hdr_rev_link = 0;
	head->hdr_fwd_link = 0;
	head->hdr_crc = 0;
	head->hdr_seq = 0;
}

#if 0
/*
 * Core I/O operations
 */
static void
readhammerbuf(struct volume_info *vol, void *data, int64_t offset)
{
	ssize_t n;

	n = pread(vol->fd, data, HAMMER_BUFSIZE, offset);
	if (n != HAMMER_BUFSIZE)
		err(1, "Read volume %d (%s)", vol->vol_no, vol->name);
}

#endif

static void
writehammerbuf(struct volume_info *vol, const void *data, int64_t offset)
{
	ssize_t n;

	n = pwrite(vol->fd, data, HAMMER_BUFSIZE, offset);
	if (n != HAMMER_BUFSIZE)
		err(1, "Write volume %d (%s)", vol->vol_no, vol->name);
}

void
panic(const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vfprintf(stderr, ctl, va);
	va_end(va);
	fprintf(stderr, "\n");
	exit(1);
}

