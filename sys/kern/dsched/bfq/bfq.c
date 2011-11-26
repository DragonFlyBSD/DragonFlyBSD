/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Brills Peng <brillsp@gmail.com>
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


/*
 * BFQ disk scheduler, the algorithm routines and the interfaces with the
 * dsched framework.
 *
 */

#include <inttypes.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>
#include <sys/dsched.h>
#include <sys/fcntl.h>
#include <machine/varargs.h>

#include <kern/dsched/bfq/bfq.h>
#include <kern/dsched/bfq/bfq_helper_thread.h>

#define _DSCHED_BFQ_BFQ_C_
#include <kern/dsched/bfq/bfq_ktr.h>

/* Make sure our structs fit */
CTASSERT(sizeof(struct bfq_thread_io) <= DSCHED_THREAD_IO_MAX_SZ);
CTASSERT(sizeof(struct bfq_disk_ctx) <= DSCHED_DISK_CTX_MAX_SZ);


static dsched_prepare_t		bfq_prepare;
static dsched_teardown_t	bfq_teardown;
static dsched_cancel_t		bfq_cancel_all;
static dsched_queue_t		bfq_queue;
static dsched_new_tdio_t	bfq_new_tdio;
static dsched_destroy_tdio_t	bfq_destroy_tdio;
static dsched_bio_done_t	bfq_bio_done;


static void bfq_update_peak_rate(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio);
static int bfq_slow_tdio(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio);
static void bfq_expire(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio, enum bfq_expire_reason reason);
static void bfq_update_tdio_seek_avg(struct bfq_thread_io *bfq_tdio, struct bio *bp);
static void bfq_update_tdio_ttime_avg(struct bfq_thread_io *bfq_tdio);
static void bfq_update_as_avg_wait(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio, int flag);
static void bfq_update_avg_time_slice(struct bfq_disk_ctx *bfq_diskctx, struct timeval tv);



struct dsched_policy dsched_bfq_policy = {
	.name		= "bfq",
	.prepare	= bfq_prepare,
	.teardown	= bfq_teardown,
	.cancel_all	= bfq_cancel_all,
	.bio_queue	= bfq_queue,
	.new_tdio	= bfq_new_tdio,
	.destroy_tdio	= bfq_destroy_tdio,
	.bio_done	= bfq_bio_done,
	.polling_func	= (void (*)(struct dsched_disk_ctx *))helper_msg_dequeue,
};


struct sysctl_oid *bfq_mod_oid;

struct dsched_bfq_stats bfq_stats;

static int dsched_bfq_version_maj = 1;
static int dsched_bfq_version_min = 0;

/*
 * bfq_prepare(): the .prepare callback of the bfq policy. Initialize
 * all fields in bfq_diskctx and initialize the corresponding helper
 * thread.
 *
 * lock: none
 * refcount: none
 *
 * Returns 0
 */
static int
bfq_prepare(struct dsched_disk_ctx *diskctx)
{
	struct bfq_disk_ctx *bfq_diskctx = (struct bfq_disk_ctx *)diskctx;

	BFQ_LOCKINIT(bfq_diskctx);

	bfq_diskctx->pending_dequeue = 0;

	wf2q_init(&bfq_diskctx->bfq_wf2q);

	callout_init_mp(&bfq_diskctx->bfq_callout);

	bfq_diskctx->bfq_blockon = NULL;
	bfq_diskctx->bfq_active_tdio = NULL;
	bfq_diskctx->bfq_remaining_budget = 0;

	bfq_diskctx->bfq_max_budget = BFQ_DEFAULT_MAX_BUDGET;
	bfq_diskctx->bfq_peak_rate_samples = 0;
	bfq_diskctx->bfq_peak_rate = 0;

#if 0
	bfq_diskctx->bfq_flag = BFQ_FLAG_AS | BFQ_FLAG_AUTO_MAX_BUDGET;
#endif
	bfq_diskctx->bfq_flag = BFQ_FLAG_AS;

	bfq_diskctx->bfq_as_miss = 0;
	bfq_diskctx->bfq_as_hit = 0;

	bfq_diskctx->bfq_as_avg_wait_miss = 0;
	bfq_diskctx->bfq_as_avg_wait_all = 0;
	bfq_diskctx->bfq_as_max_wait = 0;
	bfq_diskctx->bfq_as_max_wait2 = 0;
	bfq_diskctx->bfq_as_high_wait_count = 0;
	bfq_diskctx->bfq_as_high_wait_count2 = 0;

	bfq_diskctx->bfq_avg_time_slice = 0;
	bfq_diskctx->bfq_max_time_slice = 0;
	bfq_diskctx->bfq_high_time_slice_count = 0;

	/* initiailize the helper thread */
	helper_init(bfq_diskctx);

	dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: initialized!\n");
	return 0;
}

/*
 * bfq_teardown(): .teardown callback of the bfq policy. Send message
 * of killing to the helper thread and deallocate resources used by
 * the helper thread (currently the objcache)
 *
 * XXX: deadlock causing when the caller of bfq_teardown() and the
 * helper thread are on the same CPU.
 *
 * lock: none
 * refcount: none
 *
 */

static void
bfq_teardown(struct dsched_disk_ctx *diskctx)
{
	struct bfq_disk_ctx *bfq_diskctx = (struct bfq_disk_ctx *)diskctx;
	KKASSERT(diskctx);

	helper_msg_kill(bfq_diskctx);

	tsleep(diskctx, 0, "teardn", hz * 3 / 2);

	helper_uninit(bfq_diskctx);
}

/*
 * bfq_cancel_all(): .cancel_all callback of the bfq policy. Cancel
 * all bios that queue in each bfq_thread_io structure in the
 * wf2q tree.
 *
 * lock:
 *	BFQ_LOCK: protect from wf2q_insert operation in bfq_queue() and
 *	bfq_dequeue(); wf2q_get_next operation in bfq_dequeue()
 *	THREAD_IO_LOCK: protect from queue iteration in bfq_dequeue() and
 *	queue insertion in bfq_queue()
 *
 * refcount:
 *	unref thread_io structures; they are referenced in queue(),
 *	when a bio is queued. The refcount may decrease to zero.
 *
 */
