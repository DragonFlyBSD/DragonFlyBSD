/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/kern/kern_xio.c,v 1.8 2005/03/01 23:35:13 dillon Exp $
 */
/*
 * Kernel XIO interface.  An initialized XIO is basically a collection of
 * appropriately held vm_page_t's.  XIO buffers are vmspace agnostic and
 * can represent userspace or kernelspace buffers, and can be passed to
 * foreign threads outside of the originating vmspace.  XIO buffers are
 * not mapped into KVM and thus can be manipulated and passed around with
 * very low overheads.
 *
 * The intent is for XIO to be used in the I/O path, VFS, CAPS, and other
 * places that need to pass (possibly userspace) data between threads.
 *
 * TODO: check for busy page when modifying, check writeable.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>
#include <sys/xio.h>
#include <sys/sfbuf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_page2.h>

/*
 * Just do basic initialization of an empty XIO
 */
void
xio_init(xio_t xio)
{
    xio->xio_flags = 0;
    xio->xio_bytes = 0;
    xio->xio_error = 0;
    xio->xio_offset = 0;
    xio->xio_npages = 0;
    xio->xio_pages = xio->xio_internal_pages;
}

/*
 * Initialize an XIO given a userspace buffer.  0 is returned on success,
 * an error code on failure.  The actual number of bytes that could be
 * accomodated in the XIO will be stored in xio_bytes and the page offset
 * will be stored in xio_offset.
 */
int
xio_init_ubuf(xio_t xio, void *ubase, size_t ubytes, int flags)
{
    vm_offset_t addr;
    vm_paddr_t paddr;
    vm_page_t m;
    int i;
    int n;
    int s;
    int vmprot;

    addr = trunc_page((vm_offset_t)ubase);
    xio->xio_flags = flags;
    xio->xio_bytes = 0;
    xio->xio_error = 0;
    if (ubytes == 0) {
	xio->xio_offset = 0;
	xio->xio_npages = 0;
    } else {
	vmprot = (flags & XIOF_WRITE) ? VM_PROT_WRITE : VM_PROT_READ;
	xio->xio_offset = (vm_offset_t)ubase & PAGE_MASK;
	xio->xio_pages = xio->xio_internal_pages;
	if ((n = PAGE_SIZE - xio->xio_offset) > ubytes)
	    n = ubytes;
	for (i = 0; n && i < XIO_INTERNAL_PAGES; ++i) {
	    if (vm_fault_quick((caddr_t)addr, vmprot) < 0)
		break;
	    if ((paddr = pmap_kextract(addr)) == 0)
		break;
	    s = splvm();
	    m = PHYS_TO_VM_PAGE(paddr);
	    vm_page_hold(m);
	    splx(s);
	    xio->xio_pages[i] = m;
	    ubytes -= n;
	    xio->xio_bytes += n;
	    if ((n = ubytes) > PAGE_SIZE)
		n = PAGE_SIZE;
	    addr += PAGE_SIZE;
	}
	xio->xio_npages = i;

	/*
	 * If a failure occured clean out what we loaded and return EFAULT.
	 * Return 0 on success.
	 */
	if (i < XIO_INTERNAL_PAGES && n) {
	    xio_release(xio);
	    xio->xio_error = EFAULT;
	}
    }
    return(xio->xio_error);
}

/*
 * Initialize an XIO given a kernelspace buffer.  0 is returned on success,
 * an error code on failure.  The actual number of bytes that could be
 * accomodated in the XIO will be stored in xio_bytes and the page offset
 * will be stored in xio_offset.
 */
int
xio_init_kbuf(xio_t xio, void *kbase, size_t kbytes)
{
    vm_offset_t addr;
    vm_paddr_t paddr;
    vm_page_t m;
    int i;
    int n;
    int s;

    addr = trunc_page((vm_offset_t)kbase);
    xio->xio_flags = 0;
    xio->xio_offset = (vm_offset_t)kbase & PAGE_MASK;
    xio->xio_bytes = 0;
    xio->xio_pages = xio->xio_internal_pages;
    xio->xio_error = 0;
    if ((n = PAGE_SIZE - xio->xio_offset) > kbytes)
	n = kbytes;
    for (i = 0; n && i < XIO_INTERNAL_PAGES; ++i) {
	if ((paddr = pmap_kextract(addr)) == 0)
	    break;
	s = splvm();
	m = PHYS_TO_VM_PAGE(paddr);
	vm_page_hold(m);
	splx(s);
	xio->xio_pages[i] = m;
	kbytes -= n;
	xio->xio_bytes += n;
	if ((n = kbytes) > PAGE_SIZE)
	    n = PAGE_SIZE;
	addr += PAGE_SIZE;
    }
    xio->xio_npages = i;

    /*
     * If a failure occured clean out what we loaded and return EFAULT.
     * Return 0 on success.
     */
    if (i < XIO_INTERNAL_PAGES && n) {
	xio_release(xio);
	xio->xio_error = EFAULT;
    }
    return(xio->xio_error);
}

