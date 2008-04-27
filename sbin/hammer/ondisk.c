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
 * $DragonFly: src/sbin/hammer/ondisk.c,v 1.15 2008/04/27 00:43:55 dillon Exp $
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

static void *alloc_blockmap(int zone, int bytes, hammer_off_t *result_offp,
			struct buffer_info **bufferp);
static hammer_off_t alloc_bigblock(struct volume_info *volume,
			hammer_off_t owner);
#if 0
static void init_fifo_head(hammer_fifo_head_t head, u_int16_t hdr_type);
static hammer_off_t hammer_alloc_fifo(int32_t base_bytes, int32_t ext_bytes,
			struct buffer_info **bufp, u_int16_t hdr_type);
static void readhammerbuf(struct volume_info *vol, void *data,
			int64_t offset);
#endif
static void writehammerbuf(struct volume_info *vol, const void *data,
			int64_t offset);


uuid_t Hammer_FSType;
uuid_t Hammer_FSId;
int64_t BootAreaSize;
int64_t MemAreaSize;
int64_t UndoBufferSize;
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
		/*init_fifo_head(&ondisk->head, HAMMER_HEAD_TYPE_VOL);*/
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
	int vol_no;
	int zone;
	int n;

	zone = HAMMER_ZONE_DECODE(buf_offset);
	if (zone > HAMMER_ZONE_RAW_BUFFER_INDEX) {
		buf_offset = blockmap_lookup(buf_offset, NULL, NULL);
	}
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

void *
get_buffer_data(hammer_off_t buf_offset, struct buffer_info **bufferp,
		int isnew)
{
	struct buffer_info *buffer;

	if ((buffer = *bufferp) != NULL) {
		if (isnew || 
		    ((buffer->buf_offset ^ buf_offset) & ~HAMMER_BUFMASK64)) {
			rel_buffer(buffer);
			buffer = *bufferp = NULL;
		}
	}
	if (buffer == NULL)
		buffer = *bufferp = get_buffer(buf_offset, isnew);
	return((char *)buffer->ondisk + ((int32_t)buf_offset & HAMMER_BUFMASK));
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
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;

	node = alloc_blockmap(HAMMER_ZONE_BTREE_INDEX, sizeof(*node),
			      offp, &buffer);
	bzero(node, sizeof(*node));
	/* XXX buffer not released, pointer remains valid */
	return(node);
}

hammer_record_ondisk_t
alloc_record_element(hammer_off_t *offp, int32_t data_len, void **datap)
{
	struct buffer_info *record_buffer = NULL;
	struct buffer_info *data_buffer = NULL;
	hammer_record_ondisk_t rec;

	rec = alloc_blockmap(HAMMER_ZONE_RECORD_INDEX, sizeof(*rec),
			     offp, &record_buffer);
	bzero(rec, sizeof(*rec));

	if (data_len >= HAMMER_BUFSIZE) {
		assert(data_len <= HAMMER_BUFSIZE); /* just one buffer */
		*datap = alloc_blockmap(HAMMER_ZONE_LARGE_DATA_INDEX, data_len,
					&rec->base.data_off, &data_buffer);
		rec->base.data_len = data_len;
		bzero(*datap, data_len);
	} else if (data_len) {
		*datap = alloc_blockmap(HAMMER_ZONE_SMALL_DATA_INDEX, data_len,
					&rec->base.data_off, &data_buffer);
		rec->base.data_len = data_len;
		bzero(*datap, data_len);
	} else {
		*datap = NULL;
	}
	/* XXX buf not released, ptr remains valid */
	return(rec);
}

/*
 * Format a new freemap.  Set all layer1 entries to UNAVAIL.  The initialize
 * code will load each volume's freemap.
 */
void
format_freemap(struct volume_info *root_vol, hammer_blockmap_t blockmap)
{
	struct buffer_info *buffer = NULL;
	hammer_off_t layer1_offset;
	struct hammer_blockmap_layer1 *layer1;
	int i, isnew;

	layer1_offset = alloc_bigblock(root_vol, 0);
	for (i = 0; i < (int)HAMMER_BLOCKMAP_RADIX1; ++i) {
		isnew = ((i % HAMMER_BLOCKMAP_RADIX1_PERBUFFER) == 0);
		layer1 = get_buffer_data(layer1_offset + i * sizeof(*layer1),
					 &buffer, isnew);
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
		layer1->layer1_crc = crc32(layer1, sizeof(*layer1));
	}
	rel_buffer(buffer);

	blockmap = &root_vol->ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	blockmap->phys_offset = layer1_offset;
	blockmap->alloc_offset = HAMMER_ENCODE_RAW_BUFFER(255, -1);
	blockmap->next_offset = HAMMER_ENCODE_RAW_BUFFER(0, 0);
	blockmap->reserved01 = 0;
	blockmap->entry_crc = crc32(blockmap, sizeof(*blockmap));
	root_vol->cache.modified = 1;
}

