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
 */

#include <sys/diskslice.h>
#include <sys/diskmbr.h>

#include "hammer_util.h"

static void get_buffer_readahead(struct buffer_info *base);
static __inline void *get_ondisk(hammer_off_t buf_offset,
			struct buffer_info **bufferp, int isnew);
static int readhammerbuf(struct volume_info *vol, void *data, int64_t offset);
static int writehammerbuf(struct volume_info *vol, const void *data,
			int64_t offset);

int DebugOpt;

uuid_t Hammer_FSType;
uuid_t Hammer_FSId;
int64_t BootAreaSize;
int64_t MemAreaSize;
int64_t UndoBufferSize;
int	UseReadBehind = -4;
int	UseReadAhead = 4;
int	AssertOnFailure = 1;
struct volume_list VolList = TAILQ_HEAD_INITIALIZER(VolList);

static __inline
int
buffer_hash(hammer_off_t buf_offset)
{
	int hi;

	hi = (int)(buf_offset / HAMMER_BUFSIZE) & HAMMER_BUFLISTMASK;
	return(hi);
}

static struct buffer_info*
find_buffer(struct volume_info *volume, hammer_off_t buf_offset)
{
	int hi;
	struct buffer_info *buf;

	hi = buffer_hash(buf_offset);
	TAILQ_FOREACH(buf, &volume->buffer_lists[hi], entry)
		if (buf->buf_offset == buf_offset)
			return(buf);
	return(NULL);
}

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
	int i, n;
	struct stat st1, st2;

	/*
	 * Allocate the volume structure
	 */
	vol = malloc(sizeof(*vol));
	bzero(vol, sizeof(*vol));
	for (i = 0; i < HAMMER_BUFLISTS; ++i)
		TAILQ_INIT(&vol->buffer_lists[i]);
	vol->name = strdup(filename);
	vol->fd = open(vol->name, oflags);
	if (vol->fd < 0) {
		err(1, "setup_volume: %s: Open failed", vol->name);
	}

	/*
	 * Read or initialize the volume header
	 */
	vol->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
	if (isnew > 0) {
		bzero(ondisk, HAMMER_BUFSIZE);
	} else {
		n = readhammerbuf(vol, ondisk, 0);
		if (n == -1) {
			err(1, "setup_volume: %s: Read failed at offset 0",
			    vol->name);
		}
		vol_no = ondisk->vol_no;
		if (ondisk->vol_rootvol != HAMMER_ROOT_VOLNO) {
			errx(1, "setup_volume: Invalid root volume# %d",
				ondisk->vol_rootvol);
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

	if (isnew > 0) {
		vol->cache.modified = 1;
        }

	if (fstat(vol->fd, &st1) != 0){
		errx(1, "setup_volume: %s: Failed to stat", vol->name);
	}

	/*
	 * Link the volume structure in
	 */
	TAILQ_FOREACH(scan, &VolList, entry) {
		if (scan->vol_no == vol_no) {
			errx(1, "setup_volume: %s: Duplicate volume number %d "
				"against %s", vol->name, vol_no, scan->name);
		}
		if (fstat(scan->fd, &st2) != 0){
			errx(1, "setup_volume: %s: Failed to stat %s",
				vol->name, scan->name);
		}
		if ((st1.st_ino == st2.st_ino) && (st1.st_dev == st2.st_dev)) {
			errx(1, "setup_volume: %s: Specified more than once",
				vol->name);
		}
	}
	TAILQ_INSERT_TAIL(&VolList, vol, entry);
	return(vol);
}

/*
 * Check basic volume characteristics.
 */
void
check_volume(struct volume_info *vol)
{
	struct partinfo pinfo;
	struct stat st;

	/*
	 * Get basic information about the volume
	 */
	if (ioctl(vol->fd, DIOCGPART, &pinfo) < 0) {
		/*
		 * Allow the formatting of regular files as HAMMER volumes
		 */
		if (fstat(vol->fd, &st) < 0)
			err(1, "Unable to stat %s", vol->name);
		vol->size = st.st_size;
		vol->type = "REGFILE";
	} else {
		/*
		 * When formatting a block device as a HAMMER volume the
		 * sector size must be compatible.  HAMMER uses 16384 byte
		 * filesystem buffers.
		 */
		if (pinfo.reserved_blocks) {
			errx(1, "HAMMER cannot be placed in a partition "
				"which overlaps the disklabel or MBR");
		}
		if (pinfo.media_blksize > HAMMER_BUFSIZE ||
		    HAMMER_BUFSIZE % pinfo.media_blksize) {
			errx(1, "A media sector size of %d is not supported",
			     pinfo.media_blksize);
		}

		vol->size = pinfo.media_size;
		vol->device_offset = pinfo.media_offset;
		vol->type = "DEVICE";
	}
}

struct volume_info *
get_volume(int32_t vol_no)
{
	struct volume_info *vol;

	TAILQ_FOREACH(vol, &VolList, entry) {
		if (vol->vol_no == vol_no)
			break;
	}
	if (vol == NULL) {
		if (AssertOnFailure)
			errx(1, "get_volume: Volume %d does not exist!",
				vol_no);
		return(NULL);
	}
	++vol->cache.refs;
	/* not added to or removed from hammer cache */
	return(vol);
}

struct volume_info *
get_root_volume(void)
{
	return(get_volume(HAMMER_ROOT_VOLNO));
}

void
rel_volume(struct volume_info *volume)
{
	if (volume == NULL)
		return;
	/* not added to or removed from hammer cache */
	--volume->cache.refs;
}

/*
 * Acquire the specified buffer.  isnew is -1 only when called
 * via get_buffer_readahead() to prevent another readahead.
 */
struct buffer_info *
get_buffer(hammer_off_t buf_offset, int isnew)
{
	void *ondisk;
	struct buffer_info *buf;
	struct volume_info *volume;
	hammer_off_t orig_offset = buf_offset;
	int vol_no;
	int zone;
	int hi, n;
	int dora = 0;

	zone = HAMMER_ZONE_DECODE(buf_offset);
	if (zone > HAMMER_ZONE_RAW_BUFFER_INDEX) {
		buf_offset = blockmap_lookup(buf_offset, NULL, NULL, NULL);
	}
	if (buf_offset == HAMMER_OFF_BAD)
		return(NULL);

	if (AssertOnFailure) {
		assert((buf_offset & HAMMER_OFF_ZONE_MASK) ==
		       HAMMER_ZONE_RAW_BUFFER);
	}
	vol_no = HAMMER_VOL_DECODE(buf_offset);
	volume = get_volume(vol_no);
	if (volume == NULL)
		return(NULL);

	buf_offset &= ~HAMMER_BUFMASK64;
	buf = find_buffer(volume, buf_offset);

	if (buf == NULL) {
		buf = malloc(sizeof(*buf));
		bzero(buf, sizeof(*buf));
		if (DebugOpt > 1) {
			fprintf(stderr, "get_buffer: %016llx %016llx at %p\n",
				(long long)orig_offset, (long long)buf_offset,
				buf);
		}
		buf->buf_offset = buf_offset;
		buf->raw_offset = hammer_xlate_to_phys(volume->ondisk,
							buf_offset);
		buf->volume = volume;
		hi = buffer_hash(buf_offset);
		TAILQ_INSERT_TAIL(&volume->buffer_lists[hi], buf, entry);
		++volume->cache.refs;
		buf->cache.u.buffer = buf;
		hammer_cache_add(&buf->cache, ISBUFFER);
		dora = (isnew == 0);
	} else {
		if (DebugOpt > 1) {
			fprintf(stderr, "get_buffer: %016llx %016llx at %p *\n",
				(long long)orig_offset, (long long)buf_offset,
				buf);
		}
		hammer_cache_used(&buf->cache);
		++buf->use_count;
	}
	++buf->cache.refs;
	hammer_cache_flush();
	if ((ondisk = buf->ondisk) == NULL) {
		buf->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		if (isnew <= 0) {
			n = readhammerbuf(volume, ondisk, buf->raw_offset);
			if (n == -1) {
				if (AssertOnFailure)
					err(1, "get_buffer: %s:%016llx "
					    "Read failed at offset %016llx",
					    volume->name,
					    (long long)buf->buf_offset,
					    (long long)buf->raw_offset);
				bzero(ondisk, HAMMER_BUFSIZE);
			}
		}
	}
	if (isnew > 0) {
		bzero(ondisk, HAMMER_BUFSIZE);
		buf->cache.modified = 1;
	}
	if (dora)
		get_buffer_readahead(buf);
	return(buf);
}

static void
get_buffer_readahead(struct buffer_info *base)
{
	struct buffer_info *buf;
	struct volume_info *vol;
	hammer_off_t buf_offset;
	int64_t raw_offset;
	int ri = UseReadBehind;
	int re = UseReadAhead;

	raw_offset = base->raw_offset + ri * HAMMER_BUFSIZE;
	vol = base->volume;

	while (ri < re) {
		if (raw_offset >= vol->ondisk->vol_buf_end)
			break;
		if (raw_offset < vol->ondisk->vol_buf_beg || ri == 0) {
			++ri;
			raw_offset += HAMMER_BUFSIZE;
			continue;
		}
		buf_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no,
			raw_offset - vol->ondisk->vol_buf_beg);
		buf = find_buffer(vol, buf_offset);
		if (buf == NULL) {
			buf = get_buffer(buf_offset, -1);
			rel_buffer(buf);
		}
		++ri;
		raw_offset += HAMMER_BUFSIZE;
	}
}

