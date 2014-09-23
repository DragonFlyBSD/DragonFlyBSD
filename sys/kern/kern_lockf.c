/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 * Copyright (c) 2006 Matthew Dillon <dillon@backplane.com>.  All rights reserved.
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Scooter Morris at Genentech Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_lockf.c	8.3 (Berkeley) 1/6/94
 * $FreeBSD: src/sys/kern/kern_lockf.c,v 1.25 1999/11/16 16:28:56 phk Exp $
 */

#include "opt_debug_lockf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/resourcevar.h>

#include <sys/lockf.h>
#include <machine/limits.h>	/* for LLONG_MAX */
#include <machine/stdarg.h>

#include <sys/spinlock2.h>

#ifdef INVARIANTS
int lf_global_counter = 0;
#endif

#ifdef LOCKF_DEBUG
int lf_print_ranges = 0;

static void _lf_print_lock(const struct lockf *);
static void _lf_printf(const char *, ...);

#define lf_print_lock(lock) if (lf_print_ranges) _lf_print_lock(lock)
#define lf_printf(ctl, args...)	if (lf_print_ranges) _lf_printf(ctl, args)
#else
#define lf_print_lock(lock)
#define lf_printf(ctl, args...)
#endif

static MALLOC_DEFINE(M_LOCKF, "lockf", "Byte-range locking structures");

static void	lf_wakeup(struct lockf *, off_t, off_t);
static struct lockf_range *lf_alloc_range(void);
static void	lf_create_range(struct lockf_range *, struct proc *, int, int,
				off_t, off_t);
static void	lf_insert(struct lockf_range_list *list,
				struct lockf_range *elm,
				struct lockf_range *insert_point);
static void	lf_destroy_range(struct lockf_range *);

static int	lf_setlock(struct lockf *, struct proc *, int, int,
			   off_t, off_t);
static int	lf_getlock(struct flock *, struct lockf *, struct proc *,
			   int, int, off_t, off_t);

static int	lf_count_change(struct proc *, int);

/*
 * Return TRUE (non-zero) if the type and posix flags match.
 */
static __inline
int
lf_match(struct lockf_range *range, int type, int flags)
{
	if (range->lf_type != type)
		return(0);
	if ((range->lf_flags ^ flags) & F_POSIX)
		return(0);
	return(1);
}

/*
 * Check whether range and [start, end] overlap.
 */
static __inline
int
lf_overlap(const struct lockf_range *range, off_t start, off_t end)
{
	if (range->lf_start >= start && range->lf_start <= end)
		return(1);
	else if (start >= range->lf_start && start <= range->lf_end)
		return(1);
	else
		return(0);
}


/*
 * Change the POSIX lock accounting for the given process.
 */
void
lf_count_adjust(struct proc *p, int increase)
{
	struct uidinfo *uip;

	KKASSERT(p != NULL);

	uip = p->p_ucred->cr_uidinfo;
	spin_lock(&uip->ui_lock);

	if (increase)
		uip->ui_posixlocks += p->p_numposixlocks;
	else
		uip->ui_posixlocks -= p->p_numposixlocks;

	KASSERT(uip->ui_posixlocks >= 0,
		("Negative number of POSIX locks held by %s user: %d.",
		 increase ? "new" : "old", uip->ui_posixlocks));
	spin_unlock(&uip->ui_lock);
}

static int
lf_count_change(struct proc *owner, int diff)
{
	struct uidinfo *uip;
	int max, ret;

	/* we might actually not have a process context */
	if (owner == NULL)
		return(0);

	uip = owner->p_ucred->cr_uidinfo;

	max = MIN(owner->p_rlimit[RLIMIT_POSIXLOCKS].rlim_cur,
		  maxposixlocksperuid);

	spin_lock(&uip->ui_lock);
	if (diff > 0 && owner->p_ucred->cr_uid != 0 && max != -1 &&
	    uip->ui_posixlocks >= max ) {
		ret = 1;
	} else {
		uip->ui_posixlocks += diff;
		owner->p_numposixlocks += diff;
		KASSERT(uip->ui_posixlocks >= 0,
			("Negative number of POSIX locks held by user: %d.",
			 uip->ui_posixlocks));
		KASSERT(owner->p_numposixlocks >= 0,
			("Negative number of POSIX locks held by proc: %d.",
			 uip->ui_posixlocks));
		ret = 0;
	}
	spin_unlock(&uip->ui_lock);
	return ret;
}