static void
bfq_cancel_all(struct dsched_disk_ctx *diskctx)
{
	struct bio *bio;
	struct bfq_thread_io *bfq_tdio;
	struct bfq_disk_ctx *bfq_diskctx = (struct bfq_disk_ctx *)diskctx;

	BFQ_LOCK(bfq_diskctx);

	while ((bfq_tdio = wf2q_get_next_thread_io(&bfq_diskctx->bfq_wf2q))) {
		DSCHED_THREAD_IO_LOCK(&bfq_tdio->head);
		KKASSERT(lockstatus(&bfq_tdio->head.lock, curthread) == LK_EXCLUSIVE);

		while ((bio = TAILQ_FIRST(&bfq_tdio->head.queue))) {
			bfq_tdio->head.qlength--;
			TAILQ_REMOVE(&bfq_tdio->head.queue, bio, link);
			dsched_cancel_bio(bio);
			dsched_thread_io_unref(&bfq_tdio->head);
		}

		KKASSERT(bfq_tdio->head.qlength == 0);
		DSCHED_THREAD_IO_UNLOCK(&bfq_tdio->head);
	}

	BFQ_UNLOCK(bfq_diskctx);
}

/*
 * bfq_new_tdio(): .new_tdio callback of the bfq policy. Initialize
 * the bfq_thread_io structure.
 *
 * lock: none
 * refcount: none
 */
static void
bfq_new_tdio(struct dsched_thread_io *tdio)
{
	struct bfq_thread_io *bfq_tdio = (struct bfq_thread_io *) tdio;

	/* the queue has to be initialized some where else */
	tdio->qlength = 0;

	tdio->debug_priv = 0xF00FF00F;

	bfq_tdio->budget = BFQ_DEFAULT_MIN_BUDGET;
	bfq_tdio->weight = BFQ_DEFAULT_WEIGHT;

	bfq_tdio->tdio_as_switch = 1;
	bfq_tdio->maybe_timeout = 0;

	bfq_tdio->seek_samples = 0;
	bfq_tdio->seek_avg = 0;
	bfq_tdio->seek_total = 0;
	bfq_tdio->ttime_samples = 0;
	bfq_tdio->ttime_avg = 0;
	bfq_tdio->service_received = 0;
	bfq_tdio->bio_dispatched = 0;
	bfq_tdio->bio_completed = 0;

	KTR_LOG(dsched_bfq_thread_created, bfq_tdio);
}

/*
 * bfq_helper_destroy_tdio(): called after a thread_io struct is destroyed.
 * if the scheduler is AS waiting for a destroyed tdio, this function resumes
 * the scheduler.
 *
 * lock:
 *	BFQ_LOCK: protect from nullify bfq_diskctx->bfq_blockon/bfq_active_tdio
 *	in bfq_timeout()
 *
 * refcount: none
 *
 * Calling path: bfq_destroy_tdio --lwkt_msg--> helper_thread --call--> me
 *
 */
void
bfq_helper_destroy_tdio(struct dsched_thread_io *tdio, struct bfq_disk_ctx *bfq_diskctx)
{
	KKASSERT(bfq_diskctx);

	BFQ_LOCK(bfq_diskctx);

	/*
	 * Test whether the scheduler is pending on the tdio to
	 * be destroyed.
	 */
	if (((struct dsched_thread_io *)bfq_diskctx->bfq_blockon == tdio) &&
	    callout_pending(&bfq_diskctx->bfq_callout)) {
		dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: pending on a being destroyed thread!\n");

		callout_stop(&bfq_diskctx->bfq_callout);

		bfq_diskctx->bfq_blockon = NULL;
		bfq_diskctx->bfq_active_tdio = NULL;

		BFQ_UNLOCK(bfq_diskctx);

		helper_msg_dequeue(bfq_diskctx);
		return;
	}
	BFQ_UNLOCK(bfq_diskctx);

}

/*
 * bfq_destroy_tdio(): .destroy_tdio callback of the bfq policy
 *
 * Called immediate after a dsched_thread_io struct's refcount decreases
 * to zero. This function will record the seek_avg and ttime_avg of the
 * destroyed thread with the KTR facility.
 *
 * lock: none
 *
 * refcount: the tdio's refcount should be zero. It may be nuked, and
 * any read/write to the tdio is not safe by then.
 */
static void
bfq_destroy_tdio(struct dsched_thread_io *tdio)
{
	struct bfq_thread_io *bfq_tdio = (struct bfq_thread_io *)tdio;

	/*
	 * do not log threads without I/O
	 */
	if (bfq_tdio->seek_samples != 0 || bfq_tdio->ttime_samples != 0) {
		KTR_LOG(dsched_bfq_thread_seek_avg, bfq_tdio, bfq_tdio->seek_avg );
		KTR_LOG(dsched_bfq_thread_ttime_avg, bfq_tdio, bfq_tdio->ttime_avg);
	}

	helper_msg_destroy_tdio((struct bfq_disk_ctx *)tdio->diskctx, tdio);
}

/*
 * bfq_bio_done(): .bio_done callback of the bfq policy
 *
 * Called after a bio is done, (by request_polling_biodone of dsched).
 * This function judges whet her a thread consumes up its time slice, and
 * if so, it will set the maybe_timeout flag in bfq_tdio structure. Any
 * further action of that thread or the bfq scheduler will cause the
 * thread to be expired. (in bfq_queue() or in bfq_dequeue())
 *
 * This function requires the bfq_tdio pointer of the thread that pushes
 * bp to be stored by dsched_set_bio_priv() earlier. Currently it is
 * stored when bfq_queue() is called.
 *
 * lock: none. This function CANNOT be blocked by any lock
 *
 * refcount:
 *	the corresponding tdio's refcount should decrease by 1 after
 *	this function call. The counterpart increasing is in bfq_queue().
 *	For each bio pushed down, we increase the refcount of the pushing
 *	tdio.
 */
static void
bfq_bio_done(struct bio *bp)
{
	struct disk *dp = dsched_get_bio_dp(bp);
	struct bfq_thread_io *bfq_tdio = dsched_get_bio_priv(bp);
	struct bfq_disk_ctx *bfq_diskctx = dsched_get_disk_priv(dp);
	struct timeval tv;
	int ticks_expired;

	KKASSERT(bfq_tdio);

	dsched_thread_io_ref(&bfq_tdio->head);

	atomic_add_int(&bfq_tdio->bio_completed, 1);

	/* the tdio has already expired */
	if (bfq_tdio != bfq_diskctx->bfq_active_tdio)
		goto rtn;
	atomic_add_int(&bfq_tdio->service_received, BIO_SIZE(bp));

	/* current time */
	getmicrotime(&tv);
	bfq_tdio->last_request_done_time = tv;
	timevalsub (&tv, &bfq_tdio->service_start_time);
	ticks_expired = tvtohz_high(&tv);

	/* the thread has run out its time slice */
	if ((ticks_expired != 0x7fffffff) &&
	    (ticks_expired >= BFQ_SLICE_TIMEOUT)) {
		/*
		 * we cannot block here, so just set a flag
		 */
#if 0
		bfq_tdio->maybe_timeout = 1;
#endif
		if (atomic_cmpset_int(&bfq_tdio->maybe_timeout, 0, 1)) {
			bfq_update_avg_time_slice(bfq_diskctx, tv);
			dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: %p may time out\n", bfq_tdio);
		}
	}
rtn:
	dsched_thread_io_unref(&bfq_tdio->head); /* ref'ed in this function */
	dsched_thread_io_unref(&bfq_tdio->head); /* ref'ed in queue() */

}

