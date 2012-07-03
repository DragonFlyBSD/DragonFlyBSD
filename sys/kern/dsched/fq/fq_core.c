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
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/ctype.h>
#include <sys/buf2.h>
#include <sys/syslog.h>
#include <sys/dsched.h>
#include <machine/param.h>

#include <kern/dsched/fq/fq.h>

static int	dsched_fq_version_maj = 1;
static int	dsched_fq_version_min = 1;

/* Make sure our structs fit */
CTASSERT(sizeof(struct fq_thread_io) <= DSCHED_THREAD_IO_MAX_SZ);
CTASSERT(sizeof(struct fq_disk_ctx) <= DSCHED_DISK_CTX_MAX_SZ);

struct dsched_fq_stats	fq_stats;

extern struct dsched_policy dsched_fq_policy;

void
fq_dispatcher(struct fq_disk_ctx *diskctx)
{
	struct dispatch_prep *dispatch_ary;
	struct dsched_thread_io	*ds_tdio, *ds_tdio2;
	struct fq_thread_io	*tdio;
	struct bio *bio, *bio2;
	int idle;
	int i, prepd_io;

	/*
	 * Array is dangerously big for an on-stack declaration, allocate
	 * it instead.
	 */
	dispatch_ary = kmalloc(sizeof(*dispatch_ary) * FQ_DISPATCH_ARRAY_SZ,
			       M_TEMP, M_INTWAIT | M_ZERO);

	/*
	 * We need to manually assign an tdio to the tdctx of this thread
	 * since it isn't assigned one during fq_prepare, as the disk
	 * is not set up yet.
	 */
	tdio = (struct fq_thread_io *)dsched_new_policy_thread_tdio(&diskctx->head,
	    &dsched_fq_policy);

	DSCHED_DISK_CTX_LOCK(&diskctx->head);
	for(;;) {
		idle = 0;
		/*
		 * sleep ~60 ms, failsafe low hz rates.
		 */
		if ((lksleep(diskctx, &diskctx->head.lock, 0,
			     "fq_dispatcher", (hz + 14) / 15) == 0)) {
			/*
			 * We've been woken up; this either means that we are
			 * supposed to die away nicely or that the disk is idle.
			 */

			if (__predict_false(diskctx->die == 1))
				break;

			/*
			 * We have been awakened because the disk is idle.
			 * So let's get ready to dispatch some extra bios.
			 */
			idle = 1;
		}

		/* Maybe the disk is idle and we just didn't get the wakeup */
		if (idle == 0)
			idle = diskctx->idle;

		/* Set the number of prepared requests to 0 */
		i = 0;

		/*
		 * XXX: further room for improvements here. It would be better
		 *	to dispatch a few requests from each tdio as to ensure
		 *	real fairness.
		 */
		TAILQ_FOREACH_MUTABLE(ds_tdio, &diskctx->head.tdio_list, dlink, ds_tdio2) {
			tdio = (struct fq_thread_io *)ds_tdio;
			if (tdio->head.qlength == 0)
				continue;

			DSCHED_THREAD_IO_LOCK(&tdio->head);
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

			prepd_io = 0;
			TAILQ_FOREACH_MUTABLE(bio, &tdio->head.queue, link, bio2) {
				if (atomic_cmpset_int(&tdio->rebalance, 1, 0))
					fq_balance_self(tdio);
				if (((tdio->max_tp > 0) &&
				    (tdio->issued + prepd_io >= tdio->max_tp)) ||
				    (i == FQ_DISPATCH_ARRAY_SZ))
					break;

				TAILQ_REMOVE(&tdio->head.queue, bio, link);
				--tdio->head.qlength;

				/*
				 * beware that we do have an tdio reference
				 * from the queueing
				 *
				 * XXX: note that here we don't dispatch it yet
				 *	but just prepare it for dispatch so
				 *	that no locks are held when calling
				 *	into the drivers.
				 */
				dispatch_ary[i].bio = bio;
				dispatch_ary[i].tdio = tdio;
				++i;
				++prepd_io;
			}
			DSCHED_THREAD_IO_UNLOCK(&tdio->head);

		}

		dsched_disk_ctx_ref(&diskctx->head);
		DSCHED_DISK_CTX_UNLOCK(&diskctx->head);

		/*
		 * Dispatch all the previously prepared bios, now without
		 * holding any locks.
		 */
		for (--i; i >= 0; i--) {
			bio = dispatch_ary[i].bio;
			tdio = dispatch_ary[i].tdio;
			fq_dispatch(diskctx, bio, tdio);
		}

		DSCHED_DISK_CTX_LOCK(&diskctx->head);
		dsched_disk_ctx_unref(&diskctx->head);
	}

	/*
	 * If we are supposed to die, drain all queues, then
	 * unlock and exit.
	 */
	fq_drain(diskctx, FQ_DRAIN_FLUSH);
	DSCHED_DISK_CTX_UNLOCK(&diskctx->head);
	kfree(dispatch_ary, M_TEMP);

	kprintf("fq_dispatcher is peacefully dying\n");
	lwkt_exit();
	/* NOTREACHED */
}

