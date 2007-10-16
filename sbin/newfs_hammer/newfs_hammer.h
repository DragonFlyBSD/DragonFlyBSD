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
 * $DragonFly: src/sbin/newfs_hammer/newfs_hammer.h,v 1.1 2007/10/16 18:30:53 dillon Exp $
 */

#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <vfs/hammer/hammer_alist.h>
#include <vfs/hammer/hammer_disk.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <uuid.h>
#include <assert.h>
#include <err.h>

struct supercl_info;
struct cluster_info;
struct buffer_info;

/*
 * These structures are used by newfs_hammer to track the filesystem
 * buffers it constructs while building the filesystem.  No attempt
 * is made to try to make this efficient.
 */
struct volume_info {
	struct volume_info *next;
	int		vol_no;
	int64_t		vol_cluster_off;

	const char	*name;
	int		fd;
	off_t		size;
	const char	*type;

	struct hammer_alist_live alist;
	struct hammer_volume_ondisk *ondisk;

	struct supercl_info *supercl_base;
	struct cluster_info *cluster_base;
};

struct supercl_info {
	struct supercl_info	*next;
	int32_t			scl_no;
	int64_t			scl_offset;

	struct volume_info	*volume;

	struct hammer_alist_live alist;
	struct hammer_supercl_ondisk *ondisk;
};

struct cluster_info {
	struct cluster_info	*next;
	int32_t			clu_no;
	int64_t			clu_offset;

	struct supercl_info	*supercl;
	struct volume_info	*volume;

	struct hammer_alist_live alist_master;
	struct hammer_alist_live alist_btree;
	struct hammer_alist_live alist_record;
	struct hammer_alist_live alist_mdata;
	struct hammer_cluster_ondisk *ondisk;

	struct buffer_info 	*buffer_base;
};

struct buffer_info {
	struct buffer_info	*next;
	int32_t			buf_no;
	int32_t			buf_offset;

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
extern int32_t ClusterSize;
extern int UsingSuperClusters;
extern int NumVolumes;
extern struct volume_info *VolBase;

uint32_t crc32(const void *buf, size_t size);

struct volume_info *get_volume(int32_t vol_no);
struct supercl_info *get_supercl(struct volume_info *vol, int32_t scl_no);
struct cluster_info *get_cluster(struct volume_info *vol, int32_t clu_no);
struct buffer_info *get_buffer(struct cluster_info *cl, int32_t buf_no,
				int64_t buf_type);
void *alloc_btree_element(struct cluster_info *cluster, int32_t *offp);
void *alloc_data_element(struct cluster_info *cluster,
				int32_t bytes, int32_t *offp);
void *alloc_record_element(struct cluster_info *cluster, int32_t *offp);

void flush_all_volumes(void);
void flush_volume(struct volume_info *vol);
void flush_supercl(struct supercl_info *supercl);
void flush_cluster(struct cluster_info *cl);
void flush_buffer(struct buffer_info *buf);

void hammer_super_alist_template(struct hammer_alist_config *conf); 
void hammer_buffer_alist_template(struct hammer_alist_config *conf); 
