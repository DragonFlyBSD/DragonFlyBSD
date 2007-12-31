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
 * $DragonFly: src/sys/vfs/hammer/hammer_disk.h,v 1.14 2007/12/31 05:33:12 dillon Exp $
 */

#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif

/*
 * The structures below represent the on-disk format for a HAMMER
 * filesystem.  Note that all fields for on-disk structures are naturally
 * aligned.  The host endian format is used - compatibility is possible
 * if the implementation detects reversed endian and adjusts data accordingly.
 *
 * Most of HAMMER revolves around the concept of an object identifier.  An
 * obj_id is a 64 bit quantity which uniquely identifies a filesystem object
 * FOR THE ENTIRE LIFE OF THE FILESYSTEM.  This uniqueness allows backups
 * and mirrors to retain varying amounts of filesystem history by removing
 * any possibility of conflict through identifier reuse.
 *
 * A HAMMER filesystem may spam multiple volumes.
 *
 * A HAMMER filesystem uses a 16K filesystem buffer size.  All filesystem
 * I/O is done in multiples of 16K.  Most buffer-sized headers such as those
 * used by volumes, super-clusters, clusters, and basic filesystem buffers
 * use fixed-sized A-lists which are heavily dependant on HAMMER_BUFSIZE.
 */
#define HAMMER_BUFSIZE	16384
#define HAMMER_BUFMASK	(HAMMER_BUFSIZE - 1)

/*
 * Hammer transction ids are 64 bit unsigned integers and are usually
 * synchronized with the time of day in nanoseconds.
 */
typedef u_int64_t hammer_tid_t;

#define HAMMER_MAX_TID	0xFFFFFFFFFFFFFFFFULL
#define HAMMER_MIN_KEY	-0x8000000000000000LL
#define HAMMER_MAX_KEY	0x7FFFFFFFFFFFFFFFLL

/*
 * Most HAMMER data structures are embedded in 16K filesystem buffers.
 * All filesystem buffers except those designated as pure-data buffers
 * contain this 128-byte header.
 *
 * This structure contains an embedded A-List used to manage space within
 * the filesystem buffer.  It is not used by volume or cluster header
 * buffers, or by pure-data buffers.  The granularity is variable and
 * depends on the type of filesystem buffer.  BLKSIZE is just a minimum.
 */

#define HAMMER_FSBUF_HEAD_SIZE	128
#define HAMMER_FSBUF_MAXBLKS	256
#define HAMMER_FSBUF_BLKMASK	(HAMMER_FSBUF_MAXBLKS - 1)
#define HAMMER_FSBUF_METAELMS	HAMMER_ALIST_METAELMS_256_1LYR	/* 11 */

struct hammer_fsbuf_head {
	u_int64_t buf_type;
	u_int32_t buf_crc;
	u_int32_t buf_reserved07;
	u_int32_t reserved[6];
	struct hammer_almeta buf_almeta[HAMMER_FSBUF_METAELMS];
};

typedef struct hammer_fsbuf_head *hammer_fsbuf_head_t;

/*
 * Note: Pure-data buffers contain pure-data and have no buf_type.
 * Piecemeal data buffers do have a header and use HAMMER_FSBUF_DATA.
 */
#define HAMMER_FSBUF_VOLUME	0xC8414D4DC5523031ULL	/* HAMMER01 */
#define HAMMER_FSBUF_SUPERCL	0xC8414D52C3555052ULL	/* HAMRSUPR */
#define HAMMER_FSBUF_CLUSTER	0xC8414D52C34C5553ULL	/* HAMRCLUS */
#define HAMMER_FSBUF_RECORDS	0xC8414D52D2454353ULL	/* HAMRRECS */
#define HAMMER_FSBUF_BTREE	0xC8414D52C2545245ULL	/* HAMRBTRE */
#define HAMMER_FSBUF_DATA	0xC8414D52C4415441ULL	/* HAMRDATA */

#define HAMMER_FSBUF_VOLUME_REV	0x313052C54D4D41C8ULL	/* (reverse endian) */

