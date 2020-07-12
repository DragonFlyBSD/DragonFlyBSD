/*
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_FS_H_
#define _LINUX_FS_H_

#include <linux/wait.h>
#include <linux/cache.h>
#include <linux/stat.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/pid.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/capability.h>
#include <linux/atomic.h>
#include <linux/shrinker.h>
#include <linux/lockdep.h>

#include <sys/file.h>	/* for struct file */
#include <sys/vnode.h>	/* for struct vnode */

struct address_space;

struct poll_table_struct;
struct vm_area_struct;

struct inode;

static inline unsigned imajor(const struct inode *inode)
{
	const struct vnode *vp = (const void *)inode;

	return vp->v_umajor;
}

static inline unsigned iminor(const struct inode *inode)
{
	const struct vnode *vp = (const void *)inode;

	return vp->v_uminor;
}

struct file_operations {
	struct module *owner;
	int (*open) (struct inode *, struct file *);
	int (*release) (struct inode *, struct file *);
	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	loff_t (*llseek) (struct file *, loff_t, int);
};

extern loff_t noop_llseek(struct file *file, loff_t offset, int whence);

static inline unsigned long
invalidate_mapping_pages(struct vm_object *obj, pgoff_t start, pgoff_t end)
{
	int start_count, end_count, clean_only = 1;

	VM_OBJECT_LOCK(obj);
	start_count = obj->resident_page_count;
	/* Only non-dirty pages must be freed or invalidated */
	vm_object_page_remove(obj, start, end, clean_only);
	end_count = obj->resident_page_count;
	VM_OBJECT_UNLOCK(obj);
	return (start_count - end_count);
}

#endif	/* _LINUX_FS_H_ */
