/*
 * Copyright (c) 2004,2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@backplane.com> and Matthew Dillon
 * <dillon@backplane.com>.
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
 * The MSF_BUF API is an augmentation similar to the SFBUF APIs and uses XIO
 * page array handling.  The SFBUF API originally came from:
 *
 *	Copyright (c) 2003 Alan L. Cox <alc@cs.rice.edu>.  All rights reserved.
 *	Copyright (c) 1998 David Greenman.  All rights reserved.
 *	src/sys/sys/sfbuf.h,v 1.4 2004/04/01 17:58:06 dillon
 *
 * $DragonFly: src/sys/sys/msfbuf.h,v 1.12 2006/05/21 03:43:47 dillon Exp $
 */
#ifndef _SYS_MSFBUF_H_
#define _SYS_MSFBUF_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

#ifndef _SYS_XIO_H_
#include <sys/xio.h>
#endif

#if defined(MALLOC_DECLARE)
MALLOC_DECLARE(M_MSFBUF);
#endif

/*
 * MSF_BUFs are used for caching ephermal mappings that span more than
 * one page.
 *
 * The interface returns an msf_buf data structure which has information
 * about managing the ephermal mapping, its KVA pointer and an embedded
 * XIO structure which describes the mapping.
 *
 * The embedded XIO structure be passed around to the DEV system because
 * it is ref-counted; thus making it perfectly usable by anything that
 * can accept an XIO as a transfer unit, most notably the buffer-cache
 * and the XIO API.
 *
 */

/*
 * Type of mapping.
 *
 * MSF_TYPE_PGLIST - mapping based on raw list of pages.
 * MSF_TYPE_XIO    - mapping based on an XIO.
 * MSF_TYPE_UBUF   - mapping based on an arbitrary user buffer.
 * MSF_TYPE_KBUF   - mapping based on an arbitrary kernel buffer.
 */
enum msf_type { MSF_TYPE_UNKNOWN, MSF_TYPE_PGLIST, MSF_TYPE_XIO,
				MSF_TYPE_UBUF, MSF_TYPE_KBUF };

struct msf_buf {
	TAILQ_ENTRY(msf_buf) free_list;	/* free list of buffers */
	vm_offset_t 	ms_kva;			/* KVA offset */
	cpumask_t    	ms_cpumask;		/* CPU mask for synchronization */
	struct xio		*ms_xio;		/* xio being used */
	struct xio  	ms_internal_xio;/* xio embedded */
	int         	ms_refcnt;		/* map usage tracking */
	int          	ms_flags;		/* control flags */
	enum msf_type   ms_type;		/* type of mapped data  */ 
};

/* Flags. */
#define	MSF_ONFREEQ 	0x0001	/* currently on the freelist */
#define	MSF_CATCH   	0x0004	/* allow interruption */
#define	MSF_CPUPRIVATE	0x0008	/* sync mapping to current cpu only */

typedef struct msf_buf	*msf_t;

#if defined(_KERNEL)

/*
 * Return a KVA offset to the client
 */
static __inline
char *
msf_buf_kva(struct msf_buf *msf)
{
	return ((char *)msf->ms_kva + msf->ms_xio->xio_offset);
}

static __inline
int
msf_buf_bytes(struct msf_buf *msf)
{
	return (msf->ms_xio->xio_bytes);
}

/*
 * Return a reference to the underlying pages of an MSF_BUF
 */
static __inline
struct vm_page **
msf_buf_pages(struct msf_buf *msf)
{
	return (msf->ms_xio->xio_pages);
}

/* API function prototypes */
int msf_map_pagelist(struct msf_buf **, struct vm_page **, int, int);
int msf_map_xio(struct msf_buf **, struct xio *, int);
int msf_map_ubuf(struct msf_buf **, void *, size_t, int);
int msf_map_kbuf(struct msf_buf **, void *, size_t, int);
int msf_uio_iterate(struct uio *uio,
                int (*callback)(void *info, char *buf, int bytes), void *info);
void	msf_buf_free(struct msf_buf *);
void	msf_buf_ref(struct msf_buf *);

#endif	/* _KERNEL */
#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* _SYS_MSFBUF_H_ */