/*
 * The B-Tree structures need hammer_fsbuf_head.
 */
#include "hammer_btree.h"

/*
 * HAMMER Volume header
 *
 * A HAMMER filesystem is built from any number of block devices,  Each block
 * device contains a volume header followed by however many super-clusters
 * and clusters fit into the volume.  Clusters cannot be migrated but the
 * data they contain can, so HAMMER can use a truncated cluster for any
 * extra space at the end of the volume.
 *
 * The volume containing the root cluster is designated as the master volume.
 * The root cluster designation can be moved to any volume.
 *
 * The volume header takes up an entire 16K filesystem buffer and includes
 * a one or two-layered A-list to manage the clusters making up the volume.
 * A volume containing up to 32768 clusters (2TB) can be managed with a
 * single-layered A-list.  A two-layer A-list is capable of managing up
 * to 4096 super-clusters with each super-cluster containing 32768 clusters
 * (8192 TB per volume total).  The number of volumes is limited to 32768
 * but it only takes 512 to fill out a 64 bit address space so for all
 * intents and purposes the filesystem has no limits.
 *
 * cluster addressing within a volume depends on whether a single or
 * duel-layer A-list is used.  If a duel-layer A-list is used a 16K
 * super-cluster buffer is needed for every 32768 clusters in the volume.
 * However, because the A-list's hinting is grouped in multiples of 16
 * we group 16 super-cluster buffers together (starting just after the
 * volume header), followed by 16384x16 clusters, and repeat.
 *
 * The number of super-clusters is limited to 4096 because the A-list's
 * master radix is stored as a 32 bit signed quantity which will overflow
 * if more then 4096*32768 elements is specified.  XXX
 *
 * NOTE: A 32768-element single-layer and 16384-element duel-layer A-list
 * is the same size.
 *
 * Special field notes:
 *
 *	vol_bot_beg - offset of boot area (mem_beg - bot_beg bytes)
 *	vol_mem_beg - offset of memory log (clu_beg - mem_beg bytes)
 *	vol_clo_beg - offset of cluster #0 in volume
 *
 *	The memory log area allows a kernel to cache new records and data
 *	in memory without allocating space in the actual filesystem to hold
 *	the records and data.  In the event that a filesystem becomes full,
 *	any records remaining in memory can be flushed to the memory log
 *	area.  This allows the kernel to immediately return success.
 */
#define HAMMER_VOL_MAXCLUSTERS		32768	/* 1-layer */
#define HAMMER_VOL_MAXSUPERCLUSTERS	4096	/* 2-layer */
#define HAMMER_VOL_SUPERCLUSTER_GROUP	16
#define HAMMER_VOL_METAELMS_1LYR	HAMMER_ALIST_METAELMS_32K_1LYR
#define HAMMER_VOL_METAELMS_2LYR	HAMMER_ALIST_METAELMS_16K_2LYR

#define HAMMER_BOOT_MINBYTES		(32*1024)
#define HAMMER_BOOT_NOMBYTES		(64LL*1024*1024)
#define HAMMER_BOOT_MAXBYTES		(256LL*1024*1024)

#define HAMMER_MEM_MINBYTES		(256*1024)
#define HAMMER_MEM_NOMBYTES		(1LL*1024*1024*1024)
#define HAMMER_MEM_MAXBYTES		(64LL*1024*1024*1024)

struct hammer_volume_ondisk {
	struct hammer_fsbuf_head head;
	int64_t vol_bot_beg;	/* byte offset of boot area or 0 */
	int64_t vol_mem_beg;	/* byte offset of memory log or 0 */
	int64_t vol_clo_beg;	/* byte offset of first cl/supercl in volume */
	int64_t vol_clo_end;	/* byte offset of volume EOF */
	int64_t vol_locked;	/* reserved clusters are >= this offset */

	uuid_t    vol_fsid;	/* identify filesystem */
	uuid_t    vol_fstype;	/* identify filesystem type */
	char	  vol_name[64];	/* Name of volume */

	int32_t vol_no;		/* volume number within filesystem */
	int32_t vol_count;	/* number of volumes making up FS */

