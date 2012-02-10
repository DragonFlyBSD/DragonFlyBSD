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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

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
hammer2_inode_lock_sh(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_SHARED);
}

void
hammer2_inode_lock_up(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_EXCLUSIVE);
	++ip->chain.busy;
	lockmgr(&ip->lk, LK_DOWNGRADE);
}

void
hammer2_inode_lock_ex(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_EXCLUSIVE);
}

void
hammer2_inode_unlock_ex(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_RELEASE);
}

void
hammer2_inode_unlock_up(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_UPGRADE);
	--ip->chain.busy;
	lockmgr(&ip->lk, LK_RELEASE);
}

void
hammer2_inode_unlock_sh(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_RELEASE);
}

/*
 * Mount-wide locks
 */

void
hammer2_mount_exlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->lk, LK_EXCLUSIVE);
}

void
hammer2_mount_shlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->lk, LK_SHARED);
}

void
hammer2_mount_unlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->lk, LK_RELEASE);
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

	return (key);
}
