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
 * To acquire a spinlock we first increment counta.  Then we check if counta
 * meets our requirements.  For an exclusive spinlock it must be 1, of a
 * shared spinlock it must either be 1 or the SHARED_SPINLOCK bit must be set.
 *
 * Shared spinlock failure case: Decrement the count, loop until we can
 * transition from 0 to SHARED_SPINLOCK|1, or until we find SHARED_SPINLOCK
 * is set and increment the count.
 *
 * Exclusive spinlock failure case: While maintaining the count, clear the
 * SHARED_SPINLOCK flag unconditionally.  Then use an atomic add to transfer
 * the count from the low bits to the high bits of counta.  Then loop until
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

#ifdef _KERNEL_VIRTUAL
#include <pthread.h>
#endif

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

static long spinlocks_add_latency;
SYSCTL_LONG(_debug, OID_AUTO, spinlocks_add_latency, CTLFLAG_RW,
    &spinlocks_add_latency, 0,
    "Add spinlock latency");

#endif

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
	if (atomic_cmpset_int(&spin->counta, SPINLOCK_SHARED|0, 1))
		return TRUE;
	/*atomic_add_int(&spin->counta, -1);*/
	--gd->gd_spinlocks;
	crit_exit_raw(gd->gd_curthread);

	return (FALSE);
}

/*
 * The spin_lock() inline was unable to acquire the lock and calls this
 * function with spin->counta already incremented, passing (spin->counta - 1)
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
 * Backoff algorithms can create even worse starvation problems, particularly
 * on multi-socket cpus, and don't really improve performance when a lot
 * of cores are contending.  However, if we are contested on an exclusive
 * lock due to a large number of shared locks being present, we throw in
 * extra cpu_pause()'s to account for the necessary time it will take other
 * cores to contend among themselves and release their shared locks.
 */
void
_spin_lock_contested(struct spinlock *spin, const char *ident, int value)
{
	indefinite_info_t info;
	uint32_t ovalue;

	/*
	 * WARNING! Caller has already incremented the lock.  We must
	 *	    increment the count value (from the inline's fetch-add)
	 *	    to match.
	 *
	 * Handle the degenerate case where the spinlock is flagged SHARED
	 * with only our reference.  We can convert it to EXCLUSIVE.
	 */
	++value;
	if (value == (SPINLOCK_SHARED | 1)) {
		if (atomic_cmpset_int(&spin->counta, SPINLOCK_SHARED | 1, 1))
			return;
	}

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
	ovalue = atomic_fetchadd_int(&spin->counta, SPINLOCK_EXCLWAIT - 1);
	ovalue += SPINLOCK_EXCLWAIT - 1;
	if (ovalue & SPINLOCK_SHARED) {
		atomic_clear_int(&spin->counta, SPINLOCK_SHARED);
		ovalue &= ~SPINLOCK_SHARED;
	}

	indefinite_init(&info, ident, 0, 'S');

	/*
	 * Spin until we can acquire a low-count of 1.
	 */
	for (;;) {
		/*
		 * If the low bits are zero, try to acquire the exclusive lock
		 * by transfering our high bit reservation to the low bits.
		 *
		 * NOTE: Reading spin->counta prior to the swap is extremely
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
		if ((ovalue & (SPINLOCK_EXCLWAIT - 1)) == 0) {
			if (atomic_fcmpset_int(&spin->counta, &ovalue,
				      (ovalue - SPINLOCK_EXCLWAIT) | 1)) {
				break;
			}
			continue;
		}

		/*
		 * Throw in extra cpu_pause()'s when we are waiting on
		 * multiple other shared lock holders to release (the
		 * indefinite_check() also throws one in).
		 *
		 * We know these are shared lock holders when the count
		 * is larger than 1, because an exclusive lock holder can
		 * only have one count.  Do this optimization only when
		 * the number of shared lock holders is 3 or greater.
		 */
		ovalue &= SPINLOCK_EXCLWAIT - 1;
		while (ovalue > 2) {
			cpu_pause();
			cpu_pause();
			--ovalue;
		}

		if (indefinite_check(&info))
			break;
		/*
		 * ovalue was wrong anyway, just reload
		 */
		ovalue = spin->counta;
	}
	indefinite_done(&info);
}

/*
 * The spin_lock_shared() inline was unable to acquire the lock and calls
 * this function with spin->counta already incremented.
 *
 * This is not in the critical path unless there is contention between
 * shared and exclusive holders.
 */
void
_spin_lock_shared_contested(struct spinlock *spin, const char *ident)
{
	indefinite_info_t info;
	uint32_t ovalue;

	/*
	 * Undo the inline's increment.
	 */
	ovalue = atomic_fetchadd_int(&spin->counta, -1) - 1;

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
		 * NOTE: Reading spin->counta prior to the swap is extremely
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
		if (ovalue == 0) {
			if (atomic_fcmpset_int(&spin->counta, &ovalue,
					      SPINLOCK_SHARED | 1)) {
				break;
			}
			continue;
		}

		/*
		 * Ignore the EXCLWAIT bits if we have waited too long.
		 * This would be a situation where most of the cpu cores
		 * are concurrently cycling both shared and exclusive use
		 * of the same spinlock, which can cause one or more cores
		 * to wait indefinitely on a shared spinlock.  This can
		 * only occur in the most extreme testing environments.
		 */
		if (info.secs > 1 && (ovalue & (SPINLOCK_EXCLWAIT - 1)) == 0) {
			if (atomic_fcmpset_int(&spin->counta, &ovalue,
					       ovalue | SPINLOCK_SHARED | 1)) {
				break;
			}
			continue;
		}

		/*
		 * If SHARED is already set, go for the increment, improving
		 * the exclusive to multiple-readers transition.
		 */
		if (ovalue & SPINLOCK_SHARED) {
			ovalue = atomic_fetchadd_int(&spin->counta, 1);
			/* ovalue += 1; NOT NEEDED */
			if (ovalue & SPINLOCK_SHARED)
				break;
			ovalue = atomic_fetchadd_int(&spin->counta, -1);
			ovalue += -1;
			continue;
		}
		if (indefinite_check(&info))
			break;
		/*
		 * ovalue was wrong anyway, just reload
		 */
		ovalue = spin->counta;
	}
	indefinite_done(&info);
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
