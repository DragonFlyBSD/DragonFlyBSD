/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/vm/vm_vmspace.c,v 1.12 2007/06/29 21:54:15 dillon Exp $
 */
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kern_syscall.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/vkernel.h>
#include <sys/vmspace.h>

#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <ddb/ddb.h>

#include <machine/vmparam.h>

#include <sys/spinlock2.h>
#include <sys/sysref2.h>

static struct vmspace_entry *vkernel_find_vmspace(struct vkernel_common *vc,
						  void *id);
static void vmspace_entry_delete(struct vmspace_entry *ve,
				 struct vkernel_common *vc);

static MALLOC_DEFINE(M_VKERNEL, "vkernel", "VKernel structures");

/*
 * vmspace_create (void *id, int type, void *data)
 *
 * Create a VMSPACE under the control of the caller with the specified id.
 * An id of NULL cannot be used.  The type and data fields must currently
 * be 0.
 *
 * The vmspace starts out completely empty.  Memory may be mapped into the
 * VMSPACE with vmspace_mmap() and MAP_VPAGETABLE section(s) controlled
 * with vmspace_mcontrol().
 */
int
sys_vmspace_create(struct vmspace_create_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;

	if (vkernel_enable == 0)
		return (EOPNOTSUPP);

	/*
	 * Create a virtual kernel side-structure for the process if one
	 * does not exist.
	 */
	if ((vk = curproc->p_vkernel) == NULL) {
		vk = kmalloc(sizeof(*vk), M_VKERNEL, M_WAITOK|M_ZERO);
		vc = kmalloc(sizeof(*vc), M_VKERNEL, M_WAITOK|M_ZERO);
		vc->vc_refs = 1;
		spin_init(&vc->vc_spin);
		RB_INIT(&vc->vc_root);
		vk->vk_common = vc;
		curproc->p_vkernel = vk;
	}
	vc = vk->vk_common;

	/*
	 * Create a new VMSPACE
	 */
	if (vkernel_find_vmspace(vc, uap->id))
		return (EEXIST);
	ve = kmalloc(sizeof(struct vmspace_entry), M_VKERNEL, M_WAITOK|M_ZERO);
	ve->vmspace = vmspace_alloc(VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	ve->id = uap->id;
	pmap_pinit2(vmspace_pmap(ve->vmspace));
	RB_INSERT(vmspace_rb_tree, &vc->vc_root, ve);
	return (0);
}

/*
 * vmspace_destroy (void *id)
 *
 * Destroy a VMSPACE.
 */
int
sys_vmspace_destroy(struct vmspace_destroy_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);
	if (ve->refs)
		return (EBUSY);
	vmspace_entry_delete(ve, vc);
	return(0);
}

/*
 * vmspace_ctl (void *id, int cmd, struct trapframe *tframe,
 *		struct vextframe *vframe);
 *
 * Transfer control to a VMSPACE.  Control is returned after the specified
 * number of microseconds or if a page fault, signal, trap, or system call
 * occurs.  The context is updated as appropriate.
 */
