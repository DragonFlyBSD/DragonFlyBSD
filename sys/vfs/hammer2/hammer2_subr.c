/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>
#include <sys/dirent.h>

#include "hammer2.h"

/*
 * Mount-wide locks
 */

void
hammer2_mount_exlock(hammer2_mount_t *hmp)
{
	ccms_thread_lock(&hmp->vchain.core->cst, CCMS_STATE_EXCLUSIVE);
}

void
hammer2_mount_shlock(hammer2_mount_t *hmp)
{
	ccms_thread_lock(&hmp->vchain.core->cst, CCMS_STATE_SHARED);
}

void
hammer2_mount_unlock(hammer2_mount_t *hmp)
{
	ccms_thread_unlock(&hmp->vchain.core->cst);
}

void
hammer2_voldata_lock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->voldatalk, LK_EXCLUSIVE);
}

void
hammer2_voldata_unlock(hammer2_mount_t *hmp, int modify)
{
	if (modify &&
	    (hmp->vchain.flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&hmp->vchain.flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(&hmp->vchain);
	}
	lockmgr(&hmp->voldatalk, LK_RELEASE);
}

/*
 * Return the directory entry type for an inode.
 *
 * ip must be locked sh/ex.
 */
int
hammer2_get_dtype(hammer2_chain_t *chain)
{
	uint8_t type;

	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);

	if ((type = chain->data->ipdata.type) == HAMMER2_OBJTYPE_HARDLINK)
		type = chain->data->ipdata.target_type;

	switch(type) {
	case HAMMER2_OBJTYPE_UNKNOWN:
		return (DT_UNKNOWN);
	case HAMMER2_OBJTYPE_DIRECTORY:
		return (DT_DIR);
	case HAMMER2_OBJTYPE_REGFILE:
		return (DT_REG);
	case HAMMER2_OBJTYPE_FIFO:
		return (DT_FIFO);
	case HAMMER2_OBJTYPE_CDEV:	/* not supported */
		return (DT_CHR);
	case HAMMER2_OBJTYPE_BDEV:	/* not supported */
		return (DT_BLK);
	case HAMMER2_OBJTYPE_SOFTLINK:
		return (DT_LNK);
	case HAMMER2_OBJTYPE_HARDLINK:	/* (never directly associated w/vp) */
		return (DT_UNKNOWN);
	case HAMMER2_OBJTYPE_SOCKET:
		return (DT_SOCK);
	case HAMMER2_OBJTYPE_WHITEOUT:	/* not supported */
		return (DT_UNKNOWN);
	default:
		return (DT_UNKNOWN);
	}
	/* not reached */
}

/*
 * Return the directory entry type for an inode
 */
int
hammer2_get_vtype(hammer2_chain_t *chain)
{
	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INODE);

	switch(chain->data->ipdata.type) {
	case HAMMER2_OBJTYPE_UNKNOWN:
		return (VBAD);
	case HAMMER2_OBJTYPE_DIRECTORY:
		return (VDIR);
	case HAMMER2_OBJTYPE_REGFILE:
		return (VREG);
	case HAMMER2_OBJTYPE_FIFO:
		return (VFIFO);
	case HAMMER2_OBJTYPE_CDEV:	/* not supported */
		return (VCHR);
	case HAMMER2_OBJTYPE_BDEV:	/* not supported */
		return (VBLK);
	case HAMMER2_OBJTYPE_SOFTLINK:
		return (VLNK);
	case HAMMER2_OBJTYPE_HARDLINK:	/* XXX */
		return (VBAD);
	case HAMMER2_OBJTYPE_SOCKET:
		return (VSOCK);
	case HAMMER2_OBJTYPE_WHITEOUT:	/* not supported */
		return (DT_UNKNOWN);
	default:
		return (DT_UNKNOWN);
	}
	/* not reached */
}

