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
 * $DragonFly: src/sys/vm/vm_vmspace.c,v 1.2 2006/09/17 21:09:40 dillon Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/vkernel.h>

#include <vm/vm_extern.h>
#include <vm/pmap.h>

static struct vmspace_entry *vkernel_find_vmspace(struct vkernel *vk, void *id);

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
	struct vkernel *vk;
	struct vmspace_entry *ve;

	if (vkernel_enable == 0)
		return (EOPNOTSUPP);

	/*
	 * Create a virtual kernel side-structure for the process if one
	 * does not exist.
	 */
	if ((vk = curproc->p_vkernel) == NULL) {
		vk = kmalloc(sizeof(*vk), M_VKERNEL, M_WAITOK|M_ZERO);
		vk->vk_refs = 1;
		RB_INIT(&vk->vk_root);
		curproc->p_vkernel = vk;
	}

	/*
	 * Create a new VMSPACE
	 */
	if (vkernel_find_vmspace(vk, uap->id))
		return (EEXIST);
	ve = kmalloc(sizeof(struct vmspace_entry), M_VKERNEL, M_WAITOK|M_ZERO);
	ve->vmspace = vmspace_alloc(VM_MIN_ADDRESS, VM_MAXUSER_ADDRESS);
	ve->id = uap->id;
	pmap_pinit2(vmspace_pmap(ve->vmspace));
	RB_INSERT(vmspace_rb_tree, &vk->vk_root, ve);
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
	struct vkernel *vk;
	struct vmspace_entry *ve;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	if ((ve = vkernel_find_vmspace(vk, uap->id)) == NULL)
		return (ENOENT);
	/* XXX check if active */
	RB_REMOVE(vmspace_rb_tree, &vk->vk_root, ve);
	vmspace_free(ve->vmspace);
	kfree(ve, M_VKERNEL);
	return(0);
}

/*
 * vmspace_ctl (void *id, int cmd, void *ctx, int ctx_bytes, int timeout_us)
 *
 * Transfer control to a VMSPACE.  Control is returned after the specified
 * number of microseconds or if a page fault, signal, trap, or system call
 * occurs.
 */
int
sys_vmspace_ctl(struct vmspace_ctl_args *uap)
{
	struct vkernel *vk;
	struct vmspace_entry *ve;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	if ((ve = vkernel_find_vmspace(vk, uap->id)) == NULL)
		return (ENOENT);
	return(EINVAL);
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
	struct vkernel *vk;
	struct vmspace_entry *ve;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	if ((ve = vkernel_find_vmspace(vk, uap->id)) == NULL)
		return (ENOENT);
	return(EINVAL);
}

/*
 * vmspace_munmap(id, addr, len)
 *
 * unmap memory within a VMSPACE.
 */
int
sys_vmspace_munmap(struct vmspace_munmap_args *uap)
{
	struct vkernel *vk;
	struct vmspace_entry *ve;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	if ((ve = vkernel_find_vmspace(vk, uap->id)) == NULL)
		return (ENOENT);
	return(EINVAL);
}

/*
 * vmspace_mcontrol(id, addr, len, behav, value)
 *
 * madvise/mcontrol support for a vmspace.
 */
int
sys_vmspace_mcontrol(struct vmspace_mcontrol_args *uap)
{
	struct vkernel *vk;
	struct vmspace_entry *ve;
	vm_offset_t start, end;

	if ((vk = curproc->p_vkernel) == NULL)
		return (EINVAL);
	if ((ve = vkernel_find_vmspace(vk, uap->id)) == NULL)
		return (ENOENT);

	/*
	 * This code is basically copied from sys_mcontrol()
	 */
	if (uap->behav < 0 || uap->behav > MADV_CONTROL_END)
		return (EINVAL);

	if (VM_MAXUSER_ADDRESS > 0 &&
		((vm_offset_t) uap->addr + uap->len) > VM_MAXUSER_ADDRESS)
		return (EINVAL);
#ifndef i386
        if (VM_MIN_ADDRESS > 0 && uap->addr < VM_MIN_ADDRESS)
		return (EINVAL);
#endif
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
	struct vkernel *vk = data;

	RB_REMOVE(vmspace_rb_tree, &vk->vk_root, ve);
	vmspace_free(ve->vmspace);
	kfree(ve, M_VKERNEL);
	return(0);
}

static
struct vmspace_entry *
vkernel_find_vmspace(struct vkernel *vk, void *id)
{
	struct vmspace_entry *ve;
	struct vmspace_entry key;

	key.id = id;
	ve = RB_FIND(vmspace_rb_tree, &vk->vk_root, &key);
	return (ve);
}

/*
 * Manage vkernel refs, used by the kernel when fork()ing or exit()ing
 * a vkernel process.
 */
void
vkernel_hold(struct vkernel *vk)
{
	++vk->vk_refs;
}

void
vkernel_drop(struct vkernel *vk)
{
	KKASSERT(vk->vk_refs > 0);
	if (--vk->vk_refs == 0) {
		RB_SCAN(vmspace_rb_tree, &vk->vk_root, NULL,
			rb_vmspace_delete, vk);
		kfree(vk, M_VKERNEL);
	}
}

