/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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
#ifndef VFS_HAMMER2_DISK_H_
#define VFS_HAMMER2_DISK_H_

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
 * to optimize storage efficiency.  The minimum fragment size is 64 bytes.
 * Since allocations are in powers of 2 fragments must also be sized in
 * powers of 2 (64, 128, 256, ... 65536).
 *
 * For the moment the maximum allocation size is HAMMER2_PBUFSIZE (64K),
 * which is 2^16.  Larger extents may be supported in the future.
 *
 * A full indirect block uses supports 1024 x 64-byte blockrefs.
 *
 * A maximally sized file (2^64-1 bytes) requires 5 indirect block levels.
 * The hammer2_blockset in the volume header or file inode has another 8
 * entries, giving us 66+3 = 69 bits of address space.  However, some bits
 * are taken up by (potentially) requests for redundant copies.  HAMMER2
 * currently supports up to 8 copies, which brings the address space down
 * to 66 bits and gives us 2 bits of leeway.
 */
#define HAMMER2_MIN_ALLOC	64	/* minimum allocation size */
#define HAMMER2_MIN_RADIX	6	/* minimum allocation size 2^N */
#define HAMMER2_MAX_RADIX	16	/* maximum allocation size 2^N */
#define HAMMER2_IND1_RADIX	26	/* lowest full indirect block radix */
#define HAMMER2_IND2_RADIX	36
#define HAMMER2_IND3_RADIX	46
#define HAMMER2_IND4_RADIX	56
#define HAMMER2_IND5_RADIX	66	/* highest full indirect block radix */

/*
 * HAMMER2 utilizes 64K physical buffers and 16K logical filesystem buffers.
 * The smaller logical filesystem buffers reduce ram waste when the OS is
 * caching lots of small files.
 */
#define HAMMER2_PBUFRADIX	16	/* physical buf (1<<16) bytes */
#define HAMMER2_PBUFSIZE	65536	/* fixed physical device buffer size */
#define HAMMER2_LBUFSIZE	16384	/* vnode/logical file buffer size */

/*
 * HAMMER2 processes blockrefs in sets of 8.  The set is fully associative,
 * is not sorted, and may contain holes.
 *
 * A full indirect block supports 1024 blockrefs.
 *
 * An inode embeds one set of blockrefs but may also use the data area for
 * up to 512 bytes of direct data.
 */
#define HAMMER2_SET_COUNT	8	/* direct entries & associativity */
#define HAMMER2_SET_RADIX	3
#define HAMMER2_IND_COUNT	1024	/* 1 << HAMMER2_IND_RADIX */
#define HAMMER2_IND_RADIX	10
#define HAMMER2_EMBEDDED_BYTES	512
#define HAMMER2_EMBEDDED_RADIX	9

#define HAMMER2_PBUFMASK	(HAMMER2_PBUFSIZE - 1)
#define HAMMER2_LBUFMASK	(HAMMER2_LBUFSIZE - 1)

#define HAMMER2_PBUFSIZE64	((hammer2_off_t)HAMMER2_PBUFSIZE)
#define HAMMER2_PBUFMASK64	((hammer2_off_t)HAMMER2_PBUFMASK)
#define HAMMER2_LBUFMASK64	((hammer2_off_t)HAMMER2_LBUFMASK)

#define HAMMER2_UUID_STRING	"5cbb9ad1-862d-11dc-a94d-01301bb8a9f5"

/*
 * A HAMMER2 filesystem is always sized in multiples of 8MB.
 *
 * A 4MB segment is reserved at the beginning of each 2GB zone.  This segment
 * contains the volume header, the free block table, and possibly other
 * information in the future.  4MB = 64 x 64K blocks.
 */
#define HAMMER2_VOLUME_ALIGN		(8 * 1024 * 1024)
#define HAMMER2_VOLUME_ALIGN64		((hammer2_off_t)HAMMER2_VOLUME_ALIGN)
#define HAMMER2_VOLUME_ALIGNMASK	(HAMMER2_VOLUME_ALIGN - 1)
#define HAMMER2_VOLUME_ALIGNMASK64     ((hammer2_off_t)HAMMER2_VOLUME_ALIGNMASK)

#define HAMMER2_NEWFS_ALIGN		(HAMMER2_VOLUME_ALIGN)
#define HAMMER2_NEWFS_ALIGN64		((hammer2_off_t)HAMMER2_VOLUME_ALIGN)
#define HAMMER2_NEWFS_ALIGNMASK		(HAMMER2_VOLUME_ALIGN - 1)
#define HAMMER2_NEWFS_ALIGNMASK64	((hammer2_off_t)HAMMER2_NEWFS_ALIGNMASK)

