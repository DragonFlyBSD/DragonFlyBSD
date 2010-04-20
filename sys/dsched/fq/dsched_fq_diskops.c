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
extern struct lock	fq_tdctx_lock;
extern TAILQ_HEAD(, fq_thread_ctx)	dsched_tdctx_list;
extern struct callout	fq_callout;

struct dsched_policy dsched_fq_policy = {
	.name = "fq",

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
	struct	fq_disk_ctx	*diskctx;
	struct fq_thread_ctx	*tdctx;
	struct fq_thread_io	*tdio;
	struct thread *td_core, *td_balance;

	diskctx = fq_disk_ctx_alloc(dp);
	fq_disk_ctx_ref(diskctx);
	dsched_set_disk_priv(dp, diskctx);

	FQ_GLOBAL_THREAD_CTX_LOCK();
	TAILQ_FOREACH(tdctx, &dsched_tdctx_list, link) {
		tdio = fq_thread_io_alloc(dp, tdctx);
#if 0
		fq_thread_io_ref(tdio);
#endif
	}
	FQ_GLOBAL_THREAD_CTX_UNLOCK();

	lwkt_create((void (*)(void *))fq_dispatcher, diskctx, &td_core, NULL,
	    TDF_MPSAFE, -1, "fq_dispatch_%s", dp->d_cdev->si_name);
	lwkt_create((void (*)(void *))fq_balance_thread, diskctx, &td_balance,
	    NULL, TDF_MPSAFE, -1, "fq_balance_%s", dp->d_cdev->si_name);
	diskctx->td_balance = td_balance;

	return 0;
}



static void
fq_teardown(struct disk *dp)
{
	struct fq_disk_ctx *diskctx;

	diskctx = dsched_get_disk_priv(dp);
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

	fq_disk_ctx_unref(diskctx); /* from prepare */
	fq_disk_ctx_unref(diskctx); /* from alloc */

	dsched_set_disk_priv(dp, NULL);
}


/* Must be called with locked diskctx */
void
fq_drain(struct fq_disk_ctx *diskctx, int mode)
{
	struct fq_thread_io *tdio, *tdio2;
	struct bio *bio, *bio2;

	TAILQ_FOREACH_MUTABLE(tdio, &diskctx->fq_tdio_list, dlink, tdio2) {
		if (tdio->qlength == 0)
			continue;

		FQ_THREAD_IO_LOCK(tdio);
		TAILQ_FOREACH_MUTABLE(bio, &tdio->queue, link, bio2) {
			TAILQ_REMOVE(&tdio->queue, bio, link);
			--tdio->qlength;
			if (__predict_false(mode == FQ_DRAIN_CANCEL)) {
				/* FQ_DRAIN_CANCEL */
				dsched_cancel_bio(bio);
				atomic_add_int(&fq_stats.cancelled, 1);

				/* Release ref acquired on fq_queue */
				/* XXX: possible failure point */
				fq_thread_io_unref(tdio);
			} else {
				/* FQ_DRAIN_FLUSH */
				fq_dispatch(diskctx, bio, tdio);
			}
		}
		FQ_THREAD_IO_UNLOCK(tdio);
	}
	return;
}


static void
fq_flush(struct disk *dp, struct bio *bio)
{
	/* we don't do anything here */
}


static void
fq_cancel(struct disk *dp)
{
	struct fq_disk_ctx	*diskctx;

	diskctx = dsched_get_disk_priv(dp);
	KKASSERT(diskctx != NULL);

	/*
	 * all bios not in flight are queued in their respective tdios.
	 * good thing we have a list of tdios per disk diskctx.
	 */
	FQ_DISK_CTX_LOCK(diskctx);
	fq_drain(diskctx, FQ_DRAIN_CANCEL);
	FQ_DISK_CTX_UNLOCK(diskctx);
}


