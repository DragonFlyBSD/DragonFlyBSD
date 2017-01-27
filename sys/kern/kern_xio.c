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
 * $DragonFly: src/sys/kern/kern_xio.c,v 1.16 2008/05/09 07:24:45 dillon Exp $
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

#include <cpu/lwbuf.h>

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
 * Initialize an XIO given a kernelspace buffer.  0 is returned on success,
 * an error code on failure.  The actual number of bytes that could be
 * accomodated in the XIO will be stored in xio_bytes and the page offset
 * will be stored in xio_offset.
 *
 * WARNING! We cannot map user memory directly into an xio unless we also
 *	    make the mapping use managed pages, otherwise modifications to
 *	    the memory will race against pageouts and flushes.
 */
int
xio_init_kbuf(xio_t xio, void *kbase, size_t kbytes)
{
    vm_offset_t addr;
    vm_paddr_t paddr;
    vm_page_t m;
    int i;
    int n;

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
	m = PHYS_TO_VM_PAGE(paddr);
	vm_page_hold(m);
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
 * Initialize an XIO given an array of vm_page pointers.  The caller is
 * responsible for any modified state changes for the pages.
 */
int
xio_init_pages(xio_t xio, struct vm_page **mbase, int npages, int xflags)
{
    int i;

    KKASSERT(npages <= XIO_INTERNAL_PAGES);

    xio->xio_flags = xflags;
    xio->xio_offset = 0;
    xio->xio_bytes = npages * PAGE_SIZE;
    xio->xio_pages = xio->xio_internal_pages;
    xio->xio_npages = npages;
    xio->xio_error = 0;
    for (i = 0; i < npages; ++i) {
	vm_page_hold(mbase[i]);
	xio->xio_pages[i] = mbase[i];
    }
    return(0);
}

/*
 * Cleanup an XIO so it can be destroyed.  The pages associated with the
 * XIO are released.
 */
void
xio_release(xio_t xio)
{
    int i;
    vm_page_t m;

    for (i = 0; i < xio->xio_npages; ++i) {
	m = xio->xio_pages[i];
	if (xio->xio_flags & XIOF_WRITE)
		vm_page_dirty(m);
	vm_page_unhold(m);
    }
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
xio_uio_copy(xio_t xio, int uoffset, struct uio *uio, size_t *sizep)
{
    size_t bytes;
    int error;

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
    struct lwbuf *lwb;
    struct lwbuf lwb_cache;

    if (uoffset + bytes > xio->xio_bytes)
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
	lwb = lwbuf_alloc(m, &lwb_cache);
	error = copyout((char *)lwbuf_kva(lwb) + offset, uptr, n);
	lwbuf_free(lwb);
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
    struct lwbuf *lwb;
    struct lwbuf lwb_cache;

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
	lwb = lwbuf_alloc(m, &lwb_cache);
	bcopy((char *)lwbuf_kva(lwb) + offset, kptr, n);
	lwbuf_free(lwb);
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

/*
 * Copy the specified number of bytes from userland to the xio.
 * Return an error code or 0 on success.  
 *
 * uoffset is the abstracted starting offset in the XIO, not the actual
 * offset, and usually starts at 0.
 *
 * Data in pages backing the XIO will be modified.
 */
int
xio_copy_utox(xio_t xio, int uoffset, const void *uptr, int bytes)
{
    int i;
    int n;
    int error;
    int offset;
    vm_page_t m;
    struct lwbuf *lwb;
    struct lwbuf lwb_cache;

    if (uoffset + bytes > xio->xio_bytes)
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
	lwb = lwbuf_alloc(m, &lwb_cache);
	error = copyin(uptr, (char *)lwbuf_kva(lwb) + offset, n);
	lwbuf_free(lwb);
	if (error)
	    break;
	bytes -= n;
	uptr = (const char *)uptr + n;
	if (bytes == 0)
	    break;
	if ((n = bytes) > PAGE_SIZE)
	    n = PAGE_SIZE;
	offset = 0;
    }
    return(error);
}

/*
 * Copy the specified number of bytes from the kernel to the xio.
 * Return an error code or 0 on success.  
 *
 * uoffset is the abstracted starting offset in the XIO, not the actual
 * offset, and usually starts at 0.
 *
 * Data in pages backing the XIO will be modified.
 */
int
xio_copy_ktox(xio_t xio, int uoffset, const void *kptr, int bytes)
{
    int i;
    int n;
    int error;
    int offset;
    vm_page_t m;
    struct lwbuf *lwb;
    struct lwbuf lwb_cache;

    if (uoffset + bytes > xio->xio_bytes)
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
	lwb = lwbuf_alloc(m, &lwb_cache);
	bcopy(kptr, (char *)lwbuf_kva(lwb) + offset, n);
	lwbuf_free(lwb);
	bytes -= n;
	kptr = (const char *)kptr + n;
	if (bytes == 0)
	    break;
	if ((n = bytes) > PAGE_SIZE)
	    n = PAGE_SIZE;
	offset = 0;
    }
    return(error);
}