void
rel_buffer(struct buffer_info *buffer)
{
	struct volume_info *volume;
	int hi;

	if (buffer == NULL)
		return;
	assert(buffer->cache.refs > 0);
	if (--buffer->cache.refs == 0) {
		if (buffer->cache.delete) {
			hi = buffer_hash(buffer->buf_offset);
			volume = buffer->volume;
			if (buffer->cache.modified)
				flush_buffer(buffer);
			TAILQ_REMOVE(&volume->buffer_lists[hi], buffer, entry);
			hammer_cache_del(&buffer->cache);
			free(buffer->ondisk);
			free(buffer);
			rel_volume(volume);
		}
	}
}

/*
 * Retrieve a pointer to a buffer data given a buffer offset.  The underlying
 * bufferp is freed if isnew or the offset is out of range of the cached data.
 * If bufferp is freed a referenced buffer is loaded into it.
 */
void *
get_buffer_data(hammer_off_t buf_offset, struct buffer_info **bufferp,
		int isnew)
{
	if (*bufferp != NULL) {
		if (isnew > 0 ||
		    (((*bufferp)->buf_offset ^ buf_offset) & ~HAMMER_BUFMASK64)) {
			rel_buffer(*bufferp);
			*bufferp = NULL;
		}
	}
	return(get_ondisk(buf_offset, bufferp, isnew));
}

