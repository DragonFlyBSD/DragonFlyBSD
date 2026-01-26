/*
 * Copyright (c) 2003 Matt Dillon <dillon@backplane.com>
 * Copyright (c) 2008 The DragonFly Project.
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
 *	Machine independant code should not directly include this file.
 */

#ifndef	_MACHINE_THREAD_H_
#define	_MACHINE_THREAD_H_

#include <cpu/tls.h>

struct md_thread {
	unsigned int	mtd_cpl;
	struct savetls	mtd_savetls;	/* vkernel TLS save area */
};

#ifdef _KERNEL

#define td_cpl	td_mach.mtd_cpl
#define td_tls	td_mach.mtd_savetls

struct globaldata;

extern int __mycpu__dummy;

static __inline
struct globaldata *
_get_mycpu(void)
{
	struct globaldata *gd;

	/*
	 * ARM64 uses x18 as a dedicated per-CPU data pointer register.
	 * This follows FreeBSD convention. The register is set up during
	 * early boot to point to the mdglobaldata structure for this CPU.
	 */
	__asm __volatile("mov %0, x18" : "=r" (gd));
	return (gd);
}

#define mycpu	_get_mycpu()
#define mycpuid (_get_mycpu()->gd_cpuid)

#define curthread	mycpu->gd_curthread
#define	curproc		curthread->td_proc

#endif	/* _KERNEL */

#endif	/* !_MACHINE_THREAD_H_ */
