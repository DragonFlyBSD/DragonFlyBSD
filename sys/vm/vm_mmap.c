/*
 * (MPSAFE)
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: vm_mmap.c 1.6 91/10/21$
 *
 *	@(#)vm_mmap.c	8.4 (Berkeley) 1/12/94
 * $FreeBSD: src/sys/vm/vm_mmap.c,v 1.108.2.6 2002/07/02 20:06:19 dillon Exp $
 */

/*
 * Mapped file (mmap) interface to VM
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysmsg.h>
#include <sys/filedesc.h>
#include <sys/kern_syscall.h>
#include <sys/proc.h>
#include <sys/caps.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/vmmeter.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_pageout.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#include <sys/file2.h>
#include <sys/thread.h>
#include <vm/vm_page2.h>

static int max_proc_mmap = 1000000;
SYSCTL_INT(_vm, OID_AUTO, max_proc_mmap, CTLFLAG_RW, &max_proc_mmap, 0, "");
int vkernel_enable;
SYSCTL_INT(_vm, OID_AUTO, vkernel_enable, CTLFLAG_RW, &vkernel_enable, 0, "");

/*
 * sstk_args(int incr)
 *
 * MPSAFE
 */
int
sys_sstk(struct sysmsg *sysmsg, const struct sstk_args *uap)
{
	/* Not yet implemented */
	return (EOPNOTSUPP);
}

/* 
 * mmap_args(void *addr, size_t len, int prot, int flags, int fd,
 *		long pad, off_t pos)
 *
 * Memory Map (mmap) system call.  Note that the file offset
 * and address are allowed to be NOT page aligned, though if
 * the MAP_FIXED flag it set, both must have the same remainder
 * modulo the PAGE_SIZE (POSIX 1003.1b).  If the address is not
 * page-aligned, the actual mapping starts at trunc_page(addr)
 * and the return value is adjusted up by the page offset.
 *
 * Generally speaking, only character devices which are themselves
 * memory-based, such as a video framebuffer, can be mmap'd.  Otherwise
 * there would be no cache coherency between a descriptor and a VM mapping
 * both to the same character device.
 *
 * Block devices can be mmap'd no matter what they represent.  Cache coherency
 * is maintained as long as you do not write directly to the underlying
 * character device.
 *
 * No requirements
 */
