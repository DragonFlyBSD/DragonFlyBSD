/*
 * Copyright (c) 1998, 2000, 2007, 2008, 2016, 2017 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Maxime Villard.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define _LOCORE
#define LOCORE /* DragonFly */

#include "asm.h"
#include "pdir.h"

#include <machine/param.h>
#include <machine/segments.h>
#include <machine/specialreg.h>

#define PSL_MBO		0x00000002	/* must be zero bits */

#define PTE_P		0x001	/* P: Valid */
#define PTE_W		0x002	/* R/W: Read/Write */
#define PTE_PCD		0x010	/* PCD: Cache disable */

/* 32bit version of PG_NX */
#define PG_NX32		0x80000000

#define TABLE_L2_ENTRIES (NKL2_KIMG_ENTRIES + 1)
#define TABLE_L3_ENTRIES NKL3_KIMG_ENTRIES

#define PROC0_PML4_OFF	0
#define PROC0_STK_OFF	(PROC0_PML4_OFF + 1 * PAGE_SIZE)
#define PROC0_PTP3_OFF	(PROC0_STK_OFF + UPAGES * PAGE_SIZE)
#define PROC0_PTP2_OFF	(PROC0_PTP3_OFF + NKL4_KIMG_ENTRIES * PAGE_SIZE)
#define PROC0_PTP1_OFF	(PROC0_PTP2_OFF + TABLE_L3_ENTRIES * PAGE_SIZE)
#define TABLESIZE \
  ((NKL4_KIMG_ENTRIES + TABLE_L3_ENTRIES + TABLE_L2_ENTRIES + 1 + UPAGES) \
    * PAGE_SIZE)

/*
 * fillkpt - Fill in a kernel page table
 *	eax = pte (page frame | control | status)
 *	ebx = page table address
 *	ecx = number of pages to map
 *
 * Each entry is 8 (PDE_SIZE) bytes long: we must set the 4 upper bytes to 0.
 */
#define fillkpt	\
	cmpl	$0,%ecx			;	/* zero-sized? */	\
	je 	2f			; \
1:	movl	$0,(PDE_SIZE-4)(%ebx)	;	/* upper 32 bits: 0 */	\
	movl	%eax,(%ebx)		;	/* store phys addr */	\
	addl	$PDE_SIZE,%ebx		;	/* next PTE/PDE */	\
	addl	$PAGE_SIZE,%eax		;	/* next phys page */	\
	loop	1b			; \
2:					;

/*
 * fillkpt_nox - Same as fillkpt, but sets the NX/XD bit.
 */
#define fillkpt_nox \
	cmpl	$0,%ecx			;	/* zero-sized? */	\
	je 	2f			; \
	pushl	%ebp			; \
	movl	_C_LABEL(nox_flag),%ebp	; \
1:	movl	%ebp,(PDE_SIZE-4)(%ebx)	;	/* upper 32 bits: NX */ \
	movl	%eax,(%ebx)		;	/* store phys addr */	\
	addl	$PDE_SIZE,%ebx		;	/* next PTE/PDE */	\
	addl	$PAGE_SIZE,%eax		;	/* next phys page */	\
	loop	1b			; \
	popl	%ebp			; \
2:					;

/*
 * fillkpt_blank - Fill in a kernel page table with blank entries
 *	ebx = page table address
 *	ecx = number of pages to map
 */
#define fillkpt_blank	\
	cmpl	$0,%ecx			;	/* zero-sized? */	\
	je 	2f			; \
1:	movl	$0,(PDE_SIZE-4)(%ebx)	;	/* upper 32 bits: 0 */	\
	movl	$0,(%ebx)		;	/* lower 32 bits: 0 */	\
	addl	$PDE_SIZE,%ebx		;	/* next PTE/PDE */	\
	loop	1b			; \
2:					;

/*
 * Initialization
 */
	.data

	.globl	_C_LABEL(tablesize)
	.globl	_C_LABEL(nox_flag)
	.globl	_C_LABEL(cpuid_level)
	.globl	_C_LABEL(PDPpaddr)
	.globl	_C_LABEL(atdevbase)
	.globl	_C_LABEL(lapicbase)
	.globl	_C_LABEL(stkpa)
	.globl	_C_LABEL(stkva)

	.type	_C_LABEL(tablesize), @object
