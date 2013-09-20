/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)pmap.h	8.1 (Berkeley) 6/11/93
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Avadis Tevanian, Jr.
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/vm/pmap.h,v 1.33.2.4 2002/03/06 22:44:24 silby Exp $
 * $DragonFly: src/sys/vm/pmap.h,v 1.29 2008/08/25 17:01:42 dillon Exp $
 */

/*
 *	Machine address mapping definitions -- machine-independent
 *	section.  [For machine-dependent section, see "machine/pmap.h".]
 */

#ifndef	_VM_PMAP_H_
#define	_VM_PMAP_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

#ifndef _MACHINE_PMAP_H_
#include <machine/pmap.h>
#endif

#ifdef _KERNEL

#ifndef _VM_VM_H_
#include <vm/vm.h>
#endif

struct lwp;
struct proc;
struct thread;
struct vm_page;
struct vmspace;
struct vmspace_entry;
struct vm_map_entry;

/*
 * Most of these variables represent parameters set up by low level MD kernel
 * boot code to be used by higher level MI initialization code to identify
 * the portions of kernel and physical memory which are free for allocation.
 *
 * KvaStart/KvaEnd/	- Reserved Kernel Virtual Memory address space.
 * KvaSize		  This range represents the entire KVA space but
 *			  might omit certain special mapping areas.  It is
 *			  used to determine what kernel memory userland has
 *			  access to.
 *
 * virtual_{start,end}	- KVA space available for allocation, not including
 *			  KVA space reserved during MD startup.  Used by
 *			  the KMEM subsystem, used to initialize kernel_map.
 *
 * phys_avail[]		- Array of {start,end} physical addresses, not
 *			  including physical memory allocated by MD startup
 *			  code.  Used to initialize the VM subsystem.
 */
extern vm_offset_t KvaStart;
extern vm_offset_t KvaEnd;
extern vm_offset_t KvaSize;
extern vm_offset_t virtual_start;
extern vm_offset_t virtual_end;
extern vm_offset_t virtual2_start;
extern vm_offset_t virtual2_end;
extern vm_paddr_t phys_avail[];	

/*
 * Return true if the passed address is in the kernel address space.
 * This is mainly a check that the address is NOT in the user address space.
 *
 * For a vkernels all addresses are in the kernel address space.
 */
static inline int
kva_p(const void *addr)
{
#ifdef _KERNEL_VIRTUAL
	return (addr != NULL);
#else
	return ((unsigned long)KvaStart <= (unsigned long)addr) &&
		((unsigned long)addr < (unsigned long)KvaEnd);
#endif
}

void		 pmap_change_wiring (pmap_t, vm_offset_t, boolean_t,
			vm_map_entry_t);
void		 pmap_clear_modify (struct vm_page *m);
void		 pmap_clear_reference (struct vm_page *m);
void		 pmap_collect (void);
void		 pmap_copy (pmap_t, pmap_t, vm_offset_t, vm_size_t,
			vm_offset_t);
void		 pmap_copy_page (vm_paddr_t, vm_paddr_t);
void		 pmap_copy_page_frag (vm_paddr_t, vm_paddr_t, size_t bytes);
void		 pmap_enter (pmap_t, vm_offset_t, struct vm_page *,
			vm_prot_t, boolean_t, struct vm_map_entry *);
void		 pmap_enter_quick (pmap_t, vm_offset_t, struct vm_page *);
vm_page_t	 pmap_fault_page_quick(pmap_t, vm_offset_t, vm_prot_t);
vm_paddr_t	 pmap_extract (pmap_t pmap, vm_offset_t va);
void		 pmap_growkernel (vm_offset_t, vm_offset_t);
void		 pmap_init (void);
boolean_t	 pmap_is_modified (struct vm_page *m);
boolean_t	 pmap_ts_referenced (struct vm_page *m);
vm_offset_t	 pmap_map (vm_offset_t *, vm_paddr_t, vm_paddr_t, int);
void		 pmap_object_init_pt (pmap_t pmap, vm_offset_t addr,
		    vm_prot_t prot, vm_object_t object, vm_pindex_t pindex,
		    vm_offset_t size, int pagelimit);
boolean_t	 pmap_page_exists_quick (pmap_t pmap, struct vm_page *m);
void		 pmap_page_protect (struct vm_page *m, vm_prot_t prot);
void		 pmap_page_init (struct vm_page *m);
vm_paddr_t	 pmap_phys_address (vm_pindex_t);
void		 pmap_pinit (pmap_t);
void		 pmap_puninit (pmap_t);
void		 pmap_pinit0 (pmap_t);
void		 pmap_pinit2 (pmap_t);
void		 pmap_protect (pmap_t, vm_offset_t, vm_offset_t, vm_prot_t);
void		 pmap_qenter (vm_offset_t, struct vm_page **, int);
void		 pmap_qremove (vm_offset_t, int);
void		 pmap_kenter (vm_offset_t, vm_paddr_t);
void		 pmap_kenter_quick (vm_offset_t, vm_paddr_t);
void		 pmap_kenter_sync (vm_offset_t);
void		 pmap_kenter_sync_quick (vm_offset_t);
void		 pmap_kmodify_rw(vm_offset_t va);
void		 pmap_kmodify_nc(vm_offset_t va);
void		 pmap_kremove (vm_offset_t);
void		 pmap_kremove_quick (vm_offset_t);
void		 pmap_reference (pmap_t);
void		 pmap_remove (pmap_t, vm_offset_t, vm_offset_t);
void		 pmap_remove_pages (pmap_t, vm_offset_t, vm_offset_t);
void		 pmap_zero_page (vm_paddr_t);
void		 pmap_page_assertzero (vm_paddr_t);
void		 pmap_zero_page_area (vm_paddr_t, int off, int size);
int		 pmap_prefault_ok (pmap_t, vm_offset_t);
void		 pmap_change_attr(vm_offset_t va, vm_size_t count, int mode);
int		 pmap_mincore (pmap_t pmap, vm_offset_t addr);
void		 pmap_init_proc (struct proc *);
void		 pmap_init_thread (struct thread *td);
void		 pmap_replacevm (struct proc *, struct vmspace *, int);
void		 pmap_setlwpvm (struct lwp *, struct vmspace *);

vm_offset_t	 pmap_addr_hint (vm_object_t obj, vm_offset_t addr, vm_size_t size);
void		*pmap_kenter_temporary (vm_paddr_t pa, long i);
void		 pmap_init2 (void);
struct vm_page	*pmap_kvtom(vm_offset_t va);
void		 pmap_object_init(vm_object_t object);
void		 pmap_object_free(vm_object_t object);


#endif /* _KERNEL */

#endif /* _VM_PMAP_H_ */
