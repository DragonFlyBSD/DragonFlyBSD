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

TAILQ_HEAD(tdio_list_head, dsched_thread_io);

MALLOC_DEFINE(M_DSCHED, "dsched", "dsched allocs");

static dsched_prepare_t		noop_prepare;
static dsched_teardown_t	noop_teardown;
static dsched_cancel_t		noop_cancel;
static dsched_queue_t		noop_queue;

static void dsched_thread_io_unref_destroy(struct dsched_thread_io *tdio);
static void dsched_sysctl_add_disk(struct dsched_disk_ctx *diskctx, char *name);
static void dsched_disk_ctx_destroy(struct dsched_disk_ctx *diskctx);
static void dsched_thread_io_destroy(struct dsched_thread_io *tdio);
static void dsched_thread_ctx_destroy(struct dsched_thread_ctx *tdctx);

static struct dsched_thread_io *dsched_thread_io_alloc(
		struct disk *dp, struct dsched_thread_ctx *tdctx,
		struct dsched_policy *pol, int tdctx_locked);

static int	dsched_inited = 0;
static int	default_set = 0;

struct lock	dsched_lock;
static int	dsched_debug_enable = 0;

struct dsched_stats	dsched_stats;

struct objcache_malloc_args dsched_disk_ctx_malloc_args = {
	DSCHED_DISK_CTX_MAX_SZ, M_DSCHED };
struct objcache_malloc_args dsched_thread_io_malloc_args = {
	DSCHED_THREAD_IO_MAX_SZ, M_DSCHED };
struct objcache_malloc_args dsched_thread_ctx_malloc_args = {
	DSCHED_THREAD_CTX_MAX_SZ, M_DSCHED };

static struct objcache	*dsched_diskctx_cache;
static struct objcache	*dsched_tdctx_cache;
static struct objcache	*dsched_tdio_cache;

struct lock	dsched_tdctx_lock;

static struct dsched_policy_head dsched_policy_list =
		TAILQ_HEAD_INITIALIZER(dsched_policy_list);

static struct dsched_policy dsched_noop_policy = {
	.name = "noop",

	.prepare = noop_prepare,
	.teardown = noop_teardown,
	.cancel_all = noop_cancel,
	.bio_queue = noop_queue
};

static struct dsched_policy *default_policy = &dsched_noop_policy;

/*
 * dsched_debug() is a SYSCTL and TUNABLE controlled debug output function
 * using kvprintf
 */
int
dsched_debug(int level, char *fmt, ...)
{
	__va_list ap;

	__va_start(ap, fmt);
	if (level <= dsched_debug_enable)
		kvprintf(fmt, ap);
	__va_end(ap);

	return 0;
}

/*
 * Called on disk_create()
 * tries to read which policy to use from loader.conf, if there's
 * none specified, the default policy is used.
 */
void
dsched_disk_create_callback(struct disk *dp, const char *head_name, int unit)
{
	char tunable_key[SPECNAMELEN + 48];
	char sched_policy[DSCHED_POLICY_NAME_LENGTH];
	char *ptr;
	struct dsched_policy *policy = NULL;

	/* Also look for serno stuff? */
	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	ksnprintf(tunable_key, sizeof(tunable_key),
		  "dsched.policy.%s%d", head_name, unit);
	if (TUNABLE_STR_FETCH(tunable_key, sched_policy,
	    sizeof(sched_policy)) != 0) {
		policy = dsched_find_policy(sched_policy);
	}

	ksnprintf(tunable_key, sizeof(tunable_key),
		  "dsched.policy.%s", head_name);

	for (ptr = tunable_key; *ptr; ptr++) {
		if (*ptr == '/')
			*ptr = '-';
	}
	if (!policy && (TUNABLE_STR_FETCH(tunable_key, sched_policy,
	    sizeof(sched_policy)) != 0)) {
		policy = dsched_find_policy(sched_policy);
	}

	ksnprintf(tunable_key, sizeof(tunable_key), "dsched.policy.default");
	if (!policy && !default_set &&
	    (TUNABLE_STR_FETCH(tunable_key, sched_policy,
			       sizeof(sched_policy)) != 0)) {
		policy = dsched_find_policy(sched_policy);
	}

	if (!policy) {
		if (!default_set && bootverbose) {
			dsched_debug(0,
				     "No policy for %s%d specified, "
				     "or policy not found\n",
				     head_name, unit);
		}
		dsched_set_policy(dp, default_policy);
	} else {
		dsched_set_policy(dp, policy);
	}

	if (strncmp(head_name, "mapper/", strlen("mapper/")) == 0)
		ksnprintf(tunable_key, sizeof(tunable_key), "%s", head_name);
	else
		ksnprintf(tunable_key, sizeof(tunable_key), "%s%d", head_name, unit);
	for (ptr = tunable_key; *ptr; ptr++) {
		if (*ptr == '/')
			*ptr = '-';
	}
	dsched_sysctl_add_disk(
	    (struct dsched_disk_ctx *)dsched_get_disk_priv(dp),
	    tunable_key);

	lockmgr(&dsched_lock, LK_RELEASE);
}

/*
 * Called from disk_setdiskinfo (or rather _setdiskinfo). This will check if
 * there's any policy associated with the serial number of the device.
 */