/*
 * Advisory record locking support
 */
int
lf_advlock(struct vop_advlock_args *ap, struct lockf *lock, u_quad_t size)
{
	struct flock *fl = ap->a_fl;
	struct proc *owner;
	off_t start, end;
	int type, flags, error;
	lwkt_token_t token;

	/*
	 * Convert the flock structure into a start and end.
	 */
	switch (fl->l_whence) {
	case SEEK_SET:
	case SEEK_CUR:
		/*
		 * Caller is responsible for adding any necessary offset
		 * when SEEK_CUR is used.
		 */
		start = fl->l_start;
		break;

	case SEEK_END:
		start = size + fl->l_start;
		break;

	default:
		return(EINVAL);
	}

	flags = ap->a_flags;
	if (start < 0)
		return(EINVAL);
	if (fl->l_len == 0) {
		flags |= F_NOEND;
		end = LLONG_MAX;
	} else if (fl->l_len < 0) {
		return(EINVAL);
	} else {
		end = start + fl->l_len - 1;
		if (end < start)
			return(EINVAL);
	}
	
	type = fl->l_type;
	/*
	 * This isn't really correct for flock-style locks,
	 * but the current handling is somewhat broken anyway.
	 */
	owner = (struct proc *)ap->a_id;

	/*
	 * Do the requested operation.
	 */
	token = lwkt_getpooltoken(lock);

	if (lock->init_done == 0) {
		TAILQ_INIT(&lock->lf_range);
		TAILQ_INIT(&lock->lf_blocked);
		lock->init_done = 1;
	}

	switch(ap->a_op) {
	case F_SETLK:
		/*
		 * NOTE: It is possible for both lf_range and lf_blocked to
		 * be empty if we block and get woken up, but another process
		 * then gets in and issues an unlock.  So VMAYHAVELOCKS must
		 * be set after the lf_setlock() operation completes rather
		 * then before.
		 */
		error = lf_setlock(lock, owner, type, flags, start, end);
		vsetflags(ap->a_vp, VMAYHAVELOCKS);
		break;

	case F_UNLCK:
		error = lf_setlock(lock, owner, type, flags, start, end);
		if (TAILQ_EMPTY(&lock->lf_range) &&
		    TAILQ_EMPTY(&lock->lf_blocked)) {
			vclrflags(ap->a_vp, VMAYHAVELOCKS);
		}
		break;

	case F_GETLK:
		error = lf_getlock(fl, lock, owner, type, flags, start, end);
		break;

	default:
		error = EINVAL;
		break;
	}
	lwkt_reltoken(token);
	return(error);
}

