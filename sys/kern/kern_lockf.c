/*
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/sys/kern/kern_lockf.c,v 1.16 2004/06/25 15:32:18 joerg Exp $
 */

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

#ifdef INVARIANTS
int lf_global_counter = 0;
#endif
#ifdef LOCKF_DEBUG
int lf_print_ranges = 0;

static void	lf_print_lock(const struct lockf *);
#endif

static MALLOC_DEFINE(M_LOCKF, "lockf", "Byte-range locking structures");

static void	lf_wakeup(struct lockf *, off_t, off_t);
static int	lf_overlap(const struct lockf_range *, off_t, off_t);
static int	lf_overlap_left(const struct lockf_range *, off_t, off_t);
static int	lf_overlap_right(const struct lockf_range *, off_t, off_t);
static int	lf_overlap_left2(const struct lockf_range *, off_t, off_t);
static int	lf_overlap_right2(const struct lockf_range *, off_t, off_t);
static int	lf_overlap_embedded(const struct lockf_range *, off_t, off_t);
static struct lockf_range *lf_alloc_range(void);
static void	lf_create_range(struct lockf_range *, struct proc *, int, int,
				off_t, off_t, int);
static void	lf_destroy_range(struct lockf_range *, int);

static int	lf_setlock(struct lockf *, struct proc *, int, int,
			   off_t, off_t);
static int	lf_clearlock(struct lockf *, struct proc *, int, int,
			     off_t, off_t);
static int	lf_getlock(struct flock *, struct lockf *, struct proc *,
			   int, int, off_t, off_t);

static int	lf_count_change(struct proc *, int);

/*
 * Change the POSIX lock accounting for the given process.
 */
void
lf_count_adjust(struct proc *p, int increase)
{
	struct uidinfo *uip;

	KKASSERT(p != NULL);

	uip = p->p_ucred->cr_uidinfo;

	if (increase)
		uip->ui_posixlocks += p->p_numposixlocks;
	else
		uip->ui_posixlocks -= p->p_numposixlocks;

	KASSERT(uip->ui_posixlocks >= 0,
		("Negative number of POSIX locks held by %s user: %d.",
		 increase ? "new" : "old", uip->ui_posixlocks));
}

static int
lf_count_change(struct proc *owner, int diff)
{
	struct uidinfo *uip;
	int max;

	/* we might actually not have a process context */
	if (owner == NULL)
		return(0);

	uip = owner->p_ucred->cr_uidinfo;

	max = MIN(owner->p_rlimit[RLIMIT_POSIXLOCKS].rlim_cur,
		  maxposixlocksperuid);
	if (diff > 0 && owner->p_ucred->cr_uid != 0 && max != -1 &&
	    uip->ui_posixlocks >= max )
		return(1);

	uip->ui_posixlocks += diff;

	KASSERT(uip->ui_posixlocks >= 0,
		("Negative number of POSIX locks held by user: %d.",
		 uip->ui_posixlocks));

	return(0);
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
	lwkt_tokref ilock;

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
	if (start < 0)
		return(EINVAL);
	if (fl->l_len == 0) {
		flags |= F_NOEND;
		end = LLONG_MAX;
	} else {
		end = start + fl->l_len - 1;
		if (end < start)
			return(EINVAL);
	}
	
	flags = ap->a_flags;
	type = fl->l_type;
	/*
	 * This isn't really correct for flock-style locks,
	 * but the current handling is somewhat broken anyway.
	 */
	owner = (struct proc *)ap->a_id;

	/*
	 * Do the requested operation.
	 */
	lwkt_gettoken(&ilock, lwkt_token_pool_get(lock));

	if (lock->init_done == 0) {
		TAILQ_INIT(&lock->lf_range);
		TAILQ_INIT(&lock->lf_blocked);
		lock->init_done = 1;
	}

	switch(ap->a_op) {
	case F_SETLK:
		error = lf_setlock(lock, owner, type, flags, start, end);
		break;

	case F_UNLCK:
		error = lf_clearlock(lock, owner, type, flags, start, end);
		break;

	case F_GETLK:
		error = lf_getlock(fl, lock, owner, type, flags, start, end);
		break;

	default:
		error = EINVAL;
		break;
	}
	lwkt_reltoken(&ilock);
	return(error);
}

