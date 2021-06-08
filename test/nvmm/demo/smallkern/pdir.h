/*
 * Copyright (c) 2018 The NetBSD Foundation, Inc. All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Maxime Villard.
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

#ifndef PDIR_H_
#define PDIR_H_

#define PAGE_SIZE	4096
#define PDE_SIZE	8
#define FRAMESIZE	8

#define IOM_BEGIN	0x0a0000		/* Start of I/O Memory "hole" */
#define IOM_END		0x100000		/* End of I/O Memory "hole" */
#define IOM_SIZE	(IOM_END - IOM_BEGIN)

#define SMALLKERNBASE		0x0
#define SMALLKERNTEXTOFF	(SMALLKERNBASE + 0x100000)

#define L4_SLOT_SMALLKERN	0
#define L4_SLOT_PTE		255

#define PDIR_SLOT_KERN	L4_SLOT_SMALLKERN
#define PDIR_SLOT_PTE	L4_SLOT_PTE

#define PTE_BASE	((pt_entry_t *)(L4_SLOT_PTE * NBPD_L4))

#define L1_BASE	PTE_BASE
#define L2_BASE	((pd_entry_t *)((char *)L1_BASE + L4_SLOT_PTE * NBPD_L3))
#define L3_BASE	((pd_entry_t *)((char *)L2_BASE + L4_SLOT_PTE * NBPD_L2))
#define L4_BASE	((pd_entry_t *)((char *)L3_BASE + L4_SLOT_PTE * NBPD_L1))

#define PDP_BASE	L4_BASE

#define NKL4_MAX_ENTRIES	(unsigned long)1
#define NKL3_MAX_ENTRIES	(unsigned long)(NKL4_MAX_ENTRIES * 512)
#define NKL2_MAX_ENTRIES	(unsigned long)(NKL3_MAX_ENTRIES * 512)
#define NKL1_MAX_ENTRIES	(unsigned long)(NKL2_MAX_ENTRIES * 512)

#define NKL4_KIMG_ENTRIES	1
#define NKL3_KIMG_ENTRIES	1
#define NKL2_KIMG_ENTRIES	32

#define L1_SHIFT	12
#define L2_SHIFT	21
#define L3_SHIFT	30
#define L4_SHIFT	39
#define NBPD_L1		(1UL << L1_SHIFT) /* # bytes mapped by L1 ent (4K) */
#define NBPD_L2		(1UL << L2_SHIFT) /* # bytes mapped by L2 ent (2MB) */
#define NBPD_L3		(1UL << L3_SHIFT) /* # bytes mapped by L3 ent (1G) */
#define NBPD_L4		(1UL << L4_SHIFT) /* # bytes mapped by L4 ent (512G) */

#define L4_MASK		0x0000ff8000000000
#define L3_MASK		0x0000007fc0000000
#define L2_MASK		0x000000003fe00000
#define L1_MASK		0x00000000001ff000

#define L4_FRAME	L4_MASK
#define L3_FRAME	(L4_FRAME|L3_MASK)
#define L2_FRAME	(L3_FRAME|L2_MASK)
#define L1_FRAME	(L2_FRAME|L1_MASK)

#define pl1_i(va)	(((va) & L1_FRAME) >> L1_SHIFT)
#define pl2_i(va)	(((va) & L2_FRAME) >> L2_SHIFT)
#define pl3_i(va)	(((va) & L3_FRAME) >> L3_SHIFT)
#define pl4_i(va)	(((va) & L4_FRAME) >> L4_SHIFT)

#endif /* !PDIR_H_ */
