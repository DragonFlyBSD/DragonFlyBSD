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
 * $DragonFly: src/sys/vfs/hammer/hammer_subs.c,v 1.26 2008/06/20 21:24:53 dillon Exp $
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

/*
 * Obtain a shared lock at a lower priority then thread waiting for an
 * exclusive lock.  To avoid a deadlock this may only be done if no other
 * shared locks are being held by the caller.
 */
void
hammer_lock_sh_lowpri(struct hammer_lock *lock)
{
	KKASSERT(lock->refs > 0);
	crit_enter();
	while (lock->locktd != NULL || lock->exwanted) {
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
 * meta-data.  The flusher holds the lock exclusively while the reblocker
 * and pruner use a shared lock.
 *
 * Modifying operations can run in parallel until the flusher needs to
 * sync the disk media.
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

int
hammer_nohistory(hammer_inode_t ip)
{
	if (ip->hmp->hflags & HMNT_NOHISTORY)
		return(1);
	if (ip->ino_data.uflags & (SF_NOHISTORY|UF_NOHISTORY))
		return(1);
	return(0);
}

/*
 * Return a namekey hash.   The 64 bit namekey hash consists of a 32 bit
 * crc in the MSB and 0 in the LSB.  The caller will use the low bits to
 * generate a unique key and will scan all entries with the same upper
 * 32 bits when issuing a lookup.
 *
 * We strip bit 63 in order to provide a positive key, this way a seek
 * offset of 0 will represent the base of the directory.
 *
 * This function can never return 0.  We use the MSB-0 space to synthesize
 * artificial directory entries such as "." and "..".
 */
int64_t
hammer_directory_namekey(void *name, int len)
{
	int64_t key;

	key = (int64_t)(crc32(name, len) & 0x7FFFFFFF) << 32;
	if (key == 0)
		key |= 0x100000000LL;
	return(key);
}

hammer_tid_t
hammer_now_tid(void)
{
	struct timespec ts;
	hammer_tid_t tid;

	getnanotime(&ts);
	tid = ts.tv_sec * 1000000000LL + ts.tv_nsec;
	return(tid);
}

hammer_tid_t
hammer_str_to_tid(const char *str)
{
	hammer_tid_t tid;
	int len = strlen(str);

	if (len > 10)
		tid = strtouq(str, NULL, 0);			/* full TID */
	else
		tid = strtouq(str, NULL, 0) * 1000000000LL;	/* time_t */
	return(tid);
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

