/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_subs.c,v 1.35 2008/10/15 22:38:37 dillon Exp $
 */
/*
 * HAMMER structural locking
 */

#include "hammer.h"
#include <sys/dirent.h>

void
hammer_lock_ex_ident(struct hammer_lock *lock, const char *ident)
{
	thread_t td = curthread;

	KKASSERT(lock->refs > 0);
	crit_enter();
	if (lock->locktd != td) {
		while (lock->locktd != NULL || lock->lockcount) {
			++lock->exwanted;
			lock->wanted = 1;
			if (hammer_debug_locks) {
				kprintf("hammer_lock_ex: held by %p\n",
					lock->locktd);
			}
			++hammer_contention_count;
			tsleep(lock, 0, ident, 0);
			if (hammer_debug_locks)
				kprintf("hammer_lock_ex: try again\n");
			--lock->exwanted;
		}
		lock->locktd = td;
	}
	KKASSERT(lock->lockcount >= 0);
	++lock->lockcount;
	crit_exit();
}

/*
 * Try to obtain an exclusive lock
 */
int
hammer_lock_ex_try(struct hammer_lock *lock)
{
	thread_t td = curthread;

	KKASSERT(lock->refs > 0);
	crit_enter();
	if (lock->locktd != td) {
		if (lock->locktd != NULL || lock->lockcount) {
			crit_exit();
			return(EAGAIN);
		}
		lock->locktd = td;
	}
	KKASSERT(lock->lockcount >= 0);
	++lock->lockcount;
	crit_exit();
	return(0);
}

/*
 * Obtain a shared lock
 *
 * We do not give pending exclusive locks priority over shared locks as
 * doing so could lead to a deadlock.
 */
void
hammer_lock_sh(struct hammer_lock *lock)
{
	KKASSERT(lock->refs > 0);
	crit_enter();
	while (lock->locktd != NULL) {
		if (lock->locktd == curthread) {
			Debugger("hammer_lock_sh: lock_sh on exclusive");
			++lock->lockcount;
			crit_exit();
			return;
		}
		lock->wanted = 1;
		tsleep(lock, 0, "hmrlck", 0);
	}
	KKASSERT(lock->lockcount <= 0);
	--lock->lockcount;
	crit_exit();
}

int
hammer_lock_sh_try(struct hammer_lock *lock)
{
	KKASSERT(lock->refs > 0);
	crit_enter();
	if (lock->locktd) {
		crit_exit();
		return(EAGAIN);
	}
	KKASSERT(lock->lockcount <= 0);
	--lock->lockcount;
	crit_exit();
	return(0);
}

/*
 * Upgrade a shared lock to an exclusively held lock.  This function will
 * return EDEADLK If there is more then one shared holder.
 *
 * No error occurs and no action is taken if the lock is already exclusively
 * held by the caller.  If the lock is not held at all or held exclusively
 * by someone else, this function will panic.
 */
int
hammer_lock_upgrade(struct hammer_lock *lock)
{
	int error;

	crit_enter();
	if (lock->lockcount > 0) {
		if (lock->locktd != curthread)
			panic("hammer_lock_upgrade: illegal lock state");
		error = 0;
	} else if (lock->lockcount == -1) {
		lock->lockcount = 1;
		lock->locktd = curthread;
		error = 0;
	} else if (lock->lockcount != 0) {
		error = EDEADLK;
	} else {
		panic("hammer_lock_upgrade: lock is not held");
		/* NOT REACHED */
		error = 0;
	}
	crit_exit();
	return(error);
}

/*
 * Downgrade an exclusively held lock to a shared lock.
 */
void
hammer_lock_downgrade(struct hammer_lock *lock)
{
	KKASSERT(lock->lockcount == 1 && lock->locktd == curthread);
	crit_enter();
	lock->lockcount = -1;
	lock->locktd = NULL;
	if (lock->wanted) {
		lock->wanted = 0;
		wakeup(lock);
	}
	crit_exit();
	/* XXX memory barrier */
}