static int
fq_queue(struct disk *dp, struct bio *obio)
{
	struct bio *bio, *bio2;
	struct fq_thread_ctx	*tdctx;
	struct fq_thread_io	*tdio;
	struct fq_disk_ctx	*diskctx;
	int found = 0;
	int max_tp, transactions;

	/* We don't handle flushes, let dsched dispatch them */
	if (__predict_false(obio->bio_buf->b_cmd == BUF_CMD_FLUSH))
		return (EINVAL);

	/* get tdctx and tdio */
	tdctx = dsched_get_buf_priv(obio->bio_buf);

	/*
	 * XXX: hack. we don't want the assert because some null-tdctxs are
	 * leaking through; just dispatch them. These come from the
	 * mi_startup() mess, which does the initial root mount.
	 */
#if 0
	KKASSERT(tdctx != NULL);
#endif
	if (tdctx == NULL) {
		/* We don't handle this case, let dsched dispatch */
		atomic_add_int(&fq_stats.no_tdctx, 1);
		return (EINVAL);
	}


	FQ_THREAD_CTX_LOCK(tdctx);
#if 0
	kprintf("fq_queue, tdctx = %p\n", tdctx);
#endif
	KKASSERT(!TAILQ_EMPTY(&tdctx->fq_tdio_list));
	TAILQ_FOREACH(tdio, &tdctx->fq_tdio_list, link) {
		if (tdio->dp == dp) {
			fq_thread_io_ref(tdio);
			found = 1;
			break;
		}
	}
	FQ_THREAD_CTX_UNLOCK(tdctx);
	dsched_clr_buf_priv(obio->bio_buf);
	fq_thread_ctx_unref(tdctx); /* acquired on new_buf */

	KKASSERT(found == 1);
	diskctx = dsched_get_disk_priv(dp);

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
		KKASSERT(tdio->qlength >= 0);

		if (tdio->qlength > 0) {
			FQ_THREAD_IO_LOCK(tdio);

			TAILQ_FOREACH_MUTABLE(bio, &tdio->queue, link, bio2) {
				/* Rebalance ourselves if required */
				if (atomic_cmpset_int(&tdio->rebalance, 1, 0))
					fq_balance_self(tdio);
				if ((tdio->max_tp > 0) && (tdio->issued >= tdio->max_tp))
					break;
				TAILQ_REMOVE(&tdio->queue, bio, link);
				--tdio->qlength;

				/*
				 * beware that we do have an tdio reference from the
				 * queueing
				 */
				fq_dispatch(diskctx, bio, tdio);
			}
			FQ_THREAD_IO_UNLOCK(tdio);
		}

		/* Nothing is pending from previous IO, so just pass it down */
		fq_thread_io_ref(tdio);

		fq_dispatch(diskctx, obio, tdio);
	} else {
		/*
		 * This thread has exceeeded its fair share,
		 * the transactions are now rate limited. At
		 * this point, the rate would be exceeded, so
		 * we just queue requests instead of
		 * despatching them.
		 */
		FQ_THREAD_IO_LOCK(tdio);
		fq_thread_io_ref(tdio);

		/*
		 * Prioritize reads by inserting them at the front of the
		 * queue.
		 *
		 * XXX: this might cause issues with data that should
		 * 	have been written and is being read, but hasn't
		 *	actually been written yet.
		 */
		if (obio->bio_buf->b_cmd == BUF_CMD_READ)
			TAILQ_INSERT_HEAD(&tdio->queue, obio, link);
		else
			TAILQ_INSERT_TAIL(&tdio->queue, obio, link);

		++tdio->qlength;
		FQ_THREAD_IO_UNLOCK(tdio);
	}

	fq_thread_io_unref(tdio);
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

	fq_disk_ctx_ref(diskctx);
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

	fq_disk_ctx_unref(diskctx);
	/* decrease the ref count that was bumped for us on dispatch */
	fq_thread_io_unref(tdio);

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
	dsched_strategy_async(diskctx->dp, bio, fq_completed, tdio);

	atomic_add_int(&tdio->issued, 1);
	atomic_add_int(&diskctx->incomplete_tp, 1);
	atomic_add_int(&fq_stats.transactions, 1);
}
