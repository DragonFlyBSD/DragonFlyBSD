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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <machine/atomic.h>
#include <sys/malloc.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/spinlock2.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>
#include <sys/dsched.h>
#include <machine/varargs.h>
#include <machine/param.h>

#include <dsched/fq/dsched_fq.h>

MALLOC_DECLARE(M_DSCHEDFQ);

static int	dsched_fq_version_maj = 0;
static int	dsched_fq_version_min = 8;

struct dsched_fq_stats	fq_stats;

struct objcache_malloc_args fq_disk_ctx_malloc_args = {
	sizeof(struct fq_disk_ctx), M_DSCHEDFQ };
struct objcache_malloc_args fq_thread_io_malloc_args = {
	sizeof(struct fq_thread_io), M_DSCHEDFQ };
struct objcache_malloc_args fq_thread_ctx_malloc_args = {
	sizeof(struct fq_thread_ctx), M_DSCHEDFQ };

static struct objcache	*fq_diskctx_cache;
static struct objcache	*fq_tdctx_cache;
static struct objcache	*fq_tdio_cache;

TAILQ_HEAD(, fq_thread_ctx)	dsched_tdctx_list =
		TAILQ_HEAD_INITIALIZER(dsched_tdctx_list);

struct lock	fq_tdctx_lock;

extern struct dsched_policy dsched_fq_policy;