int
kern_mmap(struct vmspace *vms, caddr_t uaddr, size_t ulen,
	  int uprot, int uflags, int fd, off_t upos, void **res)
{
	struct thread *td = curthread;
 	struct proc *p = td->td_proc;
	struct file *fp = NULL;
	struct vnode *vp;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	vm_prot_t prot, maxprot;
	void *handle;
	int flags, error;
	off_t pos;
	vm_object_t obj;

	KKASSERT(p);

	addr = (vm_offset_t) uaddr;
	size = ulen;
	prot = uprot & VM_PROT_ALL;
	flags = uflags;
	pos = upos;

	/*
	 * Make sure mapping fits into numeric range etc.
	 *
	 * NOTE: We support the full unsigned range for size now.
	 */
	if (((flags & MAP_ANON) && (fd != -1 || pos != 0)))
		return (EINVAL);

	if (size == 0)
		return (EINVAL);

	if (flags & MAP_STACK) {
		if (fd != -1)
			return (EINVAL);
		if ((prot & (PROT_READ|PROT_WRITE)) != (PROT_READ|PROT_WRITE))
			return (EINVAL);
		flags |= MAP_ANON;
		pos = 0;
	}

	/*
	 * Virtual page table support has been removed and now always
	 * returns EOPNOTSUPP.
	 */
	if (flags & MAP_VPAGETABLE)
		return (EOPNOTSUPP);

	/*
	 * Align the file position to a page boundary,
	 * and save its page offset component.
	 */
	pageoff = (pos & PAGE_MASK);
	pos -= pageoff;

	/* Adjust size for rounding (on both ends). */
	size += pageoff;			/* low end... */
	size = (vm_size_t) round_page(size);	/* hi end */
	if (size < ulen)			/* wrap */
		return(EINVAL);

	/*
	 * Check for illegal addresses.  Watch out for address wrap... Note
	 * that VM_*_ADDRESS are not constants due to casts (argh).
	 */
	if (flags & (MAP_FIXED | MAP_TRYFIXED)) {
		/*
		 * The specified address must have the same remainder
		 * as the file offset taken modulo PAGE_SIZE, so it
		 * should be aligned after adjustment by pageoff.
		 */
		addr -= pageoff;
		if (addr & PAGE_MASK)
			return (EINVAL);

		/*
		 * Address range must be all in user VM space and not wrap.
		 */
		tmpaddr = addr + size;
		if (tmpaddr < addr)
			return (EINVAL);
		if (VM_MAX_USER_ADDRESS > 0 && tmpaddr > VM_MAX_USER_ADDRESS)
			return (EINVAL);
		if (VM_MIN_USER_ADDRESS > 0 && addr < VM_MIN_USER_ADDRESS)
			return (EINVAL);
	} else {
		/*
		 * Get a hint of where to map. It also provides mmap offset
		 * randomization if enabled.
		 */
		addr = vm_map_hint(p, addr, prot, flags);
	}

	if (flags & MAP_ANON) {
		/*
		 * Mapping blank space is trivial.
		 */
		handle = NULL;
		maxprot = VM_PROT_ALL;
	} else {
		/*
		 * Mapping file, get fp for validation. Obtain vnode and make
		 * sure it is of appropriate type.
		 */
		fp = holdfp(td, fd, -1);
		if (fp == NULL)
			return (EBADF);
		if (fp->f_type != DTYPE_VNODE) {
			error = EINVAL;
			if (fp->f_type == DTYPE_DMABUF) {
				kprintf("dmabuf-mmap-attempt\n");
			}
			goto done;
		}
		/*
		 * POSIX shared-memory objects are defined to have
		 * kernel persistence, and are not defined to support
		 * read(2)/write(2) -- or even open(2).  Thus, we can
		 * use MAP_ASYNC to trade on-disk coherence for speed.
		 * The shm_open(3) library routine turns on the FPOSIXSHM
		 * flag to request this behavior.
		 */
		if (fp->f_flag & FPOSIXSHM)
			flags |= MAP_NOSYNC;
		vp = (struct vnode *) fp->f_data;

		/*
		 * Validate the vnode for the operation.
		 */
		switch(vp->v_type) {
		case VREG:
			/*
			 * Get the proper underlying object
			 */
			if ((obj = vp->v_object) == NULL) {
				error = EINVAL;
				goto done;
			}
			KKASSERT((struct vnode *)obj->handle == vp);
			break;
		case VCHR:
			/*
			 * Make sure a device has not been revoked.  
			 * Mappability is handled by the device layer.
			 */
			if (vp->v_rdev == NULL) {
				error = EBADF;
				goto done;
			}
			break;
		default:
			/*
			 * Nothing else is mappable.
			 */
			error = EINVAL;
			goto done;
		}

		/*
		 * XXX hack to handle use of /dev/zero to map anon memory (ala
		 * SunOS).
		 */
		if (vp->v_type == VCHR && iszerodev(vp->v_rdev)) {
			handle = NULL;
			maxprot = VM_PROT_ALL;
			flags |= MAP_ANON;
			pos = 0;
		} else {
			/*
			 * cdevs does not provide private mappings of any kind.
			 */
			if (vp->v_type == VCHR &&
			    (flags & (MAP_PRIVATE|MAP_COPY))) {
				error = EINVAL;
				goto done;
			}
			/*
			 * Ensure that file and memory protections are
			 * compatible.  Note that we only worry about
			 * writability if mapping is shared; in this case,
			 * current and max prot are dictated by the open file.
			 * XXX use the vnode instead?  Problem is: what
			 * credentials do we use for determination? What if
			 * proc does a setuid?
			 */
			maxprot = VM_PROT_EXECUTE;
			if (fp->f_flag & FREAD) {
				maxprot |= VM_PROT_READ;
			} else if (prot & PROT_READ) {
				error = EACCES;
				goto done;
			}
			/*
			 * If we are sharing potential changes (either via
			 * MAP_SHARED or via the implicit sharing of character
			 * device mappings), and we are trying to get write
			 * permission although we opened it without asking
			 * for it, bail out.  Check for superuser, only if
			 * we're at securelevel < 1, to allow the XIG X server
			 * to continue to work.
			 *
			 * PROT_WRITE + MAP_SHARED
			 */
			if ((flags & MAP_SHARED) != 0 || vp->v_type == VCHR) {
				if ((fp->f_flag & FWRITE) != 0) {
					struct vattr va;
					if ((error = VOP_GETATTR(vp, &va))) {
						goto done;
					}
					if ((va.va_flags &
					    (IMMUTABLE|APPEND)) == 0) {
						maxprot |= VM_PROT_WRITE;

						/*
						 * SHARED+RW regular file mmap()
						 * updates v_lastwrite_ts.
						 */
						if ((prot & PROT_WRITE) &&
						    vp->v_type == VREG &&
						    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY) == 0) {
							vfs_timestamp(&vp->v_lastwrite_ts);
							vsetflags(vp, VLASTWRITETS);
							vn_unlock(vp);
						}
					} else if (prot & PROT_WRITE) {
						error = EPERM;
						goto done;
					}
				} else if ((prot & PROT_WRITE) != 0) {
					error = EACCES;
					goto done;
				}
			} else {
				maxprot |= VM_PROT_WRITE;
			}
			handle = (void *)vp;
		}
	}

	lwkt_gettoken(&vms->vm_map.token);

	/*
	 * Do not allow more then a certain number of vm_map_entry structures
	 * per process.  0 to disable.
	 */
	if (max_proc_mmap && vms->vm_map.nentries >= max_proc_mmap) {
		error = ENOMEM;
		lwkt_reltoken(&vms->vm_map.token);
		goto done;
	}

	error = vm_mmap(&vms->vm_map, &addr, size, prot, maxprot,
			flags, handle, pos, fp);
	if (error == 0)
		*res = (void *)(addr + pageoff);

	lwkt_reltoken(&vms->vm_map.token);
done:
	if (fp)
		dropfp(td, fd, fp);

	return (error);
}

/*
 * mmap system call handler
 *
 * No requirements.
 */