void
dsched_disk_update_callback(struct disk *dp, struct disk_info *info)
{
	char tunable_key[SPECNAMELEN + 48];
	char sched_policy[DSCHED_POLICY_NAME_LENGTH];
	struct dsched_policy *policy = NULL;

	if (info->d_serialno == NULL)
		return;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	ksnprintf(tunable_key, sizeof(tunable_key), "dsched.policy.%s",
	    info->d_serialno);

	if((TUNABLE_STR_FETCH(tunable_key, sched_policy,
	    sizeof(sched_policy)) != 0)) {
		policy = dsched_find_policy(sched_policy);	
	}

	if (policy) {
		dsched_switch(dp, policy);	
	}

	dsched_sysctl_add_disk(
	    (struct dsched_disk_ctx *)dsched_get_disk_priv(dp),
	    info->d_serialno);

	lockmgr(&dsched_lock, LK_RELEASE);
}

/*
 * Called on disk_destroy()
 * shuts down the scheduler core and cancels all remaining bios
 */
void
dsched_disk_destroy_callback(struct disk *dp)
{
	struct dsched_policy *old_policy;
	struct dsched_disk_ctx *diskctx;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	diskctx = dsched_get_disk_priv(dp);

	old_policy = dp->d_sched_policy;
	dp->d_sched_policy = &dsched_noop_policy;
	old_policy->cancel_all(dsched_get_disk_priv(dp));
	old_policy->teardown(dsched_get_disk_priv(dp));

	if (diskctx->flags & DSCHED_SYSCTL_CTX_INITED)
		sysctl_ctx_free(&diskctx->sysctl_ctx);

	policy_destroy(dp);
	atomic_subtract_int(&old_policy->ref_count, 1);
	KKASSERT(old_policy->ref_count >= 0);

	lockmgr(&dsched_lock, LK_RELEASE);
}


/*
 * Caller must have dp->diskctx locked
 */
void
dsched_queue(struct disk *dp, struct bio *bio)
{
	struct dsched_thread_ctx	*tdctx;
	struct dsched_thread_io		*tdio;
	struct dsched_disk_ctx		*diskctx;
	int	error;

	if (dp->d_sched_policy == &dsched_noop_policy) {
		dsched_clr_buf_priv(bio->bio_buf);
		atomic_add_int(&dsched_stats.no_tdctx, 1);
		dsched_strategy_raw(dp, bio);
		return;
	}

	error = 0;
	tdctx = dsched_get_buf_priv(bio->bio_buf);
	if (tdctx == NULL) {
		/* We don't handle this case, let dsched dispatch */
		atomic_add_int(&dsched_stats.no_tdctx, 1);
		dsched_strategy_raw(dp, bio);
		return;
	}

	DSCHED_THREAD_CTX_LOCK(tdctx);

	/*
	 * XXX:
	 * iterate in reverse to make sure we find the most up-to-date
	 * tdio for a given disk. After a switch it may take some time
	 * for everything to clean up.
	 */
	TAILQ_FOREACH_REVERSE(tdio, &tdctx->tdio_list, tdio_list_head, link) {
		if (tdio->dp == dp) {
			dsched_thread_io_ref(tdio);
			break;
		}
	}
	if (tdio == NULL) {
		tdio = dsched_thread_io_alloc(dp, tdctx, dp->d_sched_policy, 1);
		dsched_thread_io_ref(tdio);
	}

	DSCHED_THREAD_CTX_UNLOCK(tdctx);
	dsched_clr_buf_priv(bio->bio_buf);
	dsched_thread_ctx_unref(tdctx); /* acquired on new_buf */

	diskctx = dsched_get_disk_priv(dp);
	dsched_disk_ctx_ref(diskctx);

	if (dp->d_sched_policy != &dsched_noop_policy)
		KKASSERT(tdio->debug_policy == dp->d_sched_policy);

	KKASSERT(tdio->debug_inited == 0xF00F1234);

	error = dp->d_sched_policy->bio_queue(diskctx, tdio, bio);

	if (error) {
		dsched_strategy_raw(dp, bio);
	}
	dsched_disk_ctx_unref(diskctx);
	dsched_thread_io_unref(tdio);
}


/*
 * Called from each module_init or module_attach of each policy
 * registers the policy in the local policy list.
 */
int
dsched_register(struct dsched_policy *d_policy)
{
	struct dsched_policy *policy;
	int error = 0;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	policy = dsched_find_policy(d_policy->name);

	if (!policy) {
		TAILQ_INSERT_TAIL(&dsched_policy_list, d_policy, link);
		atomic_add_int(&d_policy->ref_count, 1);
	} else {
		dsched_debug(LOG_ERR, "Policy with name %s already registered!\n",
		    d_policy->name);
		error = EEXIST;
	}

	lockmgr(&dsched_lock, LK_RELEASE);
	return error;
}

/*
 * Called from each module_detach of each policy
 * unregisters the policy
 */
int
dsched_unregister(struct dsched_policy *d_policy)
{
	struct dsched_policy *policy;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);
	policy = dsched_find_policy(d_policy->name);

	if (policy) {
		if (policy->ref_count > 1) {
			lockmgr(&dsched_lock, LK_RELEASE);
			return EBUSY;
		}
		TAILQ_REMOVE(&dsched_policy_list, policy, link);
		atomic_subtract_int(&policy->ref_count, 1);
		KKASSERT(policy->ref_count == 0);
	}
	lockmgr(&dsched_lock, LK_RELEASE);

	return 0;
}


/*
 * switches the policy by first removing the old one and then
 * enabling the new one.
 */
