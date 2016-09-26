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

#ifndef HAMMER_UTIL_H_
#define HAMMER_UTIL_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/queue.h>
#include <sys/mount.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>

#include <vfs/hammer/hammer_disk.h>
#include <vfs/hammer/hammer_ioctl.h>
#include <vfs/hammer/hammer_crc.h>
#include <uuid.h>

struct cache_info {
	TAILQ_ENTRY(cache_info) entry;
	int refs;	/* structural references */
	int modified;	/* ondisk modified flag */
	int delete;	/* delete flag - delete on last ref */
};

#define HAMMER_BUFLISTS		64
#define HAMMER_BUFLISTMASK	(HAMMER_BUFLISTS - 1)

/*
 * These structures are used by hammer(8) and newfs_hammer(8)
 * to track the filesystem buffers.
 *
 * vol_free_off and vol_free_end are zone-2 offsets.
 * These two are initialized only when newly creating a filesystem.
 */
struct volume_info {
	TAILQ_ENTRY(volume_info) entry;
	const char		*name;
	const char		*type;
	int			vol_no;
	int			rdonly;
	int			fd;
	off_t			size;
	off_t			device_offset;

	hammer_off_t		vol_free_off;
	hammer_off_t		vol_free_end;

	hammer_volume_ondisk_t ondisk;

	TAILQ_HEAD(, buffer_info) buffer_lists[HAMMER_BUFLISTS];
};

struct buffer_info {
	struct cache_info	cache;		/* must be at offset 0 */
	TAILQ_ENTRY(buffer_info) entry;
	hammer_off_t		buf_offset;	/* full hammer offset spec */
	int64_t			raw_offset;	/* physical offset */
	struct volume_info	*volume;
	void			*ondisk;
};

/*
 * Data structure for zone statistics.
 */
struct zone_stat {
	int64_t			blocks;		/* number of big-blocks */
	int64_t			items;		/* number of items */
	int64_t			used;		/* bytes used */
};

extern uuid_t Hammer_FSType;
extern uuid_t Hammer_FSId;
extern int UseReadBehind;
extern int UseReadAhead;
extern int DebugOpt;
extern const char *zone_labels[];

struct volume_info *init_volume(int32_t vol_no, const char *filename,
				int oflags);
struct volume_info *load_volume(const char *filename, int oflags);
void assert_volume_offset(struct volume_info *vol);
struct volume_info *get_volume(int32_t vol_no);
struct volume_info *get_root_volume(void);
void *get_buffer_data(hammer_off_t buf_offset, struct buffer_info **bufferp,
				int isnew);
void rel_buffer(struct buffer_info *buffer);

hammer_off_t alloc_bigblock(struct volume_info *volume, int zone);
void *alloc_blockmap(int zone, int bytes, hammer_off_t *result_offp,
	       struct buffer_info **bufferp);
hammer_off_t blockmap_lookup(hammer_off_t bmap_off,
				hammer_blockmap_layer1_t layer1,
				hammer_blockmap_layer2_t layer2,
				int *errorp);
void format_undomap(struct volume_info *root_vol, int64_t *undo_buffer_size);

hammer_node_ondisk_t alloc_btree_node(hammer_off_t *offp,
			 struct buffer_info **data_bufferp);
void *alloc_meta_element(hammer_off_t *offp, int32_t data_len,
			 struct buffer_info **data_bufferp);
void *alloc_data_element(hammer_off_t *offp, int32_t data_len,
			 struct buffer_info **data_bufferp);

void format_blockmap(struct volume_info *vol, int zone, hammer_off_t offset);
void format_freemap(struct volume_info *root_vol);
int64_t initialize_freemap(struct volume_info *vol);
int64_t count_freemap(struct volume_info *vol);
void print_blockmap(const struct volume_info *root_vol);

void flush_all_volumes(void);
void flush_volume(struct volume_info *vol);
void flush_buffer(struct buffer_info *buf);

int64_t init_boot_area_size(int64_t value, off_t avg_vol_size);
int64_t init_mem_area_size(int64_t value, off_t avg_vol_size);

int hammer_parse_cache_size(const char *arg);
void hammer_cache_add(struct cache_info *cache);
void hammer_cache_del(struct cache_info *cache);
void hammer_cache_used(struct cache_info *cache);
void hammer_cache_flush(void);

int hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);
void hammer_key_beg_init(hammer_base_elm_t base);
void hammer_key_end_init(hammer_base_elm_t base);
int getyn(void);
const char *sizetostr(off_t size);
int hammer_fs_to_vol(const char *fs, struct hammer_ioc_volume_list *iocp);
int hammer_fs_to_rootvol(const char *fs, char *buf, int len);

struct zone_stat *hammer_init_zone_stat(void);
struct zone_stat *hammer_init_zone_stat_bits(void);
void hammer_cleanup_zone_stat(struct zone_stat *stats);
void hammer_add_zone_stat(struct zone_stat *stats, hammer_off_t offset,
			int bytes);
void hammer_add_zone_stat_layer2(struct zone_stat *stats,
			hammer_blockmap_layer2_t layer2);
void hammer_print_zone_stat(const struct zone_stat *stats);

#endif /* !HAMMER_UTIL_H_ */
