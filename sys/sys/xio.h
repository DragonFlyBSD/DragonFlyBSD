/*
 * Copyright (c) 2004 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/sys/xio.h,v 1.2 2004/04/09 12:51:20 joerg Exp $
 */

/*
 * The XIO structure is intended to replace UIO for messaged I/O operations
 * within the kernel.  The originator of the transaction must supply an XIO
 * structure containing a list of appropriate held vm_page's representing
 * the buffer.  The target of the transaction will generally map the
 * pages using the SF_BUF facility, complete the operation, and reply the
 * message.
 */
#ifndef _SYS_XIO_H_
#define	_SYS_XIO_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
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

#endif

#if defined(_KERNEL)

int xio_init_ubuf(xio_t xio, void *ubase, size_t ubytes, int vmprot);
int xio_init_kbuf(xio_t xio, void *kbase, size_t kbytes);
void xio_release(xio_t xio);
int xio_uio_copy(xio_t xio, struct uio *uio, int *sizep);
int xio_copy_xtou(xio_t xio, void *uptr, int bytes);
int xio_copy_xtok(xio_t xio, void *kptr, int bytes);

#endif /* _KERNEL */

#endif /* !_SYS_XIO_H_ */
