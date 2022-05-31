/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Jeffrey Roberson <jeff@freebsd.org>
 * Copyright (c) 2016 Matthew Macy <mmacy@mattmacy.io>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_GTASKQUEUE_H_
#define _SYS_GTASKQUEUE_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

#include <sys/bus.h>
#include <sys/types.h>
#include <sys/taskqueue.h>

struct gtaskqueue;
struct taskqgroup;

#define TASK_ENQUEUED		0x1
#define TASK_NOENQUEUE		0x2
#define TASK_NETWORK		0x4

#define TASK_IS_NET(ta)		((ta)->ta_flags & TASK_NETWORK)

typedef void gtask_fn_t(void *context);

struct gtask {
	STAILQ_ENTRY(gtask) ta_link;	/* (q) link for queue */
	uint16_t ta_flags;		/* (q) state flags */
	u_short ta_priority;		/* (c) Priority */
	gtask_fn_t *ta_func;		/* (c) task handler */
	void    *ta_context;		/* (c) argument for handler */
};

#define	GROUPTASK_NAMELEN	32

/*
 * Taskqueue groups.  Manages dynamic thread groups and irq binding for
 * device and other tasks.
 */
struct grouptask {
	struct gtask		gt_task;
	void			*gt_taskqueue;
	LIST_ENTRY(grouptask)	gt_list;
	void			*gt_uniq;
	char			gt_name[GROUPTASK_NAMELEN];
	device_t		gt_dev;
	struct resource		*gt_irq;
	int			gt_cpu;
};

void	gtaskqueue_block(struct gtaskqueue *queue);
void	gtaskqueue_unblock(struct gtaskqueue *queue);

int	gtaskqueue_cancel(struct gtaskqueue *queue, struct gtask *gtask);
void	gtaskqueue_drain(struct gtaskqueue *queue, struct gtask *task);
void	gtaskqueue_drain_all(struct gtaskqueue *queue);

void	grouptask_block(struct grouptask *grouptask);
void	grouptask_unblock(struct grouptask *grouptask);
int	grouptaskqueue_enqueue(struct gtaskqueue *queue, struct gtask *task);

void	taskqgroup_attach(struct taskqgroup *qgroup, struct grouptask *grptask,
	    void *uniq, device_t dev, struct resource *irq, const char *name);
int	taskqgroup_attach_cpu(struct taskqgroup *qgroup,
	    struct grouptask *grptask, void *uniq, int cpu, device_t dev,
	    struct resource *irq, const char *name);
void	taskqgroup_detach(struct taskqgroup *qgroup, struct grouptask *gtask);
struct taskqgroup *taskqgroup_create(const char *name, int cnt, int stride);
void	taskqgroup_destroy(struct taskqgroup *qgroup);
void	taskqgroup_bind(struct taskqgroup *qgroup);
void	taskqgroup_drain_all(struct taskqgroup *qgroup);

#define	GTASK_INIT(gtask, flags, priority, func, context) do {	\
	(gtask)->ta_flags = (flags);				\
	(gtask)->ta_priority = (priority);			\
	(gtask)->ta_func = (func);				\
	(gtask)->ta_context = (context);			\
} while (0)

#define	GROUPTASK_INIT(gtask, priority, func, context)	\
	GTASK_INIT(&(gtask)->gt_task, 0, priority, func, context)

#define	GROUPTASK_ENQUEUE(gtask)			\
	grouptaskqueue_enqueue((gtask)->gt_taskqueue, &(gtask)->gt_task)

#define TASKQGROUP_DECLARE(name)			\
extern struct taskqgroup *qgroup_##name

#define TASKQGROUP_DEFINE(name, cnt, stride)				\
									\
struct taskqgroup *qgroup_##name;					\
									\
static void								\
taskqgroup_define_##name(void *arg)					\
{									\
	qgroup_##name = taskqgroup_create(#name, (cnt), (stride));	\
}									\
SYSINIT(taskqgroup_##name, SI_BOOT2_TASKQ, SI_ORDER_FIRST,		\
    taskqgroup_define_##name, NULL);					\
									\
static void								\
taskqgroup_bind_##name(void *arg)					\
{									\
	taskqgroup_bind(qgroup_##name);					\
}									\
SYSINIT(taskqgroup_bind_##name, SI_BOOT2_TASKQ, SI_ORDER_ANY,		\
    taskqgroup_bind_##name, NULL)

TASKQGROUP_DECLARE(softirq);

#endif /* !_SYS_GTASKQUEUE_H_ */
