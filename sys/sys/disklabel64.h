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
 */

#ifndef _SYS_DISKLABEL64_H_
#define	_SYS_DISKLABEL64_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#if defined(_KERNEL) && !defined(_SYS_SYSTM_H_)
#include <sys/systm.h>
#endif
#ifndef _SYS_IOCCOM_H_
#include <sys/ioccom.h>
#endif
#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif

/*
 * disklabel64's start at offset 0 on the disk or slice they reside.  All
 * values are byte offsets, not block numbers, in order to allow portability.
 * Unlike the original 32 bit disklabels, the on-disk format for a 64 bit
 * disklabel is slice-relative and does not have to be translated.
 *
 * Currently the number of partitions is limited to 16, but the virgin
 * disklabel code will leave enough space for 32.
 */
#define DISKMAGIC64	((u_int32_t)0xc4464c59)	/* The disk magic number */
#ifndef MAXPARTITIONS64
#define	MAXPARTITIONS64	16
#endif
#ifndef RESPARTITIONS64
#define	RESPARTITIONS64	32
#endif

/*
 * Space within the label reserved for the stage2 boot code.
 */
#ifndef BOOT2SIZE64
#define BOOT2SIZE64	(1024 * 32)
#endif


#ifndef LOCORE

/*
 * A disklabel64 starts at slice relative offset 0, NOT SECTOR 1.  In
 * otherwords, d_magic is at byte offset 512 within the slice, regardless
 * of the sector size.
 *
 * The d_reserved0 area is not included in the crc and any kernel writeback
 * of the label will not change the d_reserved area on-disk.  It is purely
 * a shim to allow us to avoid sector calculations when reading or
 * writing the label.  Since byte offsets are used in our 64 bit disklabel,
 * the entire disklabel and the I/O required to access it becomes
 * sector-agnostic.
 */
struct disklabel64 {
	char	  d_reserved0[512];	/* reserved or unused */
	u_int32_t d_magic;		/* the magic number */
	u_int32_t d_crc;		/* crc32() d_magic thru last part */
	u_int32_t d_align;		/* partition alignment requirement */
	u_int32_t d_npartitions;	/* number of partitions */
	struct uuid d_stor_uuid;	/* unique uuid for label */

	u_int64_t d_total_size;		/* total size incl everything (bytes) */
	u_int64_t d_bbase;		/* boot area base offset (bytes) */
					/* boot area is bbase - (pbase-1) */
	u_int64_t d_pbase;		/* first allocatable offset (bytes) */
	u_int64_t d_pstop;		/* last allocatable offset+1 (bytes) */
					/* pbase and pstop are physically aligned */
	u_int64_t d_abase;		/* location of backup copy if not 0 */

	u_char	  d_packname[64];
	u_char    d_reserved[64];

	/*
	 * Note: offsets are relative to the base of the slice, NOT to
	 * d_pbase.  Unlike 32 bit disklabels the on-disk format for
	 * a 64 bit disklabel remains slice-relative.
	 *
	 * An uninitialized partition has a p_boffset and p_bsize of 0.
	 *
	 * If p_fstype is not supported for a live partition it is set
	 * to FS_OTHER.  This is typically the case when the filesystem
	 * is identified by its uuid.
	 */
	struct partition64 {		/* the partition table */
		u_int64_t p_boffset;	/* slice relative offset, in bytes */
		u_int64_t p_bsize;	/* size of partition, in bytes */
		u_int8_t  p_fstype;
		u_int8_t  p_unused01;	/* reserved, must be 0 */
		u_int8_t  p_unused02;	/* reserved, must be 0 */
		u_int8_t  p_unused03;	/* reserved, must be 0 */
		u_int32_t p_unused04;	/* reserved, must be 0 */
		u_int32_t p_unused05;	/* reserved, must be 0 */
		u_int32_t p_unused06;	/* reserved, must be 0 */
		struct uuid p_type_uuid;/* mount type as UUID */
		struct uuid p_stor_uuid;/* unique uuid for storage */
	} d_partitions[MAXPARTITIONS64];/* actually may be more */
};

#ifdef _KERNEL
extern struct disklabel_ops disklabel64_ops;
#endif

/*
 * Disk-specific ioctls.
 */
#define DIOCGDINFO64	_IOR('d', 101, struct disklabel64)
#define DIOCSDINFO64	_IOW('d', 102, struct disklabel64)
#define DIOCWDINFO64	_IOW('d', 103, struct disklabel64)
#define DIOCGDVIRGIN64	_IOR('d', 105, struct disklabel64)

#endif /* LOCORE */

#endif /* !_SYS_DISKLABEL64_H_ */