_C_LABEL(tablesize):	.long	TABLESIZE
END(tablesize)
	.type	_C_LABEL(nox_flag), @object
LABEL(nox_flag)		.long	0	/* 32bit NOX flag, set if supported */
END(nox_flag)
	.type	_C_LABEL(cpuid_level), @object
LABEL(cpuid_level)	.long	-1	/* max. level accepted by cpuid instr */
END(cpuid_level)
	.type	_C_LABEL(PDPpaddr), @object
LABEL(PDPpaddr)		.quad	0
END(PDPpaddr)
	.type	_C_LABEL(atdevbase), @object
LABEL(atdevbase)	.quad	0	/* location of start of iomem in virt */
END(atdevbase)
	.type	_C_LABEL(lapicbase), @object
LABEL(lapicbase)	.quad	0	/* location of start of lapic in virt */
END(lapicbase)
	.type	_C_LABEL(stkpa), @object
LABEL(stkpa)		.quad	0
END(stkpa)
	.type	_C_LABEL(stkva), @object
LABEL(stkva)		.quad	0
END(stkva)

	.globl	gdt64_lo
	.globl	gdt64_start

#define GDT64_LIMIT gdt64_end-gdt64_start-1
/* Temporary gdt64, with base address in low memory */
	.type	_C_LABEL(gdt64_lo), @object
LABEL(gdt64_lo)
	.word	GDT64_LIMIT
	.quad	gdt64_start
END(gdt64_lo)
.align 64
#undef GDT64_LIMIT

	.type	_C_LABEL(gdt64_start), @object
LABEL(gdt64_start)
	.quad 0x0000000000000000	/* always empty */
	.quad 0x00af9a000000ffff	/* kernel CS */
	.quad 0x00cf92000000ffff	/* kernel DS */
	.quad 0x0000000000000000	/* kernel TSS [1/2] */
	.quad 0x0000000000000000	/* kernel TSS [2/2] */
END(gdt64_start)
gdt64_end:

	.type	_C_LABEL(farjmp64), @object
_C_LABEL(farjmp64):
	.long	longmode
	.word	GSEL(GCODE_SEL, SEL_KPL)
END(farjmp64)

	/* Space for the temporary stack */
	.size	tmpstk, tmpstk - .
	.space	512
tmpstk:

	.text

ENTRY(start)
	.code32

	xorl	%eax,%eax

	/* Switch to new stack now. */
	movl	$_C_LABEL(tmpstk),%esp

	/* First, reset the PSL. */
	pushl	$PSL_MBO
	popfl

	xorl	%eax,%eax
	cpuid
	movl	%eax,_C_LABEL(cpuid_level)

	/*
	 * Retrieve the NX/XD flag. We use the 32bit version of PG_NX.
	 */
	movl	$0x80000001,%eax
	cpuid
	andl	$CPUID_NOX,%edx
	jz	.Lno_NOX
	movl	$PG_NX32,_C_LABEL(nox_flag)
.Lno_NOX:

/*
 * There are four levels of pages in amd64: PML4 -> PDP -> PD -> PT. They will
 * be referred to as: L4 -> L3 -> L2 -> L1.
 *
 * Physical address space:
 * +-----------------+------------------+
 * | SMALLKERN IMAGE | BOOTSTRAP TABLES |
 * +-----------------+------------------+
 *                  (1)
 *
 * Virtual address space of the smallkern:
 * +-----------------+------------------+
 * | SMALLKERN IMAGE | BOOTSTRAP TABLES |
 * +-----------------+------------------+
 *
 * PROC0 STK is obviously not linked as a page level. It just happens to be
 * caught between L4 and L3.
 *
 * (PROC0 STK + L4 + L3 + L2 + L1) is later referred to as BOOTSTRAP TABLES.
 *
 * Important note: the kernel segments are properly 4k-aligned
 * (see kern.ldscript), so there's no need to enforce alignment.
 */

	/* Find end of the kernel image; brings us on (1). */
	movl	$_C_LABEL(__smallkern_end),%edi

	/* We are on (1). Align up for LAPIC PAGE. */
	movl	%edi,%esi
	addl	$PGOFSET,%esi
	andl	$~PGOFSET,%esi

	/* We are on the BOOTSTRAP TABLES. Save L4's physical address. */
	movl	$_C_LABEL(PDPpaddr),%ebp
	movl	%esi,(%ebp)
	movl	$0,4(%ebp)

	/* Now, zero out the BOOTSTRAP TABLES (before filling them in). */
	movl	%esi,%edi
	xorl	%eax,%eax
	cld
	movl	$TABLESIZE,%ecx
	shrl	$2,%ecx
	rep
	stosl				/* copy eax -> edi */