#define HAMMER2_RESERVE_BYTES64		(2LLU * 1024 * 1024 * 1024)
#define HAMMER2_RESERVE_MASK64		(HAMMER2_RESERVE_BYTES64 - 1)
#define HAMMER2_RESERVE_SEG		(4 * 1024 * 1024)
#define HAMMER2_RESERVE_SEG64		((hammer2_off_t)HAMMER2_RESERVE_SEG)
#define HAMMER2_RESERVE_BLOCKS		(HAMMER2_RESERVE_SEG / HAMMER2_PBUFSIZE)

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
 * to as a power of 2.  This value typically ranges from HAMMER2_MIN_RADIX
 * to HAMMER2_MAX_RADIX (6-16).  Larger values may be supported in the future
 * to support file extents.
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
#define HAMMER2_DIRHASH_LOMASK	0x00000000FFFFFFFFULL

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
	} check;
};

typedef struct hammer2_blockref hammer2_blockref_t;

#define HAMMER2_BREF_SYNC1		0x01	/* modification synchronized */
#define HAMMER2_BREF_SYNC2		0x02	/* modification committed */
#define HAMMER2_BREF_DESYNCCHLD		0x04	/* desynchronize children */
#define HAMMER2_BREF_DELETED		0x80	/* indicates a deletion */

#define HAMMER2_BLOCKREF_BYTES		64	/* blockref struct in bytes */

#define HAMMER2_BREF_TYPE_EMPTY		0
#define HAMMER2_BREF_TYPE_INODE		1
#define HAMMER2_BREF_TYPE_INDIRECT	2
#define HAMMER2_BREF_TYPE_DATA		3
#define HAMMER2_BREF_TYPE_VOLUME	255	/* pseudo-type */

#define HAMMER2_ENC_COMPMETHOD(n)	(n)
#define HAMMER2_ENC_CHECKMETHOD(n)	((n) << 4)
#define HAMMER2_DEC_COMPMETHOD(n)	((n) & 15)
#define HAMMER2_DEC_CHECKMETHOD(n)	(((n) >> 4) & 15)

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
 */
struct hammer2_blockset {
	hammer2_blockref_t	blockref[HAMMER2_SET_COUNT];
};

typedef struct hammer2_blockset hammer2_blockset_t;

/*
 * Catch programmer snafus
 */
#if (1 << HAMMER2_IND_RADIX) != HAMMER2_IND_COUNT
#error "hammer2 indirect radix is incorrect"
#endif
#if (HAMMER2_IND_COUNT * 64) != HAMMER2_PBUFSIZE
#error "hammer2 indirect entries is incorrect"
#endif
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
 * The media indirect block structure.
 */
struct hammer2_indblock_data {
	hammer2_blockset_t blocksets[HAMMER2_IND_COUNT / HAMMER2_SET_COUNT];
};

typedef struct hammer2_indblock_data hammer2_indblock_data_t;

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
 */
#define HAMMER2_INODE_BYTES		1024	/* (asserted by code) */
#define HAMMER2_INODE_MAXNAME		256	/* maximum name in bytes */
#define HAMMER2_INODE_VERSION_ONE	1

struct hammer2_inode_data {
	uint16_t	version;	/* 0000 inode data version */
	uint16_t	reserved02;	/* 0002 */
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

	hammer2_tid_t	inum;		/* 0058 inode number */
	hammer2_off_t	size;		/* 0060 size of file */
	uint64_t	nlinks;		/* 0068 hard links (typ only dirs) */
	hammer2_tid_t	iparent;	/* 0070 parent inum (recovery only) */
	uint64_t	reserved78;	/* 0078 */