static int
lf_setlock(struct lockf *lock, struct proc *owner, int type, int flags,
	   off_t start, off_t end)
{
	struct lockf_range *range;
	struct lockf_range *brange;
	struct lockf_range *next;
	struct lockf_range *first_match;
	struct lockf_range *last_match;
	struct lockf_range *insert_point;
	struct lockf_range *new_range1;
	struct lockf_range *new_range2;
	int wakeup_needed;
	int double_clip;
	int unlock_override;
	int error = 0;
	int count;
	struct lockf_range_list deadlist;

	new_range1 = NULL;
	new_range2 = NULL;
	count = 0;

restart:
	/*
	 * Preallocate two ranges so we don't have to worry about blocking
	 * in the middle of the lock code.
	 */
	if (new_range1 == NULL)
		new_range1 = lf_alloc_range();
	if (new_range2 == NULL)
		new_range2 = lf_alloc_range();
	first_match = NULL;
	last_match = NULL;
	insert_point = NULL;
	wakeup_needed = 0;

	lf_print_lock(lock);

	/*
	 * Locate the insertion point for the new lock (the first range
	 * with an lf_start >= start).
	 *
	 * Locate the first and latch ranges owned by us that overlap
	 * the requested range.
	 */
	TAILQ_FOREACH(range, &lock->lf_range, lf_link) {
		if (insert_point == NULL && range->lf_start >= start)
			insert_point = range;

		/*
		 * Skip non-overlapping locks.  Locks are sorted by lf_start
		 * So we can terminate the search when lf_start exceeds the
		 * requested range (insert_point is still guarenteed to be
		 * set properly).
		 */
		if (range->lf_end < start)
			continue;
		if (range->lf_start > end) {
			range = NULL;
			break;
		}

		/*
		 * Overlapping lock.  Set first_match and last_match if we
		 * are the owner.
		 */
		if (range->lf_owner == owner) {
			if (first_match == NULL)
				first_match = range;
			last_match = range;
			continue;
		}

		/*
		 * If we aren't the owner check for a conflicting lock.  Only
		 * if not unlocking.
		 */
		if (type != F_UNLCK) {
			if (type == F_WRLCK || range->lf_type == F_WRLCK)
				break;
		}
	}

	/*
	 * If a conflicting lock was observed, block or fail as appropriate.
	 * (this code is skipped when unlocking)
	 */
	if (range != NULL) {
		if ((flags & F_WAIT) == 0) {
			error = EAGAIN;
			goto do_cleanup;
		}

		/*
		 * We are blocked. For POSIX locks we have to check
		 * for deadlocks and return with EDEADLK. This is done
		 * by checking whether range->lf_owner is already
		 * blocked.
		 *
		 * Since flock-style locks cover the whole file, a
		 * deadlock between those is nearly impossible.
		 * This can only occur if a process tries to lock the
		 * same inode exclusively while holding a shared lock
		 * with another descriptor.
		 * XXX How can we cleanly detect this?
		 * XXX The current mixing of flock & fcntl/lockf is evil.
		 *
		 * Handle existing locks of flock-style like POSIX locks.
		 */
		if (flags & F_POSIX) {
			TAILQ_FOREACH(brange, &lock->lf_blocked, lf_link) {
				if (brange->lf_owner == range->lf_owner) {
					error = EDEADLK;
					goto do_cleanup;
				}
			}
		}
		
		/*
		 * For flock-style locks, we must first remove
		 * any shared locks that we hold before we sleep
		 * waiting for an exclusive lock.
		 */
		if ((flags & F_POSIX) == 0 && type == F_WRLCK)
			lf_setlock(lock, owner, F_UNLCK, 0, start, end);

		brange = new_range1;
		new_range1 = NULL;
		lf_create_range(brange, owner, type, 0, start, end);
		TAILQ_INSERT_TAIL(&lock->lf_blocked, brange, lf_link);
		error = tsleep(brange, PCATCH, "lockf", 0);

		/*
		 * We may have been awaked by a signal and/or by a
		 * debugger continuing us (in which case we must remove
		 * ourselves from the blocked list) and/or by another
		 * process releasing/downgrading a lock (in which case
		 * we have already been removed from the blocked list
		 * and our lf_flags field is 1).
		 *
		 * Sleep if it looks like we might be livelocking.
		 */
		if (brange->lf_flags == 0)
			TAILQ_REMOVE(&lock->lf_blocked, brange, lf_link);
		if (count == 2)
			tsleep(brange, 0, "lockfz", 2);
		else
			++count;
		lf_destroy_range(brange);

		if (error)
			goto do_cleanup;
		goto restart;
	}

	/*
	 * If there are no overlapping locks owned by us then creating
	 * the new lock is easy.  This is the most common case.
	 */
	if (first_match == NULL) {
		if (type == F_UNLCK)
			goto do_wakeup;
		if (flags & F_POSIX) {
			if (lf_count_change(owner, 1)) {
				error = ENOLCK;
				goto do_cleanup;
			}
		}
		range = new_range1;
		new_range1 = NULL;
		lf_create_range(range, owner, type, flags, start, end);
		lf_insert(&lock->lf_range, range, insert_point);
		goto do_wakeup;
	}

	/*
	 * double_clip - Calculate a special case where TWO locks may have
	 *		 to be added due to the new lock breaking up an
	 *		 existing incompatible lock in the middle.
	 *
	 * unlock_override - Calculate a special case where NO locks
	 *		 need to be created.  This occurs when an unlock
	 *		 does not clip any locks at the front and rear.
	 *
	 * WARNING!  closef() and fdrop() assume that an F_UNLCK of the
	 *	     entire range will always succeed so the unlock_override
	 *	     case is mandatory.
	 */
	double_clip = 0;
	unlock_override = 0;
	if (first_match->lf_start < start) {
		if (first_match == last_match && last_match->lf_end > end)
			double_clip = 1;
	} else if (type == F_UNLCK && last_match->lf_end <= end) {
		unlock_override = 1;
	}

	/*
	 * Figure out the worst case net increase in POSIX locks and account
	 * for it now before we start modifying things.  If neither the
	 * first or last locks match we have an issue.  If there is only
	 * one overlapping range which needs to be clipped on both ends
	 * we wind up having to create up to two new locks, else only one.
	 *
	 * When unlocking the worst case is always 1 new lock if our
	 * unlock request cuts the middle out of an existing lock range.
	 *
	 * count represents the 'cleanup' adjustment needed.  It starts
	 * negative, is incremented whenever we create a new POSIX lock,
	 * and decremented whenever we delete an existing one.  At the
	 * end of the day it had better be <= 0 or we didn't calculate the
	 * worse case properly here.
	 */
	count = 0;
	if ((flags & F_POSIX) && !unlock_override) {
		if (!lf_match(first_match, type, flags) &&
		    !lf_match(last_match, type, flags)
		) {
			if (double_clip && type != F_UNLCK)
				count = -2;
			else
				count = -1;
		}
		if (count && lf_count_change(owner, -count)) {
			error = ENOLCK;
			goto do_cleanup;
		}
	}
	/* else flock style lock which encompasses entire range */

	/*
	 * Create and insert the lock represented the requested range.
	 * Adjust the net POSIX lock count.  We have to move our insertion
	 * point since brange now represents the first record >= start.
	 *
	 * When unlocking, no new lock is inserted but we still clip.
	 */
	if (type != F_UNLCK) {
		brange = new_range1;
		new_range1 = NULL;
		lf_create_range(brange, owner, type, flags, start, end);
		lf_insert(&lock->lf_range, brange, insert_point);
		insert_point = brange;
		if (flags & F_POSIX)
			++count;
	} else {
		brange = NULL;
	}

	/*
	 * Handle the double_clip case.  This is the only case where
	 * we wind up having to add TWO locks.
	 */
	if (double_clip) {
		KKASSERT(first_match == last_match);
		last_match = new_range2;
		new_range2 = NULL;
		lf_create_range(last_match, first_match->lf_owner,
				first_match->lf_type, first_match->lf_flags,
				end + 1, first_match->lf_end);
		first_match->lf_end = start - 1;
		first_match->lf_flags &= ~F_NOEND;

		/*
		 * Figure out where to insert the right side clip.
		 */
		lf_insert(&lock->lf_range, last_match, first_match);
		if (last_match->lf_flags & F_POSIX)
			++count;
	}

	/*
	 * Clip or destroy the locks between first_match and last_match,
	 * inclusive.  Ignore the primary lock we created (brange).  Note
	 * that if double-clipped, first_match and last_match will be
	 * outside our clipping range.  Otherwise first_match and last_match
	 * will be deleted.
	 *
	 * We have already taken care of any double clipping.
	 *
	 * The insert_point may become invalid as we delete records, do not
	 * use that pointer any more.  Also, when removing something other
	 * then 'range' we have to check to see if the item we are removing
	 * is 'next' and adjust 'next' properly.
	 *
	 * NOTE: brange will be NULL if F_UNLCKing.
	 */
	TAILQ_INIT(&deadlist);
	next = first_match;

	while ((range = next) != NULL) {
		next = TAILQ_NEXT(range, lf_link);

		/*
		 * Ignore elements that we do not own and ignore the
		 * primary request range which we just created.
		 */
		if (range->lf_owner != owner || range == brange)
			continue;

		/*
		 * We may have to wakeup a waiter when downgrading a lock.
		 */
		if (type == F_UNLCK)
			wakeup_needed = 1;
		if (type == F_RDLCK && range->lf_type == F_WRLCK)
			wakeup_needed = 1;

		/*
		 * Clip left.  This can only occur on first_match. 
		 *
		 * Merge the left clip with brange if possible.  This must
		 * be done specifically, not in the optimized merge heuristic
		 * below, since we may have counted on it in our 'count'
		 * calculation above.
		 */
		if (range->lf_start < start) {
			KKASSERT(range == first_match);
			if (brange &&
			    range->lf_end >= start - 1 &&
			    lf_match(range, type, flags)) {
				range->lf_end = brange->lf_end;
				range->lf_flags |= brange->lf_flags & F_NOEND;
				/*
				 * Removing something other then 'range',
				 * adjust 'next' if necessary.
				 */
				if (next == brange)
					next = TAILQ_NEXT(next, lf_link);
				TAILQ_REMOVE(&lock->lf_range, brange, lf_link);
				if (brange->lf_flags & F_POSIX)
					--count;
				TAILQ_INSERT_TAIL(&deadlist, brange, lf_link);
				brange = range;
			} else if (range->lf_end >= start) {
				range->lf_end = start - 1;
				if (type != F_UNLCK)
					range->lf_flags &= ~F_NOEND;
			}
			if (range == last_match)
				break;
			continue;
		}

		/*
		 * Clip right.  This can only occur on last_match. 
		 *
		 * Merge the right clip if possible.  This must be done
		 * specifically, not in the optimized merge heuristic
		 * below, since we may have counted on it in our 'count'
		 * calculation.
		 *
		 * Since we are adjusting lf_start, we have to move the
		 * record to maintain the sorted list.  Since lf_start is
		 * only getting larger we can use the next element as the
		 * insert point (we don't have to backtrack).
		 */
		if (range->lf_end > end) {
			KKASSERT(range == last_match);
			if (brange &&
			    range->lf_start <= end + 1 && 
			    lf_match(range, type, flags)) {
				brange->lf_end = range->lf_end;
				brange->lf_flags |= range->lf_flags & F_NOEND;
				TAILQ_REMOVE(&lock->lf_range, range, lf_link);
				if (range->lf_flags & F_POSIX)
					--count;
				TAILQ_INSERT_TAIL(&deadlist, range, lf_link);
			} else if (range->lf_start <= end) {
				range->lf_start = end + 1;
				TAILQ_REMOVE(&lock->lf_range, range, lf_link);
				lf_insert(&lock->lf_range, range, next);
			}
			/* range == last_match, we are done */
			break;
		}

		/*
		 * The record must be entirely enclosed.  Note that the
		 * record could be first_match or last_match, and will be
		 * deleted.
		 */
		KKASSERT(range->lf_start >= start && range->lf_end <= end);
		TAILQ_REMOVE(&lock->lf_range, range, lf_link);
		if (range->lf_flags & F_POSIX)
			--count;
		TAILQ_INSERT_TAIL(&deadlist, range, lf_link);
		if (range == last_match)
			break;
	}

	/*
	 * Attempt to merge locks adjacent to brange.  For example, we may
	 * have had to clip first_match and/or last_match, and they might
	 * be adjacent.  Or there might simply have been an adjacent lock
	 * already there.
	 *
	 * Don't get fancy, just check adjacent elements in the list if they
	 * happen to be owned by us.
	 *
	 * This case only gets hit if we have a situation where a shared
	 * and exclusive lock are adjacent, and the exclusive lock is 
	 * downgraded to shared or the shared lock is upgraded to exclusive.
	 */
	if (brange) {
		range = TAILQ_PREV(brange, lockf_range_list, lf_link);
		if (range &&
		    range->lf_owner == owner && 
		    range->lf_end == brange->lf_start - 1 &&
		    lf_match(range, type, flags)
		) {
			/*
			 * Extend range to cover brange and scrap brange.
			 */
			range->lf_end = brange->lf_end;
			range->lf_flags |= brange->lf_flags & F_NOEND;
			TAILQ_REMOVE(&lock->lf_range, brange, lf_link);
			if (brange->lf_flags & F_POSIX)
				--count;
			TAILQ_INSERT_TAIL(&deadlist, brange, lf_link);
			brange = range;
		}
		range = TAILQ_NEXT(brange, lf_link);
		if (range &&
		    range->lf_owner == owner &&
		    range->lf_start == brange->lf_end + 1 &&
		    lf_match(range, type, flags)
		) {
			/*
			 * Extend brange to cover range and scrap range.
			 */
			brange->lf_end = range->lf_end;
			brange->lf_flags |= range->lf_flags & F_NOEND;
			TAILQ_REMOVE(&lock->lf_range, range, lf_link);
			if (range->lf_flags & F_POSIX)
				--count;
			TAILQ_INSERT_TAIL(&deadlist, range, lf_link);
		}
	}

	/*
	 * Destroy deleted elements.  We didn't want to do it in the loop
	 * because the free() might have blocked.
	 *
	 * Adjust the count for any posix locks we thought we might create
	 * but didn't.
	 */
	while ((range = TAILQ_FIRST(&deadlist)) != NULL) {
		TAILQ_REMOVE(&deadlist, range, lf_link);
		lf_destroy_range(range);
	}

	KKASSERT(count <= 0);
	if (count < 0)
		lf_count_change(owner, count);
do_wakeup:
	lf_print_lock(lock);
	if (wakeup_needed)
		lf_wakeup(lock, start, end);
	error = 0;
do_cleanup:
	if (new_range1 != NULL)
		lf_destroy_range(new_range1);
	if (new_range2 != NULL)
		lf_destroy_range(new_range2);
	return(error);
}