/*
 * Load the volume's remaining free space into the freemap.  If this is
 * the root volume, initialize the freemap owner for the layer1 bigblock.
 *
 * Returns the number of bigblocks available.
 */
int64_t
initialize_freemap(struct volume_info *vol)
{
	struct volume_info *root_vol;
	struct buffer_info *buffer1 = NULL;
	struct buffer_info *buffer2 = NULL;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_off_t layer1_base;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t phys_offset;
	hammer_off_t aligned_vol_free_end;
	int64_t count = 0;

	root_vol = get_volume(RootVolNo);
	aligned_vol_free_end = (vol->vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
				& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	printf("initialize freemap volume %d\n", vol->vol_no);

	/*
	 * Initialize the freemap.  First preallocate the bigblocks required
	 * to implement layer2.   This preallocation is a bootstrap allocation
	 * using blocks from the target volume.
	 */
	layer1_base = root_vol->ondisk->vol0_blockmap[
					HAMMER_ZONE_FREEMAP_INDEX].phys_offset;
	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = layer1_base +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			layer1->phys_offset = alloc_bigblock(vol, 0);
			layer1->blocks_free = 0;
			buffer1->cache.modified = 1;
		}
	}

	/*
	 * Now fill everything in.
	 */
	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_LARGEBLOCK_SIZE) {
		layer1_offset = layer1_base +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);

		assert(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);
		layer2_offset = layer1->phys_offset +
				HAMMER_BLOCKMAP_LAYER2_OFFSET(phys_offset);

		layer2 = get_buffer_data(layer2_offset, &buffer2, 0);
		if (phys_offset < vol->vol_free_off) {
			/*
			 * Fixups XXX - bigblocks already allocated as part
			 * of the freemap bootstrap.
			 */
			layer2->u.owner = HAMMER_ENCODE_FREEMAP(0, 0); /* XXX */
		} else if (phys_offset < vol->vol_free_end) {
			++layer1->blocks_free;
			buffer1->cache.modified = 1;
			layer2->u.owner = HAMMER_BLOCKMAP_FREE;
			++count;
		} else {
			layer2->u.owner = HAMMER_BLOCKMAP_UNAVAIL;
		}
		layer2->entry_crc = crc32(layer2, sizeof(*layer2));
		buffer2->cache.modified = 1;

		/*
		 * Finish-up layer 1
		 */
		if (((phys_offset + HAMMER_LARGEBLOCK_SIZE) & HAMMER_BLOCKMAP_LAYER2_MASK) == 0) {
			layer1->layer1_crc = crc32(layer1, sizeof(*layer1));
			buffer1->cache.modified = 1;
		}
	}
	rel_buffer(buffer1);
	rel_buffer(buffer2);
	rel_volume(root_vol);
	return(count);
}

/*
 * Allocate big-blocks using our poor-man's volume->vol_free_off and
 * update the freemap if owner != 0.
 */
hammer_off_t
alloc_bigblock(struct volume_info *volume, hammer_off_t owner)
{
	struct buffer_info *buffer = NULL;
	struct volume_info *root_vol;
	hammer_off_t result_offset;
	hammer_off_t layer_offset;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	int didget;

	if (volume == NULL) {
		volume = get_volume(RootVolNo);
		didget = 1;
	} else {
		didget = 0;
	}
	result_offset = volume->vol_free_off;
	if (result_offset >= volume->vol_free_end)
		panic("alloc_bigblock: Ran out of room, filesystem too small");
	volume->vol_free_off += HAMMER_LARGEBLOCK_SIZE;

	/*
	 * Update the freemap
	 */
	if (owner) {
		root_vol = get_volume(RootVolNo);
		layer_offset = root_vol->ondisk->vol0_blockmap[
					HAMMER_ZONE_FREEMAP_INDEX].phys_offset;
		layer_offset += HAMMER_BLOCKMAP_LAYER1_OFFSET(result_offset);
		layer1 = get_buffer_data(layer_offset, &buffer, 0);
		assert(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);
		--layer1->blocks_free;
		layer1->layer1_crc = crc32(layer1, sizeof(*layer1));
		buffer->cache.modified = 1;
		layer_offset = layer1->phys_offset +
			       HAMMER_BLOCKMAP_LAYER2_OFFSET(result_offset);
		layer2 = get_buffer_data(layer_offset, &buffer, 0);
		assert(layer2->u.owner == HAMMER_BLOCKMAP_FREE);
		layer2->u.owner = owner;
		layer2->entry_crc = crc32(layer2, sizeof(*layer2));
		buffer->cache.modified = 1;

		rel_buffer(buffer);
		rel_volume(root_vol);
	}

	if (didget)
		rel_volume(volume);
	return(result_offset);
}