/*
 * bfq_timeout(): called after the callout alarm strikes.
 *
 * This function getting called indicates that after waiting for
 * BFQ_T_WAIT / BFQ_T_WAIT_MIN ticks, the thread "active_tdio"
 * represents does not push any further bios. This tdio should
 * be expired with the reason BFQ_REASON_TOO_IDLE, but if the tdio
 * is marked as timeout (in bfq_biodone()) first, we expire it
 * for BFQ_REASON_TIMEOUT. The bfq scheduler should resume working
 * (and pick another thread to serve).
 *
 * It is possible that this function gets called a litter after
 * the thread pushes a bio with bfq_queue(), and thus a "fake timeout"
 * happens. We treat it as the callout does not strike, and continue
 * to serve the active_tdio.
 *
 * lock:
 *	BFQ_LOCK: protect bfq_diskctx->blockon and bfq_diskctx->active_tdio
 *	they should either changed in bfq_queue() or in this function,
 *	atomically.
 *	TDIO_LOCK: protect from dequeue() updateing the budget by the
 *	maybe_timeout branch. (Not necessary, because we already hold the
 *	BFQ_LOCK, and no one else could change the budget of the tdio)
 *
 * refcount:
 *  the refcount of bfq_diskctx->bfq_active_tdio will decrease one
 *  after this function. (The counterpart increasing is in bfq_dequeue(),
 *  before resetting the callout alarm.)
 *
 * AS timeout:
 * during the waiting period, no bio is pushed by the being
 * waited tdio
 *
 * Calling path:
 * callout facility --> helper_msg_timeout --lwkt_msg--> helper thread
 *  --> me
 */
void
bfq_timeout(void *p)
{
	/* waiting time out:
	 * no deceptive idleness, and unblock dispatching
	 */
	struct bfq_disk_ctx *bfq_diskctx = (struct bfq_disk_ctx *)p;
	struct bfq_thread_io *bfq_tdio;

	BFQ_LOCK(bfq_diskctx);

	/*
	 * the timeout occurs after the thread
	 * pushing one more bio
	 */
	if (bfq_diskctx->bfq_blockon == NULL) {
		dsched_debug(BFQ_DEBUG_VERBOSE , "BFQ: fake AS timeout \n");
		goto rtn;
	}

	bfq_diskctx->bfq_as_miss++;

	KKASSERT(bfq_diskctx->bfq_active_tdio);
	bfq_tdio = bfq_diskctx->bfq_active_tdio;

	DSCHED_THREAD_IO_LOCK(&bfq_tdio->head);

	bfq_update_as_avg_wait(bfq_diskctx, bfq_tdio, BFQ_AS_STAT_ALL|BFQ_AS_STAT_ONLY_MISS);

	bfq_diskctx->bfq_blockon = NULL;
	bfq_diskctx->bfq_active_tdio = NULL;
	dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: unblocked %p\n", bfq_tdio);

	wf2q_update_vd(bfq_tdio, bfq_tdio->budget - bfq_diskctx->bfq_remaining_budget);
	/*
	 * the time slice expired before as timeout
	 * this should be REASON_TIMEOUT
	 */
	if (bfq_tdio->maybe_timeout) {
		bfq_expire(bfq_diskctx, bfq_tdio, BFQ_REASON_TIMEOUT);
		dsched_debug(BFQ_DEBUG_VERBOSE, "%p time out in timeout()\n", bfq_tdio);
	} else {
		bfq_expire(bfq_diskctx, bfq_tdio, BFQ_REASON_TOO_IDLE);
		dsched_debug(BFQ_DEBUG_VERBOSE, "%p too idle\n", bfq_tdio);
	}

	DSCHED_THREAD_IO_UNLOCK(&bfq_tdio->head);

	/* ref'ed in dequeue(), before resetting callout */
	dsched_thread_io_unref(&bfq_tdio->head);
rtn:
	BFQ_UNLOCK(bfq_diskctx);
	helper_msg_dequeue(bfq_diskctx);
}

/*
 * bfq_queue(): .queue callback of the bfq policy.
 *
 * A thread calls this function to hand in its I/O requests (bio).
 * Their bios are stored in the per-thread queue, in tdio structure.
 * Currently, the sync/async bios are queued together, which may cause
 * some issues on performance.
 *
 * Besides queueing bios, this function also calculates the average
 * thinking time and average seek distance of a thread, using the
 * information in bio structure.
 *
 * If the calling thread is waiting by the bfq scheduler due to
 * the AS feature, this function will cancel the callout alarm
 * and resume the scheduler to continue serving this thread.
 *
 * lock:
 *   THREAD_IO_LOCK: protect from queue iteration in bfq_dequeue()
 *   BFQ_LOCK: protect from other insertions/deletions in wf2q_augtree
 *   in bfq_queue() or bfq_dequeue().
 *
 * refcount:
 *   If the calling thread is waited by the scheduler, the refcount
 *   of the related tdio will decrease by 1 after this function. The
 *   counterpart increasing is in bfq_dequeue(), before resetting the
 *   callout alarm.
 *
 * Return value:
 *  EINVAL: if bio->bio_buf->b_cmd == BUF_CMD_FLUSH
 *  0: bio is queued successfully.
 */
static int
bfq_queue(struct dsched_disk_ctx *diskctx, struct dsched_thread_io *tdio,
		struct  bio *bio)
{
	struct bfq_disk_ctx *bfq_diskctx = (struct bfq_disk_ctx *)diskctx;
	struct bfq_thread_io *bfq_tdio = (struct bfq_thread_io *)tdio;
	int original_qlength;

	/* we do not handle flush requests. push it down to dsched */
	if (__predict_false(bio->bio_buf->b_cmd == BUF_CMD_FLUSH))
		return (EINVAL);

	DSCHED_THREAD_IO_LOCK(tdio);
	KKASSERT(tdio->debug_priv == 0xF00FF00F);
	dsched_debug(BFQ_DEBUG_NORMAL, "bfq: tdio %p pushes bio %p\n", bfq_tdio, bio);

