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
 * $DragonFly: src/sbin/hammer/ondisk.c,v 1.9 2008/01/24 02:16:47 dillon Exp $
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


struct hammer_alist_config Buf_alist_config;
struct hammer_alist_config Vol_normal_alist_config;
struct hammer_alist_config Vol_super_alist_config;
struct hammer_alist_config Supercl_alist_config;
struct hammer_alist_config Clu_master_alist_config;
struct hammer_alist_config Clu_slave_alist_config;
uuid_t Hammer_FSType;
uuid_t Hammer_FSId;
int64_t BootAreaSize;
int64_t MemAreaSize;
int     UsingSuperClusters;
int     NumVolumes;
int	RootVolNo = -1;
struct volume_list VolList = TAILQ_HEAD_INITIALIZER(VolList);

void
init_alist_templates(void)
{
	/*
	 * Initialize the alist templates we will be using
	 */
	hammer_alist_template(&Buf_alist_config, HAMMER_FSBUF_MAXBLKS,
			      1, HAMMER_FSBUF_METAELMS, 0);
	hammer_alist_template(&Vol_normal_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_VOL_METAELMS_1LYR, 0);
	hammer_alist_template(&Vol_super_alist_config,
			  HAMMER_VOL_MAXSUPERCLUSTERS * HAMMER_SCL_MAXCLUSTERS,
			      HAMMER_SCL_MAXCLUSTERS, HAMMER_VOL_METAELMS_2LYR,
			      0);
	hammer_super_alist_template(&Vol_super_alist_config);
	hammer_alist_template(&Supercl_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_SUPERCL_METAELMS, 0);
	hammer_alist_template(&Clu_master_alist_config, HAMMER_CLU_MAXBUFFERS,
			      1, HAMMER_CLU_MASTER_METAELMS, 0);
	hammer_alist_template(&Clu_slave_alist_config,
			      HAMMER_CLU_MAXBUFFERS * HAMMER_FSBUF_MAXBLKS,
			      HAMMER_FSBUF_MAXBLKS, HAMMER_CLU_SLAVE_METAELMS,
			      1);
	hammer_buffer_alist_template(&Clu_slave_alist_config);
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
	int n;

	/*
	 * Allocate the volume structure
	 */
	vol = malloc(sizeof(*vol));
	bzero(vol, sizeof(*vol));
	TAILQ_INIT(&vol->cluster_list);
	TAILQ_INIT(&vol->supercl_list);
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
		vol->using_supercl = UsingSuperClusters;
	} else {
		n = pread(vol->fd, ondisk, HAMMER_BUFSIZE, 0);
		if (n != HAMMER_BUFSIZE) {
			err(1, "setup_volume: %s: Read failed at offset 0",
			    filename);
		}
		if (ondisk->vol_flags & HAMMER_VOLF_USINGSUPERCL)
			vol->using_supercl = 1;
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
	if (vol->using_supercl) {
		vol->clu_alist.config = &Vol_super_alist_config;
		vol->clu_alist.meta = ondisk->vol_almeta.super;
		vol->clu_alist.info = vol;
	} else {
		vol->clu_alist.config = &Vol_normal_alist_config;
		vol->clu_alist.meta = ondisk->vol_almeta.normal;
	}
	vol->buf_alist.config = &Buf_alist_config;
	vol->buf_alist.meta = ondisk->head.buf_almeta;

	if (isnew) {
		hammer_alist_init(&vol->clu_alist, 0, 0, HAMMER_ASTATE_ALLOC);
		initbuffer(&vol->buf_alist, &ondisk->head, HAMMER_FSBUF_VOLUME);
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

struct supercl_info *
get_supercl(struct volume_info *vol, int32_t scl_no, hammer_alloc_state_t isnew)
{
	struct hammer_supercl_ondisk *ondisk;
	struct supercl_info *scl;
	int32_t scl_group;
	int64_t scl_group_size;
	int64_t clusterSize = vol->ondisk->vol_clsize;
	int n;

	assert(vol->using_supercl);

	TAILQ_FOREACH(scl, &vol->supercl_list, entry) {
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
		scl->volume = vol;
		TAILQ_INSERT_TAIL(&vol->supercl_list, scl, entry);
		++vol->cache.refs;
		scl->cache.u.supercl = scl;
		hammer_cache_add(&scl->cache, ISSUPERCL);

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
				  clusterSize * HAMMER_SCL_MAXCLUSTERS);
		scl->scl_offset = vol->ondisk->vol_clo_beg +
				  scl_group * scl_group_size +
				  (scl_no % HAMMER_VOL_SUPERCLUSTER_GROUP) *
				  HAMMER_BUFSIZE;
	}
	++scl->cache.refs;
	hammer_cache_flush();
	if ((ondisk = scl->ondisk) == NULL) {
		scl->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		scl->clu_alist.config = &Supercl_alist_config;
		scl->clu_alist.meta = ondisk->scl_meta;
		scl->buf_alist.config = &Buf_alist_config;
		scl->buf_alist.meta = ondisk->head.buf_almeta;
		if (isnew == 0) {
			n = pread(vol->fd, ondisk, HAMMER_BUFSIZE,
				  scl->scl_offset);
			if (n != HAMMER_BUFSIZE) {
				err(1, "get_supercl: %s:%d Read failed "
				    "at offset %lld",
				    vol->name, scl_no, scl->scl_offset);
			}
		}
	}
	if (isnew) {
		bzero(ondisk, HAMMER_BUFSIZE);
		hammer_alist_init(&scl->clu_alist, 0, 0, isnew);
                initbuffer(&scl->buf_alist, &ondisk->head,
			   HAMMER_FSBUF_SUPERCL);
		scl->cache.modified = 1;
	}
	return(scl);
}

void
rel_supercl(struct supercl_info *supercl)
{
	struct volume_info *volume;

	assert(supercl->cache.refs > 0);
	if (--supercl->cache.refs == 0) {
		if (supercl->cache.delete) {
			volume = supercl->volume;
			if (supercl->cache.modified)
				flush_supercl(supercl);
			TAILQ_REMOVE(&volume->supercl_list, supercl, entry);
			hammer_cache_del(&supercl->cache);
			free(supercl->ondisk);
			free(supercl);
			rel_volume(volume);
		}
	}
}

struct cluster_info *
get_cluster(struct volume_info *vol, int32_t clu_no, hammer_alloc_state_t isnew)
{
	struct hammer_cluster_ondisk *ondisk;
	struct cluster_info *cl;
	int32_t scl_group;
	int64_t scl_group_size;
	int64_t clusterSize = vol->ondisk->vol_clsize;
	int n;

	TAILQ_FOREACH(cl, &vol->cluster_list, entry) {
		if (cl->clu_no == clu_no)
			break;
	}
	if (cl == NULL) {
		/*
		 * Allocate the cluster
		 */
		cl = malloc(sizeof(*cl));
		bzero(cl, sizeof(*cl));
		TAILQ_INIT(&cl->buffer_list);
		cl->clu_no = clu_no;
		cl->volume = vol;
		TAILQ_INSERT_TAIL(&vol->cluster_list, cl, entry);
		++vol->cache.refs;
		cl->cache.u.cluster = cl;
		hammer_cache_add(&cl->cache, ISCLUSTER);
		if (vol->using_supercl) {
			cl->supercl = get_supercl(vol, clu_no / HAMMER_SCL_MAXCLUSTERS, 0);
			++cl->supercl->cache.refs;
		}

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
		if (vol->using_supercl) {
			scl_group = clu_no / HAMMER_VOL_SUPERCLUSTER_GROUP /
				    HAMMER_SCL_MAXCLUSTERS;
			scl_group_size = 
				((int64_t)HAMMER_BUFSIZE *
				HAMMER_VOL_SUPERCLUSTER_GROUP) +
				((int64_t)HAMMER_VOL_SUPERCLUSTER_GROUP *
				clusterSize * HAMMER_SCL_MAXCLUSTERS);
			scl_group_size += HAMMER_VOL_SUPERCLUSTER_GROUP *
					  HAMMER_BUFSIZE;
			cl->clu_offset =
				vol->ondisk->vol_clo_beg +
				scl_group * scl_group_size +
				(HAMMER_BUFSIZE * HAMMER_VOL_SUPERCLUSTER_GROUP) +
				 ((int64_t)clu_no % ((int64_t)HAMMER_SCL_MAXCLUSTERS * HAMMER_VOL_SUPERCLUSTER_GROUP)) *
				 clusterSize;
		} else {
			cl->clu_offset = vol->ondisk->vol_clo_beg +
					 (int64_t)clu_no * clusterSize;
		}
	}
	++cl->cache.refs;
	hammer_cache_flush();
	if ((ondisk = cl->ondisk) == NULL) {
		cl->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		cl->alist_master.config = &Clu_master_alist_config;
		cl->alist_master.meta = ondisk->clu_master_meta;
		cl->alist_btree.config = &Clu_slave_alist_config;
		cl->alist_btree.meta = ondisk->clu_btree_meta;
		cl->alist_btree.info = cl;
		cl->alist_record.config = &Clu_slave_alist_config;
		cl->alist_record.meta = ondisk->clu_record_meta;
		cl->alist_record.info = cl;
		cl->alist_mdata.config = &Clu_slave_alist_config;
		cl->alist_mdata.meta = ondisk->clu_mdata_meta;
		cl->alist_mdata.info = cl;
		if (isnew == 0) {
			n = pread(vol->fd, ondisk, HAMMER_BUFSIZE,
				  cl->clu_offset);
			if (n != HAMMER_BUFSIZE) {
				err(1, "get_cluster: %s:%d Read failed "
				    "at offset %lld",
				    vol->name, clu_no, cl->clu_offset);
			}
		}
	}
	if (isnew) {
		bzero(ondisk, HAMMER_BUFSIZE);
		hammer_alist_init(&cl->alist_master, 0, 0, isnew);
		hammer_alist_init(&cl->alist_btree, 0, 0, HAMMER_ASTATE_ALLOC);
		hammer_alist_init(&cl->alist_record, 0, 0, HAMMER_ASTATE_ALLOC);
		hammer_alist_init(&cl->alist_mdata, 0, 0, HAMMER_ASTATE_ALLOC);
		cl->cache.modified = 1;
	}
	return(cl);
}

void
rel_cluster(struct cluster_info *cluster)
{
	struct volume_info *volume;
	struct supercl_info *supercl;

	assert(cluster->cache.refs > 0);
	if (--cluster->cache.refs == 0) {
		if (cluster->cache.delete) {
			volume = cluster->volume;
			supercl = cluster->supercl;
			if (cluster->cache.modified)
				flush_cluster(cluster);
			TAILQ_REMOVE(&volume->cluster_list, cluster, entry);
			hammer_cache_del(&cluster->cache);
			free(cluster->ondisk);
			free(cluster);
			rel_volume(volume);
			if (supercl)
				rel_supercl(supercl);
		}
	}
}

/*
 * Acquire the specified buffer.
 * 
 * We are formatting a new buffer is buf_type != 0
 */
struct buffer_info *
get_buffer(struct cluster_info *cl, int32_t buf_no, int64_t buf_type)
{
	hammer_fsbuf_ondisk_t ondisk;
	struct buffer_info *buf;
	int n;

	/*
	 * Find the buffer.  Note that buffer 0 corresponds to the cluster
	 * header and should never be requested.
	 */
	assert(buf_no != 0);
	TAILQ_FOREACH(buf, &cl->buffer_list, entry) {
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
		TAILQ_INSERT_TAIL(&cl->buffer_list, buf, entry);
		++cl->cache.refs;
		buf->cache.u.buffer = buf;
		hammer_cache_add(&buf->cache, ISBUFFER);
	}
	++buf->cache.refs;
	hammer_cache_flush();
	if ((ondisk = buf->ondisk) == NULL) {
		buf->ondisk = ondisk = malloc(HAMMER_BUFSIZE);
		buf->alist.config = &Buf_alist_config;
		buf->alist.meta = ondisk->head.buf_almeta;
		if (buf_type == 0) {
			n = pread(cl->volume->fd, ondisk, HAMMER_BUFSIZE,
				  buf->buf_offset);
			if (n != HAMMER_BUFSIZE) {
				err(1, "get_buffer: %s:%d:%d Read failed at "
				       "offset %lld",
				    cl->volume->name, buf->cluster->clu_no,
				    buf_no, buf->buf_offset);
			}
		}
	}
	if (buf_type) {
		bzero(ondisk, HAMMER_BUFSIZE);
		initbuffer(&buf->alist, &ondisk->head, buf_type);
		buf->cache.modified = 1;
	}
	return(buf);
}

void
rel_buffer(struct buffer_info *buffer)
{
	struct cluster_info *cluster;

	assert(buffer->cache.refs > 0);
	if (--buffer->cache.refs == 0) {
		if (buffer->cache.delete) {
			cluster = buffer->cluster;
			if (buffer->cache.modified)
				flush_buffer(buffer);
			TAILQ_REMOVE(&cluster->buffer_list, buffer, entry);
			hammer_cache_del(&buffer->cache);
			free(buffer->ondisk);
			free(buffer);
			rel_cluster(cluster);
		}
	}
}

/*
 * Retrieve a pointer to a B-Tree node given a cluster offset.  The underlying
 * bufp is freed if non-NULL and a referenced buffer is loaded into it.
 */
hammer_node_ondisk_t
get_node(struct cluster_info *cl, int32_t offset, struct buffer_info **bufp)
{
	struct buffer_info *buf;

	if (*bufp)
		rel_buffer(*bufp);
	*bufp = buf = get_buffer(cl, offset / HAMMER_BUFSIZE, 0);
	if (buf->ondisk->head.buf_type != HAMMER_FSBUF_BTREE) {
		errx(1, "get_node %d:%d:%d - not a B-Tree node buffer!",
		     cl->volume->vol_no, cl->clu_no, offset);
	}
	return((void *)((char *)buf->ondisk + (offset & HAMMER_BUFMASK)));
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
		++cluster->ondisk->stat_idx_bufs;
		++cluster->volume->ondisk->vol_stat_idx_bufs;
		++cluster->volume->ondisk->vol0_stat_idx_bufs;
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
		++cluster->ondisk->stat_data_bufs;
		++cluster->volume->ondisk->vol_stat_data_bufs;
		++cluster->volume->ondisk->vol0_stat_data_bufs;
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
alloc_record_element(struct cluster_info *cluster, int32_t *offp,
		     u_int8_t rec_type)
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
		++cluster->ondisk->stat_rec_bufs;
		++cluster->volume->ondisk->vol_stat_rec_bufs;
		++cluster->volume->ondisk->vol0_stat_rec_bufs;
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
		assert(elm_no != HAMMER_ALIST_BLOCK_NONE);
	}
	cluster->ondisk->idx_record = elm_no;
	buf = get_buffer(cluster, elm_no / HAMMER_FSBUF_MAXBLKS, 0);
	assert(buf->ondisk->head.buf_type != 0);
	item = &buf->ondisk->record.recs[elm_no & HAMMER_FSBUF_BLKMASK];
	*offp = buf->buf_no * HAMMER_BUFSIZE +
		((char *)item - (char *)buf->ondisk);
	++cluster->ondisk->stat_records;
	if (rec_type == HAMMER_RECTYPE_CLUSTER)
		++cluster->ondisk->stat_records;
	return(item);
}