/*
 * Cleanup an XIO so it can be destroyed.  The pages associated with the
 * XIO are released.
 */
void
xio_release(xio_t xio)
{
    int i;
    int s;
    vm_page_t m;

    s = splvm();
    for (i = 0; i < xio->xio_npages; ++i) {
	m = xio->xio_pages[i];
	vm_page_unhold(m);
    }
    splx(s);
    xio->xio_offset = 0;
    xio->xio_npages = 0;
    xio->xio_bytes = 0;
    xio->xio_error = ENOBUFS;
}

/*
 * Copy data between an XIO and a UIO.  If the UIO represents userspace it
 * must be relative to the current context.
 *
 * uoffset is the abstracted starting offset in the XIO, not the actual
 * offset, and usually starts at 0.
 *
 * The XIO is not modified.  The UIO is updated to reflect the copy.
 *
 * UIO_READ	xio -> uio
 * UIO_WRITE	uio -> xio
 */
int
xio_uio_copy(xio_t xio, int uoffset, struct uio *uio, int *sizep)
{
    int error;
    int bytes;

    bytes = xio->xio_bytes - uoffset;
    if (bytes > uio->uio_resid)
	bytes = uio->uio_resid;
    KKASSERT(bytes >= 0);
    error = uiomove_fromphys(xio->xio_pages, xio->xio_offset + uoffset, 
				bytes, uio);
    if (error == 0)
	*sizep = bytes;
    else
	*sizep = 0;
    return(error);
}

/*
 * Copy the specified number of bytes from the xio to a userland
 * buffer.  Return an error code or 0 on success.  
 *
 * uoffset is the abstracted starting offset in the XIO, not the actual
 * offset, and usually starts at 0.
 *
 * The XIO is not modified.
 */
int
xio_copy_xtou(xio_t xio, int uoffset, void *uptr, int bytes)
{
    int i;
    int n;
    int error;
    int offset;
    vm_page_t m;
    struct sf_buf *sf;

    if (bytes > xio->xio_bytes)
	return(EFAULT);

    offset = (xio->xio_offset + uoffset) & PAGE_MASK;
    if ((n = PAGE_SIZE - offset) > bytes)
	n = bytes;

    error = 0;
    for (i = (xio->xio_offset + uoffset) >> PAGE_SHIFT; 
	 i < xio->xio_npages; 
	 ++i
    ) {
	m = xio->xio_pages[i];
	sf = sf_buf_alloc(m, SFBA_QUICK);
	error = copyout((char *)sf_buf_kva(sf) + offset, uptr, n);
	sf_buf_free(sf);
	if (error)
	    break;
	bytes -= n;
	uptr = (char *)uptr + n;
	if (bytes == 0)
	    break;
	if ((n = bytes) > PAGE_SIZE)
	    n = PAGE_SIZE;
	offset = 0;
    }
    return(error);
}

/*
 * Copy the specified number of bytes from the xio to a kernel
 * buffer.  Return an error code or 0 on success.
 *
 * uoffset is the abstracted starting offset in the XIO, not the actual
 * offset, and usually starts at 0.
 *
 * The XIO is not modified.
 */
int
xio_copy_xtok(xio_t xio, int uoffset, void *kptr, int bytes)
{
    int i;
    int n;
    int error;
    int offset;
    vm_page_t m;
    struct sf_buf *sf;

    if (bytes + uoffset > xio->xio_bytes)
	return(EFAULT);

    offset = (xio->xio_offset + uoffset) & PAGE_MASK;
    if ((n = PAGE_SIZE - offset) > bytes)
	n = bytes;

    error = 0;
    for (i = (xio->xio_offset + uoffset) >> PAGE_SHIFT; 
	 i < xio->xio_npages; 
	 ++i
    ) {
	m = xio->xio_pages[i];
	sf = sf_buf_alloc(m, SFBA_QUICK);
	bcopy((char *)sf_buf_kva(sf) + offset, kptr, n);
	sf_buf_free(sf);
	bytes -= n;
	kptr = (char *)kptr + n;
	if (bytes == 0)
	    break;
	if ((n = bytes) > PAGE_SIZE)
	    n = PAGE_SIZE;
	offset = 0;
    }
    return(error);
}

