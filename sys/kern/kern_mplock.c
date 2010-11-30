/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 * Helper functions for MP lock acquisition and release.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/caps.h>
#include <sys/spinlock.h>
#include <sys/ktr.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>
#include <sys/spinlock2.h>

#ifdef SMP
static int chain_mplock = 0;
static int bgl_yield = 10;
static __int64_t mplock_contention_count = 0;

SYSCTL_INT(_lwkt, OID_AUTO, chain_mplock, CTLFLAG_RW, &chain_mplock, 0,
    "Chain IPI's to other CPU's potentially needing the MP lock when it is yielded");
SYSCTL_INT(_lwkt, OID_AUTO, bgl_yield_delay, CTLFLAG_RW, &bgl_yield, 0,
    "Duration of delay when MP lock is temporarily yielded");
SYSCTL_QUAD(_lwkt, OID_AUTO, mplock_contention_count, CTLFLAG_RW,
	&mplock_contention_count, 0, "spinning due to MPLOCK contention");

/*
 * Kernel Trace
 */
#if !defined(KTR_GIANT_CONTENTION)
#define KTR_GIANT_CONTENTION    KTR_ALL
#endif

KTR_INFO_MASTER(giant);
KTR_INFO(KTR_GIANT_CONTENTION, giant, beg, 0,
	"thread=%p held %s:%-5d  want %s:%-5d",
	 sizeof(void *) * 3 + sizeof(int) * 2);
KTR_INFO(KTR_GIANT_CONTENTION, giant, end, 1,
	"thread=%p held %s:%-5d  want %s:%-5d",
	 sizeof(void *) * 3 + sizeof(int) * 2);

#define loggiant(name)						\
	KTR_LOG(giant_ ## name, curthread,			\
		mp_lock_holder_file, mp_lock_holder_line,	\
		file, line)

int	mp_lock;
int	cpu_contention_mask;
const char *mp_lock_holder_file;	/* debugging */
int	mp_lock_holder_line;		/* debugging */

/*
 * Sets up the initial MP lock state near the start of the kernel boot
 */
void
cpu_get_initial_mplock(void)
{
	mp_lock = 0;	/* cpu 0 */
	curthread->td_mpcount = 1;
}

/*
 * This code is called from the get_mplock() inline when the mplock
 * is not already held.  td_mpcount has already been predisposed
 * (incremented).
 */
void
_get_mplock_predisposed(const char *file, int line)
{
	globaldata_t gd = mycpu;

	if (gd->gd_intr_nesting_level) {
		panic("Attempt to acquire mplock not already held "
		      "in hard section, ipi or interrupt %s:%d",
		      file, line);
	}
	if (atomic_cmpset_int(&mp_lock, -1, gd->gd_cpuid) == 0)
		_get_mplock_contested(file, line);
#ifdef INVARIANTS
	mp_lock_holder_file = file;
	mp_lock_holder_line = line;
#endif
}

/*
 * Called when the MP lock could not be trvially acquired.  The caller
 * has already bumped td_mpcount.
 */
void
_get_mplock_contested(const char *file, int line)
{
	globaldata_t gd = mycpu;
	int ov;
	int nv;
	const void **stkframe = (const void **)&file;

	++mplock_contention_count;
	for (;;) {
		ov = mp_lock;
		nv = gd->gd_cpuid;
		if (ov == gd->gd_cpuid)
			break;
		if (ov == -1) {
			if (atomic_cmpset_int(&mp_lock, ov, gd->gd_cpuid))
				break;
		} else {
			gd->gd_curthread->td_mplock_stallpc = stkframe[-1];
			loggiant(beg);
			lwkt_switch();
			loggiant(end);
			KKASSERT(gd->gd_cpuid == mp_lock);
			break;
		}
	}
}

/*
 * Called if td_mpcount went negative or if td_mpcount + td_xpcount is 0
 * and we were unable to release the MP lock.  Handles sanity checks
 * and conflicts.
 *
 * It is possible for the inline release to have raced an interrupt which
 * get/rel'd the MP lock, causing the inline's cmpset to fail.  If this
 * case occurs mp_lock will either already be in a released state or it
 * will have already been acquired by another cpu.
 */
