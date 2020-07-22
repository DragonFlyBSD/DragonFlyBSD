/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
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

#include <sys/param.h>
#include <linux/rbtree.h>

int
panic_cmp(struct rb_node *one, struct rb_node *two)
{
	panic("no cmp");
}

RB_GENERATE(linux_root, rb_node, __entry, panic_cmp);

#include <linux/string.h>
#include <linux/slab.h>

void *
kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *dst;

	dst = kmalloc(len, M_DRM, gfp);
	if (dst)
		memcpy(dst, src, len);
	return (dst);
}

#include <linux/fb.h>

struct fb_info *
framebuffer_alloc(size_t size, struct device *dev)
{
	return kzalloc(sizeof(struct fb_info), GFP_KERNEL);
}

void
framebuffer_release(struct fb_info *info)
{
	kfree(info);
}

#include <asm/processor.h>

struct cpuinfo_x86 boot_cpu_data;

static int
init_boot_cpu_data(void *arg)
{
	boot_cpu_data.x86_clflush_size = cpu_clflush_line_size;

	return 0;
}

SYSINIT(boot_cpu_data_init, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, init_boot_cpu_data, NULL);

#include <linux/sched.h>

pid_t get_task_pid(struct task_struct *task, enum pid_type type)
{
	if (task->dfly_td == NULL)
		return -1;

	if (task->dfly_td->td_proc == NULL)
		return -1;

	return task->dfly_td->td_proc->p_pid;
}
