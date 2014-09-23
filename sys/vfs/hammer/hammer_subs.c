/*
 * Copyright (c) 2007-2011 The DragonFly Project.  All rights reserved.
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
/*
 * HAMMER structural locking
 */

#include "hammer.h"
#include <sys/dirent.h>

void
hammer_lock_ex_ident(struct hammer_lock *lock, const char *ident)
{
	thread_t td = curthread;
	u_int lv;
	u_int nlv;

	KKASSERT(lock->refs);
	for (;;) {
		lv = lock->lockval;

		if (lv == 0) {
			nlv = 1 | HAMMER_LOCKF_EXCLUSIVE;
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				lock->lowner = td;
				break;
			}
		} else if ((lv & HAMMER_LOCKF_EXCLUSIVE) &&
			   lock->lowner == td) {
			nlv = (lv + 1);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv))
				break;
		} else {
			if (hammer_debug_locks) {
				kprintf("hammer_lock_ex: held by %p\n",
					lock->lowner);
			}
			nlv = lv | HAMMER_LOCKF_WANTED;
			++hammer_contention_count;
			tsleep_interlock(&lock->lockval, 0);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				tsleep(&lock->lockval, PINTERLOCKED, ident, 0);
				if (hammer_debug_locks)
					kprintf("hammer_lock_ex: try again\n");
			}
		}
	}
}

/*
 * Try to obtain an exclusive lock
 */
int
hammer_lock_ex_try(struct hammer_lock *lock)
{
	thread_t td = curthread;
	int error;
	u_int lv;
	u_int nlv;

	KKASSERT(lock->refs);
	for (;;) {
		lv = lock->lockval;

		if (lv == 0) {
			nlv = 1 | HAMMER_LOCKF_EXCLUSIVE;
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				lock->lowner = td;
				error = 0;
				break;
			}
		} else if ((lv & HAMMER_LOCKF_EXCLUSIVE) &&
			   lock->lowner == td) {
			nlv = (lv + 1);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				error = 0;
				break;
			}
		} else {
			error = EAGAIN;
			break;
		}
	}
	return (error);
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
	thread_t td = curthread;
	u_int lv;
	u_int nlv;
	const char *ident = "hmrlck";

	KKASSERT(lock->refs);
	for (;;) {
		lv = lock->lockval;

		if ((lv & HAMMER_LOCKF_EXCLUSIVE) == 0) {
			nlv = (lv + 1);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv))
				break;
		} else if (lock->lowner == td) {
			/*
			 * Disallowed case, drop into kernel debugger for
			 * now.  A cont continues w/ an exclusive lock.
			 */
			nlv = (lv + 1);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				if (hammer_debug_critical)
					Debugger("hammer_lock_sh: holding ex");
				break;
			}
		} else {
			nlv = lv | HAMMER_LOCKF_WANTED;
			++hammer_contention_count;
			tsleep_interlock(&lock->lockval, 0);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv))
				tsleep(&lock->lockval, PINTERLOCKED, ident, 0);
		}
	}
}

int
hammer_lock_sh_try(struct hammer_lock *lock)
{
	thread_t td = curthread;
	u_int lv;
	u_int nlv;
	int error;

	KKASSERT(lock->refs);
	for (;;) {
		lv = lock->lockval;

		if ((lv & HAMMER_LOCKF_EXCLUSIVE) == 0) {
			nlv = (lv + 1);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				error = 0;
				break;
			}
		} else if (lock->lowner == td) {
			/*
			 * Disallowed case, drop into kernel debugger for
			 * now.  A cont continues w/ an exclusive lock.
			 */
			nlv = (lv + 1);
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				if (hammer_debug_critical)
					Debugger("hammer_lock_sh: holding ex");
				error = 0;
				break;
			}
		} else {
			error = EAGAIN;
			break;
		}
	}
	return (error);
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
hammer_lock_upgrade(struct hammer_lock *lock, int shcount)
{
	thread_t td = curthread;
	u_int lv;
	u_int nlv;
	int error;

	for (;;) {
		lv = lock->lockval;

		if ((lv & ~HAMMER_LOCKF_WANTED) == shcount) {
			nlv = lv | HAMMER_LOCKF_EXCLUSIVE;
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				lock->lowner = td;
				error = 0;
				break;
			}
		} else if (lv & HAMMER_LOCKF_EXCLUSIVE) {
			if (lock->lowner != curthread)
				panic("hammer_lock_upgrade: illegal state");
			error = 0;
			break;
		} else if ((lv & ~HAMMER_LOCKF_WANTED) == 0) {
			panic("hammer_lock_upgrade: lock is not held");
			/* NOT REACHED */
			error = EDEADLK;
			break;
		} else {
			error = EDEADLK;
			break;
		}
	}
	return (error);
}

