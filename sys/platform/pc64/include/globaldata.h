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
 *	Only machine-dependant code should ever include this file.  MI
 *	code and header files do NOT include this file.  e.g. sys/globaldata.h
 *	should not include this file.
 *
 * $FreeBSD: src/sys/i386/include/globaldata.h,v 1.11.2.1 2000/05/16 06:58:10 dillon Exp $
 * $DragonFly: src/sys/platform/pc64/include/globaldata.h,v 1.2 2008/08/29 17:07:17 dillon Exp $
 */

#ifndef _MACHINE_GLOBALDATA_H_
#define _MACHINE_GLOBALDATA_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>	/* struct globaldata */
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>		/* struct thread */
#endif
#ifndef _MACHINE_SEGMENTS_H_
#include <machine/segments.h>	/* struct user_segment_descriptor */
#endif
#ifndef _MACHINE_TSS_H_
#include <machine/tss.h>	/* struct x86_64tss */
#endif
#ifndef _MACHINE_NPX_H_
#include <machine/npx.h>
#endif

/*
 * Note on interrupt control.  Pending interrupts not yet dispatched are
 * marked in gd_fpending or gd_spending.  Once dispatched the interrupt's
 * pending bit is cleared and the interrupt is masked.  Upon completion
 * the interrupt is unmasked.
 *
 * For edge triggered interrupts interrupts may be enabled again at this
 * point and if they occur before the interrupt service routine is complete
 * the service routine will loop.
 *
 * The current thread's cpl is stored in the thread structure.
 *
 * Note: the embedded globaldata and/or the mdglobaldata structure
 * may exceed the size of a page.
 */
struct mdglobaldata {
	struct globaldata mi;
	struct user_segment_descriptor gd_common_tssd;
	struct user_segment_descriptor *gd_tss_gdt;
	struct thread   *gd_npxthread;
	struct x86_64tss gd_common_tss;
	union savefpu	gd_savefpu;	/* fast bcopy/zero temp fpu save area */
	int		gd_fpu_lock;	/* fast bcopy/zero cpu lock */
	int		gd_fpending;	/* fast interrupt pending */
	int		unused001;
	int		gd_spending;	/* software interrupt pending */
	int		gd_sdelayed;	/* delayed software ints */
	int		gd_currentldt;
	int		gd_private_tss;
	u_int		unused002;
	u_int		gd_other_cpus;
	u_int		gd_ss_eflags;
	pt_entry_t	*gd_CMAP1;
	pt_entry_t	*gd_CMAP2;
	pt_entry_t	*gd_CMAP3;
	pt_entry_t	*gd_PMAP1;
	caddr_t		gd_CADDR1;
	caddr_t		gd_CADDR2;
	caddr_t		gd_CADDR3;
	pt_entry_t	*gd_PADDR1;
	u_int		gd_acpi_id;
	u_int		gd_apic_id;
	register_t	gd_scratch_rsp;
	register_t	unused003;
	register_t	gd_user_fs;	/* current user fs in MSR */
	register_t	gd_user_gs;	/* current user gs in MSR */
	cpumask_t	gd_invltlb_ret;
};

#define MDGLOBALDATA_BASEALLOC_SIZE	\
	((sizeof(struct mdglobaldata) + PAGE_MASK) & ~PAGE_MASK)
#define MDGLOBALDATA_BASEALLOC_PAGES	\
	(MDGLOBALDATA_BASEALLOC_SIZE / PAGE_SIZE)
#define MDGLOBALDATA_PAD		\
	(MDGLOBALDATA_BASEALLOC_SIZE - sizeof(struct mdglobaldata))

/*
 * This is the upper (0xff800000) address space layout that is per-cpu.
 * It is setup in locore.s and pmap.c for the BSP and in mp_machdep.c for
 * each AP.  genassym helps export this to the assembler code.
 *
 * JG WARNING!  page-bounded fields are hardwired for SMPpt[] setup in
 * i386/i386/mp_machdep.c and locore.s.
 */
struct privatespace {
	/* JG TODO: fix comments describing layout */
	/* page 0 - data page */
	struct mdglobaldata mdglobaldata;
	char		__filler0[MDGLOBALDATA_PAD];

	/* page 1..4 - CPAGE1,CPAGE2,CPAGE3,PPAGE1 */
	char		CPAGE1[PAGE_SIZE];		/* SMPpt[1] */
	char		CPAGE2[PAGE_SIZE];		/* SMPpt[2] */
	char		CPAGE3[PAGE_SIZE];		/* SMPpt[3] */
	char		PPAGE1[PAGE_SIZE];		/* SMPpt[4] */

	/* page 5..4+UPAGES - idle stack (UPAGES pages) */
	char		idlestack[UPAGES * PAGE_SIZE];	/* SMPpt[5..] */
};
#define mdcpu  		((struct mdglobaldata *)_get_mycpu())

#endif

#ifdef _KERNEL

extern struct privatespace CPU_prvspace[];

#endif

#endif
