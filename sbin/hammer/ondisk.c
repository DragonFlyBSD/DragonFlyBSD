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
 * $DragonFly: src/sbin/hammer/ondisk.c,v 1.3 2007/11/20 07:16:27 dillon Exp $
 */

#include "newfs_hammer.h"

static void initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head,
			u_int64_t type);
static void alloc_new_buffer(struct cluster_info *cluster, hammer_alist_t live,
			u_int64_t type, int32_t nelements);
#if 0
static void readhammerbuf(struct volume_info *vol, void *data,
			int64_t offset);
#endif
static void writehammerbuf(struct volume_info *vol, const void *data,
			int64_t offset);

/*
 * Lookup the requested information structure and related on-disk buffer.
 * Except for getvolume(), these functions will create and initialize any
 * missing info structures.
 */
struct volume_info *
get_volume(int32_t vol_no)
{
	struct volume_info *vol;
	struct hammer_volume_ondisk *ondisk;

	for (vol = VolBase; vol; vol = vol->next) {
		if (vol->vol_no == vol_no)
			break;
	}
	if (vol && vol->ondisk == NULL) {
		vol->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		bzero(ondisk, HAMMER_BUFSIZE);
		if (UsingSuperClusters) {
			vol->clu_alist.config = &Vol_super_alist_config;
			vol->clu_alist.meta = ondisk->vol_almeta.super;
			vol->clu_alist.info = vol;
			hammer_alist_init(&vol->clu_alist);
		} else {
			vol->clu_alist.config = &Vol_normal_alist_config;
			vol->clu_alist.meta = ondisk->vol_almeta.normal;
			hammer_alist_init(&vol->clu_alist);
		}
		vol->buf_alist.config = &Buf_alist_config;
		vol->buf_alist.meta = ondisk->head.buf_almeta;
                initbuffer(&vol->buf_alist, &ondisk->head, HAMMER_FSBUF_VOLUME);
        }
	return(vol);
}

struct supercl_info *
get_supercl(struct volume_info *vol, int32_t scl_no)
{
	struct hammer_supercl_ondisk *ondisk;
	struct supercl_info *scl;
	int32_t scl_group;
	int64_t scl_group_size;

	assert(UsingSuperClusters);

	for (scl = vol->supercl_base; scl; scl = scl->next) {
		if (scl->scl_no == scl_no)
			break;
	}
	if (scl == NULL) {
		/*
		 * Allocate the scl
		 */
		scl = malloc(sizeof(*scl));
		bzero(scl, sizeof(*scl));
		scl->scl_no = scl_no;
		scl->next = vol->supercl_base;
		scl->volume = vol;
		vol->supercl_base = scl;

		/*
		 * Calculate the super-cluster's offset in the volume.
		 *
		 * The arrangement is [scl * N][N * 32768 clusters], repeat.
		 * N is typically 16.
		 */
		scl_group = scl_no / HAMMER_VOL_SUPERCLUSTER_GROUP;
		scl_group_size = ((int64_t)HAMMER_BUFSIZE *
				  HAMMER_VOL_SUPERCLUSTER_GROUP) +
				  ((int64_t)HAMMER_VOL_SUPERCLUSTER_GROUP *
				  ClusterSize * HAMMER_SCL_MAXCLUSTERS);
		scl->scl_offset = vol->ondisk->vol_clo_beg +
				  scl_group * scl_group_size +
				  (scl_no % HAMMER_VOL_SUPERCLUSTER_GROUP) *
				  HAMMER_BUFSIZE;
	}
	if (scl->ondisk == NULL) {
		scl->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		bzero(ondisk, HAMMER_BUFSIZE);
		scl->clu_alist.config = &Supercl_alist_config;
		scl->clu_alist.meta = ondisk->scl_meta;
		hammer_alist_init(&scl->clu_alist);
		scl->buf_alist.config = &Buf_alist_config;
		scl->buf_alist.meta = ondisk->head.buf_almeta;
                initbuffer(&scl->buf_alist, &ondisk->head, HAMMER_FSBUF_SUPERCL);
	}
	return(scl);
}

