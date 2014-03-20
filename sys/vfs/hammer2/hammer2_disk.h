/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#ifndef _VFS_HAMMER2_DISK_H_
#define _VFS_HAMMER2_DISK_H_

#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif
#ifndef _SYS_DMSG_H_
#include <sys/dmsg.h>
#endif

/*
 * The structures below represent the on-disk media structures for the HAMMER2
 * filesystem.  Note that all fields for on-disk structures are naturally
 * aligned.  The host endian format is typically used - compatibility is
 * possible if the implementation detects reversed endian and adjusts accesses
 * accordingly.
 *
 * HAMMER2 primarily revolves around the directory topology:  inodes,
 * directory entries, and block tables.  Block device buffer cache buffers
 * are always 64KB.  Logical file buffers are typically 16KB.  All data
 * references utilize 64-bit byte offsets.
 *
 * Free block management is handled independently using blocks reserved by
 * the media topology.
 */

/*
 * The data at the end of a file or directory may be a fragment in order
 * to optimize storage efficiency.  The minimum fragment size is 1KB.
 * Since allocations are in powers of 2 fragments must also be sized in
 * powers of 2 (1024, 2048, ... 65536).
 *
 * For the moment the maximum allocation size is HAMMER2_PBUFSIZE (64K),
 * which is 2^16.  Larger extents may be supported in the future.  Smaller
 * fragments might be supported in the future (down to 64 bytes is possible),
 * but probably will not be.
 *
 * A full indirect block use supports 1024 x 64-byte blockrefs in a 64KB
 * buffer.  Indirect blocks down to 1KB are supported to keep small
 * directories small.
 *
 * A maximally sized file (2^64-1 bytes) requires 5 indirect block levels.
 * The hammer2_blockset in the volume header or file inode has another 8
 * entries, giving us 66+3 = 69 bits of address space.  However, some bits
 * are taken up by (potentially) requests for redundant copies.  HAMMER2
 * currently supports up to 8 copies, which brings the address space down
 * to 66 bits and gives us 2 bits of leeway.
 */
#define HAMMER2_MIN_ALLOC	1024	/* minimum allocation size */
#define HAMMER2_MIN_RADIX	10	/* minimum allocation size 2^N */
#define HAMMER2_MAX_ALLOC	65536	/* maximum allocation size */
#define HAMMER2_MAX_RADIX	16	/* maximum allocation size 2^N */
#define HAMMER2_KEY_RADIX	64	/* number of bits in key */

/*
 * MINALLOCSIZE		- The minimum allocation size.  This can be smaller
 *		  	  or larger than the minimum physical IO size.
 *
 *			  NOTE: Should not be larger than 1K since inodes
 *				are 1K.
 *
 * MINIOSIZE		- The minimum IO size.  This must be less than
 *			  or equal to HAMMER2_LBUFSIZE.
 *
 * HAMMER2_LBUFSIZE	- Nominal buffer size for I/O rollups.
 *
 * HAMMER2_PBUFSIZE	- Topological block size used by files for all
 *			  blocks except the block straddling EOF.
 *
 * HAMMER2_SEGSIZE	- Allocation map segment size, typically 2MB
 *			  (space represented by a level0 bitmap).
 */

#define HAMMER2_SEGSIZE		(1 << HAMMER2_FREEMAP_LEVEL0_RADIX)
#define HAMMER2_SEGRADIX	HAMMER2_FREEMAP_LEVEL0_RADIX

#define HAMMER2_PBUFRADIX	16	/* physical buf (1<<16) bytes */
#define HAMMER2_PBUFSIZE	65536
#define HAMMER2_LBUFRADIX	14	/* logical buf (1<<14) bytes */
#define HAMMER2_LBUFSIZE	16384

/*
 * Generally speaking we want to use 16K and 64K I/Os
 */
#define HAMMER2_MINIORADIX	HAMMER2_LBUFRADIX
#define HAMMER2_MINIOSIZE	HAMMER2_LBUFSIZE

#define HAMMER2_IND_BYTES_MIN	HAMMER2_LBUFSIZE
#define HAMMER2_IND_BYTES_MAX	HAMMER2_PBUFSIZE
#define HAMMER2_IND_COUNT_MIN	(HAMMER2_IND_BYTES_MIN / \
				 sizeof(hammer2_blockref_t))
#define HAMMER2_IND_COUNT_MAX	(HAMMER2_IND_BYTES_MAX / \
				 sizeof(hammer2_blockref_t))

/*
 * In HAMMER2, arrays of blockrefs are fully set-associative, meaning that
 * any element can occur at any index and holes can be anywhere.  As a
 * future optimization we will be able to flag that such arrays are sorted
 * and thus optimize lookups, but for now we don't.
 *
 * Inodes embed either 512 bytes of direct data or an array of 8 blockrefs,
 * resulting in highly efficient storage for files <= 512 bytes and for files
 * <= 512KB.  Up to 8 directory entries can be referenced from a directory
 * without requiring an indirect block.
 *
 * Indirect blocks are typically either 4KB (64 blockrefs / ~4MB represented),
 * or 64KB (1024 blockrefs / ~64MB represented).
 */
#define HAMMER2_SET_COUNT		8	/* direct entries */
#define HAMMER2_SET_RADIX		3
#define HAMMER2_EMBEDDED_BYTES		512	/* inode blockset/dd size */
#define HAMMER2_EMBEDDED_RADIX		9

#define HAMMER2_PBUFMASK	(HAMMER2_PBUFSIZE - 1)
#define HAMMER2_LBUFMASK	(HAMMER2_LBUFSIZE - 1)
#define HAMMER2_SEGMASK		(HAMMER2_SEGSIZE - 1)

#define HAMMER2_LBUFMASK64	((hammer2_off_t)HAMMER2_LBUFMASK)
#define HAMMER2_PBUFSIZE64	((hammer2_off_t)HAMMER2_PBUFSIZE)
#define HAMMER2_PBUFMASK64	((hammer2_off_t)HAMMER2_PBUFMASK)
#define HAMMER2_SEGSIZE64	((hammer2_off_t)HAMMER2_SEGSIZE)
#define HAMMER2_SEGMASK64	((hammer2_off_t)HAMMER2_SEGMASK)

