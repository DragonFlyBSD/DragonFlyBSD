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


MALLOC_DEFINE(M_DSCHEDFQ, "dschedfq", "fq dsched allocs");

static dsched_prepare_t		fq_prepare;
static dsched_teardown_t	fq_teardown;
static dsched_flush_t		fq_flush;
static dsched_cancel_t		fq_cancel;
static dsched_queue_t		fq_queue;

/* These are in _procops */
dsched_new_buf_t	fq_new_buf;
dsched_new_proc_t	fq_new_proc;
dsched_new_thread_t	fq_new_thread;
dsched_exit_buf_t	fq_exit_buf;
dsched_exit_proc_t	fq_exit_proc;
dsched_exit_thread_t	fq_exit_thread;

extern struct dsched_fq_stats	fq_stats;
extern struct spinlock	fq_fqmp_lock;
extern TAILQ_HEAD(, dsched_fq_mpriv)	dsched_fqmp_list;
extern struct callout	fq_callout;

struct dsched_ops dsched_fq_ops = {
	.head = {
		.name = "fq"
	},
	.prepare = fq_prepare,
	.teardown = fq_teardown,
	.flush = fq_flush,
	.cancel_all = fq_cancel,
	.bio_queue = fq_queue,

	.new_buf = fq_new_buf,
	.new_proc = fq_new_proc,
	.new_thread = fq_new_thread,
	.exit_buf = fq_exit_buf,
	.exit_proc = fq_exit_proc,
	.exit_thread = fq_exit_thread,
};



static int
fq_prepare(struct disk *dp)
{
	struct	dsched_fq_dpriv	*dpriv;
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_priv	*fqp;
	struct thread *td_core;

	dpriv = fq_alloc_dpriv(dp);
	fq_reference_dpriv(dpriv);
	dsched_set_disk_priv(dp, dpriv);

	FQ_GLOBAL_FQMP_LOCK();
	TAILQ_FOREACH(fqmp, &dsched_fqmp_list, link) {
		fqp = fq_alloc_priv(dp);

		FQ_FQMP_LOCK(fqmp);
#if 0
		fq_reference_priv(fqp);
#endif
		TAILQ_INSERT_TAIL(&fqmp->fq_priv_list, fqp, link);
		FQ_FQMP_UNLOCK(fqmp);
	}

	FQ_GLOBAL_FQMP_UNLOCK();
	lwkt_create((void (*)(void *))fq_dispatcher, dpriv, &td_core, NULL,
	    0, 0, "fq_dispatcher_%s", dp->d_cdev->si_name);
	fq_balance_thread(dpriv);

	return 0;
}



static void
fq_teardown(struct disk *dp)
{
	struct dsched_fq_dpriv *dpriv;

	dpriv = dsched_get_disk_priv(dp);
	KKASSERT(dpriv != NULL);

	/* Basically kill the dispatcher thread */
	callout_stop(&fq_callout);
	dpriv->die = 1;
	wakeup(dpriv);
	tsleep(dpriv, 0, "fq_dispatcher", hz/5); /* wait 200 ms */
	callout_stop(&fq_callout);
	wakeup(dpriv);
	tsleep(dpriv, 0, "fq_dispatcher", hz/5); /* wait 200 ms */
	callout_stop(&fq_callout);
	/* XXX: we really need callout_drain, this REALLY sucks */


	fq_dereference_dpriv(dpriv); /* from prepare */
	fq_dereference_dpriv(dpriv); /* from alloc */

	dsched_set_disk_priv(dp, NULL);
	/* XXX: get rid of dpriv, cancel all queued requests...
	 *      but how do we get rid of all loose fqps?
	 *    --> possibly same solution as devfs; tracking a list of
	 *        orphans.
	 *    but for now we don't care much about this yet
	 */
}


static void
fq_flush(struct disk *dp, struct bio *biin)
{
	/* Our flushes are handled by queue, this should never be called */
	return;
}