	u_int32_t vol_version;	/* version control information */
	u_int32_t vol_reserved01;
	u_int32_t vol_flags;	/* volume flags */
	u_int32_t vol_rootvol;	/* which volume is the root volume? */

	int32_t vol_clsize;	/* cluster size (same for all volumes) */
	int32_t vol_nclusters;
	u_int32_t vol_reserved06;
	u_int32_t vol_reserved07;

	int32_t vol_blocksize;		/* for statfs only */
	int64_t vol_nblocks;		/* total allocatable hammer bufs */

	/*
	 * This statistical information can get out of sync after a crash
	 * and is recovered slowly.
	 */
	int64_t	vol_stat_bytes;		/* for statfs only */
	int64_t unused08;		/* for statfs only */
	int64_t vol_stat_data_bufs;	/* hammer bufs allocated to data */
	int64_t vol_stat_rec_bufs;	/* hammer bufs allocated to records */
	int64_t vol_stat_idx_bufs;	/* hammer bufs allocated to B-Tree */

	/*
	 * These fields are initialized and space is reserved in every
	 * volume making up a HAMMER filesytem, but only the master volume
	 * contains valid data.
	 */
	int64_t	vol0_stat_bytes;	/* for statfs only */
	int64_t vol0_stat_inodes;	/* for statfs only */
	int64_t vol0_stat_data_bufs;	/* hammer bufs allocated to data */
	int64_t vol0_stat_rec_bufs;	/* hammer bufs allocated to records */
	int64_t vol0_stat_idx_bufs;	/* hammer bufs allocated to B-Tree */

	int32_t vol0_root_clu_no;	/* root cluster no (index) in rootvol */
	hammer_tid_t vol0_root_clu_id;	/* root cluster id */
	hammer_tid_t vol0_nexttid;	/* next TID */
	u_int64_t vol0_recid;		/* fs-wide record id allocator */
	u_int64_t vol0_synchronized_rec_id; /* XXX */

	char	reserved[1024];

	/*
	 * Meta elements for the volume header's A-list, which is either a
	 * 1-layer A-list capable of managing 32768 clusters, or a 2-layer
	 * A-list capable of managing 16384 super-clusters (each of which
	 * can handle 32768 clusters).
	 */
	union {
		struct hammer_almeta	super[HAMMER_VOL_METAELMS_2LYR];
		struct hammer_almeta	normal[HAMMER_VOL_METAELMS_1LYR];
	} vol_almeta;
	u_int32_t	vol0_bitmap[1024];
};

typedef struct hammer_volume_ondisk *hammer_volume_ondisk_t;

#define HAMMER_VOLF_VALID		0x0001	/* valid entry */
#define HAMMER_VOLF_OPEN		0x0002	/* volume is open */
#define HAMMER_VOLF_USINGSUPERCL	0x0004	/* using superclusters */

/*
 * HAMMER Super-cluster header
 *
 * A super-cluster is used to increase the maximum size of a volume.
 * HAMMER's volume header can manage up to 32768 direct clusters or
 * 16384 super-clusters.  Each super-cluster (which is basically just
 * a 16K filesystem buffer) can manage up to 32768 clusters.  So adding
 * a super-cluster layer allows a HAMMER volume to be sized upwards of
 * around 32768TB instead of 2TB.
 *
 * Any volume initially formatted to be over 32G reserves space for the layer
 * but the layer is only enabled if the volume exceeds 2TB.
 */
#define HAMMER_SUPERCL_METAELMS		HAMMER_ALIST_METAELMS_32K_1LYR
#define HAMMER_SCL_MAXCLUSTERS		HAMMER_VOL_MAXCLUSTERS

struct hammer_supercl_ondisk {
	struct hammer_fsbuf_head head;
	uuid_t	vol_fsid;	/* identify filesystem - sanity check */
	uuid_t	vol_fstype;	/* identify filesystem type - sanity check */
	int32_t reserved[1024];

	struct hammer_almeta	scl_meta[HAMMER_SUPERCL_METAELMS];
};

