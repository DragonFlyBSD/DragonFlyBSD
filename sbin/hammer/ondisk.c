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

static void check_volume(struct volume_info *vol);
static void get_buffer_readahead(struct buffer_info *base);
static __inline int readhammervol(struct volume_info *vol);
static __inline int readhammerbuf(struct buffer_info *buf);
static __inline int writehammervol(struct volume_info *vol);
static __inline int writehammerbuf(struct buffer_info *buf);

uuid_t Hammer_FSType;
uuid_t Hammer_FSId;
int UseReadBehind = -4;
int UseReadAhead = 4;
int DebugOpt;

TAILQ_HEAD(volume_list, volume_info);
static struct volume_list VolList = TAILQ_HEAD_INITIALIZER(VolList);
static int valid_hammer_volumes;

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

static
struct volume_info *
__alloc_volume(const char *volname, int oflags)
{
	struct volume_info *vol;
	int i;

	vol = malloc(sizeof(*vol));
	if (vol == NULL)
		err(1, "alloc_volume");
	bzero(vol, sizeof(*vol));

	vol->vol_no = -1;
	vol->rdonly = (oflags == O_RDONLY);
	vol->name = strdup(volname);
	vol->fd = open(vol->name, oflags);
	if (vol->fd < 0)
		err(1, "alloc_volume: Failed to open %s", vol->name);
	check_volume(vol);

	vol->ondisk = malloc(HAMMER_BUFSIZE);
	if (vol->ondisk == NULL)
		err(1, "alloc_volume");
	bzero(vol->ondisk, HAMMER_BUFSIZE);

	for (i = 0; i < HAMMER_BUFLISTS; ++i)
		TAILQ_INIT(&vol->buffer_lists[i]);

	return(vol);
}

static void
__add_volume(struct volume_info *vol)
{
	struct volume_info *scan;
	struct stat st1, st2;

	if (fstat(vol->fd, &st1) != 0)
		errx(1, "add_volume: %s: Failed to stat", vol->name);

	TAILQ_FOREACH(scan, &VolList, entry) {
		if (scan->vol_no == vol->vol_no) {
			errx(1, "add_volume: %s: Duplicate volume number %d "
				"against %s",
				vol->name, vol->vol_no, scan->name);
		}
		if (fstat(scan->fd, &st2) != 0) {
			errx(1, "add_volume: %s: Failed to stat %s",
				vol->name, scan->name);
		}
		if ((st1.st_ino == st2.st_ino) && (st1.st_dev == st2.st_dev)) {
			errx(1, "add_volume: %s: Specified more than once",
				vol->name);
		}
	}

	TAILQ_INSERT_TAIL(&VolList, vol, entry);
}

/*
 * Initialize a volume structure and ondisk vol_no field.
 */
struct volume_info *
init_volume(int32_t vol_no, const char *filename, int oflags)
{
	struct volume_info *vol;

	vol = __alloc_volume(filename, oflags);
	vol->vol_no = vol->ondisk->vol_no = vol_no;

	__add_volume(vol);

	return(vol);
}

/*
 * Initialize a volume structure and read ondisk volume header.
 */
struct volume_info*
load_volume(const char *filename, int oflags)
{
	struct volume_info *vol;
	hammer_volume_ondisk_t ondisk;
	int n;

	vol = __alloc_volume(filename, oflags);

	n = readhammervol(vol);
	if (n == -1) {
		err(1, "load_volume: %s: Read failed at offset 0", vol->name);
	}
	ondisk = vol->ondisk;
	vol->vol_no = ondisk->vol_no;

	if (ondisk->vol_rootvol != HAMMER_ROOT_VOLNO) {
		errx(1, "load_volume: Invalid root volume# %d",
			ondisk->vol_rootvol);
	}

	if (bcmp(&Hammer_FSType, &ondisk->vol_fstype, sizeof(Hammer_FSType))) {
		errx(1, "load_volume: %s: Header does not indicate "
			"that this is a hammer volume", vol->name);
	}

	if (valid_hammer_volumes++ == 0) {
		Hammer_FSId = ondisk->vol_fsid;
	} else if (bcmp(&Hammer_FSId, &ondisk->vol_fsid, sizeof(Hammer_FSId))) {
		errx(1, "load_volume: %s: FSId does match other volumes!",
			vol->name);
	}

	__add_volume(vol);

	return(vol);
}

/*
 * Check basic volume characteristics.
 */
static void
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