	dsched_set_bio_priv(bio, tdio);
	dsched_thread_io_ref(tdio);

	if ((bio->bio_buf->b_cmd == BUF_CMD_READ) ||
	    (bio->bio_buf->b_cmd == BUF_CMD_WRITE)) {
		bfq_update_tdio_seek_avg(bfq_tdio, bio);
	}

	bfq_update_tdio_ttime_avg(bfq_tdio);

	/* update last_bio_pushed_time */
	getmicrotime(&bfq_tdio->last_bio_pushed_time);

	if ((bfq_tdio->seek_samples > BFQ_VALID_MIN_SAMPLES) &&
	    BFQ_TDIO_SEEKY(bfq_tdio))
		dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: tdio %p is seeky\n", bfq_tdio);

	/*
	 * If a tdio taks too long to think, we disable the AS feature of it.
	 */
	if ((bfq_tdio->ttime_samples > BFQ_VALID_MIN_SAMPLES) &&
	    (bfq_tdio->ttime_avg > BFQ_T_WAIT * (1000 / hz) * 1000) &&
	    (bfq_tdio->service_received > bfq_tdio->budget / 8)) {
		dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: tdio %p takes too long time to think\n", bfq_tdio);
		bfq_tdio->tdio_as_switch = 0;
	} else {
		bfq_tdio->tdio_as_switch = 1;
	}

	/* insert the bio into the tdio's own queue */
	KKASSERT(lockstatus(&tdio->lock, curthread) == LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&tdio->queue, bio, link);
#if 0
	tdio->qlength++;
#endif
	original_qlength = atomic_fetchadd_int(&tdio->qlength, 1);
	DSCHED_THREAD_IO_UNLOCK(tdio);
	/*
	 * A new thread:
	 * In dequeue function, we remove the thread
	 * from the aug-tree if it has no further bios.
	 * Therefore "new" means a really new thread (a
	 * newly created thread or a thread that pushed no more
	 * bios when the scheduler was waiting for it) or
	 * one that was removed from the aug-tree earlier.
	 */
	if (original_qlength == 0) {
		/*
		 * a really new thread
		 */
		BFQ_LOCK(bfq_diskctx);
		if (bfq_tdio != bfq_diskctx->bfq_active_tdio) {
			/* insert the tdio into the wf2q queue */
			wf2q_insert_thread_io(&bfq_diskctx->bfq_wf2q, bfq_tdio);
		} else {
			/*
			 * the thread being waited by the scheduler
			 */
			if (bfq_diskctx->bfq_blockon == bfq_tdio) {
				/*
				 * XXX: possible race condition here:
				 * if the callout function is triggered when
				 * the following code is executed, then after
				 * releasing the TDIO lock, the callout function
				 * will set the thread inactive and it will never
				 * be inserted into the aug-tree (so its bio pushed
				 * this time will not be dispatched) until it pushes
				 * further bios
				 */
				bfq_diskctx->bfq_as_hit++;
				bfq_update_as_avg_wait(bfq_diskctx, bfq_tdio, BFQ_AS_STAT_ALL);

				if (callout_pending(&bfq_diskctx->bfq_callout))
					callout_stop(&bfq_diskctx->bfq_callout);
				bfq_diskctx->bfq_blockon = NULL;

				/* ref'ed in dequeue(), before resetting callout */
				dsched_thread_io_unref(&bfq_tdio->head);

				dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: %p pushes a new bio when AS\n", bfq_tdio);
			}
		}

		BFQ_UNLOCK(bfq_diskctx);
	}

	helper_msg_dequeue(bfq_diskctx);

	return 0;
}

/*
 * bfq_dequeue(): dispatch bios to the disk driver.
 *
 * This function will push as many bios as the number of free slots
 * in the tag queue.
 *
 * In the progress of dispatching, the following events may happen:
 *  - Current thread is timeout: Expire the current thread for
 *    BFQ_REASON_TIMEOUT, and select a new thread to serve in the
 *    wf2q tree.
 *
 *  - Current thread runs out of its budget: Expire the current thread
 *    for BFQ_REASON_OUT_OF_BUDGET, and select a new thread to serve
 *
 *  - Current thread has no further bios in its queue: if the AS feature
 *    is turned on, the bfq scheduler sets an alarm and starts to suspend.
 *    The bfq_timeout() or bfq_queue() calls may resume the scheduler.
 *
 * Implementation note: The bios selected to be dispatched will first
 * be stored in an array bio_do_dispatch. After this function releases
 * all the locks it holds, it will call dsched_strategy_request_polling()
 * for each bio stored.
 *
 * With the help of bfq_disk_ctx->pending_dequeue,
 * there will be only one bfq_dequeue pending on the BFQ_LOCK.
 *
 * lock:
 *	BFQ_LOCK: protect from wf2q_augtree operations in bfq_queue()
 *	THREAD_IO_LOCK: locks the active_tdio. Protect from queue insertions
 *	in bfq_queue; Protect the active_tdio->budget
 *
 * refcount:
 *  If the scheduler decides to suspend, the refcount of active_tdio
 *  increases by 1. The counterpart decreasing is in bfq_queue() and
 *  bfq_timeout()
 * blocking:
 *  May be blocking on the disk driver lock. It depends on drivers.
 *
 * Calling path:
 * The callers could be:
 *	bfq_queue(), bfq_timeout() and the registered polling function.
 *
 *	caller --> helper_msg_dequeue --lwkt_msg--> helper_thread-> me
 *
 */
