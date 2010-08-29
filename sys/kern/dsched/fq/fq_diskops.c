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

static dsched_prepare_t		fq_prepare;
static dsched_teardown_t	fq_teardown;
static dsched_cancel_t		fq_cancel;
static dsched_queue_t		fq_queue;

extern struct dsched_fq_stats	fq_stats;

struct dsched_policy dsched_fq_policy = {
	.name = "fq",

	.prepare = fq_prepare,
	.teardown = fq_teardown,
	.cancel_all = fq_cancel,
	.bio_queue = fq_queue
};

static int
fq_prepare(struct dsched_disk_ctx *ds_diskctx)
{
	struct	fq_disk_ctx	*diskctx = (struct fq_disk_ctx *)ds_diskctx;
	struct thread *td_core, *td_balance;

	lwkt_create((void (*)(void *))fq_dispatcher, diskctx, &td_core,
		    NULL, 0, -1, "fq_dispatch_%s",
		    ds_diskctx->dp->d_cdev->si_name);
	lwkt_create((void (*)(void *))fq_balance_thread, diskctx, &td_balance,
		    NULL, 0, -1, "fq_balance_%s",
		    ds_diskctx->dp->d_cdev->si_name);
	diskctx->td_balance = td_balance;

	return 0;
}



static void
fq_teardown(struct dsched_disk_ctx *ds_diskctx)
{
	struct fq_disk_ctx *diskctx = (struct fq_disk_ctx *)ds_diskctx;
	KKASSERT(diskctx != NULL);

	/* Basically kill the dispatcher thread */
	diskctx->die = 1;
	wakeup(diskctx->td_balance);
	wakeup(diskctx);
	tsleep(diskctx, 0, "fq_dispatcher", hz/5); /* wait 200 ms */
	wakeup(diskctx->td_balance);
	wakeup(diskctx);
	tsleep(diskctx, 0, "fq_dispatcher", hz/10); /* wait 100 ms */
	wakeup(diskctx->td_balance);
	wakeup(diskctx);
}


/* Must be called with locked diskctx */
void
fq_drain(struct fq_disk_ctx *diskctx, int mode)
{
	struct dsched_thread_io *ds_tdio, *ds_tdio2;
	struct fq_thread_io *tdio;
	struct bio *bio, *bio2;

	TAILQ_FOREACH_MUTABLE(ds_tdio, &diskctx->head.tdio_list, dlink, ds_tdio2) {
		tdio = (struct fq_thread_io *)ds_tdio;
		if (tdio->head.qlength == 0)
			continue;

		DSCHED_THREAD_IO_LOCK(&tdio->head);
		TAILQ_FOREACH_MUTABLE(bio, &tdio->head.queue, link, bio2) {
			TAILQ_REMOVE(&tdio->head.queue, bio, link);
			--tdio->head.qlength;
			if (__predict_false(mode == FQ_DRAIN_CANCEL)) {
				/* FQ_DRAIN_CANCEL */
				dsched_cancel_bio(bio);
				atomic_add_int(&fq_stats.cancelled, 1);

				/* Release ref acquired on fq_queue */
				/* XXX: possible failure point */
				dsched_thread_io_unref(&tdio->head);
			} else {
				/* FQ_DRAIN_FLUSH */
				fq_dispatch(diskctx, bio, tdio);
			}
		}
		DSCHED_THREAD_IO_UNLOCK(&tdio->head);
	}
	return;
}

static void
fq_cancel(struct dsched_disk_ctx *ds_diskctx)
{
	struct fq_disk_ctx	*diskctx = (struct fq_disk_ctx *)ds_diskctx;

	KKASSERT(diskctx != NULL);

	/*
	 * all bios not in flight are queued in their respective tdios.
	 * good thing we have a list of tdios per disk diskctx.
	 */
	DSCHED_DISK_CTX_LOCK(&diskctx->head);
	fq_drain(diskctx, FQ_DRAIN_CANCEL);
	DSCHED_DISK_CTX_UNLOCK(&diskctx->head);
}