/*
 * Retrieve a pointer to a B-Tree node given a zone offset.  The underlying
 * bufferp is freed if non-NULL and a referenced buffer is loaded into it.
 */
hammer_node_ondisk_t
get_node(hammer_off_t node_offset, struct buffer_info **bufferp)
{
	if (*bufferp != NULL) {
		rel_buffer(*bufferp);
		*bufferp = NULL;
	}
	return(get_ondisk(node_offset, bufferp, 0));
}

/*
 * Return a pointer to a buffer data given a buffer offset.
 * If *bufferp is NULL acquire the buffer otherwise use that buffer.
 */
static __inline
void *
get_ondisk(hammer_off_t buf_offset, struct buffer_info **bufferp, int isnew)
{
	struct buffer_info *buffer;

	buffer = *bufferp;
	if (buffer == NULL) {
		buffer = *bufferp = get_buffer(buf_offset, isnew);
		if (buffer == NULL)
			return(NULL);
	}

	return((char *)buffer->ondisk +
		((int32_t)buf_offset & HAMMER_BUFMASK));
}

/*
 * Allocate HAMMER elements - btree nodes, meta data, data storage
 */
void *
alloc_btree_element(hammer_off_t *offp, struct buffer_info **data_bufferp)
{
	hammer_node_ondisk_t node;

	node = alloc_blockmap(HAMMER_ZONE_BTREE_INDEX, sizeof(*node),
			      offp, data_bufferp);
	bzero(node, sizeof(*node));
	return (node);
}

void *
alloc_meta_element(hammer_off_t *offp, int32_t data_len,
		   struct buffer_info **data_bufferp)
{
	void *data;

	data = alloc_blockmap(HAMMER_ZONE_META_INDEX, data_len,
			      offp, data_bufferp);
	bzero(data, data_len);
	return (data);
}

/*
 * The only data_len supported by HAMMER userspace for large data zone
 * (zone 10) is HAMMER_BUFSIZE which is 16KB.  >16KB data does not fit
 * in a buffer allocated by get_buffer().  Also alloc_blockmap() does
 * not consider >16KB buffer size.
 */
