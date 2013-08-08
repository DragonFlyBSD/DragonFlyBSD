/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 2003 Peter Wemm
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)vmparam.h	5.9 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/amd64/include/vmparam.h,v 1.44 2003/12/07 04:51:04 alc Exp $
 * $DragonFly: src/sys/platform/pc64/include/vmparam.h,v 1.2 2008/08/29 17:07:17 dillon Exp $
 */


#ifndef _MACHINE_VMPARAM_H_
#define	_MACHINE_VMPARAM_H_

/*
 * Machine dependent constants for x86_64.
 */

/*
 * Virtual memory related constants, all in bytes
 */
#define	MAXTSIZ		(128UL*1024*1024)	/* max text size */
#ifndef DFLDSIZ
#define	DFLDSIZ		(128UL*1024*1024)	/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(32768UL*1024*1024)	/* max data size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(8UL*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(512UL*1024*1024)	/* max stack size */
#endif
#ifndef SGROWSIZ
#define	SGROWSIZ	(128UL*1024)		/* amount to grow stack */
#endif

/*
 * The time for a process to be blocked before being very swappable.
 * This is a number of seconds which the system takes as being a non-trivial
 * amount of real time.  You probably shouldn't change this;
 * it is used in subtle ways (fractions and multiples of it are, that is, like
 * half of a ``long time'', almost a long time, etc.)
 * It is related to human patience and other factors which don't really
 * change over time.
 */
#define	MAXSLP 		20

/*
 * We provide a machine specific single page allocator through the use
 * of the direct mapped segment.  This uses 2MB pages for reduced
 * TLB pressure.
 */
#define	UMA_MD_SMALL_ALLOC

/*
 * The number of PHYSSEG entries must be one greater than the number
 * of phys_avail entries because the phys_avail entry that spans the
 * largest physical address that is accessible by ISA DMA is split
 * into two PHYSSEG entries. 
 */
#define	VM_PHYSSEG_MAX		31

/*
 * Virtual addresses of things.  Derived from the page directory and
 * page table indexes from pmap.h for precision.
 * Because of the page that is both a PD and PT, it looks a little
 * messy at times, but hey, we'll do anything to save a page :-)
 *
 * The kernel address space can be up to (I think) 511 page directory
 * pages.  Each one represents 1G.  NKPDPE defines the size of the kernel
 * address space, curently set to 128G.
 *
 * The ending address is non-inclusive of the per-cpu data array
 * which starts at MPPTDI (-16MB mark).  MPPTDI is the page directory
 * index in the last PD of the kernel address space and is typically
 * set to (NPDEPG - 8) = (512 - 8).
 */
#define NKPDPE			128
#define	VM_MIN_KERNEL_ADDRESS	KVADDR(KPML4I, NPDPEPG - NKPDPE, 0, 0)
#define	VM_MAX_KERNEL_ADDRESS	KVADDR(KPML4I, NPDPEPG - 1, MPPTDI, NPTEPG - 1)

#define	DMAP_MIN_ADDRESS	KVADDR(DMPML4I, 0, 0, 0)
#define	DMAP_MAX_ADDRESS	KVADDR(DMPML4I+NDMPML4E, 0, 0, 0)

#define	KERNBASE		KVADDR(KPML4I, KPDPI, 0, 0)
#define	PTOV_OFFSET		KERNBASE

#define UPT_MAX_ADDRESS		KVADDR(PML4PML4I, PML4PML4I, PML4PML4I, PML4PML4I)
#define UPT_MIN_ADDRESS		KVADDR(PML4PML4I, 0, 0, 0)

#define VM_MIN_USER_ADDRESS	((vm_offset_t)0)
#define VM_MAX_USER_ADDRESS	UVADDR(NUPDP_USER, 0, 0, 0)

#define USRSTACK		VM_MAX_USER_ADDRESS

#define VM_MAX_ADDRESS		UPT_MAX_ADDRESS
#define VM_MIN_ADDRESS		(0)

#define	PHYS_TO_DMAP(x)		((vm_offset_t)(x) | DMAP_MIN_ADDRESS)
#define	DMAP_TO_PHYS(x)		((vm_paddr_t)(x) & ~DMAP_MIN_ADDRESS)

/* initial pagein size of beginning of executable file */
#ifndef VM_INITIAL_PAGEIN
#define	VM_INITIAL_PAGEIN	16
#endif

#endif /* _MACHINE_VMPARAM_H_ */