#define HAMMER2_UUID_STRING	"5cbb9ad1-862d-11dc-a94d-01301bb8a9f5"

/*
 * A HAMMER2 filesystem is always sized in multiples of 8MB.
 *
 * A 4MB segment is reserved at the beginning of each 2GB zone.  This segment
 * contains the volume header (or backup volume header), the free block
 * table, and possibly other information in the future.
 *
 * 4MB = 64 x 64K blocks.  Each 4MB segment is broken down as follows:
 *
 *	+-----------------------+
 *      |	Volume Hdr	| block 0	volume header & alternates
 *	+-----------------------+		(first four zones only)
 *	|   FreeBlk Section A   | block 1-4
 *	+-----------------------+
 *	|   FreeBlk Section B   | block 5-8
 *	+-----------------------+
 *	|   FreeBlk Section C   | block 9-12
 *	+-----------------------+
 *	|   FreeBlk Section D   | block 13-16
 *	+-----------------------+
 *      |			| block 17...63
 *      |	reserved	|
 *      |			|
 *	+-----------------------+
 *
 * The first few 2GB zones contain volume headers and volume header backups.
 * After that the volume header block# is reserved.
 *
 *			Freemap (see the FREEMAP document)
 *
 * The freemap utilizes blocks #1-16 for now, see the FREEMAP document.
 * The filesystems rotations through the sections to avoid disturbing the
 * 'previous' version of the freemap during a flush.
 *
 * Each freemap section is 4 x 64K blocks and represents 2GB, 2TB, 2PB,
 * and 2EB indirect map, plus the volume header has a set of 8 blockrefs
 * for another 3 bits for a total of 64 bits of address space.  The Level 0
 * 64KB block representing 2GB of storage is a hammer2_bmap_data[1024].
 * Each element contains a 128x2 bit bitmap representing 16KB per chunk for
 * 2MB of storage (x1024 elements = 2GB).  2 bits per chunk:
 *
 *	00	Free
 *	01	(reserved)
 *	10	Possibly free
 *	11	Allocated
 *
 * One important thing to note here is that the freemap resolution is 16KB,
 * but the minimuim storage allocation size is 1KB.  The hammer2 vfs keeps
 * track of sub-allocations in memory (on umount or reboot obvious the whole
 * 16KB will be considered allocated even if only 1KB is allocated).  It is
 * possible for fragmentation to build up over time.
 *
 * The Second thing to note is that due to the way snapshots and inode
 * replication works, deleting a file cannot immediately free the related
 * space.  Instead, the freemap elements transition from 11->10.  The bulk
 * freeing code which does a complete scan is then responsible for
 * transitioning the elements to 00 or back to 11 or to 01 for that matter.
 *
 * WARNING!  ZONE_SEG and VOLUME_ALIGN must be a multiple of 1<<LEVEL0_RADIX
 *	     (i.e. a multiple of 2MB).  VOLUME_ALIGN must be >= ZONE_SEG.
 */
#define HAMMER2_VOLUME_ALIGN		(8 * 1024 * 1024)
#define HAMMER2_VOLUME_ALIGN64		((hammer2_off_t)HAMMER2_VOLUME_ALIGN)
#define HAMMER2_VOLUME_ALIGNMASK	(HAMMER2_VOLUME_ALIGN - 1)
#define HAMMER2_VOLUME_ALIGNMASK64     ((hammer2_off_t)HAMMER2_VOLUME_ALIGNMASK)

#define HAMMER2_NEWFS_ALIGN		(HAMMER2_VOLUME_ALIGN)
#define HAMMER2_NEWFS_ALIGN64		((hammer2_off_t)HAMMER2_VOLUME_ALIGN)
#define HAMMER2_NEWFS_ALIGNMASK		(HAMMER2_VOLUME_ALIGN - 1)
#define HAMMER2_NEWFS_ALIGNMASK64	((hammer2_off_t)HAMMER2_NEWFS_ALIGNMASK)

#define HAMMER2_ZONE_BYTES64		(2LLU * 1024 * 1024 * 1024)
#define HAMMER2_ZONE_MASK64		(HAMMER2_ZONE_BYTES64 - 1)
#define HAMMER2_ZONE_SEG		(4 * 1024 * 1024)
#define HAMMER2_ZONE_SEG64		((hammer2_off_t)HAMMER2_ZONE_SEG)
#define HAMMER2_ZONE_BLOCKS_SEG		(HAMMER2_ZONE_SEG / HAMMER2_PBUFSIZE)

/*
 * 64 x 64KB blocks are reserved at the base of each 2GB zone.  These blocks
 * are used to store the volume header or volume header backups, allocation
 * tree, and other information in the future.
 *
 * All specified blocks are not necessarily used in all 2GB zones.  However,
 * dead areas are reserved for future use and MUST NOT BE USED for other
 * purposes.
 *
 * The freemap is arranged into 15 groups of 4x64KB each.  The 4 sub-groups
 * are labeled ZONEFM1..4 and representing HAMMER2_FREEMAP_LEVEL{1-4}_RADIX,
 * for the up to 4 levels of radix tree representing the freemap.  For
 * simplicity we are reserving all four radix tree layers even though the
 * higher layers do not require teh reservation at each 2GB mark.  That
 * space is reserved for future use.
 *
 * Freemap blocks are not allocated dynamically but instead rotate through
 * one of 15 possible copies.  We require 15 copies for several reasons:
 *
 * (1) For distinguishing freemap 'allocations' made by the current flush
 *     verses the concurrently running front-end (at flush_tid + 1).  This
 *     theoretically requires two copies but the algorithm is greatly
 *     simplified if we use three.
 *
 * (2) There are up to 4 copies of the volume header (iterated on each flush),
 *     and if the mount code is forced to use an older copy due to corruption
 *     we must be sure that the state of the freemap AS-OF the earlier copy
 *     remains valid.
 *
 *     This means 3 copies x 4 flushes = 12 copies to be able to mount any
 *     of the four volume header backups after on boot or after a crash.
 *
 * (3) Freemap recovery on-mount eats a copy.  We don't want freemap recovery
 *     to blow away the copy used by some other volume header in case H2
 *     crashes during the recovery.  Total is now 13.
 *
 * (4) And I want some breathing room to ensure that complex flushes do not
 *     cause problems.  Also note that bulk block freeing itself must be
 *     careful so even on a live system, post-mount, the four volume header
 *     backups effectively represent short-lived snapshots.  And I only
 *     have room for 15 copies so it works out.
 *
 * Preferably I would like to improve the algorithm to only use 2 copies per
 * volume header (which would be a total of 2 x 4 = 8 + 1 for freemap recovery
 * + 1 for breathing room = 10 total instead of 15).  For now we use 15.
 */
