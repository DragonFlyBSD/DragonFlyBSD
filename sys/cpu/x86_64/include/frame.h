/*-
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2008-2018 The DragonFly Project.
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
 *	from: @(#)frame.h	5.2 (Berkeley) 1/18/91
 * $FreeBSD: src/sys/amd64/include/frame.h,v 1.26 2003/11/08 04:39:22 peter Exp $
 */

#ifndef _CPU_FRAME_H_
#define _CPU_FRAME_H_

/* JG? */
#include <sys/types.h>

/*
 * System stack frames.
 */

/*
 * Exception/Trap Stack Frame
 *
 * The ordering of this is specifically so that we can take first 6
 * the syscall arguments directly from the beginning of the frame.
 */

struct trapframe {
	/* note: tf_rdi matches mc_rdi in mcontext */
	register_t	tf_rdi;
	register_t	tf_rsi;
	register_t	tf_rdx;
	register_t	tf_rcx;
	register_t	tf_r8;
	register_t	tf_r9;
	register_t	tf_rax;
	register_t	tf_rbx;
	register_t	tf_rbp;
	register_t	tf_r10;
	register_t	tf_r11;
	register_t	tf_r12;
	register_t	tf_r13;
	register_t	tf_r14;
	register_t	tf_r15;
	register_t	tf_xflags;
	register_t	tf_trapno;
	register_t	tf_addr;
	register_t	tf_flags;
	/* below portion defined in hardware */
	register_t	tf_err;
	register_t	tf_rip;
	register_t	tf_cs;
	register_t	tf_rflags;
#define tf_sp tf_rsp
	register_t	tf_rsp;
	register_t	tf_ss;
} __packed;

/* Interrupt stack frame */

struct intrframe {
	register_t	if_vec;	/* vec */
	/* fs XXX */
	/* es XXX */
	/* ds XXX */
	register_t	if_rdi;
	register_t	if_rsi;
	register_t	if_rdx;
	register_t	if_rcx;
	register_t	if_r8;
	register_t	if_r9;
	register_t	if_rax;
	register_t	if_rbx;
	register_t	if_rbp;
	register_t	if_r10;
	register_t	if_r11;
	register_t	if_r12;
	register_t	if_r13;
	register_t	if_r14;
	register_t	if_r15;
	register_t	if_xflags;	/* compat with trap frame - xflags */
	register_t	:64;		/* compat with trap frame - trapno */
	register_t	:64;		/* compat with trap frame - addr */
	register_t	:64;		/* compat with trap frame - flags */
	register_t	:64;		/* compat with trap frame - err */
	/* below portion defined in hardware */
	register_t	if_rip;
	register_t	if_cs;
	register_t	if_rflags;
	register_t	if_rsp;
	register_t	if_ss;
} __packed;

/*
 * The trampframe is placed at the top of the trampoline page and
 * contains all the information needed to trampoline into and out
 * of the isolated user pmap.
 */
struct trampframe {
	register_t	tr_cr2;
	register_t	tr_rax;
	register_t	tr_rcx;
	register_t	tr_rdx;
	register_t	tr_err;
	register_t	tr_rip;
	register_t	tr_cs;
	register_t	tr_rflags;
	register_t	tr_rsp;
	register_t	tr_ss;

	/*
	 * Top of hw stack in TSS is &tr_pcb_rsp (first push is tr_ss).
	 * Make sure this is at least 16-byte aligned, so be sure the
	 * fields below are in multiples of 16 bytes.
	 */
	register_t	tr_pcb_rsp;	/* hw frame tramp top of stack */
	register_t	tr_pcb_flags;	/* copy of pcb control flags */
	register_t	tr_pcb_cr3_iso;	/* copy of isolated pml4e */
	register_t	tr_pcb_cr3;	/* copy of primary pml4e */
	uint32_t	tr_pcb_spec_ctrl[2];/* SPEC_CTRL + ficticious flags */
	register_t	tr_pcb_gs_kernel; /* (used by nmi, dbg) */
	register_t	tr_pcb_gs_saved;  /* (used by nmi) */
	register_t	tr_pcb_cr3_saved; /* (used by nmi) */
} __packed;

int	kdb_trap(int, int, struct trapframe *);

#endif /* _CPU_FRAME_H_ */