static void
fq_cancel(struct disk *dp)
{
	struct dsched_fq_dpriv	*dpriv;
	struct dsched_fq_priv	*fqp, *fqp2;
	struct bio *bio, *bio2;

	dpriv = dsched_get_disk_priv(dp);
	KKASSERT(dpriv != NULL);

	/*
	 * all bios not in flight are queued in their respective fqps.
	 * good thing we have a list of fqps per disk dpriv.
	 */
	FQ_DPRIV_LOCK(dpriv);
	TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
		if (fqp->qlength > 0) {
			FQ_FQP_LOCK(fqp);
			TAILQ_FOREACH_MUTABLE(bio, &fqp->queue, link, bio2) {
				TAILQ_REMOVE(&fqp->queue, bio, link);
				--fqp->qlength;
				dsched_cancel_bio(bio);
				atomic_add_int(&fq_stats.cancelled, 1);
			}
			FQ_FQP_UNLOCK(fqp);
		}
	}
	FQ_DPRIV_UNLOCK(dpriv);
}


static int
fq_queue(struct disk *dp, struct bio *obio)
{
	struct bio *bio, *bio2;
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_priv	*fqp;
	struct dsched_fq_dpriv	*dpriv;
	int found = 0;
	int count;
	int max_tp, transactions;

	/* get fqmp and fqp */
	fqmp = dsched_get_buf_priv(obio->bio_buf);

	/*
	 * XXX: hack. we don't want the assert because some null-fqmps are
	 * leaking through; just dispatch them. These come from the
	 * mi_startup() mess, which does the initial root mount.
	 */
#if 0
	KKASSERT(fqmp != NULL);
#endif
	if (fqmp == NULL) {
		atomic_add_int(&fq_stats.no_fqmp, 1);
		dsched_strategy_raw(dp, obio);
		return 0;
	}


	FQ_FQMP_LOCK(fqmp);
#if 0
	kprintf("fq_queue, fqmp = %p\n", fqmp);
#endif
	KKASSERT(!TAILQ_EMPTY(&fqmp->fq_priv_list));
	TAILQ_FOREACH(fqp, &fqmp->fq_priv_list, link) {
		if (fqp->dp == dp) {
			fq_reference_priv(fqp);
			found = 1;
			break;
		}
	}
	FQ_FQMP_UNLOCK(fqmp);
	dsched_clr_buf_priv(obio->bio_buf);
	fq_dereference_mpriv(fqmp); /* acquired on new_buf */
	atomic_subtract_int(&fq_stats.nbufs, 1);

	KKASSERT(found == 1);
	dpriv = dsched_get_disk_priv(dp);

	/* XXX: probably rather pointless doing this atomically */
	max_tp = atomic_fetchadd_int(&fqp->max_tp, 0);
#if 0
	transactions = atomic_fetchadd_int(&fqp->transactions, 0);
#endif
	transactions = atomic_fetchadd_int(&fqp->issued, 0);

	/* | No rate limiting || Hasn't reached limit rate |     */
	if ((max_tp == 0) || (transactions < max_tp)) {
		/*
		 * Process pending bios from previous _queue() actions that
		 * have been rate-limited and hence queued in the fqp.
		 */
		KKASSERT(fqp->qlength >= 0);

		if (fqp->qlength > 0) {
			FQ_FQP_LOCK(fqp);
			count = 0;

			TAILQ_FOREACH_MUTABLE(bio, &fqp->queue, link, bio2) {
				if ((fqp->max_tp > 0) && (fqp->issued >= fqp->max_tp))
					break;
				TAILQ_REMOVE(&fqp->queue, bio, link);
				--fqp->qlength;

				/*
				 * beware that we do have an fqp reference from the
				 * queueing
				 */
				fq_dispatch(dpriv, bio, fqp);
			}
			FQ_FQP_UNLOCK(fqp);
		}

		/* Nothing is pending from previous IO, so just pass it down */
		fq_reference_priv(fqp);

		fq_dispatch(dpriv, obio, fqp);
	} else {
		/*
		 * This thread has exceeeded its fair share,
		 * the transactions are now rate limited. At
		 * this point, the rate would be exceeded, so
		 * we just queue requests instead of
		 * despatching them.
		 */
		FQ_FQP_LOCK(fqp);
		fq_reference_priv(fqp);

		/*
		 * Prioritize reads by inserting them at the front of the
		 * queue.
		 *
		 * XXX: this might cause issues with data that should
		 * 	have been written and is being read, but hasn't
		 *	actually been written yet.
		 */
		if (obio->bio_buf->b_cmd == BUF_CMD_READ)
			TAILQ_INSERT_HEAD(&fqp->queue, obio, link);
		else
			TAILQ_INSERT_TAIL(&fqp->queue, obio, link);

		++fqp->qlength;
		FQ_FQP_UNLOCK(fqp);
	}

	fq_dereference_priv(fqp);
	return 0;
}


