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
#include <sys/sysctl.h>
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

MALLOC_DEFINE(M_DSCHED, "dsched", "Disk Scheduler Framework allocations");

static dsched_prepare_t	default_prepare;
static dsched_teardown_t	default_teardown;
static dsched_flush_t	default_flush;
static dsched_cancel_t	default_cancel;
static dsched_queue_t	default_queue;
#if 0
static biodone_t	default_completed;
#endif

dsched_new_buf_t	*default_new_buf;
dsched_new_proc_t	*default_new_proc;
dsched_new_thread_t	*default_new_thread;
dsched_exit_buf_t	*default_exit_buf;
dsched_exit_proc_t	*default_exit_proc;
dsched_exit_thread_t	*default_exit_thread;

static d_open_t      dsched_dev_open;
static d_close_t     dsched_dev_close;
static d_ioctl_t     dsched_dev_ioctl;

static int dsched_dev_list_disks(struct dsched_ioctl *data);
static int dsched_dev_list_disk(struct dsched_ioctl *data);
static int dsched_dev_list_policies(struct dsched_ioctl *data);
static int dsched_dev_handle_switch(char *disk, char *policy);


struct lock	dsched_lock;
static int	dsched_debug_enable = 0;
static int	dsched_test1 = 0;
static cdev_t	dsched_dev;

static struct dsched_policy_head dsched_policy_list =
		TAILQ_HEAD_INITIALIZER(dsched_policy_list);

static struct dsched_ops dsched_default_ops = {
	.head = {
		.name = "noop"
	},
	.prepare = default_prepare,
	.teardown = default_teardown,
	.flush = default_flush,
	.cancel_all = default_cancel,
	.bio_queue = default_queue,
};


static struct dev_ops dsched_dev_ops = {
	{ "dsched", 0, 0 },
	.d_open = dsched_dev_open,
	.d_close = dsched_dev_close,
	.d_ioctl = dsched_dev_ioctl
};

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
dsched_create(struct disk *dp, const char *head_name, int unit)
{
	char tunable_key[SPECNAMELEN + 48];
	char sched_policy[DSCHED_POLICY_NAME_LENGTH];
	struct dsched_policy *policy = NULL;

	/* Also look for serno stuff? */
	/* kprintf("dsched_create() for disk %s%d\n", head_name, unit); */
	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	ksnprintf(tunable_key, sizeof(tunable_key), "kern.dsched.policy.%s%d",
	    head_name, unit);
	if (TUNABLE_STR_FETCH(tunable_key, sched_policy,
	    sizeof(sched_policy)) != 0) {
		policy = dsched_find_policy(sched_policy);
	}

	ksnprintf(tunable_key, sizeof(tunable_key), "kern.dsched.policy.%s",
	    head_name);
	if (!policy && (TUNABLE_STR_FETCH(tunable_key, sched_policy,
	    sizeof(sched_policy)) != 0)) {
		policy = dsched_find_policy(sched_policy);
	}

	ksnprintf(tunable_key, sizeof(tunable_key), "kern.dsched.policy.default");
	if (!policy && (TUNABLE_STR_FETCH(tunable_key, sched_policy,
	    sizeof(sched_policy)) != 0)) {
		policy = dsched_find_policy(sched_policy);
	}

	if (!policy) {
		dsched_debug(0, "No policy for %s%d specified, "
		    "or policy not found\n", head_name, unit);
		dsched_set_policy(dp, &dsched_default_ops);
	} else {
		dsched_set_policy(dp, policy->d_ops);
	}

	lockmgr(&dsched_lock, LK_RELEASE);
}

/*
 * Called on disk_destroy()
 * shuts down the scheduler core and cancels all remaining bios
 */
void
dsched_destroy(struct disk *dp)
{
	struct dsched_ops *old_ops;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	old_ops = dp->d_sched_ops;
	dp->d_sched_ops = &dsched_default_ops;
	old_ops->cancel_all(dp);
	old_ops->teardown(dp);
	atomic_subtract_int(&old_ops->head.ref_count, 1);
	KKASSERT(old_ops->head.ref_count >= 0);

	lockmgr(&dsched_lock, LK_RELEASE);
}