typedef struct hammer_supercl_ondisk *hammer_supercl_ondisk_t;

/*
 * HAMMER Cluster header
 *
 * A cluster is limited to 64MB and is made up of 4096 16K filesystem
 * buffers.  The cluster header contains four A-lists to manage these
 * buffers.
 *
 * master_alist - This is a non-layered A-list which manages pure-data
 *		  allocations and allocations on behalf of other A-lists.
 *
 * btree_alist  - This is a layered A-list which manages filesystem buffers
 *		  containing B-Tree nodes.
 *
 * record_alist - This is a layered A-list which manages filesystem buffers
 *		  containing records.
 *
 * mdata_alist  - This is a layered A-list which manages filesystem buffers
 *		  containing piecemeal record data.
 * 
 * General storage management works like this:  All the A-lists except the
 * master start in an all-allocated state.  Now lets say you wish to allocate
 * a B-Tree node out the btree_alist.  If the allocation fails you allocate
 * a pure data block out of master_alist and then free that  block in
 * btree_alist, thereby assigning more space to the btree_alist, and then
 * retry your allocation out of the btree_alist.  In the reverse direction,
 * filesystem buffers can be garbage collected back to master_alist simply
 * by doing whole-buffer allocations in btree_alist and then freeing the
 * space in master_alist.  The whole-buffer-allocation approach to garbage
 * collection works because A-list allocations are always power-of-2 sized
 * and aligned.
 */
#define HAMMER_CLU_MAXBUFFERS		4096
#define HAMMER_CLU_MASTER_METAELMS	HAMMER_ALIST_METAELMS_4K_1LYR
#define HAMMER_CLU_SLAVE_METAELMS	HAMMER_ALIST_METAELMS_4K_2LYR
#define HAMMER_CLU_MAXBYTES		(HAMMER_CLU_MAXBUFFERS * HAMMER_BUFSIZE)

struct hammer_cluster_ondisk {
	struct hammer_fsbuf_head head;
	uuid_t	vol_fsid;	/* identify filesystem - sanity check */
	uuid_t	vol_fstype;	/* identify filesystem type - sanity check */

	hammer_tid_t clu_id;	/* unique cluster self identification */
	hammer_tid_t clu_gen;	/* generation number */
	int32_t vol_no;		/* cluster contained in volume (sanity) */
	u_int32_t clu_flags;	/* cluster flags */

	int32_t clu_start;	/* start of data (byte offset) */
	int32_t clu_limit;	/* end of data (byte offset) */
	int32_t clu_no;		/* cluster index in volume (sanity) */
	u_int32_t clu_reserved03;

	u_int32_t clu_reserved04;
	u_int32_t clu_reserved05;
	u_int32_t clu_reserved06;
	u_int32_t clu_reserved07;

	/*
	 * These fields are heuristics to aid in locality of reference
	 * allocations.
	 */
	int32_t idx_data;	/* data append point (element no) */
	int32_t idx_index;	/* index append point (element no) */
	int32_t idx_record;	/* record prepend point (element no) */
	int32_t idx_ldata;	/* large block data append pt (buf_no) */

	/*
	 * These fields can become out of sync after a filesystem crash
	 * and are cleaned up in the background.  They are used for
	 * reporting only.
	 */
	int32_t stat_inodes;	/* number of inodes in cluster */
	int32_t stat_data_bufs; /* hammer bufs allocated to data */
	int32_t stat_rec_bufs;	/* hammer bufs allocated to records */
	int32_t stat_idx_bufs;	/* hammer bufs allocated to B-Tree */

	/* 
	 * Specify the range of information stored in this cluster as two
	 * btree elements.   These elements match the left and right
	 * boundary elements in the internal B-Tree node of the parent
	 * cluster that points to the root of our cluster.  Because these
	 * are boundary elements, the right boundary is range-NONinclusive.
	 */
	struct hammer_base_elm clu_btree_beg;
	struct hammer_base_elm clu_btree_end;