static int
lf_setlock(struct lockf *lock, struct proc *owner, int type, int flags,
	   off_t start, off_t end)
{
	struct lockf_range *range, *first_match, *insert_point;
	int wakeup_needed, lock_needed;
	/* pre-allocation to avoid blocking in the middle of the algorithm */
	struct lockf_range *new_range1 = NULL, *new_range2 = NULL;
	int error = 0;
	
	/* for restauration in case of hitting the POSIX lock limit below */
	struct lockf_range *orig_first_match = NULL;
	off_t orig_end = -1;
	int orig_flags = 0;

restart:
	if (new_range1 == NULL)
		new_range1 = lf_alloc_range();
	if (new_range2 == NULL)
		new_range2 = lf_alloc_range();
	first_match = NULL;
	insert_point = NULL;
	wakeup_needed = 0;

#ifdef LOCKF_DEBUG
	if (lf_print_ranges)
		lf_print_lock(lock);
#endif

	TAILQ_FOREACH(range, &lock->lf_range, lf_link) {
		if (insert_point == NULL && range->lf_start >= start)
			insert_point = range;
		if (lf_overlap(range, start, end) == 0)
			continue;
		if (range->lf_owner == owner) {
			if (first_match == NULL)
				first_match = range;
			continue;
		}
		if (type == F_WRLCK || range->lf_type == F_WRLCK)
			break;
	}

	if (range != NULL) {
		struct lockf_range *brange;

		if ((flags & F_WAIT) == 0) {
			error = EAGAIN;
			goto do_cleanup;
		}

		/*
		 * We are blocked. For POSIX locks we have to check
		 * for deadlocks and return with EDEADLK. This is done
		 * by checking wether range->lf_owner is already
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
			TAILQ_FOREACH(brange, &lock->lf_blocked, lf_link)
				if (brange->lf_owner == range->lf_owner) {
					error = EDEADLK;
					goto do_cleanup;
				}
		}
		
		/*
		 * For flock-style locks, we must first remove
		 * any shared locks that we hold before we sleep
		 * waiting for an exclusive lock.
		 */
		if ((flags & F_FLOCK) && type == F_WRLCK)
			lf_clearlock(lock, owner, type, flags, start, end);

		brange = new_range1;
		new_range1 = NULL;
		lf_create_range(brange, owner, type, 0, start, end, 0);
		TAILQ_INSERT_TAIL(&lock->lf_blocked, brange, lf_link);
		error = tsleep(brange, PCATCH, "lockf", 0);

		/*
		 * We may have been awaked by a signal and/or by a
		 * debugger continuing us (in which case we must remove
		 * ourselves from the blocked list) and/or by another
		 * process releasing/downgrading a lock (in which case
		 * we have already been removed from the blocked list
		 * and our lf_flags field is 1).
		 */
		if (brange->lf_flags == 0)
			TAILQ_REMOVE(&lock->lf_blocked, brange, lf_link);
		lf_destroy_range(brange, 0);

		if (error)
			goto do_cleanup;
		goto restart;
	}

	if (first_match == NULL) {
		if (flags & F_POSIX) {
			if (lf_count_change(owner, 1)) {
				error = ENOLCK;
				goto do_cleanup;
			}
		}
		range = new_range1;
		new_range1 = NULL;
		lf_create_range(range, owner, type, flags, start, end, 1);
		if (insert_point != NULL)
			TAILQ_INSERT_BEFORE(insert_point, range, lf_link);
		else
			TAILQ_INSERT_TAIL(&lock->lf_range, range, lf_link);
		goto do_wakeup;
	}

	lock_needed = 1;

	if (lf_overlap_left(first_match, start, end)) {
		KKASSERT((flags & F_POSIX) != 0);
		if (first_match->lf_end > end) {
			if (first_match->lf_type == type)
				goto do_wakeup;
			if (lf_count_change(owner, 2)) {
				error = ENOLCK;
				goto do_cleanup;
			}
			range = new_range1;
			new_range1 = NULL;
			lf_create_range(range, owner, type, flags,
					start, end, 1);
			if (insert_point != NULL)
				TAILQ_INSERT_BEFORE(insert_point, range,
						    lf_link);
			else
				TAILQ_INSERT_TAIL(&lock->lf_range, range,
						  lf_link);
			insert_point = range;
			range = new_range2;
			new_range2 = NULL;
			lf_create_range(range, owner, first_match->lf_type,
					first_match->lf_flags, end + 1,
					first_match->lf_end, 1);
			TAILQ_INSERT_AFTER(&lock->lf_range, insert_point,
					   range, lf_link);
			first_match->lf_flags &= ~F_NOEND;
			first_match->lf_end = start - 1;
			if (type == F_RDLCK)
				wakeup_needed = 1;
			goto do_wakeup;
		}
		/*
		 * left match, but not right match
		 *
		 * handle the lf_type != type case directly,
		 * merge the other case with the !lock_needed path.
		 */
		if (first_match->lf_type != type) {
			/*
			 * This is needed if the lockf acquisition below fails.
			 */
			orig_first_match = first_match;
			orig_end = first_match->lf_end;
			orig_flags = first_match->lf_flags;
			first_match->lf_end = start - 1;
			first_match->lf_flags &= ~F_NOEND;
			if (type == F_RDLCK)
				wakeup_needed = 1;
			/* Try to find the next matching range */
			range = TAILQ_NEXT(first_match, lf_link);
			while (range != NULL) {
				if (range->lf_owner == owner &&
				    lf_overlap(range, start, end))
					break;
				range = TAILQ_NEXT(range, lf_link);
			}
			if (range == NULL)
				goto do_wakeup;
			first_match = range;
			/* fall through to !left_match behaviour */
		} else {
			first_match->lf_end = end;
			first_match->lf_flags |= flags & F_NOEND;
			lock_needed = 0;
		}
	}

	if (lf_overlap_embedded(first_match, start, end)) {
		if (first_match != insert_point) {
			TAILQ_REMOVE(&lock->lf_range, first_match, lf_link);
			TAILQ_INSERT_BEFORE(insert_point, first_match, lf_link);
		}
		first_match->lf_start = start;
		first_match->lf_end = end;
		first_match->lf_flags |= flags & F_NOEND;
		first_match->lf_type = type;
		lock_needed = 0;		
	}

	if (lock_needed == 0) {
		struct lockf_range *nrange;

		range = TAILQ_NEXT(first_match, lf_link);
		while (range != NULL) {
			if (range->lf_owner != owner) {
				range = TAILQ_NEXT(range, lf_link);
				continue;
			}
			if (lf_overlap_embedded(range, start, end)) {
				nrange = TAILQ_NEXT(range, lf_link);
				TAILQ_REMOVE(&lock->lf_range, range,
					     lf_link);
				lf_count_change(owner, -1);
				lf_destroy_range(range, 1);
				range = nrange;
				continue;
			}
			if (lf_overlap_right(range, start, end) == 0) {
				range = TAILQ_NEXT(range, lf_link);
				continue;
			}
			if (range->lf_type != type) {
				range->lf_start = end + 1;
				nrange = TAILQ_NEXT(range, lf_link);
				TAILQ_REMOVE(&lock->lf_range, range, lf_link);
				while (nrange != NULL) {
					if (nrange->lf_start >= end + 1)
						break;
					nrange = TAILQ_NEXT(nrange, lf_link);
				}
				if (nrange != NULL)
					TAILQ_INSERT_BEFORE(nrange, range,
							    lf_link);
				else
					TAILQ_INSERT_TAIL(&lock->lf_range,
							  range, lf_link);
				break;
			}
			first_match->lf_end = range->lf_end;
			first_match->lf_flags |=
			    range->lf_flags & F_NOEND;
			TAILQ_REMOVE(&lock->lf_range, range, lf_link);
			lf_count_change(owner, -1);
			lf_destroy_range(range, 1);
			break;
		}
		goto do_wakeup;
	}

	if (lf_overlap_right(first_match, start, end)) {
		KKASSERT((flags & F_POSIX) != 0);
		if (first_match->lf_type == type) {
			first_match->lf_start = start;
			if (first_match != insert_point) {
				TAILQ_REMOVE(&lock->lf_range, first_match,
					     lf_link);
				TAILQ_INSERT_BEFORE(insert_point, first_match,
						    lf_link);
			}
			goto do_wakeup;
		}
		if (lf_count_change(owner, 1)) {
			if (orig_first_match != NULL) {
				orig_first_match->lf_end = orig_end;
				orig_first_match->lf_flags = orig_end;
			}
			error = ENOLCK;
			goto do_cleanup;
		}
		first_match->lf_start = end + 1;
		KKASSERT(new_range1 != NULL);
		range = new_range1;
		new_range1 = NULL;
		lf_create_range(range, owner, type, flags, start, end, 1);
		TAILQ_INSERT_BEFORE(insert_point, range, lf_link);
		range = TAILQ_NEXT(first_match, lf_link);
		TAILQ_REMOVE(&lock->lf_range, first_match, lf_link);
		while (range != NULL) {
			if (range->lf_start >= first_match->lf_start)
				break;
			range = TAILQ_NEXT(range, lf_link);
		}
		if (range != NULL)
			TAILQ_INSERT_BEFORE(range, first_match, lf_link);
		else
			TAILQ_INSERT_TAIL(&lock->lf_range, first_match, lf_link);
		goto do_wakeup;
	}

do_wakeup:
#ifdef LOCKF_DEBUG
	if (lf_print_ranges)
		lf_print_lock(lock);
#endif
	if (wakeup_needed)
		lf_wakeup(lock, start, end);
	error = 0;
do_cleanup:
	if (new_range1 != NULL)
		lf_destroy_range(new_range1, 0);
	if (new_range2 != NULL)
		lf_destroy_range(new_range2, 0);
	return(error);
}