u_int8_t
hammer2_get_obj_type(enum vtype vtype)
{
	switch(vtype) {
	case VDIR:
		return(HAMMER2_OBJTYPE_DIRECTORY);
	case VREG:
		return(HAMMER2_OBJTYPE_REGFILE);
	case VFIFO:
		return(HAMMER2_OBJTYPE_FIFO);
	case VSOCK:
		return(HAMMER2_OBJTYPE_SOCKET);
	case VCHR:
		return(HAMMER2_OBJTYPE_CDEV);
	case VBLK:
		return(HAMMER2_OBJTYPE_BDEV);
	case VLNK:
		return(HAMMER2_OBJTYPE_SOFTLINK);
	default:
		return(HAMMER2_OBJTYPE_UNKNOWN);
	}
	/* not reached */
}

/*
 * Convert a hammer2 64-bit time to a timespec.
 */
void
hammer2_time_to_timespec(u_int64_t xtime, struct timespec *ts)
{
	ts->tv_sec = (unsigned long)(xtime / 1000000);
	ts->tv_nsec = (unsigned int)(xtime % 1000000) * 1000L;
}

u_int64_t
hammer2_timespec_to_time(struct timespec *ts)
{
	u_int64_t xtime;

	xtime = (unsigned)(ts->tv_nsec / 1000) +
		(unsigned long)ts->tv_sec * 1000000ULL;
	return(xtime);
}

/*
 * Convert a uuid to a unix uid or gid
 */
u_int32_t
hammer2_to_unix_xid(uuid_t *uuid)
{
	return(*(u_int32_t *)&uuid->node[2]);
}

void
hammer2_guid_to_uuid(uuid_t *uuid, u_int32_t guid)
{
	bzero(uuid, sizeof(*uuid));
	*(u_int32_t *)&uuid->node[2] = guid;
}

/*
 * Borrow HAMMER1's directory hash algorithm #1 with a few modifications.
 * The filename is split into fields which are hashed separately and then
 * added together.
 *
 * Differences include: bit 63 must be set to 1 for HAMMER2 (HAMMER1 sets
 * it to 0), this is because bit63=0 is used for hidden hardlinked inodes.
 * (This means we do not need to do a 0-check/or-with-0x100000000 either).
 *
 * Also, the iscsi crc code is used instead of the old crc32 code.
 */
hammer2_key_t
hammer2_dirhash(const unsigned char *name, size_t len)
{
	const unsigned char *aname = name;
	uint32_t crcx;
	uint64_t key;
	size_t i;
	size_t j;

	key = 0;

	/*
	 * m32
	 */
	crcx = 0;
	for (i = j = 0; i < len; ++i) {
		if (aname[i] == '.' ||
		    aname[i] == '-' ||
		    aname[i] == '_' ||
		    aname[i] == '~') {
			if (i != j)
				crcx += hammer2_icrc32(aname + j, i - j);
			j = i + 1;
		}
	}
	if (i != j)
		crcx += hammer2_icrc32(aname + j, i - j);

	/*
	 * The directory hash utilizes the top 32 bits of the 64-bit key.
	 * Bit 63 must be set to 1.
	 */
	crcx |= 0x80000000U;
	key |= (uint64_t)crcx << 32;

	/*
	 * l16 - crc of entire filename
	 *
	 * This crc reduces degenerate hash collision conditions
	 */
	crcx = hammer2_icrc32(aname, len);
	crcx = crcx ^ (crcx << 16);
	key |= crcx & 0xFFFF0000U;

	/*
	 * Set bit 15.  This allows readdir to strip bit 63 so a positive
	 * 64-bit cookie/offset can always be returned, and still guarantee
	 * that the values 0x0000-0x7FFF are available for artificial entries.
	 * ('.' and '..').
	 */
	key |= 0x8000U;

	return (key);
}

#if 0
/*
 * Return the power-of-2 radix greater or equal to
 * the specified number of bytes.
 *
 * Always returns at least the minimum media allocation
 * size radix, HAMMER2_MIN_RADIX (10), which is 1KB.
 */