/*
 * Downgrade an exclusively held lock to a shared lock.
 */
void
hammer_lock_downgrade(struct hammer_lock *lock, int shcount)
{
	thread_t td __debugvar = curthread;
	u_int lv;
	u_int nlv;

	KKASSERT((lock->lockval & ~HAMMER_LOCKF_WANTED) ==
		 (HAMMER_LOCKF_EXCLUSIVE | shcount));
	KKASSERT(lock->lowner == td);

	/*
	 * NOTE: Must clear owner before releasing exclusivity
	 */
	lock->lowner = NULL;

	for (;;) {
		lv = lock->lockval;
		nlv = lv & ~(HAMMER_LOCKF_EXCLUSIVE | HAMMER_LOCKF_WANTED);
		if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
			if (lv & HAMMER_LOCKF_WANTED)
				wakeup(&lock->lockval);
			break;
		}
	}
}

void
hammer_unlock(struct hammer_lock *lock)
{
	thread_t td __debugvar = curthread;
	u_int lv;
	u_int nlv;

	lv = lock->lockval;
	KKASSERT(lv != 0);
	if (lv & HAMMER_LOCKF_EXCLUSIVE)
		KKASSERT(lock->lowner == td);

	for (;;) {
		lv = lock->lockval;
		nlv = lv & ~(HAMMER_LOCKF_EXCLUSIVE | HAMMER_LOCKF_WANTED);
		if (nlv > 1) {
			nlv = lv - 1;
			if (atomic_cmpset_int(&lock->lockval, lv, nlv))
				break;
		} else if (nlv == 1) {
			nlv = 0;
			if (lv & HAMMER_LOCKF_EXCLUSIVE)
				lock->lowner = NULL;
			if (atomic_cmpset_int(&lock->lockval, lv, nlv)) {
				if (lv & HAMMER_LOCKF_WANTED)
					wakeup(&lock->lockval);
				break;
			}
		} else {
			panic("hammer_unlock: lock %p is not held", lock);
		}
	}
}

/*
 * The calling thread must be holding a shared or exclusive lock.
 * Returns < 0 if lock is held shared, and > 0 if held exlusively.
 */
int
hammer_lock_status(struct hammer_lock *lock)
{
	u_int lv = lock->lockval;

	if (lv & HAMMER_LOCKF_EXCLUSIVE)
		return(1);
	else if (lv)
		return(-1);
	panic("hammer_lock_status: lock must be held: %p", lock);
}

/*
 * Bump the ref count for a lock (not the excl/share count, but a separate
 * structural reference count).  The CHECK flag will be set on a 0->1
 * transition.
 *
 * This function does nothing to serialize races between multple threads.
 * The caller can interlock it later on to deal with serialization.
 *
 * MPSAFE
 */
void
hammer_ref(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		if ((lv & ~HAMMER_REFS_FLAGS) == 0) {
			nlv = (lv + 1) | HAMMER_REFS_CHECK;
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				return;
		} else {
			nlv = (lv + 1);
			KKASSERT((int)nlv > 0);
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				return;
		}
	}
	/* not reached */
}