struct cluster_info *
get_cluster(struct volume_info *vol, int32_t clu_no)
{
	struct hammer_cluster_ondisk *ondisk;
	struct cluster_info *cl;
	int32_t scl_group;
	int64_t scl_group_size;

	for (cl = vol->cluster_base; cl; cl = cl->next) {
		if (cl->clu_no == clu_no)
			break;
	}
	if (cl == NULL) {
		/*
		 * Allocate the cluster
		 */
		cl = malloc(sizeof(*cl));
		bzero(cl, sizeof(*cl));
		cl->clu_no = clu_no;
		cl->next = vol->cluster_base;
		if (UsingSuperClusters) {
			cl->supercl = get_supercl(vol, clu_no / HAMMER_SCL_MAXCLUSTERS);
		}
		cl->volume = vol;
		vol->cluster_base = cl;

		/*
		 * Calculate the cluster's offset in the volume
		 *
		 * The arrangement is [scl * N][N * 32768 clusters], repeat.
		 * N is typically 16.
		 *
		 * Note that the cluster offset calculation is slightly
		 * different from the supercluster offset calculation due
		 * to the way the grouping works.
		 */
		if (UsingSuperClusters) {
			scl_group = clu_no / HAMMER_VOL_SUPERCLUSTER_GROUP /
				    HAMMER_SCL_MAXCLUSTERS;
			scl_group_size = 
				((int64_t)HAMMER_BUFSIZE *
				HAMMER_VOL_SUPERCLUSTER_GROUP) +
				((int64_t)HAMMER_VOL_SUPERCLUSTER_GROUP *
				ClusterSize * HAMMER_SCL_MAXCLUSTERS);
			scl_group_size += HAMMER_VOL_SUPERCLUSTER_GROUP *
					  HAMMER_BUFSIZE;
			cl->clu_offset =
				vol->ondisk->vol_clo_beg +
				scl_group * scl_group_size +
				(HAMMER_BUFSIZE * HAMMER_VOL_SUPERCLUSTER_GROUP) +
				 ((int64_t)clu_no % ((int64_t)HAMMER_SCL_MAXCLUSTERS * HAMMER_VOL_SUPERCLUSTER_GROUP)) *
				 HAMMER_BUFSIZE;
		} else {
			cl->clu_offset = vol->ondisk->vol_clo_beg +
					 (int64_t)clu_no * ClusterSize;
		}
	}
	if (cl->ondisk == NULL) {
		cl->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		bzero(ondisk, HAMMER_BUFSIZE);
		cl->alist_master.config = &Clu_master_alist_config;
		cl->alist_master.meta = ondisk->clu_master_meta;
		hammer_alist_init(&cl->alist_master);
		cl->alist_btree.config = &Clu_slave_alist_config;
		cl->alist_btree.meta = ondisk->clu_btree_meta;
		cl->alist_btree.info = cl;
		hammer_alist_init(&cl->alist_btree);
		cl->alist_record.config = &Clu_slave_alist_config;
		cl->alist_record.meta = ondisk->clu_record_meta;
		cl->alist_record.info = cl;
		hammer_alist_init(&cl->alist_record);
		cl->alist_mdata.config = &Clu_slave_alist_config;
		cl->alist_mdata.meta = ondisk->clu_mdata_meta;
		cl->alist_mdata.info = cl;
		hammer_alist_init(&cl->alist_mdata);
	}
	return(cl);
}

struct buffer_info *
get_buffer(struct cluster_info *cl, int32_t buf_no, int64_t buf_type)
{
	hammer_fsbuf_ondisk_t ondisk;
	struct buffer_info *buf;

	/*
	 * Find the buffer.  Note that buffer 0 corresponds to the cluster
	 * header and should never be requested.
	 */
	assert(buf_no != 0);
	for (buf = cl->buffer_base; buf; buf = buf->next) {
		if (buf->buf_no == buf_no)
			break;
	}
	if (buf == NULL) {
		buf = malloc(sizeof(*buf));
		bzero(buf, sizeof(*buf));
		buf->buf_no = buf_no;
		buf->buf_offset = cl->clu_offset + buf_no * HAMMER_BUFSIZE;
		buf->cluster = cl;
		buf->volume = cl->volume;
		buf->next = cl->buffer_base;
		cl->buffer_base = buf;
	}
	if (buf->ondisk == NULL) {
		buf->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		bzero(ondisk, HAMMER_BUFSIZE);
		buf->alist.config = &Buf_alist_config;
		buf->alist.meta = ondisk->head.buf_almeta;
		initbuffer(&buf->alist, &ondisk->head, buf_type);
	}
	return(buf);
}

/*
 * Allocate HAMMER elements - btree nodes, data storage, and record elements
 */