#define HAMMER2_ZONE_VOLHDR		0	/* volume header or backup */
#define HAMMER2_ZONE_FREEMAP_00		1
#define HAMMER2_ZONE_FREEMAP_01		5
#define HAMMER2_ZONE_FREEMAP_02		9
#define HAMMER2_ZONE_FREEMAP_03		13
#define HAMMER2_ZONE_FREEMAP_04		17
#define HAMMER2_ZONE_FREEMAP_05		21
#define HAMMER2_ZONE_FREEMAP_06		25
#define HAMMER2_ZONE_FREEMAP_07		29
#define HAMMER2_ZONE_FREEMAP_08		33
#define HAMMER2_ZONE_FREEMAP_09		37
#define HAMMER2_ZONE_FREEMAP_10		41
#define HAMMER2_ZONE_FREEMAP_11		45
#define HAMMER2_ZONE_FREEMAP_12		49
#define HAMMER2_ZONE_FREEMAP_13		53
#define HAMMER2_ZONE_FREEMAP_14		57
#define HAMMER2_ZONE_FREEMAP_END	61	/* (non-inclusive) */
#define HAMMER2_ZONE_UNUSED62		62
#define HAMMER2_ZONE_UNUSED63		63

#define HAMMER2_ZONE_FREEMAP_COPIES	15
						/* relative to FREEMAP_x */
#define HAMMER2_ZONEFM_LEVEL1		0	/* 2GB leafmap */
#define HAMMER2_ZONEFM_LEVEL2		1	/* 2TB indmap */
#define HAMMER2_ZONEFM_LEVEL3		2	/* 2PB indmap */
#define HAMMER2_ZONEFM_LEVEL4		3	/* 2EB indmap */
/* LEVEL5 is a set of 8 blockrefs in the volume header 16EB */


/*
 * Freemap radii.  Please note that LEVEL 1 blockref array entries
 * point to 256-byte sections of the bitmap representing 2MB of storage.
 * Even though the chain structures represent only 256 bytes, they are
 * mapped using larger 16K or 64K buffer cache buffers.
 */
#define HAMMER2_FREEMAP_LEVEL5_RADIX	64	/* 16EB */
#define HAMMER2_FREEMAP_LEVEL4_RADIX	61	/* 2EB */
#define HAMMER2_FREEMAP_LEVEL3_RADIX	51	/* 2PB */
#define HAMMER2_FREEMAP_LEVEL2_RADIX	41	/* 2TB */
#define HAMMER2_FREEMAP_LEVEL1_RADIX	31	/* 2GB */
#define HAMMER2_FREEMAP_LEVEL0_RADIX	21	/* 2MB (entry in l-1 leaf) */

#define HAMMER2_FREEMAP_LEVELN_PSIZE	65536	/* physical bytes */

#define HAMMER2_FREEMAP_COUNT		(int)(HAMMER2_FREEMAP_LEVELN_PSIZE / \
					 sizeof(hammer2_bmap_data_t))
#define HAMMER2_FREEMAP_BLOCK_RADIX	14
#define HAMMER2_FREEMAP_BLOCK_SIZE	(1 << HAMMER2_FREEMAP_BLOCK_RADIX)
#define HAMMER2_FREEMAP_BLOCK_MASK	(HAMMER2_FREEMAP_BLOCK_SIZE - 1)

/*
 * Two linear areas can be reserved after the initial 2MB segment in the base
 * zone (the one starting at offset 0).  These areas are NOT managed by the
 * block allocator and do not fall under HAMMER2 crc checking rules based
 * at the volume header (but can be self-CRCd internally, depending).
 */
#define HAMMER2_BOOT_MIN_BYTES		HAMMER2_VOLUME_ALIGN
#define HAMMER2_BOOT_NOM_BYTES		(64*1024*1024)
#define HAMMER2_BOOT_MAX_BYTES		(256*1024*1024)

#define HAMMER2_REDO_MIN_BYTES		HAMMER2_VOLUME_ALIGN
#define HAMMER2_REDO_NOM_BYTES		(256*1024*1024)
#define HAMMER2_REDO_MAX_BYTES		(1024*1024*1024)

/*
 * Most HAMMER2 types are implemented as unsigned 64-bit integers.
 * Transaction ids are monotonic.
 *
 * We utilize 32-bit iSCSI CRCs.
 */
typedef uint64_t hammer2_tid_t;
typedef uint64_t hammer2_off_t;
typedef uint64_t hammer2_key_t;
typedef uint32_t hammer2_crc32_t;

/*
 * Miscellanious ranges (all are unsigned).
 */
#define HAMMER2_MIN_TID		1ULL
#define HAMMER2_MAX_TID		0xFFFFFFFFFFFFFFFFULL
#define HAMMER2_MIN_KEY		0ULL
#define HAMMER2_MAX_KEY		0xFFFFFFFFFFFFFFFFULL
#define HAMMER2_MIN_OFFSET	0ULL
#define HAMMER2_MAX_OFFSET	0xFFFFFFFFFFFFFFFFULL

/*
 * HAMMER2 data offset special cases and masking.
 *
 * All HAMMER2 data offsets have to be broken down into a 64K buffer base
 * offset (HAMMER2_OFF_MASK_HI) and a 64K buffer index (HAMMER2_OFF_MASK_LO).
 *
 * Indexes into physical buffers are always 64-byte aligned.  The low 6 bits
 * of the data offset field specifies how large the data chunk being pointed
 * to as a power of 2.  The theoretical minimum radix is thus 6 (The space
 * needed in the low bits of the data offset field).  However, the practical
 * minimum allocation chunk size is 1KB (a radix of 10), so HAMMER2 sets
 * HAMMER2_MIN_RADIX to 10.  The maximum radix is currently 16 (64KB), but
 * we fully intend to support larger extents in the future.
 */
