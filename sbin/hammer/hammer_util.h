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

#include <sys/types.h>
#include <sys/tree.h>
#include <sys/queue.h>

#include <vfs/hammer/hammer_disk.h>
#include <uuid.h>

/*
 * Cache management - so the user code can keep its memory use under control
 */
struct volume_info;
struct buffer_info;

TAILQ_HEAD(volume_list, volume_info);

struct cache_info {
	TAILQ_ENTRY(cache_info) entry;
	union {
		struct volume_info *volume;
		struct buffer_info *buffer;
	} u;
	enum cache_type { ISVOLUME, ISBUFFER } type;
	int refs;	/* structural references */
	int modified;	/* ondisk modified flag */
	int delete;	/* delete flag - delete on last ref */
};

#define HAMMER_BUFLISTS		64
#define HAMMER_BUFLISTMASK	(HAMMER_BUFLISTS - 1)

/*
 * These structures are used by newfs_hammer to track the filesystem
 * buffers it constructs while building the filesystem.  No attempt
 * is made to try to make this efficient.
 */
struct volume_info {
	struct cache_info	cache;
	TAILQ_ENTRY(volume_info) entry;
	int			vol_no;
	hammer_off_t		vol_alloc;	/* volume-relative offset */
	hammer_off_t		vol_free_off;	/* zone-2 offset */
	hammer_off_t		vol_free_end;	/* zone-2 offset */

	char			*name;
	int			fd;
	off_t			size;
	off_t			device_offset;
	const char		*type;

	struct hammer_volume_ondisk *ondisk;

	TAILQ_HEAD(, buffer_info) buffer_lists[HAMMER_BUFLISTS];
};

struct buffer_info {
	struct cache_info	cache;
	TAILQ_ENTRY(buffer_info) entry;
	hammer_off_t		buf_offset;	/* full hammer offset spec */
	int64_t			raw_offset;	/* physical offset */
	int			flags;		/* origination flags */
	int			use_count;	/* read count */
	struct volume_info	*volume;
	void			*ondisk;
};

extern uuid_t Hammer_FSType;
extern uuid_t Hammer_FSId;
extern int64_t BootAreaSize;
extern int64_t MemAreaSize;
extern int64_t UndoBufferSize;
extern int DebugOpt;
extern const char *ScoreBoardFile;
extern const char *RestrictTarget;
extern int NumVolumes;
extern int RootVolNo;
extern struct volume_list VolList;
extern int UseReadBehind;
extern int UseReadAhead;
extern int AssertOnFailure;

uint32_t crc32(const void *buf, size_t size);
uint32_t crc32_ext(const void *buf, size_t size, uint32_t ocrc);

struct volume_info *setup_volume(int32_t vol_no, const char *filename,
				int isnew, int oflags);
struct volume_info *get_volume(int32_t vol_no);
struct volume_info *test_volume(int32_t vol_no);
struct buffer_info *get_buffer(hammer_off_t buf_offset, int isnew);
void *get_buffer_data(hammer_off_t buf_offset, struct buffer_info **bufferp,
				int isnew);
hammer_node_ondisk_t get_node(hammer_off_t node_offset,
				struct buffer_info **bufp);

void rel_volume(struct volume_info *volume);
void rel_buffer(struct buffer_info *buffer);

hammer_off_t blockmap_lookup(hammer_off_t bmap_off,
				struct hammer_blockmap_layer1 *layer1,
				struct hammer_blockmap_layer2 *layer2,
				int *errorp);
void format_blockmap(hammer_blockmap_t blockmap, hammer_off_t zone_base);
void format_undomap(hammer_volume_ondisk_t ondisk);

void *alloc_btree_element(hammer_off_t *offp,
			 struct buffer_info **data_bufferp);
void *alloc_meta_element(hammer_off_t *offp, int32_t data_len,
			 struct buffer_info **data_bufferp);
void *alloc_data_element(hammer_off_t *offp, int32_t data_len,
			 struct buffer_info **data_bufferp);

int hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);
void hammer_key_beg_init(hammer_base_elm_t base);
void hammer_key_end_init(hammer_base_elm_t base);
int hammer_crc_test_leaf(void *data, hammer_btree_leaf_elm_t leaf);

void format_freemap(struct volume_info *root_vol, hammer_blockmap_t blockmap);
int64_t initialize_freemap(struct volume_info *vol);

void flush_all_volumes(void);
void flush_volume(struct volume_info *vol);
void flush_buffer(struct buffer_info *buf);

void hammer_cache_set(int bytes);
void hammer_cache_add(struct cache_info *cache, enum cache_type type);
void hammer_cache_del(struct cache_info *cache);
void hammer_cache_used(struct cache_info *cache);
void hammer_cache_flush(void);

void score_printf(size_t i, size_t w, const char *ctl, ...) __printflike(3, 4);

void panic(const char *ctl, ...) __printflike(1, 2);

