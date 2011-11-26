/*
 * Copyright (c) 2009, 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#ifndef	_DSCHED_FQ_H_
#define	_DSCHED_FQ_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_BIO_H_
#include <sys/bio.h>
#endif
#ifndef _SYS_BIOTRACK_H_
#include <sys/biotrack.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

#define FQ_PRIO_BIAS		5
#define FQ_PRIO_MAX		10
#define FQ_PRIO_MIN		1
#define FQ_PRIO_IDLE		-1
#define	FQ_BUCKET_ACTIVE	0x01

#define FQ_DISPATCH_SML_ARRAY_SZ	128
#define FQ_DISPATCH_ARRAY_SZ	512

#define	FQ_DRAIN_CANCEL	0x1
#define	FQ_DRAIN_FLUSH	0x2

struct disk;
struct proc;

struct fq_disk_ctx {
	struct dsched_disk_ctx head;

	struct thread	*td;		/* dispatcher thread td */
	struct thread	*td_balance;	/* balancer thread td */
	int	avg_rq_time;		/* XXX: not yet used */
	int32_t	incomplete_tp;		/* IOs issued but not completed */
	int	idle;			/* disk idle ? */
	struct timeval start_idle;	/* disk idleness start time */
	int	idle_time;		/* aggregate idle time in interval */
	int	die;			/* flag to kill related threads */
	struct timeval start_interval;	/* current interval start time */

	int	prev_full;		/* disk >90% busy during prev. to last
					   interval? */
	int	last_full;		/* disk >90% busy during last interval */
	int	disk_busy;		/* disk >90% busy during cur. interval */
	int64_t	budgetpb[FQ_PRIO_MAX+1];/* next interval budget for each thread
					   in each prio */
};

struct fq_thread_io {
	struct dsched_thread_io head;

	int32_t	transactions;	/* IOs completed so far during current interval */
	int32_t	avg_latency;	/* avg latency for current interval IOs */
	int32_t	interval_transactions;	/* IOs completed during last interval */
	int32_t	interval_avg_latency;	/* avg latency for last interval IOs */
	int32_t	max_tp;		/* rate limit of transactions per interval */
	int32_t	issued;		/* IOs issued to disk (but not completed) */

	int	rebalance;	/* thread needs to rebalance w/ fq_balance_self */
};

struct dispatch_prep {
	struct fq_thread_io	*tdio;
	struct bio		*bio;
};


void	fq_balance_thread(struct fq_disk_ctx *diskctx);
void	fq_dispatcher(struct fq_disk_ctx *diskctx);
biodone_t	fq_completed;

void	fq_dispatch(struct fq_disk_ctx *diskctx, struct bio *bio,
			struct fq_thread_io *tdio);
void	fq_drain(struct fq_disk_ctx *diskctx, int mode);
void	fq_balance_self(struct fq_thread_io *tdio);
#endif /* _KERNEL || _KERNEL_STRUCTURES */


struct dsched_fq_stats {
	int32_t	procs_limited;

	int32_t	transactions;
	int32_t	transactions_completed;
	int32_t	cancelled;
};

#endif /* _DSCHED_FQ_H_ */