int
sys_vmspace_ctl(struct vmspace_ctl_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;
	struct lwp *lwp;
	struct proc *p;
	int framesz;
	int error;

	lwp = curthread->td_lwp;
	p = lwp->lwp_proc;

	if ((vk = p->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);

	/*
	 * Signal mailbox interlock
	 */
	if (p->p_flag & P_MAILBOX) {
		p->p_flag &= ~P_MAILBOX;
		return (EINTR);
	}

	switch(uap->cmd) {
	case VMSPACE_CTL_RUN:
		/*
		 * Save the caller's register context, swap VM spaces, and
		 * install the passed register context.  Return with
		 * EJUSTRETURN so the syscall code doesn't adjust the context.
		 */
		++ve->refs;
		framesz = sizeof(struct trapframe);
		vk->vk_user_trapframe = uap->tframe;
		vk->vk_user_vextframe = uap->vframe;
		bcopy(uap->sysmsg_frame, &vk->vk_save_trapframe, framesz);
		bcopy(&curthread->td_tls, &vk->vk_save_vextframe.vx_tls,
		      sizeof(vk->vk_save_vextframe.vx_tls));
		error = copyin(uap->tframe, uap->sysmsg_frame, framesz);
		if (error == 0)
			error = copyin(&uap->vframe->vx_tls, &curthread->td_tls, sizeof(struct savetls));
		if (error == 0)
			error = cpu_sanitize_frame(uap->sysmsg_frame);
		if (error == 0)
			error = cpu_sanitize_tls(&curthread->td_tls);
		if (error) {
			bcopy(&vk->vk_save_trapframe, uap->sysmsg_frame, framesz);
			bcopy(&vk->vk_save_vextframe.vx_tls, &curthread->td_tls,
			      sizeof(vk->vk_save_vextframe.vx_tls));
			set_user_TLS();
			--ve->refs;
		} else {
			lwp->lwp_ve = ve;
			pmap_setlwpvm(lwp, ve->vmspace);
			set_user_TLS();
			set_vkernel_fp(uap->sysmsg_frame);
			error = EJUSTRETURN;
		}
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return(error);
}

/*
 * vmspace_mmap(id, addr, len, prot, flags, fd, offset)
 *
 * map memory within a VMSPACE.  This function is just like a normal mmap()
 * but operates on the vmspace's memory map.  Most callers use this to create
 * a MAP_VPAGETABLE mapping.
 */
int
sys_vmspace_mmap(struct vmspace_mmap_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;
	int error;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);
	error = kern_mmap(ve->vmspace, uap->addr, uap->len,
			  uap->prot, uap->flags,
			  uap->fd, uap->offset, &uap->sysmsg_resultp);
	return (error);
}

/*
 * vmspace_munmap(id, addr, len)
 *
 * unmap memory within a VMSPACE.
 */
int
sys_vmspace_munmap(struct vmspace_munmap_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;
	vm_offset_t addr;
	vm_size_t size, pageoff;
	vm_map_t map;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);

	/*
	 * Copied from sys_munmap()
	 */
	addr = (vm_offset_t)uap->addr;
	size = uap->len;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t)round_page(size);
	if (addr + size < addr)
		return (EINVAL);
	if (size == 0)
		return (0);

	if (VM_MAX_USER_ADDRESS > 0 && addr + size > VM_MAX_USER_ADDRESS)
		return (EINVAL);
	if (VM_MIN_USER_ADDRESS > 0 && addr < VM_MIN_USER_ADDRESS)
		return (EINVAL);
	map = &ve->vmspace->vm_map;
	if (!vm_map_check_protection(map, addr, addr + size, VM_PROT_NONE))
		return (EINVAL);
	vm_map_remove(map, addr, addr + size);
	return (0);
}

/* 
 * vmspace_pread(id, buf, nbyte, flags, offset)
 *
 * Read data from a vmspace.  The number of bytes read is returned or
 * -1 if an unrecoverable error occured.  If the number of bytes read is
 * less then the request size, a page fault occured in the VMSPACE which
 * the caller must resolve in order to proceed.
 */
int
sys_vmspace_pread(struct vmspace_pread_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);
	return (EINVAL);
}

/*
 * vmspace_pwrite(id, buf, nbyte, flags, offset)
 *
 * Write data to a vmspace.  The number of bytes written is returned or
 * -1 if an unrecoverable error occured.  If the number of bytes written is
 * less then the request size, a page fault occured in the VMSPACE which
 * the caller must resolve in order to proceed.
 */
int
sys_vmspace_pwrite(struct vmspace_pwrite_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);
	return (EINVAL);
}

/*
 * vmspace_mcontrol(id, addr, len, behav, value)
 *
 * madvise/mcontrol support for a vmspace.
 */
int
sys_vmspace_mcontrol(struct vmspace_mcontrol_args *uap)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;
	vm_offset_t start, end;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	vc = vk->vk_common;
	if ((ve = vkernel_find_vmspace(vc, uap->id)) == NULL)
		return (ENOENT);

	/*
	 * This code is basically copied from sys_mcontrol()
	 */
	if (uap->behav < 0 || uap->behav > MADV_CONTROL_END)
		return (EINVAL);

	if (VM_MAX_USER_ADDRESS > 0 &&
		((vm_offset_t) uap->addr + uap->len) > VM_MAX_USER_ADDRESS)
		return (EINVAL);
        if (VM_MIN_USER_ADDRESS > 0 && uap->addr < VM_MIN_USER_ADDRESS)
		return (EINVAL);
	if (((vm_offset_t) uap->addr + uap->len) < (vm_offset_t) uap->addr)
		return (EINVAL);

	start = trunc_page((vm_offset_t) uap->addr);
	end = round_page((vm_offset_t) uap->addr + uap->len);

	return (vm_map_madvise(&ve->vmspace->vm_map, start, end,
				uap->behav, uap->value));
}

/*
 * Red black tree functions
 */
static int rb_vmspace_compare(struct vmspace_entry *, struct vmspace_entry *);
RB_GENERATE(vmspace_rb_tree, vmspace_entry, rb_entry, rb_vmspace_compare);
   
/* a->start is address, and the only field has to be initialized */
static int
rb_vmspace_compare(struct vmspace_entry *a, struct vmspace_entry *b)
{
        if ((char *)a->id < (char *)b->id)
                return(-1);
        else if ((char *)a->id > (char *)b->id)
                return(1);
        return(0);
}