/*
 * Drop the ref count for a lock (not the excl/share count, but a separate
 * structural reference count).  The CHECK flag will be cleared on a 1->0
 * transition.
 *
 * This function does nothing to serialize races between multple threads.
 *
 * MPSAFE
 */
void
hammer_rel(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		if ((lv & ~HAMMER_REFS_FLAGS) == 1) {
			nlv = (lv - 1) & ~HAMMER_REFS_CHECK;
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				return;
		} else {
			KKASSERT((int)lv > 0);
			nlv = (lv - 1);
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				return;
		}
	}
	/* not reached */
}

/*
 * The hammer_*_interlock() and hammer_*_interlock_done() functions are
 * more sophisticated versions which handle MP transition races and block
 * when necessary.
 *
 * hammer_ref_interlock() bumps the ref-count and conditionally acquires
 * the interlock for 0->1 transitions or if the CHECK is found to be set.
 *
 * This case will return TRUE, the interlock will be held, and the CHECK
 * bit also set.  Other threads attempting to ref will see the CHECK bit
 * and block until we clean up.
 *
 * FALSE is returned for transitions other than 0->1 when the CHECK bit
 * is not found to be set, or if the function loses the race with another
 * thread.
 *
 * TRUE is only returned to one thread and the others will block.
 * Effectively a TRUE indicator means 'someone transitioned 0->1
 * and you are the first guy to successfully lock it after that, so you
 * need to check'.  Due to races the ref-count may be greater than 1 upon
 * return.
 *
 * MPSAFE
 */
int
hammer_ref_interlock(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	/*
	 * Integrated reference count bump, lock, and check, with hot-path.
	 *
	 * (a) Return 1	(+LOCKED, +CHECK)	0->1 transition
	 * (b) Return 0 (-LOCKED, -CHECK)	N->N+1 transition
	 * (c) Break out (+CHECK)		Check condition and Cannot lock
	 * (d) Return 1 (+LOCKED, +CHECK)	Successfully locked
	 */
	for (;;) {
		lv = lock->refs;
		if (lv == 0) {
			nlv = 1 | HAMMER_REFS_LOCKED | HAMMER_REFS_CHECK;
			if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
				lock->rowner = curthread;
				return(1);
			}
		} else {
			nlv = (lv + 1);
			if ((lv & ~HAMMER_REFS_FLAGS) == 0)
				nlv |= HAMMER_REFS_CHECK;
			if ((nlv & HAMMER_REFS_CHECK) == 0) {
				if (atomic_cmpset_int(&lock->refs, lv, nlv))
					return(0);
			} else if (lv & HAMMER_REFS_LOCKED) {
				/* CHECK also set here */
				if (atomic_cmpset_int(&lock->refs, lv, nlv))
					break;
			} else {
				/* CHECK also set here */
				nlv |= HAMMER_REFS_LOCKED;
				if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
					lock->rowner = curthread;
					return(1);
				}
			}
		}
	}

	/*
	 * Defered check condition because we were unable to acquire the
	 * lock.  We must block until the check condition is cleared due
	 * to a race with another thread, or we are able to acquire the
	 * lock.
	 *
	 * (a) Return 0	(-CHECK)		Another thread handled it
	 * (b) Return 1 (+LOCKED, +CHECK)	We handled it.
	 */
	for (;;) {
		lv = lock->refs;
		if ((lv & HAMMER_REFS_CHECK) == 0)
			return(0);
		if (lv & HAMMER_REFS_LOCKED) {
			tsleep_interlock(&lock->refs, 0);
			nlv = (lv | HAMMER_REFS_WANTED);
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				tsleep(&lock->refs, PINTERLOCKED, "h1lk", 0);
		} else {
			/* CHECK also set here */
			nlv = lv | HAMMER_REFS_LOCKED;
			if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
				lock->rowner = curthread;
				return(1);
			}
		}
	}
	/* not reached */
}