void *
alloc_btree_element(struct cluster_info *cluster, int32_t *offp)
{
	struct buffer_info *buf;
	hammer_alist_t live;
	int32_t elm_no;
	void *item;

	live = &cluster->alist_btree;
	elm_no = hammer_alist_alloc_fwd(live, 1, cluster->ondisk->idx_index);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_fwd(live, 1, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_BTREE, HAMMER_BTREE_NODES);
		elm_no = hammer_alist_alloc(live, 1);
		assert(elm_no != HAMMER_ALIST_BLOCK_NONE);
	}
	cluster->ondisk->idx_index = elm_no;
	buf = get_buffer(cluster, elm_no / HAMMER_FSBUF_MAXBLKS, 0);
	assert(buf->ondisk->head.buf_type != 0);
	item = &buf->ondisk->btree.nodes[elm_no & HAMMER_FSBUF_BLKMASK];
	*offp = buf->buf_no * HAMMER_BUFSIZE +
		((char *)item - (char *)buf->ondisk);
	return(item);
}

void *
alloc_data_element(struct cluster_info *cluster, int32_t bytes, int32_t *offp)
{
	struct buffer_info *buf;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
	void *item;

	/*
	 * Try to allocate a btree-node.  If elm_no is HAMMER_ALIST_BLOCK_NONE
	 * and buf is non-NULL we have to initialize a new buffer's a-list.
	 */
	live = &cluster->alist_mdata;
	elm_no = hammer_alist_alloc_fwd(live, nblks, cluster->ondisk->idx_data);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_fwd(live, 1, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_DATA, HAMMER_DATA_NODES);
		elm_no = hammer_alist_alloc(live, nblks);
		assert(elm_no != HAMMER_ALIST_BLOCK_NONE);
	}
	cluster->ondisk->idx_index = elm_no;
	buf = get_buffer(cluster, elm_no / HAMMER_FSBUF_MAXBLKS, 0);
	assert(buf->ondisk->head.buf_type != 0);
	item = &buf->ondisk->data.data[elm_no & HAMMER_FSBUF_BLKMASK];
	*offp = buf->buf_no * HAMMER_BUFSIZE +
		((char *)item - (char *)buf->ondisk);
	return(item);
}

void *
alloc_record_element(struct cluster_info *cluster, int32_t *offp)
{
	struct buffer_info *buf;
	hammer_alist_t live;
	int32_t elm_no;
	void *item;

	live = &cluster->alist_record;
	elm_no = hammer_alist_alloc_rev(live, 1, cluster->ondisk->idx_record);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_RECORDS, HAMMER_RECORD_NODES);
		elm_no = hammer_alist_alloc(live, 1);
		assert(elm_no != HAMMER_ALIST_BLOCK_NONE);
	}
	cluster->ondisk->idx_record = elm_no;
	buf = get_buffer(cluster, elm_no / HAMMER_FSBUF_MAXBLKS, 0);
	assert(buf->ondisk->head.buf_type != 0);
	item = &buf->ondisk->record.recs[elm_no & HAMMER_FSBUF_BLKMASK];
	*offp = buf->buf_no * HAMMER_BUFSIZE +
		((char *)item - (char *)buf->ondisk);
	return(item);
}

static void
alloc_new_buffer(struct cluster_info *cluster, hammer_alist_t live,
		 u_int64_t type, int32_t nelements)
{
	int32_t buf_no;
	struct buffer_info *buf;

	buf_no = hammer_alist_alloc(&cluster->alist_master, 1);
	assert(buf_no != HAMMER_ALIST_BLOCK_NONE);
	buf = get_buffer(cluster, buf_no, type);
	hammer_alist_free(live, buf_no * HAMMER_FSBUF_MAXBLKS, nelements);
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

	for (vol = VolBase; vol; vol = vol->next)
		flush_volume(vol);
}

void
flush_volume(struct volume_info *vol)
{
	struct supercl_info *supercl;
	struct cluster_info *cl;

	for (supercl = vol->supercl_base; supercl; supercl = supercl->next)
		flush_supercl(supercl);
	for (cl = vol->cluster_base; cl; cl = cl->next)
		flush_cluster(cl);
	writehammerbuf(vol, vol->ondisk, 0);
}

void
flush_supercl(struct supercl_info *supercl)
{
	int64_t supercl_offset;

	supercl_offset = supercl->scl_offset;
	writehammerbuf(supercl->volume, supercl->ondisk, supercl_offset);
}

void
flush_cluster(struct cluster_info *cl)
{
	struct buffer_info *buf;
	int64_t cluster_offset;

	for (buf = cl->buffer_base; buf; buf = buf->next)
		flush_buffer(buf);
	cluster_offset = cl->clu_offset;
	writehammerbuf(cl->volume, cl->ondisk, cluster_offset);
}

void
flush_buffer(struct buffer_info *buf)
{
	writehammerbuf(buf->volume, buf->ondisk, buf->buf_offset);
}

/*
 * Generic buffer initialization
 */
static void
initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head, u_int64_t type)
{
	head->buf_type = type;
	hammer_alist_init(live);
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