int
sys_mmap(struct sysmsg *sysmsg, const struct mmap_args *uap)
{
	int error;
	int flags = uap->flags;
	off_t upos = uap->pos;

	/*
	 * Work around fairly serious problems with trying to have an
	 * auto-grow stack segment related to other unrelated calls to
	 * mmap() potentially getting addresses within such segments.
	 *
	 * Our attempt to use TRYFIXED to mediate the problem basically
	 * failed.  For example, rtld-elf uses it to try to optimize
	 * shlib placement, but could run afoul of this issue.
	 *
	 * The only remaining true MAP_STACK we allow is the user stack as
	 * created by the exec code.  All userland MAP_STACK's are converted
	 * to normal mmap()s right here.
	 */
	if (flags & MAP_STACK) {
		if (uap->fd != -1)
			return (EINVAL);
		if ((uap->prot & (PROT_READ|PROT_WRITE)) !=
		    (PROT_READ|PROT_WRITE)) {
			return (EINVAL);
		}
		flags &= ~MAP_STACK;
		flags |= MAP_ANON;
		upos = 0;
	}

	error = kern_mmap(curproc->p_vmspace, uap->addr, uap->len,
			  uap->prot, flags,
			  uap->fd, upos, &sysmsg->sysmsg_resultp);

	return (error);
}

/*
 * msync system call handler 
 *
 * msync_args(void *addr, size_t len, int flags)
 *
 * No requirements
 */
int
sys_msync(struct sysmsg *sysmsg, const struct msync_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	int flags;
	vm_map_t map;
	int rv;

	addr = (vm_offset_t) uap->addr;
	size = uap->len;
	flags = uap->flags;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t) round_page(size);
	if (size < uap->len)		/* wrap */
		return(EINVAL);
	tmpaddr = addr + size;		/* workaround gcc4 opt */
	if (tmpaddr < addr)		/* wrap */
		return(EINVAL);

	if ((flags & (MS_ASYNC|MS_INVALIDATE)) == (MS_ASYNC|MS_INVALIDATE))
		return (EINVAL);

	map = &p->p_vmspace->vm_map;

	/*
	 * map->token serializes extracting the address range for size == 0
	 * msyncs with the vm_map_clean call; if the token were not held
	 * across the two calls, an intervening munmap/mmap pair, for example,
	 * could cause msync to occur on a wrong region.
	 */
	lwkt_gettoken(&map->token);

	/*
	 * XXX Gak!  If size is zero we are supposed to sync "all modified
	 * pages with the region containing addr".  Unfortunately, we don't
	 * really keep track of individual mmaps so we approximate by flushing
	 * the range of the map entry containing addr. This can be incorrect
	 * if the region splits or is coalesced with a neighbor.
	 */
	if (size == 0) {
		vm_map_entry_t entry;

		vm_map_lock_read(map);
		rv = vm_map_lookup_entry(map, addr, &entry);
		if (rv == FALSE) {
			vm_map_unlock_read(map);
			rv = KERN_INVALID_ADDRESS;
			goto done;
		}
		addr = entry->ba.start;
		size = entry->ba.end - entry->ba.start;
		vm_map_unlock_read(map);
	}

	/*
	 * Clean the pages and interpret the return value.
	 */
	rv = vm_map_clean(map, addr, addr + size, (flags & MS_ASYNC) == 0,
			  (flags & MS_INVALIDATE) != 0);
done:
	lwkt_reltoken(&map->token);

	switch (rv) {
	case KERN_SUCCESS:
		break;
	case KERN_INVALID_ADDRESS:
		return (EINVAL);	/* Sun returns ENOMEM? */
	case KERN_FAILURE:
		return (EIO);
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * munmap system call handler
 *
 * munmap_args(void *addr, size_t len)
 *
 * No requirements
 */
int
sys_munmap(struct sysmsg *sysmsg, const struct munmap_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	vm_map_t map;

	addr = (vm_offset_t) uap->addr;
	size = uap->len;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t) round_page(size);
	if (size < uap->len)		/* wrap */
		return(EINVAL);
	tmpaddr = addr + size;		/* workaround gcc4 opt */
	if (tmpaddr < addr)		/* wrap */
		return(EINVAL);

	if (size == 0)
		return (0);

	/*
	 * Check for illegal addresses.  Watch out for address wrap... Note
	 * that VM_*_ADDRESS are not constants due to casts (argh).
	 */
	if (VM_MAX_USER_ADDRESS > 0 && tmpaddr > VM_MAX_USER_ADDRESS)
		return (EINVAL);
	if (VM_MIN_USER_ADDRESS > 0 && addr < VM_MIN_USER_ADDRESS)
		return (EINVAL);

	map = &p->p_vmspace->vm_map;

	/* map->token serializes between the map check and the actual unmap */
	lwkt_gettoken(&map->token);

	/*
	 * Make sure entire range is allocated.
	 */
	if (!vm_map_check_protection(map, addr, addr + size,
				     VM_PROT_NONE, FALSE)) {
		lwkt_reltoken(&map->token);
		return (EINVAL);
	}
	/* returns nothing but KERN_SUCCESS anyway */
	vm_map_remove(map, addr, addr + size);
	lwkt_reltoken(&map->token);
	return (0);
}

/*
 * mprotect_args(const void *addr, size_t len, int prot)
 *
 * No requirements.
 */