void
assert_volume_offset(struct volume_info *vol)
{
	assert(hammer_is_zone_raw_buffer(vol->vol_free_off));
	assert(hammer_is_zone_raw_buffer(vol->vol_free_end));
}

struct volume_info *
get_volume(int32_t vol_no)
{
	struct volume_info *vol;

	TAILQ_FOREACH(vol, &VolList, entry) {
		if (vol->vol_no == vol_no)
			break;
	}

	return(vol);
}

struct volume_info *
get_root_volume(void)
{
	struct volume_info *root_vol;

	root_vol = get_volume(HAMMER_ROOT_VOLNO);
	assert(root_vol != NULL);

	return(root_vol);
}

/*
 * Acquire the specified buffer.  isnew is -1 only when called
 * via get_buffer_readahead() to prevent another readahead.
 */
static struct buffer_info *
get_buffer(hammer_off_t buf_offset, int isnew)
{
	struct buffer_info *buf;
	struct volume_info *volume;
	int vol_no;
	int zone;
	int hi;
	int dora = 0;
	int error = 0;

	zone = HAMMER_ZONE_DECODE(buf_offset);
	if (zone > HAMMER_ZONE_RAW_BUFFER_INDEX)
		buf_offset = blockmap_lookup(buf_offset, NULL, NULL, &error);
	if (error || buf_offset == HAMMER_OFF_BAD)
		return(NULL);
	assert(hammer_is_zone_raw_buffer(buf_offset));

	vol_no = HAMMER_VOL_DECODE(buf_offset);
	volume = get_volume(vol_no);
	assert(volume != NULL);

	buf_offset &= ~HAMMER_BUFMASK64;
	buf = find_buffer(volume, buf_offset);

	if (buf == NULL) {
		buf = malloc(sizeof(*buf));
		bzero(buf, sizeof(*buf));
		buf->buf_offset = buf_offset;
		buf->raw_offset = hammer_xlate_to_phys(volume->ondisk,
							buf_offset);
		buf->volume = volume;
		buf->ondisk = malloc(HAMMER_BUFSIZE);
		if (isnew <= 0) {
			if (readhammerbuf(buf) == -1) {
				err(1, "get_buffer: %s:%016jx "
				    "Read failed at offset %016jx",
				    volume->name,
				    (intmax_t)buf->buf_offset,
				    (intmax_t)buf->raw_offset);
			}
		}

		hi = buffer_hash(buf_offset);
		TAILQ_INSERT_TAIL(&volume->buffer_lists[hi], buf, entry);
		hammer_cache_add(&buf->cache);
		dora = (isnew == 0);
	} else {
		assert(buf->ondisk != NULL);
		assert(isnew != -1);
		hammer_cache_used(&buf->cache);
	}

	++buf->cache.refs;
	hammer_cache_flush();

	if (isnew > 0) {
		assert(buf->cache.modified == 0);
		bzero(buf->ondisk, HAMMER_BUFSIZE);
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

	if (*bufferp == NULL) {
		*bufferp = get_buffer(buf_offset, isnew);
		if (*bufferp == NULL)
			return(NULL);
	}

	return(((char *)(*bufferp)->ondisk) +
		((int32_t)buf_offset & HAMMER_BUFMASK));
}

/*
 * Allocate HAMMER elements - B-Tree nodes
 */
hammer_node_ondisk_t
alloc_btree_node(hammer_off_t *offp, struct buffer_info **data_bufferp)
{
	hammer_node_ondisk_t node;

	node = alloc_blockmap(HAMMER_ZONE_BTREE_INDEX, sizeof(*node),
			      offp, data_bufferp);
	bzero(node, sizeof(*node));
	return(node);
}

/*
 * Allocate HAMMER elements - meta data (inode, direntry, PFS, etc)
 */
void *
alloc_meta_element(hammer_off_t *offp, int32_t data_len,
		   struct buffer_info **data_bufferp)
{
	void *data;

	data = alloc_blockmap(HAMMER_ZONE_META_INDEX, data_len,
			      offp, data_bufferp);
	bzero(data, data_len);
	return(data);
}

/*
 * Format a new blockmap.  This is mostly a degenerate case because
 * all allocations are now actually done from the freemap.
 */
void
format_blockmap(struct volume_info *root_vol, int zone, hammer_off_t offset)
{
	hammer_blockmap_t blockmap;
	hammer_off_t zone_base;

	/* Only root volume needs formatting */
	assert(root_vol->vol_no == HAMMER_ROOT_VOLNO);

	assert(hammer_is_zone2_mapped_index(zone));

	blockmap = &root_vol->ondisk->vol0_blockmap[zone];
	zone_base = HAMMER_ZONE_ENCODE(zone, offset);

	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = 0;
	blockmap->first_offset = zone_base;
	blockmap->next_offset = zone_base;
	blockmap->alloc_offset = HAMMER_ENCODE(zone, 255, -1);
	hammer_crc_set_blockmap(blockmap);
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
	hammer_blockmap_layer1_t layer1;
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
		hammer_crc_set_layer1(layer1);
	}
	assert(i == HAMMER_BIGBLOCK_SIZE);
	rel_buffer(buffer);

	blockmap = &root_vol->ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = layer1_offset;
	blockmap->first_offset = 0;
	blockmap->next_offset = HAMMER_ENCODE_RAW_BUFFER(0, 0);
	blockmap->alloc_offset = HAMMER_ENCODE_RAW_BUFFER(255, -1);
	hammer_crc_set_blockmap(blockmap);
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
	hammer_blockmap_layer1_t layer1;
	hammer_blockmap_layer2_t layer2;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t phys_offset;
	hammer_off_t block_offset;
	hammer_off_t aligned_vol_free_end;
	hammer_blockmap_t freemap;
	int64_t count = 0;
	int64_t layer1_count = 0;

	root_vol = get_root_volume();

	assert_volume_offset(vol);
	aligned_vol_free_end = (vol->vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
				& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	printf("initialize freemap volume %d\n", vol->vol_no);

	/*
	 * Initialize the freemap.  First preallocate the big-blocks required
	 * to implement layer2.   This preallocation is a bootstrap allocation
	 * using blocks from the target volume.
	 */
	freemap = &root_vol->ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			layer1->phys_offset = alloc_bigblock(vol,
						HAMMER_ZONE_FREEMAP_INDEX);
			layer1->blocks_free = 0;
			buffer1->cache.modified = 1;
			hammer_crc_set_layer1(layer1);
		}
	}

	/*
	 * Now fill everything in.
	 */
	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(vol->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_count = 0;
		layer1_offset = freemap->phys_offset +
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
				 * Big-blocks already allocated as part
				 * of the freemap bootstrap.
				 */
				layer2->zone = HAMMER_ZONE_FREEMAP_INDEX;
				layer2->append_off = HAMMER_BIGBLOCK_SIZE;
				layer2->bytes_free = 0;
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
			hammer_crc_set_layer2(layer2);
			buffer2->cache.modified = 1;
		}

		layer1->blocks_free += layer1_count;
		hammer_crc_set_layer1(layer1);
		buffer1->cache.modified = 1;
	}

	rel_buffer(buffer1);
	rel_buffer(buffer2);
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

	assert_volume_offset(vol);
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
format_undomap(struct volume_info *root_vol, int64_t *undo_buffer_size)
{
	const int undo_zone = HAMMER_ZONE_UNDO_INDEX;
	hammer_off_t undo_limit;
	hammer_blockmap_t blockmap;
	hammer_volume_ondisk_t ondisk;
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
	undo_limit = *undo_buffer_size;
	if (undo_limit == 0) {
		undo_limit = HAMMER_VOL_BUF_SIZE(ondisk) / 1000;
		if (undo_limit < 500*1024*1024)
			undo_limit = 500*1024*1024;
	}
	undo_limit = (undo_limit + HAMMER_BIGBLOCK_MASK64) &
		     ~HAMMER_BIGBLOCK_MASK64;
	if (undo_limit < HAMMER_BIGBLOCK_SIZE)
		undo_limit = HAMMER_BIGBLOCK_SIZE;
	if (undo_limit > HAMMER_BIGBLOCK_SIZE * HAMMER_UNDO_LAYER2)
		undo_limit = HAMMER_BIGBLOCK_SIZE * HAMMER_UNDO_LAYER2;
	*undo_buffer_size = undo_limit;

	blockmap = &ondisk->vol0_blockmap[undo_zone];
	bzero(blockmap, sizeof(*blockmap));
	blockmap->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
	blockmap->first_offset = HAMMER_ZONE_ENCODE(undo_zone, 0);
	blockmap->next_offset = blockmap->first_offset;
	blockmap->alloc_offset = HAMMER_ZONE_ENCODE(undo_zone, undo_limit);
	hammer_crc_set_blockmap(blockmap);

	limit_index = undo_limit / HAMMER_BIGBLOCK_SIZE;
	assert(limit_index <= HAMMER_UNDO_LAYER2);

	for (n = 0; n < limit_index; ++n) {
		ondisk->vol0_undo_array[n] = alloc_bigblock(root_vol,
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

		hammer_crc_set_fifo_head(head, bytes);

		scan += bytes;
	}
	rel_buffer(buffer);
}

const char *zone_labels[] = {
	"",		/* 0 */
	"raw_volume",	/* 1 */
	"raw_buffer",	/* 2 */
	"undo",		/* 3 */
	"freemap",	/* 4 */
	"",		/* 5 */
	"",		/* 6 */
	"",		/* 7 */
	"btree",	/* 8 */
	"meta",		/* 9 */
	"large_data",	/* 10 */
	"small_data",	/* 11 */
	"",		/* 12 */
	"",		/* 13 */
	"",		/* 14 */
	"unavail",	/* 15 */
};

void
print_blockmap(const struct volume_info *root_vol)
{
	hammer_blockmap_t blockmap;
	hammer_volume_ondisk_t ondisk;
	int64_t size, used;
	int i;
#define INDENT ""

	ondisk = root_vol->ondisk;
	printf(INDENT"vol_label\t%s\n", ondisk->vol_label);
	printf(INDENT"vol_count\t%d\n", ondisk->vol_count);
	printf(INDENT"vol_bot_beg\t%s\n", sizetostr(ondisk->vol_bot_beg));
	printf(INDENT"vol_mem_beg\t%s\n", sizetostr(ondisk->vol_mem_beg));
	printf(INDENT"vol_buf_beg\t%s\n", sizetostr(ondisk->vol_buf_beg));
	printf(INDENT"vol_buf_end\t%s\n", sizetostr(ondisk->vol_buf_end));
	printf(INDENT"vol0_next_tid\t%016jx\n",
	       (uintmax_t)ondisk->vol0_next_tid);

	blockmap = &ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	size = blockmap->alloc_offset & HAMMER_OFF_LONG_MASK;
	if (blockmap->first_offset <= blockmap->next_offset)
		used = blockmap->next_offset - blockmap->first_offset;
	else
		used = blockmap->alloc_offset - blockmap->first_offset +
			(blockmap->next_offset & HAMMER_OFF_LONG_MASK);
	printf(INDENT"undo_size\t%s\n", sizetostr(size));
	printf(INDENT"undo_used\t%s\n", sizetostr(used));

	printf(INDENT"zone #             "
	       "phys             first            next             alloc\n");
	for (i = 0; i < HAMMER_MAX_ZONES; i++) {
		blockmap = &ondisk->vol0_blockmap[i];
		printf(INDENT"zone %-2d %-10s %016jx %016jx %016jx %016jx\n",
			i, zone_labels[i],
			(uintmax_t)blockmap->phys_offset,
			(uintmax_t)blockmap->first_offset,
			(uintmax_t)blockmap->next_offset,
			(uintmax_t)blockmap->alloc_offset);
	}
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
	if (writehammervol(volume) == -1)
		err(1, "Write volume %d (%s)", volume->vol_no, volume->name);
}

void
flush_buffer(struct buffer_info *buffer)
{
	struct volume_info *vol;

	vol = buffer->volume;
	if (writehammerbuf(buffer) == -1)
		err(1, "Write volume %d (%s)", vol->vol_no, vol->name);
	buffer->cache.modified = 0;
}

/*
 * Core I/O operations
 */
static int
__read(struct volume_info *vol, void *data, int64_t offset, int size)
{
	ssize_t n;

	n = pread(vol->fd, data, size, offset);
	if (n != size)
		return(-1);
	return(0);
}

static __inline int
readhammervol(struct volume_info *vol)
{
	return(__read(vol, vol->ondisk, 0, HAMMER_BUFSIZE));
}

static __inline int
readhammerbuf(struct buffer_info *buf)
{
	return(__read(buf->volume, buf->ondisk, buf->raw_offset, HAMMER_BUFSIZE));
}

static int
__write(struct volume_info *vol, const void *data, int64_t offset, int size)
{
	ssize_t n;

	if (vol->rdonly)
		return(0);

	n = pwrite(vol->fd, data, size, offset);
	if (n != size)
		return(-1);
	return(0);
}

static __inline int
writehammervol(struct volume_info *vol)
{
	return(__write(vol, vol->ondisk, 0, HAMMER_BUFSIZE));
}

static __inline int
writehammerbuf(struct buffer_info *buf)
{
	return(__write(buf->volume, buf->ondisk, buf->raw_offset, HAMMER_BUFSIZE));
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
