/*
 * (MPSAFE)
 *
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
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kern_syscall.h>
#include <sys/mman.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/vkernel.h>
#include <sys/vmspace.h>

#include <vm/vm_extern.h>
#include <vm/pmap.h>

#include <machine/vmparam.h>
#include <machine/vmm.h>

#include <sys/sysref2.h>

static struct vmspace_entry *vkernel_find_vmspace(struct vkernel_proc *vkp,
						  void *id);
static void vmspace_entry_delete(struct vmspace_entry *ve,
				 struct vkernel_proc *vkp);

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
 *
 * No requirements.
 */
int
sys_vmspace_create(struct vmspace_create_args *uap)
{
	struct vmspace_entry *ve;
	struct vkernel_proc *vkp;
	struct proc *p = curproc;
	int error;

	if (vkernel_enable == 0)
		return (EOPNOTSUPP);

	/*
	 * Create a virtual kernel side-structure for the process if one
	 * does not exist.
	 *
	 * Implement a simple resolution for SMP races.
	 */
	if ((vkp = p->p_vkernel) == NULL) {
		vkp = kmalloc(sizeof(*vkp), M_VKERNEL, M_WAITOK|M_ZERO);
		lwkt_gettoken(&p->p_token);
		if (p->p_vkernel == NULL) {
			vkp->refs = 1;
			lwkt_token_init(&vkp->token, "vkernel");
			RB_INIT(&vkp->root);
			p->p_vkernel = vkp;
		} else {
			kfree(vkp, M_VKERNEL);
			vkp = p->p_vkernel;
		}
		lwkt_reltoken(&p->p_token);
	}

	if (curthread->td_vmm)
		return 0;

	/*
	 * Create a new VMSPACE, disallow conflicting ids
	 */
	ve = kmalloc(sizeof(struct vmspace_entry), M_VKERNEL, M_WAITOK|M_ZERO);
	ve->vmspace = vmspace_alloc(VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	ve->id = uap->id;
	pmap_pinit2(vmspace_pmap(ve->vmspace));

	lwkt_gettoken(&vkp->token);
	if (RB_INSERT(vmspace_rb_tree, &vkp->root, ve)) {
		vmspace_free(ve->vmspace);
		ve->vmspace = NULL; /* safety */
		kfree(ve, M_VKERNEL);
		error = EEXIST;
	} else {
		error = 0;
	}
	lwkt_reltoken(&vkp->token);

	return (error);
}

/*
 * Destroy a VMSPACE given its identifier.
 *
 * No requirements.
 */
int
sys_vmspace_destroy(struct vmspace_destroy_args *uap)
{
	struct vkernel_proc *vkp;
	struct vmspace_entry *ve;
	int error;

	if ((vkp = curproc->p_vkernel) == NULL) {
		error = EINVAL;
		goto done3;
	}
	lwkt_gettoken(&vkp->token);
	if ((ve = vkernel_find_vmspace(vkp, uap->id)) == NULL) {
		error = ENOENT;
		goto done2;
	}
	if (ve->refs) {
		error = EBUSY;
		goto done2;
	}
	vmspace_entry_delete(ve, vkp);
	error = 0;
done2:
	lwkt_reltoken(&vkp->token);
done3:
	return(error);
}

/*
 * vmspace_ctl (void *id, int cmd, struct trapframe *tframe,
 *		struct vextframe *vframe);
 *
 * Transfer control to a VMSPACE.  Control is returned after the specified
 * number of microseconds or if a page fault, signal, trap, or system call
 * occurs.  The context is updated as appropriate.
 *
 * No requirements.
 */
int
sys_vmspace_ctl(struct vmspace_ctl_args *uap)
{
	struct vkernel_proc *vkp;
	struct vkernel_lwp *vklp;
	struct vmspace_entry *ve = NULL;
	struct lwp *lp;
	struct proc *p;
	int framesz;
	int error;

	lp = curthread->td_lwp;
	p = lp->lwp_proc;

	if ((vkp = p->p_vkernel) == NULL)
		return (EINVAL);

	/*
	 * ve only matters when VMM is not used.
	 */
	if (curthread->td_vmm == NULL) {
		lwkt_gettoken(&vkp->token);
		if ((ve = vkernel_find_vmspace(vkp, uap->id)) == NULL) {
			error = ENOENT;
			goto done;
		}
	}

	switch(uap->cmd) {
	case VMSPACE_CTL_RUN:
		/*
		 * Save the caller's register context, swap VM spaces, and
		 * install the passed register context.  Return with
		 * EJUSTRETURN so the syscall code doesn't adjust the context.
		 */
		if (curthread->td_vmm == NULL)
			atomic_add_int(&ve->refs, 1);

		framesz = sizeof(struct trapframe);
		if ((vklp = lp->lwp_vkernel) == NULL) {
			vklp = kmalloc(sizeof(*vklp), M_VKERNEL,
				       M_WAITOK|M_ZERO);
			lp->lwp_vkernel = vklp;
		}
		vklp->user_trapframe = uap->tframe;
		vklp->user_vextframe = uap->vframe;
		bcopy(uap->sysmsg_frame, &vklp->save_trapframe, framesz);
		bcopy(&curthread->td_tls, &vklp->save_vextframe.vx_tls,
		      sizeof(vklp->save_vextframe.vx_tls));
		error = copyin(uap->tframe, uap->sysmsg_frame, framesz);
		if (error == 0) {
			error = copyin(&uap->vframe->vx_tls,
				       &curthread->td_tls,
				       sizeof(struct savetls));
		}
		if (error == 0)
			error = cpu_sanitize_frame(uap->sysmsg_frame);
		if (error == 0)
			error = cpu_sanitize_tls(&curthread->td_tls);
		if (error) {
			bcopy(&vklp->save_trapframe, uap->sysmsg_frame,
			      framesz);
			bcopy(&vklp->save_vextframe.vx_tls, &curthread->td_tls,
			      sizeof(vklp->save_vextframe.vx_tls));
			set_user_TLS();
			if (curthread->td_vmm == NULL)
				atomic_subtract_int(&ve->refs, 1);
		} else {
			/* If it's a VMM thread just set the CR3. We also set the
			 * vklp->ve to a key to be able to distinguish when a
			 * vkernel user process runs and when not (when it's NULL)
			 */
			if (curthread->td_vmm == NULL) {
				vklp->ve = ve;
				pmap_setlwpvm(lp, ve->vmspace);
			} else {
				vklp->ve = uap->id;
				vmm_vm_set_guest_cr3((register_t)uap->id);
			}
			set_user_TLS();
			set_vkernel_fp(uap->sysmsg_frame);
			error = EJUSTRETURN;
		}
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
done:
	if (curthread->td_vmm == NULL)
		lwkt_reltoken(&vkp->token);
	return(error);
}

/*
 * vmspace_mmap(id, addr, len, prot, flags, fd, offset)
 *
 * map memory within a VMSPACE.  This function is just like a normal mmap()
 * but operates on the vmspace's memory map.  Most callers use this to create
 * a MAP_VPAGETABLE mapping.
 *
 * No requirements.
 */
int
sys_vmspace_mmap(struct vmspace_mmap_args *uap)
{
	struct vkernel_proc *vkp;
	struct vmspace_entry *ve;
	int error;

	/*
	 * We hold the vmspace token to serialize calls to vkernel_find_vmspace.
	 */
	lwkt_gettoken(&vmspace_token);
	if ((vkp = curproc->p_vkernel) == NULL) {
		error = EINVAL;
		goto done3;
	}

	/*
	 * NOTE: kern_mmap() can block so we need to temporarily ref ve->refs.
	 */
	lwkt_gettoken(&vkp->token);
	if ((ve = vkernel_find_vmspace(vkp, uap->id)) != NULL) {
		atomic_add_int(&ve->refs, 1);
		error = kern_mmap(ve->vmspace, uap->addr, uap->len,
				  uap->prot, uap->flags,
				  uap->fd, uap->offset, &uap->sysmsg_resultp);
		atomic_subtract_int(&ve->refs, 1);
	} else {
		error = ENOENT;
	}
	lwkt_reltoken(&vkp->token);
done3:
	lwkt_reltoken(&vmspace_token);
	return (error);
}

/*
 * vmspace_munmap(id, addr, len)
 *
 * unmap memory within a VMSPACE.
 *
 * No requirements.
 */
int
sys_vmspace_munmap(struct vmspace_munmap_args *uap)
{
	struct vkernel_proc *vkp;
	struct vmspace_entry *ve;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	vm_map_t map;
	int error;

	if ((vkp = curproc->p_vkernel) == NULL) {
		error = EINVAL;
		goto done3;
	}
	lwkt_gettoken(&vkp->token);
	if ((ve = vkernel_find_vmspace(vkp, uap->id)) == NULL) {
		error = ENOENT;
		goto done2;
	}

	/*
	 * NOTE: kern_munmap() can block so we need to temporarily
	 *	 ref ve->refs.
	 */
	atomic_add_int(&ve->refs, 1);

	/*
	 * Copied from sys_munmap()
	 */
	addr = (vm_offset_t)uap->addr;
	size = uap->len;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t)round_page(size);
	if (size < uap->len) {		/* wrap */
		error = EINVAL;
		goto done1;
	}
	tmpaddr = addr + size;		/* workaround gcc4 opt */
	if (tmpaddr < addr) {		/* wrap */
		error = EINVAL;
		goto done1;
	}
	if (size == 0) {
		error = 0;
		goto done1;
	}

	if (VM_MAX_USER_ADDRESS > 0 && tmpaddr > VM_MAX_USER_ADDRESS) {
		error = EINVAL;
		goto done1;
	}
	if (VM_MIN_USER_ADDRESS > 0 && addr < VM_MIN_USER_ADDRESS) {
		error = EINVAL;
		goto done1;
	}
	map = &ve->vmspace->vm_map;
	if (!vm_map_check_protection(map, addr, tmpaddr, VM_PROT_NONE, FALSE)) {
		error = EINVAL;
		goto done1;
	}
	vm_map_remove(map, addr, addr + size);
	error = 0;
done1:
	atomic_subtract_int(&ve->refs, 1);
done2:
	lwkt_reltoken(&vkp->token);
done3:
	return (error);
}

/* 
 * vmspace_pread(id, buf, nbyte, flags, offset)
 *
 * Read data from a vmspace.  The number of bytes read is returned or
 * -1 if an unrecoverable error occured.  If the number of bytes read is
 * less then the request size, a page fault occured in the VMSPACE which
 * the caller must resolve in order to proceed.
 *
 * (not implemented yet)
 * No requirements.
 */
int
sys_vmspace_pread(struct vmspace_pread_args *uap)
{
	struct vkernel_proc *vkp;
	struct vmspace_entry *ve;
	int error;

	if ((vkp = curproc->p_vkernel) == NULL) {
		error = EINVAL;
		goto done3;
	}
	lwkt_gettoken(&vkp->token);
	if ((ve = vkernel_find_vmspace(vkp, uap->id)) == NULL) {
		error = ENOENT;
		goto done2;
	}
	error = EINVAL;
done2:
	lwkt_reltoken(&vkp->token);
done3:
	return (error);
}

/*
 * vmspace_pwrite(id, buf, nbyte, flags, offset)
 *
 * Write data to a vmspace.  The number of bytes written is returned or
 * -1 if an unrecoverable error occured.  If the number of bytes written is
 * less then the request size, a page fault occured in the VMSPACE which
 * the caller must resolve in order to proceed.
 *
 * (not implemented yet)
 * No requirements.
 */
int
sys_vmspace_pwrite(struct vmspace_pwrite_args *uap)
{
	struct vkernel_proc *vkp;
	struct vmspace_entry *ve;
	int error;

	if ((vkp = curproc->p_vkernel) == NULL) {
		error = EINVAL;
		goto done3;
	}
	lwkt_gettoken(&vkp->token);
	if ((ve = vkernel_find_vmspace(vkp, uap->id)) == NULL) {
		error = ENOENT;
		goto done2;
	}
	error = EINVAL;
done2:
	lwkt_reltoken(&vkp->token);
done3:
	return (error);
}

/*
 * vmspace_mcontrol(id, addr, len, behav, value)
 *
 * madvise/mcontrol support for a vmspace.
 *
 * No requirements.
 */
int
sys_vmspace_mcontrol(struct vmspace_mcontrol_args *uap)
{
	struct vkernel_proc *vkp;
	struct vmspace_entry *ve;
	vm_offset_t start, end;
	vm_offset_t tmpaddr = (vm_offset_t)uap->addr + uap->len;
	int error;

	if ((vkp = curproc->p_vkernel) == NULL) {
		error = EINVAL;
		goto done3;
	}
	lwkt_gettoken(&vkp->token);
	if ((ve = vkernel_find_vmspace(vkp, uap->id)) == NULL) {
		error = ENOENT;
		goto done2;
	}

	/*
	 * NOTE: kern_madvise() can block so we need to temporarily
	 *	 ref ve->refs.
	 */
	atomic_add_int(&ve->refs, 1);

	/*
	 * This code is basically copied from sys_mcontrol()
	 */
	if (uap->behav < 0 || uap->behav > MADV_CONTROL_END) {
		error = EINVAL;
		goto done1;
	}

	if (tmpaddr < (vm_offset_t)uap->addr) {
		error = EINVAL;
		goto done1;
	}
	if (VM_MAX_USER_ADDRESS > 0 && tmpaddr > VM_MAX_USER_ADDRESS) {
		error = EINVAL;
		goto done1;
	}
        if (VM_MIN_USER_ADDRESS > 0 && uap->addr < VM_MIN_USER_ADDRESS) {
		error = EINVAL;
		goto done1;
	}

	start = trunc_page((vm_offset_t) uap->addr);
	end = round_page(tmpaddr);

	error = vm_map_madvise(&ve->vmspace->vm_map, start, end,
				uap->behav, uap->value);
done1:
	atomic_subtract_int(&ve->refs, 1);
done2:
	lwkt_reltoken(&vkp->token);
done3:
	return (error);
}

/*
 * Red black tree functions
 */
static int rb_vmspace_compare(struct vmspace_entry *, struct vmspace_entry *);
RB_GENERATE(vmspace_rb_tree, vmspace_entry, rb_entry, rb_vmspace_compare);
   
/*
 * a->start is address, and the only field has to be initialized.
 * The caller must hold vkp->token.
 *
 * The caller must hold vkp->token.
 */
static int
rb_vmspace_compare(struct vmspace_entry *a, struct vmspace_entry *b)
{
        if ((char *)a->id < (char *)b->id)
                return(-1);
        else if ((char *)a->id > (char *)b->id)
                return(1);
        return(0);
}

/*
 * The caller must hold vkp->token.
 */
static
int
rb_vmspace_delete(struct vmspace_entry *ve, void *data)
{
	struct vkernel_proc *vkp = data;

	KKASSERT(ve->refs == 0);
	vmspace_entry_delete(ve, vkp);
	return(0);
}

/*
 * Remove a vmspace_entry from the RB tree and destroy it.  We have to clean
 * up the pmap, the vm_map, then destroy the vmspace.
 *
 * This function must remove the ve immediately before it might potentially
 * block.
 *
 * The caller must hold vkp->token.
 */
static
void
vmspace_entry_delete(struct vmspace_entry *ve, struct vkernel_proc *vkp)
{
	RB_REMOVE(vmspace_rb_tree, &vkp->root, ve);

	pmap_remove_pages(vmspace_pmap(ve->vmspace),
			  VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	vm_map_remove(&ve->vmspace->vm_map,
		      VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	vmspace_free(ve->vmspace);
	ve->vmspace = NULL; /* safety */
	kfree(ve, M_VKERNEL);
}

/*
 * Locate the ve for (id), return the ve or NULL.  If found this function
 * will bump ve->refs which prevents the ve from being immediately destroyed
 * (but it can still be removed).
 *
 * The caller must hold vkp->token.
 */
static
struct vmspace_entry *
vkernel_find_vmspace(struct vkernel_proc *vkp, void *id)
{
	struct vmspace_entry *ve;
	struct vmspace_entry key;

	key.id = id;
	ve = RB_FIND(vmspace_rb_tree, &vkp->root, &key);
	return (ve);
}

/*
 * Manage vkernel refs, used by the kernel when fork()ing or exit()ing
 * a vkernel process.
 *
 * No requirements.
 */
void
vkernel_inherit(struct proc *p1, struct proc *p2)
{
	struct vkernel_proc *vkp;

	vkp = p1->p_vkernel;
	KKASSERT(vkp->refs > 0);
	atomic_add_int(&vkp->refs, 1);
	p2->p_vkernel = vkp;
}

/*
 * No requirements.
 */
void
vkernel_exit(struct proc *p)
{
	struct vkernel_proc *vkp;
	struct lwp *lp;

	vkp = p->p_vkernel;

	/*
	 * Restore the original VM context if we are killed while running
	 * a different one.
	 *
	 * This isn't supposed to happen.  What is supposed to happen is
	 * that the process should enter vkernel_trap() before the handling
	 * the signal.
	 */
	RB_FOREACH(lp, lwp_rb_tree, &p->p_lwp_tree) {
		vkernel_lwp_exit(lp);
	}

	/*
	 * Dereference the common area
	 */
	p->p_vkernel = NULL;
	KKASSERT(vkp->refs > 0);

	if (atomic_fetchadd_int(&vkp->refs, -1) == 1) {
		lwkt_gettoken(&vkp->token);
		RB_SCAN(vmspace_rb_tree, &vkp->root, NULL,
			rb_vmspace_delete, vkp);
		lwkt_reltoken(&vkp->token);
		kfree(vkp, M_VKERNEL);
	}
}

/*
 * No requirements.
 */
void
vkernel_lwp_exit(struct lwp *lp)
{
	struct vkernel_lwp *vklp;
	struct vmspace_entry *ve;

	if ((vklp = lp->lwp_vkernel) != NULL) {
		if ((ve = vklp->ve) != NULL) {
			kprintf("Warning, pid %d killed with "
				"active VC!\n", lp->lwp_proc->p_pid);
			pmap_setlwpvm(lp, lp->lwp_proc->p_vmspace);
			vklp->ve = NULL;
			KKASSERT(ve->refs > 0);
			atomic_subtract_int(&ve->refs, 1);
		}
		lp->lwp_vkernel = NULL;
		kfree(vklp, M_VKERNEL);
	}
}

/*
 * A VM space under virtual kernel control trapped out or made a system call
 * or otherwise needs to return control to the virtual kernel context.
 *
 * No requirements.
 */
void
vkernel_trap(struct lwp *lp, struct trapframe *frame)
{
	struct proc *p = lp->lwp_proc;
	struct vmspace_entry *ve;
	struct vkernel_lwp *vklp;
	int error;

	/*
	 * Which vmspace entry was running?
	 */
	vklp = lp->lwp_vkernel;
	KKASSERT(vklp);

	/* If it's a VMM thread just set the vkernel CR3 back */
	if (curthread->td_vmm == NULL) {
		ve = vklp->ve;
		KKASSERT(ve != NULL);

		/*
		 * Switch the LWP vmspace back to the virtual kernel's VM space.
		 */
		vklp->ve = NULL;
		pmap_setlwpvm(lp, p->p_vmspace);
		KKASSERT(ve->refs > 0);
		atomic_subtract_int(&ve->refs, 1);
		/* ve is invalid once we kill our ref */
	} else {
		vklp->ve = NULL;
		vmm_vm_set_guest_cr3(p->p_vkernel->vkernel_cr3);
	}
	/*
	 * Copy the emulated process frame to the virtual kernel process.
	 * The emulated process cannot change TLS descriptors so don't
	 * bother saving them, we already have a copy.
	 *
	 * Restore the virtual kernel's saved context so the virtual kernel
	 * process can resume.
	 */
	error = copyout(frame, vklp->user_trapframe, sizeof(*frame));
	bcopy(&vklp->save_trapframe, frame, sizeof(*frame));
	bcopy(&vklp->save_vextframe.vx_tls, &curthread->td_tls,
	      sizeof(vklp->save_vextframe.vx_tls));
	set_user_TLS();
	cpu_vkernel_trap(frame, error);
}