static
int
rb_vmspace_delete(struct vmspace_entry *ve, void *data)
{
	struct vkernel_common *vc = data;

	KKASSERT(ve->refs == 0);
	vmspace_entry_delete(ve, vc);
	return(0);
}

/*
 * Remove a vmspace_entry from the RB tree and destroy it.  We have to clean
 * up the pmap, the vm_map, then destroy the vmspace.
 */
static
void
vmspace_entry_delete(struct vmspace_entry *ve, struct vkernel_common *vc)
{
	RB_REMOVE(vmspace_rb_tree, &vc->vc_root, ve);

	pmap_remove_pages(vmspace_pmap(ve->vmspace),
			  VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	vm_map_remove(&ve->vmspace->vm_map,
		      VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	sysref_put(&ve->vmspace->vm_sysref);
	kfree(ve, M_VKERNEL);
}


static
struct vmspace_entry *
vkernel_find_vmspace(struct vkernel_common *vc, void *id)
{
	struct vmspace_entry *ve;
	struct vmspace_entry key;

	key.id = id;
	ve = RB_FIND(vmspace_rb_tree, &vc->vc_root, &key);
	return (ve);
}

/*
 * Manage vkernel refs, used by the kernel when fork()ing or exit()ing
 * a vkernel process.
 */
void
vkernel_inherit(struct proc *p1, struct proc *p2)
{
	struct vkernel_common *vc;
	struct vkernel *vk;

	vk = p1->p_vkernel;
	vc = vk->vk_common;
	KKASSERT(vc->vc_refs > 0);
	atomic_add_int(&vc->vc_refs, 1);
	vk = kmalloc(sizeof(*vk), M_VKERNEL, M_WAITOK|M_ZERO);
	p2->p_vkernel = vk;
	vk->vk_common = vc;
}

void
vkernel_exit(struct proc *p)
{
	struct vkernel_common *vc;
	struct vmspace_entry *ve;
	struct vkernel *vk;
	struct lwp *lp;
	int freeme = 0;

	vk = p->p_vkernel;
	p->p_vkernel = NULL;
	vc = vk->vk_common;
	vk->vk_common = NULL;

	/*
	 * Restore the original VM context if we are killed while running
	 * a different one.
	 *
	 * This isn't supposed to happen.  What is supposed to happen is
	 * that the process should enter vkernel_trap() before the handling
	 * the signal.
	 */
	LIST_FOREACH(lp, &p->p_lwps, lwp_list) {
		if ((ve = lp->lwp_ve) != NULL) {
			kprintf("Warning, pid %d killed with active VC!\n",
				p->p_pid);
#ifdef DDB
			db_print_backtrace();
#endif
			lp->lwp_ve = NULL;
			pmap_setlwpvm(lp, p->p_vmspace);
			KKASSERT(ve->refs > 0);
			--ve->refs;
		}
	}

	/*
	 * Dereference the common area
	 */
	KKASSERT(vc->vc_refs > 0);
	spin_lock_wr(&vc->vc_spin);
	if (--vc->vc_refs == 0)	
		freeme = 1;
	spin_unlock_wr(&vc->vc_spin);

	if (freeme) {
		RB_SCAN(vmspace_rb_tree, &vc->vc_root, NULL,
			rb_vmspace_delete, vc);
		kfree(vc, M_VKERNEL);
	}
	kfree(vk, M_VKERNEL);
}

/*
 * A VM space under virtual kernel control trapped out or made a system call
 * or otherwise needs to return control to the virtual kernel context.
 */
int
vkernel_trap(struct lwp *lp, struct trapframe *frame)
{
	struct proc *p = lp->lwp_proc;
	struct vmspace_entry *ve;
	struct vkernel *vk;
	int error;

	/*
	 * Which vmspace entry was running?
	 */
	vk = p->p_vkernel;
	ve = lp->lwp_ve;
	KKASSERT(ve != NULL);

	/*
	 * Switch the LWP vmspace back to the virtual kernel's VM space.
	 */
	lp->lwp_ve = NULL;
	pmap_setlwpvm(lp, p->p_vmspace);
	KKASSERT(ve->refs > 0);
	--ve->refs;

	/*
	 * Copy the emulated process frame to the virtual kernel process.
	 * The emulated process cannot change TLS descriptors so don't
	 * bother saving them, we already have a copy.
	 *
	 * Restore the virtual kernel's saved context so the virtual kernel
	 * process can resume.
	 */
	error = copyout(frame, vk->vk_user_trapframe, sizeof(*frame));
	bcopy(&vk->vk_save_trapframe, frame, sizeof(*frame));
	bcopy(&vk->vk_save_vextframe.vx_tls, &curthread->td_tls,
	      sizeof(vk->vk_save_vextframe.vx_tls));
	set_user_TLS();
	return(error);
}

