/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/vm/Attic/vm_copy.c,v 1.1 2004/01/18 12:29:50 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/resourcevar.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_page2.h>

/*
 * Perform a generic copy between two vm_map's.
 *
 * This code is intended to eventually replace vm_uiomove() and is already
 * used by lwkt_caps.c
 *
 * XXX do COW page optimizations if possible when allowed by page alignment
 * and maxbytes.  maxbytes - bytes represents slop space in the target
 * buffer that can be junked (or invalidated) by the copy.
 */
int
vmspace_copy(struct vmspace *svm, vm_offset_t saddr,
		struct vmspace *dvm, vm_offset_t daddr,
		ssize_t bytes, ssize_t maxbytes)
{
#ifdef NEW_VMSPACE
    vm_paddr_t pa1, pa2;
#else
    vm_page_t m1, m2;
#endif
    int rv;

    if (bytes == 0)
	return(0);
    if (maxbytes < bytes)
	maxbytes = bytes;
    KKASSERT(bytes > 0);

    while (bytes) {
	int n;

	n = bytes;
	if (n > PAGE_SIZE - (saddr & PAGE_MASK))
	    n = PAGE_SIZE - (saddr & PAGE_MASK);
	if (n > PAGE_SIZE - (daddr & PAGE_MASK))
	    n = PAGE_SIZE - (daddr & PAGE_MASK);

	/*
	 * Wire and copy on a page-by-page basis.  There are more efficient
	 * ways of doing this, but this is 'safe'.
	 */
#ifdef NEW_VMSPACE
	rv = vm_fault_wire(&svm->vm_map, saddr, saddr + n);
	if (rv != KERN_SUCCESS)
	    return(EFAULT);
	rv = vm_fault_wire(&dvm->vm_map, daddr, daddr + n);
	if (rv != KERN_SUCCESS) {
	    vm_fault_unwire(&svm->vm_map, saddr, saddr + n);
	    return(EFAULT);
	}
	pa1 = pmap_extract(&svm->vm_pmap, saddr);
	pa2 = pmap_extract(&dvm->vm_pmap, daddr);
	pmap_copy_page_frag(pa1, pa2, n);
	vm_fault_unwire(&svm->vm_map, saddr, saddr + n);
	vm_fault_unwire(&dvm->vm_map, daddr, daddr + n);
#else
	for (;;) {
	    m1 = pmap_extract_vmpage(&svm->vm_pmap, saddr, VM_PROT_READ);
	    if (m1 == NULL) {
		rv = vm_fault(&svm->vm_map, saddr, VM_PROT_READ, VM_FAULT_NORMAL);
		if (rv != KERN_SUCCESS)
		    return(EFAULT);
		continue;
	    }
	    m2 = pmap_extract_vmpage(&dvm->vm_pmap, daddr, VM_PROT_WRITE);
	    if (m2 == NULL) {
		rv = vm_fault(&dvm->vm_map, daddr, VM_PROT_WRITE, VM_FAULT_NORMAL);
		if (rv != KERN_SUCCESS)
		    return(EFAULT);
		continue;
	    }
	    break;
	}

	pmap_copy_page_frag(m1->phys_addr | (saddr & PAGE_MASK),
			    m2->phys_addr | (daddr & PAGE_MASK), n);
#endif
	bytes -= n;
	saddr += n;
	daddr += n;
    }
    return(0);
}