#define HAMMER2_OFF_BAD		((hammer2_off_t)-1)
#define HAMMER2_OFF_MASK	0xFFFFFFFFFFFFFFC0ULL
#define HAMMER2_OFF_MASK_LO	(HAMMER2_OFF_MASK & HAMMER2_PBUFMASK64)
#define HAMMER2_OFF_MASK_HI	(~HAMMER2_PBUFMASK64)
#define HAMMER2_OFF_MASK_RADIX	0x000000000000003FULL
#define HAMMER2_MAX_COPIES	6

/*
 * HAMMER2 directory support and pre-defined keys
 */
#define HAMMER2_DIRHASH_VISIBLE	0x8000000000000000ULL
#define HAMMER2_DIRHASH_USERMSK	0x7FFFFFFFFFFFFFFFULL
#define HAMMER2_DIRHASH_LOMASK	0x0000000000007FFFULL
#define HAMMER2_DIRHASH_HIMASK	0xFFFFFFFFFFFF0000ULL
#define HAMMER2_DIRHASH_FORCED	0x0000000000008000ULL	/* bit forced on */

#define HAMMER2_SROOT_KEY	0x0000000000000000ULL	/* volume to sroot */

/*
 * The media block reference structure.  This forms the core of the HAMMER2
 * media topology recursion.  This 64-byte data structure is embedded in the
 * volume header, in inodes (which are also directory entries), and in
 * indirect blocks.
 *
 * A blockref references a single media item, which typically can be a
 * directory entry (aka inode), indirect block, or data block.
 *
 * The primary feature a blockref represents is the ability to validate
 * the entire tree underneath it via its check code.  Any modification to
 * anything propagates up the blockref tree all the way to the root, replacing
 * the related blocks.  Propagations can shortcut to the volume root to
 * implement the 'fast syncing' feature but this only delays the eventual
 * propagation.
 *
 * The check code can be a simple 32-bit iscsi code, a 64-bit crc,
 * or as complex as a 192 bit cryptographic hash.  192 bits is the maximum
 * supported check code size, which is not sufficient for unverified dedup
 * UNLESS one doesn't mind once-in-a-blue-moon data corruption (such as when
 * farming web data).  HAMMER2 has an unverified dedup feature for just this
 * purpose.
 *
 * --
 *
 * NOTE: The range of keys represented by the blockref is (key) to
 *	 ((key) + (1LL << keybits) - 1).  HAMMER2 usually populates
 *	 blocks bottom-up, inserting a new root when radix expansion
 *	 is required.
 */
struct hammer2_blockref {		/* MUST BE EXACTLY 64 BYTES */
	uint8_t		type;		/* type of underlying item */
	uint8_t		methods;	/* check method & compression method */
	uint8_t		copyid;		/* specify which copy this is */
	uint8_t		keybits;	/* #of keybits masked off 0=leaf */
	uint8_t		vradix;		/* virtual data/meta-data size */
	uint8_t		flags;		/* blockref flags */
	uint8_t		reserved06;
	uint8_t		reserved07;
	hammer2_key_t	key;		/* key specification */
	hammer2_tid_t	mirror_tid;	/* propagate for mirror scan */
	hammer2_tid_t	modify_tid;	/* modifications sans propagation */
	hammer2_off_t	data_off;	/* low 6 bits is phys size (radix)*/
	union {				/* check info */
		char	buf[24];
		struct {
			uint32_t value;
			uint32_t unused[5];
		} iscsi32;
		struct {
			uint64_t value;
			uint64_t unused[2];
		} crc64;
		struct {
			char data[24];
		} sha192;

		/*
		 * Freemap hints are embedded in addition to the icrc32.
		 *
		 * bigmask - Radixes available for allocation (0-31).
		 *	     Heuristical (may be permissive but not
		 *	     restrictive).  Typically only radix values
		 *	     10-16 are used (i.e. (1<<10) through (1<<16)).
		 *
		 * avail   - Total available space remaining, in bytes
		 */
		struct {
			uint32_t icrc32;
			uint32_t bigmask;	/* available radixes */
			uint64_t avail;		/* total available bytes */
			uint64_t unused;	/* unused must be 0 */
		} freemap;

		/*
		 * Debugging
		 */
		struct {
			hammer2_tid_t sync_tid;
		} debug;
	} check;
};

typedef struct hammer2_blockref hammer2_blockref_t;

#if 0
#define HAMMER2_BREF_SYNC1		0x01	/* modification synchronized */
#define HAMMER2_BREF_SYNC2		0x02	/* modification committed */
#define HAMMER2_BREF_DESYNCCHLD		0x04	/* desynchronize children */
#define HAMMER2_BREF_DELETED		0x80	/* indicates a deletion */
#endif

#define HAMMER2_BLOCKREF_BYTES		64	/* blockref struct in bytes */

/*
 * On-media and off-media blockref types.
 */
#define HAMMER2_BREF_TYPE_EMPTY		0
#define HAMMER2_BREF_TYPE_INODE		1
#define HAMMER2_BREF_TYPE_INDIRECT	2
#define HAMMER2_BREF_TYPE_DATA		3
#define HAMMER2_BREF_TYPE_UNUSED04	4
#define HAMMER2_BREF_TYPE_FREEMAP_NODE	5
#define HAMMER2_BREF_TYPE_FREEMAP_LEAF	6
#define HAMMER2_BREF_TYPE_FREEMAP	254	/* pseudo-type */
#define HAMMER2_BREF_TYPE_VOLUME	255	/* pseudo-type */

#define HAMMER2_ENC_CHECK(n)		((n) << 4)
#define HAMMER2_DEC_CHECK(n)		(((n) >> 4) & 15)

#define HAMMER2_CHECK_NONE		0
#define HAMMER2_CHECK_ISCSI32		1
#define HAMMER2_CHECK_CRC64		2
#define HAMMER2_CHECK_SHA192		3
#define HAMMER2_CHECK_FREEMAP		4

#define HAMMER2_ENC_COMP(n)		(n)
#define HAMMER2_ENC_LEVEL(n)		((n) << 4)
#define HAMMER2_DEC_COMP(n)		((n) & 15)
#define HAMMER2_DEC_LEVEL(n)		(((n) >> 4) & 15)

