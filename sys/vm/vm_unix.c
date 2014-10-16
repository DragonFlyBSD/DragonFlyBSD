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
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/thread.h>

/*
 * sys_obreak backs the C library sbrk call
 *
 * obreak_args(char *nsize)
 *
 * No requirements.
 */
int
sys_obreak(struct obreak_args *uap)
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
	old = base + ctob(vm->vm_dsize);

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
				 PAGE_SIZE,
				 FALSE, VM_MAPTYPE_NORMAL,
				 VM_PROT_ALL, VM_PROT_ALL, 0);
		if (rv != KERN_SUCCESS) {
			error = ENOMEM;
			goto done;
		}
		if (vm->vm_map.flags & MAP_WIREFUTURE)
			vm_map_wire(&vm->vm_map, old, new, FALSE);

		vm->vm_dsize += btoc(diff);
	} else if (new < old) {
		rv = vm_map_remove(&vm->vm_map, new, old);
		if (rv != KERN_SUCCESS) {
			error = ENOMEM;
			goto done;
		}
		vm->vm_dsize -= btoc(old - new);
	}
done:
	lwkt_reltoken(&vm->vm_map.token);

	return (error);
}
