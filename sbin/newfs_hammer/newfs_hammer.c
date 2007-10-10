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
 * $DragonFly: src/sbin/newfs_hammer/newfs_hammer.c,v 1.1 2007/10/10 19:35:53 dillon Exp $
 */

#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <vfs/hammer/hammerfs.h>

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

struct vol_info {
	const char *name;
	int volno;
	int fd;
	off_t size;
	const char *type;
} *VolInfoArray;

uint32_t crc32(const void *buf, size_t size);

static void usage(void);
static void check_volume(struct vol_info *info);
static void format_volume(struct vol_info *info, int nvols, uuid_t *fsid,
		const char *label, int32_t clsize);
static int32_t format_cluster(struct vol_info *info,
		struct hammer_volume_ondisk *vol, int isroot);
static void format_root(struct vol_info *info,
		struct hammer_volume_ondisk *vol,
		struct hammer_cluster_ondisk *cl,
		struct hammer_fsbuf_recs *recbuf,
		struct hammer_fsbuf_data *databuf,
		int recoff, int dataoff);
static const char *sizetostr(off_t size);
static int64_t getsize(const char *str, int64_t minval, int64_t maxval);
static hammer_tid_t createtid(void);
#if 0
static void add_record(struct vol_info *info, struct hammer_volume_ondisk *vol,
	       struct hammer_cluster_ondisk *cl, hammer_record_t rec,
	       void *data, int bytes);
#endif

uuid_t Hammer_FSType;
struct hammer_alist Buf_alist;
struct hammer_alist Vol_alist;
struct hammer_alist Clu_alist;

int
main(int ac, char **av)
{
	int i;
	int ch;
	int nvols;
	uuid_t fsid;
	int32_t clsize;
	u_int32_t status;
	off_t total;
	const char *label = NULL;

	/*
	 * Sanity check basic filesystem structures
	 */
	assert(sizeof(struct hammer_almeta) == HAMMER_ALMETA_SIZE);
	assert(sizeof(struct hammer_fsbuf_head) == HAMMER_FSBUF_HEAD_SIZE);
	assert(sizeof(struct hammer_volume_ondisk) <= HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_cluster_ondisk) <= HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_fsbuf_data) == HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_fsbuf_recs) == HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_fsbuf_btree) == HAMMER_BUFSIZE);

	clsize = 0;

	/*
	 * Generate a filesysem id and lookup the filesystem type
	 */
	uuidgen(&fsid, 1);
	uuid_name_lookup(&Hammer_FSType, "DragonFly HAMMER", &status);
	if (status != uuid_s_ok) {
		fprintf(stderr, "uuids file does not have the DragonFly HAMMER filesystem type\n");
		exit(1);
	}

	/*
	 * Initialize the alist templates we will be using
	 */
	hammer_alist_template(&Buf_alist,
			      HAMMER_FSBUF_MAXBLKS, HAMMER_FSBUF_METAELMS);
	hammer_alist_template(&Vol_alist,
			      HAMMER_VOL_MAXCLUSTERS, HAMMER_VOL_METAELMS);
	hammer_alist_template(&Clu_alist,
			      HAMMER_CLU_MAXBUFFERS, HAMMER_CLU_METAELMS);

	/*
	 * Parse arguments
	 */
	while ((ch = getopt(ac, av, "L:s:")) != -1) {
		switch(ch) {
		case 'L':
			label = optarg;
			break;
		case 's':
			/*
			 * The cluster's size is limited by type chunking and
			 * A-List limitations.  Each chunk must be at least
			 * HAMMER_BUFSIZE and the A-List cannot accomodate
			 * more then 32768 buffers.
			 */
			clsize = getsize(optarg, 
					 HAMMER_BUFSIZE * 256LL,
					 HAMMER_BUFSIZE * 32768LL);
			break;
		default:
			usage();
			break;
		}
	}

	if (label == NULL) {
		fprintf(stderr,
			"newfs_hammer: A filesystem label must be specified\n");
		exit(1);
	}

	/*
	 * Collect volume information
	 */
	ac -= optind;
	av += optind;
	nvols = ac;

	VolInfoArray = malloc(sizeof(*VolInfoArray) * nvols);
	total = 0;
	for (i = 0; i < nvols; ++i) {
		VolInfoArray[i].name = av[i];
		VolInfoArray[i].volno = i;
		check_volume(&VolInfoArray[i]);
		total += VolInfoArray[i].size;
	}

	/*
	 * Calculate the size of a cluster (clsize).  A cluster is broken
	 * down into 256 chunks which must be at least filesystem buffer
	 * sized.  This gives us a minimum chunk size of around 4MB.
	 */
	if (clsize == 0) {
		clsize = HAMMER_BUFSIZE * 256;
		while (clsize < total / nvols / 256 && clsize < 0x80000000U) {
			clsize <<= 1;
		}
	}

	printf("---------------------------------------------\n");
	printf("%d volume%s total size %s\n",
		nvols, (nvols == 1 ? "" : "s"), sizetostr(total));
	printf("cluster-size:        %s\n", sizetostr(clsize));
	printf("max-volume-size:     %s\n",
		sizetostr((int64_t)clsize * 32768LL));
	printf("max-filesystem-size: %s\n",
		sizetostr((int64_t)clsize * 32768LL * 32768LL));
	printf("\n");

	/*
	 * Format volumes.  Format the root volume last so vol0_nexttid
	 * represents the next TID globally.
	 */
	for (i = nvols - 1; i >= 0; --i) {
		format_volume(&VolInfoArray[i], nvols, &fsid, label, clsize);
	}
	return(0);
}