#define HAMMER2_COMP_NONE		0
#define HAMMER2_COMP_AUTOZERO		1
#define HAMMER2_COMP_LZ4		2
#define HAMMER2_COMP_ZLIB		3

#define HAMMER2_COMP_NEWFS_DEFAULT	HAMMER2_COMP_LZ4
#define HAMMER2_COMP_STRINGS		{ "none", "autozero", "lz4", "zlib" }
#define HAMMER2_COMP_STRINGS_COUNT	4


/*
 * HAMMER2 block references are collected into sets of 8 blockrefs.  These
 * sets are fully associative, meaning the elements making up a set are
 * not sorted in any way and may contain duplicate entries, holes, or
 * entries which shortcut multiple levels of indirection.  Sets are used
 * in various ways:
 *
 * (1) When redundancy is desired a set may contain several duplicate
 *     entries pointing to different copies of the same data.  Up to 8 copies
 *     are supported but the set structure becomes a bit inefficient once
 *     you go over 4.
 *
 * (2) The blockrefs in a set can shortcut multiple levels of indirections
 *     within the bounds imposed by the parent of set.
 *
 * When a set fills up another level of indirection is inserted, moving
 * some or all of the set's contents into indirect blocks placed under the
 * set.  This is a top-down approach in that indirect blocks are not created
 * until the set actually becomes full (that is, the entries in the set can
 * shortcut the indirect blocks when the set is not full).  Depending on how
 * things are filled multiple indirect blocks will eventually be created.
 *
 * Indirect blocks are typically 4KB (64 entres) or 64KB (1024 entries) and
 * are also treated as fully set-associative.
 */
struct hammer2_blockset {
	hammer2_blockref_t	blockref[HAMMER2_SET_COUNT];
};

typedef struct hammer2_blockset hammer2_blockset_t;

/*
 * Catch programmer snafus
 */
#if (1 << HAMMER2_SET_RADIX) != HAMMER2_SET_COUNT
#error "hammer2 direct radix is incorrect"
#endif
#if (1 << HAMMER2_PBUFRADIX) != HAMMER2_PBUFSIZE
#error "HAMMER2_PBUFRADIX and HAMMER2_PBUFSIZE are inconsistent"
#endif
#if (1 << HAMMER2_MIN_RADIX) != HAMMER2_MIN_ALLOC
#error "HAMMER2_MIN_RADIX and HAMMER2_MIN_ALLOC are inconsistent"
#endif

/*
 * hammer2_bmap_data - A freemap entry in the LEVEL1 block.
 *
 * Each 64-byte entry contains the bitmap and meta-data required to manage
 * a LEVEL0 (2MB) block of storage.  The storage is managed in 128 x 16KB
 * chunks.  Smaller allocation granularity is supported via a linear iterator
 * and/or must otherwise be tracked in ram.
 *
 * (data structure must be 64 bytes exactly)
 *
 * linear  - A BYTE linear allocation offset used for sub-16KB allocations
 *	     only.  May contain values between 0 and 2MB.  Must be ignored
 *	     if 16KB-aligned (i.e. force bitmap scan), otherwise may be
 *	     used to sub-allocate within the 16KB block (which is already
 *	     marked as allocated in the bitmap).
 *
 *	     Sub-allocations need only be 1KB-aligned and do not have to be
 *	     size-aligned, and 16KB or larger allocations do not update this
 *	     field, resulting in pretty good packing.
 *
 *	     Please note that file data granularity may be limited by
 *	     other issues such as buffer cache direct-mapping and the
 *	     desire to support sector sizes up to 16KB (so H2 only issues
 *	     I/O's in multiples of 16KB anyway).
 *
 * class   - Clustering class.  Cleared to 0 only if the entire leaf becomes
 *	     free.  Used to cluster device buffers so all elements must have
 *	     the same device block size, but may mix logical sizes.
 *
 *	     Typically integrated with the blockref type in the upper 8 bits
 *	     to localize inodes and indrect blocks, improving bulk free scans
 *	     and directory scans.
 *
 * bitmap  - Two bits per 16KB allocation block arranged in arrays of
 *	     32-bit elements, 128x2 bits representing ~2MB worth of media
 *	     storage.  Bit patterns are as follows:
 *
 *	     00	Unallocated
 *	     01 (reserved)
 *	     10 Possibly free
 *           11 Allocated
 */
struct hammer2_bmap_data {
	int32_t linear;		/* 00 linear sub-granular allocation offset */
	uint16_t class;		/* 04-05 clustering class ((type<<8)|radix) */
	uint8_t reserved06;	/* 06 */
	uint8_t reserved07;	/* 07 */
	uint32_t reserved08;	/* 08 */
	uint32_t reserved0C;	/* 0C */
	uint32_t reserved10;	/* 10 */
	uint32_t reserved14;	/* 14 */
	uint32_t reserved18;	/* 18 */
	uint32_t avail;		/* 1C */
	uint32_t bitmap[8];	/* 20-3F 256 bits manages 2MB/16KB/2-bits */
};

typedef struct hammer2_bmap_data hammer2_bmap_data_t;

