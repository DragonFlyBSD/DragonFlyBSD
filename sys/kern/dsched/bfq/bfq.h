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


#ifndef _DSCHED_BFQ_H_
#define _DSCHED_BFQ_H_

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

#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif

#ifndef _SYS_DSCHED_H_
#include <sys/dsched.h>
#endif

#ifndef _DSCHED_BFQ_WF2Q_H_
#include <kern/dsched/bfq/wf2q.h>
#endif

struct wf2q_t;

struct bfq_thread_io {
	struct dsched_thread_io head;
	RB_ENTRY(bfq_thread_io) entry;
	int budget;	/* The budget of a thread */
	int vd;		/* Virtual deadline (finish time) */
	int ve;		/* Virtual eligible time (start time) */
	int min_vd;	/* Minimum vd among the sub trees, used for augmented rb-tree */
	int weight;	/* Weight of the thread, the higher, the more
			   chance to be dispatched the thread will have */

	volatile int maybe_timeout;	/* a flag indicating that the tdio may
					  expire, only when active_tdio = this is it valid */
	int tdio_as_switch;

	/* Statistic data */
	off_t	last_seek_end;  /* the end point of seeking of the last bio
							   pushed down */
	uint32_t seek_samples;	/* averange seek length samples */
	off_t	seek_avg;	/* averange seek length, fixed point */
	off_t	seek_total;

	uint32_t ttime_samples;	/* averange think time samples */
	uint64_t ttime_avg;	/* averange think time, usec */
	uint64_t ttime_total;

	struct timeval service_start_time; /* the time when the first request
						  of the current service period is dispatched */
	struct timeval last_request_done_time;	/* the time when the last
						   request is done */
	struct timeval as_start_time;	/* the start time of AS waiting */
	struct timeval last_bio_pushed_time;

	uint32_t service_received;	/* the amount of read/write during
				           the time slice */
	uint32_t bio_dispatched;	/* number of bios dispatched during
					   the current period */
	uint32_t bio_completed;		/* number of bios completed during
					   the current period */
};

struct bfq_disk_ctx {
	struct dsched_disk_ctx head;

	struct lock bfq_lock;

	struct callout bfq_callout;	/* the blocking-timer callout */
	struct wf2q_t bfq_wf2q;		/* the wf2q scheduler */

	struct bfq_thread_io *bfq_blockon;	/* waiting on any */
	struct bfq_thread_io *bfq_active_tdio;	/* currently active tdio */

	int pending_dequeue; /* number of dequeue() calls pending
				on BFQ_LOCK */

	int bfq_max_budget;
	int bfq_remaining_budget; /* remaining budget of the current tdio */

	uint32_t bfq_flag; /* SEE BFQ_FLAG_* define for all flags */

	/* Statistic data */
	uint32_t bfq_peak_rate_samples; /* peak rate samples */
	uint64_t bfq_peak_rate;		/* peak rate, fixed point */

	int bfq_as_miss;
	int bfq_as_hit;

	uint32_t bfq_as_avg_wait_miss;	/* average AS waiting time for
					   only AS miss, ms */
	uint32_t bfq_as_avg_wait_all;	/* average AS waiting time for all, ms */
	uint32_t bfq_as_max_wait;	/* maximum AS waiting time, ms */
	uint32_t bfq_as_max_wait2;	/* maximum AS waiting time(from callout), ms */

	int bfq_as_high_wait_count; /* the number of times when AS waiting time
				       is longer than 5 * BFQ_T_WAIT_MIN (50ms now) */
	int bfq_as_high_wait_count2; /* the number of times when AS waiting
					time is longer than 5 * BFQ_T_WAIT_MIN (50ms now) */

	uint32_t bfq_avg_time_slice;	/* average time slice length, ms */
	uint32_t bfq_max_time_slice;	/* maximum time slice length, ms */
	int bfq_high_time_slice_count;	/* the number of times when a time slice
					    is longer than 5 * BFQ_SLICE_TIMEOUT */

