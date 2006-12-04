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
 * $DragonFly: src/sys/sys/vkernel.h,v 1.5 2006/12/04 18:04:05 dillon Exp $
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
#ifndef _MACHINE_FRAME_H_
#include <machine/frame.h>
#endif

struct vmspace_rb_tree;
struct vmspace_entry;
RB_PROTOTYPE(vmspace_rb_tree, vmspace_entry, rb_entry, rb_vmspace_compare);

/*
 * Process operating as virtual kernels manage multiple VM spaces.  The
 * original VM space and trap context is saved in the process's vkernel
 * structure.
 */
struct vkernel {
	struct vmspace *vk_save_vmspace;	/* saved VM space */
	struct trapframe vk_save_frame;		/* saved trap frame */
	struct trapframe *vk_user_frame;	/* copyback to user process */
	struct vkernel_common *vk_common;	/* shared data */
	struct vmspace_entry *vk_current;
};

struct vkernel_common {
	RB_HEAD(vmspace_rb_tree, vmspace_entry) vc_root;
	struct spinlock vc_spin;
	int vc_refs;
};

struct vmspace_entry {
	void *id;
	struct vmspace *vmspace;
	int flags;
	int refs;				/* when vk_current */
	RB_ENTRY(vmspace_entry) rb_entry;
};

#ifdef _KERNEL

void vkernel_inherit(struct proc *p1, struct proc *p2);
void vkernel_exit(struct proc *p);
int vkernel_trap(struct proc *p, struct trapframe *frame);

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
 */

typedef u_int32_t	vpte_t;

#define VPTE_PAGE_ENTRIES	(PAGE_SIZE / sizeof(vpte_t))
#define VPTE_PAGE_BITS		10
#define VPTE_PAGE_MASK		((1 << VPTE_PAGE_BITS) - 1)

#define VPTE_V		0x00000001
#define VPTE_PS		0x00000002
#define VPTE_R		0x00000004
#define VPTE_W		0x00000008
#define VPTE_X		0x00000010

#endif

