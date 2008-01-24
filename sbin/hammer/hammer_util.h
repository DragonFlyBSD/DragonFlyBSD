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
 * $DragonFly: src/sbin/hammer/hammer_util.h,v 1.7 2008/01/24 02:16:47 dillon Exp $
 */

#include <sys/types.h>
#include <sys/tree.h>
#include <sys/queue.h>

#include <vfs/hammer/hammer_alist.h>
#include <vfs/hammer/hammer_disk.h>
#include <uuid.h>

/*
 * Cache management - so the user code can keep its memory use under control
 */
struct volume_info;
struct supercl_info;
struct cluster_info;
struct buffer_info;

TAILQ_HEAD(volume_list, volume_info);

struct cache_info {
	TAILQ_ENTRY(cache_info) entry;
	union {
		struct volume_info *volume;
		struct supercl_info *supercl;
		struct cluster_info *cluster;
		struct buffer_info *buffer;
	} u;
	enum cache_type { ISVOLUME, ISSUPERCL, ISCLUSTER, ISBUFFER } type;
	int refs;	/* structural references */
	int modified;	/* ondisk modified flag */
	int delete;	/* delete flag - delete on last ref */
};

/*
 * These structures are used by newfs_hammer to track the filesystem
 * buffers it constructs while building the filesystem.  No attempt
 * is made to try to make this efficient.
 */
struct volume_info {
	struct cache_info	cache;
	TAILQ_ENTRY(volume_info) entry;
	int			vol_no;
	int64_t			vol_alloc;

	char			*name;
	int			fd;
	off_t			size;
	const char		*type;
	int			using_supercl;

	struct hammer_alist_live clu_alist;	/* cluster allocator */
	struct hammer_alist_live buf_alist;	/* buffer head only */
	struct hammer_volume_ondisk *ondisk;

	TAILQ_HEAD(, cluster_info) cluster_list;
	TAILQ_HEAD(, supercl_info) supercl_list;
};

struct supercl_info {
	struct cache_info	cache;
	TAILQ_ENTRY(supercl_info) entry;
	int32_t			scl_no;
	int64_t			scl_offset;

	struct volume_info	*volume;

	struct hammer_alist_live clu_alist;
	struct hammer_alist_live buf_alist;
	struct hammer_supercl_ondisk *ondisk;
};

struct cluster_info {
	struct cache_info	cache;
	TAILQ_ENTRY(cluster_info) entry;
	int32_t			clu_no;
	int64_t			clu_offset;

	struct supercl_info	*supercl;
	struct volume_info	*volume;

	struct hammer_alist_live alist_master;
	struct hammer_alist_live alist_btree;
	struct hammer_alist_live alist_record;
	struct hammer_alist_live alist_mdata;
	struct hammer_cluster_ondisk *ondisk;

	TAILQ_HEAD(, buffer_info) buffer_list;
};

struct buffer_info {
	struct cache_info	cache;
	TAILQ_ENTRY(buffer_info) entry;
	int32_t			buf_no;
	int64_t			buf_offset;

	struct cluster_info	*cluster;
	struct volume_info	*volume;

	struct hammer_alist_live alist;
	hammer_fsbuf_ondisk_t	ondisk;
};

extern struct hammer_alist_config Buf_alist_config;
extern struct hammer_alist_config Vol_normal_alist_config;
extern struct hammer_alist_config Vol_super_alist_config;
extern struct hammer_alist_config Supercl_alist_config;
extern struct hammer_alist_config Clu_master_alist_config;
extern struct hammer_alist_config Clu_slave_alist_config;
extern uuid_t Hammer_FSType;
extern uuid_t Hammer_FSId;
extern int64_t BootAreaSize;
extern int64_t MemAreaSize;
extern int UsingSuperClusters;
extern int NumVolumes;
extern int RootVolNo;
extern struct volume_list VolList;

uint32_t crc32(const void *buf, size_t size);

struct volume_info *setup_volume(int32_t vol_no, const char *filename,
				int isnew, int oflags);
struct volume_info *get_volume(int32_t vol_no);
struct supercl_info *get_supercl(struct volume_info *vol, int32_t scl_no,
				hammer_alloc_state_t isnew);
struct cluster_info *get_cluster(struct volume_info *vol, int32_t clu_no,
				hammer_alloc_state_t isnew);
struct buffer_info *get_buffer(struct cluster_info *cl, int32_t buf_no,
				int64_t buf_type);
hammer_node_ondisk_t get_node(struct cluster_info *cl, int32_t offset,
				struct buffer_info **bufp);

void init_alist_templates(void);
void rel_volume(struct volume_info *volume);
void rel_supercl(struct supercl_info *supercl);
void rel_cluster(struct cluster_info *cluster);
void rel_buffer(struct buffer_info *buffer);

void *alloc_btree_element(struct cluster_info *cluster, int32_t *offp);
void *alloc_data_element(struct cluster_info *cluster,
				int32_t bytes, int32_t *offp);
void *alloc_record_element(struct cluster_info *cluster, int32_t *offp,
				u_int8_t rec_type);

int hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);

void flush_all_volumes(void);
void flush_volume(struct volume_info *vol);
void flush_supercl(struct supercl_info *supercl);
void flush_cluster(struct cluster_info *cl);
void flush_buffer(struct buffer_info *buf);

void hammer_super_alist_template(struct hammer_alist_config *conf); 
void hammer_buffer_alist_template(struct hammer_alist_config *conf); 

void hammer_cache_add(struct cache_info *cache, enum cache_type type);
void hammer_cache_del(struct cache_info *cache);
void hammer_cache_flush(void);

void panic(const char *ctl, ...);