void *
alloc_data_element(hammer_off_t *offp, int32_t data_len,
		   struct buffer_info **data_bufferp)
{
	void *data;
	int zone;

	if (data_len == 0)
		return(NULL);

	zone = hammer_data_zone_index(data_len);
	assert(data_len <= HAMMER_BUFSIZE); /* just one buffer */
	assert(zone == HAMMER_ZONE_LARGE_DATA_INDEX ||
	       zone == HAMMER_ZONE_SMALL_DATA_INDEX);

	data = alloc_blockmap(zone, data_len, offp, data_bufferp);
	bzero(data, data_len);
	return(data);
}

/*
 * Format a new freemap.  Set all layer1 entries to UNAVAIL.  The initialize
 * code will load each volume's freemap.
 */
void
format_freemap(struct volume_info *root_vol)
{
	struct buffer_info *buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_blockmap_t blockmap;
	struct hammer_blockmap_layer1 *layer1;
	int i, isnew;

	/* Only root volume needs formatting */
	assert(root_vol->vol_no == HAMMER_ROOT_VOLNO);

	layer1_offset = alloc_bigblock(root_vol, HAMMER_ZONE_FREEMAP_INDEX);
	for (i = 0; i < HAMMER_BIGBLOCK_SIZE; i += sizeof(*layer1)) {
		isnew = ((i % HAMMER_BUFSIZE) == 0);
		layer1 = get_buffer_data(layer1_offset + i, &buffer, isnew);
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
		layer1->blocks_free = 0;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
	}
	assert(i == HAMMER_BIGBLOCK_SIZE);
	rel_buffer(buffer);

	blockmap = &root_vol->ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = layer1_offset;
	blockmap->first_offset = 0;
	blockmap->next_offset = HAMMER_ENCODE_RAW_BUFFER(0, 0);
	blockmap->alloc_offset = HAMMER_ENCODE_RAW_BUFFER(255, -1);
	blockmap->entry_crc = crc32(blockmap, HAMMER_BLOCKMAP_CRCSIZE);
	root_vol->cache.modified = 1;
}

/*
 * Load the volume's remaining free space into the freemap.
 *
 * Returns the number of big-blocks available.
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
	hammer_off_t block_offset;
	hammer_off_t aligned_vol_free_end;
	hammer_blockmap_t freemap;
	int64_t count = 0;
	int64_t layer1_count = 0;

	root_vol = get_root_volume();
	aligned_vol_free_end = (vol->vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
				& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	printf("initialize freemap volume %d\n", vol->vol_no);

	/*
	 * Initialize the freemap.  First preallocate the big-blocks required
	 * to implement layer2.   This preallocation is a bootstrap allocation
	 * using blocks from the target volume.
	 */
	freemap = &root_vol->ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	layer1_base = freemap->phys_offset;

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = layer1_base +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			layer1->phys_offset = alloc_bigblock(vol,
						HAMMER_ZONE_FREEMAP_INDEX);
			layer1->blocks_free = 0;
			buffer1->cache.modified = 1;
			layer1->layer1_crc = crc32(layer1,
						   HAMMER_LAYER1_CRCSIZE);
		}
	}

	/*
	 * Now fill everything in.
	 */
	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_count = 0;
		layer1_offset = layer1_base +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);
		assert(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

		for (block_offset = 0;
		     block_offset < HAMMER_BLOCKMAP_LAYER2;
		     block_offset += HAMMER_BIGBLOCK_SIZE) {
			layer2_offset = layer1->phys_offset +
				        HAMMER_BLOCKMAP_LAYER2_OFFSET(block_offset);
			layer2 = get_buffer_data(layer2_offset, &buffer2, 0);
			bzero(layer2, sizeof(*layer2));

			if (phys_offset + block_offset < vol->vol_free_off) {
				/*
				 * Fixups XXX - big-blocks already allocated as part
				 * of the freemap bootstrap.
				 */
				if (layer2->zone == 0) {
					layer2->zone = HAMMER_ZONE_FREEMAP_INDEX;
					layer2->append_off = HAMMER_BIGBLOCK_SIZE;
					layer2->bytes_free = 0;
				}
			} else if (phys_offset + block_offset < vol->vol_free_end) {
				layer2->zone = 0;
				layer2->append_off = 0;
				layer2->bytes_free = HAMMER_BIGBLOCK_SIZE;
				++count;
				++layer1_count;
			} else {
				layer2->zone = HAMMER_ZONE_UNAVAIL_INDEX;
				layer2->append_off = HAMMER_BIGBLOCK_SIZE;
				layer2->bytes_free = 0;
			}
			layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
			buffer2->cache.modified = 1;
		}

		layer1->blocks_free += layer1_count;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
		buffer1->cache.modified = 1;
	}

	rel_buffer(buffer1);
	rel_buffer(buffer2);
	rel_volume(root_vol);
	return(count);
}