/*
 * Format the undo-map for the root volume.
 */
void
format_undomap(hammer_volume_ondisk_t ondisk)
{
	const int undo_zone = HAMMER_ZONE_UNDO_INDEX;
	hammer_off_t undo_limit;
	hammer_blockmap_t blockmap;
	hammer_off_t scan;
	struct hammer_blockmap_layer2 *layer2;
	int n;
	int limit_index;

	/*
	 * Size the undo buffer in multiples of HAMMER_LARGEBLOCK_SIZE,
	 * up to HAMMER_UNDO_LAYER2 large blocks.  Size to approximately
	 * 0.1% of the disk.
	 */
	undo_limit = UndoBufferSize;
	if (undo_limit == 0)
		undo_limit = (ondisk->vol_buf_end - ondisk->vol_buf_beg) / 1000;
	undo_limit = (undo_limit + HAMMER_LARGEBLOCK_MASK64) &
		     ~HAMMER_LARGEBLOCK_MASK64;
	if (undo_limit < HAMMER_LARGEBLOCK_SIZE)
		undo_limit = HAMMER_LARGEBLOCK_SIZE;
	if (undo_limit > HAMMER_LARGEBLOCK_SIZE * HAMMER_UNDO_LAYER2)
		undo_limit = HAMMER_LARGEBLOCK_SIZE * HAMMER_UNDO_LAYER2;
	UndoBufferSize = undo_limit;

	blockmap = &ondisk->vol0_blockmap[undo_zone];
	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
	blockmap->first_offset = HAMMER_ZONE_ENCODE(undo_zone, 0);
	blockmap->next_offset = blockmap->first_offset;
	blockmap->alloc_offset = HAMMER_ZONE_ENCODE(undo_zone, undo_limit);
	
	blockmap->entry_crc = crc32(blockmap, sizeof(*blockmap));

	layer2 = &ondisk->vol0_undo_array[0];
	n = 0;
	scan = blockmap->next_offset;
	limit_index = undo_limit / HAMMER_LARGEBLOCK_SIZE;

	assert(limit_index < HAMMER_UNDO_LAYER2);

	for (n = 0; n < limit_index; ++n) {
		layer2->u.phys_offset = alloc_bigblock(NULL, scan);
		layer2->bytes_free = -1;	/* not used */
		layer2->entry_crc = crc32(layer2, sizeof(*layer2));

		scan += HAMMER_LARGEBLOCK_SIZE;
		++layer2;
	}
	while (n < HAMMER_UNDO_LAYER2) {
		layer2->u.phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
		layer2->bytes_free = -1;
		layer2->entry_crc = crc32(layer2, sizeof(*layer2));
		++layer2;
		++n;
	}
}

/*
 * Format a new blockmap.  Set the owner to the base of the blockmap
 * (meaning either the blockmap layer1 bigblock, layer2 bigblock, or
 * target bigblock).
 */
void
format_blockmap(hammer_blockmap_t blockmap, hammer_off_t zone_off)
{
	blockmap->phys_offset = alloc_bigblock(NULL, zone_off);
	blockmap->alloc_offset = zone_off;
	blockmap->first_offset = zone_off;
	blockmap->next_offset = zone_off;
	blockmap->entry_crc = crc32(blockmap, sizeof(*blockmap));
}

static
void *
alloc_blockmap(int zone, int bytes, hammer_off_t *result_offp,
	       struct buffer_info **bufferp)
{
	struct buffer_info *buffer1 = NULL;
	struct buffer_info *buffer2 = NULL;
	struct volume_info *volume;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t bigblock_offset;
	void *ptr;

	volume = get_volume(RootVolNo);

	rootmap = &volume->ondisk->vol0_blockmap[zone];

