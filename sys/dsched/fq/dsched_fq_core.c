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

struct objcache_malloc_args dsched_fq_dpriv_malloc_args = {
	sizeof(struct dsched_fq_dpriv), M_DSCHEDFQ };
struct objcache_malloc_args dsched_fq_priv_malloc_args = {
	sizeof(struct dsched_fq_priv), M_DSCHEDFQ };
struct objcache_malloc_args dsched_fq_mpriv_malloc_args = {
	sizeof(struct dsched_fq_mpriv), M_DSCHEDFQ };

static struct objcache	*fq_dpriv_cache;
static struct objcache	*fq_mpriv_cache;
static struct objcache	*fq_priv_cache;

TAILQ_HEAD(, dsched_fq_mpriv)	dsched_fqmp_list =
		TAILQ_HEAD_INITIALIZER(dsched_fqmp_list);

struct lock	fq_fqmp_lock;
struct callout	fq_callout;

extern struct dsched_ops dsched_fq_ops;

void
fq_reference_dpriv(struct dsched_fq_dpriv *dpriv)
{
	int refcount;

	refcount = atomic_fetchadd_int(&dpriv->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_reference_priv(struct dsched_fq_priv *fqp)
{
	int refcount;

	refcount = atomic_fetchadd_int(&fqp->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_reference_mpriv(struct dsched_fq_mpriv *fqmp)
{
	int refcount;

	refcount = atomic_fetchadd_int(&fqmp->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_dereference_dpriv(struct dsched_fq_dpriv *dpriv)
{
	struct dsched_fq_priv	*fqp, *fqp2;
	int refcount;

	refcount = atomic_fetchadd_int(&dpriv->refcount, -1);


	KKASSERT(refcount >= 0 || refcount <= -0x400);

	if (refcount == 1) {
		atomic_subtract_int(&dpriv->refcount, 0x400); /* mark as: in destruction */
#if 1
		kprintf("dpriv (%p) destruction started, trace:\n", dpriv);
		print_backtrace(4);
#endif
		lockmgr(&dpriv->lock, LK_EXCLUSIVE);
		TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
			TAILQ_REMOVE(&dpriv->fq_priv_list, fqp, dlink);
			fqp->flags &= ~FQP_LINKED_DPRIV;
			fq_dereference_priv(fqp);
		}
		lockmgr(&dpriv->lock, LK_RELEASE);

		objcache_put(fq_dpriv_cache, dpriv);
		atomic_subtract_int(&fq_stats.dpriv_allocations, 1);
	}
}

void
fq_dereference_priv(struct dsched_fq_priv *fqp)
{
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_dpriv	*dpriv;
	int refcount;

	refcount = atomic_fetchadd_int(&fqp->refcount, -1);

	KKASSERT(refcount >= 0 || refcount <= -0x400);

	if (refcount == 1) {
		atomic_subtract_int(&fqp->refcount, 0x400); /* mark as: in destruction */
#if 0
		kprintf("fqp (%p) destruction started, trace:\n", fqp);
		print_backtrace(8);
#endif
		dpriv = fqp->dpriv;
		KKASSERT(dpriv != NULL);
		KKASSERT(fqp->qlength == 0);

		if (fqp->flags & FQP_LINKED_DPRIV) {
			lockmgr(&dpriv->lock, LK_EXCLUSIVE);

			TAILQ_REMOVE(&dpriv->fq_priv_list, fqp, dlink);
			fqp->flags &= ~FQP_LINKED_DPRIV;

			lockmgr(&dpriv->lock, LK_RELEASE);
		}

		if (fqp->flags & FQP_LINKED_FQMP) {
			fqmp = fqp->fqmp;
			KKASSERT(fqmp != NULL);

			spin_lock_wr(&fqmp->lock);

			TAILQ_REMOVE(&fqmp->fq_priv_list, fqp, link);
			fqp->flags &= ~FQP_LINKED_FQMP;

			spin_unlock_wr(&fqmp->lock);
		}

		objcache_put(fq_priv_cache, fqp);
		atomic_subtract_int(&fq_stats.fqp_allocations, 1);
#if 0
		fq_dereference_dpriv(dpriv);
#endif
	}
}

void
fq_dereference_mpriv(struct dsched_fq_mpriv *fqmp)
{
	struct dsched_fq_priv	*fqp, *fqp2;
	int refcount;

	refcount = atomic_fetchadd_int(&fqmp->refcount, -1);

	KKASSERT(refcount >= 0 || refcount <= -0x400);

	if (refcount == 1) {
		atomic_subtract_int(&fqmp->refcount, 0x400); /* mark as: in destruction */
#if 0
		kprintf("fqmp (%p) destruction started, trace:\n", fqmp);
		print_backtrace(8);
#endif
		FQ_GLOBAL_FQMP_LOCK();

		TAILQ_FOREACH_MUTABLE(fqp, &fqmp->fq_priv_list, link, fqp2) {
			TAILQ_REMOVE(&fqmp->fq_priv_list, fqp, link);
			fqp->flags &= ~FQP_LINKED_FQMP;
			fq_dereference_priv(fqp);
		}
		TAILQ_REMOVE(&dsched_fqmp_list, fqmp, link);

		FQ_GLOBAL_FQMP_UNLOCK();

		objcache_put(fq_mpriv_cache, fqmp);
		atomic_subtract_int(&fq_stats.fqmp_allocations, 1);
	}
}


struct dsched_fq_priv *
fq_alloc_priv(struct disk *dp, struct dsched_fq_mpriv *fqmp)
{
	struct dsched_fq_priv	*fqp;
#if 0
	fq_reference_dpriv(dsched_get_disk_priv(dp));
#endif
	fqp = objcache_get(fq_priv_cache, M_WAITOK);
	bzero(fqp, sizeof(struct dsched_fq_priv));

	/* XXX: maybe we do need another ref for the disk list for fqp */
	fq_reference_priv(fqp);

	FQ_FQP_LOCKINIT(fqp);
	fqp->dp = dp;

	fqp->dpriv = dsched_get_disk_priv(dp);
	TAILQ_INIT(&fqp->queue);

	TAILQ_INSERT_TAIL(&fqp->dpriv->fq_priv_list, fqp, dlink);
	fqp->flags |= FQP_LINKED_DPRIV;

	if (fqmp) {
		fqp->fqmp = fqmp;
		fqp->p = fqmp->p;

		/* Put the fqp in the fqmp list */
		FQ_FQMP_LOCK(fqmp);
		TAILQ_INSERT_TAIL(&fqmp->fq_priv_list, fqp, link);
		FQ_FQMP_UNLOCK(fqmp);
		fqp->flags |= FQP_LINKED_FQMP;
	}

	atomic_add_int(&fq_stats.fqp_allocations, 1);
	return fqp;
}


struct dsched_fq_dpriv *
fq_alloc_dpriv(struct disk *dp)
{
	struct dsched_fq_dpriv *dpriv;

	dpriv = objcache_get(fq_dpriv_cache, M_WAITOK);
	bzero(dpriv, sizeof(struct dsched_fq_dpriv));
	fq_reference_dpriv(dpriv);
	dpriv->dp = dp;
	dpriv->avg_rq_time = 0;
	dpriv->incomplete_tp = 0;
	FQ_DPRIV_LOCKINIT(dpriv);
	TAILQ_INIT(&dpriv->fq_priv_list);

	atomic_add_int(&fq_stats.dpriv_allocations, 1);
	return dpriv;
}


struct dsched_fq_mpriv *
fq_alloc_mpriv(struct proc *p)
{
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_priv	*fqp;
	struct disk	*dp = NULL;

	fqmp = objcache_get(fq_mpriv_cache, M_WAITOK);
	bzero(fqmp, sizeof(struct dsched_fq_mpriv));
	fq_reference_mpriv(fqmp);
#if 0
	kprintf("fq_alloc_mpriv, new fqmp = %p\n", fqmp);
#endif
	FQ_FQMP_LOCKINIT(fqmp);
	TAILQ_INIT(&fqmp->fq_priv_list);
	fqmp->p = p;

	while ((dp = dsched_disk_enumerate(dp, &dsched_fq_ops))) {
		fqp = fq_alloc_priv(dp, fqmp);
#if 0
		fq_reference_priv(fqp);
#endif
	}

	FQ_GLOBAL_FQMP_LOCK();
	TAILQ_INSERT_TAIL(&dsched_fqmp_list, fqmp, link);
	FQ_GLOBAL_FQMP_UNLOCK();

	atomic_add_int(&fq_stats.fqmp_allocations, 1);
	return fqmp;
}


void
fq_dispatcher(struct dsched_fq_dpriv *dpriv)
{
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_priv	*fqp, *fqp2;
	struct bio *bio, *bio2;
	int idle;

	/*
	 * We need to manually assign an fqp to the fqmp of this thread
	 * since it isn't assigned one during fq_prepare, as the disk
	 * is not set up yet.
	 */
	fqmp = dsched_get_thread_priv(curthread);
	KKASSERT(fqmp != NULL);

	fqp = fq_alloc_priv(dpriv->dp, fqmp);
#if 0
	fq_reference_priv(fqp);
#endif

	FQ_DPRIV_LOCK(dpriv);
	for(;;) {
		idle = 0;
		/* sleep ~60 ms */
		if ((lksleep(dpriv, &dpriv->lock, 0, "fq_dispatcher", hz/15) == 0)) {
			/*
			 * We've been woken up; this either means that we are
			 * supposed to die away nicely or that the disk is idle.
			 */

			if (__predict_false(dpriv->die == 1)) {
				/* If we are supposed to die, drain all queues */
				fq_drain(dpriv, FQ_DRAIN_FLUSH);

				/* Now we can safely unlock and exit */
				FQ_DPRIV_UNLOCK(dpriv);
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
			idle = dpriv->idle;

		/*
		 * XXX: further room for improvements here. It would be better
		 *	to dispatch a few requests from each fqp as to ensure
		 *	real fairness.
		 */
		TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
			if (fqp->qlength == 0)
				continue;

			FQ_FQP_LOCK(fqp);
			if (atomic_cmpset_int(&fqp->rebalance, 1, 0))
				fq_balance_self(fqp);
			/*
			 * XXX: why 5 extra? should probably be dynamic,
			 *	relying on information on latency.
			 */
			if ((fqp->max_tp > 0) && idle &&
			    (fqp->issued >= fqp->max_tp)) {
				fqp->max_tp += 5;
			}

			TAILQ_FOREACH_MUTABLE(bio, &fqp->queue, link, bio2) {
				if (atomic_cmpset_int(&fqp->rebalance, 1, 0))
					fq_balance_self(fqp);
				if ((fqp->max_tp > 0) &&
				    ((fqp->issued >= fqp->max_tp)))
					break;

				TAILQ_REMOVE(&fqp->queue, bio, link);
				--fqp->qlength;

				/*
				 * beware that we do have an fqp reference
				 * from the queueing
				 */
				fq_dispatch(dpriv, bio, fqp);
			}
			FQ_FQP_UNLOCK(fqp);

		}
	}
}

void
fq_balance_thread(struct dsched_fq_dpriv *dpriv)
{
	struct	dsched_fq_priv	*fqp, *fqp2;
	static struct timeval old_tv;
	struct timeval tv;
	int64_t	total_budget, product;
	int64_t budget[FQ_PRIO_MAX+1];
	int	n, i, sum, total_disk_time;
	int	lost_bits;

	getmicrotime(&old_tv);

	FQ_DPRIV_LOCK(dpriv);
	for (;;) {
		/* sleep ~1s */
		if ((lksleep(curthread, &dpriv->lock, 0, "fq_balancer", hz/2) == 0)) {
			if (__predict_false(dpriv->die)) {
				FQ_DPRIV_UNLOCK(dpriv);
				lwkt_exit();
			}
		}

		bzero(budget, sizeof(budget));
		total_budget = 0;
		n = 0;

		getmicrotime(&tv);

		total_disk_time = (int)(1000000*((tv.tv_sec - old_tv.tv_sec)) +
		    (tv.tv_usec - old_tv.tv_usec));

		if (total_disk_time == 0)
			total_disk_time = 1;

		dsched_debug(LOG_INFO, "total_disk_time = %d\n", total_disk_time);

		old_tv = tv;

		dpriv->disk_busy = (100*(total_disk_time - dpriv->idle_time)) / total_disk_time;
		if (dpriv->disk_busy < 0)
			dpriv->disk_busy = 0;

		dpriv->idle_time = 0;
		lost_bits = 0;

		TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
			fqp->s_avg_latency = fqp->avg_latency;
			fqp->s_transactions = fqp->transactions;
			if (fqp->s_transactions > 0 /* 30 */) {
				product = fqp->s_avg_latency * fqp->s_transactions;
				product >>= lost_bits;
				while(total_budget >= INT64_MAX - product) {
					++lost_bits;
					product >>= 1;
					total_budget >>= 1;
				}
				total_budget += product;
				++budget[(fqp->p) ? fqp->p->p_ionice : 0];
				KKASSERT(total_budget >= 0);
				dsched_debug(LOG_INFO,
				    "%d) avg_latency = %d, transactions = %d, ioprio = %d\n",
				    n, fqp->s_avg_latency, fqp->s_transactions,
				    (fqp->p) ? fqp->p->p_ionice : 0);
				++n;
			} else {
				fqp->max_tp = 0;
			}
			fqp->rebalance = 0;
			fqp->transactions = 0;
			fqp->avg_latency = 0;
			fqp->issued = 0;
		}

		dsched_debug(LOG_INFO, "%d procs competing for disk\n"
		    "total_budget = %jd (lost bits = %d)\n"
		    "incomplete tp = %d\n", n, (intmax_t)total_budget,
		    lost_bits, dpriv->incomplete_tp);

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
			dpriv->budgetpb[i] = ((FQ_PRIO_BIAS+i)*total_budget/sum) << lost_bits;
			KKASSERT(dpriv->budgetpb[i] >= 0);
		}

		if (total_budget > dpriv->max_budget)
			dpriv->max_budget = total_budget;

		dsched_debug(4, "disk is %d%% busy\n", dpriv->disk_busy);
		TAILQ_FOREACH(fqp, &dpriv->fq_priv_list, dlink) {
			fqp->rebalance = 1;
		}

		dpriv->prev_full = dpriv->last_full;
		dpriv->last_full = (dpriv->disk_busy >= 90)?1:0;
	}
}


/*
 * fq_balance_self should be called from all sorts of dispatchers. It basically
 * offloads some of the heavier calculations on throttling onto the process that
 * wants to do I/O instead of doing it in the fq_balance thread.
 * - should be called with dpriv lock held
 */
void
fq_balance_self(struct dsched_fq_priv *fqp) {
	struct dsched_fq_dpriv *dpriv;

	int64_t budget, used_budget;
	int64_t	avg_latency;
	int64_t	transactions;

	transactions = (int64_t)fqp->s_transactions;
	avg_latency = (int64_t)fqp->s_avg_latency;
	dpriv = fqp->dpriv;

	used_budget = ((int64_t)avg_latency * transactions);
	budget = dpriv->budgetpb[(fqp->p) ? fqp->p->p_ionice : 0];

	if (used_budget > 0) {
		dsched_debug(LOG_INFO,
		    "info: used_budget = %jd, budget = %jd\n",
		    (intmax_t)used_budget, budget);
	}

	if ((used_budget > budget) && (dpriv->disk_busy >= 90)) {
		KKASSERT(avg_latency != 0);

		fqp->max_tp = budget/(avg_latency);
		atomic_add_int(&fq_stats.procs_limited, 1);

		dsched_debug(LOG_INFO,
		    "rate limited to %d transactions\n", fqp->max_tp);

	} else if (((used_budget*2 < budget) || (dpriv->disk_busy < 80)) &&
	    (!dpriv->prev_full && !dpriv->last_full)) {
		fqp->max_tp = 0;
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
	fq_priv_cache = objcache_create("fq-priv-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_fq_priv_malloc_args );

	fq_mpriv_cache = objcache_create("fq-mpriv-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_fq_mpriv_malloc_args );

	FQ_GLOBAL_FQMP_LOCKINIT();

	fq_dpriv_cache = objcache_create("fq-dpriv-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_fq_dpriv_malloc_args );

	bzero(&fq_stats, sizeof(struct dsched_fq_stats));

	dsched_register(&dsched_fq_ops);
	callout_init_mp(&fq_callout);

	kprintf("FQ scheduler policy version %d.%d loaded\n",
	    dsched_fq_version_maj, dsched_fq_version_min);
}

static void
fq_earlyuninit(void)
{
	callout_stop(&fq_callout);
	callout_deactivate(&fq_callout);
	return;
}

SYSINIT(fq_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, fq_init, NULL);
SYSUNINIT(fq_register, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, fq_uninit, NULL);

SYSINIT(fq_early, SI_SUB_CREATE_INIT-1, SI_ORDER_FIRST, fq_earlyinit, NULL);
SYSUNINIT(fq_early, SI_SUB_CREATE_INIT-1, SI_ORDER_ANY, fq_earlyuninit, NULL);
