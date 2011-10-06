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
 * $DragonFly: src/sys/kern/kern_spinlock.c,v 1.16 2008/09/11 01:11:42 y0netan1 Exp $
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
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
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
KTR_INFO(KTR_SPIN_CONTENTION, spin, backoff, 2,
	 "spin=%p bo1=%d thr=%p bo=%d",
	 ((2 * sizeof(void *)) + (2 * sizeof(int))));
KTR_INFO(KTR_SPIN_CONTENTION, spin, bofail, 3, SPIN_STRING, SPIN_ARG_SIZE);

#define logspin(name, mtx, type)			\
	KTR_LOG(spin_ ## name, mtx, type)

#define logspin_backoff(mtx, bo1, thr, bo)		\
	KTR_LOG(spin_backoff, mtx, bo1, thr, bo)

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

static int spinlocks_backoff_limit = BACKOFF_LIMIT;
SYSCTL_INT(_debug, OID_AUTO, spinlocks_bolim, CTLFLAG_RW,
    &spinlocks_backoff_limit, 0,
    "Contested spinlock backoff limit");

#define SPINLOCK_NUM_POOL	(1024)
static struct spinlock pool_spinlocks[SPINLOCK_NUM_POOL];

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
	bo->base = 0;	/* silence gcc */
}

/*
 * We contested due to another exclusive lock holder.  We lose.
 */
int
spin_trylock_wr_contested2(globaldata_t gd)
{
	++spinlocks_contested1;
	--gd->gd_spinlocks_wr;
	--gd->gd_curthread->td_critcount;
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
spin_lock_wr_contested2(struct spinlock *mtx)
{
	struct exponential_backoff backoff;
	int value;

	/*
	 * Wait until we can gain exclusive access vs another exclusive
	 * holder.
	 */
	++spinlocks_contested1;
	exponential_init(&backoff, mtx);

	logspin(beg, mtx, 'w');
	do {
		if (exponential_backoff(&backoff))
			break;
		value = atomic_swap_int(&mtx->lock, SPINLOCK_EXCLUSIVE);
	} while (value & SPINLOCK_EXCLUSIVE);
	logspin(end, mtx, 'w');
}

static __inline int
_spin_pool_hash(void *ptr)
{
	int i;
	i = ((int) (uintptr_t) ptr >> 2) ^ ((int) (uintptr_t) ptr >> 12);
	i &= (SPINLOCK_NUM_POOL - 1);
	return (i);
}

struct spinlock *
spin_pool_lock(void *chan)
{
	struct spinlock *sp;

	sp = &pool_spinlocks[_spin_pool_hash(chan)];
	spin_lock(sp);

	return (sp);
}

void
spin_pool_unlock(void *chan)
{
	struct spinlock *sp;

	sp = &pool_spinlocks[_spin_pool_hash(chan)];
	spin_unlock(sp);
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
	int backoff;

#ifdef _RDTSC_SUPPORTED_
	if (cpu_feature & CPUID_TSC) {
		backoff =
		(((u_long)rdtsc() ^ (((u_long)curthread) >> 5)) &
		 (bo->backoff - 1)) + BACKOFF_INITIAL;
	} else
#endif
		backoff = bo->backoff;
	logspin_backoff(bo->mtx, bo->backoff, curthread, backoff);

	/*
	 * Quick backoff
	 */
	for (; backoff; --backoff)
		cpu_pause();
	if (bo->backoff < spinlocks_backoff_limit) {
		bo->backoff <<= 1;
		return (FALSE);
	} else {
		bo->backoff = BACKOFF_INITIAL;
	}

	logspin(bofail, bo->mtx, 'u');

	/*
	 * Indefinite
	 */
	++spinlocks_contested2;
	cpu_spinlock_contested();
	if (bo->nsec == 0) {
		bo->base = sys_cputimer->count();
		bo->nsec = 1;
	}

	count = sys_cputimer->count();
	if (count - bo->base > sys_cputimer->freq) {
		kprintf("spin_lock: %p, indefinite wait!\n", bo->mtx);
		if (panicstr)
			return (TRUE);
#if defined(INVARIANTS)
		if (spin_lock_test_mode) {
			print_backtrace(-1);
			return (TRUE);
		}
#endif
		++bo->nsec;
#if defined(INVARIANTS)
		if (bo->nsec == 11)
			print_backtrace(-1);
#endif
		if (bo->nsec == 60)
			panic("spin_lock: %p, indefinite wait!\n", bo->mtx);
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
SYSCTL_INT(_debug, OID_AUTO, spin_test_count, CTLFLAG_RW, &spin_test_count, 0,
    "Number of iterations to use for spinlock wait code test");

static int
sysctl_spin_lock_test(SYSCTL_HANDLER_ARGS)
{
        struct spinlock mtx;
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
		spin_init(&mtx);
		spin_lock(&mtx);	/* force an indefinite wait */
		spin_lock_test_mode = 1;
		spin_lock(&mtx);
		spin_unlock(&mtx);	/* Clean up the spinlock count */
		spin_unlock(&mtx);
		spin_lock_test_mode = 0;
	}

	/*
	 * Time best-case exclusive spinlocks
	 */
	if (value == 2) {
		globaldata_t gd = mycpu;

		spin_init(&mtx);
		for (i = spin_test_count; i > 0; --i) {
		    spin_lock_quick(gd, &mtx);
		    spin_unlock_quick(gd, &mtx);
		}
	}

        return (0);
}

SYSCTL_PROC(_debug, KERN_PROC_ALL, spin_lock_test, CTLFLAG_RW|CTLTYPE_INT,
        0, 0, sysctl_spin_lock_test, "I", "Test spinlock wait code");

#endif	/* INVARIANTS */
#endif	/* SMP */
