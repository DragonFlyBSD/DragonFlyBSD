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
hammer2_dev_exlock(hammer2_dev_t *hmp)
{
	hammer2_mtx_ex(&hmp->vchain.lock);
}

void
hammer2_dev_shlock(hammer2_dev_t *hmp)
{
	hammer2_mtx_sh(&hmp->vchain.lock);
}

void
hammer2_dev_unlock(hammer2_dev_t *hmp)
{
	hammer2_mtx_unlock(&hmp->vchain.lock);
}

/*
 * Return the directory entry type for an inode.
 *
 * ip must be locked sh/ex.
 */
int
hammer2_get_dtype(const hammer2_inode_data_t *ipdata)
{
	uint8_t type;

	if ((type = ipdata->meta.type) == HAMMER2_OBJTYPE_HARDLINK)
		type = ipdata->meta.target_type;

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
hammer2_get_vtype(uint8_t type)
{
	switch(type) {
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
hammer2_timespec_to_time(const struct timespec *ts)
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
hammer2_to_unix_xid(const uuid_t *uuid)
{
	return(*(const u_int32_t *)&uuid->node[2]);
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
 * size radix, HAMMER2_RADIX_MIN (10), which is 1KB.
 */
int
hammer2_allocsize(size_t bytes)
{
	int radix;

	if (bytes < HAMMER2_ALLOC_MIN)
		bytes = HAMMER2_ALLOC_MIN;
	if (bytes == HAMMER2_PBUFSIZE)
		radix = HAMMER2_PBUFRADIX;
	else if (bytes >= 16384)
		radix = 14;
	else if (bytes >= 1024)
		radix = 10;
	else
		radix = HAMMER2_RADIX_MIN;

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
	else if (bytes >= HAMMER2_ALLOC_MIN)	/* clamp */
		radix = HAMMER2_RADIX_MIN;
	else
		radix = 0;

	while (((size_t)1 << radix) < bytes)
		++radix;
	return (radix);
}

/*
 * The logical block size is currently always PBUFSIZE.
 */
int
hammer2_calc_logical(hammer2_inode_t *ip, hammer2_off_t uoff,
		     hammer2_key_t *lbasep, hammer2_key_t *leofp)
{
	KKASSERT(ip->flags & HAMMER2_INODE_METAGOOD);
	if (lbasep)
		*lbasep = uoff & ~HAMMER2_PBUFMASK64;
	if (leofp) {
		*leofp = (ip->meta.size + HAMMER2_PBUFMASK64) &
			 ~HAMMER2_PBUFMASK64;
	}
	return (HAMMER2_PBUFSIZE);
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

	KKASSERT(ip->flags & HAMMER2_INODE_METAGOOD);
	lblksize = hammer2_calc_logical(ip, lbase, NULL, NULL);
	if (lbase + lblksize <= ip->meta.size)
		return (lblksize);
	if (lbase >= ip->meta.size)
		return (0);
	eofbytes = (int)(ip->meta.size - lbase);
	pblksize = lblksize;
	while (pblksize >= eofbytes && pblksize >= HAMMER2_ALLOC_MIN)
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

void
hammer2_adjreadcounter(hammer2_blockref_t *bref, size_t bytes)
{
	long *counterp;

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_DATA:
		counterp = &hammer2_iod_file_read;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		counterp = &hammer2_iod_meta_read;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		counterp = &hammer2_iod_indr_read;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		counterp = &hammer2_iod_fmap_read;
		break;
	default:
		counterp = &hammer2_iod_volu_read;
		break;
	}
	*counterp += bytes;
}

int
hammer2_signal_check(time_t *timep)
{
	int error = 0;

	lwkt_user_yield();
	if (*timep != time_second) {
		*timep = time_second;
		if (CURSIG(curthread->td_lwp) != 0)
			error = EINTR;
	}
	return error;
}

const char *
hammer2_error_str(int error)
{
	const char *str;

	switch(error) {
	case HAMMER2_ERROR_NONE:
		str = "0";
		break;
	case HAMMER2_ERROR_IO:
		str = "I/O";
		break;
	case HAMMER2_ERROR_CHECK:
		str = "check/crc";
		break;
	case HAMMER2_ERROR_INCOMPLETE:
		str = "incomplete-node";
		break;
	default:
		str = "unknown";
		break;
	}
	return (str);
}
