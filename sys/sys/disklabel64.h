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
 * $DragonFly: src/sys/sys/disklabel64.h,v 1.1 2007/06/18 05:13:42 dillon Exp $
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
 * 64 disklabels start at offset 0 on the disk or slice they reside.  All
 * values are byte offsets, not block numbers, in order to allow portability.
 * Unlike the original 32 bit disklabels, the on-disk format for a 64 bit
 * disklabel is slice-relative and does not have to be translated.
 */

#define DISKMAGIC64	((u_int32_t)0xc4464c59)	/* The disk magic number */
#ifndef MAXPARTITIONS64
#define	MAXPARTITIONS64	16
#endif

#ifndef LOCORE

struct disklabel64 {
	u_int32_t d_magic;		/* the magic number */
	u_int32_t d_crc;		/* crc32() */
	u_int32_t d_align;		/* partition alignment requirement */
	u_int32_t d_npartitions;	/* number of partitions */
	struct uuid d_obj_uuid;		/* unique uuid for label */

	u_int64_t d_total_size;		/* total size incl everything (bytes) */
	u_int64_t d_bbase;		/* boot area base offset (bytes) */
					/* boot area is pbase - bbase */
	u_int64_t d_pbase;		/* first allocatable offset (bytes) */
	u_int64_t d_pstop;		/* last allocatable offset+1 (bytes) */
	u_int64_t d_abase;		/* location of backup copy if not 0 */

	struct partition64 {		/* the partition table */
		u_int64_t p_boffset;	/* slice relative offset, in bytes */
		u_int64_t p_bsize;	/* size of partition, in bytes */
		u_int32_t p_unused00;	/* reserved for future use */
		u_int32_t p_unused01;	/* reserved for future use */
		u_int32_t p_unused02;	/* reserved for future use */
		u_int32_t p_unused03;	/* reserved for future use */
		struct uuid p_type_uuid;/* mount type as UUID */
		struct uuid p_obj_uuid; /* unique uuid for storage */
	} d_partitions[MAXPARTITIONS64];/* actually may be more */
};

/*
 * Disk-specific ioctls.
 */
#define DIOCGDINFO64	_IOR('d', 101, struct disklabel64)
#define DIOCSDINFO64	_IOW('d', 102, struct disklabel64)
#define DIOCWDINFO64	_IOW('d', 103, struct disklabel64)
#define DIOCGDVIRGIN64	_IOR('d', 105, struct disklabel64)

#endif /* LOCORE */

#endif /* !_SYS_DISKLABEL64_H_ */
