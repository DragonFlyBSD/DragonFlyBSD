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
 * $DragonFly: src/sys/sys/xio.h,v 1.10 2007/08/13 17:20:05 dillon Exp $
 */

/*
 * An XIO holds a platform-agnostic page list representing a data set for
 * the purposes of I/O, mapping (SFBUF/MSFBUF), or other operations.  The
 * representation of the data set is byte aligned.  xio_offset and xio_bytes
 * specifies the precise byte-ranged block within the page list being
 * represented.
 *
 * XIOs do not track an ongoing I/O, they simply represent a block of data.
 * For this reason most XIO API functions have a 'uoffset' argument which
 * the caller may use to index within the represented dataset.  This index
 * is relative to the represented dataset, NOT to the beginning of the
 * first page.
 */
#ifndef _SYS_XIO_H_
#define	_SYS_XIO_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_UIO_H_
#include <sys/uio.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif
#ifndef _MACHINE_PARAM_H_
#include <machine/param.h>
#endif

#define XIO_INTERNAL_PAGES	btoc(MAXPHYS)
#define XIO_INTERNAL_SIZE	(XIO_INTERNAL_PAGES * PAGE_SIZE)

struct vm_page;

struct xio {
	struct vm_page **xio_pages;
	int	xio_npages;	/* number of pages in xio_pages[] array */
	int	xio_offset;	/* byte offset (may exceed a page) */
	int	xio_bytes;	/* number of bytes to transfer */
	int	xio_flags;
	int	xio_error;
	struct vm_page *xio_internal_pages[XIO_INTERNAL_PAGES];
};

typedef struct xio *xio_t;

#define XIOF_READ	0x0001
#define XIOF_WRITE	0x0002
#define XIOF_VMLINEAR	0x0004	/* must be VM object linear */

#endif

#if defined(_KERNEL)

void xio_init(xio_t xio);
int xio_init_kbuf(xio_t xio, void *kbase, size_t kbytes);
int xio_init_pages(xio_t xio, struct vm_page **mbase, int npages, int xflags);
void xio_release(xio_t xio);
int xio_uio_copy(xio_t xio, int uoffset, struct uio *uio, size_t *sizep);
int xio_copy_xtou(xio_t xio, int uoffset, void *uptr, int bytes);
int xio_copy_xtok(xio_t xio, int uoffset, void *kptr, int bytes);
int xio_copy_utox(xio_t xio, int uoffset, const void *uptr, int bytes);
int xio_copy_ktox(xio_t xio, int uoffset, const void *kptr, int bytes);

/*
 * XIOs are not modified by copy operations, the caller must track the 
 * offset itself.  This routine will return the number of bytes remaining
 * in an XIO's buffer given an offset relative to the buffer used to
 * originally construct the XIO.
 */
static __inline
int
xio_remaining(xio_t xio, int uoffset)
{
	return(xio->xio_bytes - uoffset);
}

/*
 * XIOs do not map data but if the page list WERE mapped, this routine will
 * return the actual KVA offset given a user offset relative to the original
 * buffer used to construct the XIO.
 */
static __inline
int
xio_kvaoffset(xio_t xio, int uoffset)
{
	return(xio->xio_offset + uoffset);
}

#endif /* _KERNEL */

#endif /* !_SYS_XIO_H_ */