static
void
usage(void)
{
	fprintf(stderr, "newfs_hammer vol0 [vol1 ...]\n");
	exit(1);
}

/*
 * Convert the size in bytes to a human readable string.
 */
static const char *
sizetostr(off_t size)
{
	static char buf[32];

	if (size < 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2f", (double)size);
	} else if (size < 1024 * 1024 / 2) {
		snprintf(buf, sizeof(buf), "%6.2fKB",
			(double)size / 1024);
	} else if (size < 1024 * 1024 * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fMB",
			(double)size / (1024 * 1024));
	} else if (size < 1024 * 1024 * 1024LL * 1024LL / 2) {
		snprintf(buf, sizeof(buf), "%6.2fGB",
			(double)size / (1024 * 1024 * 1024LL));
	} else {
		snprintf(buf, sizeof(buf), "%6.2fTB",
			(double)size / (1024 * 1024 * 1024LL * 1024LL));
	}
	return(buf);
}

static int64_t
getsize(const char *str, int64_t minval, int64_t maxval)
{
	int64_t val;
	char *ptr;

	val = strtoll(str, &ptr, 0);
	switch(*ptr) {
	case 't':
	case 'T':
		val *= 1024;
		/* fall through */
	case 'g':
	case 'G':
		val *= 1024;
		/* fall through */
	case 'm':
	case 'M':
		val *= 1024;
		/* fall through */
	case 'k':
	case 'K':
		val *= 1024;
		break;
	default:
		fprintf(stderr, "Unknown suffix in number '%s'\n", str);
		exit(1);
	}
	if (ptr[1]) {
		fprintf(stderr, "Unknown suffix in number '%s'\n", str);
		exit(1);
	}
	if (val < minval) {
		fprintf(stderr, "Value too small: %s, min is %s\n",
			str, sizetostr(minval));
		exit(1);
	}
	if (val > maxval) {
		fprintf(stderr, "Value too large: %s, max is %s\n",
			str, sizetostr(maxval));
		exit(1);
	}
	return(val);
}

static hammer_tid_t
createtid(void)
{
	static hammer_tid_t lasttid;
	struct timeval tv;

	if (lasttid == 0) {
		gettimeofday(&tv, NULL);
		lasttid = tv.tv_sec * 1000000000LL +
			  tv.tv_usec * 1000LL;
	}
	return(lasttid++);
}

static void *
allocbuffer(u_int64_t type)
{
	hammer_fsbuf_head_t head;

	head = malloc(HAMMER_BUFSIZE);
	bzero(head, HAMMER_BUFSIZE);
	head->buf_type = type;

	/*
	 * Filesystem buffer alist.  The alist starts life in an
	 * all-allocated state.  The alist will be in-band or out-of-band
	 * depending on the type of buffer and the number of blocks under
	 * management will also depend on the type of buffer.
	 */
	hammer_alist_init(&Buf_alist, head->buf_almeta);
	/* crc is set on writeout */
	return(head);
}

static void *
allocindexbuffer(struct vol_info *info, struct hammer_volume_ondisk *vol,
		 struct hammer_cluster_ondisk *cl, int64_t cloff, int64_t *poff)
{
	hammer_fsbuf_head_t head;
	int32_t clno;

	head = allocbuffer(HAMMER_FSBUF_BTREE);
	clno = hammer_alist_alloc(&Clu_alist, cl->clu_almeta, 1);
	assert(clno != HAMMER_ALIST_BLOCK_NONE);
	*poff = cloff + clno * HAMMER_BUFSIZE;