void
dsched_queue(struct disk *dp, struct bio *bio)
{
	int error = 0;
	error = dp->d_sched_ops->bio_queue(dp, bio);

	if (error) {
		if (bio->bio_buf->b_cmd == BUF_CMD_FLUSH) {
			dp->d_sched_ops->flush(dp, bio);
		}
		dsched_strategy_raw(dp, bio);
	}
}


/*
 * Called from each module_init or module_attach of each policy
 * registers the policy in the local policy list.
 */
int
dsched_register(struct dsched_ops *d_ops)
{
	struct dsched_policy *policy;
	int error = 0;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);

	policy = dsched_find_policy(d_ops->head.name);

	if (!policy) {
		if ((d_ops->new_buf != NULL) || (d_ops->new_proc != NULL) ||
		    (d_ops->new_thread != NULL)) {
			/*
			 * Policy ops has hooks for proc/thread/buf creation,
			 * so check if there are already hooks for those present
			 * and if so, stop right now.
			 */
			if ((default_new_buf != NULL) || (default_new_proc != NULL) ||
			    (default_new_thread != NULL) || (default_exit_proc != NULL) ||
			    (default_exit_thread != NULL)) {
				dsched_debug(LOG_ERR, "A policy with "
				    "proc/thread/buf hooks is already in use!");
				error = 1;
				goto done;
			}

			/* If everything is fine, just register the hooks */
			default_new_buf = d_ops->new_buf;
			default_new_proc = d_ops->new_proc;
			default_new_thread = d_ops->new_thread;
			default_exit_buf = d_ops->exit_buf;
			default_exit_proc = d_ops->exit_proc;
			default_exit_thread = d_ops->exit_thread;
		}

		policy = kmalloc(sizeof(struct dsched_policy), M_DSCHED, M_WAITOK);
		policy->d_ops = d_ops;
		TAILQ_INSERT_TAIL(&dsched_policy_list, policy, link);
		atomic_add_int(&policy->d_ops->head.ref_count, 1);
	} else {
		dsched_debug(LOG_ERR, "Policy with name %s already registered!\n",
		    d_ops->head.name);
		error = 1;
	}

done:
	lockmgr(&dsched_lock, LK_RELEASE);
	return error;
}

/*
 * Called from each module_detach of each policy
 * unregisters the policy
 */
int
dsched_unregister(struct dsched_ops *d_ops)
{
	struct dsched_policy *policy;

	lockmgr(&dsched_lock, LK_EXCLUSIVE);
	policy = dsched_find_policy(d_ops->head.name);

	if (policy) {
		if (policy->d_ops->head.ref_count > 1)
			return 1;
		TAILQ_REMOVE(&dsched_policy_list, policy, link);
		atomic_subtract_int(&policy->d_ops->head.ref_count, 1);
		KKASSERT(policy->d_ops->head.ref_count >= 0);
		kfree(policy, M_DSCHED);
	}
	lockmgr(&dsched_lock, LK_RELEASE);
	return 0;
}


/*
 * switches the policy by first removing the old one and then
 * enabling the new one.
 */
int
dsched_switch(struct disk *dp, struct dsched_ops *new_ops)
{
	struct dsched_ops *old_ops;

	/* If we are asked to set the same policy, do nothing */
	if (dp->d_sched_ops == new_ops)
		return 0;

	/* lock everything down, diskwise */
	lockmgr(&dsched_lock, LK_EXCLUSIVE);
	old_ops = dp->d_sched_ops;

	atomic_subtract_int(&dp->d_sched_ops->head.ref_count, 1);
	KKASSERT(dp->d_sched_ops->head.ref_count >= 0);

	dp->d_sched_ops = &dsched_default_ops;
	old_ops->teardown(dp);

	/* Bring everything back to life */
	dsched_set_policy(dp, new_ops);
		lockmgr(&dsched_lock, LK_RELEASE);
	return 0;
}


/*
 * Loads a given policy and attaches it to the specified disk.
 * Also initializes the core for the policy
 */
