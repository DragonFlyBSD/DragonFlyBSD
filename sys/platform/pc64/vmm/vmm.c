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

#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/eventhandler.h>
#include <sys/proc.h>
#include <sys/vkernel.h>

#include <machine/vmm.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>

#include "vmm.h"

static struct vmm_ctl *ctl = NULL;

struct sysctl_ctx_list vmm_sysctl_ctx;
struct sysctl_oid *vmm_sysctl_tree;

int vmm_enabled;

static int
sysctl_vmm_enable(SYSCTL_HANDLER_ARGS)
{
	int error, new_val;

	new_val = vmm_enabled;

	error = sysctl_handle_int(oidp, &new_val, 0, req);
        if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val != 0 && new_val != 1)
		return (EINVAL);

	if (vmm_enabled != new_val) {
		if (new_val == 1) {
			if (ctl->enable()) {
				kprintf("VMM: vmm enable() failed\n");
				return (EINVAL);
			}
		} else if (new_val == 0) {
			if (ctl->disable()) {
				kprintf("VMM: vmm disable() failed\n");
				return (EINVAL);
			}
		}
	} else {
		return (EINVAL);
	}

	vmm_enabled = new_val;

	return (0);
}

static void
vmm_shutdown(void)
{
	if(vmm_enabled)
		ctl->disable();
}

static void
vmm_init(void)
{
	sysctl_ctx_init(&vmm_sysctl_ctx);
	vmm_sysctl_tree = SYSCTL_ADD_NODE(&vmm_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw),
	    OID_AUTO, "vmm",
	    CTLFLAG_RD, 0, "VMM options");

	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		ctl = get_ctl_intel();
	} else if (cpu_vendor_id == CPU_VENDOR_AMD) {
		ctl = get_ctl_amd();
	}

	if (ctl->init()) {
		SYSCTL_ADD_INT(&vmm_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "enable", CTLFLAG_RD,
		    &vmm_enabled, 0,
		    "enable not supported");
	} else {
		SYSCTL_ADD_STRING(&vmm_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "type", CTLFLAG_RD,
		    ctl->name, 0,
		    "Type of the VMM");
		SYSCTL_ADD_PROC(&vmm_sysctl_ctx,
		    SYSCTL_CHILDREN(vmm_sysctl_tree),
		    OID_AUTO, "enable", CTLTYPE_INT | CTLFLAG_WR,
		    NULL, sizeof vmm_enabled, sysctl_vmm_enable, "I",
		    "Control the state of the VMM");

		if (ctl->enable()) {
			kprintf("VMM: vmm enable() failed\n");
		} else {
			vmm_enabled = 1;
		}

		EVENTHANDLER_REGISTER(shutdown_pre_sync, vmm_shutdown, NULL, SHUTDOWN_PRI_DEFAULT-1);
	}
}
SYSINIT(vmm_init, SI_BOOT2_CPU_TOPOLOGY, SI_ORDER_ANY, vmm_init, NULL);


int
vmm_vminit(struct guest_options *options)
{
	if (!vmm_enabled) {
		return ENODEV;
	}

	return ctl->vminit(options);
}

int
vmm_vmdestroy(void)
{
	if (!vmm_enabled) {
		return ENODEV;
	}

	return ctl->vmdestroy();
}

int
vmm_vmrun(void)
{
	if (!vmm_enabled) {
		return ENODEV;
	}
	return ctl->vmrun();
}

int
vmm_vm_set_tls_area(void)
{
	if (!vmm_enabled) {
		return ENODEV;
	}
	return ctl->vm_set_tls_area();
}

void
vmm_vm_set_guest_cr3(register_t guest_cr3)
{
	ctl->vm_set_guest_cr3(guest_cr3);
}

void
vmm_lwp_return(struct lwp *lp, struct trapframe *frame)
{
	ctl->vm_lwp_return(lp, frame);
}

int
vmm_vm_get_gpa(struct proc *p, register_t *gpa, register_t uaddr)
{
	return ctl->vm_get_gpa(p, gpa, uaddr);
}
