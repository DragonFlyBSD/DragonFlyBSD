/*-
 * Copyright (c) Peter Wemm <peter@netplex.com.au>
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
 * $FreeBSD: src/sys/i386/i386/globals.s,v 1.13.2.1 2000/05/16 06:58:06 dillon Exp $
 */

#include <machine/asmacros.h>
#include <machine/pmap.h>

#include "assym.s"

	/*
	 * Define the layout of the per-cpu address space.  This is
	 * "constructed" in locore.s on the BSP and in mp_machdep.c for
	 * each AP.  DO NOT REORDER THESE WITHOUT UPDATING THE REST!
	 *
	 * On UP the per-cpu address space is simply placed in the data
	 * segment.
	 */
	.data

	.globl	globaldata
	.set	globaldata,0

	/*
	 * Define layout of the global data.  On SMP this lives in
	 * the per-cpu address space, otherwise it's in the data segment.
	 */
	.globl	gd_curthread, gd_npxthread, gd_reqflags, gd_common_tss
	.set	gd_curthread,globaldata + GD_CURTHREAD
	.set	gd_npxthread,globaldata + GD_NPXTHREAD
	.set	gd_reqflags,globaldata + GD_REQFLAGS
	.set	gd_common_tss,globaldata + GD_COMMON_TSS

	.globl	gd_common_tssd, gd_tss_gdt
	.set	gd_common_tssd,globaldata + GD_COMMON_TSSD
	.set	gd_tss_gdt,globaldata + GD_TSS_GDT

	.globl	gd_currentldt
	.set	gd_currentldt,globaldata + GD_CURRENTLDT

	.globl	gd_fpu_lock, gd_savefpu
	.set	gd_fpu_lock, globaldata + GD_FPU_LOCK
	.set	gd_savefpu, globaldata + GD_SAVEFPU

	/*
	 * The BSP version of these get setup in locore.s and pmap.c, while
	 * the AP versions are setup in mp_machdep.c.
	 */
	.globl  gd_cpuid, gd_cpumask, gd_other_cpus
	.globl	gd_ss_eflags, gd_intr_nesting_level
	.globl  gd_spending, gd_ipending
	.globl	gd_cnt, gd_private_tss
	.globl	gd_scratch_rsp
	.globl	gd_user_fs, gd_user_gs
	.globl	gd_sample_pc

	.set    gd_cpuid,globaldata + GD_CPUID
	.set    gd_cpumask,globaldata + GD_CPUMASK
	.set    gd_private_tss,globaldata + GD_PRIVATE_TSS
	.set    gd_other_cpus,globaldata + GD_OTHER_CPUS
	.set    gd_ss_eflags,globaldata + GD_SS_EFLAGS
	.set    gd_intr_nesting_level,globaldata + GD_INTR_NESTING_LEVEL
	.set	gd_ipending,globaldata + GD_IPENDING
	.set	gd_spending,globaldata + GD_SPENDING
	.set	gd_cnt,globaldata + GD_CNT
	.set	gd_scratch_rsp,globaldata + GD_SCRATCH_RSP
	.set	gd_user_fs,globaldata + GD_USER_FS
	.set	gd_user_gs,globaldata + GD_USER_GS
	.set	gd_sample_pc,globaldata + GD_SAMPLE_PC

