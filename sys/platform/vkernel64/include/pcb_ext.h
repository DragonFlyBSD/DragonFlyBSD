/*-
 * Copyright (c) 1997 Jonathan Lemon
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
 * $FreeBSD: src/sys/i386/include/pcb_ext.h,v 1.4 1999/12/29 04:33:04 peter Exp $
 */

#ifndef _MACHINE_PCB_EXT_H_
#define _MACHINE_PCB_EXT_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * Extension to the 386 process control block
 */
#include <machine/tss.h>
#include <machine/segments.h>

struct pcb_ext {
	struct 	user_segment_descriptor ext_tssd;	/* tss descriptor */
	struct 	x86_64tss	ext_tss;	/* per-process x86_64tss */
	caddr_t	ext_iomap;		/* i/o permission bitmap */
};

/* JG remove this structure? */
struct pcb_ldt {
	caddr_t	ldt_base;
	int	ldt_len;
	int	ldt_refcnt;
	u_long	ldt_active;
	struct	user_segment_descriptor ldt_sd;
};

#ifdef _KERNEL

struct pcb;

void set_user_ldt (struct pcb *);
struct pcb_ldt *user_ldt_alloc (struct pcb *, int);
void user_ldt_free (struct pcb *);

#endif

#endif /* _MACHINE_PCB_EXT_H_ */