/*
 * This is the same as hammer_ref_interlock() but asserts that the
 * 0->1 transition is always true, thus the lock must have no references
 * on entry or have CHECK set, and will have one reference with the
 * interlock held on return.  It must also not be interlocked on entry
 * by anyone.
 *
 * NOTE that CHECK will never be found set when the ref-count is 0.
 *
 * TRUE is always returned to match the API for hammer_ref_interlock().
 * This function returns with one ref, the lock held, and the CHECK bit set.
 */
int
hammer_ref_interlock_true(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;

		if (lv) {
			panic("hammer_ref_interlock_true: bad lock %p %08x",
			      lock, lock->refs);
		}
		nlv = 1 | HAMMER_REFS_LOCKED | HAMMER_REFS_CHECK;
		if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
			lock->rowner = curthread;
			return (1);
		}
	}
}

/*
 * Unlock the interlock acquired by hammer_ref_interlock() and clear the
 * CHECK flag.  The ref-count remains unchanged.
 *
 * This routine is called in the load path when the load succeeds.
 */
void
hammer_ref_interlock_done(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		nlv = lv & ~HAMMER_REFS_FLAGS;
		if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
			if (lv & HAMMER_REFS_WANTED)
				wakeup(&lock->refs);
			break;
		}
	}
}

/*
 * hammer_rel_interlock() works a bit differently in that it must
 * acquire the lock in tandem with a 1->0 transition.  CHECK is
 * not used.
 *
 * TRUE is returned on 1->0 transitions with the lock held on return
 * and FALSE is returned otherwise with the lock not held.
 *
 * It is important to note that the refs are not stable and may
 * increase while we hold the lock, the TRUE indication only means
 * that we transitioned 1->0, not necessarily that we stayed at 0.
 *
 * Another thread bumping refs while we hold the lock will set CHECK,
 * causing one of the competing hammer_ref_interlock() calls to
 * return TRUE after we release our lock.
 *
 * MPSAFE
 */
int
hammer_rel_interlock(struct hammer_lock *lock, int locked)
{
	u_int lv;
	u_int nlv;

	/*
	 * In locked mode (failure/unload path) we release the
	 * ref-count but leave it locked.
	 */
	if (locked) {
		hammer_rel(lock);
		return(1);
	}

	/*
	 * Integrated reference count drop with LOCKED, plus the hot-path
	 * returns.
	 */
	for (;;) {
		lv = lock->refs;

		if (lv == 1) {
			nlv = 0 | HAMMER_REFS_LOCKED;
			if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
				lock->rowner = curthread;
				return(1);
			}
		} else if ((lv & ~HAMMER_REFS_FLAGS) == 1) {
			if ((lv & HAMMER_REFS_LOCKED) == 0) {
				nlv = (lv - 1) | HAMMER_REFS_LOCKED;
				if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
					lock->rowner = curthread;
					return(1);
				}
			} else {
				nlv = lv | HAMMER_REFS_WANTED;
				tsleep_interlock(&lock->refs, 0);
				if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
					tsleep(&lock->refs, PINTERLOCKED,
					       "h0lk", 0);
				}
			}
		} else {
			nlv = (lv - 1);
			KKASSERT((int)nlv >= 0);
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				return(0);
		}
	}
	/* not reached */
}

/*
 * Unlock the interlock acquired by hammer_rel_interlock().
 *
 * If orig_locked is non-zero the interlock was originally held prior to
 * the hammer_rel_interlock() call and passed through to us.  In this
 * case we want to retain the CHECK error state if not transitioning
 * to 0.
 *
 * The code is the same either way so we do not have to conditionalize
 * on orig_locked.
 */