void
bfq_dequeue(struct dsched_disk_ctx *diskctx)
{
	int free_slots,
	    bio_index = 0, i,
	    remaining_budget = 0;/* remaining budget of current active process */

	struct bio *bio, *bio_to_dispatch[33];
	struct bfq_thread_io *active_tdio = NULL;
	struct bfq_disk_ctx *bfq_diskctx = (struct bfq_disk_ctx *)diskctx;

	BFQ_LOCK(bfq_diskctx);
	atomic_cmpset_int(&bfq_diskctx->pending_dequeue, 1, 0);

	/*
	 * The whole scheduler is waiting for further bios
	 * from process currently being served
	 */
	if (bfq_diskctx->bfq_blockon != NULL)
		goto rtn;

	remaining_budget = bfq_diskctx->bfq_remaining_budget;
	active_tdio = bfq_diskctx->bfq_active_tdio;
	dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: dequeue: Im in. active_tdio = %p\n", active_tdio);

	free_slots = diskctx->max_tag_queue_depth - diskctx->current_tag_queue_depth;
	KKASSERT(free_slots >= 0 && free_slots <= 32);

	if (active_tdio)
		DSCHED_THREAD_IO_LOCK(&active_tdio->head);

	while (free_slots) {
		/* Here active_tdio must be locked ! */
		if (active_tdio) {
			/*
			 * the bio_done function has marked the current
			 * tdio timeout
			 */
			if (active_tdio->maybe_timeout) {
				dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: %p time out in dequeue()\n", active_tdio);
				wf2q_update_vd(active_tdio, active_tdio->budget - remaining_budget);
				bfq_expire(bfq_diskctx, active_tdio, BFQ_REASON_TIMEOUT);

				/* there still exist bios not dispatched,
				 * reinsert the tdio into aug-tree*/
				if (active_tdio->head.qlength > 0) {
					wf2q_insert_thread_io(&bfq_diskctx->bfq_wf2q, active_tdio);
					KKASSERT(bfq_diskctx->bfq_wf2q.wf2q_tdio_count);
				}

				active_tdio->maybe_timeout = 0;
				DSCHED_THREAD_IO_UNLOCK(&active_tdio->head);
				active_tdio = NULL;
				continue;
			}

			/* select next bio to dispatch */
			/* TODO: a wiser slection */
			KKASSERT(lockstatus(&active_tdio->head.lock, curthread) == LK_EXCLUSIVE);
			bio = TAILQ_FIRST(&active_tdio->head.queue);
			dsched_debug(BFQ_DEBUG_NORMAL, "bfq: the first bio in queue of active_tdio %p is %p\n", active_tdio, bio);

			dsched_debug(BFQ_DEBUG_VERBOSE, "bfq: active_tdio %p exists, remaining budget = %d, tdio budget = %d\n, qlength = %d, first bio = %p, first bio cmd = %d, first bio size = %d\n", active_tdio, remaining_budget, active_tdio->budget, active_tdio->head.qlength, bio, bio?bio->bio_buf->b_cmd:-1, bio?bio->bio_buf->b_bcount:-1);

			/*
			 * The bio is not read or write, just
			 * push it down.
			 */
			if (bio && (bio->bio_buf->b_cmd != BUF_CMD_READ) &&
			    (bio->bio_buf->b_cmd != BUF_CMD_WRITE)) {
				dsched_debug(BFQ_DEBUG_NORMAL, "bfq: remove bio %p from the queue of %p\n", bio, active_tdio);
				KKASSERT(lockstatus(&active_tdio->head.lock, curthread) == LK_EXCLUSIVE);
				TAILQ_REMOVE(&active_tdio->head.queue, bio, link);
				active_tdio->head.qlength--;
				free_slots--;

#if 0
				dsched_strategy_request_polling(diskctx->dp, bio, diskctx);
#endif
				bio_to_dispatch[bio_index++] = bio;
				KKASSERT(bio_index <= bfq_diskctx->head.max_tag_queue_depth);
				continue;
			}
			/*
			 * Run out of budget
			 * But this is not because the size of bio is larger
			 * than the complete budget.
			 * If the size of bio is larger than the complete
			 * budget, then use a complete budget to cover it.
			 */
			if (bio && (remaining_budget < BIO_SIZE(bio)) &&
			    (remaining_budget != active_tdio->budget)) {
				/* charge budget used */
				wf2q_update_vd(active_tdio, active_tdio->budget - remaining_budget);
				bfq_expire(bfq_diskctx, active_tdio, BFQ_REASON_OUT_OF_BUDGET);
				wf2q_insert_thread_io(&bfq_diskctx->bfq_wf2q, active_tdio);
				dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: thread %p ran out of budget\n", active_tdio);
				DSCHED_THREAD_IO_UNLOCK(&active_tdio->head);
				active_tdio = NULL;
			} else { /* if (bio && remaining_budget < BIO_SIZE(bio) && remaining_budget != active_tdio->budget) */

				/*
				 * Having enough budget,
				 * or having a complete budget and the size of bio
				 * is larger than that.
				 */
				if (bio) {
					/* dispatch */
					remaining_budget -= BIO_SIZE(bio);
					/*
					 * The size of the first bio is larger
					 * than the whole budget, we should
					 * charge the extra part
					 */
					if (remaining_budget < 0)
						wf2q_update_vd(active_tdio, -remaining_budget);
					/* compensate */
					wf2q_update_vd(active_tdio, -remaining_budget);
					/*
					 * remaining_budget may be < 0,
					 * but to prevent the budget of current tdio
					 * to substract a negative number,
					 * the remaining_budget has to be >= 0
					 */
					remaining_budget = MAX(0, remaining_budget);
					dsched_debug(BFQ_DEBUG_NORMAL, "bfq: remove bio %p from the queue of %p\n", bio, active_tdio);
					KKASSERT(lockstatus(&active_tdio->head.lock, curthread) == LK_EXCLUSIVE);
					TAILQ_REMOVE(&active_tdio->head.queue, bio, link);
					free_slots--;
					active_tdio->head.qlength--;
					active_tdio->bio_dispatched++;
					wf2q_inc_tot_service(&bfq_diskctx->bfq_wf2q, BIO_SIZE(bio));
					dsched_debug(BFQ_DEBUG_VERBOSE,
					    "BFQ: %p's bio dispatched, size=%d, remaining_budget = %d\n",
					    active_tdio, BIO_SIZE(bio), remaining_budget);
#if 0
					dsched_strategy_request_polling(diskctx->dp, bio, diskctx);
#endif
					bio_to_dispatch[bio_index++] = bio;
					KKASSERT(bio_index <= bfq_diskctx->head.max_tag_queue_depth);

				} else { /* if (bio) */

					KKASSERT(active_tdio);
					/*
					 * If AS feature is switched off,
					 * expire the tdio as well
					 */
					if ((remaining_budget <= 0) ||
					    !(bfq_diskctx->bfq_flag & BFQ_FLAG_AS) ||
					    !active_tdio->tdio_as_switch) {
						active_tdio->budget -= remaining_budget;
						wf2q_update_vd(active_tdio, active_tdio->budget);
						bfq_expire(bfq_diskctx, active_tdio, BFQ_REASON_OUT_OF_BUDGET);
						DSCHED_THREAD_IO_UNLOCK(&active_tdio->head);
						active_tdio = NULL;
					} else {

						/* no further bio, wait for a while */
						bfq_diskctx->bfq_blockon = active_tdio;
						/*
						 * Increase ref count to ensure that
						 * tdio will not be destroyed during waiting.
						 */
						dsched_thread_io_ref(&active_tdio->head);
						/*
						 * If the tdio is seeky but not thingking for
						 * too long, we wait for it a little shorter
						 */
						if (active_tdio->seek_samples >= BFQ_VALID_MIN_SAMPLES && BFQ_TDIO_SEEKY(active_tdio))
							callout_reset(&bfq_diskctx->bfq_callout, BFQ_T_WAIT_MIN, (void (*) (void *))helper_msg_as_timeout, bfq_diskctx);
						else
							callout_reset(&bfq_diskctx->bfq_callout, BFQ_T_WAIT, (void (*) (void *))helper_msg_as_timeout, bfq_diskctx);

						/* save the start time of blocking */
						getmicrotime(&active_tdio->as_start_time);

						dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: blocked on %p, remaining_budget = %d\n", active_tdio, remaining_budget);
						DSCHED_THREAD_IO_UNLOCK(&active_tdio->head);
						goto save_and_rtn;
					}
				}
			}
		} else { /* if (active_tdio) */
			/* there is no active tdio */

			/* no pending bios at all */
			active_tdio = wf2q_get_next_thread_io(&bfq_diskctx->bfq_wf2q);

			if (!active_tdio) {
				KKASSERT(bfq_diskctx->bfq_wf2q.wf2q_tdio_count == 0);
				dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: no more eligible tdio!\n");
				goto save_and_rtn;
			}

			/*
			 * A new tdio is picked,
			 * initialize the service related statistic data
			 */
			DSCHED_THREAD_IO_LOCK(&active_tdio->head);
			active_tdio->service_received = 0;

			/*
			 * Reset the maybe_timeout flag, which
			 * may be set by a biodone after the the service is done
			 */
			getmicrotime(&active_tdio->service_start_time);
			active_tdio->maybe_timeout = 0;

			remaining_budget = active_tdio->budget;
			dsched_debug(BFQ_DEBUG_VERBOSE, "bfq: active_tdio %p selected, remaining budget = %d, tdio budget = %d\n, qlength = %d\n", active_tdio, remaining_budget, active_tdio->budget, active_tdio->head.qlength);
		}

	}/* while (free_slots) */

	/* reach here only when free_slots == 0 */
	if (active_tdio) /* && lockcount(&active_tdio->head.lock) > 0) */
		DSCHED_THREAD_IO_UNLOCK(&active_tdio->head);

save_and_rtn:
	/* save the remaining budget */
	bfq_diskctx->bfq_remaining_budget = remaining_budget;
	bfq_diskctx->bfq_active_tdio = active_tdio;
rtn:
	BFQ_UNLOCK(bfq_diskctx);
	/*dispatch the planned bios*/
	for (i = 0; i < bio_index; i++)
		dsched_strategy_request_polling(diskctx->dp, bio_to_dispatch[i], diskctx);

}

