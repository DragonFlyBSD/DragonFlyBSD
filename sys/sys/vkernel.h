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
 */

#ifndef _SYS_VKERNEL_H_
#define _SYS_VKERNEL_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
/*
 * KERNEL ONLY DEFINITIONS
 */

#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#include <machine/frame.h>
#include <machine/vframe.h>
#include <machine/limits.h>

struct vmspace_rb_tree;
struct vmspace_entry;
RB_PROTOTYPE(vmspace_rb_tree, vmspace_entry, rb_entry, rb_vmspace_compare);

/*
 * Process operating as virtual kernels manage multiple VM spaces.  The
 * original VM space and trap context is saved in the process's vkernel
 * structure.
 */
struct vkernel_lwp {
	struct trapframe save_trapframe;	/* swapped context */
	struct vextframe save_vextframe;
	struct trapframe *user_trapframe;	/* copyback to vkernel */
	struct vextframe *user_vextframe;
	struct vmspace_entry *ve;
};

struct vkernel_proc {
	RB_HEAD(vmspace_rb_tree, vmspace_entry) root;
	struct lwkt_token token;
	int refs;
	register_t vkernel_cr3;
};

struct vmspace_entry {
	void *id;
	struct vmspace *vmspace;
	int flags;
	int refs;				/* current LWP assignments */
	RB_ENTRY(vmspace_entry) rb_entry;
};

#define VKE_DELETED	0x0001

#ifdef _KERNEL

void vkernel_inherit(struct proc *p1, struct proc *p2);
void vkernel_exit(struct proc *p);
void vkernel_lwp_exit(struct lwp *lp);
void vkernel_trap(struct lwp *lp, struct trapframe *frame);

#endif

#else
/*
 * USER ONLY DEFINITIONS
 */

#ifndef _MACHINE_PARAM_H_
#include <machine/param.h>
#endif

#endif

/*
 * KERNEL AND USER DEFINITIONS
 *
 * WARNING: vpte_t is 64 bits on a 64-bit box and 32 bits on a 32 bit box.
 *	    A 2-layer page table is used on 32 bit boxes and a 4-layer
 *	    page table is used on 64 bit boxes.
 */
typedef u_long	vpte_t;

#if LONG_BIT == 32
#define VPTE_FRAME_END		32
#define VPTE_PAGE_BITS		10
#define VPTE_FRAME		0xFFFFF000L
#elif LONG_BIT == 64
#define VPTE_FRAME_END		48
#define VPTE_PAGE_BITS		9
#define VPTE_FRAME		0x000FFFFFFFFFF000L
#else
#error "LONG_BIT not defined"
#endif

#define VPTE_PAGE_ENTRIES	(PAGE_SIZE / sizeof(vpte_t))
#define VPTE_PAGE_MASK		((1 << VPTE_PAGE_BITS) - 1)
#define VPTE_PAGETABLE_SIZE	PAGE_SIZE

#define VPTE_V		0x00000001	/* valid */
#define VPTE_RW		0x00000002	/* read/write */
#define VPTE_U		0x00000004	/* user access bit if managed vmspace */

#define VPTE_A		0x00000020	/* page accessed bit */
#define VPTE_M		0x00000040	/* page modified bit */
#define VPTE_PS		0x00000080	/* page directory direct mapping */

#define VPTE_G		0x00000100	/* global bit ?? */
#define VPTE_WIRED	0x00000200	/* wired */
#define VPTE_MANAGED	0x00000400	/* managed bit ?? */


#endif
