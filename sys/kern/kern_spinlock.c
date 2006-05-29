/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu. and Matthew Dillon
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $DragonFly: src/sys/kern/kern_spinlock.c,v 1.5 2006/05/29 07:29:14 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#ifdef INVARIANTS
#include <sys/proc.h>
#endif
#include <ddb/ddb.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/clock.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/ktr.h>

#define	BACKOFF_INITIAL	1
#define	BACKOFF_LIMIT	256

#ifdef SMP

/*
 * Kernal Trace
 */
#if !defined(KTR_SPIN_CONTENTION)
#define KTR_SPIN_CONTENTION	KTR_ALL
#endif
#define SPIN_STRING	"spin=%p type=%c"
#define SPIN_ARG_SIZE	(sizeof(void *) + sizeof(int))

KTR_INFO_MASTER(spin);
KTR_INFO(KTR_SPIN_CONTENTION, spin, beg, 0, SPIN_STRING, SPIN_ARG_SIZE);
KTR_INFO(KTR_SPIN_CONTENTION, spin, end, 1, SPIN_STRING, SPIN_ARG_SIZE);

#define logspin(name, mtx, type)			\
	KTR_LOG(spin_ ## name, mtx, type)

#ifdef INVARIANTS
static int spin_lock_test_mode;
#endif

static int64_t spinlocks_contested1;
SYSCTL_QUAD(_debug, OID_AUTO, spinlocks_contested1, CTLFLAG_RD, &spinlocks_contested1, 0, "");
static int64_t spinlocks_contested2;
SYSCTL_QUAD(_debug, OID_AUTO, spinlocks_contested2, CTLFLAG_RD, &spinlocks_contested2, 0, "");

struct exponential_backoff {
	int backoff;
	int nsec;
	struct spinlock *mtx;
	sysclock_t base;
};
static int exponential_backoff(struct exponential_backoff *bo);

static __inline
void
exponential_init(struct exponential_backoff *bo, struct spinlock *mtx)
{
	bo->backoff = BACKOFF_INITIAL;
	bo->nsec = 0;
	bo->mtx = mtx;
}

/*
 * We were either contested due to another exclusive lock holder,
 * or due to the presence of shared locks.  We have to undo the mess
 * we created by returning the shared locks.
 *
 * If there was another exclusive lock holder only the exclusive bit
 * in value will be the only bit set.  We don't have to do anything since
 * restoration does not involve any work.  
 *
 * Otherwise we successfully obtained the exclusive bit.  Attempt to
 * clear the shared bits.  If we are able to clear the shared bits 
 * we win.  Otherwise we lose and we have to restore the shared bits
 * we couldn't clear (and also clear our exclusive bit).
 */
int
spin_trylock_wr_contested(struct spinlock *mtx, int value)
{
	int bit;

	++spinlocks_contested1;
	if ((value & SPINLOCK_EXCLUSIVE) == 0) {
		while (value) {
			bit = bsfl(value);
			if (globaldata_find(bit)->gd_spinlocks_rd != 0) {
				atomic_swap_int(&mtx->lock, value);
				return (FALSE);
			}
			value &= ~(1 << bit);
		}
		return (TRUE);
	}
	return (FALSE);
}

/*
 * We were either contested due to another exclusive lock holder,
 * or due to the presence of shared locks
 *
 * NOTE: If value indicates an exclusively held mutex, no shared bits
 * would have been set and we can throw away value. 
 */
void
spin_lock_wr_contested(struct spinlock *mtx, int value)
{
	struct exponential_backoff backoff;
	globaldata_t gd = mycpu;
	int bit;
	int mask;

	/*
	 * Wait until we can gain exclusive access vs another exclusive
	 * holder.
	 */
	exponential_init(&backoff, mtx);
	++spinlocks_contested1;
	logspin(beg, mtx, 'w');

	while (value & SPINLOCK_EXCLUSIVE) {
		value = atomic_swap_int(&mtx->lock, 0x80000000);
		if (exponential_backoff(&backoff)) {
			value &= ~SPINLOCK_EXCLUSIVE;
			break;
		}
	}

	/*
	 * Kill the cached shared bit for our own cpu.  This is the most
	 * common case and there's no sense wasting cpu on it.  Since
	 * spinlocks aren't recursive, we can't own a shared ref on the
	 * spinlock while trying to get an exclusive one.
	 *
	 * If multiple bits are set do not stall on any single cpu.  Check
	 * all cpus that have the cache bit set, then loop and check again,
	 * until we've cleaned all the bits.
	 */
	value &= ~gd->gd_cpumask;

	while ((mask = value) != 0) {
		while (mask) {
			bit = bsfl(value);
			if (globaldata_find(bit)->gd_spinlocks_rd == 0) {
				value &= ~(1 << bit);
			} else if (exponential_backoff(&backoff)) {
				value = 0;
				break;
			}
			mask &= ~(1 << bit);
		}
	}
	logspin(end, mtx, 'w');
}

/*
 * The cache bit wasn't set for our cpu.  Loop until we can set the bit.
 * As with the spin_lock_rd() inline we need a memory fence after incrementing
 * gd_spinlocks_rd to interlock against exclusive spinlocks waiting for
 * that field to clear.
 */
void
spin_lock_rd_contested(struct spinlock *mtx)
{
	struct exponential_backoff backoff;
	globaldata_t gd = mycpu;
	int value = mtx->lock;

	exponential_init(&backoff, mtx);
	++spinlocks_contested1;
	logspin(beg, mtx, 'r');

	while ((value & gd->gd_cpumask) == 0) {
		if (value & SPINLOCK_EXCLUSIVE) {
			--gd->gd_spinlocks_rd;
			if (exponential_backoff(&backoff)) {
				++gd->gd_spinlocks_rd;
				break;
			}
			++gd->gd_spinlocks_rd;
			cpu_mfence();
		} else {
			if (atomic_cmpset_int(&mtx->lock, value, value|gd->gd_cpumask))
				break;
		}
		value = mtx->lock;
	}
	logspin(end, mtx, 'r');
}

/*
 * Handle exponential backoff and indefinite waits.
 *
 * If the system is handling a panic we hand the spinlock over to the caller
 * after 1 second.  After 10 seconds we attempt to print a debugger
 * backtrace.  We also run pending interrupts in order to allow a console
 * break into DDB.
 */
static
int
exponential_backoff(struct exponential_backoff *bo)
{
	sysclock_t count;
	int i;

	/*
	 * Quick backoff
	 */
	for (i = 0; i < bo->backoff; ++i)
		cpu_nop();
	if (bo->backoff < BACKOFF_LIMIT) {
		bo->backoff <<= 1;
		return (FALSE);
	}

	/*
	 * Indefinite
	 */
	++spinlocks_contested2;
	if (bo->nsec == 0) {
		bo->base = sys_cputimer->count();
		bo->nsec = 1;
	}

	count = sys_cputimer->count();
	if (count - bo->base > sys_cputimer->freq) {
		printf("spin_lock: %p, indefinite wait!\n", bo->mtx);
		if (panicstr)
			return (TRUE);
#ifdef INVARIANTS
		if (spin_lock_test_mode) {
			db_print_backtrace();
			return (TRUE);
		}
#endif
		if (++bo->nsec == 11)
			db_print_backtrace();
		if (bo->nsec == 60)
			panic("spin_lock: %p, indefinite wait!\n", bo->mtx);
		splz();
		bo->base = count;
	}
	return (FALSE);
}

/*
 * If INVARIANTS is enabled various spinlock timing tests can be run
 * by setting debug.spin_lock_test:
 *
 *	1	Test the indefinite wait code
 *	2	Time the best-case exclusive lock overhead (spin_test_count)
 *	3	Time the best-case shared lock overhead (spin_test_count)
 */

#ifdef INVARIANTS

static int spin_test_count = 10000000;
SYSCTL_INT(_debug, OID_AUTO, spin_test_count, CTLFLAG_RW, &spin_test_count, 0, "");

static int
sysctl_spin_lock_test(SYSCTL_HANDLER_ARGS)
{
        struct spinlock mtx;
	int error;
	int value = 0;
	int i;

	if ((error = suser(curthread)) != 0)
		return (error);
	if ((error = SYSCTL_IN(req, &value, sizeof(value))) != 0)
		return (error);

	/*
	 * Indefinite wait test
	 */
	if (value == 1) {
		spin_init(&mtx);
		spin_lock_wr(&mtx);	/* force an indefinite wait */
		spin_lock_test_mode = 1;
		spin_lock_wr(&mtx);
		spin_unlock_wr(&mtx);	/* Clean up the spinlock count */
		spin_unlock_wr(&mtx);
		spin_lock_test_mode = 0;
	}

	/*
	 * Time best-case exclusive spinlocks
	 */
	if (value == 2) {
		globaldata_t gd = mycpu;

		spin_init(&mtx);
		for (i = spin_test_count; i > 0; --i) {
		    spin_lock_wr_quick(gd, &mtx);
		    spin_unlock_wr_quick(gd, &mtx);
		}
	}

	/*
	 * Time best-case shared spinlocks
	 */
	if (value == 3) {
		globaldata_t gd = mycpu;

		spin_init(&mtx);
		for (i = spin_test_count; i > 0; --i) {
		    spin_lock_rd_quick(gd, &mtx);
		    spin_unlock_rd_quick(gd, &mtx);
		}
	}
        return (0);
}

SYSCTL_PROC(_debug, KERN_PROC_ALL, spin_lock_test, CTLFLAG_RW|CTLTYPE_INT,
        0, 0, sysctl_spin_lock_test, "I", "Test spinlock wait code");

#endif	/* INVARIANTS */
#endif	/* SMP */