void
hammer_unlock(struct hammer_lock *lock)
{
	crit_enter();
	KKASSERT(lock->lockcount != 0);
	if (lock->lockcount < 0) {
		if (++lock->lockcount == 0 && lock->wanted) {
			lock->wanted = 0;
			wakeup(lock);
		}
	} else {
		KKASSERT(lock->locktd == curthread);
		if (--lock->lockcount == 0) {
			lock->locktd = NULL;
			if (lock->wanted) {
				lock->wanted = 0;
				wakeup(lock);
			}
		}

	}
	crit_exit();
}

/*
 * The calling thread must be holding a shared or exclusive lock.
 * Returns < 0 if lock is held shared, and > 0 if held exlusively.
 */
int
hammer_lock_status(struct hammer_lock *lock)
{
	if (lock->lockcount < 0)
		return(-1);
	if (lock->lockcount > 0)
		return(1);
	panic("hammer_lock_status: lock must be held: %p", lock);
}

void
hammer_ref(struct hammer_lock *lock)
{
	KKASSERT(lock->refs >= 0);
	crit_enter();
	++lock->refs;
	crit_exit();
}

void
hammer_unref(struct hammer_lock *lock)
{
	KKASSERT(lock->refs > 0);
	crit_enter();
	--lock->refs;
	crit_exit();
}

/*
 * The sync_lock must be held when doing any modifying operations on
 * meta-data.  It does not have to be held when modifying non-meta-data buffers
 * (backend or frontend).
 *
 * The flusher holds the lock exclusively while all other consumers hold it
 * shared.  All modifying operations made while holding the lock are atomic
 * in that they will be made part of the same flush group.
 *
 * Due to the atomicy requirement deadlock recovery code CANNOT release the
 * sync lock, nor can we give pending exclusive sync locks priority over
 * a shared sync lock as this could lead to a 3-way deadlock.
 */
void
hammer_sync_lock_ex(hammer_transaction_t trans)
{
	++trans->sync_lock_refs;
	hammer_lock_ex(&trans->hmp->sync_lock);
}

void
hammer_sync_lock_sh(hammer_transaction_t trans)
{
	++trans->sync_lock_refs;
	hammer_lock_sh(&trans->hmp->sync_lock);
}

int
hammer_sync_lock_sh_try(hammer_transaction_t trans)
{
	int error;

	++trans->sync_lock_refs;
	if ((error = hammer_lock_sh_try(&trans->hmp->sync_lock)) != 0)
		--trans->sync_lock_refs;
	return (error);
}

void
hammer_sync_unlock(hammer_transaction_t trans)
{
	--trans->sync_lock_refs;
	hammer_unlock(&trans->hmp->sync_lock);
}

/*
 * Misc
 */
u_int32_t
hammer_to_unix_xid(uuid_t *uuid)
{
	return(*(u_int32_t *)&uuid->node[2]);
}

void
hammer_guid_to_uuid(uuid_t *uuid, u_int32_t guid)
{
	bzero(uuid, sizeof(*uuid));
	*(u_int32_t *)&uuid->node[2] = guid;
}

void
hammer_time_to_timespec(u_int64_t xtime, struct timespec *ts)
{
	ts->tv_sec = (unsigned long)(xtime / 1000000);
	ts->tv_nsec = (unsigned int)(xtime % 1000000) * 1000L;
}

u_int64_t
hammer_timespec_to_time(struct timespec *ts)
{
	u_int64_t xtime;

	xtime = (unsigned)(ts->tv_nsec / 1000) +
		(unsigned long)ts->tv_sec * 1000000ULL;
	return(xtime);
}


/*
 * Convert a HAMMER filesystem object type to a vnode type
 */