/*
 * In HAMMER2 inodes ARE directory entries, with a special exception for
 * hardlinks.  The inode number is stored in the inode rather than being
 * based on the location of the inode (since the location moves every time
 * the inode or anything underneath the inode is modified).
 *
 * The inode is 1024 bytes, made up of 256 bytes of meta-data, 256 bytes
 * for the filename, and 512 bytes worth of direct file data OR an embedded
 * blockset.
 *
 * Directories represent one inode per blockref.  Inodes are not laid out
 * as a file but instead are represented by the related blockrefs.  The
 * blockrefs, in turn, are indexed by the 64-bit directory hash key.  Remember
 * that blocksets are fully associative, so a certain degree efficiency is
 * achieved just from that.
 *
 * Up to 512 bytes of direct data can be embedded in an inode, and since
 * inodes are essentially directory entries this also means that small data
 * files end up simply being laid out linearly in the directory, resulting
 * in fewer seeks and highly optimal access.
 *
 * The compression mode can be changed at any time in the inode and is
 * recorded on a blockref-by-blockref basis.
 *
 * Hardlinks are supported via the inode map.  Essentially the way a hardlink
 * works is that all individual directory entries representing the same file
 * are special cased and specify the same inode number.  The actual file
 * is placed in the nearest parent directory that is parent to all instances
 * of the hardlink.  If all hardlinks to a file are in the same directory
 * the actual file will also be placed in that directory.  This file uses
 * the inode number as the directory entry key and is invisible to normal
 * directory scans.  Real directory entry keys are differentiated from the
 * inode number key via bit 63.  Access to the hardlink silently looks up
 * the real file and forwards all operations to that file.  Removal of the
 * last hardlink also removes the real file.
 *
 * (attr_tid) is only updated when the inode's specific attributes or regular
 * file size has changed, and affects path lookups and stat.  (attr_tid)
 * represents a special cache coherency lock under the inode.  The inode
 * blockref's modify_tid will always cover it.
 *
 * (dirent_tid) is only updated when an entry under a directory inode has
 * been created, deleted, renamed, or had its attributes change, and affects
 * directory lookups and scans.  (dirent_tid) represents another special cache
 * coherency lock under the inode.  The inode blockref's modify_tid will
 * always cover it.
 */
#define HAMMER2_INODE_BYTES		1024	/* (asserted by code) */
#define HAMMER2_INODE_MAXNAME		256	/* maximum name in bytes */
#define HAMMER2_INODE_VERSION_ONE	1

#define HAMMER2_INODE_HIDDENDIR		16	/* special inode */
#define HAMMER2_INODE_START		1024	/* dynamically allocated */

struct hammer2_inode_data {
	uint16_t	version;	/* 0000 inode data version */
	uint16_t	reserved02;	/* 0002 */

	/*
	 * core inode attributes, inode type, misc flags
	 */
	uint32_t	uflags;		/* 0004 chflags */
	uint32_t	rmajor;		/* 0008 available for device nodes */
	uint32_t	rminor;		/* 000C available for device nodes */
	uint64_t	ctime;		/* 0010 inode change time */
	uint64_t	mtime;		/* 0018 modified time */
	uint64_t	atime;		/* 0020 access time (unsupported) */
	uint64_t	btime;		/* 0028 birth time */
	uuid_t		uid;		/* 0030 uid / degenerate unix uid */
	uuid_t		gid;		/* 0040 gid / degenerate unix gid */

	uint8_t		type;		/* 0050 object type */
	uint8_t		op_flags;	/* 0051 operational flags */
	uint16_t	cap_flags;	/* 0052 capability flags */
	uint32_t	mode;		/* 0054 unix modes (typ low 16 bits) */

	/*
	 * inode size, identification, localized recursive configuration
	 * for compression and backup copies.
	 */
	hammer2_tid_t	inum;		/* 0058 inode number */
	hammer2_off_t	size;		/* 0060 size of file */
	uint64_t	nlinks;		/* 0068 hard links (typ only dirs) */
	hammer2_tid_t	iparent;	/* 0070 parent inum (recovery only) */
	hammer2_key_t	name_key;	/* 0078 full filename key */
	uint16_t	name_len;	/* 0080 filename length */
	uint8_t		ncopies;	/* 0082 ncopies to local media */
	uint8_t		comp_algo;	/* 0083 compression request & algo */

	/*
	 * These fields are currently only applicable to PFSROOTs.
	 *
	 * NOTE: We can't use {volume_data->fsid, pfs_clid} to uniquely
	 *	 identify an instance of a PFS in the cluster because
	 *	 a mount may contain more than one copy of the PFS as
	 *	 a separate node.  {pfs_clid, pfs_fsid} must be used for
	 *	 registration in the cluster.
	 */
	uint8_t		target_type;	/* 0084 hardlink target type */
	uint8_t		reserved85;	/* 0085 */
	uint8_t		reserved86;	/* 0086 */
	uint8_t		pfs_type;	/* 0087 (if PFSROOT) node type */
	uint64_t	pfs_inum;	/* 0088 (if PFSROOT) inum allocator */
	uuid_t		pfs_clid;	/* 0090 (if PFSROOT) cluster uuid */
	uuid_t		pfs_fsid;	/* 00A0 (if PFSROOT) unique uuid */

	/*
	 * Quotas and cumulative sub-tree counters.
	 */
	hammer2_key_t	data_quota;	/* 00B0 subtree quota in bytes */
	hammer2_key_t	data_count;	/* 00B8 subtree byte count */
	hammer2_key_t	inode_quota;	/* 00C0 subtree quota inode count */
	hammer2_key_t	inode_count;	/* 00C8 subtree inode count */
	hammer2_tid_t	attr_tid;	/* 00D0 attributes changed */
	hammer2_tid_t	dirent_tid;	/* 00D8 directory/attr changed */

	/*
	 * Tracks (possibly degenerate) free areas covering all sub-tree
	 * allocations under inode, not counting the inode itself.
	 * 0/0 indicates empty entry.  fully set-associative.
	 */
	hammer2_off_t	reservedE0[4];	/* 00E0/E8/F0/F8 */

	unsigned char	filename[HAMMER2_INODE_MAXNAME];
					/* 0100-01FF (256 char, unterminated) */
	union {				/* 0200-03FF (64x8 = 512 bytes) */
		struct hammer2_blockset blockset;
		char data[HAMMER2_EMBEDDED_BYTES];
	} u;
};

typedef struct hammer2_inode_data hammer2_inode_data_t;

#define HAMMER2_OPFLAG_DIRECTDATA	0x01
#define HAMMER2_OPFLAG_PFSROOT		0x02
#define HAMMER2_OPFLAG_COPYIDS		0x04	/* copyids override parent */

#define HAMMER2_OBJTYPE_UNKNOWN		0
#define HAMMER2_OBJTYPE_DIRECTORY	1
#define HAMMER2_OBJTYPE_REGFILE		2
#define HAMMER2_OBJTYPE_FIFO		4
#define HAMMER2_OBJTYPE_CDEV		5
#define HAMMER2_OBJTYPE_BDEV		6
#define HAMMER2_OBJTYPE_SOFTLINK	7
#define HAMMER2_OBJTYPE_HARDLINK	8	/* dummy entry for hardlink */
#define HAMMER2_OBJTYPE_SOCKET		9
#define HAMMER2_OBJTYPE_WHITEOUT	10