	/*
	 * Setup the allocation space for b-tree nodes
	 */
	hammer_alist_free(&Buf_alist, head->buf_almeta,
			  0, HAMMER_BTREE_NODES - 1);
	return(head);
}

static void *
allocrecbuffer(struct vol_info *info, struct hammer_volume_ondisk *vol,
	       struct hammer_cluster_ondisk *cl, int64_t cloff, int64_t *poff)
{
	hammer_fsbuf_head_t head;
	int32_t clno;

	head = allocbuffer(HAMMER_FSBUF_RECORDS);
	clno = hammer_alist_alloc_rev(&Clu_alist, cl->clu_almeta, 1);
	assert(clno != HAMMER_ALIST_BLOCK_NONE);
	*poff = cloff + clno * HAMMER_BUFSIZE;

	/*
	 * Setup the allocation space for records nodes
	 */
	hammer_alist_free(&Buf_alist, head->buf_almeta,
			  0, HAMMER_RECORD_NODES - 1);
	return(head);
}

static void *
allocdatabuffer(struct vol_info *info, struct hammer_volume_ondisk *vol,
	       struct hammer_cluster_ondisk *cl, int64_t cloff, int64_t *poff)
{
	hammer_fsbuf_head_t head;
	int32_t clno;

	head = allocbuffer(HAMMER_FSBUF_DATA);
	clno = hammer_alist_alloc_rev(&Clu_alist, cl->clu_almeta, 1);
	assert(clno != HAMMER_ALIST_BLOCK_NONE);
	*poff = cloff + clno * HAMMER_BUFSIZE;

	/*
	 * Setup the allocation space for piecemeal data
	 */
	hammer_alist_free(&Buf_alist, head->buf_almeta,
			  0, HAMMER_DATA_NODES - 1);
	return(head);
}

static void
writebuffer(struct vol_info *info, int64_t offset, hammer_fsbuf_head_t buf)
{
	buf->buf_crc = 0;
	buf->buf_crc = crc32(buf, HAMMER_BUFSIZE);

	if (lseek(info->fd, offset, 0) < 0) {
		fprintf(stderr, "volume %d seek failed: %s\n", info->volno,
			strerror(errno));
		exit(1);
	}
	if (write(info->fd, buf, HAMMER_BUFSIZE) != HAMMER_BUFSIZE) {
		fprintf(stderr, "volume %d write failed @%016llx: %s\n",
			info->volno, offset, strerror(errno));
		exit(1);
	}
}

/*
 * Check basic volume characteristics.  HAMMER filesystems use a minimum
 * of a 16KB filesystem buffer size.
 */
static
void
check_volume(struct vol_info *info)
{
	struct partinfo pinfo;
	struct stat st;

	/*
	 * Get basic information about the volume
	 */
	info->fd = open(info->name, O_RDWR);
	if (info->fd < 0) {
		fprintf(stderr, "Unable to open %s R+W: %s\n",
			info->name, strerror(errno));
		exit(1);
	}
	if (ioctl(info->fd, DIOCGPART, &pinfo) < 0) {
		/*
		 * Allow the formatting of regular filews as HAMMER volumes
		 */
		if (fstat(info->fd, &st) < 0) {
			fprintf(stderr, "Unable to stat %s: %s\n",
				info->name, strerror(errno));
			exit(1);
		}
		info->size = st.st_size;
		info->type = "REGFILE";
	} else {
		/*
		 * When formatting a block device as a HAMMER volume the
		 * sector size must be compatible.  HAMMER uses 16384 byte
		 * filesystem buffers.
		 */
		if (pinfo.reserved_blocks) {
			fprintf(stderr, "HAMMER cannot be placed in a partition which overlaps the disklabel or MBR\n");
			exit(1);
		}
		if (pinfo.media_blksize > 16384 ||
		    16384 % pinfo.media_blksize) {
			fprintf(stderr, "A media sector size of %d is not supported\n", pinfo.media_blksize);
			exit(1);
		}

		info->size = pinfo.media_size;
		info->type = "DEVICE";
	}
	printf("volume %d %s %-15s size %s\n",
	       info->volno, info->type, info->name,
	       sizetostr(info->size));
}

/*
 * Format a HAMMER volume.  Cluster 0 will be initially placed in volume 0.
 */