static void
alloc_new_buffer(struct cluster_info *cluster, hammer_alist_t live,
		 u_int64_t type, int32_t nelements)
{
	int32_t buf_no;
	struct buffer_info *buf;

	if (type == HAMMER_FSBUF_RECORDS) {
		buf_no = hammer_alist_alloc_rev(&cluster->alist_master, 1,
						HAMMER_ALIST_BLOCK_MAX);
	} else {
		buf_no = hammer_alist_alloc_fwd(&cluster->alist_master, 1, 
						0);
	}
	assert(buf_no != HAMMER_ALIST_BLOCK_NONE);
	buf = get_buffer(cluster, buf_no, type);
	hammer_alist_free(live, buf_no * HAMMER_FSBUF_MAXBLKS, nelements);
	/* XXX modified bit for multiple gets/rels */
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
flush_volume(struct volume_info *vol)
{
	struct supercl_info *supercl;
	struct cluster_info *cl;

	TAILQ_FOREACH(supercl, &vol->supercl_list, entry)
		flush_supercl(supercl);
	TAILQ_FOREACH(cl, &vol->cluster_list, entry)
		flush_cluster(cl);
	writehammerbuf(vol, vol->ondisk, 0);
	vol->cache.modified = 0;
}

void
flush_supercl(struct supercl_info *supercl)
{
	int64_t supercl_offset;

	supercl_offset = supercl->scl_offset;
	writehammerbuf(supercl->volume, supercl->ondisk, supercl_offset);
	supercl->cache.modified = 0;
}

void
flush_cluster(struct cluster_info *cl)
{
	struct buffer_info *buf;
	int64_t cluster_offset;

	TAILQ_FOREACH(buf, &cl->buffer_list, entry)
		flush_buffer(buf);
	cluster_offset = cl->clu_offset;
	writehammerbuf(cl->volume, cl->ondisk, cluster_offset);
	cl->cache.modified = 0;
}

void
flush_buffer(struct buffer_info *buf)
{
	writehammerbuf(buf->volume, buf->ondisk, buf->buf_offset);
	buf->cache.modified = 0;
}

/*
 * Generic buffer initialization
 */
static void
initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head, u_int64_t type)
{
	head->buf_type = type;
	hammer_alist_init(live, 0, 0, HAMMER_ASTATE_ALLOC);
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

