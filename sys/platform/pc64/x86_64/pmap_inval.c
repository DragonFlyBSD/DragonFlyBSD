/*
 * Copyright (c) 2003-2011 The DragonFly Project.  All rights reserved.
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
 * pmap invalidation support code.  Certain hardware requirements must
 * be dealt with when manipulating page table entries and page directory
 * entries within a pmap.  In particular, we cannot safely manipulate
 * page tables which are in active use by another cpu (even if it is
 * running in userland) for two reasons: First, TLB writebacks will
 * race against our own modifications and tests.  Second, even if we
 * were to use bus-locked instruction we can still screw up the 
 * target cpu's instruction pipeline due to Intel cpu errata.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/smp.h>
#include <machine/globaldata.h>
#include <machine/pmap.h>
#include <machine/pmap_inval.h>

#if 1	/* DEBUGGING */
#define LOOPMASK	(/* 32 * */ 16 * 128 * 1024 - 1)
#endif

struct pmap_inval_info {
	vm_offset_t	va;
	pt_entry_t	*ptep;
	pt_entry_t	opte;
	pt_entry_t	npte;
	enum { INVDONE, INVSTORE, INVCMPSET } mode;
	int		success;
	cpumask_t	done;
	cpumask_t	mask;
#ifdef LOOPMASK
	cpumask_t	sigmask;
	int		failed;
	int		xloops;
#endif
} __cachealign;

typedef struct pmap_inval_info pmap_inval_info_t;

static pmap_inval_info_t	invinfo[MAXCPU];
extern cpumask_t		smp_invmask;
#ifdef LOOPMASK
#ifdef LOOPMASK_IN
extern cpumask_t		smp_in_mask;
#endif
extern cpumask_t		smp_smurf_mask;
#endif

static void
pmap_inval_init(pmap_t pmap)
{
	cpulock_t olock;
	cpulock_t nlock;

	crit_enter_id("inval");

	if (pmap != &kernel_pmap) {
		for (;;) {
			olock = pmap->pm_active_lock;
			cpu_ccfence();
			nlock = olock | CPULOCK_EXCL;
			if (olock != nlock &&
			    atomic_cmpset_int(&pmap->pm_active_lock,
					      olock, nlock)) {
				break;
			}
			lwkt_process_ipiq();
			cpu_pause();
		}
		atomic_add_acq_long(&pmap->pm_invgen, 1);
	}
}

static void
pmap_inval_done(pmap_t pmap)
{
	if (pmap != &kernel_pmap) {
		atomic_clear_int(&pmap->pm_active_lock, CPULOCK_EXCL);
		atomic_add_acq_long(&pmap->pm_invgen, 1);
	}
	crit_exit_id("inval");
}

/*
 * API function - invalidation the pte at (va) and replace *ptep with
 * npte atomically across the pmap's active cpus.
 *
 * This is a holy mess.
 *
 * Returns the previous contents of *ptep.
 */
static
void
loopdebug(const char *msg, pmap_inval_info_t *info)
{
	int p;
	int cpu = mycpu->gd_cpuid;

	cpu_lfence();
	atomic_add_long(&smp_smurf_mask.ary[0], 0);
	kprintf("%s %d mode=%d m=%08jx d=%08jx s=%08jx "
#ifdef LOOPMASK_IN
		"in=%08jx "
#endif
		"smurf=%08jx\n",
		msg, cpu, info->mode,
		info->mask.ary[0],
		info->done.ary[0],
		info->sigmask.ary[0],
#ifdef LOOPMASK_IN
		smp_in_mask.ary[0],
#endif
		smp_smurf_mask.ary[0]);
	kprintf("mdglob ");
	for (p = 0; p < ncpus; ++p)
		kprintf(" %d", CPU_prvspace[p]->mdglobaldata.gd_xinvaltlb);
	kprintf("\n");
}

#define CHECKSIGMASK(info)	_checksigmask(info, __FILE__, __LINE__)

static
void
_checksigmask(pmap_inval_info_t *info, const char *file, int line)
{
	cpumask_t tmp;

	tmp = info->mask;
	CPUMASK_ANDMASK(tmp, info->sigmask);
	if (CPUMASK_CMPMASKNEQ(tmp, info->mask)) {
		kprintf("\"%s\" line %d: bad sig/mask %08jx %08jx\n",
			file, line, info->sigmask.ary[0], info->mask.ary[0]);
	}
}