int
sys_mprotect(struct sysmsg *sysmsg, const struct mprotect_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	vm_prot_t prot;
	int error;

	addr = (vm_offset_t) uap->addr;
	size = uap->len;
	prot = uap->prot & VM_PROT_ALL;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t) round_page(size);
	if (size < uap->len)		/* wrap */
		return(EINVAL);
	tmpaddr = addr + size;		/* workaround gcc4 opt */
	if (tmpaddr < addr)		/* wrap */
		return(EINVAL);

	switch (vm_map_protect(&p->p_vmspace->vm_map, addr, addr + size,
			       prot, FALSE)) {
	case KERN_SUCCESS:
		error = 0;
		break;
	case KERN_PROTECTION_FAILURE:
		error = EACCES;
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * minherit system call handler
 *
 * minherit_args(void *addr, size_t len, int inherit)
 *
 * No requirements.
 */
int
sys_minherit(struct sysmsg *sysmsg, const struct minherit_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	vm_inherit_t inherit;
	int error;

	addr = (vm_offset_t)uap->addr;
	size = uap->len;
	inherit = uap->inherit;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t) round_page(size);
	if (size < uap->len)		/* wrap */
		return(EINVAL);
	tmpaddr = addr + size;		/* workaround gcc4 opt */
	if (tmpaddr < addr)		/* wrap */
		return(EINVAL);

	switch (vm_map_inherit(&p->p_vmspace->vm_map, addr,
			       addr + size, inherit)) {
	case KERN_SUCCESS:
		error = 0;
		break;
	case KERN_PROTECTION_FAILURE:
		error = EACCES;
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * madvise system call handler
 * 
 * madvise_args(void *addr, size_t len, int behav)
 *
 * No requirements.
 */
int
sys_madvise(struct sysmsg *sysmsg, const struct madvise_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t start, end;
	vm_offset_t tmpaddr = (vm_offset_t)uap->addr + uap->len;
	int error;

	/*
	 * Check for illegal behavior
	 */
	if (uap->behav < 0 || uap->behav >= MADV_CONTROL_END)
		return (EINVAL);
	/*
	 * Check for illegal addresses.  Watch out for address wrap... Note
	 * that VM_*_ADDRESS are not constants due to casts (argh).
	 */
	if (tmpaddr < (vm_offset_t)uap->addr)
		return (EINVAL);
	if (VM_MAX_USER_ADDRESS > 0 && tmpaddr > VM_MAX_USER_ADDRESS)
		return (EINVAL);
	if (VM_MIN_USER_ADDRESS > 0 && uap->addr < VM_MIN_USER_ADDRESS)
		return (EINVAL);

	/*
	 * Since this routine is only advisory, we default to conservative
	 * behavior.
	 */
	start = trunc_page((vm_offset_t)uap->addr);
	end = round_page(tmpaddr);

	error = vm_map_madvise(&p->p_vmspace->vm_map, start, end,
			       uap->behav, 0);
	return (error);
}

/*
 * mcontrol system call handler
 *
 * mcontrol_args(void *addr, size_t len, int behav, off_t value)
 *
 * No requirements
 */
int
sys_mcontrol(struct sysmsg *sysmsg, const struct mcontrol_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t start, end;
	vm_offset_t tmpaddr = (vm_offset_t)uap->addr + uap->len;
	int error;

	/*
	 * Check for illegal behavior
	 */
	if (uap->behav < 0 || uap->behav > MADV_CONTROL_END)
		return (EINVAL);
	/*
	 * Check for illegal addresses.  Watch out for address wrap... Note
	 * that VM_*_ADDRESS are not constants due to casts (argh).
	 */
	if (tmpaddr < (vm_offset_t) uap->addr)
		return (EINVAL);
	if (VM_MAX_USER_ADDRESS > 0 && tmpaddr > VM_MAX_USER_ADDRESS)
		return (EINVAL);
	if (VM_MIN_USER_ADDRESS > 0 && uap->addr < VM_MIN_USER_ADDRESS)
		return (EINVAL);

	/*
	 * Since this routine is only advisory, we default to conservative
	 * behavior.
	 */
	start = trunc_page((vm_offset_t)uap->addr);
	end = round_page(tmpaddr);
	
	error = vm_map_madvise(&p->p_vmspace->vm_map, start, end,
			       uap->behav, uap->value);
	return (error);
}


/*
 * mincore system call handler
 *
 * mincore_args(const void *addr, size_t len, char *vec)
 *
 * No requirements
 */
int
sys_mincore(struct sysmsg *sysmsg, const struct mincore_args *uap)
{
	struct proc *p = curproc;
	vm_offset_t addr, first_addr;
	vm_offset_t end, cend;
	pmap_t pmap;
	vm_map_t map;
	char *vec;
	int error;
	int vecindex, lastvecindex;
	vm_map_entry_t current;
	vm_map_entry_t entry;
	int mincoreinfo;
	unsigned int timestamp;

	/*
	 * Make sure that the addresses presented are valid for user
	 * mode.
	 */
	first_addr = addr = trunc_page((vm_offset_t) uap->addr);
	end = addr + (vm_size_t)round_page(uap->len);
	if (end < addr)
		return (EINVAL);
	if (VM_MAX_USER_ADDRESS > 0 && end > VM_MAX_USER_ADDRESS)
		return (EINVAL);

	/*
	 * Address of byte vector
	 */
	vec = uap->vec;

	map = &p->p_vmspace->vm_map;
	pmap = vmspace_pmap(p->p_vmspace);

	lwkt_gettoken(&map->token);
	vm_map_lock_read(map);
RestartScan:
	timestamp = map->timestamp;

	if (!vm_map_lookup_entry(map, addr, &entry))
		entry = RB_MIN(vm_map_rb_tree, &map->rb_root);

	/*
	 * Do this on a map entry basis so that if the pages are not
	 * in the current processes address space, we can easily look
	 * up the pages elsewhere.
	 */
	lastvecindex = -1;
	for (current = entry;
	     current && current->ba.start < end;
	     current = vm_map_rb_tree_RB_NEXT(current)) {
		/*
		 * ignore submaps (for now) or null objects
		 */
		if (current->maptype != VM_MAPTYPE_NORMAL)
			continue;
		if (current->ba.object == NULL)
			continue;
		
		/*
		 * limit this scan to the current map entry and the
		 * limits for the mincore call
		 */
		if (addr < current->ba.start)
			addr = current->ba.start;
		cend = current->ba.end;
		if (cend > end)
			cend = end;

		/*
		 * scan this entry one page at a time
		 */
		while (addr < cend) {
			/*
			 * Check pmap first, it is likely faster, also
			 * it can provide info as to whether we are the
			 * one referencing or modifying the page.
			 *
			 * If we have to check the VM object, only mess
			 * around with normal maps.  Do not mess around
			 * with virtual page tables (XXX).
			 */
			mincoreinfo = pmap_mincore(pmap, addr);
			if (mincoreinfo == 0 &&
			    current->maptype == VM_MAPTYPE_NORMAL) {
				vm_pindex_t pindex;
				vm_ooffset_t offset;
				vm_page_t m;

				/*
				 * calculate the page index into the object
				 */
				offset = current->ba.offset +
					 (addr - current->ba.start);
				pindex = OFF_TO_IDX(offset);

				/*
				 * if the page is resident, then gather
				 * information about it.  spl protection is
				 * required to maintain the object
				 * association.  And XXX what if the page is
				 * busy?  What's the deal with that?
				 *
				 * XXX vm_token - legacy for pmap_ts_referenced
				 *     in x86 and vkernel pmap code.
				 */
				lwkt_gettoken(&vm_token);
				vm_object_hold(current->ba.object);
				m = vm_page_lookup(current->ba.object, pindex);
				if (m && m->valid) {
					mincoreinfo = MINCORE_INCORE;
					if (m->dirty || pmap_is_modified(m))
						mincoreinfo |= MINCORE_MODIFIED_OTHER;
					if ((m->flags & PG_REFERENCED) ||
						pmap_ts_referenced(m)) {
						vm_page_flag_set(m, PG_REFERENCED);
						mincoreinfo |= MINCORE_REFERENCED_OTHER;
					}
				}
				vm_object_drop(current->ba.object);
				lwkt_reltoken(&vm_token);
			}

			/*
			 * subyte may page fault.  In case it needs to modify
			 * the map, we release the lock.
			 */
			vm_map_unlock_read(map);

			/*
			 * calculate index into user supplied byte vector
			 */
			vecindex = OFF_TO_IDX(addr - first_addr);

			/*
			 * If we have skipped map entries, we need to make sure that
			 * the byte vector is zeroed for those skipped entries.
			 */
			while((lastvecindex + 1) < vecindex) {
				error = subyte( vec + lastvecindex, 0);
				if (error) {
					error = EFAULT;
					goto done;
				}
				++lastvecindex;
			}

			/*
			 * Pass the page information to the user
			 */
			error = subyte(vec + vecindex, mincoreinfo);
			if (error) {
				error = EFAULT;
				goto done;
			}

			/*
			 * If the map has changed, due to the subyte,
			 * the previous output may be invalid.
			 */
			vm_map_lock_read(map);
			if (timestamp != map->timestamp)
				goto RestartScan;

			lastvecindex = vecindex;
			addr += PAGE_SIZE;
		}
	}

	/*
	 * subyte may page fault.  In case it needs to modify
	 * the map, we release the lock.
	 */
	vm_map_unlock_read(map);

	/*
	 * Zero the last entries in the byte vector.
	 */
	vecindex = OFF_TO_IDX(end - first_addr);
	while((lastvecindex + 1) < vecindex) {
		error = subyte( vec + lastvecindex, 0);
		if (error) {
			error = EFAULT;
			goto done;
		}
		++lastvecindex;
	}
	
	/*
	 * If the map has changed, due to the subyte, the previous
	 * output may be invalid.
	 */
	vm_map_lock_read(map);
	if (timestamp != map->timestamp)
		goto RestartScan;
	vm_map_unlock_read(map);

	error = 0;
done:
	lwkt_reltoken(&map->token);
	return (error);
}

/*
 * mlock system call handler
 *
 * mlock_args(const void *addr, size_t len)
 *
 * No requirements
 */
int
sys_mlock(struct sysmsg *sysmsg, const struct mlock_args *uap)
{
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;

	addr = (vm_offset_t) uap->addr;
	size = uap->len;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t) round_page(size);
	if (size < uap->len)		/* wrap */
		return (EINVAL);
	if (size == 0)			/* silently allow 0 size */
		return (0);
	tmpaddr = addr + size;		/* workaround gcc4 opt */
	if (tmpaddr < addr)		/* wrap */
		return (EINVAL);

	if (atop(size) + vmstats.v_wire_count > vm_page_max_wired)
		return (EAGAIN);

	/* 
	 * We do not need to synchronize against other threads updating ucred;
	 * they update p->ucred, which is synchronized into td_ucred ourselves.
	 */
#ifdef pmap_wired_count
	if (size + ptoa(pmap_wired_count(vm_map_pmap(&p->p_vmspace->vm_map))) >
	    p->p_rlimit[RLIMIT_MEMLOCK].rlim_cur) {
		return (ENOMEM);
	}
#else
	error = caps_priv_check_td(td, SYSCAP_RESTRICTEDROOT);
	if (error) {
		return (error);
	}
#endif
	/* wire the pages */
	error = vm_map_user_wiring(&p->p_vmspace->vm_map,
				   addr, addr + size, FALSE);
	return (error == KERN_SUCCESS ? 0 : ENOMEM);
}

/*
 * mlockall(int how)
 *
 * No requirements
 */
int
sys_mlockall(struct sysmsg *sysmsg, const struct mlockall_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;
	int how = uap->how;
	int rc = KERN_SUCCESS;

	if (((how & MCL_CURRENT) == 0) && ((how & MCL_FUTURE) == 0))
		return (EINVAL);

	rc = caps_priv_check_td(td, SYSCAP_RESTRICTEDROOT);
	if (rc) 
		return (rc);

	vm_map_lock(map);
	do {
		if (how & MCL_CURRENT) {
			RB_FOREACH(entry, vm_map_rb_tree, &map->rb_root) {
				/* Only writeable VM_MAPTYPE_NORMAL entries handled */
				if ((entry->eflags & MAP_ENTRY_USER_WIRED) ||
					entry->maptype != VM_MAPTYPE_NORMAL ||
					(entry->max_protection & VM_PROT_WRITE) == 0) {
					continue;
				}

				if (entry->wired_count != 0) {
					entry->wired_count++;
					entry->eflags |= MAP_ENTRY_USER_WIRED;
					continue;
				}

				entry->wired_count++;
				rc = vm_fault_wire(map, entry, TRUE, 0);
				if (rc)
					goto done;
				entry->eflags |= MAP_ENTRY_USER_WIRED;
			}
		}
		if (how & MCL_FUTURE)
			map->flags |= MAP_WIREFUTURE;
	} while(0);

done:
	RB_FOREACH(entry, vm_map_rb_tree, &map->rb_root) {
		if ((entry->eflags & MAP_ENTRY_USER_WIRED) == 0)
			continue;

		entry->eflags &= ~MAP_ENTRY_USER_WIRED;
		entry->wired_count--;
		if (entry->wired_count == 0)
			vm_fault_unwire(map, entry);
	}

	vm_map_unlock(map);

	return (rc);
}

/*
 * munlockall(void)
 *
 *	Unwire all user-wired map entries, cancel MCL_FUTURE.
 *
 * No requirements
 */
int
sys_munlockall(struct sysmsg *sysmsg, const struct munlockall_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;
	int rc = KERN_SUCCESS;

	vm_map_lock(map);

	/* Clear MAP_WIREFUTURE to cancel mlockall(MCL_FUTURE) */
	map->flags &= ~MAP_WIREFUTURE;

retry:
	RB_FOREACH(entry, vm_map_rb_tree, &map->rb_root) {
		if ((entry->eflags & MAP_ENTRY_USER_WIRED) == 0)
			continue;

		/*
		 * If we encounter an in-transition entry, we release the 
		 * map lock and retry the scan; we do not decrement any
		 * wired_count more than once because we do not touch
		 * any entries with MAP_ENTRY_USER_WIRED not set.
		 *
 		 * There is a potential interleaving with concurrent
		 * mlockall()s here -- if we abort a scan, an mlockall()
		 * could start, wire a number of entries before our 
		 * current position in, and then stall itself on this
		 * or any other in-transition entry. If that occurs, when
		 * we resume, we will unwire those entries. 
 		 */
		if (entry->eflags & MAP_ENTRY_IN_TRANSITION) {
			entry->eflags |= MAP_ENTRY_NEEDS_WAKEUP;
			++mycpu->gd_cnt.v_intrans_coll;
			++mycpu->gd_cnt.v_intrans_wait;
			vm_map_transition_wait(map, 1);
			goto retry;
		}

		KASSERT(entry->wired_count > 0, 
			("wired_count was 0 with USER_WIRED set! %p", entry));
	
		/* Drop wired count, if it hits zero, unwire the entry */
		entry->eflags &= ~MAP_ENTRY_USER_WIRED;
		entry->wired_count--;
		if (entry->wired_count == 0)
			vm_fault_unwire(map, entry);
	}

	vm_map_unlock(map);

	return (rc);
}

/*
 * munlock system call handler
 *
 * munlock_args(const void *addr, size_t len)
 *
 * No requirements
 */
int
sys_munlock(struct sysmsg *sysmsg, const struct munlock_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	vm_offset_t addr;
	vm_offset_t tmpaddr;
	vm_size_t size, pageoff;
	int error;

	addr = (vm_offset_t) uap->addr;
	size = uap->len;

	pageoff = (addr & PAGE_MASK);
	addr -= pageoff;
	size += pageoff;
	size = (vm_size_t) round_page(size);

	tmpaddr = addr + size;
	if (tmpaddr < addr)		/* wrap */
		return (EINVAL);
	if (size == 0)			/* silently allow 0 size */
		return (0);

#ifndef pmap_wired_count
	error = caps_priv_check_td(td, SYSCAP_RESTRICTEDROOT);
	if (error)
		return (error);
#endif
	/* unwire the pages */
	error = vm_map_user_wiring(&p->p_vmspace->vm_map,
				   addr, addr + size, TRUE);
	return (error == KERN_SUCCESS ? 0 : ENOMEM);
}

/*
 * Internal version of mmap.
 * Currently used by mmap, exec, and sys5 shared memory.
 * Handle is either a vnode pointer or NULL for MAP_ANON.
 * 
 * No requirements
 */
int
vm_mmap(vm_map_t map, vm_offset_t *addr, vm_size_t size, vm_prot_t prot,
	vm_prot_t maxprot, int flags, void *handle, vm_ooffset_t foff,
	struct file *fp)
{
	boolean_t fitit;
	vm_object_t object;
	vm_offset_t eaddr;
	vm_size_t   esize;
	vm_size_t   align;
	int (*uksmap)(vm_map_backing_t ba, int op, cdev_t dev, vm_page_t fake);
	struct vnode *vp;
	struct thread *td = curthread;
	struct proc *p;
	int rv = KERN_SUCCESS;
	off_t objsize;
	int docow;
	int error;

	if (size == 0)
		return (0);

	objsize = round_page(size);
	if (objsize < size)
		return (EINVAL);
	size = objsize;

	lwkt_gettoken(&map->token);
	
	/*
	 * XXX messy code, fixme
	 *
	 * NOTE: Overflow checks require discrete statements or GCC4
	 * will optimize it out.
	 */
	if ((p = curproc) != NULL && map == &p->p_vmspace->vm_map) {
		esize = map->size + size;	/* workaround gcc4 opt */
		if (esize < map->size ||
		    esize > p->p_rlimit[RLIMIT_VMEM].rlim_cur) {
			lwkt_reltoken(&map->token);
			return(ENOMEM);
		}
	}

	/*
	 * We currently can only deal with page aligned file offsets.
	 * The check is here rather than in the syscall because the
	 * kernel calls this function internally for other mmaping
	 * operations (such as in exec) and non-aligned offsets will
	 * cause pmap inconsistencies...so we want to be sure to
	 * disallow this in all cases.
	 *
	 * NOTE: Overflow checks require discrete statements or GCC4
	 * will optimize it out.
	 */
	if (foff & PAGE_MASK) {
		lwkt_reltoken(&map->token);
		return (EINVAL);
	}

	/*
	 * Handle alignment.  For large memory maps it is possible
	 * that the MMU can optimize the page table so align anything
	 * that is a multiple of SEG_SIZE to SEG_SIZE.
	 *
	 * Also align any large mapping (bigger than 16x SG_SIZE) to a
	 * SEG_SIZE address boundary.
	 */
	if (flags & MAP_SIZEALIGN) {
		align = size;
		if ((align ^ (align - 1)) != (align << 1) - 1) {
			lwkt_reltoken(&map->token);
			return (EINVAL);
		}
	} else if ((flags & MAP_FIXED) == 0 &&
		   ((size & SEG_MASK) == 0 || size > SEG_SIZE * 16)) {
		align = SEG_SIZE;
	} else {
		align = PAGE_SIZE;
	}

	if ((flags & (MAP_FIXED | MAP_TRYFIXED)) == 0) {
		fitit = TRUE;
		*addr = round_page(*addr);
	} else {
		if (*addr != trunc_page(*addr)) {
			lwkt_reltoken(&map->token);
			return (EINVAL);
		}
		eaddr = *addr + size;
		if (eaddr < *addr) {
			lwkt_reltoken(&map->token);
			return (EINVAL);
		}
		fitit = FALSE;
		if ((flags & MAP_TRYFIXED) == 0)
			vm_map_remove(map, *addr, *addr + size);
	}

	uksmap = NULL;

	/*
	 * Lookup/allocate object.
	 */
	if (flags & MAP_ANON) {
		/*
		 * Unnamed anonymous regions always start at 0.
		 */
		if (handle) {
			/*
			 * Default memory object
			 */
			object = default_pager_alloc(handle, objsize,
						     prot, foff);
			if (object == NULL) {
				lwkt_reltoken(&map->token);
				return(ENOMEM);
			}
			docow = COWF_PREFAULT_PARTIAL;
		} else {
			/*
			 * Implicit single instance of a default memory
			 * object, so we don't need a VM object yet.
			 */
			foff = 0;
			object = NULL;
			docow = 0;
		}
		vp = NULL;
	} else {
		vp = (struct vnode *)handle;

		/*
		 * Non-anonymous mappings of VCHR (aka not /dev/zero)
		 * cannot specify MAP_STACK.
		 */
		if (vp->v_type == VCHR) {
			if (flags & MAP_STACK) {
				lwkt_reltoken(&map->token);
				return(EINVAL);
			}
		}

		if (vp->v_type == VCHR && vp->v_rdev->si_ops->d_uksmap) {
			/*
			 * Device mappings without a VM object, typically
			 * sharing permanently allocated kernel memory or
			 * process-context-specific (per-process) data.
			 *
			 * The object offset for uksmap represents the
			 * lwp_tid that did the mapping.
			 *
			 * Force them to be shared.
			 */
			uksmap = vp->v_rdev->si_ops->d_uksmap;
			object = NULL;
			docow = COWF_PREFAULT_PARTIAL;
			flags &= ~(MAP_PRIVATE|MAP_COPY);
			flags |= MAP_SHARED;
		} else if (vp->v_type == VCHR) {
			/*
			 * Device mappings (device size unknown?).
			 * Force them to be shared.
			 */
			error = dev_dmmap_single(vp->v_rdev, &foff, objsize,
						&object, prot, fp);

			if (error == ENODEV) {
				handle = (void *)(intptr_t)vp->v_rdev;
				object = dev_pager_alloc(handle, objsize, prot, foff);
				if (object == NULL) {
					lwkt_reltoken(&map->token);
					return(EINVAL);
				}
			} else if (error) {
				lwkt_reltoken(&map->token);
				return(error);
			}

			docow = COWF_PREFAULT_PARTIAL;
			flags &= ~(MAP_PRIVATE|MAP_COPY);
			flags |= MAP_SHARED;
		} else {
			/*
			 * Regular file mapping (typically).  The attribute
			 * check is for the link count test only.  mmapable
			 * vnodes must already have a VM object assigned.
			 */
			struct vattr vat;
			int error;

			error = VOP_GETATTR(vp, &vat);
			if (error) {
				lwkt_reltoken(&map->token);
				return (error);
			}
			docow = COWF_PREFAULT_PARTIAL;
			object = vnode_pager_reference(vp);
			if (object == NULL && vp->v_type == VREG) {
				lwkt_reltoken(&map->token);
				kprintf("Warning: cannot mmap vnode %p, no "
					"object\n", vp);
				return(EINVAL);
			}

			/*
			 * If it is a regular file without any references
			 * we do not need to sync it.
			 */
			if (vp->v_type == VREG && vat.va_nlink == 0) {
				flags |= MAP_NOSYNC;
			}
		}
	}

	/*
	 * Deal with the adjusted flags
	 */
	if ((flags & (MAP_ANON|MAP_SHARED)) == 0)
		docow |= COWF_COPY_ON_WRITE;
	if (flags & MAP_NOSYNC)
		docow |= COWF_DISABLE_SYNCER;
	if (flags & MAP_NOCORE)
		docow |= COWF_DISABLE_COREDUMP;
	if (flags & MAP_32BIT)
		docow |= COWF_32BIT;

	/*
	 * This may place the area in its own page directory if (size) is
	 * large enough, otherwise it typically returns its argument.
	 *
	 * (object can be NULL)
	 */
	if (fitit) {
		*addr = pmap_addr_hint(object, *addr, size);
	}

	/*
	 * Stack mappings need special attention.
	 *
	 * Mappings that use virtual page tables will default to storing
	 * the page table at offset 0.
	 */
	if (uksmap) {
		rv = vm_map_find(map, uksmap, vp->v_rdev,
				 foff, addr, size,
				 align, fitit,
				 VM_MAPTYPE_UKSMAP, VM_SUBSYS_MMAP,
				 prot, maxprot, docow);
	} else if (flags & MAP_STACK) {
		rv = vm_map_stack(map, addr, size, flags,
				  prot, maxprot, docow);
	} else {
		rv = vm_map_find(map, object, NULL,
				 foff, addr, size,
				 align, fitit,
				 VM_MAPTYPE_NORMAL, VM_SUBSYS_MMAP,
				 prot, maxprot, docow);
	}

	if (rv != KERN_SUCCESS) {
		/*
		 * Lose the object reference. Will destroy the
		 * object if it's an unnamed anonymous mapping
		 * or named anonymous without other references.
		 *
		 * (NOTE: object can be NULL)
		 */
		vm_object_deallocate(object);
		goto out;
	}

	/*
	 * Shared memory is also shared with children.
	 */
	if (flags & (MAP_SHARED|MAP_INHERIT)) {
		rv = vm_map_inherit(map, *addr, *addr + size, VM_INHERIT_SHARE);
		if (rv != KERN_SUCCESS) {
			vm_map_remove(map, *addr, *addr + size);
			goto out;
		}
	}


	/* If a process has marked all future mappings for wiring, do so */
	if ((rv == KERN_SUCCESS) && (map->flags & MAP_WIREFUTURE))
		vm_map_user_wiring(map, *addr, *addr + size, FALSE);

	/*
	 * Set the access time on the vnode
	 */
	if (vp != NULL)
		vn_mark_atime(vp, td);
out:
	lwkt_reltoken(&map->token);
	
	switch (rv) {
	case KERN_SUCCESS:
		return (0);
	case KERN_INVALID_ADDRESS:
	case KERN_NO_SPACE:
		return (ENOMEM);
	case KERN_PROTECTION_FAILURE:
		return (EACCES);
	default:
		return (EINVAL);
	}
}

/*
 * Translate a Mach VM return code to zero on success or the appropriate errno
 * on failure.
 */
int
vm_mmap_to_errno(int rv)
{

	switch (rv) {
	case KERN_SUCCESS:
		return (0);
	case KERN_INVALID_ADDRESS:
	case KERN_NO_SPACE:
		return (ENOMEM);
	case KERN_PROTECTION_FAILURE:
		return (EACCES);
	default:
		return (EINVAL);
	}
}