/*
 * bfq_slow_tdio(): decide whether a tdio is slow
 *
 * This function decides whether a tdio is slow by the speed
 * estimated from the current time slice start time: if the
 * tdio is not fast enough to consume its budget (or 2/3
 * its budget) within the time slice, it is judged slow.
 *
 * Called by bfq_expire()
 *
 * lock:
 *  THREAD_IO_LOCK is expected to be held.
 * refcount:
 *	none
 *
 */
static int
bfq_slow_tdio(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio)
{
	/**
	 * A tdio is considered slow if it can not finish its budget
	 * at its current average speed
	 */
	uint64_t usec_elapsed, service_received, speed;
	int expect;
	struct timeval tv = bfq_tdio->last_request_done_time;

	timevalsub (&tv, &bfq_tdio->service_start_time);
	usec_elapsed = (uint64_t)(1000000 * (uint64_t)tv.tv_sec + tv.tv_usec);

	/* discard absurd value */
	if (usec_elapsed < 20000)
		return 0;

	service_received = (uint64_t)bfq_tdio->service_received << BFQ_FIXPOINT_SHIFT;
	speed = service_received / usec_elapsed;
	expect = (speed * BFQ_SLICE_TIMEOUT * (1000 * 1000 / hz)) >> BFQ_FIXPOINT_SHIFT;

	if (expect < 0) {
		dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: overflow on calculating slow_tdio\n");
		return 0;
	}

	if (expect < bfq_tdio->budget * 2 / 3) {
		dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: %p is judged slow\n", bfq_tdio);
		return 1;
	}

	return 0;
}

/*
 * bfq_expire(): expire a tdio for a given reason.
 *
 * Different amount of the new budget will be assign to the expired
 * tdio according to the following reasons:
 *
 * BFQ_REASON_TIMEOUT:
 *  The tdio does not consume its budget up within BFQ_SLICE_TIMEOUT ticks.
 *  We shall update the disk peak rate if the tdio is not seeky. The new
 *  budget will be the budget it actually consumes during this time
 *  slice.
 *
 * BFQ_REASON_TOO_IDLE:
 *  The tdio does not push any further bios during the scheduler is
 *  suspending. To ensure low global latency, this tdio should be
 *  punished by assign it the minimum budget. But if the tdio's not
 *  pushing any bio is because it is waiting for the dispatched bios
 *  to be done, we just keep the budget unchanged.
 *
 * BFQ_REASON_OUT_OF_BUDGET:
 *	The tdio runs out of its budget within the time slice. It usually
 *	indicates that the tdio is doing well. We increase the budget of it.
 *
 * lock:
 *  THREAD_IO_LOCK is expected to be held.
 *  BFQ_LOCK is expected to be held (needed by bfq_update_peak_rate()).
 *
 * refcount: none
 *
 * Callers: bfq_timeout(), bfq_dequeue()
 *
 */