int
hammer2_allocsize(size_t bytes)
{
	int radix;

	if (bytes < HAMMER2_MIN_ALLOC)
		bytes = HAMMER2_MIN_ALLOC;
	if (bytes == HAMMER2_PBUFSIZE)
		radix = HAMMER2_PBUFRADIX;
	else if (bytes >= 16384)
		radix = 14;
	else if (bytes >= 1024)
		radix = 10;
	else
		radix = HAMMER2_MIN_RADIX;

	while (((size_t)1 << radix) < bytes)
		++radix;
	return (radix);
}

#endif

/*
 * Convert bytes to radix with no limitations
 */
int
hammer2_getradix(size_t bytes)
{
	int radix;

	if (bytes == HAMMER2_PBUFSIZE)
		radix = HAMMER2_PBUFRADIX;
	else if (bytes >= HAMMER2_LBUFSIZE)
		radix = HAMMER2_LBUFRADIX;
	else if (bytes >= HAMMER2_MIN_ALLOC)	/* clamp */
		radix = HAMMER2_MIN_RADIX;
	else
		radix = 0;

	while (((size_t)1 << radix) < bytes)
		++radix;
	return (radix);
}

/*
 * ip must be locked sh/ex
 *
 * Use 16KB logical buffers for file blocks <= 1MB and 64KB logical buffers
 * otherwise.  The write code may utilize smaller device buffers when
 * compressing or handling the EOF case, but is not able to coalesce smaller
 * logical buffers into larger device buffers.
 *
 * For now this means that even large files will have a bunch of 16KB blocks
 * at the beginning of the file.  On the plus side this tends to cause small
 * files to cluster together in the freemap.
 */
int
hammer2_calc_logical(hammer2_inode_t *ip, hammer2_off_t uoff,
		     hammer2_key_t *lbasep, hammer2_key_t *leofp)
{
#if 0
	if (uoff < (hammer2_off_t)1024 * 1024) {
		if (lbasep)
			*lbasep = uoff & ~HAMMER2_LBUFMASK64;
		if (leofp) {
			if (ip->size > (hammer2_key_t)1024 * 1024)
				*leofp = (hammer2_key_t)1024 * 1024;
			else
				*leofp = (ip->size + HAMMER2_LBUFMASK64) &
					 ~HAMMER2_LBUFMASK64;
		}
		return (HAMMER2_LBUFSIZE);
	} else {
#endif
		if (lbasep)
			*lbasep = uoff & ~HAMMER2_PBUFMASK64;
		if (leofp) {
			*leofp = (ip->size + HAMMER2_PBUFMASK64) &
				 ~HAMMER2_PBUFMASK64;
		}
		return (HAMMER2_PBUFSIZE);
#if 0
	}
#endif
}

/*
 * Calculate the physical block size.  pblksize <= lblksize.  Primarily
 * used to calculate a smaller physical block for the logical block
 * containing the file EOF.
 *
 * Returns 0 if the requested base offset is beyond the file EOF.
 */
int
hammer2_calc_physical(hammer2_inode_t *ip, hammer2_key_t lbase)
{
	int lblksize;
	int pblksize;
	int eofbytes;

	lblksize = hammer2_calc_logical(ip, lbase, NULL, NULL);
	if (lbase + lblksize <= ip->chain->data->ipdata.size)
		return (lblksize);
	if (lbase >= ip->chain->data->ipdata.size)
		return (0);
	eofbytes = (int)(ip->chain->data->ipdata.size - lbase);
	pblksize = lblksize;
	while (pblksize >= eofbytes && pblksize >= HAMMER2_MIN_ALLOC)
		pblksize >>= 1;
	pblksize <<= 1;

	return (pblksize);
}

void
hammer2_update_time(uint64_t *timep)
{
	struct timeval tv;

	getmicrotime(&tv);
	*timep = (unsigned long)tv.tv_sec * 1000000 + tv.tv_usec;
}