	/*
	 * Alignment and buffer-boundary issues
	 */
	bytes = (bytes + 7) & ~7;
	if ((rootmap->phys_offset ^ (rootmap->phys_offset + bytes - 1)) &
	    ~HAMMER_BUFMASK64) {
		volume->cache.modified = 1;
		rootmap->phys_offset = (rootmap->phys_offset + bytes) &
				       ~HAMMER_BUFMASK64;
	}

	/*
	 * Dive layer 1
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(rootmap->alloc_offset);

	layer1 = get_buffer_data(layer1_offset, &buffer1, 0);
	if ((rootmap->alloc_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0) {
		buffer1->cache.modified = 1;
		bzero(layer1, sizeof(*layer1));
		layer1->blocks_free = HAMMER_BLOCKMAP_RADIX2;
		layer1->phys_offset = alloc_bigblock(NULL,
						     rootmap->alloc_offset);
	}

	/*
	 * Dive layer 2
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(rootmap->alloc_offset);

	layer2 = get_buffer_data(layer2_offset, &buffer2, 0);

	if ((rootmap->alloc_offset & HAMMER_LARGEBLOCK_MASK64) == 0) {
		buffer2->cache.modified = 1;
		bzero(layer2, sizeof(*layer2));
		layer2->u.phys_offset = alloc_bigblock(NULL,
						       rootmap->alloc_offset);
		layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
		--layer1->blocks_free;
	}

	buffer1->cache.modified = 1;
	buffer2->cache.modified = 1;
	volume->cache.modified = 1;
	layer2->bytes_free -= bytes;
	*result_offp = rootmap->alloc_offset;
	rootmap->alloc_offset += bytes;
	rootmap->next_offset = rootmap->alloc_offset;

	bigblock_offset = layer2->u.phys_offset + 
			  (*result_offp & HAMMER_LARGEBLOCK_MASK);
	ptr = get_buffer_data(bigblock_offset, bufferp, 0);
	(*bufferp)->cache.modified = 1;

	if (buffer1)
		rel_buffer(buffer1);
	if (buffer2)
		rel_buffer(buffer2);

	rel_volume(volume);
	return(ptr);
}

#if 0
/*
 * Reserve space from the FIFO.  Make sure that bytes does not cross a 
 * record boundary.
 *
 * Zero out base_bytes and initialize the fifo head and tail.  The
 * data area is not zerod.
 */
static
hammer_off_t
hammer_alloc_fifo(int32_t base_bytes, int32_t ext_bytes,
		  struct buffer_info **bufp, u_int16_t hdr_type)
{
	struct buffer_info *buf;
	struct volume_info *volume;
	hammer_fifo_head_t head;
	hammer_fifo_tail_t tail;
	hammer_off_t off;
	int32_t aligned_bytes;

	aligned_bytes = (base_bytes + ext_bytes + HAMMER_TAIL_ONDISK_SIZE +
			 HAMMER_HEAD_ALIGN_MASK) & ~HAMMER_HEAD_ALIGN_MASK;

	volume = get_volume(RootVolNo);
	off = volume->ondisk->vol0_fifo_end;

	/*
	 * For now don't deal with transitions across buffer boundaries,
	 * only newfs_hammer uses this function.
	 */
	assert((off & ~HAMMER_BUFMASK64) ==
		((off + aligned_bytes) & ~HAMMER_BUFMASK));

	*bufp = buf = get_buffer(off, 0);

	buf->cache.modified = 1;
	volume->cache.modified = 1;

	head = (void *)((char *)buf->ondisk + ((int32_t)off & HAMMER_BUFMASK));
	bzero(head, base_bytes);

	head->hdr_signature = HAMMER_HEAD_SIGNATURE;
	head->hdr_type = hdr_type;
	head->hdr_size = aligned_bytes;
	head->hdr_seq = volume->ondisk->vol0_next_seq++;

	tail = (void*)((char *)head + aligned_bytes - HAMMER_TAIL_ONDISK_SIZE);
	tail->tail_signature = HAMMER_TAIL_SIGNATURE;
	tail->tail_type = hdr_type;
	tail->tail_size = aligned_bytes;

	volume->ondisk->vol0_fifo_end += aligned_bytes;
	volume->cache.modified = 1;

	rel_volume(volume);

	return(off);
}

#endif

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

#if 0
/*
 * Generic buffer initialization
 */
static void
init_fifo_head(hammer_fifo_head_t head, u_int16_t hdr_type)
{
	head->hdr_signature = HAMMER_HEAD_SIGNATURE;
	head->hdr_type = hdr_type;
	head->hdr_size = 0;
	head->hdr_crc = 0;
	head->hdr_seq = 0;
}

#endif

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

