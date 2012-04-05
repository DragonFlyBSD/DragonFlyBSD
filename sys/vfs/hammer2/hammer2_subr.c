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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>
#include <sys/dirent.h>

#include "hammer2.h"

/*
 * HAMMER2 inode locks
 *
 * HAMMER2 offers shared locks, update locks, and exclusive locks on inodes.
 *
 * Shared locks allow concurrent access to an inode's fields, but exclude
 * access by concurrent exclusive locks.
 *
 * Update locks are interesting -- an update lock will be taken after all
 * shared locks on an inode are released, but once it is in place, shared
 * locks may proceed. The update field is signalled by a busy flag in the
 * inode. Only one update lock may be in place at a given time on an inode.
 *
 * Exclusive locks prevent concurrent access to the inode.
 *
 * XXX: What do we use each for? How is visibility to the inode controlled?
 */


void
hammer2_inode_lock_ex(hammer2_inode_t *ip)
{
	hammer2_chain_lock(ip->hmp, &ip->chain, HAMMER2_RESOLVE_ALWAYS);
}

void
hammer2_inode_unlock_ex(hammer2_inode_t *ip)
{
	hammer2_chain_unlock(ip->hmp, &ip->chain);
}

void
hammer2_inode_lock_sh(hammer2_inode_t *ip)
{
	KKASSERT(ip->chain.refs > 0);
	lockmgr(&ip->chain.lk, LK_SHARED);
}

void
hammer2_inode_unlock_sh(hammer2_inode_t *ip)
{
	lockmgr(&ip->chain.lk, LK_RELEASE);
}

/*
 * Soft-busy an inode.
 *
 * The inode must be exclusively locked while soft-busying or soft-unbusying
 * an inode.  Once busied or unbusied the caller can release the lock.
 */
void
hammer2_inode_busy(hammer2_inode_t *ip)
{
	if (ip->chain.busy++ == 0)
		hammer2_chain_ref(ip->hmp, &ip->chain);
}

void
hammer2_inode_unbusy(hammer2_inode_t *ip)
{
	if (--ip->chain.busy == 0)
		hammer2_chain_drop(ip->hmp, &ip->chain);
}

/*
 * Mount-wide locks
 */

void
hammer2_mount_exlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->vchain.lk, LK_EXCLUSIVE);
}

void
hammer2_mount_shlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->vchain.lk, LK_SHARED);
}

void
hammer2_mount_unlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->vchain.lk, LK_RELEASE);
}

void
hammer2_voldata_lock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->voldatalk, LK_EXCLUSIVE);
}

void
hammer2_voldata_unlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->voldatalk, LK_RELEASE);
}

/*
 * Return the directory entry type for an inode
 */
int
hammer2_get_dtype(hammer2_inode_t *ip)
{
	switch(ip->ip_data.type) {
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
hammer2_get_vtype(hammer2_inode_t *ip)
{
	switch(ip->ip_data.type) {
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

/*
 * Convert a uuid to a unix uid or gid
 */
u_int32_t
hammer2_to_unix_xid(uuid_t *uuid)
{
	return(*(u_int32_t *)&uuid->node[2]);
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

/*
 * Return the power-of-2 radix greater or equal to
 * the specified number of bytes.
 *
 * Always returns at least HAMMER2_MIN_RADIX (2^6).
 */
int
hammer2_bytes_to_radix(size_t bytes)
{
	int radix;

	if (bytes < HAMMER2_MIN_ALLOC)
		bytes = HAMMER2_MIN_ALLOC;
	if (bytes == HAMMER2_PBUFSIZE)
		radix = HAMMER2_PBUFRADIX;
	else if (bytes >= 1024)
		radix = 10;
	else
		radix = HAMMER2_MIN_RADIX;

	while (((size_t)1 << radix) < bytes)
		++radix;
	return (radix);
}

int
hammer2_calc_logical(hammer2_inode_t *ip, hammer2_off_t uoff,
		     hammer2_key_t *lbasep, hammer2_key_t *leofp)
{
	int radix;

	*lbasep = uoff & ~HAMMER2_PBUFMASK64;
	*leofp = ip->ip_data.size & ~HAMMER2_PBUFMASK;
	KKASSERT(*lbasep <= *leofp);
	if (*lbasep == *leofp) {
		radix = hammer2_bytes_to_radix(
				(size_t)(ip->ip_data.size - *leofp));
		if (radix < HAMMER2_MINALLOCRADIX)
			radix = HAMMER2_MINALLOCRADIX;
		*leofp += 1U << radix;
		return (1U << radix);
	} else {
		return (HAMMER2_PBUFSIZE);
	}
}
