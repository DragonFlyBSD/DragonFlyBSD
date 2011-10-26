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
 * The spinlock code utilizes two counters to form a virtual FIFO, allowing
 * a spinlock to allocate a slot and then only issue memory read operations
 * until it is handed the lock (if it is not the next owner for the lock).
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
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/ktr.h>

struct spinlock pmap_spin = SPINLOCK_INITIALIZER(pmap_spin);

#ifdef SMP

struct indefinite_info {
	sysclock_t	base;
	int		secs;
};

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

static int64_t spinlocks_contested1;
SYSCTL_QUAD(_debug, OID_AUTO, spinlocks_contested1, CTLFLAG_RD,
    &spinlocks_contested1, 0,
    "Spinlock contention count due to collisions with exclusive lock holders");

static int64_t spinlocks_contested2;
SYSCTL_QUAD(_debug, OID_AUTO, spinlocks_contested2, CTLFLAG_RD,
    &spinlocks_contested2, 0,
    "Serious spinlock contention count");

/*
 * We need a fairly large pool to avoid contention on large SMP systems,
 * particularly multi-chip systems.
 */
/*#define SPINLOCK_NUM_POOL	8101*/
#define SPINLOCK_NUM_POOL	8192
#define SPINLOCK_NUM_POOL_MASK	(SPINLOCK_NUM_POOL - 1)

static __cachealign struct {
	struct spinlock	spin;
	char filler[32 - sizeof(struct spinlock)];
} pool_spinlocks[SPINLOCK_NUM_POOL];

static int spin_indefinite_check(struct spinlock *spin,
				  struct indefinite_info *info);

/*
 * We contested due to another exclusive lock holder.  We lose.
 *
 * We have to unwind the attempt and may acquire the spinlock
 * anyway while doing so.  countb was incremented on our behalf.
 */
int
spin_trylock_contested(struct spinlock *spin)
{
	globaldata_t gd = mycpu;

	/*++spinlocks_contested1;*/
	--gd->gd_spinlocks_wr;
	--gd->gd_curthread->td_critcount;
	return (FALSE);
}

/*
 * The spin_lock() inline was unable to acquire the lock.
 *
 * atomic_swap_int() is the absolute fastest spinlock instruction, at
 * least on multi-socket systems.  All instructions seem to be about
 * the same on single-socket multi-core systems.  However, atomic_swap_int()
 * does not result in an even distribution of successful acquisitions.
 *
 * Another problem we have is that (at least on the 48-core opteron we test
 * with) having all 48 cores contesting the same spin lock reduces
 * performance to around 600,000 ops/sec, verses millions when fewer cores
 * are going after the same lock.
 *
 * Backoff algorithms can create even worse starvation problems, and don't
 * really improve performance when a lot of cores are contending.
 *
 * Our solution is to allow the data cache to lazy-update by reading it
 * non-atomically and only attempting to acquire the lock if the lazy read
 * looks good.  This effectively limits cache bus bandwidth.  A cpu_pause()
 * (for intel/amd anyhow) is not strictly needed as cache bus resource use
 * is governed by the lazy update.
 *
 * WARNING!!!!  Performance matters here, by a huge margin.  There are still
 * 		a few bottlenecks in the kernel (e.g. the PQ_INACTIVE
 *		vm_page_queue) where things like parallel compiles hit up
 *		against full all-cores contention right here.
 *
 *		48-core test with pre-read / -j 48 no-modules kernel compile
 *		came in at 75 seconds.  Without pre-read it came in at 170 seconds.
 *
 *		4-core test with pre-read / -j 48 no-modules kernel compile
 *		came in at 83 seconds.  Without pre-read it came in at 83 seconds
 *		as well (no difference).
 */
void
spin_lock_contested(struct spinlock *spin)
{
	struct indefinite_info info = { 0, 0 };
	int i;

	i = 0;
	++spin->countb;

	/*logspin(beg, spin, 'w');*/
	for (;;) {
		/*
		 * NOTE: Reading spin->counta prior to the swap is extremely
		 *	 important on multi-chip/many-core boxes.  On 48-core
		 *	 this one change improves fully concurrent all-cores
		 *	 compiles by 100% or better.
		 *
		 *	 I can't emphasize enough how important the pre-read is in
		 *	 preventing hw cache bus armageddon on multi-chip systems.
		 *	 And on single-chip/multi-core systems it just doesn't hurt.
		 */
		if (spin->counta == 0 && atomic_swap_int(&spin->counta, 1) == 0)
			break;
		if ((++i & 0x7F) == 0x7F) {
			++spin->countb;
			if (spin_indefinite_check(spin, &info))
				break;
		}
	}
	/*logspin(end, spin, 'w');*/
}

static __inline int
_spin_pool_hash(void *ptr)
{
	int i;

	i = ((int)(uintptr_t) ptr >> 5) ^ ((int)(uintptr_t)ptr >> 12);
	i &= SPINLOCK_NUM_POOL_MASK;
	return (i);
}

void
_spin_pool_lock(void *chan)
{
	struct spinlock *sp;

	sp = &pool_spinlocks[_spin_pool_hash(chan)].spin;
	spin_lock(sp);
}

void
_spin_pool_unlock(void *chan)
{
	struct spinlock *sp;

	sp = &pool_spinlocks[_spin_pool_hash(chan)].spin;
	spin_unlock(sp);
}


static
int
spin_indefinite_check(struct spinlock *spin, struct indefinite_info *info)
{
	sysclock_t count;

	cpu_spinlock_contested();

	count = sys_cputimer->count();
	if (info->secs == 0) {
		info->base = count;
		++info->secs;
	} else if (count - info->base > sys_cputimer->freq) {
		kprintf("spin_lock: %p, indefinite wait (%d secs)!\n",
			spin, info->secs);
		info->base = count;
		++info->secs;
		if (panicstr)
			return (TRUE);
#if defined(INVARIANTS)
		if (spin_lock_test_mode) {
			print_backtrace(-1);
			return (TRUE);
		}
#endif
#if defined(INVARIANTS)
		if (info->secs == 11)
			print_backtrace(-1);
#endif
		if (info->secs == 60)
			panic("spin_lock: %p, indefinite wait!\n", spin);
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
		spin_init(&spin);
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

		spin_init(&spin);
		for (i = spin_test_count; i > 0; --i) {
		    spin_lock_quick(gd, &spin);
		    spin_unlock_quick(gd, &spin);
		}
	}

        return (0);
}

SYSCTL_PROC(_debug, KERN_PROC_ALL, spin_lock_test, CTLFLAG_RW|CTLTYPE_INT,
        0, 0, sysctl_spin_lock_test, "I", "Test spinlock wait code");

#endif	/* INVARIANTS */
#endif	/* SMP */
