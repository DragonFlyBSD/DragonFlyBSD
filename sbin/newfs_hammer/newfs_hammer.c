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
 * $DragonFly: src/sbin/newfs_hammer/newfs_hammer.c,v 1.6 2007/11/19 00:53:39 dillon Exp $
 */

#include "newfs_hammer.h"

static int64_t getsize(const char *str, int64_t minval, int64_t maxval, int pw);
static const char *sizetostr(off_t size);
static void check_volume(struct volume_info *vol);
static void format_volume(struct volume_info *vol, int nvols,const char *label);
static int32_t format_cluster(struct volume_info *vol, int isroot);
static void format_root(struct cluster_info *cluster);
static void usage(void);

struct hammer_alist_config Buf_alist_config;
struct hammer_alist_config Vol_normal_alist_config;
struct hammer_alist_config Vol_super_alist_config;
struct hammer_alist_config Supercl_alist_config;
struct hammer_alist_config Clu_master_alist_config;
struct hammer_alist_config Clu_slave_alist_config;
uuid_t Hammer_FSType;
uuid_t Hammer_FSId;
int32_t ClusterSize;
int	UsingSuperClusters;
int	NumVolumes;
struct volume_info *VolBase;

int
main(int ac, char **av)
{
	int i;
	int ch;
	u_int32_t status;
	off_t total;
	int64_t max_volume_size;
	const char *label = NULL;

	/*
	 * Sanity check basic filesystem structures.  No cookies for us
	 * if it gets broken!
	 */
	assert(sizeof(struct hammer_almeta) == HAMMER_ALMETA_SIZE);
	assert(sizeof(struct hammer_fsbuf_head) == HAMMER_FSBUF_HEAD_SIZE);
	assert(sizeof(struct hammer_volume_ondisk) <= HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_cluster_ondisk) <= HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_fsbuf_data) == HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_fsbuf_recs) == HAMMER_BUFSIZE);
	assert(sizeof(struct hammer_fsbuf_btree) == HAMMER_BUFSIZE);
	assert(sizeof(union hammer_fsbuf_ondisk) == HAMMER_BUFSIZE);

	/*
	 * Generate a filesysem id and lookup the filesystem type
	 */
	uuidgen(&Hammer_FSId, 1);
	uuid_name_lookup(&Hammer_FSType, "DragonFly HAMMER", &status);
	if (status != uuid_s_ok) {
		errx(1, "uuids file does not have the DragonFly "
			"HAMMER filesystem type");
	}

	/*
	 * Initialize the alist templates we will be using
	 */
	hammer_alist_template(&Buf_alist_config, HAMMER_FSBUF_MAXBLKS,
			      1, HAMMER_FSBUF_METAELMS);
	hammer_alist_template(&Vol_normal_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_VOL_METAELMS_1LYR);
	hammer_alist_template(&Vol_super_alist_config,
			      HAMMER_VOL_MAXSUPERCLUSTERS,
			      HAMMER_SCL_MAXCLUSTERS, HAMMER_VOL_METAELMS_2LYR);
	hammer_super_alist_template(&Vol_super_alist_config);
	hammer_alist_template(&Supercl_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_SUPERCL_METAELMS);
	hammer_alist_template(&Clu_master_alist_config, HAMMER_CLU_MAXBUFFERS,
			      1, HAMMER_CLU_MASTER_METAELMS);
	hammer_alist_template(&Clu_slave_alist_config, HAMMER_CLU_MAXBUFFERS,
			      HAMMER_FSBUF_MAXBLKS, HAMMER_CLU_SLAVE_METAELMS);
	hammer_buffer_alist_template(&Clu_slave_alist_config);

	/*
	 * Parse arguments
	 */
	while ((ch = getopt(ac, av, "L:c:S")) != -1) {
		switch(ch) {
		case 'L':
			label = optarg;
			break;
		case 'c':
			ClusterSize = getsize(optarg, 
					 HAMMER_BUFSIZE * 256LL,
					 HAMMER_CLU_MAXBYTES, 1);
			break;
		case 'S':
			/*
			 * Force the use of super-clusters
			 */
			UsingSuperClusters = 1;
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
	NumVolumes = ac;

	total = 0;
	for (i = 0; i < NumVolumes; ++i) {
		struct volume_info *vol;

		vol = calloc(1, sizeof(struct volume_info));
		vol->fd = -1;
		vol->vol_no = i;
		vol->name = av[i];
		vol->next = VolBase;
		VolBase = vol;

		/*
		 * Load up information on the volume and initialize
		 * its remaining fields.
		 */
		check_volume(vol);
		total += vol->size;
	}

	/*
	 * Calculate the size of a cluster.  A cluster is broken
	 * down into 256 chunks which must be at least filesystem buffer
	 * sized.  This gives us a minimum chunk size of around 4MB.
	 */
	if (ClusterSize == 0) {
		ClusterSize = HAMMER_BUFSIZE * 256;
		while (ClusterSize < total / NumVolumes / 256 &&
		       ClusterSize < HAMMER_CLU_MAXBYTES) {
			ClusterSize <<= 1;
		}
	}

	printf("---------------------------------------------\n");
	printf("%d volume%s total size %s\n",
		NumVolumes, (NumVolumes == 1 ? "" : "s"), sizetostr(total));
	printf("cluster-size:        %s\n", sizetostr(ClusterSize));

	if (UsingSuperClusters) {
		max_volume_size = (int64_t)HAMMER_VOL_MAXSUPERCLUSTERS * \
				  HAMMER_SCL_MAXCLUSTERS * ClusterSize;
	} else {
		max_volume_size = (int64_t)HAMMER_VOL_MAXCLUSTERS * ClusterSize;
	}
	printf("max-volume-size:     %s\n", sizetostr(max_volume_size));

	printf("max-filesystem-size: %s\n",
	       (max_volume_size * 32768LL < max_volume_size) ?
	       "Unlimited" :
	       sizetostr(max_volume_size * 32768LL));
	printf("\n");

	/*
	 * Format the volumes.
	 */
	for (i = 0; i < NumVolumes; ++i) {
		format_volume(get_volume(i), NumVolumes, label);
	}
	flush_all_volumes();
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

/*
 * Convert a string to a 64 bit signed integer with various requirements.
 */
static int64_t
getsize(const char *str, int64_t minval, int64_t maxval, int powerof2)
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
		errx(1, "Unknown suffix in number '%s'\n", str);
		/* not reached */
	}
	if (ptr[1]) {
		errx(1, "Unknown suffix in number '%s'\n", str);
		/* not reached */
	}
	if (val < minval) {
		errx(1, "Value too small: %s, min is %s\n",
		     str, sizetostr(minval));
		/* not reached */
	}
	if (val > maxval) {
		errx(1, "Value too large: %s, max is %s\n",
		     str, sizetostr(maxval));
		/* not reached */
	}
	if (powerof2 && (val ^ (val - 1)) != ((val << 1) - 1)) {
		errx(1, "Value not power of 2: %s\n", str);
		/* not reached */
	}
	return(val);
}

