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

#define	FQ_THREAD_IO_LOCKINIT(x)	lockinit(&(x)->lock, "tdiobioq", 0, LK_CANRECURSE)
#define	FQ_THREAD_IO_LOCK(x)		fq_thread_io_ref((x)); \
					lockmgr(&(x)->lock, LK_EXCLUSIVE)
#define	FQ_THREAD_IO_UNLOCK(x)		lockmgr(&(x)->lock, LK_RELEASE); \
					fq_thread_io_unref((x));

#define	FQ_DISK_CTX_LOCKINIT(x)		lockinit(&(x)->lock, "tdiodiskq", 0, LK_CANRECURSE)
#define	FQ_DISK_CTX_LOCK(x)		fq_disk_ctx_ref((x)); \
					lockmgr(&(x)->lock, LK_EXCLUSIVE)
#define	FQ_DISK_CTX_UNLOCK(x)		lockmgr(&(x)->lock, LK_RELEASE); \
					fq_disk_ctx_unref((x))
#define FQ_DISK_CTX_LOCK_ASSERT(x)	KKASSERT(lockstatus(&(x)->lock, curthread) == LK_EXCLUSIVE)

#define	FQ_GLOBAL_THREAD_CTX_LOCKINIT(x)	lockinit(&fq_tdctx_lock, "tdctxglob", 0, LK_CANRECURSE)
#define	FQ_GLOBAL_THREAD_CTX_LOCK(x)		lockmgr(&fq_tdctx_lock, LK_EXCLUSIVE)
#define	FQ_GLOBAL_THREAD_CTX_UNLOCK(x)	lockmgr(&fq_tdctx_lock, LK_RELEASE)



#define	FQ_THREAD_CTX_LOCKINIT(x)	spin_init(&(x)->lock)
#if 0
#define	FQ_THREAD_IO_LOCKINIT(x)	spin_init(&(x)->lock)
#endif
#if 0
#define	FQ_DISK_CTX_LOCKINIT(x)	spin_init(&(x)->lock)
#endif
#if 0
#define	FQ_GLOBAL_THREAD_CTX_LOCKINIT(x)	spin_init(&fq_tdctx_lock)
#endif

#define	FQ_THREAD_CTX_LOCK(x)		fq_thread_ctx_ref((x)); \
				spin_lock_wr(&(x)->lock)
#if 0
#define	FQ_THREAD_IO_LOCK(x)		fq_thread_io_ref((x)); \
				spin_lock_wr(&(x)->lock)
#endif
#if 0
#define	FQ_DISK_CTX_LOCK(x)	fq_disk_ctx_ref((x)); \
				spin_lock_wr(&(x)->lock)
#endif
#if 0
#define	FQ_GLOBAL_THREAD_CTX_LOCK(x)	spin_lock_wr(&fq_tdctx_lock)
#endif

#define	FQ_THREAD_CTX_UNLOCK(x)	spin_unlock_wr(&(x)->lock); \
				fq_thread_ctx_unref((x))

#if 0
#define	FQ_THREAD_IO_UNLOCK(x)	spin_unlock_wr(&(x)->lock); \
				fq_thread_io_unref((x))
#endif
#if 0
#define	FQ_DISK_CTX_UNLOCK(x)	spin_unlock_wr(&(x)->lock); \
				fq_disk_ctx_unref((x))
#endif
#if 0
#define	FQ_GLOBAL_THREAD_CTX_UNLOCK(x) spin_unlock_wr(&fq_tdctx_lock)
#endif

#define FQ_PRIO_BIAS		5
#define FQ_PRIO_MAX		10
#define FQ_PRIO_MIN		1
#define FQ_PRIO_IDLE		-1
#define	FQ_BUCKET_ACTIVE	0x01

#define	FQ_DRAIN_CANCEL	0x1
#define	FQ_DRAIN_FLUSH	0x2

struct disk;
struct proc;

#define	FQ_LINKED_DISK_CTX	0x01
#define	FQ_LINKED_THREAD_CTX		0x02

struct fq_thread_io {
	TAILQ_ENTRY(fq_thread_io)	link;
	TAILQ_ENTRY(fq_thread_io)	dlink;
	TAILQ_HEAD(, bio)	queue;	/* IO queue (bio) */

	struct lock		lock;
	struct disk		*dp;
	struct fq_disk_ctx	*diskctx;
	struct fq_thread_ctx	*tdctx;
	struct proc		*p;

	int32_t	qlength;	/* IO queue length */
	int32_t	flags;

	int	refcount;
	int32_t	transactions;	/* IOs completed so far during current interval */
	int32_t	avg_latency;	/* avg latency for current interval IOs */
	int32_t	interval_transactions;	/* IOs completed during last interval */
	int32_t	interval_avg_latency;	/* avg latency for last interval IOs */
	int32_t	max_tp;		/* rate limit of transactions per interval */
	int32_t	issued;		/* IOs issued to disk (but not completed) */

	int	rebalance;	/* thread needs to rebalance w/ fq_balance_self */
};

struct fq_disk_ctx {
	struct thread	*td;		/* dispatcher thread td */
	struct thread	*td_balance;	/* balancer thread td */
	struct disk	*dp;		/* back pointer to disk struct */
	struct lock	lock;
	int	refcount;

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

	/* list contains all fq_thread_io for this disk */
	TAILQ_HEAD(, fq_thread_io)	fq_tdio_list;	/* list of thread_io of disk */
	TAILQ_ENTRY(fq_disk_ctx)	link;
};

struct fq_thread_ctx {
	struct proc *p;
	struct thread *td;
	int dead;
	struct spinlock	lock;
	int	refcount;
	TAILQ_HEAD(, fq_thread_io)	fq_tdio_list;	/* list of thread_io */
	TAILQ_ENTRY(fq_thread_ctx)	link;
};





struct fq_thread_io	*fq_thread_io_alloc(struct disk *dp, struct fq_thread_ctx *tdctx);
struct fq_disk_ctx	*fq_disk_ctx_alloc(struct disk *dp);
struct fq_thread_ctx	*fq_thread_ctx_alloc(struct proc *p);
void	fq_balance_thread(struct fq_disk_ctx *diskctx);
void	fq_dispatcher(struct fq_disk_ctx *diskctx);
biodone_t	fq_completed;

void	fq_disk_ctx_ref(struct fq_disk_ctx *diskctx);
void	fq_thread_io_ref(struct fq_thread_io *tdio);
void	fq_thread_ctx_ref(struct fq_thread_ctx *tdctx);
void	fq_disk_ctx_unref(struct fq_disk_ctx *diskctx);
void	fq_thread_io_unref(struct fq_thread_io *tdio);
void	fq_thread_ctx_unref(struct fq_thread_ctx *tdctx);
void	fq_dispatch(struct fq_disk_ctx *diskctx, struct bio *bio,
			struct fq_thread_io *tdio);
void	fq_drain(struct fq_disk_ctx *diskctx, int mode);
void	fq_balance_self(struct fq_thread_io *tdio);
#endif /* _KERNEL || _KERNEL_STRUCTURES */


struct dsched_fq_stats {
	int32_t	tdctx_allocations;
	int32_t	tdio_allocations;
	int32_t	diskctx_allocations;

	int32_t	procs_limited;

	int32_t	transactions;
	int32_t	transactions_completed;
	int32_t	cancelled;

	int32_t	no_tdctx;

	int32_t	nthreads;
	int32_t	nprocs;
};

#endif /* _DSCHED_FQ_H_ */