static
void
format_volume(struct vol_info *info, int nvols, uuid_t *fsid,
	      const char *label, int32_t clsize)
{
	struct hammer_volume_ondisk *vol;
	int32_t nclusters;
	int32_t minclsize;

	minclsize = clsize / 2;

	vol = allocbuffer(HAMMER_FSBUF_VOLUME);

	vol->vol_fsid = *fsid;
	vol->vol_fstype = Hammer_FSType;
	snprintf(vol->vol_name, sizeof(vol->vol_name), "%s", label);
	vol->vol_no = info->volno;
	vol->vol_count = nvols;
	vol->vol_version = 1;
	vol->vol_clsize = clsize;
	vol->vol_flags = 0;

	vol->vol_beg = HAMMER_BUFSIZE;
	vol->vol_end = info->size;

	if (vol->vol_end < vol->vol_beg) {
		printf("volume %d %s is too small to hold the volume header\n",
		       info->volno, info->name);
		exit(1);
	}

	/*
	 * Out-of-band volume alist - manage clusters within the volume.
	 */
	hammer_alist_init(&Vol_alist, vol->vol_almeta);
	nclusters = (vol->vol_end - vol->vol_beg + minclsize) / clsize;
	if (nclusters > 32768) {
		fprintf(stderr, "Volume is too large, max %s\n",
			sizetostr((int64_t)nclusters * clsize));
		exit(1);
		/*nclusters = 32768;*/
	}
	hammer_alist_free(&Vol_alist, vol->vol_almeta, 0, nclusters);

	/*
	 * Place the root cluster in volume 0.
	 */
	vol->vol_rootvol = 0;
	if (info->volno == vol->vol_rootvol) {
		vol->vol0_rootcluster = format_cluster(info, vol, 1);
		vol->vol0_recid = 1;
		/* global next TID */
		vol->vol0_nexttid = createtid();
	}

	/*
	 * Generate the CRC and write out the volume header
	 */
	writebuffer(info, 0, &vol->head);
}

/*
 * Format a hammer cluster.  Returns byte offset in volume of cluster.
 */
static
int32_t
format_cluster(struct vol_info *info, struct hammer_volume_ondisk *vol,
	       int isroot)
{
	struct hammer_cluster_ondisk *cl;
	union hammer_record rec;
	struct hammer_fsbuf_recs *recbuf;
	struct hammer_fsbuf_btree *idxbuf;
	struct hammer_fsbuf_data *databuf;
	int clno;
	int nbuffers;
	int64_t cloff;
	int64_t idxoff;
	int64_t recoff;
	int64_t dataoff;
	hammer_tid_t clu_id = createtid();

	clno = hammer_alist_alloc(&Vol_alist, vol->vol_almeta, 1);
	if (clno == HAMMER_ALIST_BLOCK_NONE) {
		fprintf(stderr, "volume %d %s has insufficient space\n",
			info->volno, info->name);
		exit(1);
	}
	cloff = vol->vol_beg + clno * vol->vol_clsize;
	printf("allocate cluster id=%016llx %d@%08llx\n", clu_id, clno, cloff);

	bzero(&rec, sizeof(rec));
	cl = allocbuffer(HAMMER_FSBUF_CLUSTER);

	cl->vol_fsid = vol->vol_fsid;
	cl->vol_fstype = vol->vol_fstype;
	cl->clu_gen = 1;
	cl->clu_id = clu_id;
	cl->clu_no = clno;
	cl->clu_flags = 0;
	cl->clu_start = HAMMER_BUFSIZE;
	if (info->size - cloff > vol->vol_clsize)
		cl->clu_limit = vol->vol_clsize;
	else
		cl->clu_limit = (u_int32_t)(info->size - cloff);

	/*
	 * In-band filesystem buffer management A-List.  The first filesystem
	 * buffer is the cluster header itself.
	 */
	hammer_alist_init(&Clu_alist, cl->clu_almeta);
	nbuffers = cl->clu_limit / HAMMER_BUFSIZE;
	hammer_alist_free(&Clu_alist, cl->clu_almeta, 1, nbuffers - 1);

	/*
	 * Buffer Iterators
	 */
	cl->idx_data = 1;
	cl->idx_index = 0;
	cl->idx_record = nbuffers - 1;
	cl->clu_parent = 0;	/* we are the root cluster (vol,cluster) */

	/*
	 * Allocate a buffer for the B-Tree index and another to hold
	 * records.  The B-Tree buffer isn't strictly required since
	 * the only records we allocate comfortably fit in cl->clu_btree_root
	 * but do it anyway to guarentee some locality of reference.
	 */
	idxbuf = allocindexbuffer(info, vol, cl, cloff, &idxoff);
	recbuf = allocrecbuffer(info, vol, cl, cloff, &recoff);
	databuf = allocdatabuffer(info, vol, cl, cloff, &dataoff);

	/*
	 * Cluster 0 is the root cluster.  Set the B-Tree range for this
	 * cluster to the entire key space and format the root directory. 
	 */
	if (clu_id == 0) {
		cl->clu_objstart.obj_id = -0x8000000000000000LL;
		cl->clu_objstart.key = -0x8000000000000000LL;
		cl->clu_objstart.create_tid = 0;
		cl->clu_objstart.delete_tid = 0;
		cl->clu_objstart.rec_type = 0;
		cl->clu_objstart.obj_type = 0;

		cl->clu_objend.obj_id = 0x7FFFFFFFFFFFFFFFLL;
		cl->clu_objend.key = 0x7FFFFFFFFFFFFFFFLL;
		cl->clu_objend.create_tid = 0xFFFFFFFFFFFFFFFFULL;
		cl->clu_objend.delete_tid = 0xFFFFFFFFFFFFFFFFULL;
		cl->clu_objend.rec_type = 0xFFFFU;
		cl->clu_objend.obj_type = 0xFFFFU;

		format_root(info, vol, cl, recbuf, databuf,
			    (int)(recoff - cloff), (int)(dataoff - cloff));
	}

	/*
	 * Write-out and update the index, record, and cluster buffers
	 */
	writebuffer(info, idxoff, &idxbuf->head);
	writebuffer(info, recoff, &recbuf->head);
	writebuffer(info, dataoff, &databuf->head);
	writebuffer(info, cloff, &cl->head);
	return(clno);
}