/*
 * Generate a transaction id
 */
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

/*
 * Check basic volume characteristics.  HAMMER filesystems use a minimum
 * of a 16KB filesystem buffer size.
 */
static
void
check_volume(struct volume_info *vol)
{
	struct partinfo pinfo;
	struct stat st;

	/*
	 * Get basic information about the volume
	 */
	vol->fd = open(vol->name, O_RDWR);
	if (vol->fd < 0)
		err(1, "Unable to open %s R+W", vol->name);
	if (ioctl(vol->fd, DIOCGPART, &pinfo) < 0) {
		/*
		 * Allow the formatting of regular filews as HAMMER volumes
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
		if (pinfo.media_blksize > 16384 ||
		    16384 % pinfo.media_blksize) {
			errx(1, "A media sector size of %d is not supported",
			     pinfo.media_blksize);
		}

		vol->size = pinfo.media_size;
		vol->type = "DEVICE";
	}
	printf("Volume %d %s %-15s size %s\n",
	       vol->vol_no, vol->type, vol->name,
	       sizetostr(vol->size));

	/*
	 * Strictly speaking we do not need to enable super clusters unless
	 * we have volumes > 2TB, but turning them on doesn't really hurt
	 * and if we don't the user may get confused if he tries to expand
	 * the size of an existing volume.
	 */
	if (vol->size > 200LL * 1024 * 1024 * 1024 && !UsingSuperClusters) {
		UsingSuperClusters = 1;
		printf("Enabling super-clusters\n");
	}

	/*
	 * Reserve space for (future) boot junk
	 */
	vol->vol_cluster_off = HAMMER_BUFSIZE * 16;
}

/*
 * Format a HAMMER volume.  Cluster 0 will be initially placed in volume 0.
 */
static
void
format_volume(struct volume_info *vol, int nvols, const char *label)
{
	struct hammer_volume_ondisk *ondisk;
	int32_t nclusters;
	int32_t minclsize;
	int32_t nscl_groups;
	int64_t scl_group_size;
	int64_t scl_header_size;
	int64_t n64;

	/*
	 * The last cluster in a volume may wind up truncated.  It must be
	 * at least minclsize to really be workable as a cluster.
	 */
	minclsize = ClusterSize / 4;
	if (minclsize < HAMMER_BUFSIZE * 64)
		minclsize = HAMMER_BUFSIZE * 64;

	/*
	 * Initialize basic information in the on-disk volume structure.
	 */
	ondisk = vol->ondisk;

	ondisk->vol_fsid = Hammer_FSId;
	ondisk->vol_fstype = Hammer_FSType;
	snprintf(ondisk->vol_name, sizeof(ondisk->vol_name), "%s", label);
	ondisk->vol_no = vol->vol_no;
	ondisk->vol_count = nvols;
	ondisk->vol_version = 1;
	ondisk->vol_clsize = ClusterSize;
	if (UsingSuperClusters)
		ondisk->vol_flags = HAMMER_VOLF_USINGSUPERCL;

	ondisk->vol_beg = vol->vol_cluster_off;
	ondisk->vol_end = vol->size;

	if (ondisk->vol_end < ondisk->vol_beg) {
		errx(1, "volume %d %s is too small to hold the volume header",
		     vol->vol_no, vol->name);
	}

	/*
	 * Our A-lists have been initialized but are marked all-allocated.
	 * Calculate the actual number of clusters in the volume and free
	 * them to get the filesystem ready for work.  The clusters will
	 * be initialized on-demand.
	 *
	 * If using super-clusters we must still calculate nclusters but
	 * we only need to initialize superclusters that are not going
	 * to wind up in the all-free state, which will only be the last
	 * supercluster.  hammer_alist_free() will recurse into the
	 * supercluster infrastructure and create the necessary superclusters.
	 *
	 * NOTE: The nclusters calculation ensures that the volume EOF does
	 * not occur in the middle of a supercluster buffer array.
	 */
	if (UsingSuperClusters) {
		/*
		 * Figure out how many full super-cluster groups we will have.
		 * This calculation does not include the partial supercluster
		 * group at the end.
		 */
		scl_header_size = (int64_t)HAMMER_BUFSIZE *
				  HAMMER_VOL_SUPERCLUSTER_GROUP;
		scl_group_size = scl_header_size +
				 (int64_t)HAMMER_VOL_SUPERCLUSTER_GROUP *
				 ClusterSize * HAMMER_SCL_MAXCLUSTERS;
		nscl_groups = (ondisk->vol_end - ondisk->vol_beg) /
				scl_group_size;
		nclusters = nscl_groups * HAMMER_SCL_MAXCLUSTERS *
				HAMMER_VOL_SUPERCLUSTER_GROUP;

		/*
		 * Figure out how much space we have left and calculate the
		 * remaining number of clusters.
		 */
		n64 = (ondisk->vol_end - ondisk->vol_beg) -
			(nscl_groups * scl_group_size);
		if (n64 > scl_header_size) {
			nclusters += (n64 + minclsize) / ClusterSize;
		}
		printf("%d clusters, %d full super-cluster groups\n",
			nclusters, nscl_groups);
		hammer_alist_free(&vol->clu_alist, 0, nclusters);
	} else {
		nclusters = (ondisk->vol_end - ondisk->vol_beg + minclsize) /
			    ClusterSize;
		if (nclusters > HAMMER_VOL_MAXCLUSTERS) {
			errx(1, "Volume is too large, max %s\n",
			     sizetostr((int64_t)nclusters * ClusterSize));
		}
		hammer_alist_free(&vol->clu_alist, 0, nclusters);
	}
	ondisk->vol_nclusters = nclusters;

	/*
	 * Place the root cluster in volume 0.
	 */
	ondisk->vol_rootvol = 0;
	if (ondisk->vol_no == ondisk->vol_rootvol) {
		ondisk->vol0_root_clu_id = format_cluster(vol, 1);
		ondisk->vol0_recid = 1;
		/* global next TID */
		ondisk->vol0_nexttid = createtid();
	}
}

/*
 * Format a hammer cluster.  Returns byte offset in volume of cluster.
 */
static
int32_t
format_cluster(struct volume_info *vol, int isroot)
{
	hammer_tid_t clu_id = createtid();
	struct cluster_info *cluster;
	struct hammer_cluster_ondisk *ondisk;
	int nbuffers;
	int clno;

	/*
	 * Allocate a cluster
	 */
	clno = hammer_alist_alloc(&vol->clu_alist, 1);
	if (clno == HAMMER_ALIST_BLOCK_NONE) {
		fprintf(stderr, "volume %d %s has insufficient space\n",
			vol->vol_no, vol->name);
		exit(1);
	}
	cluster = get_cluster(vol, clno);
	printf("allocate cluster id=%016llx %d@%08llx\n",
	       clu_id, clno, cluster->clu_offset);

	ondisk = cluster->ondisk;

	ondisk->vol_fsid = vol->ondisk->vol_fsid;
	ondisk->vol_fstype = vol->ondisk->vol_fstype;
	ondisk->clu_gen = 1;
	ondisk->clu_id = clu_id;
	ondisk->clu_no = clno;
	ondisk->clu_flags = 0;
	ondisk->clu_start = HAMMER_BUFSIZE;
	if (vol->size - cluster->clu_offset > ClusterSize)
		ondisk->clu_limit = ClusterSize;
	else
		ondisk->clu_limit = (u_int32_t)(vol->size - cluster->clu_offset);

	/*
	 * In-band filesystem buffer management A-List.  The first filesystem
	 * buffer is the cluster header itself.
	 */
	nbuffers = ondisk->clu_limit / HAMMER_BUFSIZE;
	hammer_alist_free(&cluster->alist_master, 1, nbuffers - 1);
	printf("cluster %d has %d buffers\n", cluster->clu_no, nbuffers);

	/*
	 * Buffer Iterators in elements.  Each buffer has 256 elements.
	 * The data and B-Tree indices are forward allocations while the
	 * record index allocates backwards.
	 */
	ondisk->idx_data = 1 * HAMMER_FSBUF_MAXBLKS;
	ondisk->idx_index = 0 * HAMMER_FSBUF_MAXBLKS;
	ondisk->idx_record = nbuffers * HAMMER_FSBUF_MAXBLKS;

	/*
	 * Initialize root cluster's parent cluster info.  -1's
	 * indicate we are the root cluster and no parent exists.
	 */
	ondisk->clu_btree_parent_vol_no = -1;
	ondisk->clu_btree_parent_clu_no = -1;
	ondisk->clu_btree_parent_offset = -1;
	ondisk->clu_btree_parent_clu_gen = -1;

	/*
	 * Cluster 0 is the root cluster.  Set the B-Tree range for this
	 * cluster to the entire key space and format the root directory. 
	 *
	 * Note that delete_tid for the ending range must be set to 0,
	 * 0 indicates 'not deleted', aka 'the most recent'.  See
	 * hammer_btree_cmp() in sys/vfs/hammer/hammer_btree.c.
	 *
	 * The root cluster's key space represents the entire key space for
	 * the filesystem.  The btree_end element appears to be inclusive
	 * only because we can't overflow our variables.  It's actually
	 * non-inclusive... that is, it is a right-side boundary element.
	 */
	if (isroot) {
		ondisk->clu_btree_beg.obj_id = -0x8000000000000000LL;
		ondisk->clu_btree_beg.key = -0x8000000000000000LL;
		ondisk->clu_btree_beg.create_tid = 0;
		ondisk->clu_btree_beg.delete_tid = 0;
		ondisk->clu_btree_beg.rec_type = 0;
		ondisk->clu_btree_beg.obj_type = 0;

		ondisk->clu_btree_end.obj_id = 0x7FFFFFFFFFFFFFFFLL;
		ondisk->clu_btree_end.key = 0x7FFFFFFFFFFFFFFFLL;
		ondisk->clu_btree_end.create_tid = 0xFFFFFFFFFFFFFFFFULL;
		ondisk->clu_btree_end.delete_tid = 0;	/* special case */
		ondisk->clu_btree_end.rec_type = 0xFFFFU;
		ondisk->clu_btree_end.obj_type = 0;

		format_root(cluster);
	}

	/*
	 * Write-out and update the index, record, and cluster buffers
	 */
	return(clno);
}

/*
 * Format the root directory.
 */
static
void
format_root(struct cluster_info *cluster)
{
	int32_t btree_off;
	int32_t rec_off;
	int32_t data_off;
	hammer_node_ondisk_t bnode;
	union hammer_record_ondisk *rec;
	struct hammer_inode_data *idata;
	hammer_btree_elm_t elm;

	bnode = alloc_btree_element(cluster, &btree_off);
	rec = alloc_record_element(cluster, &rec_off);
	idata = alloc_data_element(cluster, sizeof(*idata), &data_off);

	/*
	 * Populate the inode data and inode record for the root directory.
	 */
	idata->version = HAMMER_INODE_DATA_VERSION;
	idata->mode = 0755;

	rec->base.base.obj_id = 1;
	rec->base.base.key = 0;
	rec->base.base.create_tid = createtid();
	rec->base.base.delete_tid = 0;
	rec->base.base.rec_type = HAMMER_RECTYPE_INODE;
	rec->base.base.obj_type = HAMMER_OBJTYPE_DIRECTORY;
	rec->base.data_offset = data_off;
	rec->base.data_len = sizeof(*idata);
	rec->base.data_crc = crc32(idata, sizeof(*idata));
	rec->inode.ino_atime  = rec->base.base.create_tid;
	rec->inode.ino_mtime  = rec->base.base.create_tid;
	rec->inode.ino_size   = 0;
	rec->inode.ino_nlinks = 1;

	/*
	 * Assign the cluster's root B-Tree node.
	 */
	assert(cluster->ondisk->clu_btree_root == 0);
	cluster->ondisk->clu_btree_root = btree_off;

	/*
	 * Create the root of the B-Tree.  The root is a leaf node so we
	 * do not have to worry about boundary elements.
	 */
	bnode->count = 1;
	bnode->type = HAMMER_BTREE_TYPE_LEAF;

	elm = &bnode->elms[0];
	elm->base = rec->base.base;
	elm->leaf.rec_offset = rec_off;
	elm->leaf.data_offset = rec->base.data_offset;
	elm->leaf.data_len = rec->base.data_len;
	elm->leaf.data_crc = rec->base.data_crc;
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

