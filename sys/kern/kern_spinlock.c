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
 */

/*
 * The implementation is designed to avoid looping when compatible operations
 * are executed.
 *
 * To acquire a spinlock we first increment lock.  Then we check if lock
 * meets our requirements.  For an exclusive spinlock it must be 1, of a
 * shared spinlock it must either be 1 or the SHARED_SPINLOCK bit must be set.
 *
 * Shared spinlock failure case: Decrement the count, loop until we can
 * transition from 0 to SHARED_SPINLOCK|1, or until we find SHARED_SPINLOCK
 * is set and increment the count.
 *
 * Exclusive spinlock failure case: While maintaining the count, clear the
 * SHARED_SPINLOCK flag unconditionally.  Then use an atomic add to transfer
 * the count from the low bits to the high bits of lock.  Then loop until
 * all low bits are 0.  Once the low bits drop to 0 we can transfer the
 * count back with an atomic_cmpset_int(), atomically, and return.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#ifdef INVARIANTS
#include <sys/proc.h>
#endif
#include <sys/priv.h>
#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/clock.h>
#include <sys/indefinite2.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/ktr.h>

struct spinlock pmap_spin = SPINLOCK_INITIALIZER(pmap_spin, "pmap_spin");

/*
 * Kernal Trace
 */
#if !defined(KTR_SPIN_CONTENTION)
#define KTR_SPIN_CONTENTION	KTR_ALL
#endif
#define SPIN_STRING	"spin=%p type=%c"
#define SPIN_ARG_SIZE	(sizeof(void *) + sizeof(int))

KTR_INFO_MASTER(spin);
#if 0
KTR_INFO(KTR_SPIN_CONTENTION, spin, beg, 0, SPIN_STRING, SPIN_ARG_SIZE);
KTR_INFO(KTR_SPIN_CONTENTION, spin, end, 1, SPIN_STRING, SPIN_ARG_SIZE);
#endif