enum vtype
hammer_get_vnode_type(u_int8_t obj_type)
{
	switch(obj_type) {
	case HAMMER_OBJTYPE_DIRECTORY:
		return(VDIR);
	case HAMMER_OBJTYPE_REGFILE:
		return(VREG);
	case HAMMER_OBJTYPE_DBFILE:
		return(VDATABASE);
	case HAMMER_OBJTYPE_FIFO:
		return(VFIFO);
	case HAMMER_OBJTYPE_SOCKET:
		return(VSOCK);
	case HAMMER_OBJTYPE_CDEV:
		return(VCHR);
	case HAMMER_OBJTYPE_BDEV:
		return(VBLK);
	case HAMMER_OBJTYPE_SOFTLINK:
		return(VLNK);
	default:
		return(VBAD);
	}
	/* not reached */
}

int
hammer_get_dtype(u_int8_t obj_type)
{
	switch(obj_type) {
	case HAMMER_OBJTYPE_DIRECTORY:
		return(DT_DIR);
	case HAMMER_OBJTYPE_REGFILE:
		return(DT_REG);
	case HAMMER_OBJTYPE_DBFILE:
		return(DT_DBF);
	case HAMMER_OBJTYPE_FIFO:
		return(DT_FIFO);
	case HAMMER_OBJTYPE_SOCKET:
		return(DT_SOCK);
	case HAMMER_OBJTYPE_CDEV:
		return(DT_CHR);
	case HAMMER_OBJTYPE_BDEV:
		return(DT_BLK);
	case HAMMER_OBJTYPE_SOFTLINK:
		return(DT_LNK);
	default:
		return(DT_UNKNOWN);
	}
	/* not reached */
}

u_int8_t
hammer_get_obj_type(enum vtype vtype)
{
	switch(vtype) {
	case VDIR:
		return(HAMMER_OBJTYPE_DIRECTORY);
	case VREG:
		return(HAMMER_OBJTYPE_REGFILE);
	case VDATABASE:
		return(HAMMER_OBJTYPE_DBFILE);
	case VFIFO:
		return(HAMMER_OBJTYPE_FIFO);
	case VSOCK:
		return(HAMMER_OBJTYPE_SOCKET);
	case VCHR:
		return(HAMMER_OBJTYPE_CDEV);
	case VBLK:
		return(HAMMER_OBJTYPE_BDEV);
	case VLNK:
		return(HAMMER_OBJTYPE_SOFTLINK);
	default:
		return(HAMMER_OBJTYPE_UNKNOWN);
	}
	/* not reached */
}

/*
 * Return flags for hammer_delete_at_cursor()
 */
int
hammer_nohistory(hammer_inode_t ip)
{
	if (ip->hmp->hflags & HMNT_NOHISTORY)
		return(HAMMER_DELETE_DESTROY);
	if (ip->ino_data.uflags & (SF_NOHISTORY|UF_NOHISTORY))
		return(HAMMER_DELETE_DESTROY);
	return(0);
}

/*
 * ALGORITHM VERSION 1:
 *	Return a namekey hash.   The 64 bit namekey hash consists of a 32 bit
 *	crc in the MSB and 0 in the LSB.  The caller will use the low 32 bits
 *	to generate a unique key and will scan all entries with the same upper
 *	32 bits when issuing a lookup.
 *
 *	0hhhhhhhhhhhhhhh hhhhhhhhhhhhhhhh 0000000000000000 0000000000000000
 *
 * ALGORITHM VERSION 2:
 *
 *	The 64 bit hash key is generated from the following components.  The
 *	first three characters are encoded as 5-bit quantities, the middle
 *	N characters are hashed into a 6 bit quantity, and the last two
 *	characters are encoded as 5-bit quantities.  A 32 bit hash of the
 *	entire filename is encoded in the low 32 bits.  Bit 0 is set to
 *	0 to guarantee us a 2^24 bit iteration space.
 *
 *	0aaaaabbbbbccccc mmmmmmyyyyyzzzzz hhhhhhhhhhhhhhhh hhhhhhhhhhhhhhh0
 *
 *	This gives us a domain sort for the first three characters, the last
 *	two characters, and breaks the middle space into 64 random domains.
 *	The domain sort folds upper case, lower case, digits, and punctuation
 *	spaces together, the idea being the filenames tend to not be a mix
 *	of those domains.
 *
 *	The 64 random domains act as a sub-sort for the middle characters
 *	but may cause a random seek.  If the filesystem is being accessed
 *	in sorted order we should tend to get very good linearity for most
 *	filenames and devolve into more random seeks otherwise.
 *
 * We strip bit 63 in order to provide a positive key, this way a seek
 * offset of 0 will represent the base of the directory.
 *
 * This function can never return 0.  We use the MSB-0 space to synthesize
 * artificial directory entries such as "." and "..".
 */