	/*
	 * The cluster's B-Tree root can change as a side effect of insertion
	 * and deletion operations so store an offset instead of embedding
	 * the root node.  The parent_offset is stale if the generation number
	 * does not match.
	 *
	 * Parent linkages are explicit.
	 */
	int32_t		clu_btree_root;
	int32_t		clu_btree_parent_vol_no;
	int32_t		clu_btree_parent_clu_no;
	int32_t		clu_btree_parent_offset;
	hammer_tid_t	clu_btree_parent_clu_gen;

	u_int64_t synchronized_rec_id;

	struct hammer_almeta	clu_master_meta[HAMMER_CLU_MASTER_METAELMS];
	struct hammer_almeta	clu_btree_meta[HAMMER_CLU_SLAVE_METAELMS];
	struct hammer_almeta	clu_record_meta[HAMMER_CLU_SLAVE_METAELMS];
	struct hammer_almeta	clu_mdata_meta[HAMMER_CLU_SLAVE_METAELMS];
};

typedef struct hammer_cluster_ondisk *hammer_cluster_ondisk_t;

#define HAMMER_CLUF_OPEN		0x0001	/* cluster is dirty */

/*
 * HAMMER records are 96 byte entities encoded into 16K filesystem buffers.
 * Each record has a 64 byte header and a 32 byte extension.  170 records
 * fit into each buffer.  Storage is managed by the buffer's A-List.
 *
 * Each record may have an explicit data reference to a block of data up
 * to 2^31-1 bytes in size within the current cluster.  Note that multiple
 * records may share the same or overlapping data references.
 */

/*
 * All HAMMER records have a common 64-byte base and a 32-byte extension.
 *
 * Many HAMMER record types reference out-of-band data within the cluster.
 * This data can also be stored in-band in the record itself if it is small
 * enough.  Either way, (data_offset, data_len) points to it.
 *
 * Key comparison order:  obj_id, rec_type, key, create_tid
 */
struct hammer_base_record {
	/*
	 * 40 byte base element info - same base as used in B-Tree internal
	 * and leaf node element arrays.
	 *
	 * Fields: obj_id, key, create_tid, delete_tid, rec_type, obj_type,
	 *	   reserved07.
	 */
	struct hammer_base_elm base; /* 00 base element info */

	int32_t data_len;	/* 28 size of data (remainder zero-fill) */
	u_int32_t data_crc;	/* 2C data sanity check */
	u_int64_t rec_id;	/* 30 record id (iterator for recovery) */
	int32_t	  data_offset;	/* 38 cluster-relative data reference or 0 */
	u_int32_t reserved07;	/* 3C */
				/* 40 */
};

/*
 * Record types are fairly straightforward.  The B-Tree includes the record
 * type in its index sort.
 *
 * In particular please note that it is possible to create a pseudo-
 * filesystem within a HAMMER filesystem by creating a special object
 * type within a directory.  Pseudo-filesystems are used as replication
 * targets and even though they are built within a HAMMER filesystem they
 * get their own obj_id space (and thus can serve as a replication target)
 * and look like a mount point to the system.
 *
 * Inter-cluster records are special-cased in the B-Tree.  These records
 * are referenced from a B-Tree INTERNAL node, NOT A LEAF.  This means
 * that the element in the B-Tree node is actually a boundary element whos
 * base element fields, including rec_type, reflect the boundary, NOT
 * the inter-cluster record type.
 *
 * HAMMER_RECTYPE_CLUSTER - only set in the actual inter-cluster record,
 * not set in the left or right boundary elements around the inter-cluster
 * reference of an internal node in the B-Tree (because doing so would
 * interfere with the boundary tests).
 *
 * NOTE: hammer_ip_delete_range_all() deletes all record types greater
 * then HAMMER_RECTYPE_INODE.
 */
#define HAMMER_RECTYPE_UNKNOWN		0
#define HAMMER_RECTYPE_LOWEST		1	/* lowest record type avail */
#define HAMMER_RECTYPE_INODE		1	/* inode in obj_id space */
#define HAMMER_RECTYPE_PSEUDO_INODE	2	/* pseudo filesysem */
#define HAMMER_RECTYPE_CLUSTER		3	/* inter-cluster reference */
#define HAMMER_RECTYPE_DATA		0x10
#define HAMMER_RECTYPE_DIRENTRY		0x11
#define HAMMER_RECTYPE_DB		0x12
#define HAMMER_RECTYPE_EXT		0x13	/* ext attributes */
#define HAMMER_RECTYPE_FIX		0x14	/* fixed attribute */