int
dsched_switch(struct disk *dp, struct dsched_policy *new_policy)
{
	struct dsched_policy *old_policy;

	/* If we are asked to set the same policy, do nothing */
	if (dp->d_sched_policy == new_policy)
		return 0;

	/* lock everything down, diskwise */
	lockmgr(&dsched_lock, LK_EXCLUSIVE);
	old_policy = dp->d_sched_policy;

	atomic_subtract_int(&old_policy->ref_count, 1);
	KKASSERT(old_policy->ref_count >= 0);

	dp->d_sched_policy = &dsched_noop_policy;
	old_policy->teardown(dsched_get_disk_priv(dp));
	policy_destroy(dp);

	/* Bring everything back to life */
	dsched_set_policy(dp, new_policy);
	lockmgr(&dsched_lock, LK_RELEASE);

	return 0;
}


/*
 * Loads a given policy and attaches it to the specified disk.
 * Also initializes the core for the policy
 */
void
dsched_set_policy(struct disk *dp, struct dsched_policy *new_policy)
{
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the devfs lock */
	if ((lockstatus(&dsched_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&dsched_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	DSCHED_GLOBAL_THREAD_CTX_LOCK();

	policy_new(dp, new_policy);
	new_policy->prepare(dsched_get_disk_priv(dp));
	dp->d_sched_policy = new_policy;
	atomic_add_int(&new_policy->ref_count, 1);

	DSCHED_GLOBAL_THREAD_CTX_UNLOCK();

	kprintf("disk scheduler: set policy of %s to %s\n", dp->d_cdev->si_name,
	    new_policy->name);

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&dsched_lock, LK_RELEASE);
}

struct dsched_policy*
dsched_find_policy(char *search)
{
	struct dsched_policy *policy;
	struct dsched_policy *policy_found = NULL;
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the devfs lock */
	if ((lockstatus(&dsched_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&dsched_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	TAILQ_FOREACH(policy, &dsched_policy_list, link) {
		if (!strcmp(policy->name, search)) {
			policy_found = policy;
			break;
		}
	}

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&dsched_lock, LK_RELEASE);

	return policy_found;
}

/*
 * Returns ref'd disk
 */
struct disk *
dsched_find_disk(char *search)
{
	struct disk marker;
	struct disk *dp = NULL;

	while ((dp = disk_enumerate(&marker, dp)) != NULL) {
		if (strcmp(dp->d_cdev->si_name, search) == 0) {
			disk_enumerate_stop(&marker, NULL);
			/* leave ref on dp */
			break;
		}
	}
	return dp;
}

struct disk *
dsched_disk_enumerate(struct disk *marker, struct disk *dp,
		      struct dsched_policy *policy)
{
	while ((dp = disk_enumerate(marker, dp)) != NULL) {
		if (dp->d_sched_policy == policy)
			break;
	}
	return NULL;
}

struct dsched_policy *
dsched_policy_enumerate(struct dsched_policy *pol)
{
	if (!pol)
		return (TAILQ_FIRST(&dsched_policy_list));
	else
		return (TAILQ_NEXT(pol, link));
}

void
dsched_cancel_bio(struct bio *bp)
{
	bp->bio_buf->b_error = ENXIO;
	bp->bio_buf->b_flags |= B_ERROR;
	bp->bio_buf->b_resid = bp->bio_buf->b_bcount;

	biodone(bp);
}

void
dsched_strategy_raw(struct disk *dp, struct bio *bp)
{
	/*
	 * Ideally, this stuff shouldn't be needed... but just in case, we leave it in
	 * to avoid panics
	 */
	KASSERT(dp->d_rawdev != NULL, ("dsched_strategy_raw sees NULL d_rawdev!!"));
	if(bp->bio_track != NULL) {
		dsched_debug(LOG_INFO,
		    "dsched_strategy_raw sees non-NULL bio_track!! "
		    "bio: %p\n", bp);
		bp->bio_track = NULL;
	}
	dev_dstrategy(dp->d_rawdev, bp);
}

void
dsched_strategy_sync(struct disk *dp, struct bio *bio)
{
	struct buf *bp, *nbp;
	struct bio *nbio;

	bp = bio->bio_buf;

	nbp = getpbuf(NULL);
	nbio = &nbp->b_bio1;

	nbp->b_cmd = bp->b_cmd;
	nbp->b_bufsize = bp->b_bufsize;
	nbp->b_runningbufspace = bp->b_runningbufspace;
	nbp->b_bcount = bp->b_bcount;
	nbp->b_resid = bp->b_resid;
	nbp->b_data = bp->b_data;
#if 0
	/*
	 * Buffers undergoing device I/O do not need a kvabase/size.
	 */
	nbp->b_kvabase = bp->b_kvabase;
	nbp->b_kvasize = bp->b_kvasize;
#endif
	nbp->b_dirtyend = bp->b_dirtyend;

	nbio->bio_done = biodone_sync;
	nbio->bio_flags |= BIO_SYNC;
	nbio->bio_track = NULL;

	nbio->bio_caller_info1.ptr = dp;
	nbio->bio_offset = bio->bio_offset;

	dev_dstrategy(dp->d_rawdev, nbio);
	biowait(nbio, "dschedsync");
	bp->b_resid = nbp->b_resid;
	bp->b_error = nbp->b_error;
	biodone(bio);
#if 0
	nbp->b_kvabase = NULL;
	nbp->b_kvasize = 0;
#endif
	relpbuf(nbp, NULL);
}

void
dsched_strategy_async(struct disk *dp, struct bio *bio, biodone_t *done, void *priv)
{
	struct bio *nbio;

	nbio = push_bio(bio);
	nbio->bio_done = done;
	nbio->bio_offset = bio->bio_offset;

	dsched_set_bio_dp(nbio, dp);
	dsched_set_bio_priv(nbio, priv);

	getmicrotime(&nbio->bio_caller_info3.tv);
	dev_dstrategy(dp->d_rawdev, nbio);
}

/*
 * A special bio done call back function
 * used by policy having request polling implemented.
 */
static void
request_polling_biodone(struct bio *bp)
{
	struct dsched_disk_ctx *diskctx = NULL;
	struct disk *dp = NULL;
	struct bio *obio;
	struct dsched_policy *policy;

	dp = dsched_get_bio_dp(bp);
	policy = dp->d_sched_policy;
	diskctx = dsched_get_disk_priv(dp);
	KKASSERT(diskctx && policy);
	dsched_disk_ctx_ref(diskctx);

	/*
	 * XXX:
	 * the bio_done function should not be blocked !
	 */
	if (diskctx->dp->d_sched_policy->bio_done)
		diskctx->dp->d_sched_policy->bio_done(bp);

	obio = pop_bio(bp);
	biodone(obio);

	atomic_subtract_int(&diskctx->current_tag_queue_depth, 1);

	/* call the polling function,
	 * XXX:
	 * the polling function should not be blocked!
	 */
	if (policy->polling_func)
		policy->polling_func(diskctx);
	else
		dsched_debug(0, "dsched: the policy uses request polling without a polling function!\n");
	dsched_disk_ctx_unref(diskctx);
}

/*
 * A special dsched strategy used by policy having request polling
 * (polling function) implemented.
 *
 * The strategy is the just like dsched_strategy_async(), but
 * the biodone call back is set to a preset one.
 *
 * If the policy needs its own biodone callback, it should
 * register it in the policy structure. (bio_done field)
 *
 * The current_tag_queue_depth is maintained by this function
 * and the request_polling_biodone() function
 */

void
dsched_strategy_request_polling(struct disk *dp, struct bio *bio, struct dsched_disk_ctx *diskctx)
{
	atomic_add_int(&diskctx->current_tag_queue_depth, 1);
	dsched_strategy_async(dp, bio, request_polling_biodone, dsched_get_bio_priv(bio));
}

/*
 * Ref and deref various structures.  The 1->0 transition of the reference
 * count actually transitions 1->0x80000000 and causes the object to be
 * destroyed.  It is possible for transitory references to occur on the
 * object while it is being destroyed.  We use bit 31 to indicate that
 * destruction is in progress and to prevent nested destructions.
 */
void
dsched_disk_ctx_ref(struct dsched_disk_ctx *diskctx)
{
	int refcount __unused;

	refcount = atomic_fetchadd_int(&diskctx->refcount, 1);
}

void
dsched_thread_io_ref(struct dsched_thread_io *tdio)
{
	int refcount __unused;

	refcount = atomic_fetchadd_int(&tdio->refcount, 1);
}

void
dsched_thread_ctx_ref(struct dsched_thread_ctx *tdctx)
{
	int refcount __unused;

	refcount = atomic_fetchadd_int(&tdctx->refcount, 1);
}

void
dsched_disk_ctx_unref(struct dsched_disk_ctx *diskctx)
{
	int refs;
	int nrefs;

	/*
	 * Handle 1->0 transitions for diskctx and nested destruction
	 * recursions.  If the refs are already in destruction mode (bit 31
	 * set) on the 1->0 transition we don't try to destruct it again.
	 *
	 * 0x80000001->0x80000000 transitions are handled normally and
	 * thus avoid nested dstruction.
	 */
	for (;;) {
		refs = diskctx->refcount;
		cpu_ccfence();
		nrefs = refs - 1;

		KKASSERT(((refs ^ nrefs) & 0x80000000) == 0);
		if (nrefs) {
			if (atomic_cmpset_int(&diskctx->refcount, refs, nrefs))
				break;
			continue;
		}
		nrefs = 0x80000000;
		if (atomic_cmpset_int(&diskctx->refcount, refs, nrefs)) {
			dsched_disk_ctx_destroy(diskctx);
			break;
		}
	}
}

static
void
dsched_disk_ctx_destroy(struct dsched_disk_ctx *diskctx)
{
	struct dsched_thread_io	*tdio;
	int refs;
	int nrefs;

#if 0
	kprintf("diskctx (%p) destruction started, trace:\n", diskctx);
	print_backtrace(4);
#endif
	lockmgr(&diskctx->lock, LK_EXCLUSIVE);
	while ((tdio = TAILQ_FIRST(&diskctx->tdio_list)) != NULL) {
		KKASSERT(tdio->flags & DSCHED_LINKED_DISK_CTX);
		TAILQ_REMOVE(&diskctx->tdio_list, tdio, dlink);
		atomic_clear_int(&tdio->flags, DSCHED_LINKED_DISK_CTX);
		tdio->diskctx = NULL;
		/* XXX tdio->diskctx->dp->d_sched_policy->destroy_tdio(tdio);*/
		lockmgr(&diskctx->lock, LK_RELEASE);
		dsched_thread_io_unref_destroy(tdio);
		lockmgr(&diskctx->lock, LK_EXCLUSIVE);
	}
	lockmgr(&diskctx->lock, LK_RELEASE);

	/*
	 * Expect diskctx->refcount to be 0x80000000.  If it isn't someone
	 * else still has a temporary ref on the diskctx and we have to
	 * transition it back to an undestroyed-state (albeit without any
	 * associations), so the other user destroys it properly when the
	 * ref is released.
	 */
	while ((refs = diskctx->refcount) != 0x80000000) {
		kprintf("dsched_thread_io: destroy race diskctx=%p\n", diskctx);
		cpu_ccfence();
		KKASSERT(refs & 0x80000000);
		nrefs = refs & 0x7FFFFFFF;
		if (atomic_cmpset_int(&diskctx->refcount, refs, nrefs))
			return;
	}

	/*
	 * Really for sure now.
	 */
	if (diskctx->dp->d_sched_policy->destroy_diskctx)
		diskctx->dp->d_sched_policy->destroy_diskctx(diskctx);
	objcache_put(dsched_diskctx_cache, diskctx);
	atomic_subtract_int(&dsched_stats.diskctx_allocations, 1);
}

void
dsched_thread_io_unref(struct dsched_thread_io *tdio)
{
	int refs;
	int nrefs;

	/*
	 * Handle 1->0 transitions for tdio and nested destruction
	 * recursions.  If the refs are already in destruction mode (bit 31
	 * set) on the 1->0 transition we don't try to destruct it again.
	 *
	 * 0x80000001->0x80000000 transitions are handled normally and
	 * thus avoid nested dstruction.
	 */
	for (;;) {
		refs = tdio->refcount;
		cpu_ccfence();
		nrefs = refs - 1;

		KKASSERT(((refs ^ nrefs) & 0x80000000) == 0);
		if (nrefs) {
			if (atomic_cmpset_int(&tdio->refcount, refs, nrefs))
				break;
			continue;
		}
		nrefs = 0x80000000;
		if (atomic_cmpset_int(&tdio->refcount, refs, nrefs)) {
			dsched_thread_io_destroy(tdio);
			break;
		}
	}
}

/*
 * Unref and destroy the tdio even if additional refs are present.
 */
static
void
dsched_thread_io_unref_destroy(struct dsched_thread_io *tdio)
{
	int refs;
	int nrefs;

	/*
	 * If not already transitioned to destroy-in-progress we transition
	 * to destroy-in-progress, cleanup our ref, and destroy the tdio.
	 */
	for (;;) {
		refs = tdio->refcount;
		cpu_ccfence();
		nrefs = refs - 1;

		KKASSERT(((refs ^ nrefs) & 0x80000000) == 0);
		if (nrefs & 0x80000000) {
			if (atomic_cmpset_int(&tdio->refcount, refs, nrefs))
				break;
			continue;
		}
		nrefs |= 0x80000000;
		if (atomic_cmpset_int(&tdio->refcount, refs, nrefs)) {
			dsched_thread_io_destroy(tdio);
			break;
		}
	}
}

static void
dsched_thread_io_destroy(struct dsched_thread_io *tdio)
{
	struct dsched_thread_ctx *tdctx;
	struct dsched_disk_ctx	*diskctx;
	int refs;
	int nrefs;

#if 0
	kprintf("tdio (%p) destruction started, trace:\n", tdio);
	print_backtrace(8);
#endif
	KKASSERT(tdio->qlength == 0);

	while ((diskctx = tdio->diskctx) != NULL) {
		dsched_disk_ctx_ref(diskctx);
		lockmgr(&diskctx->lock, LK_EXCLUSIVE);
		if (diskctx != tdio->diskctx) {
			lockmgr(&diskctx->lock, LK_RELEASE);
			dsched_disk_ctx_unref(diskctx);
			continue;
		}
		KKASSERT(tdio->flags & DSCHED_LINKED_DISK_CTX);
		if (diskctx->dp->d_sched_policy->destroy_tdio)
			diskctx->dp->d_sched_policy->destroy_tdio(tdio);
		TAILQ_REMOVE(&diskctx->tdio_list, tdio, dlink);
		atomic_clear_int(&tdio->flags, DSCHED_LINKED_DISK_CTX);
		tdio->diskctx = NULL;
		dsched_thread_io_unref(tdio);
		lockmgr(&diskctx->lock, LK_RELEASE);
		dsched_disk_ctx_unref(diskctx);
	}
	while ((tdctx = tdio->tdctx) != NULL) {
		dsched_thread_ctx_ref(tdctx);
		lockmgr(&tdctx->lock, LK_EXCLUSIVE);
		if (tdctx != tdio->tdctx) {
			lockmgr(&tdctx->lock, LK_RELEASE);
			dsched_thread_ctx_unref(tdctx);
			continue;
		}
		KKASSERT(tdio->flags & DSCHED_LINKED_THREAD_CTX);
		TAILQ_REMOVE(&tdctx->tdio_list, tdio, link);
		atomic_clear_int(&tdio->flags, DSCHED_LINKED_THREAD_CTX);
		tdio->tdctx = NULL;
		dsched_thread_io_unref(tdio);
		lockmgr(&tdctx->lock, LK_RELEASE);
		dsched_thread_ctx_unref(tdctx);
	}

	/*
	 * Expect tdio->refcount to be 0x80000000.  If it isn't someone else
	 * still has a temporary ref on the tdio and we have to transition
	 * it back to an undestroyed-state (albeit without any associations)
	 * so the other user destroys it properly when the ref is released.
	 */
	while ((refs = tdio->refcount) != 0x80000000) {
		kprintf("dsched_thread_io: destroy race tdio=%p\n", tdio);
		cpu_ccfence();
		KKASSERT(refs & 0x80000000);
		nrefs = refs & 0x7FFFFFFF;
		if (atomic_cmpset_int(&tdio->refcount, refs, nrefs))
			return;
	}

	/*
	 * Really for sure now.
	 */
	objcache_put(dsched_tdio_cache, tdio);
	atomic_subtract_int(&dsched_stats.tdio_allocations, 1);
}

void
dsched_thread_ctx_unref(struct dsched_thread_ctx *tdctx)
{
	int refs;
	int nrefs;

	/*
	 * Handle 1->0 transitions for tdctx and nested destruction
	 * recursions.  If the refs are already in destruction mode (bit 31
	 * set) on the 1->0 transition we don't try to destruct it again.
	 *
	 * 0x80000001->0x80000000 transitions are handled normally and
	 * thus avoid nested dstruction.
	 */
	for (;;) {
		refs = tdctx->refcount;
		cpu_ccfence();
		nrefs = refs - 1;

		KKASSERT(((refs ^ nrefs) & 0x80000000) == 0);
		if (nrefs) {
			if (atomic_cmpset_int(&tdctx->refcount, refs, nrefs))
				break;
			continue;
		}
		nrefs = 0x80000000;
		if (atomic_cmpset_int(&tdctx->refcount, refs, nrefs)) {
			dsched_thread_ctx_destroy(tdctx);
			break;
		}
	}
}

static void
dsched_thread_ctx_destroy(struct dsched_thread_ctx *tdctx)
{
	struct dsched_thread_io	*tdio;

	lockmgr(&tdctx->lock, LK_EXCLUSIVE);

	while ((tdio = TAILQ_FIRST(&tdctx->tdio_list)) != NULL) {
		KKASSERT(tdio->flags & DSCHED_LINKED_THREAD_CTX);
		TAILQ_REMOVE(&tdctx->tdio_list, tdio, link);
		atomic_clear_int(&tdio->flags, DSCHED_LINKED_THREAD_CTX);
		tdio->tdctx = NULL;
		lockmgr(&tdctx->lock, LK_RELEASE);	/* avoid deadlock */
		dsched_thread_io_unref_destroy(tdio);
		lockmgr(&tdctx->lock, LK_EXCLUSIVE);
	}
	KKASSERT(tdctx->refcount == 0x80000000);

	lockmgr(&tdctx->lock, LK_RELEASE);

	objcache_put(dsched_tdctx_cache, tdctx);
	atomic_subtract_int(&dsched_stats.tdctx_allocations, 1);
}

/*
 * Ensures that a tdio is assigned to tdctx and disk.
 */
static
struct dsched_thread_io *
dsched_thread_io_alloc(struct disk *dp, struct dsched_thread_ctx *tdctx,
		       struct dsched_policy *pol, int tdctx_locked)
{
	struct dsched_thread_io	*tdio;
#if 0
	dsched_disk_ctx_ref(dsched_get_disk_priv(dp));
#endif
	tdio = objcache_get(dsched_tdio_cache, M_INTWAIT);
	bzero(tdio, DSCHED_THREAD_IO_MAX_SZ);

	dsched_thread_io_ref(tdio);	/* prevent ripout */
	dsched_thread_io_ref(tdio);	/* for diskctx ref */

	DSCHED_THREAD_IO_LOCKINIT(tdio);
	tdio->dp = dp;

	tdio->diskctx = dsched_get_disk_priv(dp);
	TAILQ_INIT(&tdio->queue);

	if (pol->new_tdio)
		pol->new_tdio(tdio);

	DSCHED_DISK_CTX_LOCK(tdio->diskctx);
	TAILQ_INSERT_TAIL(&tdio->diskctx->tdio_list, tdio, dlink);
	atomic_set_int(&tdio->flags, DSCHED_LINKED_DISK_CTX);
	DSCHED_DISK_CTX_UNLOCK(tdio->diskctx);

	if (tdctx) {
		/*
		 * Put the tdio in the tdctx list.  Inherit the temporary
		 * ref (one ref for each list).
		 */
		if (tdctx_locked == 0)
			DSCHED_THREAD_CTX_LOCK(tdctx);
		tdio->tdctx = tdctx;
		tdio->p = tdctx->p;
		TAILQ_INSERT_TAIL(&tdctx->tdio_list, tdio, link);
		atomic_set_int(&tdio->flags, DSCHED_LINKED_THREAD_CTX);
		if (tdctx_locked == 0)
			DSCHED_THREAD_CTX_UNLOCK(tdctx);
	} else {
		dsched_thread_io_unref(tdio);
	}

	tdio->debug_policy = pol;
	tdio->debug_inited = 0xF00F1234;

	atomic_add_int(&dsched_stats.tdio_allocations, 1);

	return(tdio);
}


struct dsched_disk_ctx *
dsched_disk_ctx_alloc(struct disk *dp, struct dsched_policy *pol)
{
	struct dsched_disk_ctx *diskctx;

	diskctx = objcache_get(dsched_diskctx_cache, M_WAITOK);
	bzero(diskctx, DSCHED_DISK_CTX_MAX_SZ);
	dsched_disk_ctx_ref(diskctx);
	diskctx->dp = dp;
	DSCHED_DISK_CTX_LOCKINIT(diskctx);
	TAILQ_INIT(&diskctx->tdio_list);
	/*
	 * XXX: magic number 32: most device has a tag queue
	 * of depth 32.
	 * Better to retrive more precise value from the driver
	 */
	diskctx->max_tag_queue_depth = 32;
	diskctx->current_tag_queue_depth = 0;

	atomic_add_int(&dsched_stats.diskctx_allocations, 1);
	if (pol->new_diskctx)
		pol->new_diskctx(diskctx);
	return diskctx;
}


struct dsched_thread_ctx *
dsched_thread_ctx_alloc(struct proc *p)
{
	struct dsched_thread_ctx	*tdctx;

	tdctx = objcache_get(dsched_tdctx_cache, M_WAITOK);
	bzero(tdctx, DSCHED_THREAD_CTX_MAX_SZ);
	dsched_thread_ctx_ref(tdctx);
#if 0
	kprintf("dsched_thread_ctx_alloc, new tdctx = %p\n", tdctx);
#endif
	DSCHED_THREAD_CTX_LOCKINIT(tdctx);
	TAILQ_INIT(&tdctx->tdio_list);
	tdctx->p = p;

	atomic_add_int(&dsched_stats.tdctx_allocations, 1);
	/* XXX: no callback here */

	return tdctx;
}

void
policy_new(struct disk *dp, struct dsched_policy *pol)
{
	struct dsched_disk_ctx *diskctx;

	diskctx = dsched_disk_ctx_alloc(dp, pol);
	dsched_disk_ctx_ref(diskctx);
	dsched_set_disk_priv(dp, diskctx);
}

void
policy_destroy(struct disk *dp) {
	struct dsched_disk_ctx *diskctx;

	diskctx = dsched_get_disk_priv(dp);
	KKASSERT(diskctx != NULL);

	dsched_disk_ctx_unref(diskctx); /* from prepare */
	dsched_disk_ctx_unref(diskctx); /* from alloc */

	dsched_set_disk_priv(dp, NULL);
}

void
dsched_new_buf(struct buf *bp)
{
	struct dsched_thread_ctx	*tdctx = NULL;

	if (dsched_inited == 0)
		return;

	if (curproc != NULL) {
		tdctx = dsched_get_proc_priv(curproc);
	} else {
		/* This is a kernel thread, so no proc info is available */
		tdctx = dsched_get_thread_priv(curthread);
	}

#if 0
	/*
	 * XXX: hack. we don't want this assert because we aren't catching all
	 *	threads. mi_startup() is still getting away without an tdctx.
	 */

	/* by now we should have an tdctx. if not, something bad is going on */
	KKASSERT(tdctx != NULL);
#endif

	if (tdctx) {
		dsched_thread_ctx_ref(tdctx);
	}
	dsched_set_buf_priv(bp, tdctx);
}

void
dsched_exit_buf(struct buf *bp)
{
	struct dsched_thread_ctx	*tdctx;

	tdctx = dsched_get_buf_priv(bp);
	if (tdctx != NULL) {
		dsched_clr_buf_priv(bp);
		dsched_thread_ctx_unref(tdctx);
	}
}

void
dsched_new_proc(struct proc *p)
{
	struct dsched_thread_ctx	*tdctx;

	if (dsched_inited == 0)
		return;

	KKASSERT(p != NULL);

	tdctx = dsched_thread_ctx_alloc(p);
	tdctx->p = p;
	dsched_thread_ctx_ref(tdctx);

	dsched_set_proc_priv(p, tdctx);
	atomic_add_int(&dsched_stats.nprocs, 1);
}


void
dsched_new_thread(struct thread *td)
{
	struct dsched_thread_ctx	*tdctx;

	if (dsched_inited == 0)
		return;

	KKASSERT(td != NULL);

	tdctx = dsched_thread_ctx_alloc(NULL);
	tdctx->td = td;
	dsched_thread_ctx_ref(tdctx);

	dsched_set_thread_priv(td, tdctx);
	atomic_add_int(&dsched_stats.nthreads, 1);
}

void
dsched_exit_proc(struct proc *p)
{
	struct dsched_thread_ctx	*tdctx;

	if (dsched_inited == 0)
		return;

	KKASSERT(p != NULL);

	tdctx = dsched_get_proc_priv(p);
	KKASSERT(tdctx != NULL);

	tdctx->dead = 0xDEAD;
	dsched_set_proc_priv(p, NULL);

	dsched_thread_ctx_unref(tdctx); /* one for alloc, */
	dsched_thread_ctx_unref(tdctx); /* one for ref */
	atomic_subtract_int(&dsched_stats.nprocs, 1);
}


void
dsched_exit_thread(struct thread *td)
{
	struct dsched_thread_ctx	*tdctx;

	if (dsched_inited == 0)
		return;

	KKASSERT(td != NULL);

	tdctx = dsched_get_thread_priv(td);
	KKASSERT(tdctx != NULL);

	tdctx->dead = 0xDEAD;
	dsched_set_thread_priv(td, 0);

	dsched_thread_ctx_unref(tdctx); /* one for alloc, */
	dsched_thread_ctx_unref(tdctx); /* one for ref */
	atomic_subtract_int(&dsched_stats.nthreads, 1);
}

/*
 * Returns ref'd tdio.
 *
 * tdio may have additional refs for the diskctx and tdctx it resides on.
 */
void
dsched_new_policy_thread_tdio(struct dsched_disk_ctx *diskctx,
			      struct dsched_policy *pol)
{
	struct dsched_thread_ctx *tdctx;

	tdctx = dsched_get_thread_priv(curthread);
	KKASSERT(tdctx != NULL);
	dsched_thread_io_alloc(diskctx->dp, tdctx, pol, 0);
}

/* DEFAULT NOOP POLICY */

static int
noop_prepare(struct dsched_disk_ctx *diskctx)
{
	return 0;
}

static void
noop_teardown(struct dsched_disk_ctx *diskctx)
{

}

static void
noop_cancel(struct dsched_disk_ctx *diskctx)
{

}

static int
noop_queue(struct dsched_disk_ctx *diskctx, struct dsched_thread_io *tdio,
	   struct bio *bio)
{
	dsched_strategy_raw(diskctx->dp, bio);
#if 0
	dsched_strategy_async(diskctx->dp, bio, noop_completed, NULL);
#endif
	return 0;
}

/*
 * SYSINIT stuff
 */
static void
dsched_init(void)
{
	dsched_tdio_cache = objcache_create("dsched-tdio-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_thread_io_malloc_args );

	dsched_tdctx_cache = objcache_create("dsched-tdctx-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_thread_ctx_malloc_args );

	dsched_diskctx_cache = objcache_create("dsched-diskctx-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_disk_ctx_malloc_args );

	bzero(&dsched_stats, sizeof(struct dsched_stats));

	lockinit(&dsched_lock, "dsched lock", 0, LK_CANRECURSE);
	DSCHED_GLOBAL_THREAD_CTX_LOCKINIT();

	dsched_register(&dsched_noop_policy);

	dsched_inited = 1;
}

static void
dsched_uninit(void)
{
}

SYSINIT(subr_dsched_register, SI_SUB_CREATE_INIT-1, SI_ORDER_FIRST, dsched_init, NULL);
SYSUNINIT(subr_dsched_register, SI_SUB_CREATE_INIT-1, SI_ORDER_ANY, dsched_uninit, NULL);

/*
 * SYSCTL stuff
 */
static int
sysctl_dsched_stats(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_handle_opaque(oidp, &dsched_stats, sizeof(struct dsched_stats), req));
}

static int
sysctl_dsched_list_policies(SYSCTL_HANDLER_ARGS)
{
	struct dsched_policy *pol = NULL;
	int error, first = 1;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	while ((pol = dsched_policy_enumerate(pol))) {
		if (!first) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error)
				break;
		} else {
			first = 0;
		}
		error = SYSCTL_OUT(req, pol->name, strlen(pol->name));
		if (error)
			break;

	}

	lockmgr(&dsched_lock, LK_RELEASE);

	error = SYSCTL_OUT(req, "", 1);

	return error;
}

static int
sysctl_dsched_policy(SYSCTL_HANDLER_ARGS)
{
	char buf[DSCHED_POLICY_NAME_LENGTH];
	struct dsched_disk_ctx *diskctx = arg1;
	struct dsched_policy *pol = NULL;
	int error;

	if (diskctx == NULL) {
		return 0;
	}

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	pol = diskctx->dp->d_sched_policy;
	memcpy(buf, pol->name, DSCHED_POLICY_NAME_LENGTH);

	error = sysctl_handle_string(oidp, buf, DSCHED_POLICY_NAME_LENGTH, req);
	if (error || req->newptr == NULL) {
		lockmgr(&dsched_lock, LK_RELEASE);
		return (error);
	}

	pol = dsched_find_policy(buf);
	if (pol == NULL) {
		lockmgr(&dsched_lock, LK_RELEASE);
		return 0;
	}

	dsched_switch(diskctx->dp, pol);

	lockmgr(&dsched_lock, LK_RELEASE);

	return error;
}

static int
sysctl_dsched_default_policy(SYSCTL_HANDLER_ARGS)
{
	char buf[DSCHED_POLICY_NAME_LENGTH];
	struct dsched_policy *pol = NULL;
	int error;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	pol = default_policy;
	memcpy(buf, pol->name, DSCHED_POLICY_NAME_LENGTH);

	error = sysctl_handle_string(oidp, buf, DSCHED_POLICY_NAME_LENGTH, req);
	if (error || req->newptr == NULL) {
		lockmgr(&dsched_lock, LK_RELEASE);
		return (error);
	}

	pol = dsched_find_policy(buf);
	if (pol == NULL) {
		lockmgr(&dsched_lock, LK_RELEASE);
		return 0;
	}

	default_set = 1;
	default_policy = pol;

	lockmgr(&dsched_lock, LK_RELEASE);

	return error;
}

SYSCTL_NODE(, OID_AUTO, dsched, CTLFLAG_RD, NULL,
    "Disk Scheduler Framework (dsched) magic");
SYSCTL_NODE(_dsched, OID_AUTO, policy, CTLFLAG_RW, NULL,
    "List of disks and their policies");
SYSCTL_INT(_dsched, OID_AUTO, debug, CTLFLAG_RW, &dsched_debug_enable,
    0, "Enable dsched debugging");
SYSCTL_PROC(_dsched, OID_AUTO, stats, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, sizeof(struct dsched_stats), sysctl_dsched_stats, "dsched_stats",
    "dsched statistics");
SYSCTL_PROC(_dsched, OID_AUTO, policies, CTLTYPE_STRING|CTLFLAG_RD,
    NULL, 0, sysctl_dsched_list_policies, "A", "names of available policies");
SYSCTL_PROC(_dsched_policy, OID_AUTO, default, CTLTYPE_STRING|CTLFLAG_RW,
    NULL, 0, sysctl_dsched_default_policy, "A", "default dsched policy");

static void
dsched_sysctl_add_disk(struct dsched_disk_ctx *diskctx, char *name)
{
	if (!(diskctx->flags & DSCHED_SYSCTL_CTX_INITED)) {
		diskctx->flags |= DSCHED_SYSCTL_CTX_INITED;
		sysctl_ctx_init(&diskctx->sysctl_ctx);
	}

	SYSCTL_ADD_PROC(&diskctx->sysctl_ctx, SYSCTL_STATIC_CHILDREN(_dsched_policy),
	    OID_AUTO, name, CTLTYPE_STRING|CTLFLAG_RW,
	    diskctx, 0, sysctl_dsched_policy, "A", "policy");
}
