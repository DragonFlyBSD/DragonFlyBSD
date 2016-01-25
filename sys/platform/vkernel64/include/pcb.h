/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)pcb.h	5.10 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/i386/include/pcb.h,v 1.32.2.1 2001/08/15 01:23:52 peter Exp $
 */

#ifndef _MACHINE_PCB_H_
#define _MACHINE_PCB_H_

/*
 * x86_64 process control block
 */
#include <machine/npx.h>

struct pcb {
	register_t	padxx[8];
	register_t	pcb_unused01;
	register_t	pcb_r15;
	register_t	pcb_r14;
	register_t	pcb_r13;
	register_t	pcb_r12;
	register_t	pcb_rbp;
	register_t	pcb_rsp;
	register_t	pcb_rbx;
	register_t	pcb_rip;
	register_t	pcb_rsi;
	register_t	pcb_rdi;
	register_t	pcb_rflags;
	register_t	pcb_fsbase;
	register_t	pcb_gsbase;
	u_long		pcb_flags;
	u_int32_t	pcb_ds;
	u_int32_t	pcb_es;
	u_int32_t	pcb_fs;
	u_int32_t	pcb_gs;
	u_int64_t	pcb_dr0;
	u_int64_t	pcb_dr1;
	u_int64_t	pcb_dr2;
	u_int64_t	pcb_dr3;
	u_int64_t	pcb_dr6;
	u_int64_t	pcb_dr7;

	struct pcb_ldt *pcb_ldt;
	union savefpu	pcb_save;
#define	PCB_DBREGS	0x02	/* process using debug registers */
#define	PCB_FPUINITDONE	0x08	/* fpu state is initialized */
#define FP_SOFTFP       0x01    /* process using software fltng pnt emulator */
#define	FP_VIRTFP	0x04	/* virtual kernel wants exception */
	caddr_t	pcb_onfault;	/* copyin/out fault recovery */
	int	pcb_unused;
	struct  pcb_ext *pcb_ext;	/* optional pcb extension */
};

#ifdef _KERNEL
void	savectx(struct pcb *);
#endif

#endif /* _MACHINE_PCB_H_ */