void
hammer_rel_interlock_done(struct hammer_lock *lock, int orig_locked __unused)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		nlv = lv & ~(HAMMER_REFS_LOCKED | HAMMER_REFS_WANTED);
		if ((lv & ~HAMMER_REFS_FLAGS) == 0)
			nlv &= ~HAMMER_REFS_CHECK;
		if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
			if (lv & HAMMER_REFS_WANTED)
				wakeup(&lock->refs);
			break;
		}
	}
}

/*
 * Acquire the interlock on lock->refs.
 *
 * Return TRUE if CHECK is currently set.  Note that CHECK will not
 * be set if the reference count is 0, but can get set if this function
 * is preceeded by, say, hammer_ref(), or through races with other
 * threads.  The return value allows the caller to use the same logic
 * as hammer_ref_interlock().
 *
 * MPSAFE
 */
int
hammer_get_interlock(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		if (lv & HAMMER_REFS_LOCKED) {
			nlv = lv | HAMMER_REFS_WANTED;
			tsleep_interlock(&lock->refs, 0);
			if (atomic_cmpset_int(&lock->refs, lv, nlv))
				tsleep(&lock->refs, PINTERLOCKED, "hilk", 0);
		} else {
			nlv = (lv | HAMMER_REFS_LOCKED);
			if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
				lock->rowner = curthread;
				return((lv & HAMMER_REFS_CHECK) ? 1 : 0);
			}
		}
	}
}

/*
 * Attempt to acquire the interlock and expect 0 refs.  Used by the buffer
 * cache callback code to disassociate or lock the bufs related to HAMMER
 * structures.
 *
 * During teardown the related bp will be acquired by hammer_io_release()
 * which interocks our test.
 *
 * Returns non-zero on success, zero on failure.
 */
int
hammer_try_interlock_norefs(struct hammer_lock *lock)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		if (lv == 0) {
			nlv = lv | HAMMER_REFS_LOCKED;
			if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
				lock->rowner = curthread;
				return(1);
			}
		} else {
			return(0);
		}
	}
	/* not reached */
}

/*
 * Release the interlock on lock->refs.  This function will set
 * CHECK if the refs is non-zero and error is non-zero, and clear
 * CHECK otherwise.
 *
 * MPSAFE
 */
