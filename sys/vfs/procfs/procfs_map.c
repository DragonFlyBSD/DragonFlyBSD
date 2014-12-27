/*
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)procfs_status.c	8.3 (Berkeley) 2/17/94
 *
 * $FreeBSD: src/sys/miscfs/procfs/procfs_map.c,v 1.24.2.1 2001/08/04 13:12:24 rwatson Exp $
 * $DragonFly: src/sys/vfs/procfs/procfs_map.c,v 1.7 2007/02/19 01:14:24 corecode Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/sbuf.h>
#include <vfs/procfs/procfs.h>

#include <vm/vm.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>

#include <machine/limits.h>

int
procfs_domap(struct proc *curp, struct lwp *lp, struct pfsnode *pfs,
	     struct uio *uio)
{
	struct proc *p = lp->lwp_proc;
	ssize_t buflen = uio->uio_offset + uio->uio_resid;
	struct vnode *vp;
	char *fullpath, *freepath;
	int error;
	vm_map_t map = &p->p_vmspace->vm_map;
	pmap_t pmap = vmspace_pmap(p->p_vmspace);
	vm_map_entry_t entry;
	struct sbuf *sb = NULL;
	unsigned int last_timestamp;

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);

	error = 0;

	if (uio->uio_offset < 0 || uio->uio_resid < 0 || buflen >= INT_MAX)
		return EINVAL;
	sb = sbuf_new (sb, NULL, buflen+1, 0);
	if (sb == NULL)
		return EIO;

	vm_map_lock_read(map);
	for (entry = map->header.next; entry != &map->header;
		entry = entry->next) {
		vm_object_t obj, tobj, lobj;
		int ref_count, shadow_count, flags;
		vm_offset_t e_start, e_end, addr;
		vm_eflags_t e_eflags;
		vm_prot_t e_prot;
		int resident, privateresident;
		char *type;

		privateresident = 0;
		switch(entry->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			obj = entry->object.vm_object;
			if (obj != NULL) {
				vm_object_hold(obj);
				if (obj->shadow_count == 1)
					privateresident = obj->resident_page_count;
			}
			break;
		case VM_MAPTYPE_UKSMAP:
			obj = NULL;
			break;
		default:
			/* ignore entry */
			continue;
		}

		e_eflags = entry->eflags;
		e_prot = entry->protection;
		e_start = entry->start;
		e_end = entry->end;

		/*
		 * Count resident pages (XXX can be horrible on 64-bit)
		 */
		resident = 0;
		addr = entry->start;
		while (addr < entry->end) {
			if (pmap_extract(pmap, addr))
				resident++;
			addr += PAGE_SIZE;
		}
		if (obj) {
			lobj = obj;
			while ((tobj = lobj->backing_object) != NULL) {
				KKASSERT(tobj != obj);
				vm_object_hold(tobj);
				if (tobj == lobj->backing_object) {
					if (lobj != obj) {
						vm_object_lock_swap();
						vm_object_drop(lobj);
					}
					lobj = tobj;
				} else {
					vm_object_drop(tobj);
				}
			}
		} else {
			lobj = NULL;
		}
		last_timestamp = map->timestamp;
		vm_map_unlock(map);

		freepath = NULL;
		fullpath = "-";
		if (lobj) {
			switch(lobj->type) {
			default:
			case OBJT_DEFAULT:
				type = "default";
				vp = NULL;
				break;
			case OBJT_VNODE:
				type = "vnode";
				vp = lobj->handle;
				vref(vp);
				break;
			case OBJT_SWAP:
				type = "swap";
				vp = NULL;
				break;
			case OBJT_DEVICE:
				type = "device";
				vp = NULL;
				break;
			case OBJT_MGTDEVICE:
				type = "mgtdevice";
				vp = NULL;
				break;
			}
			if (lobj != obj)
				vm_object_drop(lobj);
			
			flags = obj->flags;
			ref_count = obj->ref_count;
			shadow_count = obj->shadow_count;
			vm_object_drop(obj);
			if (vp != NULL) {
				vn_fullpath(p, vp, &fullpath, &freepath, 1);
				vrele(vp);
			}
		} else {
			flags = 0;
			ref_count = 0;
			shadow_count = 0;
			switch(entry->maptype) {
			case VM_MAPTYPE_UKSMAP:
				type = "uksmap";
				break;
			default:
				type = "none";
				break;
			}
		}

		/*
		 * format:
		 *  start, end, res, priv res, cow, access, type, (fullpath).
		 */
		error = sbuf_printf(sb,
#if LONG_BIT == 64
			  "0x%016lx 0x%016lx %d %d %p %s%s%s %d %d "
#else
			  "0x%08lx 0x%08lx %d %d %p %s%s%s %d %d "
#endif
			  "0x%04x %s %s %s %s\n",
			(u_long)e_start, (u_long)e_end,
			resident, privateresident, obj,
			(e_prot & VM_PROT_READ)?"r":"-",
			(e_prot & VM_PROT_WRITE)?"w":"-",
			(e_prot & VM_PROT_EXECUTE)?"x":"-",
			ref_count, shadow_count, flags,
			(e_eflags & MAP_ENTRY_COW)?"COW":"NCOW",
			(e_eflags & MAP_ENTRY_NEEDS_COPY)?"NC":"NNC",
			type, fullpath);

		if (freepath != NULL) {
			kfree(freepath, M_TEMP);
			freepath = NULL;
		}

		vm_map_lock_read(map);
		if (error == -1) {
			error = 0;
			break;
		}

		if (last_timestamp != map->timestamp) {
			vm_map_entry_t reentry;
			vm_map_lookup_entry(map, e_start, &reentry);
			entry = reentry;
		}
	}
	vm_map_unlock_read(map);
	if (sbuf_finish(sb) == 0)
		buflen = sbuf_len(sb);
	error = uiomove_frombuf(sbuf_data(sb), buflen, uio);
	sbuf_delete(sb);

	return error;
}

int
procfs_validmap(struct lwp *lp)
{
	return ((lp->lwp_proc->p_flags & P_SYSTEM) == 0);
}