#define HAMMER2_COPYID_NONE		0
#define HAMMER2_COPYID_LOCAL		((uint8_t)-1)

/*
 * PEER types identify connections and help cluster controller filter
 * out unwanted SPANs.
 */
#define HAMMER2_PEER_NONE		DMSG_PEER_NONE
#define HAMMER2_PEER_CLUSTER		DMSG_PEER_CLUSTER
#define HAMMER2_PEER_BLOCK		DMSG_PEER_BLOCK
#define HAMMER2_PEER_HAMMER2		DMSG_PEER_HAMMER2

#define HAMMER2_COPYID_COUNT		DMSG_COPYID_COUNT

/*
 * PFS types identify a PFS on media and in LNK_SPAN messages.
 */
#define HAMMER2_PFSTYPE_NONE		DMSG_PFSTYPE_NONE
#define HAMMER2_PFSTYPE_ADMIN		DMSG_PFSTYPE_ADMIN
#define HAMMER2_PFSTYPE_CLIENT		DMSG_PFSTYPE_CLIENT
#define HAMMER2_PFSTYPE_CACHE		DMSG_PFSTYPE_CACHE
#define HAMMER2_PFSTYPE_COPY		DMSG_PFSTYPE_COPY
#define HAMMER2_PFSTYPE_SLAVE		DMSG_PFSTYPE_SLAVE
#define HAMMER2_PFSTYPE_SOFT_SLAVE	DMSG_PFSTYPE_SOFT_SLAVE
#define HAMMER2_PFSTYPE_SOFT_MASTER	DMSG_PFSTYPE_SOFT_MASTER
#define HAMMER2_PFSTYPE_MASTER		DMSG_PFSTYPE_MASTER
#define HAMMER2_PFSTYPE_SNAPSHOT	DMSG_PFSTYPE_SNAPSHOT
#define HAMMER2_PFSTYPE_MAX		DMSG_PFSTYPE_MAX

/*
 *				Allocation Table
 *
 */


/*
 * Flags (8 bits) - blockref, for freemap only
 *
 * Note that the minimum chunk size is 1KB so we could theoretically have
 * 10 bits here, but we might have some future extension that allows a
 * chunk size down to 256 bytes and if so we will need bits 8 and 9.
 */
#define HAMMER2_AVF_SELMASK		0x03	/* select group */
#define HAMMER2_AVF_ALL_ALLOC		0x04	/* indicate all allocated */
#define HAMMER2_AVF_ALL_FREE		0x08	/* indicate all free */
#define HAMMER2_AVF_RESERVED10		0x10
#define HAMMER2_AVF_RESERVED20		0x20
#define HAMMER2_AVF_RESERVED40		0x40
#define HAMMER2_AVF_RESERVED80		0x80
#define HAMMER2_AVF_AVMASK32		((uint32_t)0xFFFFFF00LU)
#define HAMMER2_AVF_AVMASK64		((uint64_t)0xFFFFFFFFFFFFFF00LLU)

#define HAMMER2_AV_SELECT_A		0x00
#define HAMMER2_AV_SELECT_B		0x01
#define HAMMER2_AV_SELECT_C		0x02
#define HAMMER2_AV_SELECT_D		0x03

/*
 * The volume header eats a 64K block.  There is currently an issue where
 * we want to try to fit all nominal filesystem updates in a 512-byte section
 * but it may be a lost cause due to the need for a blockset.
 *
 * All information is stored in host byte order.  The volume header's magic
 * number may be checked to determine the byte order.  If you wish to mount
 * between machines w/ different endian modes you'll need filesystem code
 * which acts on the media data consistently (either all one way or all the
 * other).  Our code currently does not do that.
 *
 * A read-write mount may have to recover missing allocations by doing an
 * incremental mirror scan looking for modifications made after alloc_tid.
 * If alloc_tid == last_tid then no recovery operation is needed.  Recovery
 * operations are usually very, very fast.
 *
 * Read-only mounts do not need to do any recovery, access to the filesystem
 * topology is always consistent after a crash (is always consistent, period).
 * However, there may be shortcutted blockref updates present from deep in
 * the tree which are stored in the volumeh eader and must be tracked on
 * the fly.
 *
 * NOTE: The copyinfo[] array contains the configuration for both the
 *	 cluster connections and any local media copies.  The volume
 *	 header will be replicated for each local media copy.
 *
 *	 The mount command may specify multiple medias or just one and
 *	 allow HAMMER2 to pick up the others when it checks the copyinfo[]
 *	 array on mount.
 *
 * NOTE: root_blockref points to the super-root directory, not the root
 *	 directory.  The root directory will be a subdirectory under the
 *	 super-root.
 *
 *	 The super-root directory contains all root directories and all
 *	 snapshots (readonly or writable).  It is possible to do a
 *	 null-mount of the super-root using special path constructions
 *	 relative to your mounted root.
 *
 * NOTE: HAMMER2 allows any subdirectory tree to be managed as if it were
 *	 a PFS, including mirroring and storage quota operations, and this is
 *	 prefered over creating discrete PFSs in the super-root.  Instead
 *	 the super-root is most typically used to create writable snapshots,
 *	 alternative roots, and so forth.  The super-root is also used by
 *	 the automatic snapshotting mechanism.
 */
#define HAMMER2_VOLUME_ID_HBO	0x48414d3205172011LLU
#define HAMMER2_VOLUME_ID_ABO	0x11201705324d4148LLU

struct hammer2_volume_data {
	/*
	 * sector #0 - 512 bytes
	 */
	uint64_t	magic;			/* 0000 Signature */
	hammer2_off_t	boot_beg;		/* 0008 Boot area (future) */
	hammer2_off_t	boot_end;		/* 0010 (size = end - beg) */
	hammer2_off_t	aux_beg;		/* 0018 Aux area (future) */
	hammer2_off_t	aux_end;		/* 0020 (size = end - beg) */
	hammer2_off_t	volu_size;		/* 0028 Volume size, bytes */