/*
 * Returns the number of big-blocks available for filesystem data and undos
 * without formatting.
 */
int64_t
count_freemap(struct volume_info *vol)
{
	hammer_off_t phys_offset;
	hammer_off_t vol_free_off;
	hammer_off_t aligned_vol_free_end;
	int64_t count = 0;

	vol_free_off = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	aligned_vol_free_end = (vol->vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
				& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	if (vol->vol_no == HAMMER_ROOT_VOLNO)
		vol_free_off += HAMMER_BIGBLOCK_SIZE;

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		vol_free_off += HAMMER_BIGBLOCK_SIZE;
	}

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BIGBLOCK_SIZE) {
		if (phys_offset < vol_free_off) {
			;
		} else if (phys_offset < vol->vol_free_end) {
			++count;
		}
	}

	return(count);
}

/*
 * Format the undomap for the root volume.
 */
void
format_undomap(struct volume_info *root_vol)
{
	const int undo_zone = HAMMER_ZONE_UNDO_INDEX;
	hammer_off_t undo_limit;
	hammer_blockmap_t blockmap;
	struct hammer_volume_ondisk *ondisk;
	struct buffer_info *buffer = NULL;
	hammer_off_t scan;
	int n;
	int limit_index;
	uint32_t seqno;

	/* Only root volume needs formatting */
	assert(root_vol->vol_no == HAMMER_ROOT_VOLNO);
	ondisk = root_vol->ondisk;

	/*
	 * Size the undo buffer in multiples of HAMMER_BIGBLOCK_SIZE,
	 * up to HAMMER_UNDO_LAYER2 big-blocks.  Size to approximately
	 * 0.1% of the disk.
	 *
	 * The minimum UNDO fifo size is 500MB, or approximately 1% of
	 * the recommended 50G disk.
	 *
	 * Changing this minimum is rather dangerous as complex filesystem
	 * operations can cause the UNDO FIFO to fill up otherwise.
	 */
	undo_limit = UndoBufferSize;
	if (undo_limit == 0) {
		undo_limit = (ondisk->vol_buf_end - ondisk->vol_buf_beg) / 1000;
		if (undo_limit < 500*1024*1024)
			undo_limit = 500*1024*1024;
	}
	undo_limit = (undo_limit + HAMMER_BIGBLOCK_MASK64) &
		     ~HAMMER_BIGBLOCK_MASK64;
	if (undo_limit < HAMMER_BIGBLOCK_SIZE)
		undo_limit = HAMMER_BIGBLOCK_SIZE;
	if (undo_limit > HAMMER_BIGBLOCK_SIZE * HAMMER_UNDO_LAYER2)
		undo_limit = HAMMER_BIGBLOCK_SIZE * HAMMER_UNDO_LAYER2;
	UndoBufferSize = undo_limit;

	blockmap = &ondisk->vol0_blockmap[undo_zone];
	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
	blockmap->first_offset = HAMMER_ZONE_ENCODE(undo_zone, 0);
	blockmap->next_offset = blockmap->first_offset;
	blockmap->alloc_offset = HAMMER_ZONE_ENCODE(undo_zone, undo_limit);
	blockmap->entry_crc = crc32(blockmap, HAMMER_BLOCKMAP_CRCSIZE);

	limit_index = undo_limit / HAMMER_BIGBLOCK_SIZE;
	assert(limit_index <= HAMMER_UNDO_LAYER2);

	for (n = 0; n < limit_index; ++n) {
		ondisk->vol0_undo_array[n] = alloc_bigblock(NULL,
							HAMMER_ZONE_UNDO_INDEX);
	}
	while (n < HAMMER_UNDO_LAYER2) {
		ondisk->vol0_undo_array[n++] = HAMMER_BLOCKMAP_UNAVAIL;
	}

	/*
	 * Pre-initialize the UNDO blocks (HAMMER version 4+)
	 */
	printf("initializing the undo map (%jd MB)\n",
		(intmax_t)(blockmap->alloc_offset & HAMMER_OFF_LONG_MASK) /
		(1024 * 1024));

	scan = blockmap->first_offset;
	seqno = 0;

	while (scan < blockmap->alloc_offset) {
		hammer_fifo_head_t head;
		hammer_fifo_tail_t tail;
		int isnew;
		int bytes = HAMMER_UNDO_ALIGN;

		isnew = ((scan & HAMMER_BUFMASK64) == 0);
		head = get_buffer_data(scan, &buffer, isnew);
		buffer->cache.modified = 1;
		tail = (void *)((char *)head + bytes - sizeof(*tail));

		bzero(head, bytes);
		head->hdr_signature = HAMMER_HEAD_SIGNATURE;
		head->hdr_type = HAMMER_HEAD_TYPE_DUMMY;
		head->hdr_size = bytes;
		head->hdr_seq = seqno++;

		tail->tail_signature = HAMMER_TAIL_SIGNATURE;
		tail->tail_type = HAMMER_HEAD_TYPE_DUMMY;
		tail->tail_size = bytes;

		head->hdr_crc = crc32(head, HAMMER_FIFO_HEAD_CRCOFF) ^
				crc32(head + 1, bytes - sizeof(*head));

		scan += bytes;
	}
	rel_buffer(buffer);
}