static int
fq_queue(struct dsched_disk_ctx *ds_diskctx, struct dsched_thread_io *ds_tdio, struct bio *obio)
{
	struct bio *bio, *bio2;
	struct fq_thread_io	*tdio;
	struct fq_disk_ctx	*diskctx;
	int max_tp, transactions;

	/* We don't handle flushes, let dsched dispatch them */
	if (__predict_false(obio->bio_buf->b_cmd == BUF_CMD_FLUSH))
		return (EINVAL);

	tdio = (struct fq_thread_io *)ds_tdio;
	diskctx = (struct fq_disk_ctx *)ds_diskctx;

	if (atomic_cmpset_int(&tdio->rebalance, 1, 0))
		fq_balance_self(tdio);

	max_tp = tdio->max_tp;
	transactions = tdio->issued;

	/* | No rate limiting || Hasn't reached limit rate | */
	if ((max_tp == 0) || (transactions < max_tp)) {
		/*
		 * Process pending bios from previous _queue() actions that
		 * have been rate-limited and hence queued in the tdio.
		 */
		KKASSERT(tdio->head.qlength >= 0);

		if (tdio->head.qlength > 0) {
			DSCHED_THREAD_IO_LOCK(&tdio->head);

			TAILQ_FOREACH_MUTABLE(bio, &tdio->head.queue, link, bio2) {
				/* Rebalance ourselves if required */
				if (atomic_cmpset_int(&tdio->rebalance, 1, 0))
					fq_balance_self(tdio);
				if ((tdio->max_tp > 0) && (tdio->issued >= tdio->max_tp))
					break;
				TAILQ_REMOVE(&tdio->head.queue, bio, link);
				--tdio->head.qlength;

				/*
				 * beware that we do have an tdio reference from the
				 * queueing
				 */
				fq_dispatch(diskctx, bio, tdio);
			}
			DSCHED_THREAD_IO_UNLOCK(&tdio->head);
		}

		/* Nothing is pending from previous IO, so just pass it down */
		dsched_thread_io_ref(&tdio->head);

		fq_dispatch(diskctx, obio, tdio);
	} else {
		/*
		 * This thread has exceeeded its fair share,
		 * the transactions are now rate limited. At
		 * this point, the rate would be exceeded, so
		 * we just queue requests instead of
		 * despatching them.
		 */
		DSCHED_THREAD_IO_LOCK(&tdio->head);
		dsched_thread_io_ref(&tdio->head);

		/*
		 * Prioritize reads by inserting them at the front of the
		 * queue.
		 *
		 * XXX: this might cause issues with data that should
		 * 	have been written and is being read, but hasn't
		 *	actually been written yet.
		 */
		if (obio->bio_buf->b_cmd == BUF_CMD_READ)
			TAILQ_INSERT_HEAD(&tdio->head.queue, obio, link);
		else
			TAILQ_INSERT_TAIL(&tdio->head.queue, obio, link);

		++tdio->head.qlength;
		DSCHED_THREAD_IO_UNLOCK(&tdio->head);
	}

	return 0;
}


void
fq_completed(struct bio *bp)
{
	struct bio *obio;
	int	delta;
	struct fq_thread_io	*tdio;
	struct fq_disk_ctx	*diskctx;
	struct disk	*dp;
	int transactions, latency;

	struct timeval tv;

	getmicrotime(&tv);

	dp = dsched_get_bio_dp(bp);
	diskctx = dsched_get_disk_priv(dp);
	tdio = dsched_get_bio_priv(bp);
	KKASSERT(tdio != NULL);
	KKASSERT(diskctx != NULL);

	dsched_disk_ctx_ref(&diskctx->head);
	atomic_subtract_int(&diskctx->incomplete_tp, 1);

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
		if ((diskctx->incomplete_tp <= 1) && (!diskctx->idle)) {
			diskctx->idle = 1;	/* Mark disk as idle */
			diskctx->start_idle = tv;	/* Save start idle time */
			wakeup(diskctx);		/* Wake up fq_dispatcher */
		}
		transactions = atomic_fetchadd_int(&tdio->transactions, 1);
		latency = tdio->avg_latency;

		if (latency != 0) {
			/* Moving averager, ((n-1)*avg_{n-1} + x) / n */
			latency = (int)(((int64_t)(transactions) *
			    (int64_t)latency + (int64_t)delta) / ((int64_t)transactions + 1));
			KKASSERT(latency > 0);
		} else {
			latency = delta;
		}

		tdio->avg_latency = latency;

		atomic_add_int(&fq_stats.transactions_completed, 1);
	}

	dsched_disk_ctx_unref(&diskctx->head);
	/* decrease the ref count that was bumped for us on dispatch */
	dsched_thread_io_unref(&tdio->head);

	obio = pop_bio(bp);
	biodone(obio);
}

void
fq_dispatch(struct fq_disk_ctx *diskctx, struct bio *bio,
    struct fq_thread_io *tdio)
{
	struct timeval tv;

	if (diskctx->idle) {
		getmicrotime(&tv);
		atomic_add_int(&diskctx->idle_time,
		    (int)(1000000*((tv.tv_sec - diskctx->start_idle.tv_sec)) +
		    (tv.tv_usec - diskctx->start_idle.tv_usec)));
		diskctx->idle = 0;
	}
	dsched_strategy_async(diskctx->head.dp, bio, fq_completed, tdio);

	atomic_add_int(&tdio->issued, 1);
	atomic_add_int(&diskctx->incomplete_tp, 1);
	atomic_add_int(&fq_stats.transactions, 1);
}