	hammer2_off_t	data_quota;	/* 0080 subtree quota in bytes */
	hammer2_off_t	data_count;	/* 0088 subtree byte count */
	hammer2_off_t	inode_quota;	/* 0090 subtree quota inode count */
	hammer2_off_t	inode_count;	/* 0098 subtree inode count */
	uint16_t	name_len;	/* 00A0 filename length */
	uint8_t		comp_algo;	/* 00A2 compression request & algo */
	uint8_t		reservedA3;	/* 00A3 */
	uint32_t	reservedA4;	/* 00A4 */
	hammer2_key_t	name_key;	/* 00A8 full filename key */
	uint8_t		copyids[8];	/* 00B0 request copies to (up to 8) */
	uuid_t		pfsid;		/* 00B8 pfs uuid if PFSROOT */
	uint64_t	reservedC8;	/* 00C8 */
	uint64_t	reservedD0;	/* 00D0 */
	uint64_t	reservedD8;	/* 00D8 */
	uint64_t	reservedE0;	/* 00E0 */
	uint64_t	reservedE8;	/* 00E8 */
	uint64_t	reservedF0;	/* 00F0 */
	uint64_t	reservedF8;	/* 00F8 */

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

#define HAMMER2_OBJTYPE_UNKNOWN		0
#define HAMMER2_OBJTYPE_DIRECTORY	1
#define HAMMER2_OBJTYPE_REGFILE		2
#define HAMMER2_OBJTYPE_FIFO		4
#define HAMMER2_OBJTYPE_CDEV		5
#define HAMMER2_OBJTYPE_BDEV		6
#define HAMMER2_OBJTYPE_SOFTLINK	7
#define HAMMER2_OBJTYPE_HARDLINK	8
#define HAMMER2_OBJTYPE_SOCKET		9
#define HAMMER2_OBJTYPE_WHITEOUT	10

#define HAMMER2_COPYID_NONE		0
#define HAMMER2_COPYID_LOCAL		((uint8_t)-1)

#define HAMMER2_COMP_NONE		0
#define HAMMER2_COMP_AUTOZERO		1

#define HAMMER2_CHECK_NONE		0
#define HAMMER2_CHECK_ICRC		1

/*
 * The allocref structure represents the allocation table.  One 64K block
 * is broken down into 4096 x 16 byte entries.  Each indirect block chops
 * 11 bits off the 64-bit storage space, with leaf entries representing
 * 64KB blocks.  So:  (12, 12, 12, 12, 16) = 64 bit storage space.
 *
 * Each 64K freemap block breaks the 4096 entries into a 64x64 tree with
 * big_hint1 representing the top level every 64th entry and big_hint2
 * representing the lower level in each entry.  These fields specify the
 * largest contiguous radix (1-63) available for allocation in the related
 * sub-tree.  The largest contiguous radix available for the entire block
 * is saved in the parent (for the root this will be alloc_blockref in the
 * volume header).  The hints may be larger than actual and will be corrected
 * on the fly but must not be smaller.  The allocator uses the hints to
 * very quickly locate nearby blocks of the desired size.
 *
 * In indirect blocks the 64-bit free[_or_mask] field stores the total free
 * space for each of the 4096 sub-nodes in bytes.  The total free space
 * represented by the indirect block is stored in its parent.
 *
 * Each leaf element represents a 64K block.  A bitmap replaces the free space
 * count, giving us a 1KB allocation resolution.  A micro-allocation append
 * offset replaces the icrc field.  The micro-allocation feature is not
 * currently implemented and the field will be set to 65536.
 *
 * The allocation map uses reserved blocks so no data block reference is
 * required, only a bit in the flags field to specify which of two possible
 * reserved blocks to use.  This allows the allocation map to be flushed to
 * disk with minimal synchronization.
 */
struct hammer2_allocref {
	uint32_t	icrc_or_app;	/* node: icrc, leaf: append offset */
	uint16_t	flags;
	uint8_t		big_hint1;	/* upper level hint */
	uint8_t		big_hint2;	/* lower level hint */
	uint64_t	free_or_mask;	/* node: free bytes, leaf: bitmask */
};

typedef struct hammer2_allocref hammer2_allocref_t;

/*
 * WARNING - allocref size x entries must equate to the hammer buffer size,
 *	     and 12 bits per recursion is assumed by the allocator.
 *
 * ALTA-D	Since no data_offset is specified flags are needed to select
 *		which sub-block to recurse down into for root & internal nodes.
 *		(only ALTA and ALTB is currently supported).
 *
 * LEAF		Terminal entry, always set for leafs.  May be used to support
 *		4MB extent allocations and early termination in the future.
 *		(not required to shortcut allocation scans as the big_hint1/2
 *		fields are used for this).
 */
#define HAMMER2_ALLOCREF_BYTES		16	/* structure size */
#define HAMMER2_ALLOCREF_ENTRIES	4096	/* entries */
#define HAMMER2_ALLOCREF_RADIX		12	/* log2(entries) */

#if (HAMMER2_ALLOCREF_BYTES * HAMMER2_ALLOCREF_ENTRIES) != HAMMER2_PBUFSIZE
#error "allocref parameters do not fit in hammer buffer"
#endif
#if (1 << HAMMER2_ALLOCREF_RADIX) != HAMMER2_ALLOCREF_ENTRIES
#error "allocref parameters are inconsistent"
#endif

#define HAMMER2_ALLOCREF_ALTMASK	0x0003	/* select block for recurse */
#define HAMMER2_ALLOCREF_ALTA		0x0000
#define HAMMER2_ALLOCREF_ALTB		0x0001
#define HAMMER2_ALLOCREF_ALTC		0x0002	/* unsupported */
#define HAMMER2_ALLOCREF_ALTD		0x0003	/* unsupported */
#define HAMMER2_ALLOCREF_LEAF		0x0004

/*
 * Copies information stored in the volume header.  Typically formatted
 * e.g. like 'serno/A21343249.s1d'
 *
 * There are 8 copy_data[]'s in the volume header but up to 256 copyid's.
 * When a copy is removed its copyid remains reserved in the copyid bitmap
 * (copyexists[] bitmap in volume_data) until the copy references have
 * been removed from the entire filesystem and cannot be reused until the
 * removal is complete.  However, new copy entries with other ids can be
 * instantly added, replacing the original copy_data[]... which is fine as
 * long as the copyid does not conflict.
 *
 * This structure must be exactly 64 bytes long.
 */
struct hammer2_copy_data {
	uint8_t	copyid;		/* 0-255 */
	uint8_t flags;
	uint8_t reserved02;
	uint8_t reserved03;
	uint8_t path[60];	/* up to 59-char string, nul-terminated */
};

typedef struct hammer2_copy_data hammer2_copy_data_t;

#define COPYDATAF_OUTOFSYNC	0x0001

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
 * COPIES: Multiple copies may be specified on the mount line AND/OR you
 *	   just specify one and the mount code tries to pick up the others
 *	   from copyinfo[].  The copyid field in the volume header along
 *	   with the fsid validates the copies.
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
	 * 512-byte sector #0
	 */
	uint64_t	magic;			/* 0000 Signature */
	hammer2_off_t	boot_beg;		/* 0008 Boot area (future) */
	hammer2_off_t	boot_end;		/* 0010 (size = end - beg) */
	hammer2_off_t	redo_beg;		/* 0018 Redo area (future) */
	hammer2_off_t	redo_end;		/* 0020 (size = end - beg) */
	hammer2_off_t	volu_size;		/* 0028 Volume size, bytes */