#define HAMMER_FIXKEY_SYMLINK		1

#define HAMMER_OBJTYPE_UNKNOWN		0	/* (never exists on-disk) */
#define HAMMER_OBJTYPE_DIRECTORY	1
#define HAMMER_OBJTYPE_REGFILE		2
#define HAMMER_OBJTYPE_DBFILE		3
#define HAMMER_OBJTYPE_FIFO		4
#define HAMMER_OBJTYPE_CDEV		5
#define HAMMER_OBJTYPE_BDEV		6
#define HAMMER_OBJTYPE_SOFTLINK		7
#define HAMMER_OBJTYPE_PSEUDOFS		8	/* pseudo filesystem obj */

/*
 * Generic full-sized record
 */
struct hammer_generic_record {
	struct hammer_base_record base;
	char filler[32];
};

/*
 * A HAMMER inode record.
 *
 * This forms the basis for a filesystem object.  obj_id is the inode number,
 * key1 represents the pseudo filesystem id for security partitioning
 * (preventing cross-links and/or restricting a NFS export and specifying the
 * security policy), and key2 represents the data retention policy id.
 *
 * Inode numbers are 64 bit quantities which uniquely identify a filesystem
 * object for the ENTIRE life of the filesystem, even after the object has
 * been deleted.  For all intents and purposes inode numbers are simply 
 * allocated by incrementing a sequence space.
 *
 * There is an important distinction between the data stored in the inode
 * record and the record's data reference.  The record references a
 * hammer_inode_data structure but the filesystem object size and hard link
 * count is stored in the inode record itself.  This allows multiple inodes
 * to share the same hammer_inode_data structure.  This is possible because
 * any modifications will lay out new data.  The HAMMER implementation need
 * not use the data-sharing ability when laying down new records.
 *
 * A HAMMER inode is subject to the same historical storage requirements
 * as any other record.  In particular any change in filesystem or hard link
 * count will lay down a new inode record when the filesystem is synced to
 * disk.  This can lead to a lot of junk records which get cleaned up by
 * the data retention policy.
 *
 * The ino_atime and ino_mtime fields are a special case.  Modifications to
 * these fields do NOT lay down a new record by default, though the values
 * are effectively frozen for snapshots which access historical versions
 * of the inode record due to other operations.  This means that atime will
 * not necessarily be accurate in snapshots, backups, or mirrors.  mtime
 * will be accurate in backups and mirrors since it can be regenerated from
 * the mirroring stream.
 *
 * Because nlinks is historically retained the hardlink count will be
 * accurate when accessing a HAMMER filesystem snapshot.
 */
struct hammer_inode_record {
	struct hammer_base_record base;
	u_int64_t ino_atime;	/* last access time (not historical) */
	u_int64_t ino_mtime;	/* last modified time (not historical) */
	u_int64_t ino_size;	/* filesystem object size */
	u_int64_t ino_nlinks;	/* hard links */
};

/*
 * Data records specify the entire contents of a regular file object,
 * including attributes.  Small amounts of data can theoretically be
 * embedded in the record itself but the use of this ability verses using
 * an out-of-band data reference depends on the implementation.
 */
struct hammer_data_record {
	struct hammer_base_record base;
	char filler[32];
};

/*
 * A directory entry specifies the HAMMER filesystem object id, a copy of
 * the file type, and file name (either embedded or as out-of-band data).
 * If the file name is short enough to fit into den_name[] (including a
 * terminating nul) then it will be embedded in the record, otherwise it
 * is stored out-of-band.  The base record's data reference always points
 * to the nul-terminated filename regardless.
 *
 * Directory entries are indexed with a 128 bit namekey rather then an
 * offset.  A portion of the namekey is an iterator or randomizer to deal
 * with collisions.
 *
 * NOTE: base.base.obj_type holds the filesystem object type of obj_id,
 *	 e.g. a den_type equivalent.
 *
 * NOTE: den_name / the filename data reference is NOT terminated with \0.
 *
 */
