/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vm_extern.h	8.2 (Berkeley) 1/12/94
 * $FreeBSD: src/sys/vm/vm_extern.h,v 1.46.2.3 2003/01/13 22:51:17 dillon Exp $
 */

#ifndef _VM_VM_EXTERN_H_
#define	_VM_VM_EXTERN_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _VM_VM_MAP_H_
#include <vm/vm_map.h>
#endif
#ifndef _VM_VM_KERN_H_
#include <vm/vm_kern.h>
#endif
#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>
#endif

struct buf;
struct proc;
struct vmspace;
struct vmtotal;
struct mount;
struct vmspace;
struct vnode;

#ifdef _KERNEL

extern int vkernel_enable;

int grow(struct proc *, size_t);
int kernacc(c_caddr_t, int, int);
vm_offset_t kmem_alloc3(struct vm_map *, vm_size_t,
			vm_subsys_t id, int flags);
vm_offset_t kmem_alloc_nofault(struct vm_map *, vm_size_t,
			vm_subsys_t id, vm_size_t);
vm_offset_t kmem_alloc_pageable(struct vm_map *, vm_size_t,
			vm_subsys_t id);
vm_offset_t kmem_alloc_wait(struct vm_map *, vm_size_t,
			vm_subsys_t id);
vm_offset_t kmem_alloc_attr(struct vm_map *map, vm_size_t size,
			vm_subsys_t id, int flags,
			vm_paddr_t low, vm_paddr_t high, vm_memattr_t memattr);
void kmem_free(struct vm_map *, vm_offset_t, vm_size_t);
void kmem_free_wakeup(struct vm_map *, vm_offset_t, vm_size_t);
void kmem_init(void);
void kmem_suballoc(struct vm_map *, struct vm_map *,
			vm_offset_t *, vm_offset_t *, vm_size_t);
void munmapfd(struct proc *, int);
int swaponvp(struct thread *, struct vnode *, u_quad_t);
int useracc(c_caddr_t, int, int);
int vm_fault(struct vm_map *, vm_offset_t, vm_prot_t, int);
int vm_fault_collapse(struct vm_map *, struct vm_map_entry *);
vm_page_t vm_fault_page(struct vm_map *, vm_offset_t,
			vm_prot_t, int, int *, int *);
vm_page_t vm_fault_page_quick(vm_offset_t, vm_prot_t, int *, int *);
void vm_fault_copy_entry(struct vm_map *, struct vm_map *,
			struct vm_map_entry *, struct vm_map_entry *);
void vm_fault_unwire(struct vm_map *, struct vm_map_entry *);
int vm_fault_wire(struct vm_map *, struct vm_map_entry *, boolean_t, int);
void vm_fork(struct proc *, struct proc *, struct lwp *, int);
int vm_test_nominal(void);
void vm_wait_nominal(void);
void vm_init_limits(struct proc *);

int vm_mmap(struct vm_map *, vm_offset_t *, vm_size_t,
			vm_prot_t, vm_prot_t, int, void *, vm_ooffset_t);
int vm_mmap_to_errno(int rv);
vm_offset_t kmem_alloc_contig(vm_offset_t, vm_paddr_t,
			vm_paddr_t, vm_offset_t);
void vm_set_page_size(void);
struct vmspace *vmspace_alloc(vm_offset_t, vm_offset_t);
void vmspace_initrefs(struct vmspace *);
int vmspace_getrefs(struct vmspace *);
void vmspace_hold(struct vmspace *);
void vmspace_drop(struct vmspace *);
void vmspace_ref(struct vmspace *);
void vmspace_rel(struct vmspace *);
void vmspace_relexit(struct vmspace *);
void vmspace_exitfree(struct proc *);
void *kmem_alloc_swapbacked(kmem_anon_desc_t *kp, vm_size_t size,
			vm_subsys_t id);
void kmem_free_swapbacked(kmem_anon_desc_t *kp);

struct vmspace *vmspace_fork(struct vmspace *origvm,
			struct proc *p2, struct lwp *lp2);
void vmspace_exec(struct proc *, struct vmspace *);
void vmspace_unshare(struct proc *);
void vslock(caddr_t, u_int);
void vsunlock(caddr_t, u_int);
void vm_object_print(/* db_expr_t */ long, boolean_t,
			/* db_expr_t */ long, char *);

static __inline
vm_offset_t
kmem_alloc(struct vm_map *map, vm_size_t size, vm_subsys_t id)
{
	return(kmem_alloc3(map, size, id, 0));
}

static __inline
vm_offset_t
kmem_alloc_stack(struct vm_map *map, vm_size_t size, int kmflags)
{
	return(kmem_alloc3(map, size, VM_SUBSYS_STACK, kmflags|KM_STACK));
}

#endif				/* _KERNEL */

#endif				/* !_VM_VM_EXTERN_H_ */
