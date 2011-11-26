/*
 * (MPSAFE)
 *
 * 43BSD_VM.C		- 4.3BSD compatibility virtual memory syscalls
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_vm.c,v 1.4 2006/09/17 21:07:25 dillon Exp $
 *	from: DragonFly vm/vm_unix.c,v 1.3
 *	from: DragonFly vm/vm_mmap.c,v 1.15
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/sysproto.h>
#include <sys/kern_syscall.h>
#include <sys/mman.h>
#include <sys/proc.h>

#include <sys/thread.h>
#include <sys/thread2.h>

/*
 * No requirements
 */
int
sys_ovadvise(struct ovadvise_args *uap)
{
	return (EINVAL);
}

/*
 * No requirements
 */
int
sys_ogetpagesize(struct getpagesize_args *uap)
{
	uap->sysmsg_iresult = PAGE_SIZE;
	return (0);
}

/*
 * No requirements
 */
int
sys_ommap(struct ommap_args *uap)
{
	static const char cvtbsdprot[8] = {
		0,
		PROT_EXEC,
		PROT_WRITE,
		PROT_EXEC | PROT_WRITE,
		PROT_READ,
		PROT_EXEC | PROT_READ,
		PROT_WRITE | PROT_READ,
		PROT_EXEC | PROT_WRITE | PROT_READ,
	};
	int error, flags, prot;

#define	OMAP_ANON	0x0002
#define	OMAP_COPY	0x0020
#define	OMAP_SHARED	0x0010
#define	OMAP_FIXED	0x0100
#define	OMAP_INHERIT	0x0800

	prot = cvtbsdprot[uap->prot & 0x7];
	flags = 0;
	if (uap->flags & OMAP_ANON)
		flags |= MAP_ANON;
	if (uap->flags & OMAP_COPY)
		flags |= MAP_COPY;
	if (uap->flags & OMAP_SHARED)
		flags |= MAP_SHARED;
	else
		flags |= MAP_PRIVATE;
	if (uap->flags & OMAP_FIXED)
		flags |= MAP_FIXED;
	if (uap->flags & OMAP_INHERIT)
		flags |= MAP_INHERIT;

	error = kern_mmap(curproc->p_vmspace, uap->addr, uap->len,
			  prot, flags, uap->fd, uap->pos,
			  &uap->sysmsg_resultp);

	return (error);
}