/*
 * Build the page tables and levels. We go from L1 to L4, and link the levels
 * together.
 */
	/*
	 * Build L1.
	 */
	leal	(PROC0_PTP1_OFF)(%esi),%ebx

	/* Skip the area below the smallkern text. */
	movl	$(SMALLKERNTEXTOFF - SMALLKERNBASE),%ecx
	shrl	$PGSHIFT,%ecx
	fillkpt_blank

	/* Map the smallkern text RX. */
	movl	$(SMALLKERNTEXTOFF - SMALLKERNBASE),%eax /* start of TEXT */
	movl	$_C_LABEL(__rodata_start),%ecx
	subl	%eax,%ecx
	shrl	$PGSHIFT,%ecx
	orl	$(PTE_P),%eax
	fillkpt

	/* Map the smallkern rodata R. */
	movl	$_C_LABEL(__rodata_start),%eax
	movl	$_C_LABEL(__data_start),%ecx
	subl	%eax,%ecx
	shrl	$PGSHIFT,%ecx
	orl	$(PTE_P),%eax
	fillkpt_nox

	/* Map the smallkern data+bss RW. */
	movl	$_C_LABEL(__data_start),%eax
	movl	$_C_LABEL(__smallkern_end),%ecx
	subl	%eax,%ecx
	shrl	$PGSHIFT,%ecx
	orl	$(PTE_P|PTE_W),%eax
	fillkpt_nox

	/* Map some blank space, to keep pa = va. */
	movl	$_C_LABEL(__smallkern_end),%eax
	movl	%esi,%ecx		/* start of BOOTSTRAP TABLES */
	subl	%eax,%ecx
	shrl	$PGSHIFT,%ecx
	fillkpt_blank

	/* Map the BOOTSTRAP TABLES RW. */
	movl	%esi,%eax		/* start of BOOTSTRAP TABLES */
	movl	$TABLESIZE,%ecx		/* length of BOOTSTRAP TABLES */
	shrl	$PGSHIFT,%ecx
	orl	$(PTE_P|PTE_W),%eax
	fillkpt_nox

	/* Map the LAPIC PAGE RW. */
	movl	$0xfee00000,%eax
	movl	$PAGE_SIZE,%ecx	/* size of the LAPIC PAGE */
	shrl	$PGSHIFT,%ecx
	orl	$(PTE_P|PTE_W/*|PTE_PCD*/),%eax
	fillkpt_nox

	/* Map the ISA I/O MEM RW. */
	movl	$IOM_BEGIN,%eax
	movl	$IOM_SIZE,%ecx	/* size of ISA I/O MEM */
	shrl	$PGSHIFT,%ecx
	orl	$(PTE_P|PTE_W/*|PTE_PCD*/),%eax
	fillkpt_nox

	/*
	 * Build L2. Linked to L1.
	 */
	leal	(PROC0_PTP2_OFF)(%esi),%ebx
	leal	(PROC0_PTP1_OFF)(%esi),%eax
	orl	$(PTE_P|PTE_W),%eax
	movl	$(NKL2_KIMG_ENTRIES+1),%ecx
	fillkpt

	/*
	 * Build L3. Linked to L2.
	 */
	leal	(PROC0_PTP3_OFF)(%esi),%ebx
	leal	(PROC0_PTP2_OFF)(%esi),%eax
	orl	$(PTE_P|PTE_W),%eax
	movl	$NKL3_KIMG_ENTRIES,%ecx
	fillkpt

	/*
	 * Build L4. Linked to L3.
	 */
	leal	(PROC0_PML4_OFF)(%esi),%ebx
	leal	(PROC0_PTP3_OFF)(%esi),%eax
	orl	$(PTE_P|PTE_W),%eax
	movl	$NKL4_KIMG_ENTRIES,%ecx
	fillkpt

	/* Install recursive top level PDE (one entry) */
	leal	(PROC0_PML4_OFF + PDIR_SLOT_PTE * PDE_SIZE)(%esi),%ebx
	leal	(PROC0_PML4_OFF)(%esi),%eax
	orl	$(PTE_P|PTE_W),%eax
	movl	$1,%ecx
	fillkpt_nox

	/*
	 * Startup checklist:
	 * 1. Enable PAE (and SSE while here).
	 */
	movl	%cr4,%eax
	orl	$(CR4_PAE|CR4_OSFXSR|CR4_OSXMMEXCPT),%eax
	movl	%eax,%cr4

	/*
	 * 2. Set Long Mode Enable in EFER. Also enable the syscall extensions,
	 *    and NOX if available.
	 */
	movl	$MSR_EFER,%ecx
	rdmsr
	xorl	%eax,%eax
	orl	$(EFER_LME|EFER_SCE),%eax
	movl	_C_LABEL(nox_flag),%ebx
	cmpl	$0,%ebx
	je 	.Lskip_NOX
	orl	$(EFER_NXE),%eax
