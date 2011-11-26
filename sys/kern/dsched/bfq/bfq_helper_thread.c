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
 * bfq_helper_thread.c:
 * Thread function of the helper thread and
 * message sending routines.
 *
 * XXX: The current approach of serializing using lwkt messages is suboptimal.
 *	The idea is to replace it with way more fine-grained and lockless
 *	accesses spread all over the place. It makes things more complicated,
 *	but it will also improve performance significantly.
 *
 * The sysctl node of bfq is also initialized
 * here.
 */

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
#include <sys/mplock2.h>
#include <sys/buf2.h>
#include <sys/dsched.h>
#include <sys/fcntl.h>
#include <machine/varargs.h>

#include <kern/dsched/bfq/bfq.h>
#include <kern/dsched/bfq/bfq_helper_thread.h>

extern struct sysctl_oid *bfq_mod_oid;
extern struct dsched_policy dsched_bfq_policy;

static void helper_thread(struct bfq_disk_ctx *bfq_diskctx);
static int helper_msg_exec(helper_msg_t msg);
static void helper_sysctl_init(struct bfq_disk_ctx *bfq_diskctx);

MALLOC_DEFINE(M_HELPER, "bfq", "BFQ helper thread message allocations");

/*
 * All threads share one dispose port
 */
static struct lwkt_port helper_dispose_port;

/* XXX: should be an mpipe */
static struct objcache_malloc_args helper_msg_malloc_args = {
	sizeof(struct helper_msg), M_HELPER };


static helper_msg_t
helper_msg_get(struct bfq_disk_ctx *bfq_diskctx)
{
	/*
	 * XXX: wait is OK?
	 */
	return objcache_get(bfq_diskctx->helper_msg_cache, M_WAITOK);
}

static int
helper_msg_put(struct bfq_disk_ctx *bfq_diskctx, helper_msg_t msg)
{
	objcache_put(bfq_diskctx->helper_msg_cache, msg);
	return 0;
}

static void
helper_msg_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
	helper_msg_t hm = (helper_msg_t)msg;
	helper_msg_put(hm->bfq_diskctx, (helper_msg_t)msg);
}

/*
 * Initialize the dispose port. All helper threads share this port.
 * Must be called only once, and before any helper thread being created.
 *
 * Called by bfq.c: bfq_moc_handler()
 */
void
helper_init_global(void)
{
	lwkt_initport_replyonly(&helper_dispose_port, helper_msg_autofree_reply);
}

/*
 * Helper thread initialization function:
 * initialize the per-disk objcache and create the
 * helper thread.
 *
 * Called by bfq.c:bfq_prepare()
 */
void
helper_init(struct bfq_disk_ctx *bfq_diskctx)
{
	struct thread *phelper_thread;

	bfq_diskctx->helper_msg_cache = objcache_create("bfq-helper-msg-cache", 0, 0,
			NULL, NULL, NULL,
			objcache_malloc_alloc,
			objcache_malloc_free,
			&helper_msg_malloc_args);

	lwkt_create((void (*) (void *)) helper_thread, bfq_diskctx,
			&phelper_thread, NULL, 0, -1,
			"bfq_helper_td_%s", bfq_diskctx->head.dp->d_cdev->si_name);

	bfq_diskctx->helper_thread = phelper_thread;
}

static void
helper_msg_send(struct bfq_disk_ctx *bfq_diskctx, uint32_t cmd, helper_msg_t helper_msg)
{
	lwkt_port_t port = &bfq_diskctx->helper_msg_port;

	lwkt_initmsg(&helper_msg->hdr, &helper_dispose_port, 0);
	helper_msg->bfq_diskctx = bfq_diskctx;
	helper_msg->hdr.u.ms_result = cmd;

	if (port->mpu_td == curthread){
		helper_msg_exec(helper_msg);
		lwkt_replymsg(&helper_msg->hdr, 0);
	} else {
		lwkt_sendmsg(port, (lwkt_msg_t)helper_msg);
	}
}

/*
 * Deallocate the objcache.
 * Called by bfq.c: bfq_teardown()
 */
void
helper_uninit(struct bfq_disk_ctx *bfq_diskctx)
{
	objcache_destroy(bfq_diskctx->helper_msg_cache);
}

static void
helper_sysctl_init(struct bfq_disk_ctx *bfq_diskctx)
{
	struct sysctl_oid *oid;

	sysctl_ctx_init(&bfq_diskctx->bfq_sysctl_ctx);

	if (!bfq_mod_oid){
		kprintf("Failed to create BFQ dev sysctl node!\n");
		return;
	}

	oid = SYSCTL_ADD_NODE(&bfq_diskctx->bfq_sysctl_ctx,
		SYSCTL_CHILDREN(bfq_mod_oid),
		OID_AUTO,
		bfq_diskctx->head.dp->d_cdev->si_name,
		CTLFLAG_RD, 0, "");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"max_budget",
			CTLFLAG_RW,
			&bfq_diskctx->bfq_max_budget,
			0,
			"BFQ max budget");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"peak_rate",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_peak_rate,
			0,
			"BFQ estimated peak rate");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"peak_samples",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_peak_rate_samples,
			0,
			"BFQ estimated peak rate samples");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_miss",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_miss,
			0,
			"BFQ AS miss");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_hit",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_hit,
			0,
			"BFQ AS hit");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_wait_avg_all",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_avg_wait_all,
			0,
			"BFQ AS waitall");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_wait_avg_miss",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_avg_wait_miss,
			0,
			"BFQ AS waitmiss");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_wait_max",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_max_wait,
			0,
			"BFQ AS waitmax");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_wait_max2",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_max_wait2,
			0,
			"BFQ AS waitmax2");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_high_wait_count",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_high_wait_count,
			0,
			"BFQ AS high count");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"as_high_wait_count2",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_as_high_wait_count2,
			0,
			"BFQ AS high count2");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"avg_time_slice",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_avg_time_slice,
			0,
			"BFQ average time slice");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"max_time_slice",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_max_time_slice,
			0,
			"BFQ max time slice");

	SYSCTL_ADD_INT(&bfq_diskctx->bfq_sysctl_ctx,
			SYSCTL_CHILDREN(oid),
			OID_AUTO,
			"high_time_slice_count",
			CTLFLAG_RD,
			&bfq_diskctx->bfq_high_time_slice_count,
			0,
			"BFQ high time slice count");

	SYSCTL_ADD_PROC(&bfq_diskctx->bfq_sysctl_ctx, SYSCTL_CHILDREN(oid),
			OID_AUTO, "as_switch", CTLTYPE_INT|CTLFLAG_RW,
			bfq_diskctx, 0, bfq_sysctl_as_switch_handler, "I", "as_switch");

	SYSCTL_ADD_PROC(&bfq_diskctx->bfq_sysctl_ctx, SYSCTL_CHILDREN(oid),
			OID_AUTO, "auto_max_budget_switch", CTLTYPE_INT|CTLFLAG_RW,
			bfq_diskctx, 0, bfq_sysctl_auto_max_budget_handler, "I", "amb_switch");
}