	uint32_t	version;		/* 0030 */
	uint32_t	flags;			/* 0034 */
	uint8_t		copyid;			/* 0038 copyid of phys vol */
	uint8_t		freemap_version;	/* 0039 freemap algorithm */
	uint8_t		reserved003A;		/* 003A */
	uint8_t		reserved003B;		/* 003B */
	uint32_t	reserved003C;		/* 003C */

	uuid_t		fsid;			/* 0040 */
	uuid_t		fstype;			/* 0050 */

	/*
	 * allocator_size is precalculated at newfs time and does not include
	 * reserved blocks, boot, or redo areas.
	 *
	 * Initial non-reserved-area allocations do not use the allocation
	 * map but instead adjust alloc_iterator.  Dynamic allocations take
	 * over starting at (allocator_beg).  This makes newfs_hammer2's
	 * job a lot easier and can also serve as a testing jig.
	 */
	hammer2_off_t	allocator_size;		/* 0060 Total data space */
	hammer2_off_t   allocator_free;		/* 0068	Free space */
	hammer2_tid_t	allocator_beg;		/* 0070 Initial allocations */
	hammer2_tid_t	last_tid;		/* 0078 Last transaction id */
	hammer2_tid_t	alloc_tid;		/* 0080 Alloctable modify tid */
	hammer2_blockref_t alloc_blockref;	/* 0088-00C7 */

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
	 *		   the other icrc's except the last one).
	 *
	 * icrc_sects[6] - Second 512-4 bytes of volume header, which is
	 *		   the blockset for the root.
	 */
	hammer2_crc32_t	icrc_sects[8];		/* 01E0-01FF */

	/*
	 * 512-byte sector #1
	 *
	 * The entire sector is used by a blockset.
	 */
	hammer2_blockset_t sroot_blockset;	/* 0200 Superroot directory */

	/*
	 * 512-byte sector #2-33
	 *
	 * Up to 256 copyinfo specifications can be configured.  Note that
	 * any given subdirectory tree can only use 8 of the 256.  Having
	 * up to 256 configurable in the volume header allows
	 *
	 * A specification takes 64 bytes.  Each specification typically
	 * configures a device path such as 'serno/<serial>.s1d'.
	 */
	struct hammer2_copy_data copyinfo[256];	/* 0400-43FF copyinfo config */

	/*
	 * Remaining sections are reserved for future use.
	 */
	char		reserved0400[0xBBFC];	/* 4400-FFFB reserved */

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

/*
 * Prototypes for user & kernel functions.  Kernel-only prototypes are
 * elsewhere.
 */
uint32_t hammer2_icrc32(const void *buf, size_t size);
uint32_t hammer2_icrc32c(const void *buf, size_t size, uint32_t crc);

#endif
