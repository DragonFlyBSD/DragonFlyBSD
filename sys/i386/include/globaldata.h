/*-
 * Copyright (c) Peter Wemm <peter@netplex.com.au>
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
 * $FreeBSD: src/sys/i386/include/globaldata.h,v 1.11.2.1 2000/05/16 06:58:10 dillon Exp $
 * $DragonFly: src/sys/i386/include/Attic/globaldata.h,v 1.11 2003/06/27 20:27:18 dillon Exp $
 */

/*
 * This structure maps out the global data that needs to be kept on a
 * per-cpu basis.  genassym uses this to generate offsets for the assembler
 * code, which also provides external symbols so that C can get at them as
 * though they were really globals.
 *
 * The SMP parts are setup in pmap.c and locore.s for the BSP, and
 * mp_machdep.c sets up the data for the AP's to "see" when they awake.
 * The reason for doing it via a struct is so that an array of pointers
 * to each CPU's data can be set up for things like "check curproc on all
 * other processors"
 *
 * NOTE! this structure needs to remain compatible between module accessors
 * and the kernel, so we can't throw in lots of #ifdef's.
 */
struct globaldata {
	struct privatespace *gd_prvspace;	/* self-reference */
	struct thread	*gd_curthread;
	struct thread	*gd_npxthread;
	struct i386tss	gd_common_tss;
	int		gd_tdfreecount;		/* new thread cache */
	int		gd_reqpri;		/* highest pri blocked thread */
	TAILQ_HEAD(,thread) gd_tdfreeq;		/* new thread cache */
	TAILQ_HEAD(,thread) gd_tdrunq;		/* runnable threads */
	struct segment_descriptor gd_common_tssd;
	struct segment_descriptor *gd_tss_gdt;
	int		gd_currentldt;		/* USER_LDT */
	u_int		gd_cpuid;
	struct timeval	gd_stattv;
#ifdef SMP
	u_int		gd_cpu_lockid;
	u_int		gd_other_cpus;
	int		gd_inside_intr;
	u_int		gd_ss_eflags;
	pt_entry_t	*gd_prv_CMAP1;
	pt_entry_t	*gd_prv_CMAP2;
	pt_entry_t	*gd_prv_CMAP3;
	pt_entry_t	*gd_prv_PMAP1;
	caddr_t		gd_prv_CADDR1;
	caddr_t		gd_prv_CADDR2;
	caddr_t		gd_prv_CADDR3;
	unsigned	*gd_prv_PADDR1;
#endif
	u_int		gd_astpending;
	struct thread	gd_idlethread;
};

/*
 * This is the upper (0xff800000) address space layout that is per-cpu.
 * It is setup in locore.s and pmap.c for the BSP and in mp_machdep.c for
 * each AP.  genassym helps export this to the assembler code.
 */
struct privatespace {
	/* page 0 - data page */
	struct globaldata globaldata;
	char		__filler0[PAGE_SIZE - sizeof(struct globaldata)];

#ifdef SMP
	/* page 1..4 - CPAGE1,CPAGE2,CPAGE3,PPAGE1 */
	char		CPAGE1[PAGE_SIZE];
	char		CPAGE2[PAGE_SIZE];
	char		CPAGE3[PAGE_SIZE];
	char		PPAGE1[PAGE_SIZE];
#endif

	/* page 5..4+UPAGES - idle stack (UPAGES pages) */
	char		idlestack[UPAGES * PAGE_SIZE];
};

extern struct privatespace CPU_prvspace[];

