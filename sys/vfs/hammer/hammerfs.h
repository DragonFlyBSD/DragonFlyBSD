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
 * $DragonFly: src/sys/vfs/hammer/Attic/hammerfs.h,v 1.1 2007/10/10 19:37:25 dillon Exp $
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
 * I/O is done in multiples of 16K.
 */
#define HAMMER_BUFSIZE	16384
#define HAMMER_BUFMASK	(HAMMER_BUFSIZE - 1)

/*
 * Hammer transction ids are 64 bit unsigned integers and are usually
 * synchronized with the time of day in nanoseconds.
 */
typedef u_int64_t hammer_tid_t;

/*
 * Storage allocations are managed in powers of 2 with a hinted radix tree
 * based in volume and cluster headers.  The tree is not necessarily
 * contained within the header and may recurse into other storage elements.
 *
 * The allocator's basic storage element is the hammer_almeta structure
 * which is laid out recursively in a buffer.  Allocations are driven using
 * a template called hammer_alist which is constructed in memory and NOT
 * stored in the filesystem.
 */
struct hammer_almeta {
	u_int32_t	bm_bitmap;
	int32_t		bm_bighint;
};

#define HAMMER_ALMETA_SIZE	8

struct hammer_alist {
	int32_t	bl_blocks;	/* area of coverage */
	int32_t	bl_radix;	/* coverage radix */
	int32_t	bl_skip;	/* starting skip for linear layout */
	int32_t bl_free;	/* number of free blocks */
	int32_t bl_rootblks;	/* meta-blocks allocated for tree */
};

typedef struct hammer_almeta	hammer_almeta_t;
typedef struct hammer_alist	*hammer_alist_t;

#define HAMMER_ALIST_META_RADIX	(sizeof(u_int32_t) * 4)   /* 16 */
#define HAMMER_ALIST_BMAP_RADIX	(sizeof(u_int32_t) * 8)   /* 32 */
#define HAMMER_ALIST_BLOCK_NONE	((int32_t)-1)
#define HAMMER_ALIST_FORWARDS	0x0001
#define HAMMER_ALIST_BACKWARDS	0x0002

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
#define HAMMER_FSBUF_METAELMS	10	/* 10 elements needed for 256 blks */

struct hammer_fsbuf_head {
	u_int64_t buf_type;
	u_int32_t buf_crc;
	u_int32_t buf_reserved07;
	u_int32_t reserved[8];
	struct hammer_almeta buf_almeta[HAMMER_FSBUF_METAELMS];
};

typedef struct hammer_fsbuf_head *hammer_fsbuf_head_t;

#define HAMMER_FSBUF_VOLUME	0xC8414D4DC5523031ULL	/* HAMMER01 */
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
 * device contains a volume header followed by however many clusters
 * fit in the volume.  Clusters cannot be migrated but the data they contain
 * can, so HAMMER can use a truncated cluster for any extra space at the
 * end of the volume.
 *
 * The volume containing the root cluster is designated as the master volume.
 * The root cluster designation can be moved to any volume.
 *
 * The volume header takes up an entire 16K filesystem buffer and includes
 * an A-list to manage the clusters contained within the volume (up to 32768).
 * With 512M clusters a volume will be limited to 16TB.
 */
#define HAMMER_VOL_MAXCLUSTERS	32768
#define HAMMER_VOL_METAELMS	1094

struct hammer_volume_ondisk {
	struct hammer_fsbuf_head head;
	int64_t vol_beg;	/* byte offset of first cluster in volume */
	int64_t vol_end;	/* byte offset of volume EOF */
	int64_t vol_locked;	/* reserved clusters are >= this offset */

	uuid_t    vol_fsid;	/* identify filesystem */
	uuid_t    vol_fstype;	/* identify filesystem type */
	char	  vol_name[64];	/* Name of volume */

	int32_t vol_no;		/* volume number within filesystem */
	int32_t vol_count;	/* number of volumes making up FS */

	u_int32_t vol_version;	/* version control information */
	u_int32_t vol_segsize;	/* cluster size power of 2, 512M max */
	u_int32_t vol_flags;	/* volume flags */
	u_int32_t vol_rootvol;	/* which volume is the root volume? */

	int32_t vol_clsize;	/* cluster size (same for all volumes) */
	u_int32_t vol_reserved05;
	u_int32_t vol_reserved06;
	u_int32_t vol_reserved07;