void
fq_disk_ctx_ref(struct fq_disk_ctx *diskctx)
{
	int refcount;

	refcount = atomic_fetchadd_int(&diskctx->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_thread_io_ref(struct fq_thread_io *tdio)
{
	int refcount;

	refcount = atomic_fetchadd_int(&tdio->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_thread_ctx_ref(struct fq_thread_ctx *tdctx)
{
	int refcount;

	refcount = atomic_fetchadd_int(&tdctx->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_disk_ctx_unref(struct fq_disk_ctx *diskctx)
{
	struct fq_thread_io	*tdio, *tdio2;
	int refcount;

	refcount = atomic_fetchadd_int(&diskctx->refcount, -1);


	KKASSERT(refcount >= 0 || refcount <= -0x400);

	if (refcount == 1) {
		atomic_subtract_int(&diskctx->refcount, 0x400); /* mark as: in destruction */
#if 1
		kprintf("diskctx (%p) destruction started, trace:\n", diskctx);
		print_backtrace(4);
#endif
		lockmgr(&diskctx->lock, LK_EXCLUSIVE);
		TAILQ_FOREACH_MUTABLE(tdio, &diskctx->fq_tdio_list, dlink, tdio2) {
			TAILQ_REMOVE(&diskctx->fq_tdio_list, tdio, dlink);
			tdio->flags &= ~FQ_LINKED_DISK_CTX;
			fq_thread_io_unref(tdio);
		}
		lockmgr(&diskctx->lock, LK_RELEASE);

		objcache_put(fq_diskctx_cache, diskctx);
		atomic_subtract_int(&fq_stats.diskctx_allocations, 1);
	}
}

void
fq_thread_io_unref(struct fq_thread_io *tdio)
{
	struct fq_thread_ctx	*tdctx;
	struct fq_disk_ctx	*diskctx;
	int refcount;

	refcount = atomic_fetchadd_int(&tdio->refcount, -1);

	KKASSERT(refcount >= 0 || refcount <= -0x400);

	if (refcount == 1) {
		atomic_subtract_int(&tdio->refcount, 0x400); /* mark as: in destruction */
#if 0
		kprintf("tdio (%p) destruction started, trace:\n", tdio);
		print_backtrace(8);
#endif
		diskctx = tdio->diskctx;
		KKASSERT(diskctx != NULL);
		KKASSERT(tdio->qlength == 0);

		if (tdio->flags & FQ_LINKED_DISK_CTX) {
			lockmgr(&diskctx->lock, LK_EXCLUSIVE);

			TAILQ_REMOVE(&diskctx->fq_tdio_list, tdio, dlink);
			tdio->flags &= ~FQ_LINKED_DISK_CTX;

			lockmgr(&diskctx->lock, LK_RELEASE);
		}

		if (tdio->flags & FQ_LINKED_THREAD_CTX) {
			tdctx = tdio->tdctx;
			KKASSERT(tdctx != NULL);

			spin_lock_wr(&tdctx->lock);

			TAILQ_REMOVE(&tdctx->fq_tdio_list, tdio, link);
			tdio->flags &= ~FQ_LINKED_THREAD_CTX;

			spin_unlock_wr(&tdctx->lock);
		}

		objcache_put(fq_tdio_cache, tdio);
		atomic_subtract_int(&fq_stats.tdio_allocations, 1);
#if 0
		fq_disk_ctx_unref(diskctx);
#endif
	}
}

void
fq_thread_ctx_unref(struct fq_thread_ctx *tdctx)
{
	struct fq_thread_io	*tdio, *tdio2;
	int refcount;

	refcount = atomic_fetchadd_int(&tdctx->refcount, -1);

	KKASSERT(refcount >= 0 || refcount <= -0x400);

	if (refcount == 1) {
		atomic_subtract_int(&tdctx->refcount, 0x400); /* mark as: in destruction */
#if 0
		kprintf("tdctx (%p) destruction started, trace:\n", tdctx);
		print_backtrace(8);
#endif
		FQ_GLOBAL_THREAD_CTX_LOCK();

		TAILQ_FOREACH_MUTABLE(tdio, &tdctx->fq_tdio_list, link, tdio2) {
			TAILQ_REMOVE(&tdctx->fq_tdio_list, tdio, link);
			tdio->flags &= ~FQ_LINKED_THREAD_CTX;
			fq_thread_io_unref(tdio);
		}
		TAILQ_REMOVE(&dsched_tdctx_list, tdctx, link);

		FQ_GLOBAL_THREAD_CTX_UNLOCK();

		objcache_put(fq_tdctx_cache, tdctx);
		atomic_subtract_int(&fq_stats.tdctx_allocations, 1);
	}
}


struct fq_thread_io *
fq_thread_io_alloc(struct disk *dp, struct fq_thread_ctx *tdctx)
{
	struct fq_thread_io	*tdio;
#if 0
	fq_disk_ctx_ref(dsched_get_disk_priv(dp));
#endif
	tdio = objcache_get(fq_tdio_cache, M_WAITOK);
	bzero(tdio, sizeof(struct fq_thread_io));

	/* XXX: maybe we do need another ref for the disk list for tdio */
	fq_thread_io_ref(tdio);

	FQ_THREAD_IO_LOCKINIT(tdio);
	tdio->dp = dp;

	tdio->diskctx = dsched_get_disk_priv(dp);
	TAILQ_INIT(&tdio->queue);

	TAILQ_INSERT_TAIL(&tdio->diskctx->fq_tdio_list, tdio, dlink);
	tdio->flags |= FQ_LINKED_DISK_CTX;

	if (tdctx) {
		tdio->tdctx = tdctx;
		tdio->p = tdctx->p;

		/* Put the tdio in the tdctx list */
		FQ_THREAD_CTX_LOCK(tdctx);
		TAILQ_INSERT_TAIL(&tdctx->fq_tdio_list, tdio, link);
		FQ_THREAD_CTX_UNLOCK(tdctx);
		tdio->flags |= FQ_LINKED_THREAD_CTX;
	}

	atomic_add_int(&fq_stats.tdio_allocations, 1);
	return tdio;
}


struct fq_disk_ctx *
fq_disk_ctx_alloc(struct disk *dp)
{
	struct fq_disk_ctx *diskctx;

	diskctx = objcache_get(fq_diskctx_cache, M_WAITOK);
	bzero(diskctx, sizeof(struct fq_disk_ctx));
	fq_disk_ctx_ref(diskctx);
	diskctx->dp = dp;
	diskctx->avg_rq_time = 0;
	diskctx->incomplete_tp = 0;
	FQ_DISK_CTX_LOCKINIT(diskctx);
	TAILQ_INIT(&diskctx->fq_tdio_list);

	atomic_add_int(&fq_stats.diskctx_allocations, 1);
	return diskctx;
}


struct fq_thread_ctx *
fq_thread_ctx_alloc(struct proc *p)
{
	struct fq_thread_ctx	*tdctx;
	struct fq_thread_io	*tdio;
	struct disk	*dp = NULL;

	tdctx = objcache_get(fq_tdctx_cache, M_WAITOK);
	bzero(tdctx, sizeof(struct fq_thread_ctx));
	fq_thread_ctx_ref(tdctx);
#if 0
	kprintf("fq_thread_ctx_alloc, new tdctx = %p\n", tdctx);
#endif
	FQ_THREAD_CTX_LOCKINIT(tdctx);
	TAILQ_INIT(&tdctx->fq_tdio_list);
	tdctx->p = p;

	while ((dp = dsched_disk_enumerate(dp, &dsched_fq_policy))) {
		tdio = fq_thread_io_alloc(dp, tdctx);
#if 0
		fq_thread_io_ref(tdio);
#endif
	}

	FQ_GLOBAL_THREAD_CTX_LOCK();
	TAILQ_INSERT_TAIL(&dsched_tdctx_list, tdctx, link);
	FQ_GLOBAL_THREAD_CTX_UNLOCK();

	atomic_add_int(&fq_stats.tdctx_allocations, 1);
	return tdctx;
}


void
fq_dispatcher(struct fq_disk_ctx *diskctx)
{
	struct fq_thread_ctx	*tdctx;
	struct fq_thread_io	*tdio, *tdio2;
	struct bio *bio, *bio2;
	int idle;

	/*
	 * We need to manually assign an tdio to the tdctx of this thread
	 * since it isn't assigned one during fq_prepare, as the disk
	 * is not set up yet.
	 */
	tdctx = dsched_get_thread_priv(curthread);
	KKASSERT(tdctx != NULL);

	tdio = fq_thread_io_alloc(diskctx->dp, tdctx);
#if 0
	fq_thread_io_ref(tdio);
#endif

	FQ_DISK_CTX_LOCK(diskctx);
	for(;;) {
		idle = 0;
		/* sleep ~60 ms */
		if ((lksleep(diskctx, &diskctx->lock, 0, "fq_dispatcher", hz/15) == 0)) {
			/*
			 * We've been woken up; this either means that we are
			 * supposed to die away nicely or that the disk is idle.
			 */

			if (__predict_false(diskctx->die == 1)) {
				/* If we are supposed to die, drain all queues */
				fq_drain(diskctx, FQ_DRAIN_FLUSH);

				/* Now we can safely unlock and exit */
				FQ_DISK_CTX_UNLOCK(diskctx);
				kprintf("fq_dispatcher is peacefully dying\n");
				lwkt_exit();
				/* NOTREACHED */
			}

			/*
			 * We have been awakened because the disk is idle.
			 * So let's get ready to dispatch some extra bios.
			 */
			idle = 1;
		}

		/* Maybe the disk is idle and we just didn't get the wakeup */
		if (idle == 0)
			idle = diskctx->idle;

		/*
		 * XXX: further room for improvements here. It would be better
		 *	to dispatch a few requests from each tdio as to ensure
		 *	real fairness.
		 */
		TAILQ_FOREACH_MUTABLE(tdio, &diskctx->fq_tdio_list, dlink, tdio2) {
			if (tdio->qlength == 0)
				continue;

			FQ_THREAD_IO_LOCK(tdio);
			if (atomic_cmpset_int(&tdio->rebalance, 1, 0))
				fq_balance_self(tdio);
			/*
			 * XXX: why 5 extra? should probably be dynamic,
			 *	relying on information on latency.
			 */
			if ((tdio->max_tp > 0) && idle &&
			    (tdio->issued >= tdio->max_tp)) {
				tdio->max_tp += 5;
			}

			TAILQ_FOREACH_MUTABLE(bio, &tdio->queue, link, bio2) {
				if (atomic_cmpset_int(&tdio->rebalance, 1, 0))
					fq_balance_self(tdio);
				if ((tdio->max_tp > 0) &&
				    ((tdio->issued >= tdio->max_tp)))
					break;

				TAILQ_REMOVE(&tdio->queue, bio, link);
				--tdio->qlength;

				/*
				 * beware that we do have an tdio reference
				 * from the queueing
				 */
				fq_dispatch(diskctx, bio, tdio);
			}
			FQ_THREAD_IO_UNLOCK(tdio);

		}
	}
}

void
fq_balance_thread(struct fq_disk_ctx *diskctx)
{
	struct	fq_thread_io	*tdio, *tdio2;
	struct timeval tv, old_tv;
	int64_t	total_budget, product;
	int64_t budget[FQ_PRIO_MAX+1];
	int	n, i, sum, total_disk_time;
	int	lost_bits;

	FQ_DISK_CTX_LOCK(diskctx);

	getmicrotime(&diskctx->start_interval);

	for (;;) {
		/* sleep ~1s */
		if ((lksleep(curthread, &diskctx->lock, 0, "fq_balancer", hz/2) == 0)) {
			if (__predict_false(diskctx->die)) {
				FQ_DISK_CTX_UNLOCK(diskctx);
				lwkt_exit();
			}
		}

		bzero(budget, sizeof(budget));
		total_budget = 0;
		n = 0;

		old_tv = diskctx->start_interval;
		getmicrotime(&tv);

		total_disk_time = (int)(1000000*((tv.tv_sec - old_tv.tv_sec)) +
		    (tv.tv_usec - old_tv.tv_usec));

		if (total_disk_time == 0)
			total_disk_time = 1;

		dsched_debug(LOG_INFO, "total_disk_time = %d\n", total_disk_time);

		diskctx->start_interval = tv;

		diskctx->disk_busy = (100*(total_disk_time - diskctx->idle_time)) / total_disk_time;
		if (diskctx->disk_busy < 0)
			diskctx->disk_busy = 0;

		diskctx->idle_time = 0;
		lost_bits = 0;

		TAILQ_FOREACH_MUTABLE(tdio, &diskctx->fq_tdio_list, dlink, tdio2) {
			tdio->interval_avg_latency = tdio->avg_latency;
			tdio->interval_transactions = tdio->transactions;
			if (tdio->interval_transactions > 0) {
				product = (int64_t)tdio->interval_avg_latency *
				    tdio->interval_transactions;
				product >>= lost_bits;
				while(total_budget >= INT64_MAX - product) {
					++lost_bits;
					product >>= 1;
					total_budget >>= 1;
				}
				total_budget += product;
				++budget[(tdio->p) ? tdio->p->p_ionice : 0];
				KKASSERT(total_budget >= 0);
				dsched_debug(LOG_INFO,
				    "%d) avg_latency = %d, transactions = %d, ioprio = %d\n",
				    n, tdio->interval_avg_latency, tdio->interval_transactions,
				    (tdio->p) ? tdio->p->p_ionice : 0);
				++n;
			} else {
				tdio->max_tp = 0;
			}
			tdio->rebalance = 0;
			tdio->transactions = 0;
			tdio->avg_latency = 0;
			tdio->issued = 0;
		}

		dsched_debug(LOG_INFO, "%d procs competing for disk\n"
		    "total_budget = %jd (lost bits = %d)\n"
		    "incomplete tp = %d\n", n, (intmax_t)total_budget,
		    lost_bits, diskctx->incomplete_tp);

		if (n == 0)
			continue;

		sum = 0;

		for (i = 0; i < FQ_PRIO_MAX+1; i++) {
			if (budget[i] == 0)
				continue;
			sum += (FQ_PRIO_BIAS+i)*budget[i];
		}

		if (sum == 0)
			sum = 1;

		dsched_debug(LOG_INFO, "sum = %d\n", sum);

		for (i = 0; i < FQ_PRIO_MAX+1; i++) {
			if (budget[i] == 0)
				continue;

			/*
			 * XXX: if we still overflow here, we really need to switch to
			 *	some more advanced mechanism such as compound int128 or
			 *	storing the lost bits so they can be used in the
			 *	fq_balance_self.
			 */
			diskctx->budgetpb[i] = ((FQ_PRIO_BIAS+i)*total_budget/sum) << lost_bits;
			KKASSERT(diskctx->budgetpb[i] >= 0);
		}

		dsched_debug(4, "disk is %d%% busy\n", diskctx->disk_busy);
		TAILQ_FOREACH(tdio, &diskctx->fq_tdio_list, dlink) {
			tdio->rebalance = 1;
		}

		diskctx->prev_full = diskctx->last_full;
		diskctx->last_full = (diskctx->disk_busy >= 90)?1:0;
	}
}


/*
 * fq_balance_self should be called from all sorts of dispatchers. It basically
 * offloads some of the heavier calculations on throttling onto the process that
 * wants to do I/O instead of doing it in the fq_balance thread.
 * - should be called with diskctx lock held
 */
void
fq_balance_self(struct fq_thread_io *tdio) {
	struct fq_disk_ctx *diskctx;

	int64_t budget, used_budget;
	int64_t	avg_latency;
	int64_t	transactions;

	transactions = (int64_t)tdio->interval_transactions;
	avg_latency = (int64_t)tdio->interval_avg_latency;
	diskctx = tdio->diskctx;

#if 0
	/* XXX: do we really require the lock? */
	FQ_DISK_CTX_LOCK_ASSERT(diskctx);
#endif

	used_budget = ((int64_t)avg_latency * transactions);
	budget = diskctx->budgetpb[(tdio->p) ? tdio->p->p_ionice : 0];

	if (used_budget > 0) {
		dsched_debug(LOG_INFO,
		    "info: used_budget = %jd, budget = %jd\n",
		    (intmax_t)used_budget, budget);
	}

	if ((used_budget > budget) && (diskctx->disk_busy >= 90)) {
		KKASSERT(avg_latency != 0);

		tdio->max_tp = budget/(avg_latency);
		atomic_add_int(&fq_stats.procs_limited, 1);

		dsched_debug(LOG_INFO,
		    "rate limited to %d transactions\n", tdio->max_tp);

	} else if (((used_budget*2 < budget) || (diskctx->disk_busy < 80)) &&
	    (!diskctx->prev_full && !diskctx->last_full)) {
		tdio->max_tp = 0;
	}
}


static int
do_fqstats(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_handle_opaque(oidp, &fq_stats, sizeof(struct dsched_fq_stats), req));
}


SYSCTL_PROC(_kern, OID_AUTO, fq_stats, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, sizeof(struct dsched_fq_stats), do_fqstats, "fq_stats",
    "dsched_fq statistics");


static void
fq_init(void)
{

}

static void
fq_uninit(void)
{

}

static void
fq_earlyinit(void)
{
	fq_tdio_cache = objcache_create("fq-tdio-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &fq_thread_io_malloc_args );

	fq_tdctx_cache = objcache_create("fq-tdctx-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &fq_thread_ctx_malloc_args );

	FQ_GLOBAL_THREAD_CTX_LOCKINIT();

	fq_diskctx_cache = objcache_create("fq-diskctx-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &fq_disk_ctx_malloc_args );

	bzero(&fq_stats, sizeof(struct dsched_fq_stats));

	dsched_register(&dsched_fq_policy);

	kprintf("FQ scheduler policy version %d.%d loaded\n",
	    dsched_fq_version_maj, dsched_fq_version_min);
}

static void
fq_earlyuninit(void)
{
	return;
}

SYSINIT(fq_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, fq_init, NULL);
SYSUNINIT(fq_register, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, fq_uninit, NULL);

SYSINIT(fq_early, SI_SUB_CREATE_INIT-1, SI_ORDER_FIRST, fq_earlyinit, NULL);
SYSUNINIT(fq_early, SI_SUB_CREATE_INIT-1, SI_ORDER_ANY, fq_earlyuninit, NULL);
