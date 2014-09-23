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
 * $DragonFly: src/sys/platform/vkernel/platform/globaldata.c,v 1.5 2008/04/28 07:05:08 dillon Exp $
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/tls.h>
#include <sys/proc.h>
#include <vm/vm_page.h>

#include <machine/md_var.h>
#include <machine/globaldata.h>
#include <machine/vmparam.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <assert.h>

struct globaldata *
globaldata_find(int cpu)
{
	KKASSERT(cpu >= 0 && cpu < ncpus);
	return (&CPU_prvspace[cpu].mdglobaldata.mi);
}

void
cpu_gdinit(struct mdglobaldata *gd, int cpu)
{
	if (cpu)
		gd->mi.gd_curthread = &gd->mi.gd_idlethread;

	lwkt_init_thread(&gd->mi.gd_idlethread,
			gd->mi.gd_prvspace->idlestack,
			sizeof(gd->mi.gd_prvspace->idlestack),
			0, &gd->mi);
	lwkt_set_comm(&gd->mi.gd_idlethread, "idle_%d", cpu);
	gd->mi.gd_idlethread.td_switch = cpu_lwkt_switch;
	gd->mi.gd_idlethread.td_sp -= sizeof(void *);
	*(void **)gd->mi.gd_idlethread.td_sp = cpu_idle_restore;

	/*
	 * Single page mappings and where we have to store the related
	 * pte's
	 */
	gd->gd_CADDR1 = (caddr_t)gd->mi.gd_prvspace->CPAGE1;
	gd->gd_CADDR2 = (caddr_t)gd->mi.gd_prvspace->CPAGE2;
	gd->gd_CADDR3 = (caddr_t)gd->mi.gd_prvspace->CPAGE3;
	gd->gd_PADDR1 = gd->mi.gd_prvspace->PPAGE1;

	gd->gd_CMAP1 = &KernelPTA[(vm_offset_t)gd->gd_CADDR1 / PAGE_SIZE];
	gd->gd_CMAP2 = &KernelPTA[(vm_offset_t)gd->gd_CADDR2 / PAGE_SIZE];
	gd->gd_CMAP3 = &KernelPTA[(vm_offset_t)gd->gd_CADDR3 / PAGE_SIZE];
	gd->gd_PMAP1 = &KernelPTA[(vm_offset_t)gd->gd_PADDR1 / PAGE_SIZE];

	/*
	 * Whole page table mappings and where we have to store the related
	 * pde's.
	 */

	gd->gd_PT1map = gd->mi.gd_prvspace->PT1MAP;
	gd->gd_PT1pdir = NULL;
	gd->gd_PT1pde = &KernelPTD[((vm_offset_t)gd->gd_PT1map - KvaStart) / 
				   SEG_SIZE];

	gd->gd_PT2map = gd->mi.gd_prvspace->PT2MAP;
	gd->gd_PT2pdir = NULL;
	gd->gd_PT2pde = &KernelPTD[((vm_offset_t)gd->gd_PT2map - KvaStart) / 
				   SEG_SIZE];

	gd->gd_PT3map = gd->mi.gd_prvspace->PT3MAP;
	gd->gd_PT3pdir = NULL;
	gd->gd_PT3pde = &KernelPTD[((vm_offset_t)gd->gd_PT3map - KvaStart) / 
				   SEG_SIZE];

	KKASSERT(((vm_offset_t)gd->gd_PT1map & SEG_MASK) == 0);
	KKASSERT(((vm_offset_t)gd->gd_PT2map & SEG_MASK) == 0);
	KKASSERT(((vm_offset_t)gd->gd_PT3map & SEG_MASK) == 0);
}

int
is_globaldata_space(vm_offset_t saddr, vm_offset_t eaddr)
{
	if (saddr >= (vm_offset_t)&CPU_prvspace[0] &&
	    eaddr <= (vm_offset_t)&CPU_prvspace[MAXCPU]) {
		return (TRUE);
	}
	return (FALSE);
}