static int
lf_clearlock(struct lockf *lock, struct proc *owner, int type, int flags,
	     off_t start, off_t end)
{
	struct lockf_range *range, *trange;
	struct lockf_range *new_range;
	int error = 0;

	new_range = lf_alloc_range();

	TAILQ_FOREACH_MUTABLE(range, &lock->lf_range, lf_link, trange) {
		if (range->lf_end < start)
			continue;
		if (range->lf_start > end)
			break;
		if (range->lf_owner != owner)
			continue;
		if (lf_overlap_embedded(range, start, end)) {
			TAILQ_REMOVE(&lock->lf_range, range, lf_link);
			/* flock-locks are equal */
			if (range->lf_flags & F_POSIX)
				lf_count_change(owner, -1);
			lf_destroy_range(range, 1);
			continue;
		}
		if (lf_overlap_left2(range, start, end)) {
			KKASSERT(range->lf_flags & F_POSIX);
			if (lf_overlap_right2(range, start, end)) {
				struct lockf_range *nrange;

				if (lf_count_change(owner, 1)) {
					error = ENOLCK;
					goto do_cleanup;
				}
				nrange = new_range;
				new_range = NULL;
				lf_create_range(nrange, range->lf_owner,
				    range->lf_type, range->lf_flags,
				    end + 1, range->lf_end, 1);
				range->lf_end = start;
				range->lf_flags &= ~F_NOEND;
				for (; range != NULL;
				     range = TAILQ_NEXT(range, lf_link))
					if (range->lf_start >= nrange->lf_start)
						break;
				if (range != NULL)
					TAILQ_INSERT_BEFORE(range, nrange,
							    lf_link);
				else
					TAILQ_INSERT_TAIL(&lock->lf_range,
							  nrange, lf_link);
				break;
			}
			range->lf_end = start - 1;
			range->lf_flags &= ~F_NOEND;
			continue;
		}
		if (lf_overlap_right2(range, start, end)) {
			struct lockf_range *nrange = range;

			KKASSERT(range->lf_flags & F_POSIX);

			range  = TAILQ_NEXT(range, lf_link);
			TAILQ_REMOVE(&lock->lf_range, nrange, lf_link);
			for (; range != NULL;
			     range = TAILQ_NEXT(range, lf_link))
				if (range->lf_start >= nrange->lf_start)
					break;
			if (range != NULL)
				TAILQ_INSERT_BEFORE(range, nrange, lf_link);
			else
				TAILQ_INSERT_TAIL(&lock->lf_range, nrange,
						  lf_link);
			break;
		}
	}

	lf_wakeup(lock, start, end);
	error = 0;

do_cleanup:
	if (new_range != NULL)
		lf_destroy_range(new_range, 0);

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
 * Check wether range and [start, end] overlap.
 */
static int
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
 * Wakeup pending lock attempts.
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

static int
lf_overlap_left(const struct lockf_range *range, off_t start, off_t end)
{
	if (range->lf_start < start && range->lf_end >= start - 1 &&
	    range->lf_end <= end)
		return(1);
	else
		return(0);
		
}

static int
lf_overlap_right(const struct lockf_range *range, off_t start, off_t end)
{
	if (range->lf_end > end && range->lf_start >= start &&
	    range->lf_start - 1 <= end)
		return(1);
	else
		return(0);
}

static int
lf_overlap_left2(const struct lockf_range *range, off_t start, off_t end)
{
	if (range->lf_start < start && range->lf_end >= start &&
	    range->lf_end <= end)
		return(1);
	else
		return(0);
		
}

static int
lf_overlap_right2(const struct lockf_range *range, off_t start, off_t end)
{
	if (range->lf_end > end && range->lf_start >= start &&
	    range->lf_start <= end)
		return(1);
	else
		return(0);
}

static int
lf_overlap_embedded(const struct lockf_range *range, off_t start, off_t end)
{
	if (range->lf_start >= start && range->lf_end <= end)
		return(1);
	else
		return(0);
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
	lf_global_counter++;
#endif
	range = malloc(sizeof(struct lockf_range), M_LOCKF, M_WAITOK);
	range->lf_owner = NULL;
	return(range);
}

static void
lf_create_range(struct lockf_range *range, struct proc *owner, int type,
		int flags, off_t start, off_t end, int accounting)
{
	KKASSERT(start <= end);
	if (owner != NULL && (flags & F_POSIX) && accounting)
		++owner->p_numposixlocks;
	range->lf_type = type;
	range->lf_flags = flags;
	range->lf_start = start;
	range->lf_end = end;
	range->lf_owner = owner;

#ifdef LOCKF_DEBUG
	if (lf_print_ranges)
		printf("lf_create_range: %lld..%lld\n", range->lf_start,
		       range->lf_end);
#endif
}

static void
lf_destroy_range(struct lockf_range *range, int accounting)
{
	struct proc *owner = range->lf_owner;
	int flags = range->lf_flags;

#ifdef LOCKF_DEBUG
	if (lf_print_ranges)
		printf("lf_destroy_range: %lld..%lld\n", range->lf_start,
		       range->lf_end);
#endif

	free(range, M_LOCKF);
	if (owner != NULL && (flags & F_POSIX) && accounting) {
		--owner->p_numposixlocks;
		KASSERT(owner->p_numposixlocks >= 0,
			("Negative number of POSIX locks held by process: %d",
			 owner->p_numposixlocks));
	}

#ifdef INVARIANTS
	lf_global_counter--;
	KKASSERT(lf_global_counter>=0);
#endif
}

#ifdef LOCKF_DEBUG
static void
lf_print_lock(const struct lockf *lock)
{
	struct lockf_range *range;

	if (TAILQ_EMPTY(&lock->lf_range))
		printf("lockf %p: no ranges locked\n", lock);
	else
		printf("lockf %p:\n", lock);
	TAILQ_FOREACH(range, &lock->lf_range, lf_link)
		printf("\t%lld..%lld type %s owned by %d\n",
		       range->lf_start, range->lf_end,
		       range->lf_type == F_RDLCK ? "shared" : "exclusive",
		       range->lf_flags & F_POSIX ? range->lf_owner->p_pid : -1);
	if (TAILQ_EMPTY(&lock->lf_blocked))
		printf("no process waiting for range\n");
	else
		printf("blocked locks:");
	TAILQ_FOREACH(range, &lock->lf_range, lf_link)
		printf("\t%lld..%lld type %s waiting on %p\n",
		       range->lf_start, range->lf_end,
		       range->lf_type == F_RDLCK ? "shared" : "exclusive",
		       range);
}
#endif /* LOCKF_DEBUG */
