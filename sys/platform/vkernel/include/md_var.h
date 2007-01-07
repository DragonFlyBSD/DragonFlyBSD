/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/platform/vkernel/include/md_var.h,v 1.8 2007/01/07 08:37:36 dillon Exp $
 */

#ifndef _MACHINE_MD_VAR_H_
#define _MACHINE_MD_VAR_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_VKERNEL_H_
#include <sys/vkernel.h>
#endif

extern	char	sigcode[];
extern	int	szsigcode;
extern	vpte_t	*KernelPTA;	/* NOTE: Offset for direct VA translation */
extern	vpte_t	*KernelPTD;
extern	vm_offset_t crashdumpmap;
extern  int	cpu_fxsr;

extern  char    cpu_vendor[];	/* XXX belongs in i386 */
extern  u_int   cpu_id;		/* XXX belongs in i386 */

extern int	RootImageFd;
extern int	MemImageFd;

struct mdglobaldata;

vpte_t *pmap_kpte(vm_offset_t va);
void cpu_gdinit (struct mdglobaldata *gd, int cpu);

void cpu_heavy_restore(void);	/* cannot be called from C */
void cpu_lwkt_restore(void);    /* cannot be called from C */
void cpu_idle_restore(void);    /* cannot be called from C */
void cpu_kthread_restore(void);	/* cannot be called from C */
void cpu_exit_switch (struct thread *next);
void cpu_setregs (void);
void cpu_idle (void);
void go_user (struct trapframe frame);

void init_exceptions(void);
void kern_trap(struct trapframe *);
void user_trap(struct trapframe *);
void syscall2 (struct trapframe *);

#endif