void
hammer_put_interlock(struct hammer_lock *lock, int error)
{
	u_int lv;
	u_int nlv;

	for (;;) {
		lv = lock->refs;
		KKASSERT(lv & HAMMER_REFS_LOCKED);
		nlv = lv & ~(HAMMER_REFS_LOCKED | HAMMER_REFS_WANTED);

		if ((nlv & ~HAMMER_REFS_FLAGS) == 0 || error == 0)
			nlv &= ~HAMMER_REFS_CHECK;
		else
			nlv |= HAMMER_REFS_CHECK;

		if (atomic_cmpset_int(&lock->refs, lv, nlv)) {
			if (lv & HAMMER_REFS_WANTED)
				wakeup(&lock->refs);
			return;
		}
	}
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
 * ALGORITHM VERSION 0:
 *	Return a namekey hash.   The 64 bit namekey hash consists of a 32 bit
 *	crc in the MSB and 0 in the LSB.  The caller will use the low 32 bits
 *	to generate a unique key and will scan all entries with the same upper
 *	32 bits when issuing a lookup.
 *
 *	0hhhhhhhhhhhhhhh hhhhhhhhhhhhhhhh 0000000000000000 0000000000000000
 *
 * ALGORITHM VERSION 1:
 *
 *	This algorithm breaks the filename down into a separate 32-bit crcs
 *	for each filename segment separated by a special character (dot,
 *	underscore, underline, or tilde).  The CRCs are then added together.
 *	This allows temporary names.  A full-filename 16 bit crc is also
 *	generated to deal with degenerate conditions.
 *
 *	The algorithm is designed to handle create/rename situations such
 *	that a create with an extention to a rename without an extention
 *	only shifts the key space rather than randomizes it.
 *
 *	NOTE: The inode allocator cache can only match 10 bits so we do
 *	      not really have any room for a partial sorted name, and
 *	      numbers don't sort well in that situation anyway.
 *
 *	0mmmmmmmmmmmmmmm mmmmmmmmmmmmmmmm llllllllllllllll 0000000000000000
 *
 *
 * We strip bit 63 in order to provide a positive key, this way a seek
 * offset of 0 will represent the base of the directory.
 *
 * We usually strip bit 0 (set it to 0) in order to provide a consistent
 * iteration space for collisions.
 *
 * This function can never return 0.  We use the MSB-0 space to synthesize
 * artificial directory entries such as "." and "..".
 */
int64_t
hammer_directory_namekey(hammer_inode_t dip, const void *name, int len,
			 u_int32_t *max_iterationsp)
{
	const char *aname = name;
	int32_t crcx;
	int64_t key;
	int i;
	int j;

	switch (dip->ino_data.cap_flags & HAMMER_INODE_CAP_DIRHASH_MASK) {
	case HAMMER_INODE_CAP_DIRHASH_ALG0:
		/*
		 * Original algorithm
		 */
		key = (int64_t)(crc32(aname, len) & 0x7FFFFFFF) << 32;
		if (key == 0)
			key |= 0x100000000LL;
		*max_iterationsp = 0xFFFFFFFFU;
		break;
	case HAMMER_INODE_CAP_DIRHASH_ALG1:
		/*
		 * Filesystem version 6 or better will create directories
		 * using the ALG1 dirhash.  This hash breaks the filename
		 * up into domains separated by special characters and
		 * hashes each domain independently.
		 *
		 * We also do a simple sub-sort using the first character
		 * of the filename in the top 5-bits.
		 */
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
					crcx += crc32(aname + j, i - j);
				j = i + 1;
			}
		}
		if (i != j)
			crcx += crc32(aname + j, i - j);

#if 0
		/*
		 * xor top 5 bits 0mmmm into low bits and steal the top 5
		 * bits as a semi sub sort using the first character of
		 * the filename.  bit 63 is always left as 0 so directory
		 * keys are positive numbers.
		 */
		crcx ^= (uint32_t)crcx >> (32 - 5);
		crcx = (crcx & 0x07FFFFFF) | ((aname[0] & 0x0F) << (32 - 5));
#endif
		crcx &= 0x7FFFFFFFU;

		key |= (uint64_t)crcx << 32;

		/*
		 * l16 - crc of entire filename
		 *
		 * This crc reduces degenerate hash collision conditions
		 */
		crcx = crc32(aname, len);
		crcx = crcx ^ (crcx << 16);
		key |= crcx & 0xFFFF0000U;

		/*
		 * Cleanup
		 */
		if ((key & 0xFFFFFFFF00000000LL) == 0)
			key |= 0x100000000LL;
		if (hammer_debug_general & 0x0400) {
			kprintf("namekey2: 0x%016llx %*.*s\n",
				(long long)key, len, len, aname);
		}
		*max_iterationsp = 0x00FFFFFF;
		break;
	case HAMMER_INODE_CAP_DIRHASH_ALG2:
	case HAMMER_INODE_CAP_DIRHASH_ALG3:
	default:
		key = 0;			/* compiler warning */
		*max_iterationsp = 1;		/* sanity */
		panic("hammer_directory_namekey: bad algorithm %p", dip);
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

int
hammer_blockoff(int64_t file_offset)
{
	if (file_offset < HAMMER_XDEMARC)
		return((int)file_offset & HAMMER_BUFMASK);
	else
		return((int)file_offset & HAMMER_XBUFMASK);
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
	panic("hammer_blockdemarc: illegal range %lld %lld",
	      (long long)file_offset1, (long long)file_offset2);
}

udev_t
hammer_fsid_to_udev(uuid_t *uuid)
{
	u_int32_t crc;

	crc = crc32(uuid, sizeof(*uuid));
	return((udev_t)crc);
}
