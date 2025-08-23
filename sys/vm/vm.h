/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2003-2019 The DragonFly Project.  All rights reserved.
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
 *	@(#)vm.h	8.2 (Berkeley) 12/13/93
 *	@(#)vm_prot.h	8.1 (Berkeley) 6/11/93
 *	@(#)vm_inherit.h	8.1 (Berkeley) 6/11/93
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
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
 * $FreeBSD: src/sys/vm/vm.h,v 1.16.2.1 2002/12/28 19:49:41 dillon Exp $
 */

#ifndef _VM_VM_H_
#define _VM_VM_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#include <machine/pat.h>

typedef char vm_inherit_t;	/* inheritance codes */

#define	VM_INHERIT_SHARE	((vm_inherit_t) 0)
#define	VM_INHERIT_COPY		((vm_inherit_t) 1)
#define	VM_INHERIT_NONE		((vm_inherit_t) 2)
#define	VM_INHERIT_DEFAULT	VM_INHERIT_COPY

typedef u_char vm_prot_t;	/* protection codes */

#define	VM_PROT_NONE		((vm_prot_t) 0x00)
#define	VM_PROT_READ		((vm_prot_t) 0x01)
#define	VM_PROT_WRITE		((vm_prot_t) 0x02)
#define	VM_PROT_EXECUTE		((vm_prot_t) 0x04)
#define	VM_PROT_OVERRIDE_WRITE	((vm_prot_t) 0x08)	/* copy-on-write */
#define	VM_PROT_NOSYNC		((vm_prot_t) 0x10)

#define	VM_PROT_RW		(VM_PROT_READ|VM_PROT_WRITE)
#define	VM_PROT_ALL		(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)
#define	VM_PROT_DEFAULT		VM_PROT_ALL

typedef u_char vm_maptype_t;	/* type of vm_map_entry */

/*
 * NOTE: UKSMAPs are unmanaged.  The underlying kernel memory must not be
 *	 freed until all related mappings are gone.  There is no object.
 *	 The device can map different things for the same UKS mapping even
 *	 when inherited via fork().
 */
#define VM_MAPTYPE_UNSPECIFIED	0
#define VM_MAPTYPE_NORMAL	1
#define VM_MAPTYPE_UNUSED02	2	/* was VPAGETABLE */
#define VM_MAPTYPE_SUBMAP	3
#define VM_MAPTYPE_UKSMAP	4	/* user-kernel shared memory */

struct vm_map_entry;
struct vm_map;
struct vm_object;
typedef struct vm_object *vm_object_t;

/*
 * This is also defined in vm/vm_page.h.
 */
#ifndef __VM_PAGE_T_DEFINED__
#define __VM_PAGE_T_DEFINED__
struct vm_page;
typedef struct vm_page *vm_page_t;
#endif


/* Memory attributes. */

#ifndef __VM_MEMATTR_T_DEFINED__
#define __VM_MEMATTR_T_DEFINED__
typedef char vm_memattr_t;
#endif

#define VM_MEMATTR_UNCACHEABLE		((vm_memattr_t)PAT_UNCACHEABLE)
#define VM_MEMATTR_WRITE_COMBINING	((vm_memattr_t)PAT_WRITE_COMBINING)
#define VM_MEMATTR_WRITE_THROUGH	((vm_memattr_t)PAT_WRITE_THROUGH)
#define VM_MEMATTR_WRITE_PROTECTED	((vm_memattr_t)PAT_WRITE_PROTECTED)
#define VM_MEMATTR_WRITE_BACK		((vm_memattr_t)PAT_WRITE_BACK)
#define VM_MEMATTR_WEAK_UNCACHEABLE	((vm_memattr_t)PAT_UNCACHED)

#define VM_MEMATTR_DEFAULT	VM_MEMATTR_WRITE_BACK

#endif				/* _VM_VM_H_ */