struct hammer_entry_record {
	struct hammer_base_record base;
	u_int64_t obj_id;		/* object being referenced */
	u_int64_t reserved01;
	char	  den_name[16];		/* short file names fit in record */
};

/*
 * Hammer rollup record
 */
union hammer_record_ondisk {
	struct hammer_base_record	base;
	struct hammer_generic_record	generic;
	struct hammer_inode_record	inode;
	struct hammer_data_record	data;
	struct hammer_entry_record	entry;
};

typedef union hammer_record_ondisk *hammer_record_ondisk_t;

/*
 * Filesystem buffer for records
 */
#define HAMMER_RECORD_NODES	\
	((HAMMER_BUFSIZE - sizeof(struct hammer_fsbuf_head) - 32) / \
	sizeof(union hammer_record_ondisk))

#define HAMMER_RECORD_SIZE	(64+32)

struct hammer_fsbuf_recs {
	struct hammer_fsbuf_head	head;
	char				unused[32];
	union hammer_record_ondisk	recs[HAMMER_RECORD_NODES];
};

/*
 * Filesystem buffer for piecemeal data.  Note that this does not apply
 * to dedicated pure-data buffers as such buffers do not have a header.
 */

#define HAMMER_DATA_SIZE	(HAMMER_BUFSIZE - sizeof(struct hammer_fsbuf_head))
#define HAMMER_DATA_BLKSIZE	64
#define HAMMER_DATA_BLKMASK	(HAMMER_DATA_BLKSIZE-1)
#define HAMMER_DATA_NODES	(HAMMER_DATA_SIZE / HAMMER_DATA_BLKSIZE)

struct hammer_fsbuf_data {
	struct hammer_fsbuf_head head;
	u_int8_t		data[HAMMER_DATA_NODES][HAMMER_DATA_BLKSIZE];
};

/*
 * Filesystem buffer rollup
 */
union hammer_fsbuf_ondisk {
	struct hammer_fsbuf_head	head;
	struct hammer_fsbuf_btree	btree;
	struct hammer_fsbuf_recs	record;
	struct hammer_fsbuf_data	data;
};

typedef union hammer_fsbuf_ondisk *hammer_fsbuf_ondisk_t;

/*
 * HAMMER UNIX Attribute data
 *
 * The data reference in a HAMMER inode record points to this structure.  Any
 * modifications to the contents of this structure will result in a record
 * replacement operation.
 *
 * short_data_off allows a small amount of data to be embedded in the
 * hammer_inode_data structure.  HAMMER typically uses this to represent
 * up to 64 bytes of data, or to hold symlinks.  Remember that allocations
 * are in powers of 2 so 64, 192, 448, or 960 bytes of embedded data is
 * support (64+64, 64+192, 64+448 64+960).
 *
 * parent_obj_id is only valid for directories (which cannot be hard-linked),
 * and specifies the parent directory obj_id.  This field will also be set
 * for non-directory inodes as a recovery aid, but can wind up specifying
 * stale information.  However, since object id's are not reused, the worse
 * that happens is that the recovery code is unable to use it.
 */
struct hammer_inode_data {
	u_int16_t version;	/* inode data version */
	u_int16_t mode;		/* basic unix permissions */
	u_int32_t uflags;	/* chflags */
	u_int32_t rmajor;	/* used by device nodes */
	u_int32_t rminor;	/* used by device nodes */
	u_int64_t ctime;
	u_int64_t parent_obj_id;/* parent directory obj_id */
	uuid_t	uid;
	uuid_t	gid;
	/* XXX device, softlink extension */
};

#define HAMMER_INODE_DATA_VERSION	1

#define HAMMER_OBJID_ROOT		1

/*
 * Rollup various structures embedded as record data
 */
union hammer_data_ondisk {
	struct hammer_inode_data inode;
};

