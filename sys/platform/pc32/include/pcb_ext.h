/*-
 * Copyright (c) 1997 Jonathan Lemon
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
 * $FreeBSD: src/sys/i386/include/pcb_ext.h,v 1.4 1999/12/29 04:33:04 peter Exp $
 * $DragonFly: src/sys/platform/pc32/include/pcb_ext.h,v 1.7 2006/05/20 02:42:06 dillon Exp $
 */

#ifndef _MACHINE_PCB_EXT_H_
#define _MACHINE_PCB_EXT_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * Extension to the 386 process control block
 */
#ifndef _MACHINE_TSS_H_
#include "tss.h"
#endif
#ifndef _MACHINE_VM86_H_
#include "vm86.h"
#endif
#ifndef _MACHINE_SEGMENTS_H_
#include "segments.h"
#endif

struct pcb_ext {
	struct 	segment_descriptor ext_tssd;	/* tss descriptor */
	struct 	i386tss	ext_tss;	/* per-process i386tss */
	caddr_t	ext_iomap;		/* i/o permission bitmap */
	struct	vm86_kernel ext_vm86;	/* vm86 area */
};

struct pcb_ldt {
	caddr_t	ldt_base;
	int	ldt_len;
	int	ldt_refcnt;
	u_long	ldt_active;
	struct	segment_descriptor ldt_sd;
};

#ifdef _KERNEL

struct pcb;

void set_user_ldt (struct pcb *);
struct pcb_ldt *user_ldt_alloc (struct pcb *, int);
void user_ldt_free (struct pcb *);
void set_user_TLS (void);

#endif

#endif /* _I386_PCB_EXT_H_ */