/*
 * Format a new blockmap.  This is mostly a degenerate case because
 * all allocations are now actually done from the freemap.
 */
void
format_blockmap(hammer_blockmap_t blockmap, int zone, hammer_off_t offset)
{
	hammer_off_t zone_base = HAMMER_ZONE_ENCODE(zone, offset);

	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = 0;
	blockmap->first_offset = zone_base;
	blockmap->next_offset = zone_base;
	blockmap->alloc_offset = HAMMER_ENCODE(zone, 255, -1);
	blockmap->entry_crc = crc32(blockmap, HAMMER_BLOCKMAP_CRCSIZE);
}

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
	int i;

	for (i = 0; i < HAMMER_BUFLISTS; ++i) {
		TAILQ_FOREACH(buffer, &volume->buffer_lists[i], entry)
			flush_buffer(buffer);
	}
	if (writehammerbuf(volume, volume->ondisk, 0) == -1)
		err(1, "Write volume %d (%s)", volume->vol_no, volume->name);
	volume->cache.modified = 0;
}

void
flush_buffer(struct buffer_info *buffer)
{
	struct volume_info *vol;

	vol = buffer->volume;
	if (writehammerbuf(vol, buffer->ondisk, buffer->raw_offset) == -1)
		err(1, "Write volume %d (%s)", vol->vol_no, vol->name);
	buffer->cache.modified = 0;
}

/*
 * Core I/O operations
 */
static int
readhammerbuf(struct volume_info *vol, void *data, int64_t offset)
{
	ssize_t n;

	n = pread(vol->fd, data, HAMMER_BUFSIZE, offset);
	if (n != HAMMER_BUFSIZE)
		return(-1);
	return(0);
}

static int
writehammerbuf(struct volume_info *vol, const void *data, int64_t offset)
{
	ssize_t n;

	n = pwrite(vol->fd, data, HAMMER_BUFSIZE, offset);
	if (n != HAMMER_BUFSIZE)
		return(-1);
	return(0);
}

int64_t init_boot_area_size(int64_t value, off_t avg_vol_size)
{
	if (value == 0) {
		value = HAMMER_BOOT_NOMBYTES;
		while (value > avg_vol_size / HAMMER_MAX_VOLUMES)
			value >>= 1;
		if (value < HAMMER_BOOT_MINBYTES)
			value = 0;
	} else if (value < HAMMER_BOOT_MINBYTES) {
		value = HAMMER_BOOT_MINBYTES;
	}

	return(value);
}

int64_t init_mem_area_size(int64_t value, off_t avg_vol_size)
{
	if (value == 0) {
		value = HAMMER_MEM_NOMBYTES;
		while (value > avg_vol_size / HAMMER_MAX_VOLUMES)
			value >>= 1;
		if (value < HAMMER_MEM_MINBYTES)
			value = 0;
	} else if (value < HAMMER_MEM_MINBYTES) {
		value = HAMMER_MEM_MINBYTES;
	}

	return(value);
}