static void
bfq_expire(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio, enum bfq_expire_reason reason)
{
	int max_budget = bfq_diskctx->bfq_max_budget,
		budget_left,
		bio_in_flight,
		service_received;

	service_received = bfq_tdio->service_received;
	budget_left = bfq_tdio->budget - bfq_tdio->service_received;

	if (budget_left < 0) {
		dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: budget down flow: %d, %d\n", bfq_tdio->budget, bfq_tdio->service_received);
		budget_left = 0;
	}

	KKASSERT(budget_left >= 0);

	switch (reason) {
		case BFQ_REASON_TIMEOUT:
			/* the tdio is not seeky so that we can update
			 * the disk peak rate based on the service received
			 * by the tdio
			 */
			if ((bfq_tdio->seek_samples >= BFQ_VALID_MIN_SAMPLES) &&
			    (!BFQ_TDIO_SEEKY(bfq_tdio)))
				bfq_update_peak_rate(bfq_diskctx, bfq_tdio);

			/* max_budget may be updated */
			max_budget = bfq_diskctx->bfq_max_budget;

			/* update budget to service_received*/
			bfq_tdio->budget = MAX(service_received, BFQ_DEFAULT_MIN_BUDGET);

			break;

		case BFQ_REASON_TOO_IDLE:
			/*
			 * the tdio is too slow, charge full budget
			 */
			if (bfq_slow_tdio(bfq_diskctx, bfq_tdio))
				wf2q_update_vd(bfq_tdio, budget_left);

			bio_in_flight = bfq_tdio->bio_dispatched - bfq_tdio->bio_completed;
			KKASSERT(bio_in_flight >= 0);
			/*
			 * maybe the tdio pushes no bio
			 * because it is waiting for some bios
			 * dispatched to be done, in this case
			 * we do not reduce the budget too harshly
			 */
			if (bio_in_flight > 0) {
				bfq_tdio->budget = MAX(BFQ_DEFAULT_MIN_BUDGET, service_received);
			} else {
#if 0
				bfq_tdio->budget = MAX(BFQ_DEFAULT_MIN_BUDGET, bfq_diskctx->bfq_max_budget / BFQ_MIN_BUDGET_FACTOR);
#endif
				bfq_tdio->budget = BFQ_DEFAULT_MIN_BUDGET;
			}

			break;
		case BFQ_REASON_OUT_OF_BUDGET:

			if ((bfq_tdio->seek_samples >= BFQ_VALID_MIN_SAMPLES) &&
			    (!BFQ_TDIO_SEEKY(bfq_tdio)))
				bfq_update_peak_rate(bfq_diskctx, bfq_tdio);

			/* increase the budget */
			if (bfq_tdio->budget < BFQ_BUDGET_MULTIPLE_THRESHOLD)
				bfq_tdio->budget = MIN(max_budget, bfq_tdio->budget * 2);
			else
				bfq_tdio->budget = MIN(max_budget, bfq_tdio->budget + BFQ_BUDG_INC_STEP);
			break;
		default:
			break;
	}
}

/*
 * bfq_update_peak_rate(): update the peak disk speed by sampling
 * the throughput within a time slice.
 *
 * lock:
 *  BFQ_LOCK is expected to be held
 *
 * refcount:
 *	none
 *
 * Caller: bfq_expire()
 */
static void
bfq_update_peak_rate(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio)
{
	struct timeval tv = bfq_tdio->last_request_done_time;
	uint64_t usec, service_received, peak_rate;


	timevalsub (&tv, &bfq_tdio->service_start_time);
	usec = (uint64_t)(1000000 * (uint64_t)tv.tv_sec + tv.tv_usec);

	/* discard absurd value */
	if (usec < 2000 || usec > (BFQ_SLICE_TIMEOUT * (1000 / hz) * 1000)) {
		dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: absurd interval for peak rate\n");
		return;
	}

	service_received = (uint64_t)bfq_tdio->service_received << BFQ_FIXPOINT_SHIFT;
	peak_rate = service_received / usec;
	bfq_diskctx->bfq_peak_rate = (peak_rate + 7 * bfq_diskctx->bfq_peak_rate) / 8;
	bfq_diskctx->bfq_peak_rate_samples++;

	/* update the max_budget according to the peak rate */
	if (bfq_diskctx->bfq_peak_rate_samples > BFQ_VALID_MIN_SAMPLES) {
		bfq_diskctx->bfq_peak_rate_samples = BFQ_VALID_MIN_SAMPLES;
		/*
		 * if the auto max budget adjust is disabled,
		 * the bfq_max_budget will always be BFQ_DEFAULT_MAX_BUDGET;
		 */
		if (bfq_diskctx->bfq_flag & BFQ_FLAG_AUTO_MAX_BUDGET) {
			bfq_diskctx->bfq_max_budget =
				(uint32_t)((BFQ_SLICE_TIMEOUT * (1000 / hz) * bfq_diskctx->bfq_peak_rate * 1000) >> BFQ_FIXPOINT_SHIFT);
			dsched_debug(BFQ_DEBUG_NORMAL, "max budget updated to %d\n", bfq_diskctx->bfq_max_budget);
		}
	}
}

/*
 * bfq_update_tdio_seek_avg(): update the average seek distance of a
 * tdio.
 *
 * lock:
 *	THREAD_IO_LOCK is expected to be held.
 *
 * refcount:
 *  none
 *
 * Caller: bfq_queue()
 */
static void
bfq_update_tdio_seek_avg(struct bfq_thread_io *bfq_tdio, struct bio *bp)
{
	off_t seek;

	/* the first bio it dispatches,
	 * we do not calculate the seek_avg,
	 * just update the last_seek_end
	 */
	if (bfq_tdio->seek_samples == 0) {
		++bfq_tdio->seek_samples;
		goto rtn;
	}

	seek = ABS(bp->bio_offset - bfq_tdio->last_seek_end);

	/*
	 * we do not do seek_samples++,
	 * because the seek_total may overflow if seek_total += seek,
	 */
	bfq_tdio->seek_samples = (7 * bfq_tdio->seek_samples + 256) / 8;
	bfq_tdio->seek_total = (7 * bfq_tdio->seek_total + 256 * seek) / 8;
	bfq_tdio->seek_avg = (bfq_tdio->seek_total + bfq_tdio->seek_samples / 2) / bfq_tdio->seek_samples;

	dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: tdio %p seek_avg updated to %" PRIu64 "\n", bfq_tdio, bfq_tdio->seek_avg);

rtn:
	bfq_tdio->last_seek_end = bp->bio_offset + BIO_SIZE(bp);
}

/*
 * bfq_update_tdio_ttime_avg(): update the average thinking time
 * of a tdio.
 *
 * The thinking time is used to switch on / off the tdio's AS feature
 *
 * lock:
 *  THREAD_IO_LOCK is expected to be held.
 *
 * refcount:
 *  none
 *
 * Caller:
 *  bfq_queue()
 *
 */