/*
 * Check whether there is a blocking lock,
 * and if so return its process identifier.
 */
static int
lf_getlock(struct flock *fl, struct lockf *lock, struct proc *owner,
	   int type, int flags, off_t start, off_t end)
{
	struct lockf_range *range;

	TAILQ_FOREACH(range, &lock->lf_range, lf_link)
		if (range->lf_owner != owner &&
		    lf_overlap(range, start, end) &&
		    (type == F_WRLCK || range->lf_type == F_WRLCK))
			break;
	if (range == NULL) {
		fl->l_type = F_UNLCK;
		return(0);
	}
	fl->l_type = range->lf_type;
	fl->l_whence = SEEK_SET;
	fl->l_start = range->lf_start;
	if (range->lf_flags & F_NOEND)
		fl->l_len = 0;
	else
		fl->l_len = range->lf_end - range->lf_start + 1;
	if (range->lf_owner != NULL && (range->lf_flags & F_POSIX))
		fl->l_pid = range->lf_owner->p_pid;
	else
		fl->l_pid = -1;
	return(0);
}

/*
 * Wakeup pending lock attempts.  Theoretically we can stop as soon as
 * we encounter an exclusive request that covers the whole range (at least
 * insofar as the sleep code above calls lf_wakeup() if it would otherwise
 * exit instead of loop), but for now just wakeup all overlapping
 * requests.  XXX
 */
