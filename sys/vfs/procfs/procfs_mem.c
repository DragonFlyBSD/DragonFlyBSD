/*
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993 Sean Eric Fagan
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry and Sean Eric Fagan.
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
 *	@(#)procfs_mem.c	8.5 (Berkeley) 6/15/94
 *
 * $FreeBSD: src/sys/miscfs/procfs/procfs_mem.c,v 1.46.2.3 2002/01/22 17:22:59 nectar Exp $
 * $DragonFly: src/sys/vfs/procfs/procfs_mem.c,v 1.16 2007/04/29 18:25:40 dillon Exp $
 */

/*
 * This is a lightly hacked and merged version
 * of sef's pread/pwrite functions
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/vnode.h>
#include <vfs/procfs/procfs.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <sys/user.h>
#include <sys/ptrace.h>

#include <machine/vmm.h>

#include <sys/thread2.h>
#include <sys/sysref2.h>

static int	procfs_rwmem (struct proc *curp,
				  struct proc *p, struct uio *uio);

/*
 * p->p_token is held on entry.
 */
static int
procfs_rwmem(struct proc *curp, struct proc *p, struct uio *uio)
{
	int error;
	int writing;
	struct vmspace *vm;
	vm_map_t map;
	vm_offset_t pageno = 0;		/* page number */
	vm_prot_t reqprot;
	vm_offset_t kva;

	/*
	 * if the vmspace is in the midst of being allocated or deallocated,
	 * or the process is exiting, don't try to grab anything.  The
	 * page table usage in that process may be messed up.
	 */
	vm = p->p_vmspace;
	if (p->p_stat == SIDL || p->p_stat == SZOMB)
		return EFAULT;
	if ((p->p_flags & (P_WEXIT | P_INEXEC)) || vmspace_getrefs(vm) < 0)
		return EFAULT;

	/*
	 * The map we want...
	 */
	vmspace_hold(vm);
	map = &vm->vm_map;

	writing = (uio->uio_rw == UIO_WRITE);
	reqprot = VM_PROT_READ;
	if (writing)
		reqprot |= VM_PROT_WRITE | VM_PROT_OVERRIDE_WRITE;

	kva = kmem_alloc_pageable(&kernel_map, PAGE_SIZE);

	/*
	 * Only map in one page at a time.  We don't have to, but it
	 * makes things easier.  This way is trivial - right?
	 */
	do {
		vm_offset_t uva;
		vm_offset_t page_offset;	/* offset into page */
		size_t len;
		vm_page_t m;

		uva = (vm_offset_t) uio->uio_offset;

		/*
		 * Get the page number of this segment.
		 */
		pageno = trunc_page(uva);
		page_offset = uva - pageno;

		/*
		 * If the target process is running in VMM mode
		 * translate the address into a GPA (Guest Physical
		 * Address) via the EPT before doing the lookup.
		 */
		if (p->p_vmm) {
			register_t gpa;
			vmm_vm_get_gpa(p, &gpa, (register_t) pageno);
			pageno = (vm_offset_t)gpa;
		}

		/*
		 * How many bytes to copy
		 */
		len = szmin(PAGE_SIZE - page_offset, uio->uio_resid);

		/*
		 * Fault the page on behalf of the process
		 */
		m = vm_fault_page(map, pageno, reqprot,
				  VM_FAULT_NORMAL, &error);
		if (error) {
			KKASSERT(m == NULL);
			error = EFAULT;
			break;
		}

		/*
		 * Cleanup tmap then create a temporary KVA mapping and
		 * do the I/O.  We can switch between cpus so don't bother
		 * synchronizing across all cores.
		 */
		pmap_kenter_quick(kva, VM_PAGE_TO_PHYS(m));
		error = uiomove((caddr_t)(kva + page_offset), len, uio);
		pmap_kremove_quick(kva);

		/*
		 * release the page and we are done
		 */
		vm_page_unhold(m);
	} while (error == 0 && uio->uio_resid > 0);

	vmspace_drop(vm);
	kmem_free(&kernel_map, kva, PAGE_SIZE);

	return (error);
}

/*
 * Copy data in and out of the target process.
 * We do this by mapping the process's page into
 * the kernel and then doing a uiomove direct
 * from the kernel address space.
 *
 * lp->lwp_proc->p_token is held on entry.
 */
int
procfs_domem(struct proc *curp, struct lwp *lp, struct pfsnode *pfs,
	     struct uio *uio)
{
	struct proc *p = lp->lwp_proc;
	int error;

	if (uio->uio_resid == 0)
		return (0);

	if ((p->p_flags & P_INEXEC) != 0) {
		/*
		 * Can't trace a process that's currently exec'ing.
		 */
		error = EAGAIN;
	} else if (!CHECKIO(curp, p) || p_trespass(curp->p_ucred, p->p_ucred)) {
		/*
		 * Can't trace processes outside our jail
		 */
		error = EPERM;
	} else {
		error = procfs_rwmem(curp, p, uio);
	}
	return(error);
}

/*
 * Given process (p), find the vnode from which
 * its text segment is being executed.
 *
 * It would be nice to grab this information from
 * the VM system, however, there is no sure-fire
 * way of doing that.  Instead, fork(), exec() and
 * wait() all maintain the p_textvp field in the
 * process proc structure which contains a held
 * reference to the exec'ed vnode.
 *
 * XXX - Currently, this is not not used, as the
 * /proc/pid/file object exposes an information leak
 * that shouldn't happen.  Using a mount option would
 * make it configurable on a per-system (or, at least,
 * per-mount) basis; however, that's not really best.
 * The best way to do it, I think, would be as an
 * ioctl; this would restrict it to the uid running
 * program, or root, which seems a reasonable compromise.
 * However, the number of applications for this is
 * minimal, if it can't be seen in the filesytem space,
 * and doint it as an ioctl makes it somewhat less
 * useful due to the, well, inelegance.
 *
 */
struct vnode *
procfs_findtextvp(struct proc *p)
{
	return (p->p_textvp);
}