int64_t
hammer_directory_namekey(hammer_inode_t dip, const void *name, int len,
			 u_int32_t *max_iterationsp)
{
	int64_t key;
	int32_t crcx;
	const char *aname = name;

	switch (dip->ino_data.cap_flags & HAMMER_INODE_CAP_DIRHASH_MASK) {
	case HAMMER_INODE_CAP_DIRHASH_ALG0:
		key = (int64_t)(crc32(aname, len) & 0x7FFFFFFF) << 32;
		if (key == 0)
			key |= 0x100000000LL;
		*max_iterationsp = 0xFFFFFFFFU;
		break;
	case HAMMER_INODE_CAP_DIRHASH_ALG1:
		key = (u_int32_t)crc32(aname, len) & 0xFFFFFFFEU;

		switch(len) {
		default:
			crcx = crc32(aname + 3, len - 5);
			crcx = crcx ^ (crcx >> 6) ^ (crcx >> 12);
			key |=  (int64_t)(crcx & 0x3F) << 42;
			/* fall through */
		case 5:
		case 4:
			/* fall through */
		case 3:
			key |= ((int64_t)(aname[2] & 0x1F) << 48);
			/* fall through */
		case 2:
			key |= ((int64_t)(aname[1] & 0x1F) << 53) |
			       ((int64_t)(aname[len-2] & 0x1F) << 37);
			/* fall through */
		case 1:
			key |= ((int64_t)(aname[0] & 0x1F) << 58) |
			       ((int64_t)(aname[len-1] & 0x1F) << 32);
			/* fall through */
		case 0:
			break;
		}
		if ((key & 0xFFFFFFFF00000000LL) == 0)
			key |= 0x100000000LL;
		if (hammer_debug_general & 0x0400) {
			kprintf("namekey2: 0x%016llx %*.*s\n",
				key, len, len, aname);
		}
		*max_iterationsp = 0x00FFFFFF;
		break;
	case HAMMER_INODE_CAP_DIRHASH_ALG2:
	case HAMMER_INODE_CAP_DIRHASH_ALG3:
	default:
		key = 0;			/* compiler warning */
		*max_iterationsp = 1;		/* sanity */
		panic("hammer_directory_namekey: bad algorithm %p\n", dip);
		break;
	}
	return(key);
}

/*
 * Convert string after @@ (@@ not included) to TID.  Returns 0 on success,
 * EINVAL on failure.
 *
 * If this function fails *ispfs, *tidp, and *localizationp will not
 * be modified.
 */
int
hammer_str_to_tid(const char *str, int *ispfsp,
		  hammer_tid_t *tidp, u_int32_t *localizationp)
{
	hammer_tid_t tid;
	u_int32_t localization;
	char *ptr;
	int ispfs;
	int n;

	/*
	 * Forms allowed for TID:  "0x%016llx"
	 *			   "-1"
	 */
	tid = strtouq(str, &ptr, 0);
	n = ptr - str;
	if (n == 2 && str[0] == '-' && str[1] == '1') {
		/* ok */
	} else if (n == 18 && str[0] == '0' && (str[1] | 0x20) == 'x') {
		/* ok */
	} else {
		return(EINVAL);
	}

	/*
	 * Forms allowed for PFS:  ":%05d"  (i.e. "...:0" would be illegal).
	 */
	str = ptr;
	if (*str == ':') {
		localization = strtoul(str + 1, &ptr, 10) << 16;
		if (ptr - str != 6)
			return(EINVAL);
		str = ptr;
		ispfs = 1;
	} else {
		localization = *localizationp;
		ispfs = 0;
	}

	/*
	 * Any trailing junk invalidates special extension handling.
	 */
	if (*str)
		return(EINVAL);
	*tidp = tid;
	*localizationp = localization;
	*ispfsp = ispfs;
	return(0);
}