	uint32_t	version;		/* 0030 */
	uint32_t	flags;			/* 0034 */
	uint8_t		copyid;			/* 0038 copyid of phys vol */
	uint8_t		freemap_version;	/* 0039 freemap algorithm */
	uint8_t		peer_type;		/* 003A HAMMER2_PEER_xxx */
	uint8_t		reserved003B;		/* 003B */
	uint32_t	reserved003C;		/* 003C */

	uuid_t		fsid;			/* 0040 */
	uuid_t		fstype;			/* 0050 */

	/*
	 * allocator_size is precalculated at newfs time and does not include
	 * reserved blocks, boot, or redo areas.
	 *
	 * Initial non-reserved-area allocations do not use the freemap
	 * but instead adjust alloc_iterator.  Dynamic allocations take
	 * over starting at (allocator_beg).  This makes newfs_hammer2's
	 * job a lot easier and can also serve as a testing jig.
	 */
	hammer2_off_t	allocator_size;		/* 0060 Total data space */
	hammer2_off_t   allocator_free;		/* 0068	Free space */
	hammer2_off_t	allocator_beg;		/* 0070 Initial allocations */
	hammer2_tid_t	mirror_tid;		/* 0078 committed tid (vol) */
	hammer2_tid_t	alloc_tid;		/* 0080 Alloctable modify tid */
	hammer2_tid_t	inode_tid;		/* 0088 Inode allocator tid */
	hammer2_tid_t	freemap_tid;		/* 0090 committed tid (fmap) */
	hammer2_tid_t	bulkfree_tid;		/* 0098 bulkfree incremental */
	hammer2_tid_t	reserved00A0[5];	/* 00A0-00C7 */

	/*
	 * Copyids are allocated dynamically from the copyexists bitmap.
	 * An id from the active copies set (up to 8, see copyinfo later on)
	 * may still exist after the copy set has been removed from the
	 * volume header and its bit will remain active in the bitmap and
	 * cannot be reused until it is 100% removed from the hierarchy.
	 */
	uint32_t	copyexists[8];		/* 00C8-00E7 copy exists bmap */
	char		reserved0140[248];	/* 00E8-01DF */

	/*
	 * 32 bit CRC array at the end of the first 512 byte sector.
	 *
	 * icrc_sects[7] - First 512-4 bytes of volume header (including all
	 *		   the other icrc's except this one).
	 *
	 * icrc_sects[6] - Sector 1 (512 bytes) of volume header, which is
	 *		   the blockset for the root.
	 *
	 * icrc_sects[5] - Sector 2
	 * icrc_sects[4] - Sector 3
	 * icrc_sects[3] - Sector 4 (the freemap blockset)
	 */
	hammer2_crc32_t	icrc_sects[8];		/* 01E0-01FF */

	/*
	 * sector #1 - 512 bytes
	 *
	 * The entire sector is used by a blockset.
	 */
	hammer2_blockset_t sroot_blockset;	/* 0200-03FF Superroot dir */

	/*
	 * sector #2-7
	 */
	char	sector2[512];			/* 0400-05FF reserved */
	char	sector3[512];			/* 0600-07FF reserved */
	hammer2_blockset_t freemap_blockset;	/* 0800-09FF freemap  */
	char	sector5[512];			/* 0A00-0BFF reserved */
	char	sector6[512];			/* 0C00-0DFF reserved */
	char	sector7[512];			/* 0E00-0FFF reserved */

	/*
	 * sector #8-71	- 32768 bytes
	 *
	 * Contains the configuration for up to 256 copyinfo targets.  These
	 * specify local and remote copies operating as masters or slaves.
	 * copyid's 0 and 255 are reserved (0 indicates an empty slot and 255
	 * indicates the local media).
	 *
	 * Each inode contains a set of up to 8 copyids, either inherited
	 * from its parent or explicitly specified in the inode, which
	 * indexes into this array.
	 */
						/* 1000-8FFF copyinfo config */
	dmsg_vol_data_t	copyinfo[HAMMER2_COPYID_COUNT];

	/*
	 * Remaining sections are reserved for future use.
	 */
	char		reserved0400[0x6FFC];	/* 9000-FFFB reserved */

	/*
	 * icrc on entire volume header
	 */
	hammer2_crc32_t	icrc_volheader;		/* FFFC-FFFF full volume icrc*/
};

typedef struct hammer2_volume_data hammer2_volume_data_t;

/*
 * Various parts of the volume header have their own iCRCs.
 *
 * The first 512 bytes has its own iCRC stored at the end of the 512 bytes
 * and not included the icrc calculation.
 *
 * The second 512 bytes also has its own iCRC but it is stored in the first
 * 512 bytes so it covers the entire second 512 bytes.
 *
 * The whole volume block (64KB) has an iCRC covering all but the last 4 bytes,
 * which is where the iCRC for the whole volume is stored.  This is currently
 * a catch-all for anything not individually iCRCd.
 */
#define HAMMER2_VOL_ICRC_SECT0		7
#define HAMMER2_VOL_ICRC_SECT1		6

#define HAMMER2_VOLUME_BYTES		65536

#define HAMMER2_VOLUME_ICRC0_OFF	0
#define HAMMER2_VOLUME_ICRC1_OFF	512
#define HAMMER2_VOLUME_ICRCVH_OFF	0

#define HAMMER2_VOLUME_ICRC0_SIZE	(512 - 4)
#define HAMMER2_VOLUME_ICRC1_SIZE	(512)
#define HAMMER2_VOLUME_ICRCVH_SIZE	(65536 - 4)

#define HAMMER2_VOL_VERSION_MIN		1
#define HAMMER2_VOL_VERSION_DEFAULT	1
#define HAMMER2_VOL_VERSION_WIP 	2

#define HAMMER2_NUM_VOLHDRS		4

union hammer2_media_data {
	hammer2_volume_data_t	voldata;
        hammer2_inode_data_t    ipdata;
	hammer2_blockref_t	npdata[HAMMER2_IND_COUNT_MAX];
	hammer2_bmap_data_t	bmdata[HAMMER2_FREEMAP_COUNT];
	char			buf[HAMMER2_PBUFSIZE];
};

typedef union hammer2_media_data hammer2_media_data_t;

#endif /* !_VFS_HAMMER2_DISK_H_ */