.Lskip_NOX:
	wrmsr

	/*
	 * 3. Load %cr3 with pointer to PML4.
	 */
	movl	%esi,%eax
	movl	%eax,%cr3

	/*
	 * 4. Enable paging and the rest of it.
	 */
	movl	%cr0,%eax
	orl	$(CR0_PE|CR0_PG|CR0_NE|CR0_TS|CR0_MP|CR0_WP|CR0_AM),%eax
	movl	%eax,%cr0
	jmp	compat
compat:

	/*
	 * 5. Not quite done yet, we're now in a compatibility segment, in
	 *    legacy mode. We must jump to a long mode segment. Need to set up
	 *    a GDT with a long mode segment in it to do that.
	 */
	movl	$_C_LABEL(gdt64_lo),%eax
	lgdt	(%eax)
	movl	$_C_LABEL(farjmp64),%eax
	ljmp	*(%eax)

	.code64
longmode:

	/*
	 * We have arrived. Everything is identity-mapped.
	 */

	/* Store lapicbase. */
	movq	$TABLESIZE,%rdx
	addq	%rsi,%rdx
	movq	%rdx,_C_LABEL(lapicbase)(%rip)

	/* Store atdevbase. */
	addq	$PAGE_SIZE,%rdx
	movq	%rdx,_C_LABEL(atdevbase)(%rip)

	/* Set up bootstrap stack. */
	leaq	(PROC0_STK_OFF)(%rsi),%rax
	movq	%rax,_C_LABEL(stkpa)(%rip)
	leaq	(USPACE-FRAMESIZE)(%rax),%rsp
	xorq	%rbp,%rbp			/* mark end of frames */

	xorw	%ax,%ax
	movw	%ax,%gs
	movw	%ax,%fs

	/* The first physical page available. */
	leaq	(TABLESIZE)(%rsi),%rdi

	/*
	 * Continue execution in C.
	 */
	call	_C_LABEL(main)

	ret
END(start)

/* -------------------------------------------------------------------------- */

ENTRY(lidt)
	lidt	(%rdi)
	ret
END(lidt)

ENTRY(vmmcall)
	vmmcall
END(vmmcall)

ENTRY(outsb)
	movl	%edx,%ecx
	movl	%edi,%edx
	rep
	outsb
	ret
END(outsb)

ENTRY(clts)
	clts
	ret
END(clts)

ENTRY(sti)
	sti
	ret
END(sti)

ENTRY(lcr8)
	movq	%rdi, %cr8
	ret
END(lcr8)

ENTRY(rdmsr)
	movq	%rdi, %rcx
	xorq	%rax, %rax
	rdmsr
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
END(rdmsr)

ENTRY(cpuid)
	movq	%rbx,%r8
	movq	%rdi,%rax
	movq	%rsi,%rcx
	movq	%rdx,%rsi
	cpuid
	movl	%eax,0(%rsi)
	movl	%ebx,4(%rsi)
	movl	%ecx,8(%rsi)
	movl	%edx,12(%rsi)
	movq	%r8,%rbx
	ret
END(cpuid)