pt_entry_t
pmap_inval_smp(pmap_t pmap, vm_offset_t va, pt_entry_t *ptep,
	       pt_entry_t npte)
{
	globaldata_t gd = mycpu;
	pmap_inval_info_t *info;
	pt_entry_t opte;
	int cpu = gd->gd_cpuid;
	cpumask_t tmpmask;
	unsigned long rflags;

	/*
	 * Shortcut single-cpu case if possible.
	 */
	if (pmap == NULL)
		pmap = &kernel_pmap;
	pmap_inval_init(pmap);
	if (CPUMASK_CMPMASKEQ(pmap->pm_active, gd->gd_cpumask)) {
		for (;;) {
			opte = *ptep;
			cpu_ccfence();
			if (atomic_cmpset_long(ptep, opte, npte)) {
				if (va == (vm_offset_t)-1)
					cpu_invltlb();
				else
					cpu_invlpg((void *)va);
				pmap_inval_done(pmap);
				return opte;
			}
			cpu_pause();
		}
	}

	/*
	 * We must wait for other cpus which may still be finishing up a
	 * prior operation.
	 */
	info = &invinfo[cpu];
	while (CPUMASK_TESTNZERO(info->done)) {
#ifdef LOOPMASK
		int loops;

		loops = ++info->xloops;
		if ((loops & LOOPMASK) == 0) {
			info->failed = 1;
			loopdebug("orig_waitA", info);
			/* XXX recover from possible bug */
			CPUMASK_ASSZERO(info->done);
		}
#endif
		cpu_pause();
	}
	KKASSERT(info->mode == INVDONE);

	/*
	 * Must disable interrupts to prevent an Xinvltlb (which ignores
	 * critical sections) from trying to execute our command before we
	 * have managed to send any IPIs to the target cpus.
	 */
	rflags = read_rflags();
	cpu_disable_intr();

	/*
	 * Must set our cpu in the invalidation scan mask before
	 * any possibility of [partial] execution (remember, XINVLTLB
	 * can interrupt a critical section).
	 */
	if (CPUMASK_TESTBIT(smp_invmask, cpu)) {
		kprintf("bcpu %d already in\n", cpu);
	}
	ATOMIC_CPUMASK_ORBIT(smp_invmask, cpu);

	info->va = va;
	info->ptep = ptep;
	info->npte = npte;
	info->opte = 0;
#ifdef LOOPMASK
	info->failed = 0;
#endif
	tmpmask = pmap->pm_active;	/* volatile (bits may be cleared) */
	cpu_ccfence();
	CPUMASK_ANDMASK(tmpmask, smp_active_mask);
	CPUMASK_ORBIT(tmpmask, cpu);
	info->mode = INVSTORE;
	cpu_ccfence();

	/*
	 * Command may start executing the moment 'done' is initialized,
	 * disable current cpu interrupt to prevent 'done' field from
	 * changing (other cpus can't clear done bits until the originating
	 * cpu clears its mask bit, but other cpus CAN start clearing their
	 * mask bits).
	 */
	cpu_ccfence();
	info->mask = tmpmask;
#ifdef LOOPMASK
	info->sigmask = tmpmask;
	CHECKSIGMASK(info);
#endif
	info->done = tmpmask;

	/*
	 * Pass our copy of the done bits (so they don't change out from
	 * under us) to generate the Xinvltlb interrupt on the targets.
	 */
	smp_invlpg(&tmpmask);
	opte = info->opte;
	KKASSERT(info->mode == INVDONE);

	/*
	 * Target cpus will be in their loop exiting concurrently with our
	 * cleanup.  They will not lose the bitmask they obtained before so
	 * we can safely clear this bit.
	 */
	ATOMIC_CPUMASK_NANDBIT(smp_invmask, cpu);
	write_rflags(rflags);
	pmap_inval_done(pmap);

	return opte;
}

/*
 * API function - invalidation the pte at (va) and replace *ptep with
 * npte atomically only if *ptep equals opte, across the pmap's active cpus.
 *
 * Returns 1 on success, 0 on failure (caller typically retries).
 */
