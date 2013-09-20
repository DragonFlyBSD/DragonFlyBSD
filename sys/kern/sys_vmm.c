/*
 * Copyright (c) 2003-2013 The DragonFly Project.  All rights reserved.
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
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/vmm.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/cpu.h>
#include <machine/vmm.h>

/*
 * vmm guest system call:
 * - init the calling thread structure
 * - prepare for running in non-root mode
 */
int
sys_vmm_guest_ctl(struct vmm_guest_ctl_args *uap)
{
	int error = 0;
	struct guest_options options;
	struct trapframe *tf = uap->sysmsg_frame;
	unsigned long stack_limit = USRSTACK;
	unsigned char stack_page[PAGE_SIZE];

	clear_quickret();

	switch (uap->op) {
		case VMM_GUEST_RUN:
			error = copyin(uap->options, &options, sizeof(struct guest_options));
			if (error) {
				kprintf("sys_vmm_guest: error copyin guest_options\n");
				goto out;
			}

			while(stack_limit > tf->tf_sp) {
				stack_limit -= PAGE_SIZE;
				options.new_stack -= PAGE_SIZE;

				error = copyin((const void *)stack_limit, (void *)stack_page, PAGE_SIZE);
				if (error) {
					kprintf("sys_vmm_guest: error copyin stack\n");
					goto out;
				}

				error = copyout((const void *)stack_page, (void *)options.new_stack, PAGE_SIZE);
				if (error) {
					kprintf("sys_vmm_guest: error copyout stack\n");
					goto out;
				}
			}

			bcopy(tf, &options.tf, sizeof(struct trapframe));

			error = vmm_vminit(&options);
			if (error) {
				if (error == ENODEV) {
					kprintf("sys_vmm_guest: vmm_vminit failed -"
					    "no VMM available \n");
					goto out;
				} else {
					kprintf("sys_vmm_guest: vmm_vminit failed\n");
					goto out_exit;
				}
			}

			generic_lwp_return(curthread->td_lwp, tf);

			error = vmm_vmrun();

			break;
		default:
			kprintf("sys_vmm_guest: INVALID op\n");
			error = EINVAL;
			goto out;
	}
out_exit:
	exit1(W_EXITCODE(error, 0));
out:
	return (error);
}

static
void
vmm_exit_vmm(void *dummy __unused)
{
}

int
sys_vmm_guest_sync_addr(struct vmm_guest_sync_addr_args *uap)
{
	int error = 0;
	cpumask_t oactive;
	cpumask_t nactive;
	long val;
	struct proc *p = curproc;

	if (p->p_vmm == NULL)
		return ENOSYS;

	crit_enter_id("vmm_inval");

	/*
	 * Set CPUMASK_LOCK, spin if anyone else is trying to set CPUMASK_LOCK.
	 */
	for (;;) {
		oactive = p->p_vmm_cpumask & ~CPUMASK_LOCK;
		cpu_ccfence();
		nactive = oactive | CPUMASK_LOCK;
		if (atomic_cmpset_cpumask(&p->p_vmm_cpumask, oactive, nactive))
			break;
		lwkt_process_ipiq();
		cpu_pause();
	}

	/*
	 * Wait for other cpu's to exit VMM mode (for this vkernel).  No
	 * new cpus will enter VMM mode while we hold the lock.  New waiters
	 * may turn-up though so the wakeup() later on has to be
	 * unconditional.
	 */
	if (oactive & mycpu->gd_other_cpus) {
		lwkt_send_ipiq_mask(oactive & mycpu->gd_other_cpus,
				    vmm_exit_vmm, NULL);
		while (p->p_vmm_cpumask & ~CPUMASK_LOCK) {
			lwkt_process_ipiq();
			cpu_pause();
		}
	}

	/*
	 * Make the requested modification, wakeup any waiters.
	 */
	copyin(uap->srcaddr, &val, sizeof(long));
	copyout(&val, uap->dstaddr, sizeof(long));

	atomic_clear_cpumask(&p->p_vmm_cpumask, CPUMASK_LOCK);
	wakeup(&p->p_vmm_cpumask);

	crit_exit_id("vmm_inval");

	return error;
}
