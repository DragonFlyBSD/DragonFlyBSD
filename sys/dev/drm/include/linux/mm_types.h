/*
 * Copyright (c) 2018-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_MM_TYPES_H_
#define _LINUX_MM_TYPES_H_

#include <linux/types.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <asm/page.h>

#include <vm/vm_page.h>

struct address_space;

struct page {
	struct vm_page pa_vmpage;
};

struct mm_struct {
	long	refs;
	struct lock mmap_sem;
	atomic_t mm_users;
	atomic_t mm_count;
};

struct proc_struct {
	struct mm_struct *mm;
};

struct vm_area_struct {
	vm_offset_t	vm_start;
	vm_offset_t	vm_end;
	vm_offset_t	vm_pgoff;
	vm_paddr_t	vm_pfn;		/* PFN For mmap. */
	vm_memattr_t	vm_page_prot;
	void		*vm_private_data;
	int		vm_flags;
	const struct vm_operations_struct *vm_ops;
	struct mm_struct *vm_mm;
};

#endif	/* _LINUX_MM_TYPES_H_ */
