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
 * from: Utah $Hdr: vm_unix.c 1.1 89/11/07$
 *
 *	@(#)vm_unix.c	8.1 (Berkeley) 6/11/93
 * $FreeBSD: src/sys/vm/vm_unix.c,v 1.24.2.2 2002/07/02 20:06:19 dillon Exp $
 * $DragonFly: src/sys/vm/vm_unix.c,v 1.7 2006/11/07 17:51:24 dillon Exp $
 */

/*
 * Traditional sbrk/grow interface to VM
 */
#include <sys/param.h>
#include <sys/sysmsg.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/thread.h>

/*
 * sys_sbrk backs the C library sbrk call
 *
 * void *sbrk(intptr_t incr)
 *
 * No requirements.
 */
int
sys_sbrk(struct sysmsg *sysmsg, const struct sbrk_args *uap)
{
	struct proc *p = curproc;
	struct vmspace *vm = p->p_vmspace;
	vm_offset_t nbase;
	vm_offset_t base;
	vm_offset_t base_end;
	vm_offset_t incr;
	int rv;
	int error;

	error = 0;

	lwkt_gettoken(&vm->vm_map.token);

	/*
	 * Cannot assume that last data page for binary is mapped R/W.
	 */
	base = round_page((vm_offset_t)vm->vm_daddr) + vm->vm_dsize;
	incr = uap->incr;

	/*
	 * Cannot allow space to be freed with sbrk() because it is not
	 * thread-safe for userland and because unrelated mmap()s can reside
	 * in the data space if the DATA rlimit is raised on the running
	 * program.
	 */
	if (incr < 0) {
		error = EOPNOTSUPP;
		goto done;
	}

	/*
	 * Userland requests current base
	 */
	if (incr == 0) {
		sysmsg->sysmsg_resultp = (void *)base;
		goto done;
	}

	/*
	 * Calculate approximate area (vm_map_find() may change this).
	 * Check for overflow, address space, and rlimit caps.
	 */
	base_end = base + incr;
	if (base_end >= VM_MAX_USER_ADDRESS) {
		error = ENOMEM;
		goto done;
	}
	if (base_end < base ||
	    base_end - (vm_offset_t)vm->vm_daddr >
	    (vm_offset_t)p->p_rlimit[RLIMIT_DATA].rlim_cur) {
		error = ENOMEM;
		goto done;
	}

	/*
	 * Same-page optimization (protected by token)
	 */
	if ((base & PAGE_MASK) != 0 &&
	    ((base ^ (base_end - 1)) & ~(vm_offset_t)PAGE_MASK) == 0) {
		sysmsg->sysmsg_resultp = (void *)base;
		vm->vm_dsize += incr;
		goto done;
	}

	/*
	 * Formally map more space
	 */
	nbase = round_page(base);
	rv = vm_map_find(&vm->vm_map, NULL, NULL,
			 0, &nbase, round_page(incr),
			 PAGE_SIZE, FALSE,
			 VM_MAPTYPE_NORMAL, VM_SUBSYS_BRK,
			 VM_PROT_ALL, VM_PROT_ALL, 0);
	if (rv != KERN_SUCCESS) {
		error = ENOMEM;
		goto done;
	}
	base_end = nbase + round_page(incr);
	sysmsg->sysmsg_resultp = (void *)nbase;
	if (vm->vm_map.flags & MAP_WIREFUTURE)
		vm_map_wire(&vm->vm_map, base, base_end, FALSE);

	/*
	 * Adjust dsize upwards only
	 */
	incr = nbase + incr - round_page((vm_offset_t)vm->vm_daddr);
	if (vm->vm_dsize < incr)
		vm->vm_dsize = incr;

done:
	lwkt_reltoken(&vm->vm_map.token);

	return (error);
}

/*
 * sys_obreak is used by the sbrk emulation code in libc when sbrk()
 * is not supported.
 *
 * obreak_args(char *nsize)
 *
 * No requirements.
 */
int
sys_obreak(struct sysmsg *sysmsg, const struct obreak_args *uap)
{
	struct proc *p = curproc;
	struct vmspace *vm = p->p_vmspace;
	vm_offset_t new, old, base;
	int rv;
	int error;

	error = 0;

	lwkt_gettoken(&vm->vm_map.token);

	base = round_page((vm_offset_t)vm->vm_daddr);
	new = round_page((vm_offset_t)uap->nsize);
	old = base + round_page(vm->vm_dsize);

	if (new > base) {
		/*
		 * We check resource limits here, but alow processes to
		 * reduce their usage, even if they remain over the limit.
		 */
		if (new > old &&
		    (new - base) > (vm_offset_t) p->p_rlimit[RLIMIT_DATA].rlim_cur) {
			error = ENOMEM;
			goto done;
		}
		if (new >= VM_MAX_USER_ADDRESS) {
			error = ENOMEM;
			goto done;
		}
	} else if (new < base) {
		/*
		 * This is simply an invalid value.  If someone wants to
		 * do fancy address space manipulations, mmap and munmap
		 * can do most of what the user would want.
		 */
		error = EINVAL;
		goto done;
	}

	if (new > old) {
		vm_size_t diff;

		diff = new - old;
		if (vm->vm_map.size + diff > p->p_rlimit[RLIMIT_VMEM].rlim_cur) {
			error = ENOMEM;
			goto done;
		}
		rv = vm_map_find(&vm->vm_map, NULL, NULL,
				 0, &old, diff,
				 PAGE_SIZE, FALSE,
				 VM_MAPTYPE_NORMAL, VM_SUBSYS_BRK,
				 VM_PROT_ALL, VM_PROT_ALL, 0);
		if (rv != KERN_SUCCESS) {
			error = ENOMEM;
			goto done;
		}
		if (vm->vm_map.flags & MAP_WIREFUTURE)
			vm_map_wire(&vm->vm_map, old, new, FALSE);

		vm->vm_dsize += diff;
	} else if (new < old) {
		error = EOPNOTSUPP;
#if 0
		rv = vm_map_remove(&vm->vm_map, new, old);
		if (rv != KERN_SUCCESS) {
			error = ENOMEM;
			goto done;
		}
		vm->vm_dsize -= old - new;
#endif
	}
done:
	lwkt_reltoken(&vm->vm_map.token);

	return (error);
}