static void
lf_wakeup(struct lockf *lock, off_t start, off_t end)
{
	struct lockf_range *range, *nrange;

	TAILQ_FOREACH_MUTABLE(range, &lock->lf_blocked, lf_link, nrange) {
		if (lf_overlap(range, start, end) == 0)
			continue;
		TAILQ_REMOVE(&lock->lf_blocked, range, lf_link);
		range->lf_flags = 1;
		wakeup(range);
	}
}

/*
 * Allocate a range structure and initialize it sufficiently such that
 * lf_destroy_range() does not barf.
 */
static struct lockf_range *
lf_alloc_range(void)
{
	struct lockf_range *range;

#ifdef INVARIANTS
	atomic_add_int(&lf_global_counter, 1);
#endif
	range = kmalloc(sizeof(struct lockf_range), M_LOCKF, M_WAITOK);
	range->lf_owner = NULL;
	return(range);
}

static void
lf_insert(struct lockf_range_list *list, struct lockf_range *elm,
	  struct lockf_range *insert_point)
{
	while (insert_point && insert_point->lf_start < elm->lf_start)
		insert_point = TAILQ_NEXT(insert_point, lf_link);
	if (insert_point != NULL)
		TAILQ_INSERT_BEFORE(insert_point, elm, lf_link);
	else
		TAILQ_INSERT_TAIL(list, elm, lf_link);
}