void
_rel_mplock_contested(void)
{
	globaldata_t gd = mycpu;
	thread_t td = gd->gd_curthread;
	int ov;

	KKASSERT(td->td_mpcount >= 0);
	if (td->td_mpcount + td->td_xpcount == 0) {
		for (;;) {
			ov = mp_lock;
			if (ov != gd->gd_cpuid)
				break;
			if (atomic_cmpset_int(&mp_lock, ov, -1))
				break;
		}
	}
}

/*
 * Called when try_mplock() fails.
 *
 * The inline bumped td_mpcount so we have to undo it.
 *
 * It is possible to race an interrupt which acquired and released the
 * MP lock.  When combined with the td_mpcount decrement we do the MP lock
 * can wind up in any state and possibly not even owned by us.
 *
 * It is also possible for this function to be called even if td_mpcount > 1
 * if someone bumped it and raced an interrupt which then called try_mpock().
 */
void
_try_mplock_contested(const char *file, int line)
{
	globaldata_t gd = mycpu;
	thread_t td = gd->gd_curthread;
	int ov;

	--td->td_mpcount;
	KKASSERT(td->td_mpcount >= 0);
	++mplock_contention_count;

	if (td->td_mpcount + td->td_xpcount == 0) {
		for (;;) {
			ov = mp_lock;
			if (ov != gd->gd_cpuid)
				break;
			if (atomic_cmpset_int(&mp_lock, ov, -1))
				break;
		}
	}
}

/*
 * Called when cpu_try_mplock() fails.
 *
 * The inline did not touch td_mpcount so we do not either.
 */
void
_cpu_try_mplock_contested(const char *file, int line)
{
	++mplock_contention_count;
}

/*
 * Temporarily yield the MP lock.  This is part of lwkt_user_yield()
 * which is kinda hackish.  The MP lock cannot be yielded if inherited
 * due to a preemption.
 */
void
yield_mplock(thread_t td)
{
	int savecnt;

	if (td->td_xpcount == 0) {
		savecnt = td->td_mpcount;
		td->td_mpcount = 1;
		rel_mplock();
		DELAY(bgl_yield);
		get_mplock();
		td->td_mpcount = savecnt;
	}
}

#if 0

/*
 * The rel_mplock() code will call this function after releasing the
 * last reference on the MP lock if cpu_contention_mask is non-zero.
 *
 * We then chain an IPI to a single other cpu potentially needing the
 * lock.  This is a bit heuristical and we can wind up with IPIs flying
 * all over the place.
 */
static void lwkt_mp_lock_uncontested_remote(void *arg __unused);

void
lwkt_mp_lock_uncontested(void)
{
    globaldata_t gd;
    globaldata_t dgd;
    cpumask_t mask;
    cpumask_t tmpmask;
    int cpuid;

    if (chain_mplock) {
	gd = mycpu;
	clr_mplock_contention_mask(gd);
	mask = cpu_contention_mask;
	tmpmask = ~((1 << gd->gd_cpuid) - 1);

	if (mask) {
	    if (mask & tmpmask)
		    cpuid = bsfl(mask & tmpmask);
	    else
		    cpuid = bsfl(mask);
	    atomic_clear_int(&cpu_contention_mask, 1 << cpuid);
	    dgd = globaldata_find(cpuid);
	    lwkt_send_ipiq(dgd, lwkt_mp_lock_uncontested_remote, NULL);
	}
    }
}

/*
 * The idea is for this IPI to interrupt a potentially lower priority
 * thread, such as a user thread, to allow the scheduler to reschedule
 * a higher priority kernel thread that needs the MP lock.
 *
 * For now we set the LWKT reschedule flag which generates an AST in
 * doreti, though theoretically it is also possible to possibly preempt
 * here if the underlying thread was operating in user mode.  Nah.
 */
static void
lwkt_mp_lock_uncontested_remote(void *arg __unused)
{
	need_lwkt_resched();
}

#endif

#endif	/* SMP */