static void
bfq_update_tdio_ttime_avg(struct bfq_thread_io *bfq_tdio)
{
	struct timeval tv, after_start;
	uint64_t usec;

	if (bfq_tdio->ttime_samples == 0) {
		++bfq_tdio->ttime_samples;
		return;
	}

	getmicrotime(&tv);
	after_start = bfq_tdio->last_request_done_time;

#if 0
	timevalsub (&tv, &bfq_tdio->last_request_done_time);
#endif
	/*
	 * Try the interval between two bios are pushed,
	 * instead of between last_request_done_time and
	 * the current time.
	 */

	timevalsub (&tv, &bfq_tdio->last_bio_pushed_time);

	timevalsub (&after_start, &bfq_tdio->service_start_time);

	/*
	 * tv.tv_sec < 0 means the last reauest done time is
	 * after the current time.
	 * this may happen because the biodone function is not blocked
	 *
	 * after_start.tv_sec < 0 means that the last bio done happens
	 * before the current service slice, and we should drop this value.
	 */
	if (tv.tv_sec < 0 || after_start.tv_sec < 0)
		return;

	usec = (uint64_t)(1000000 * (uint64_t)tv.tv_sec + tv.tv_usec);

	bfq_tdio->ttime_samples = (7 * bfq_tdio->ttime_samples + 256) / 8;
	bfq_tdio->ttime_total = (7 * bfq_tdio->ttime_total + 256 * usec) / 8;
	bfq_tdio->ttime_avg = (bfq_tdio->ttime_total + 128) / bfq_tdio->ttime_samples;

}

/*
 * This function will also update the bfq_max_time_slice field
 *
 * tv: the timeval structure representing the length of time slice
 */
static void
bfq_update_avg_time_slice(struct bfq_disk_ctx *bfq_diskctx, struct timeval tv)
{
	uint32_t msec;

	msec = ((uint64_t)(1000000 * (uint64_t)tv.tv_sec + tv.tv_usec) >> 10 );

	if (msec > 3 * BFQ_SLICE_TIMEOUT * (1000 / hz))
		atomic_add_int(&bfq_diskctx->bfq_high_time_slice_count, 1);

	bfq_diskctx->bfq_avg_time_slice =
		(7 * bfq_diskctx->bfq_avg_time_slice + msec) / 8;

	if (bfq_diskctx->bfq_max_time_slice < msec)
		bfq_diskctx->bfq_max_time_slice = msec;
}
/*
 * This function will also update the bfq_as_max_wait field
 * flag: BFQ_AS_STAT_ALL, BFQ_AS_STAT_ONLY_MISS
 *
 */
static void
bfq_update_as_avg_wait(struct bfq_disk_ctx *bfq_diskctx, struct bfq_thread_io *bfq_tdio, int flag)
{
	struct timeval tv;
	uint32_t msec;
	getmicrotime(&tv);
	timevalsub (&tv, &bfq_tdio->as_start_time);

	/* approximately divide 1000 by left shift 10 */
	msec = ((uint64_t)(1000000 * (uint64_t)tv.tv_sec + tv.tv_usec) >> 10 );

	/* ridiculous value */
	if (msec > 10000) {
		dsched_debug(BFQ_DEBUG_NORMAL, "bfq: ridiculous as wait time!\n");
		return;
	}

	if (msec > 5 * BFQ_T_WAIT_MIN * (1000 / hz))
		atomic_add_int(&bfq_diskctx->bfq_as_high_wait_count, 1);

	if (flag & BFQ_AS_STAT_ALL) {
		bfq_diskctx->bfq_as_avg_wait_all =
			(7 * bfq_diskctx->bfq_as_avg_wait_all + msec) / 8;
	}

	if (flag & BFQ_AS_STAT_ONLY_MISS) {
		bfq_diskctx->bfq_as_avg_wait_miss =
			(7 * bfq_diskctx->bfq_as_avg_wait_miss + msec) / 8;
	}

	/* update the maximum waiting time */
	if (bfq_diskctx->bfq_as_max_wait < msec)
		bfq_diskctx->bfq_as_max_wait = msec;

	return;
}

static int
bfq_mod_handler(module_t mod, int type, void *unused)
{
	static struct sysctl_ctx_list sysctl_ctx;
	static struct sysctl_oid *oid;
	static char version[16];
	int error;

	ksnprintf(version, sizeof(version), "%d.%d",
			dsched_bfq_version_maj, dsched_bfq_version_min);

	switch (type) {
	case MOD_LOAD:
		bzero(&bfq_stats, sizeof(struct dsched_bfq_stats));
		if ((error = dsched_register(&dsched_bfq_policy)))
			return (error);

		sysctl_ctx_init(&sysctl_ctx);
		oid = SYSCTL_ADD_NODE(&sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_dsched),
		    OID_AUTO,
		    "bfq",
		    CTLFLAG_RD, 0, "");
		bfq_mod_oid = oid;

		SYSCTL_ADD_STRING(&sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "version", CTLFLAG_RD, version, 0, "bfq version");
		helper_init_global();

		kprintf("BFQ scheduler policy version %d.%d loaded. sizeof(bfq_thread_io) = %zu\n",
		    dsched_bfq_version_maj, dsched_bfq_version_min, sizeof(struct bfq_thread_io));
		break;

	case MOD_UNLOAD:
		if ((error = dsched_unregister(&dsched_bfq_policy)))
			return (error);
		sysctl_ctx_free(&sysctl_ctx);
		kprintf("BFQ scheduler policy unloaded\n");
		break;

	default:
		break;
	}

	return 0;
}

int
bfq_sysctl_as_switch_handler(SYSCTL_HANDLER_ARGS)
{
	struct bfq_disk_ctx *bfq_diskctx = arg1;
	int as_switch, error;

	as_switch = ((bfq_diskctx->bfq_flag & BFQ_FLAG_AS) ? 1 : 0);
	error = sysctl_handle_int(oidp, &as_switch, 0, req);
	if (error || !req->newptr)
		return error;

	if (as_switch == 1)
		bfq_diskctx->bfq_flag |= BFQ_FLAG_AS;
	else if (as_switch == 0)
		bfq_diskctx->bfq_flag &= ~(BFQ_FLAG_AS);
	else
		return 0;

	return error;
}

int
bfq_sysctl_auto_max_budget_handler(SYSCTL_HANDLER_ARGS)
{
	struct bfq_disk_ctx *bfq_diskctx = arg1;
	int auto_max_budget_switch, error;
	auto_max_budget_switch = ((bfq_diskctx->bfq_flag & BFQ_FLAG_AUTO_MAX_BUDGET) ? 1 : 0);
	error = sysctl_handle_int(oidp, &auto_max_budget_switch, 0, req);
	if (error || !req->newptr)
		return error;

	if (auto_max_budget_switch == 1)
		bfq_diskctx->bfq_flag |= BFQ_FLAG_AUTO_MAX_BUDGET;
	else if (auto_max_budget_switch == 0)
		bfq_diskctx->bfq_flag &= ~(BFQ_FLAG_AUTO_MAX_BUDGET);
	else
		return 0;

	return error;
}

DSCHED_POLICY_MODULE(dsched_bfq, bfq_mod_handler);
