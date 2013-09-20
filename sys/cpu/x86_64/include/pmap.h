/*-
 * Copyright (c) 1991 Regents of the University of California.
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 * from: hp300: @(#)pmap.h	7.2 (Berkeley) 12/16/90
 * from: @(#)pmap.h	7.4 (Berkeley) 5/12/91
 */

#ifndef _CPU_PMAP_H_
#define _CPU_PMAP_H_

/*
 * Page-directory and page-table entries follow this format, with a few
 * of the fields not present here and there, depending on a lot of things.
 */
				/* ---- Intel Nomenclature ---- */
#define	X86_PG_V		0x001	/* P	Valid			*/
#define	X86_PG_RW		0x002	/* R/W	Read/Write		*/
#define	X86_PG_U		0x004	/* U/S  User/Supervisor		*/
#define	X86_PG_NC_PWT		0x008	/* PWT	Write through		*/
#define	X86_PG_NC_PCD		0x010	/* PCD	Cache disable		*/
#define	X86_PG_A		0x020	/* A	Accessed		*/
#define	X86_PG_M		0x040	/* D	Dirty			*/
#define	X86_PG_PS		0x080	/* PS	Page size (0=4k,1=2M)	*/
#define	X86_PG_PTE_PAT		0x080	/* PAT	PAT index		*/
#define	X86_PG_G		0x100	/* G	Global			*/
#define	X86_PG_AVAIL1		0x200	/*    /	Available for system	*/
#define	X86_PG_AVAIL2		0x400	/*   <	programmers use		*/
#define	X86_PG_AVAIL3		0x800	/*    \				*/
#define	X86_PG_PDE_PAT		0x1000	/* PAT	PAT index		*/
#define	X86_PG_NX		(1ul<<63) /* No-execute */


/* Our various interpretations of the above */
//#define PG_W		PG_AVAIL1	/* "Wired" pseudoflag */
//#define	PG_MANAGED	PG_AVAIL2
//#define	PG_DEVICE	PG_AVAIL3
#define	PG_FRAME	(0x000ffffffffff000ul)
#define	PG_PS_FRAME	(0x000fffffffe00000ul)
//#define	PG_PROT		(PG_RW|PG_U)	/* all protection bits . */
//#define PG_N		(PG_NC_PWT|PG_NC_PCD)	/* Non-cacheable */

/*
 * Promotion to a 2MB (PDE) page mapping requires that the corresponding 4KB
 * (PTE) page mappings have identical settings for the following fields:
 */
/*
#define	PG_PTE_PROMOTE	(PG_NX | PG_MANAGED | PG_W | PG_G | PG_PTE_PAT | \
	    PG_M | PG_A | PG_NC_PCD | PG_NC_PWT | PG_U | PG_RW | PG_V)
*/
/*
 * Page Protection Exception bits
 */

#define PGEX_P		0x01	/* Protection violation vs. not present */
#define PGEX_W		0x02	/* during a Write cycle */
#define PGEX_U		0x04	/* access from User mode (UPL) */
#define PGEX_RSV	0x08	/* reserved PTE field is non-zero */
#define PGEX_I		0x10	/* during an instruction fetch */

/*
 * Virtual kernel bits, managed by software.  Stored in tf_xflags.
 *
 * PGEX_FPFAULT - Force the FP unit to generate a T_DNA fault if an
 *		  emulated user process tried to use it.  This bit is
 *		  only used by vmspace_ctl().
 */
#define PGEX_FPFAULT	0x80

#endif /* !_CPU_PMAP_H_ */
