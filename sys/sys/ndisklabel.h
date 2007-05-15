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
 * $DragonFly: src/sys/sys/Attic/ndisklabel.h,v 1.2 2007/05/15 17:51:02 dillon Exp $
 */

/*
 * DragonFly disk label
 */
#ifndef _SYS_NDISKLABEL_H_
#define	_SYS_NDISKLABEL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * A Dragonfly new-style disklabel always resides at byte offset 4096
 * from the beginning of the block device, regardless of the sector size.
 *
 * All offsets stored in the disklabel are in bytes relative to the
 * beginning of the block device, not relative to the disk label.
 */
#define DFLY_LABEL_BYTE_OFFSET	4096

/*
 * Misc constants.
 *
 * Disklabels are stored in native-endian format and are portable as long
 * as the code checks for and properly converts the label.  Only big-endian
 * and little-endian formats are supported.  All fields are structuralized.
 */
#define DFLY_DISKMAGIC		((u_int64_t)0xc4466c7942534430ULL)
#define DFLY_DISKMAGIC_OTHER	((u_int64_t)0x30445342796c46c4ULL)
#define DFLY_MAXPARTITIONS	16

#ifndef LOCORE

/*
 * The disk label and partitions a-p.  All offsets and sizes are in bytes
 * but must be sector-aligned.  Other then the alignment requirement, the
 * disklabel doesn't care what the physical sector size of the media is.
 *
 * All offsets are in bytes and are relative to the beginning of the
 * block device in question (raw disk, slice, whatever), NOT the beginning
 * of the label.
 */
struct dfly_disklabel {
	u_int64_t	d_magic;	/* magic number / endian check */
	u_int64_t	d_serialno;	/* initial disklabel creation */
	u_int64_t	d_timestamp;	/* timestamp of disklabel creation */
	u_int64_t	d_crc;		/* cyclic redundancy check for label */
					/* (after any endian translation) */
	u_int64_t	d_size;		/* size of block device in bytes */
	u_int32_t	d_npart;	/* actual number of partitions */
	u_int32_t	d_labelsize;	/* max size of label in bytes */
	u_int32_t	d_bootpart;	/* -1 if not defined */
	u_int32_t	d_version;	/* disklabel version control */
	char		d_label[64];	/* user defined label (mandatory) */
	char		d_reserved[128];

	/*
	 * Partitions do not have individual labels.  The filesystem or
	 * storage layer id is specified in ascii, 14 chars max, 0-extended.
	 * p_layer[15] must always be 0.
	 *
	 * All offsets and sizes are in bytes but still must be aligned to
	 * the native sector size of the media.
	 */
	struct dfly_diskpart {
		u_int64_t	p_offset;	/* offset in bytes */
		u_int64_t	p_size;		/* size in bytes */
		u_int32_t	p_flags;	/* misc flags */
		char		p_layer[16];	/* FS or storage layer ID */
	} d_partitions[DFLY_MAXPARTITIONS];
	/* might be extended further */
};

#endif

#define DFLY_PARTF_VALID	0x0001		/* partition defined */
#define DFLY_PARTF_GOOD		0x0002		/* fs/storage formatted */
#define DFLY_PARTF_RECURSE	0x0004		/* recursive disklabel */

#define DFLY_DISKLABEL_SWAP	"swap"		/* strcmp w/ p_layer[] */
#define DFLY_DISKLABEL_UFS	"ufs"		/* strcmp w/ p_layer[] */

#ifndef LOCORE

#define DFLY_DIOCGDINFO   _IOR('d', 101, struct dfly_disklabel) /* get */
#define DFLY_DIOCSDINFO	  _IOW('d', 102, struct dfly_disklabel) /* set */
#define DFLY_DIOCWDINFO	  _IOW('d', 103, struct dfly_disklabel) /* set, update */
#define DFLY_DIOCGDVIRGIN _IOR('d', 105, struct dfly_disklabel) /* get new */

#endif

#endif /* !_SYS_NDISKLABEL_H_ */