static void
helper_thread(struct bfq_disk_ctx *bfq_diskctx)
{
	struct dsched_thread_io *tdio;

	int r;
	helper_msg_t msg;

	tdio = dsched_new_policy_thread_tdio(&bfq_diskctx->head, &dsched_bfq_policy);

	lwkt_initport_thread(&bfq_diskctx->helper_msg_port, curthread);
	dsched_disk_ctx_ref(&bfq_diskctx->head);
	helper_sysctl_init(bfq_diskctx);

	dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: helper thread created\n");
#if 0
	/* XXX: why mplock?! */
	get_mplock();
#endif

	for(;;) {
		msg = (helper_msg_t)lwkt_waitport(&bfq_diskctx->helper_msg_port, 0);
		dsched_debug(BFQ_DEBUG_VERBOSE, "BFQ: helper: msg recv: %d\n", msg->hdr.u.ms_result);
		r = helper_msg_exec(msg);
		lwkt_replymsg(&msg->hdr, 0);
		/*
		 * received BFQ_MSG_KILL
		 */
		if (r == -1)
			break;
	}

#if 0
	rel_mplock();
#endif

	sysctl_ctx_free(&bfq_diskctx->bfq_sysctl_ctx);
	dsched_disk_ctx_unref(&bfq_diskctx->head);
	dsched_debug(BFQ_DEBUG_NORMAL, "BFQ: helper: die peacefully\n");
	lwkt_exit();
}

static int
helper_msg_exec(helper_msg_t msg)
{
	struct bfq_disk_ctx *bfq_diskctx;

	bfq_diskctx = msg->bfq_diskctx;


	switch (msg->hdr.u.ms_result)
	{
		case BFQ_MSG_DEQUEUE:
			if (atomic_cmpset_int(&bfq_diskctx->pending_dequeue, 0, 1))
				bfq_dequeue((struct dsched_disk_ctx *)bfq_diskctx);
			break;
		case BFQ_MSG_AS_TIMEOUT:
			bfq_timeout(bfq_diskctx);
			break;

		case BFQ_MSG_DESTROY_TDIO:
			bfq_helper_destroy_tdio(msg->tdio, bfq_diskctx);
			break;

		case BFQ_MSG_KILL:
			return -1;

		default:
			break;
	}
	return 0;
}

void
helper_msg_dequeue(struct bfq_disk_ctx *bfq_diskctx)
{
	helper_msg_t helper_msg = helper_msg_get(bfq_diskctx);

	helper_msg_send(bfq_diskctx, BFQ_MSG_DEQUEUE, helper_msg);
}

void
helper_msg_as_timeout(struct bfq_disk_ctx *bfq_diskctx)
{
	helper_msg_t helper_msg = helper_msg_get(bfq_diskctx);
	/**
	 * For statisticsal use, temporary
	 * ------------------------------
	 */
	struct bfq_thread_io *bfq_tdio;
	struct timeval tv;
	uint32_t msec;


	bfq_tdio = bfq_diskctx->bfq_blockon;
	if (bfq_tdio) {
		getmicrotime(&tv);
		timevalsub(&tv, &bfq_tdio->as_start_time);
		msec = ((uint64_t)(1000000*tv.tv_sec + tv.tv_usec)) >> 10;
		if (msec > 5 * BFQ_T_WAIT_MIN * (1000 / hz))
			atomic_add_int(&bfq_diskctx->bfq_as_high_wait_count2, 1);
		if (msec > bfq_diskctx->bfq_as_max_wait2)
			bfq_diskctx->bfq_as_max_wait2 = msec;
	}
	/* ----------------------------- */

	helper_msg_send(bfq_diskctx, BFQ_MSG_AS_TIMEOUT, helper_msg);
}

void
helper_msg_destroy_tdio(struct bfq_disk_ctx *bfq_diskctx, struct dsched_thread_io *tdio)
{
	helper_msg_t helper_msg = helper_msg_get(bfq_diskctx);

	helper_msg->tdio = tdio;
	helper_msg_send(bfq_diskctx, BFQ_MSG_DESTROY_TDIO, helper_msg);
}

void
helper_msg_kill(struct bfq_disk_ctx *bfq_diskctx)
{
	helper_msg_t helper_msg = helper_msg_get(bfq_diskctx);

	helper_msg_send(bfq_diskctx, BFQ_MSG_KILL, helper_msg);
}