int
pmap_inval_smp_cmpset(pmap_t pmap, vm_offset_t va, pt_entry_t *ptep,
		      pt_entry_t opte, pt_entry_t npte)
{
	globaldata_t gd = mycpu;
	pmap_inval_info_t *info;
	int success;
	int cpu = gd->gd_cpuid;
	cpumask_t tmpmask;
	unsigned long rflags;

	/*
	 * Shortcut single-cpu case if possible.
	 */
	if (pmap == NULL)
		pmap = &kernel_pmap;
	pmap_inval_init(pmap);
	if (CPUMASK_CMPMASKEQ(pmap->pm_active, gd->gd_cpumask)) {
		if (atomic_cmpset_long(ptep, opte, npte)) {
			if (va == (vm_offset_t)-1)
				cpu_invltlb();
			else
				cpu_invlpg((void *)va);
			pmap_inval_done(pmap);
			return 1;
		} else {
			pmap_inval_done(pmap);
			return 0;
		}
	}

	/*
	 * We must wait for other cpus which may still be finishing
	 * up a prior operation.
	 */
	info = &invinfo[cpu];
	while (CPUMASK_TESTNZERO(info->done)) {
#ifdef LOOPMASK
		int loops;

		loops = ++info->xloops;
		if ((loops & LOOPMASK) == 0) {
			info->failed = 1;
			loopdebug("orig_waitB", info);
			/* XXX recover from possible bug */
			CPUMASK_ASSZERO(info->done);
		}
#endif
		cpu_pause();
	}
	KKASSERT(info->mode == INVDONE);

	/*
	 * Must disable interrupts to prevent an Xinvltlb (which ignores
	 * critical sections) from trying to execute our command before we
	 * have managed to send any IPIs to the target cpus.
	 */
	rflags = read_rflags();
	cpu_disable_intr();

	/*
	 * Must set our cpu in the invalidation scan mask before
	 * any possibility of [partial] execution (remember, XINVLTLB
	 * can interrupt a critical section).
	 */
	if (CPUMASK_TESTBIT(smp_invmask, cpu)) {
		kprintf("acpu %d already in\n", cpu);
	}
	ATOMIC_CPUMASK_ORBIT(smp_invmask, cpu);

	info->va = va;
	info->ptep = ptep;
	info->npte = npte;
	info->opte = opte;
	info->failed = 0;
	tmpmask = pmap->pm_active;	/* volatile */
	cpu_ccfence();
	CPUMASK_ANDMASK(tmpmask, smp_active_mask);
	CPUMASK_ORBIT(tmpmask, cpu);
	info->mode = INVCMPSET;		/* initialize last */
	info->success = 0;

	/*
	 * Command may start executing the moment 'done' is initialized,
	 * disable current cpu interrupt to prevent 'done' field from
	 * changing (other cpus can't clear done bits until the originating
	 * cpu clears its mask bit).
	 */
	cpu_ccfence();
	info->mask = tmpmask;
#ifdef LOOPMASK
	info->sigmask = tmpmask;
	CHECKSIGMASK(info);
#endif
	info->done = tmpmask;

	/*
	 * Calling smp_invlpg() will issue the IPIs to XINVLTLB (which can
	 * execute even from inside a critical section), and will call us
	 * back with via pmap_inval_intr() with interrupts disabled.
	 *
	 * Unlike smp_invltlb(), this interface causes all cpus to stay
	 * inside XINVLTLB until the whole thing is done.  When our cpu
	 * detects that the whole thing is done we execute the requested
	 * operation and return.
	 */
	smp_invlpg(&tmpmask);
	success = info->success;
	KKASSERT(info->mode == INVDONE);

	ATOMIC_CPUMASK_NANDBIT(smp_invmask, cpu);
	write_rflags(rflags);
	pmap_inval_done(pmap);

	return success;
}

/*
 * Called with interrupts hard-disabled.
 */