void
fq_completed(struct bio *bp)
{
	struct bio *obio;
	int	delta;
	struct dsched_fq_priv	*fqp;
	struct dsched_fq_dpriv	*dpriv;
	struct disk	*dp;
	int transactions, latency;

	struct timeval tv;

	getmicrotime(&tv);

	dp = dsched_get_bio_dp(bp);
	dpriv = dsched_get_disk_priv(dp);
	fqp = dsched_get_bio_priv(bp);
	KKASSERT(fqp != NULL);
	KKASSERT(dpriv != NULL);

	fq_reference_dpriv(dpriv);

	if (!(bp->bio_buf->b_flags & B_ERROR)) {
		/*
		 * Get the start ticks from when the bio was dispatched and calculate
		 * how long it took until completion.
		 */
		delta = (int)(1000000*((tv.tv_sec - bp->bio_caller_info3.tv.tv_sec)) +
		    (tv.tv_usec - bp->bio_caller_info3.tv.tv_usec));
		if (delta <= 0)
			delta = 10000; /* default assume 10 ms */

		/* This is the last in-flight request and the disk is not idle yet */
		if ((dpriv->incomplete_tp <= 1) && (!dpriv->idle)) {
			dpriv->idle = 1;	/* Mark disk as idle */
			dpriv->start_idle = tv;	/* Save start idle time */
			wakeup(dpriv);		/* Wake up fq_dispatcher */
		}
		atomic_subtract_int(&dpriv->incomplete_tp, 1);
		transactions = atomic_fetchadd_int(&fqp->transactions, 1);
		latency = atomic_fetchadd_int(&fqp->avg_latency, 0);

		if (latency != 0) {
			/* Moving averager, ((n-1)*avg_{n-1} + x) / n */
			latency = ((transactions) *
			    latency + delta) / (transactions + 1);
		} else {
			latency = delta;
		}

		fqp->avg_latency = latency;
		atomic_add_int(&fq_stats.transactions_completed, 1);
	}

	fq_dereference_dpriv(dpriv);
	/* decrease the ref count that was bumped for us on dispatch */
	fq_dereference_priv(fqp);

	obio = pop_bio(bp);
	biodone(obio);
}

void
fq_dispatch(struct dsched_fq_dpriv *dpriv, struct bio *bio,
    struct dsched_fq_priv *fqp)
{
	struct timeval tv;

	if (dpriv->idle) {
		getmicrotime(&tv);		
		atomic_add_int(&dpriv->idle_time,
		    (int)(1000000*((tv.tv_sec - dpriv->start_idle.tv_sec)) +
		    (tv.tv_usec - dpriv->start_idle.tv_usec)));
		dpriv->idle = 0;
	}
	dsched_strategy_async(dpriv->dp, bio, fq_completed, fqp);

	atomic_add_int(&fqp->issued, 1);
	atomic_add_int(&dpriv->incomplete_tp, 1);
	atomic_add_int(&fq_stats.transactions, 1);
}