void
dsched_set_policy(struct disk *dp, struct dsched_ops *new_ops)
{
	int locked = 0;

	/* Check if it is locked already. if not, we acquire the devfs lock */
	if (!(lockstatus(&dsched_lock, curthread)) == LK_EXCLUSIVE) {
		lockmgr(&dsched_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	new_ops->prepare(dp);
	dp->d_sched_ops = new_ops;
	atomic_add_int(&new_ops->head.ref_count, 1);
	kprintf("disk scheduler: set policy of %s to %s\n", dp->d_cdev->si_name,
	    new_ops->head.name);

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
	if (!(lockstatus(&dsched_lock, curthread)) == LK_EXCLUSIVE) {
		lockmgr(&dsched_lock, LK_EXCLUSIVE);
		locked = 1;
	}

	TAILQ_FOREACH(policy, &dsched_policy_list, link) {
		if (!strcmp(policy->d_ops->head.name, search)) {
			policy_found = policy;
			break;
		}
	}

	/* If we acquired the lock, we also get rid of it */
	if (locked)
		lockmgr(&dsched_lock, LK_RELEASE);

	return policy_found;
}

struct disk*
dsched_find_disk(char *search)
{
	struct disk *dp_found = NULL;
	struct disk *dp = NULL;

	while((dp = disk_enumerate(dp))) {
		if (!strcmp(dp->d_cdev->si_name, search)) {
			dp_found = dp;
			break;
		}
	}

	return dp_found;
}

struct disk*
dsched_disk_enumerate(struct disk *dp, struct dsched_ops *ops)
{
	while ((dp = disk_enumerate(dp))) {
		if (dp->d_sched_ops == ops)
			return dp;
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
		    "bio: %x\n", (uint32_t)bp);
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
	nbp->b_kvabase = bp->b_kvabase;
	nbp->b_kvasize = bp->b_kvasize;
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

void
dsched_new_buf(struct buf *bp)
{
	if (default_new_buf != NULL)
		default_new_buf(bp);
}

void
dsched_exit_buf(struct buf *bp)
{
	if (default_exit_buf != NULL)
		default_exit_buf(bp);
}

void
dsched_new_proc(struct proc *p)
{
	if (default_new_proc != NULL)
		default_new_proc(p);
}


void
dsched_new_thread(struct thread *td)
{
	if (default_new_thread != NULL)
		default_new_thread(td);
}

void
dsched_exit_proc(struct proc *p)
{
	if (default_exit_proc != NULL)
		default_exit_proc(p);
}


void
dsched_exit_thread(struct thread *td)
{
	if (default_exit_thread != NULL)
		default_exit_thread(td);
}

int
default_prepare(struct disk *dp)
{
	return 0;
}

void
default_teardown(struct disk *dp)
{

}

void
default_flush(struct disk *dp, struct bio *bio)
{

}

void
default_cancel(struct disk *dp)
{

}

int
default_queue(struct disk *dp, struct bio *bio)
{
	dsched_strategy_raw(dp, bio);
#if 0
	dsched_strategy_async(dp, bio, default_completed, NULL);
#endif
	return 0;
}

#if 0
void
default_completed(struct bio *bp)
{
	struct bio *obio;

	obio = pop_bio(bp);
	biodone(obio);
}
#endif

/*
 * dsched device stuff
 */

static int
dsched_dev_list_disks(struct dsched_ioctl *data)
{
	struct disk *dp = NULL;
	uint32_t i;

	for (i = 0; (i <= data->num_elem) && (dp = disk_enumerate(dp)); i++);

	if (dp == NULL)
		return -1;

	strncpy(data->dev_name, dp->d_cdev->si_name, sizeof(data->dev_name));

	if (dp->d_sched_ops) {
		strncpy(data->pol_name, dp->d_sched_ops->head.name,
		    sizeof(data->pol_name));
	} else {
		strncpy(data->pol_name, "N/A (error)", 12);
	}

	return 0;
}

static int
dsched_dev_list_disk(struct dsched_ioctl *data)
{
	struct disk *dp = NULL;
	int found = 0;

	while ((dp = disk_enumerate(dp))) {
		if (!strncmp(dp->d_cdev->si_name, data->dev_name,
		    sizeof(data->dev_name))) {
			KKASSERT(dp->d_sched_ops != NULL);

			found = 1;
			strncpy(data->pol_name, dp->d_sched_ops->head.name,
			    sizeof(data->pol_name));
			break;
		}
	}
	if (!found)
		return -1;

	return 0;
}

static int
dsched_dev_list_policies(struct dsched_ioctl *data)
{
	struct dsched_policy *pol = NULL;
	uint32_t i;

	for (i = 0; (i <= data->num_elem) && (pol = dsched_policy_enumerate(pol)); i++);

	if (pol == NULL)
		return -1;

	strncpy(data->pol_name, pol->d_ops->head.name, sizeof(data->pol_name));
	return 0;
}

static int
dsched_dev_handle_switch(char *disk, char *policy)
{
	struct disk *dp;
	struct dsched_policy *pol;

	dp = dsched_find_disk(disk);
	pol = dsched_find_policy(policy);

	if ((dp == NULL) || (pol == NULL))
		return -1;

	return (dsched_switch(dp, pol->d_ops));
}

static int
dsched_dev_open(struct dev_open_args *ap)
{
	/*
	 * Only allow read-write access.
	 */
	if (((ap->a_oflags & FWRITE) == 0) || ((ap->a_oflags & FREAD) == 0))
		return(EPERM);

	/*
	 * We don't allow nonblocking access.
	 */
	if ((ap->a_oflags & O_NONBLOCK) != 0) {
		kprintf("dsched_dev: can't do nonblocking access\n");
		return(ENODEV);
	}

	return 0;
}

static int
dsched_dev_close(struct dev_close_args *ap)
{
	return 0;
}

static int
dsched_dev_ioctl(struct dev_ioctl_args *ap)
{
	int error;
	struct dsched_ioctl *data;

	error = 0;
	data = (struct dsched_ioctl *)ap->a_data;

	switch(ap->a_cmd) {
	case DSCHED_SET_DEVICE_POLICY:
		if (dsched_dev_handle_switch(data->dev_name, data->pol_name))
			error = ENOENT; /* No such file or directory */
		break;

	case DSCHED_LIST_DISK:
		if (dsched_dev_list_disk(data) != 0) {
			error = EINVAL; /* Invalid argument */
		}
		break;

	case DSCHED_LIST_DISKS:
		if (dsched_dev_list_disks(data) != 0) {
			error = EINVAL; /* Invalid argument */
		}
		break;

	case DSCHED_LIST_POLICIES:
		if (dsched_dev_list_policies(data) != 0) {
			error = EINVAL; /* Invalid argument */
		}
		break;


	default:
		error = ENOTTY; /* Inappropriate ioctl for device */
		break;
	}

	return(error);
}

/*
 * SYSINIT stuff
 */


static void
dsched_init(void)
{
	lockinit(&dsched_lock, "dsched lock", 0, 0);
	dsched_register(&dsched_default_ops);
}

static void
dsched_uninit(void)
{
}

static void
dsched_dev_init(void)
{
	dsched_dev = make_dev(&dsched_dev_ops,
            0,
            UID_ROOT,
            GID_WHEEL,
            0600,
            "dsched");
}

static void
dsched_dev_uninit(void)
{
	destroy_dev(dsched_dev);
}

SYSINIT(subr_dsched_register, SI_SUB_CREATE_INIT-2, SI_ORDER_FIRST, dsched_init, NULL);
SYSUNINIT(subr_dsched_register, SI_SUB_CREATE_INIT-2, SI_ORDER_ANY, dsched_uninit, NULL);
SYSINIT(subr_dsched_dev_register, SI_SUB_DRIVERS, SI_ORDER_ANY, dsched_dev_init, NULL);
SYSUNINIT(subr_dsched_dev_register, SI_SUB_DRIVERS, SI_ORDER_ANY, dsched_dev_uninit, NULL);

/*
 * SYSCTL stuff
 */
SYSCTL_INT(_kern, OID_AUTO, dsched_debug, CTLFLAG_RW, &dsched_debug_enable,
		0, "Enable dsched debugging");
SYSCTL_INT(_kern, OID_AUTO, dsched_test1, CTLFLAG_RW, &dsched_test1,
		0, "Switch dsched test1 method");