	struct sysctl_ctx_list bfq_sysctl_ctx; /* bfq statistics interface
						  with sysctl */
	/* helper thread and its lwkt message cache and port*/
	struct thread *helper_thread;
	struct objcache *helper_msg_cache;
	struct lwkt_port helper_msg_port;
};

enum bfq_expire_reason {
	BFQ_REASON_TIMEOUT = 0,
	BFQ_REASON_TOO_IDLE,
	BFQ_REASON_OUT_OF_BUDGET,
	BFQ_REASON_NO_MORE_REQ
};

#define BFQ_FLAG_AS 0x01
#define BFQ_FLAG_AUTO_MAX_BUDGET 0x02

#define BFQ_TDIO_SEEKY(x) (((x)->seek_avg) > (1024 * SECT_SIZE))

#define BFQ_LOCKINIT(x)			\
		lockinit(&(x)->bfq_lock, "bfqwf2q", 0, LK_CANRECURSE);

#define BFQ_LOCK(x)	do {		\
		dsched_disk_ctx_ref(&(x)->head);	\
		lockmgr(&(x)->bfq_lock, LK_EXCLUSIVE);	\
	} while(0)

#define BFQ_UNLOCK(x)	do {		\
		lockmgr(&(x)->bfq_lock, LK_RELEASE);	\
		dsched_disk_ctx_unref(&(x)->head);	\
	} while(0)

#define SECT_SIZE 512 /* XXX: DEV_BSIZE? */
#define BFQ_DEBUG_CRITICAL 1
#define BFQ_DEBUG_NORMAL 2
#define BFQ_DEBUG_VERBOSE 3
#define BFQ_DEFAULT_MAX_BUDGET (1024*512) /* 1024 sectors / 0.2sec */
#define BFQ_DEFAULT_MIN_BUDGET (32*512) /* 32 sectors / 0.2sec */
#define BFQ_BUDG_INC_STEP (1*128*512) /* The linear increasing step of budget */

/* If the budget is larger than this threshold,
 * it will get linear increment, else,
 * it will get exponential increment.*/
#define BFQ_BUDGET_MULTIPLE_THRESHOLD (256*512)

#define BFQ_DEFAULT_WEIGHT 1

/* Get the size of a bio */
#define BIO_SIZE(x) ((x)->bio_buf->b_bcount)

/* Anticipatory waiting time (ticks) ~ 20ms, min ~ 10ms */
#define BFQ_T_WAIT ((hz/50) > 5 ? (hz/50) : 5)

#define BFQ_T_WAIT_MIN ((hz/100 > 0) ? (hz/100) : 1)

/* Time slice for each service period ~200ms (ticks) */
#define BFQ_SLICE_TIMEOUT (hz/5)

#define BFQ_FIXPOINT_SHIFT 10 /* fixed point arithmetic shift */

#define BFQ_VALID_MIN_SAMPLES 80 /* minimum number of samples */

#define ABS(x) (((x) < 0) ? (-(x)) : (x))

/* as statistics define */
#define BFQ_AS_STAT_ALL 0x1
#define BFQ_AS_STAT_ONLY_MISS 0x2

/* functions helper thread calls */
void bfq_timeout(void *);
void bfq_dequeue(struct dsched_disk_ctx *);
void bfq_helper_destroy_tdio(struct dsched_thread_io *, struct bfq_disk_ctx *);

/* sysctl handlers, registered in the helper thread */
int bfq_sysctl_as_switch_handler(SYSCTL_HANDLER_ARGS);
int bfq_sysctl_auto_max_budget_handler(SYSCTL_HANDLER_ARGS);

#endif /* _KERNEL || _KERNEL_STRUCTURES */
struct dsched_bfq_stats {
	int32_t as_missed;
	int32_t as_hit;
	int32_t as_fake;
	int32_t unused;
};
#endif /*_DSCHED_BFQ_H_ */