void
hammer_crc_set_blockmap(hammer_blockmap_t blockmap)
{
	blockmap->entry_crc = crc32(blockmap, HAMMER_BLOCKMAP_CRCSIZE);
}

void
hammer_crc_set_volume(hammer_volume_ondisk_t ondisk)
{
	ondisk->vol_crc = crc32(ondisk, HAMMER_VOL_CRCSIZE1) ^
			  crc32(&ondisk->vol_crc + 1, HAMMER_VOL_CRCSIZE2);
}

int
hammer_crc_test_blockmap(hammer_blockmap_t blockmap)
{
	hammer_crc_t crc;

	crc = crc32(blockmap, HAMMER_BLOCKMAP_CRCSIZE);
	return (blockmap->entry_crc == crc);
}

int
hammer_crc_test_volume(hammer_volume_ondisk_t ondisk)
{
	hammer_crc_t crc;

	crc = crc32(ondisk, HAMMER_VOL_CRCSIZE1) ^
	      crc32(&ondisk->vol_crc + 1, HAMMER_VOL_CRCSIZE2);
	return (ondisk->vol_crc == crc);
}

int
hammer_crc_test_btree(hammer_node_ondisk_t ondisk)
{
	hammer_crc_t crc;

	crc = crc32(&ondisk->crc + 1, HAMMER_BTREE_CRCSIZE);
	return (ondisk->crc == crc);
}

/*
 * Test or set the leaf->data_crc field.  Deal with any special cases given
 * a generic B-Tree leaf element and its data.
 *
 * NOTE: Inode-data: the atime and mtime fields are not CRCd, allowing them
 *       to be updated in-place.
 */
int
hammer_crc_test_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
	hammer_crc_t crc;

	if (leaf->data_len == 0) {
		crc = 0;
	} else {
		switch(leaf->base.rec_type) {
		case HAMMER_RECTYPE_INODE:
			if (leaf->data_len != sizeof(struct hammer_inode_data))
				return(0);
			crc = crc32(data, HAMMER_INODE_CRCSIZE);
			break;
		default:
			crc = crc32(data, leaf->data_len);
			break;
		}
	}
	return (leaf->data_crc == crc);
}

void
hammer_crc_set_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
	if (leaf->data_len == 0) {
		leaf->data_crc = 0;
	} else {
		switch(leaf->base.rec_type) {
		case HAMMER_RECTYPE_INODE:
			KKASSERT(leaf->data_len ==
				  sizeof(struct hammer_inode_data));
			leaf->data_crc = crc32(data, HAMMER_INODE_CRCSIZE);
			break;
		default:
			leaf->data_crc = crc32(data, leaf->data_len);
			break;
		}
	}
}

void
hkprintf(const char *ctl, ...)
{
	__va_list va;

	if (hammer_debug_debug) {
		__va_start(va, ctl);
		kvprintf(ctl, va);
		__va_end(va);
	}
}

/*
 * Return the block size at the specified file offset.
 */
int
hammer_blocksize(int64_t file_offset)
{
	if (file_offset < HAMMER_XDEMARC)
		return(HAMMER_BUFSIZE);
	else
		return(HAMMER_XBUFSIZE);
}

/*
 * Return the demarkation point between the two offsets where
 * the block size changes. 
 */
int64_t
hammer_blockdemarc(int64_t file_offset1, int64_t file_offset2)
{
	if (file_offset1 < HAMMER_XDEMARC) {
		if (file_offset2 <= HAMMER_XDEMARC)
			return(file_offset2);
		return(HAMMER_XDEMARC);
	}
	panic("hammer_blockdemarc: illegal range %lld %lld\n",
	      file_offset1, file_offset2);
}

udev_t
hammer_fsid_to_udev(uuid_t *uuid)
{
	u_int32_t crc;

	crc = crc32(uuid, sizeof(*uuid));
	return((udev_t)crc);
}

