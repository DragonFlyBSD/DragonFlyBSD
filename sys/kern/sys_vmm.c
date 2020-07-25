/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Mihai Carabas <mihai.carabas@gmail.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmsg.h>
#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/vmm.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/vmm.h>
#include <machine/vmparam.h>

#include <vm/vm_map.h>

/*
 * vmm guest system call:
 * - init the calling thread structure
 * - prepare for running in non-root mode
 */
int
sys_vmm_guest_ctl(struct sysmsg *sysmsg,
		  const struct vmm_guest_ctl_args *uap)
{
	int error = 0;
	struct vmm_guest_options options;
	struct trapframe *tf = sysmsg->sysmsg_frame;
	unsigned long stack_limit = USRSTACK;
	unsigned char stack_page[PAGE_SIZE];

	clear_quickret();

	switch (uap->op) {
	case VMM_GUEST_RUN:
		error = copyin(uap->options, &options,
			       sizeof(struct vmm_guest_options));
		if (error) {
			kprintf("%s: error copyin vmm_guest_options\n",
				__func__);
			goto out;
		}

		while(stack_limit > tf->tf_sp) {
			stack_limit -= PAGE_SIZE;
			options.new_stack -= PAGE_SIZE;

			error = copyin((const void *)stack_limit,
				       (void *)stack_page, PAGE_SIZE);
			if (error) {
				kprintf("%s: error copyin stack\n",
					__func__);
				goto out;
			}

			error = copyout((const void *)stack_page,
					(void *)options.new_stack, PAGE_SIZE);
			if (error) {
				kprintf("%s: error copyout stack\n",
				    __func__);
				goto out;
			}
		}

		bcopy(tf, &options.tf, sizeof(struct trapframe));

		error = vmm_vminit(&options);
		if (error) {
			if (error == ENODEV) {
				kprintf("%s: vmm_vminit failed - "
					"no VMM available \n", __func__);
				goto out;
			}
			kprintf("%s: vmm_vminit failed\n", __func__);
			goto out_exit;
		}

		generic_lwp_return(curthread->td_lwp, tf);

		error = vmm_vmrun();

		break;
	default:
		kprintf("%s: INVALID op\n", __func__);
		error = EINVAL;
		goto out;
	}
out_exit:
	exit1(W_EXITCODE(error, 0));
out:
	return (error);
}

/*
 * The remote IPI will force the cpu out of any VMM mode it is
 * in.  When combined with bumping pm_invgen we can ensure that
 * INVEPT will be called when it returns.
 */
static void
vmm_exit_vmm(void *dummy __unused)
{
}

/*
 * Swap the 64 bit value between *dstaddr and *srcaddr in a pmap-safe manner
 * and invalidate the tlb on all cpus the vkernel is running on.
 *
 * If dstaddr is NULL, just invalidate the tlb on the current cpu.
 *
 * v = *srcaddr
 * v = swap(dstaddr, v)
 * *dstaddr = v
 */
int
sys_vmm_guest_sync_addr(struct sysmsg *sysmsg,
			const struct vmm_guest_sync_addr_args *uap)
{
	int error = 0;
	cpulock_t olock;
	cpulock_t nlock;
	cpumask_t mask;
	struct proc *p = curproc;
	long v;

	if (p->p_vmm == NULL)
		return ENOSYS;
	if (uap->dstaddr == NULL)
		return 0;

	crit_enter_id("vmm_inval");

	/*
	 * Acquire CPULOCK_EXCL, spin while we wait.  This will prevent
	 * any other cpu trying to use related VMMs to wait for us.
	 */
	KKASSERT(CPUMASK_TESTMASK(p->p_vmm_cpumask, mycpu->gd_cpumask) == 0);
	for (;;) {
		olock = p->p_vmm_cpulock & ~CPULOCK_EXCL;
		cpu_ccfence();
		nlock = olock | CPULOCK_EXCL;
		if (atomic_cmpset_int(&p->p_vmm_cpulock, olock, nlock))
			break;
		lwkt_process_ipiq();
		cpu_pause();
	}

	/*
	 * Wait for other cpu's to exit VMM mode (for this vkernel).  No
	 * new cpus will enter VMM mode while we hold the lock.  New waiters
	 * may turn-up though so the wakeup() later on has to be
	 * unconditional.
	 *
	 * We must test on p_vmm_cpulock's counter, not the mask, because
	 * VMM entries will set the mask bit unconditionally first
	 * (interlocking our IPI below) and then conditionally bump the
	 * counter.
	 */
	if (olock & CPULOCK_CNTMASK) {
		mask = p->p_vmm_cpumask;
		CPUMASK_ANDMASK(mask, mycpu->gd_other_cpus);
		lwkt_send_ipiq_mask(mask, vmm_exit_vmm, NULL);
		while (p->p_vmm_cpulock & CPULOCK_CNTMASK) {
			lwkt_process_ipiq();
			cpu_pause();
		}
	}

#ifndef _KERNEL_VIRTUAL
	/*
	 * Ensure that any new entries into VMM mode using
	 * vmm's managed under this process will issue a
	 * INVEPT before resuming.
	 */
	atomic_add_acq_long(&p->p_vmspace->vm_pmap.pm_invgen, 1);
#endif

	/*
	 * Make the requested modification, wakeup any waiters.
	 */
	v = fuword64(uap->srcaddr);
	v = swapu64(uap->dstaddr, v);
	suword64(uap->srcaddr, v);

	/*
	 * VMMs on remote cpus will not be re-entered until we
	 * clear the lock.
	 */
	atomic_clear_int(&p->p_vmm_cpulock, CPULOCK_EXCL);
#if 0
	wakeup(&p->p_vmm_cpulock);
#endif

	crit_exit_id("vmm_inval");

	return error;
}