	/*
	 * These fields are initialized and space is reserved in every
	 * volume making up a HAMMER filesytem, but only the master volume
	 * contains valid data.
	 */
	int32_t vol0_rootcluster;	/* root cluster no (index) in rootvol */
	u_int32_t vol0_reserved02;
	u_int32_t vol0_reserved03;
	hammer_tid_t vol0_nexttid;	/* next TID */
	u_int64_t vol0_recid;		/* fs-wide record id allocator */

	char	reserved[1024];

	hammer_almeta_t	vol_almeta[HAMMER_VOL_METAELMS];
	u_int32_t	vol0_bitmap[1024];
};

#define HAMMER_VOLF_VALID	0x0001	/* valid entry */
#define HAMMER_VOLF_OPEN	0x0002	/* volume is open */

/*
 * HAMMER Cluster header
 *
 * The cluster header contains all the information required to identify a
 * cluster, locate critical information areas within the cluster, and
 * to manage space within the cluster.
 *
 * A Cluster contains pure data, incremental data, b-tree nodes, and records.
 */
#define HAMMER_CLU_MAXBUFFERS	32768
#define HAMMER_CLU_METAELMS	1094

struct hammer_cluster_ondisk {
	struct hammer_fsbuf_head head;
	uuid_t	vol_fsid;	/* identify filesystem - sanity check */
	uuid_t	vol_fstype;	/* identify filesystem type - sanity check */

	u_int64_t clu_gen;	/* identify generation number of cluster */
	u_int64_t clu_unused01;

	hammer_tid_t clu_id;	/* unique cluster self identification */
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

	int32_t idx_data;	/* data append point (byte offset) */
	int32_t idx_index;	/* index append point (byte offset) */
	int32_t idx_record;	/* record prepend point (byte offset) */
	u_int32_t idx_reserved03;

	/* 
	 * Specify the range of information stored in this cluster.  These
	 * structures match the B-Tree elements in our parent cluster
	 * (if any) that point to us.  Note that clu_objend is
	 * range-inclusive, not range-exclusive so e.g. 0-1023 instead
	 * of 0-1024.
	 */
	int64_t	clu_parent;		/* parent vol & cluster */
	struct hammer_base_elm clu_objstart;
	struct hammer_base_elm clu_objend;

	/*
	 * The root node of the cluster's B-Tree is embedded in the
	 * cluster header.  The node is 504 bytes.
	 */
	struct hammer_btree_node clu_btree_root;

	/*
	 * HAMMER needs a separate bitmap to indicate which buffers are
	 * managed (contain a hammer_fsbuf_head).  Any buffers not so
	 * designated are either unused or contain pure data.
	 *
	 * synchronized_rec_id is the synchronization point for the
	 * cluster.  Any records with a greater or equal rec_id found
	 * when recovering a cluster are likely incomplete and will be
	 * ignored.
	 */
	u_int64_t synchronized_rec_id;
	u_int32_t managed_buffers_bitmap[HAMMER_CLU_MAXBUFFERS/32];

	char	reserved[1024];
	hammer_almeta_t	clu_almeta[HAMMER_CLU_METAELMS];
};

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
	int64_t	obj_id;		/* 00 object record is associated with */
	int64_t key;		/* 08 indexing key (offset or namekey) */

	hammer_tid_t create_tid;/* 10 transaction id for record creation */
	hammer_tid_t delete_tid;/* 18 transaction id for record update/delete */

	u_int16_t rec_type;	/* 20 type of record */
	u_int16_t obj_type;	/* 22 type of object (if inode) */
	u_int32_t data_offset;	/* 24 intra-cluster data reference */
				/*    An offset of 0 indicates zero-fill */
	int32_t data_len;	/* 28 size of data (remainder zero-fill) */
	u_int32_t data_crc;	/* 2C data sanity check */
	u_int64_t rec_id;	/* 30 record id (iterator for recovery) */
	u_int64_t reserved07;	/* 38 */
				/* 40 */
};