void
fq_balance_thread(struct fq_disk_ctx *diskctx)
{
	struct dsched_thread_io	*ds_tdio;
	struct	fq_thread_io	*tdio;
	struct timeval tv, old_tv;
	int64_t	total_budget, product;
	int64_t budget[FQ_PRIO_MAX+1];
	int	n, i, sum, total_disk_time;
	int	lost_bits;

	DSCHED_DISK_CTX_LOCK(&diskctx->head);

	getmicrotime(&diskctx->start_interval);

	for (;;) {
		/* sleep ~1s */
		if ((lksleep(curthread, &diskctx->head.lock, 0, "fq_balancer", hz/2) == 0)) {
			if (__predict_false(diskctx->die)) {
				DSCHED_DISK_CTX_UNLOCK(&diskctx->head);
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

		TAILQ_FOREACH(ds_tdio, &diskctx->head.tdio_list, dlink) {
			tdio = (struct fq_thread_io *)ds_tdio;
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
				++budget[(tdio->head.p) ? tdio->head.p->p_ionice : 0];
				KKASSERT(total_budget >= 0);
				dsched_debug(LOG_INFO,
				    "%d) avg_latency = %d, transactions = %d, ioprio = %d\n",
				    n, tdio->interval_avg_latency, tdio->interval_transactions,
				    (tdio->head.p) ? tdio->head.p->p_ionice : 0);
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
		TAILQ_FOREACH(ds_tdio, &diskctx->head.tdio_list, dlink) {
			tdio = (struct fq_thread_io *)ds_tdio;
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
	diskctx = (struct fq_disk_ctx *)tdio->head.diskctx;

#if 0
	/* XXX: do we really require the lock? */
	DSCHED_DISK_CTX_LOCK_ASSERT(diskctx);
#endif

	used_budget = avg_latency * transactions;
	budget = diskctx->budgetpb[(tdio->head.p) ? tdio->head.p->p_ionice : 0];

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

static int
fq_mod_handler(module_t mod, int type, void *unused)
{
	static struct sysctl_ctx_list sysctl_ctx;
	static struct sysctl_oid *oid;
	static char version[16];
	int error;

	ksnprintf(version, sizeof(version), "%d.%d",
	    dsched_fq_version_maj, dsched_fq_version_min);

	switch (type) {
	case MOD_LOAD:
		bzero(&fq_stats, sizeof(struct dsched_fq_stats));
		if ((error = dsched_register(&dsched_fq_policy)))
			return (error);

		sysctl_ctx_init(&sysctl_ctx);
		oid = SYSCTL_ADD_NODE(&sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_dsched),
		    OID_AUTO,
		    "fq",
		    CTLFLAG_RD, 0, "");

		SYSCTL_ADD_PROC(&sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "stats", CTLTYPE_OPAQUE|CTLFLAG_RD,
		    0, 0, do_fqstats, "S,dsched_fq_stats", "fq statistics");

		SYSCTL_ADD_STRING(&sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "version", CTLFLAG_RD, version, 0, "fq version");

		kprintf("FQ scheduler policy version %d.%d loaded\n",
		    dsched_fq_version_maj, dsched_fq_version_min);
		break;

	case MOD_UNLOAD:
		if ((error = dsched_unregister(&dsched_fq_policy)))
			return (error);
		sysctl_ctx_free(&sysctl_ctx);
		kprintf("FQ scheduler policy unloaded\n");
		break;

	default:
		break;
	}

	return 0;
}

DSCHED_POLICY_MODULE(dsched_fq, fq_mod_handler);