static void
lf_create_range(struct lockf_range *range, struct proc *owner, int type,
		int flags, off_t start, off_t end)
{
	KKASSERT(start <= end);
	range->lf_type = type;
	range->lf_flags = flags;
	range->lf_start = start;
	range->lf_end = end;
	range->lf_owner = owner;

	lf_printf("lf_create_range: %lld..%lld\n",
			range->lf_start, range->lf_end);
}

static void
lf_destroy_range(struct lockf_range *range)
{
	lf_printf("lf_destroy_range: %lld..%lld\n",
		  range->lf_start, range->lf_end);
	kfree(range, M_LOCKF);
#ifdef INVARIANTS
	atomic_add_int(&lf_global_counter, -1);
	KKASSERT(lf_global_counter >= 0);
#endif
}

#ifdef LOCKF_DEBUG

static void
_lf_printf(const char *ctl, ...)
{
	struct proc *p;
	__va_list va;

	if (lf_print_ranges) {
	    if ((p = curproc) != NULL)
		kprintf("pid %d (%s): ", p->p_pid, p->p_comm);
	}
	__va_start(va, ctl);
	kvprintf(ctl, va);
	__va_end(va);
}

static void
_lf_print_lock(const struct lockf *lock)
{
	struct lockf_range *range;

	if (lf_print_ranges == 0)
		return;

	if (TAILQ_EMPTY(&lock->lf_range)) {
		lf_printf("lockf %p: no ranges locked\n", lock);
	} else {
		lf_printf("lockf %p:\n", lock);
	}
	TAILQ_FOREACH(range, &lock->lf_range, lf_link)
		kprintf("\t%jd..%jd type %s owned by %d\n",
		       (uintmax_t)range->lf_start, (uintmax_t)range->lf_end,
		       range->lf_type == F_RDLCK ? "shared" : "exclusive",
		       range->lf_flags & F_POSIX ? range->lf_owner->p_pid : -1);
	if (TAILQ_EMPTY(&lock->lf_blocked))
		kprintf("no process waiting for range\n");
	else
		kprintf("blocked locks:");
	TAILQ_FOREACH(range, &lock->lf_blocked, lf_link)
		kprintf("\t%jd..%jd type %s waiting on %p\n",
		       (uintmax_t)range->lf_start, (uintmax_t)range->lf_end,
		       range->lf_type == F_RDLCK ? "shared" : "exclusive",
		       range);
}
#endif /* LOCKF_DEBUG */