int
pmap_inval_intr(cpumask_t *cpumaskp)
{
	globaldata_t gd = mycpu;
	pmap_inval_info_t *info;
	int loopme = 0;
	int cpu;
	cpumask_t cpumask;
#ifdef LOOPMASK
	int loops;
#endif

	/*
	 * Check all cpus for invalidations we may need to service.
	 */
	cpu_ccfence();
	cpu = gd->gd_cpuid;
	cpumask = *cpumaskp;

        while (CPUMASK_TESTNZERO(cpumask)) {
                int n = BSFCPUMASK(cpumask);

#ifdef LOOPMASK
		KKASSERT(n >= 0 && n < MAXCPU);
#endif

                CPUMASK_NANDBIT(cpumask, n);
		info = &invinfo[n];

		/*
		 * Due to interrupts/races we can catch a new operation
		 * in an older interrupt.  A fence is needed once we detect
		 * the (not) done bit.
		 */
		if (!CPUMASK_TESTBIT(info->done, cpu))
			continue;
		cpu_lfence();

		/*
		 * info->mask and info->done always contain the originating
		 * cpu until the originator is done.  Targets may still be
		 * present in info->done after the originator is done (they
		 * will be finishing up their loops).
		 *
		 * Clear info->mask bits on other cpus to indicate that they
		 * have quiesced (entered the loop).  Once the other mask bits
		 * are clear we can execute the operation on the original,
		 * then clear the mask and done bits on the originator.  The
		 * targets will then finish up their side and clear their
		 * done bits.
		 *
		 * The command is considered 100% done when all done bits have
		 * been cleared.
		 */
		if (n != cpu) {
			/*
			 * Command state machine for 'other' cpus.
			 */
			if (CPUMASK_TESTBIT(info->mask, cpu)) {
				/*
				 * Other cpu indicate to originator that they
				 * are quiesced.
				 */
				ATOMIC_CPUMASK_NANDBIT(info->mask, cpu);
				loopme = 1;
			} else if (CPUMASK_TESTBIT(info->mask, n)) {
				/*
				 * Other cpu waits for originator (n) to
				 * complete the command.
				 */
				loopme = 1;
			} else {
				/*
				 * Other cpu detects that the originator has
				 * completed its command.  Now that the page
				 * table entry has changed, we can follow up
				 * with our own invalidation.
				 */
				if (info->va == (vm_offset_t)-1)
					cpu_invltlb();
				else
					cpu_invlpg((void *)info->va);
				ATOMIC_CPUMASK_NANDBIT(info->done, cpu);
				/* info invalid now */
				/* loopme left alone */
			}
		} else if (CPUMASK_TESTBIT(info->mask, cpu)) {
			/*
			 * Originator is waiting for other cpus
			 */
			if (CPUMASK_CMPMASKNEQ(info->mask, gd->gd_cpumask)) {
				/*
				 * Originator waits for other cpus to enter
				 * their loop (aka quiesce).
				 */
				loopme = 1;
#ifdef LOOPMASK
				loops = ++info->xloops;
				if ((loops & LOOPMASK) == 0) {
					info->failed = 1;
					loopdebug("orig_waitC", info);
					/* XXX recover from possible bug */
					mdcpu->gd_xinvaltlb = 0;
					smp_invlpg(&smp_active_mask);
				}
#endif
			} else {
				/*
				 * Originator executes operation and clears
				 * mask to allow other cpus to finish.
				 */
				KKASSERT(info->mode != INVDONE);
				if (info->mode == INVSTORE) {
					info->opte = *info->ptep;
					cpu_ccfence();
					if (atomic_cmpset_long(info->ptep,
						     info->opte, info->npte)) {
						CHECKSIGMASK(info);
						ATOMIC_CPUMASK_NANDBIT(info->mask, cpu);
						CHECKSIGMASK(info);
					}
					/* else will loop/retry */
				} else {
					if (atomic_cmpset_long(info->ptep,
							      info->opte, info->npte)) {
						info->success = 1;
					} else {
						info->success = 0;
					}
					CHECKSIGMASK(info);
					ATOMIC_CPUMASK_NANDBIT(info->mask, cpu);
					CHECKSIGMASK(info);
				}
				loopme = 1;
			}
		} else {
			/*
			 * Originator does not have to wait for the other
			 * cpus to finish.  It clears its done bit.  A new
			 * command will not be initiated by the originator
			 * until the other cpus have cleared their done bits
			 * (asynchronously).
			 */
			if (info->va == (vm_offset_t)-1)
				cpu_invltlb();
			else
				cpu_invlpg((void *)info->va);
#ifdef LOOPMASK
			info->xloops = 0;
#endif
			/* leave loopme alone */
			/* other cpus may still be finishing up */
			/* can't race originator since that's us */
			info->mode = INVDONE;
			ATOMIC_CPUMASK_NANDBIT(info->done, cpu);
		}
        }
	return loopme;
}