/*
 * Format the root directory.  Basically we just lay down the inode record
 * and create a degenerate entry in the cluster's root btree.
 *
 * There is no '.' or '..'.
 */
static
void
format_root(struct vol_info *info, struct hammer_volume_ondisk *vol,
	    struct hammer_cluster_ondisk *cl, struct hammer_fsbuf_recs *recbuf,
	    struct hammer_fsbuf_data *databuf, int recoff, int dataoff)
{
	struct hammer_inode_record *record;
	struct hammer_inode_data *inode_data;
	struct hammer_leaf_elm *elm;
	int recblk;
	int inodeblk;

	/*
	 * Allocate record and data space and calculate cluster-relative
	 * offsets for intra-cluster references.
	 */
	recblk = hammer_alist_alloc(&Buf_alist, recbuf->head.buf_almeta, 1);
	assert(recblk != HAMMER_ALIST_BLOCK_NONE);
	inodeblk = hammer_alist_alloc(&Buf_alist, databuf->head.buf_almeta, 1);
	assert(inodeblk != HAMMER_ALIST_BLOCK_NONE);
	assert(sizeof(*inode_data) <= HAMMER_DATA_BLKSIZE);

	record = &recbuf->recs[recblk].inode;
	recoff += offsetof(struct hammer_fsbuf_recs, recs[recblk]);

	inode_data = (void *)databuf->data[inodeblk];
	dataoff += offsetof(struct hammer_fsbuf_data, data[inodeblk][0]);

	/*
	 * Populate the inode data and inode record for the root directory.
	 */
	inode_data->version = HAMMER_INODE_DATA_VERSION;
	inode_data->mode = 0755;

	record->base.obj_id = 1;
	record->base.key = 0;
	record->base.create_tid = createtid();
	record->base.delete_tid = 0;
	record->base.rec_type = HAMMER_RECTYPE_INODE;
	record->base.obj_type = HAMMER_OBJTYPE_DIRECTORY;
	record->base.data_offset = dataoff;
	record->base.data_len = sizeof(*inode_data);
	record->base.data_crc = crc32(inode_data, sizeof(*inode_data));
	record->ino_atime  = record->base.create_tid;
	record->ino_mtime  = record->base.create_tid;
	record->ino_size   = 0;
	record->ino_nlinks = 1;

	/*
	 * Insert the record into the B-Tree.  The B-Tree is empty so just
	 * install the record manually.
	 */
	assert(cl->clu_btree_root.count == 0);
	cl->clu_btree_root.count = 1;
	elm = &cl->clu_btree_root.elms[0].leaf;
	elm->base.obj_id = record->base.obj_id;
	elm->base.key = record->base.key;
	elm->base.create_tid = record->base.create_tid;
	elm->base.delete_tid = record->base.delete_tid;
	elm->base.rec_type = record->base.rec_type;
	elm->base.obj_type = record->base.obj_type;

	elm->rec_offset = recoff;
	elm->data_offset = record->base.data_offset;
	elm->data_len = record->base.data_len;
	elm->data_crc = record->base.data_crc;
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