#define logspin(name, spin, type)			\
	KTR_LOG(spin_ ## name, spin, type)

#ifdef INVARIANTS
static int spin_lock_test_mode;
#endif

#ifdef DEBUG_LOCKS_LATENCY

__read_frequently static long spinlocks_add_latency;
SYSCTL_LONG(_debug, OID_AUTO, spinlocks_add_latency, CTLFLAG_RW,
    &spinlocks_add_latency, 0,
    "Add spinlock latency");

#endif

__read_frequently static long spin_backoff_max = 4096;
SYSCTL_LONG(_debug, OID_AUTO, spin_backoff_max, CTLFLAG_RW,
    &spin_backoff_max, 0,
    "Spinlock exponential backoff limit");

/* 1 << n clock cycles, approx */
__read_frequently static long spin_window_shift = 8;
SYSCTL_LONG(_debug, OID_AUTO, spin_window_shift, CTLFLAG_RW,
    &spin_window_shift, 0,
    "Spinlock TSC windowing");

__read_frequently int indefinite_uses_rdtsc = 1;
SYSCTL_INT(_debug, OID_AUTO, indefinite_uses_rdtsc, CTLFLAG_RW,
    &indefinite_uses_rdtsc, 0,
    "Indefinite code uses RDTSC");

/*
 * We contested due to another exclusive lock holder.  We lose.
 *
 * We have to unwind the attempt and may acquire the spinlock
 * anyway while doing so.
 */
int
spin_trylock_contested(struct spinlock *spin)
{
	globaldata_t gd = mycpu;

	/*
	 * Handle degenerate case, else fail.
	 */
	if (atomic_cmpset_int(&spin->lock, SPINLOCK_SHARED|0, 1))
		return TRUE;
	/*atomic_add_int(&spin->lock, -1);*/
	--gd->gd_spinlocks;
	crit_exit_quick(gd->gd_curthread);

	return (FALSE);
}

/*
 * The spin_lock() inline was unable to acquire the lock and calls this
 * function with spin->lock already incremented, passing (spin->lock - 1)
 * to the function (the result of the inline's fetchadd).
 *
 * Note that we implement both exclusive and shared spinlocks, so we cannot
 * use atomic_swap_int().  Instead, we try to use atomic_fetchadd_int()
 * to put most of the burden on the cpu.  Atomic_cmpset_int() (cmpxchg)
 * can cause a lot of unnecessary looping in situations where it is just
 * trying to increment the count.
 *
 * Similarly, we leave the SHARED flag intact and incur slightly more
 * overhead when switching from shared to exclusive.  This allows us to
 * use atomic_fetchadd_int() for both spinlock types in the critical
 * path.
 *
 * The exponential (n^1.5) backoff algorithm is designed to both reduce
 * cache bus contention between cpu cores and sockets, and to allow some
 * bursting of exclusive locks in heavily contended situations to improve
 * performance.
 *
 * The exclusive lock priority mechanism prevents even heavily contended
 * exclusive locks from being starved by shared locks
 */
void
_spin_lock_contested(struct spinlock *spin, const char *ident, int value)
{
	indefinite_info_t info;
	uint32_t ovalue;
	long expbackoff;
	long loop;

	/*
	 * WARNING! Caller has already incremented the lock.  We must
	 *	    increment the count value (from the inline's fetch-add)
	 *	    to match.
	 *
	 * Handle the degenerate case where the spinlock is flagged SHARED
	 * with only our reference.  We can convert it to EXCLUSIVE.
	 */
	if (value == (SPINLOCK_SHARED | 1) - 1) {
		if (atomic_cmpset_int(&spin->lock, SPINLOCK_SHARED | 1, 1))
			return;
	}
	/* ++value; value not used after this */
	info.type = 0;		/* avoid improper gcc warning */
	info.ident = NULL;	/* avoid improper gcc warning */
	info.secs = 0;		/* avoid improper gcc warning */
	info.base = 0;		/* avoid improper gcc warning */
	expbackoff = 0;

	/*
	 * Transfer our exclusive request to the high bits and clear the
	 * SPINLOCK_SHARED bit if it was set.  This makes the spinlock
	 * appear exclusive, preventing any NEW shared or exclusive
	 * spinlocks from being obtained while we wait for existing
	 * shared or exclusive holders to unlock.
	 *
	 * Don't tread on earlier exclusive waiters by stealing the lock
	 * away early if the low bits happen to now be 1.
	 *
	 * The shared unlock understands that this may occur.
	 */
	ovalue = atomic_fetchadd_int(&spin->lock, SPINLOCK_EXCLWAIT - 1);
	ovalue += SPINLOCK_EXCLWAIT - 1;
	if (ovalue & SPINLOCK_SHARED) {
		atomic_clear_int(&spin->lock, SPINLOCK_SHARED);
		ovalue &= ~SPINLOCK_SHARED;
	}

	for (;;) {
		expbackoff = (expbackoff + 1) * 3 / 2;
		if (expbackoff == 6)		/* 1, 3, 6, 10, ... */
			indefinite_init(&info, ident, 0, 'S');
		if (indefinite_uses_rdtsc) {
			if ((rdtsc() >> spin_window_shift) % ncpus != mycpuid)  {
				for (loop = expbackoff; loop; --loop)
					cpu_pause();
			}
		}
		/*cpu_lfence();*/

		/*
		 * If the low bits are zero, try to acquire the exclusive lock
		 * by transfering our high bit reservation to the low bits.
		 *
		 * NOTE: Avoid unconditional atomic op by testing ovalue,
		 *	 otherwise we get cache bus armageddon.
		 *
		 * NOTE: We must also ensure that the SHARED bit is cleared.
		 *	 It is possible for it to wind up being set on a
		 *	 shared lock override of the EXCLWAIT bits.
		 */
		ovalue = spin->lock;
		cpu_ccfence();
		if ((ovalue & (SPINLOCK_EXCLWAIT - 1)) == 0) {
			uint32_t nvalue;

			nvalue= ((ovalue - SPINLOCK_EXCLWAIT) | 1) &
				~SPINLOCK_SHARED;
			if (atomic_fcmpset_int(&spin->lock, &ovalue, nvalue))
				break;
			continue;
		}
		if (expbackoff > 6 + spin_backoff_max)
			expbackoff = 6 + spin_backoff_max;
		if (expbackoff >= 6) {
			if (indefinite_check(&info))
				break;
		}
	}
	if (expbackoff >= 6)
		indefinite_done(&info);
}

/*
 * The spin_lock_shared() inline was unable to acquire the lock and calls
 * this function with spin->lock already incremented.
 *
 * This is not in the critical path unless there is contention between
 * shared and exclusive holders.
 *
 * Exclusive locks have priority over shared locks.  However, this can
 * cause shared locks to be starved when large numbers of threads are
 * competing for exclusive locks so the shared lock code uses TSC-windowing
 * to selectively ignore the exclusive priority mechanism.  This has the
 * effect of allowing a limited number of shared locks to compete against
 * exclusive waiters at any given moment.
 *
 * Note that shared locks do not implement exponential backoff.  Instead,
 * the shared lock simply polls the lock value.  One cpu_pause() is built
 * into indefinite_check().
 */
void
_spin_lock_shared_contested(struct spinlock *spin, const char *ident)
{
	indefinite_info_t info;
	uint32_t ovalue;

	/*
	 * Undo the inline's increment.
	 */
	ovalue = atomic_fetchadd_int(&spin->lock, -1) - 1;

	indefinite_init(&info, ident, 0, 's');
	cpu_pause();

#ifdef DEBUG_LOCKS_LATENCY
	long j;
	for (j = spinlocks_add_latency; j > 0; --j)
		cpu_ccfence();
#endif

	for (;;) {
		/*
		 * Loop until we can acquire the shared spinlock.  Note that
		 * the low bits can be zero while the high EXCLWAIT bits are
		 * non-zero.  In this situation exclusive requesters have
		 * priority (otherwise shared users on multiple cpus can hog
		 * the spinlnock).
		 *
		 * NOTE: Reading spin->lock prior to the swap is extremely
		 *	 important on multi-chip/many-core boxes.  On 48-core
		 *	 this one change improves fully concurrent all-cores
		 *	 compiles by 100% or better.
		 *
		 *	 I can't emphasize enough how important the pre-read
		 *	 is in preventing hw cache bus armageddon on
		 *	 multi-chip systems.  And on single-chip/multi-core
		 *	 systems it just doesn't hurt.
		 */
		cpu_ccfence();

		/*
		 * Ignore the EXCLWAIT bits if we are inside our window.
		 */
		if (indefinite_uses_rdtsc &&
		    (ovalue & (SPINLOCK_EXCLWAIT - 1)) == 0 &&
		    (rdtsc() >> spin_window_shift) % ncpus == mycpuid)  {
			if (atomic_fcmpset_int(&spin->lock, &ovalue,
					       ovalue | SPINLOCK_SHARED | 1)) {
				break;
			}
			continue;
		}

		/*
		 * Check ovalue tightly (no exponential backoff for shared
		 * locks, that would result in horrible performance.  Instead,
		 * shared locks depend on the exclusive priority mechanism
		 * to avoid starving exclusive locks).
		 */
		if (ovalue == 0) {
			if (atomic_fcmpset_int(&spin->lock, &ovalue,
					      SPINLOCK_SHARED | 1)) {
				break;
			}
			continue;
		}

		/*
		 * If SHARED is already set, go for the increment, improving
		 * the exclusive to multiple-readers transition.
		 */
		if (ovalue & SPINLOCK_SHARED) {
			ovalue = atomic_fetchadd_int(&spin->lock, 1);
			/* ovalue += 1; NOT NEEDED */
			if (ovalue & SPINLOCK_SHARED)
				break;
			ovalue = atomic_fetchadd_int(&spin->lock, -1);
			ovalue += -1;
			continue;
		}
		if (indefinite_check(&info))
			break;
		/*
		 * ovalue was wrong anyway, just reload
		 */
		ovalue = spin->lock;
	}
	indefinite_done(&info);
}

/*
 * Automatically avoid use of rdtsc when running in a VM
 */
static void
spinlock_sysinit(void *dummy __unused)
{
	if (vmm_guest)
		indefinite_uses_rdtsc = 0;
}
SYSINIT(spinsysinit, SI_BOOT2_PROC0, SI_ORDER_FIRST, spinlock_sysinit, NULL);


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
SYSCTL_INT(_debug, OID_AUTO, spin_test_count, CTLFLAG_RW, &spin_test_count, 0,
    "Number of iterations to use for spinlock wait code test");

static int
sysctl_spin_lock_test(SYSCTL_HANDLER_ARGS)
{
        struct spinlock spin;
	int error;
	int value = 0;
	int i;

	if ((error = priv_check(curthread, PRIV_ROOT)) != 0)
		return (error);
	if ((error = SYSCTL_IN(req, &value, sizeof(value))) != 0)
		return (error);

	/*
	 * Indefinite wait test
	 */
	if (value == 1) {
		spin_init(&spin, "sysctllock");
		spin_lock(&spin);	/* force an indefinite wait */
		spin_lock_test_mode = 1;
		spin_lock(&spin);
		spin_unlock(&spin);	/* Clean up the spinlock count */
		spin_unlock(&spin);
		spin_lock_test_mode = 0;
	}

	/*
	 * Time best-case exclusive spinlocks
	 */
	if (value == 2) {
		globaldata_t gd = mycpu;

		spin_init(&spin, "sysctllocktest");
		for (i = spin_test_count; i > 0; --i) {
		    _spin_lock_quick(gd, &spin, "test");
		    spin_unlock_quick(gd, &spin);
		}
	}

        return (0);
}

SYSCTL_PROC(_debug, KERN_PROC_ALL, spin_lock_test, CTLFLAG_RW|CTLTYPE_INT,
        0, 0, sysctl_spin_lock_test, "I", "Test spinlock wait code");

#endif	/* INVARIANTS */