#define HAMMER_RECTYPE_UNKNOWN		0
#define HAMMER_RECTYPE_INODE		1	/* inode in obj_id space */
#define HAMMER_RECTYPE_SLAVE		2	/* slave inode */
#define HAMMER_RECTYPE_OBJZONE		3	/* subdivide obj_id space */
#define HAMMER_RECTYPE_DATA_CREATE	0x10
#define HAMMER_RECTYPE_DATA_ZEROFILL	0x11
#define HAMMER_RECTYPE_DATA_DELETE	0x12
#define HAMMER_RECTYPE_DATA_UPDATE	0x13
#define HAMMER_RECTYPE_DIR_CREATE	0x20
#define HAMMER_RECTYPE_DIR_DELETE	0x22
#define HAMMER_RECTYPE_DIR_UPDATE	0x23
#define HAMMER_RECTYPE_DB_CREATE	0x30
#define HAMMER_RECTYPE_DB_DELETE	0x32
#define HAMMER_RECTYPE_DB_UPDATE	0x33
#define HAMMER_RECTYPE_EXT_CREATE	0x40	/* ext attributes */
#define HAMMER_RECTYPE_EXT_DELETE	0x42
#define HAMMER_RECTYPE_EXT_UPDATE	0x43

#define HAMMER_OBJTYPE_DIRECTORY	1
#define HAMMER_OBJTYPE_REGFILE		2
#define HAMMER_OBJTYPE_DBFILE		3
#define HAMMER_OBJTYPE_FIFO		4
#define HAMMER_OBJTYPE_DEVNODE		5
#define HAMMER_OBJTYPE_SOFTLINK		6

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
 */
struct hammer_entry_record {
	struct hammer_base_record base;
	u_int64_t obj_id;		/* object being referenced */
	u_int64_t reserved01;
	u_int8_t  den_type;		/* cached file type */
	char	  den_name[15];		/* short file names fit in record */
};

/*
 * Hammer rollup record
 */
union hammer_record {
	struct hammer_base_record	base;
	struct hammer_generic_record	generic;
	struct hammer_inode_record	inode;
	struct hammer_data_record	data;
	struct hammer_entry_record	entry;
};

typedef union hammer_record *hammer_record_t;

/*
 * Filesystem buffer for records
 */
#define HAMMER_RECORD_NODES	\
	((HAMMER_BUFSIZE - sizeof(struct hammer_fsbuf_head)) / \
	sizeof(union hammer_record))

struct hammer_fsbuf_recs {
	struct hammer_fsbuf_head	head;
	char				unused[32];
	union hammer_record		recs[HAMMER_RECORD_NODES];
};

/*
 * Filesystem buffer for piecemeal data.  Note that this does not apply
 * to dedicated pure-data buffers as such buffers do not have a header.
 */

#define HAMMER_DATA_SIZE	(HAMMER_BUFSIZE - sizeof(struct hammer_fsbuf_head))
#define HAMMER_DATA_BLKSIZE	64
#define HAMMER_DATA_NODES	(HAMMER_DATA_SIZE / HAMMER_DATA_BLKSIZE)

struct hammer_fsbuf_data {
	struct hammer_fsbuf_head head;
	u_int8_t		data[HAMMER_DATA_NODES][HAMMER_DATA_BLKSIZE];
};


/*
 * HAMMER UNIX Attribute data
 *
 * The data reference in a HAMMER inode record points to this structure.  Any
 * modifications to the contents of this structure will result in a record
 * replacement operation.
 *
 * state_sum allows a filesystem object to be validated to a degree by
 * generating a checksum of all of its pieces (in no particular order) and
 * checking it against this field.
 */
struct hammer_inode_data {
	u_int16_t version;	/* inode data version */
	u_int16_t mode;		/* basic unix permissions */
	u_int32_t uflags;	/* chflags */
	u_int64_t reserved01;
	u_int64_t reserved02;
	u_int64_t state_sum;	/* cumulative checksum */
	uuid_t	uid;
	uuid_t	gid;
};

#define HAMMER_INODE_DATA_VERSION	1

/*
 * Function library support available to kernel and userland
 */
void hammer_alist_template(hammer_alist_t, int blocks, int maxmeta);
void hammer_alist_init(hammer_alist_t bl, hammer_almeta_t *meta);
int32_t hammer_alist_alloc(hammer_alist_t bl, hammer_almeta_t *meta,
			int32_t count);
int32_t hammer_alist_alloc_rev(hammer_alist_t bl, hammer_almeta_t *meta,
			int32_t count);
#if 0
int32_t hammer_alist_alloc_from(hammer_alist_t bl, hammer_almeta_t *meta,
			int32_t count, int32_t start, int flags);
#endif
void hammer_alist_free(hammer_alist_t bl, hammer_almeta_t *meta,
			int32_t blkno, int32_t count);

