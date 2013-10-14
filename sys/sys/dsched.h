/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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

#ifndef _SYS_DSCHED_H_
#define	_SYS_DSCHED_H_

#if defined(_KERNEL)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_BIO_H_
#include <sys/bio.h>
#endif
#ifndef _SYS_BIOTRACK_H_
#include <sys/biotrack.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_CONF_H_
#include <sys/conf.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif
#ifndef _SYS_SYSCTL_H_
#include <sys/sysctl.h>
#endif
#ifndef _SYS_DISK_H_
#include <sys/disk.h>
#endif

#define	DSCHED_POLICY_NAME_LENGTH	64

#define dsched_set_disk_priv(dp, x)	((dp)->d_dsched_priv1 = (x))
#define dsched_get_disk_priv(dp)	((dp)?((dp)->d_dsched_priv1):NULL)
#define dsched_set_proc_priv(pp, x)	((pp)->p_dsched_priv1 = (x))
#define dsched_get_proc_priv(pp)	((pp)?((pp)->p_dsched_priv1):NULL)

#define dsched_set_thread_priv(td, x)	((td)->td_dsched_priv1 = (x))
#define dsched_get_thread_priv(td)	((td)?((td)->td_dsched_priv1):NULL)

#define dsched_set_buf_priv(bp, x)	((bp)->b_iosched = (x))
#define dsched_get_buf_priv(bp)		((bp)?((bp)->b_iosched):NULL)
#define	dsched_clr_buf_priv(bp)		((bp)->b_iosched = NULL)
#define	dsched_is_clear_buf_priv(bp)	((bp)->b_iosched == NULL)


#define	dsched_set_bio_dp(bio, x)	((bio)->bio_caller_info1.ptr = (x))
#define	dsched_get_bio_dp(bio)		((bio)?((bio)->bio_caller_info1.ptr):NULL)
#define	dsched_set_bio_priv(bio, x)	((bio)->bio_caller_info2.ptr = (x))
#define	dsched_get_bio_priv(bio)	((bio)?((bio)->bio_caller_info2.ptr):NULL)
#define	dsched_set_bio_stime(bio, x)	((bio)->bio_caller_info3.lvalue = (x))
#define	dsched_get_bio_stime(bio)	((bio)?((bio)->bio_caller_info3.lvalue):0)
#define	dsched_set_bio_tdio(bio, x)	((bio)->bio_caller_info3.ptr = (x))
#define	dsched_get_bio_tdio(bio)	((bio)?((bio)->bio_caller_info3.ptr):0)


struct dsched_thread_ctx {
	TAILQ_ENTRY(dsched_thread_ctx)	link;

	TAILQ_HEAD(, dsched_thread_io)	tdio_list;	/* list of thread_io */
	struct lock	lock;

	int32_t		refcount;
	
	struct proc *p;
	struct thread *td;	
	int32_t	dead;
};

struct dsched_disk_ctx {
	TAILQ_ENTRY(dsched_disk_ctx)	link;

	TAILQ_HEAD(, dsched_thread_io)	tdio_list;	/* list of thread_io of disk */
	struct lock	lock;

	int32_t		refcount;
	int32_t		flags;

	int		max_tag_queue_depth;		/* estimated max tag queue depth */
	int		current_tag_queue_depth;	/* estimated current tag queue depth */

	struct disk	*dp;		/* back pointer to disk struct */

	struct sysctl_ctx_list sysctl_ctx;
};

struct dsched_policy;

struct dsched_thread_io {
	TAILQ_ENTRY(dsched_thread_io)	link;
	TAILQ_ENTRY(dsched_thread_io)	dlink;

	TAILQ_HEAD(, bio)	queue;	/* IO queue (bio) */
	struct lock		lock;
	int32_t			qlength;/* IO queue length */

	int32_t	refcount;

	int32_t	flags;
	
	struct disk		*dp;
	struct dsched_disk_ctx	*diskctx;
	struct dsched_thread_ctx	*tdctx;
	struct proc		*p;
	struct dsched_policy	*debug_policy;
	int			debug_inited;
	int			debug_priv;
};

typedef int	dsched_prepare_t(struct dsched_disk_ctx *diskctx);
typedef void	dsched_teardown_t(struct dsched_disk_ctx *diskctx);
typedef void	dsched_cancel_t(struct dsched_disk_ctx *diskctx);
typedef int	dsched_queue_t(struct dsched_disk_ctx *diskctx,
		    struct dsched_thread_io *tdio, struct bio *bio);
typedef void dsched_dequeue_t(struct dsched_disk_ctx *diskctx);

typedef	void	dsched_new_tdio_t(struct dsched_thread_io *tdio);
typedef	void	dsched_new_diskctx_t(struct dsched_disk_ctx *diskctx);
typedef	void	dsched_destroy_tdio_t(struct dsched_thread_io *tdio);
typedef	void	dsched_destroy_diskctx_t(struct dsched_disk_ctx *diskctx);
typedef void	dsched_bio_done_t(struct bio *bio);
typedef void	dsched_polling_func_t(struct dsched_disk_ctx *diskctx);

struct dsched_policy {
	char			name[DSCHED_POLICY_NAME_LENGTH];
	uint64_t		uniq_id;
	int			ref_count;

	TAILQ_ENTRY(dsched_policy) link;

	dsched_prepare_t	*prepare;
	dsched_teardown_t	*teardown;
	dsched_cancel_t		*cancel_all;
	dsched_queue_t		*bio_queue;

	dsched_new_tdio_t	*new_tdio;
	dsched_new_diskctx_t	*new_diskctx;
	dsched_destroy_tdio_t	*destroy_tdio;
	dsched_destroy_diskctx_t	*destroy_diskctx;

	dsched_bio_done_t	*bio_done;	/* call back when a bio dispatched by dsched_strategy_request_polling() is done */
	dsched_polling_func_t	*polling_func; /* it gets called when the disk is idle or about to idle */
};

TAILQ_HEAD(dsched_policy_head, dsched_policy);


#define	DSCHED_THREAD_IO_LOCKINIT(x)	\
		lockinit(&(x)->lock, "tdiobioq", 0, LK_CANRECURSE)

#define	DSCHED_THREAD_IO_LOCK(x)	do {			\
			dsched_thread_io_ref((x)); 		\
			lockmgr(&(x)->lock, LK_EXCLUSIVE);	\
		} while(0)

#define	DSCHED_THREAD_IO_UNLOCK(x)	do {			\
			lockmgr(&(x)->lock, LK_RELEASE);	\
			dsched_thread_io_unref((x));		\
		} while(0)

#define	DSCHED_DISK_CTX_LOCKINIT(x)	\
		lockinit(&(x)->lock, "tdiodiskq", 0, LK_CANRECURSE)

#define	DSCHED_DISK_CTX_LOCK(x)		do {			\
			dsched_disk_ctx_ref((x));		\
			lockmgr(&(x)->lock, LK_EXCLUSIVE);	\
		} while(0)

#define	DSCHED_DISK_CTX_UNLOCK(x)	do {			\
			lockmgr(&(x)->lock, LK_RELEASE);	\
			dsched_disk_ctx_unref((x));		\
		} while(0)

#define DSCHED_DISK_CTX_LOCK_ASSERT(x)	\
		KKASSERT(lockstatus(&(x)->lock, curthread) == LK_EXCLUSIVE)

#define	DSCHED_GLOBAL_THREAD_CTX_LOCKINIT(x)	\
		lockinit(&dsched_tdctx_lock, "tdctxglob", 0, LK_CANRECURSE)
#define	DSCHED_GLOBAL_THREAD_CTX_LOCK(x)	\
		lockmgr(&dsched_tdctx_lock, LK_EXCLUSIVE)
#define	DSCHED_GLOBAL_THREAD_CTX_UNLOCK(x)	\
		lockmgr(&dsched_tdctx_lock, LK_RELEASE)

#define	DSCHED_THREAD_CTX_LOCKINIT(x)	\
		lockinit(&(x)->lock, "tdctx", 0, LK_CANRECURSE)

#define	DSCHED_THREAD_CTX_LOCK(x)	do {			\
			dsched_thread_ctx_ref((x));		\
			lockmgr(&(x)->lock, LK_EXCLUSIVE);	\
		} while(0)

#define DSCHED_THREAD_CTX_UNLOCK(x)	do {			\
			lockmgr(&(x)->lock, LK_RELEASE);	\
			dsched_thread_ctx_unref((x));		\
		} while(0)

/* flags for thread_io */
#define	DSCHED_LINKED_DISK_CTX		0x01
#define	DSCHED_LINKED_THREAD_CTX	0x02
/* flags for disk_ctx */
#define	DSCHED_SYSCTL_CTX_INITED	0x01

#define DSCHED_THREAD_CTX_MAX_SZ	sizeof(struct dsched_thread_ctx)
#define DSCHED_THREAD_IO_MAX_SZ		384
#define DSCHED_DISK_CTX_MAX_SZ		1024

#define DSCHED_POLICY_MODULE(name, evh, version)			\
static moduledata_t name##_mod = {					\
    #name,								\
    evh,								\
    NULL								\
};									\
DECLARE_MODULE(name, name##_mod, SI_SUB_PRE_DRIVERS, SI_ORDER_MIDDLE);	\
MODULE_VERSION(name, version)

void	dsched_disk_create_callback(struct disk *dp, const char *head_name, int unit);
void	dsched_disk_update_callback(struct disk *dp, struct disk_info *info);
void	dsched_disk_destroy_callback(struct disk *dp);
void	dsched_queue(struct disk *dp, struct bio *bio);
int	dsched_register(struct dsched_policy *d_policy);
int	dsched_unregister(struct dsched_policy *d_policy);
int	dsched_switch(struct disk *dp, struct dsched_policy *new_policy);
void	dsched_set_policy(struct disk *dp, struct dsched_policy *new_policy);
struct dsched_policy *dsched_find_policy(char *search);
struct disk *dsched_find_disk(char *search);
struct dsched_policy *dsched_policy_enumerate(struct dsched_policy *pol);
struct disk *dsched_disk_enumerate(struct disk *marker, struct disk *dp,
			struct dsched_policy *policy);
void	dsched_cancel_bio(struct bio *bp);
void	dsched_strategy_raw(struct disk *dp, struct bio *bp);
void	dsched_strategy_sync(struct disk *dp, struct bio *bp);
void	dsched_strategy_async(struct disk *dp, struct bio *bp, biodone_t *done, void *priv);
void	dsched_strategy_request_polling(struct disk *bp, struct bio *bio, struct dsched_disk_ctx *diskctx);
int	dsched_debug(int level, char *fmt, ...) __printflike(2, 3);

void	policy_new(struct disk *dp, struct dsched_policy *pol);
void	policy_destroy(struct disk *dp);

void	dsched_disk_ctx_ref(struct dsched_disk_ctx *diskctx);
void	dsched_thread_io_ref(struct dsched_thread_io *tdio);
void	dsched_thread_ctx_ref(struct dsched_thread_ctx *tdctx);
void	dsched_disk_ctx_unref(struct dsched_disk_ctx *diskctx);
void	dsched_thread_io_unref(struct dsched_thread_io *tdio);
void	dsched_thread_ctx_unref(struct dsched_thread_ctx *tdctx);

void	dsched_new_policy_thread_tdio(struct dsched_disk_ctx *diskctx,
			struct dsched_policy *pol);
void	dsched_thread_io_alloc(struct disk *dp,
			struct dsched_thread_ctx *tdctx,
			struct dsched_policy *pol);
struct dsched_disk_ctx *dsched_disk_ctx_alloc(struct disk *dp,
			struct dsched_policy *pol);
struct dsched_thread_ctx *dsched_thread_ctx_alloc(struct proc *p);

typedef	void	dsched_new_buf_t(struct buf *bp);
typedef	void	dsched_new_proc_t(struct proc *p);
typedef	void	dsched_new_thread_t(struct thread *td);
typedef	void	dsched_exit_buf_t(struct buf *bp);
typedef	void	dsched_exit_proc_t(struct proc *p);
typedef	void	dsched_exit_thread_t(struct thread *td);

dsched_new_buf_t	dsched_new_buf;
dsched_new_proc_t	dsched_new_proc;
dsched_new_thread_t	dsched_new_thread;
dsched_exit_buf_t	dsched_exit_buf;
dsched_exit_proc_t	dsched_exit_proc;
dsched_exit_thread_t	dsched_exit_thread;

#endif /* _KERNEL */


#define	DSCHED_NAME_LENGTH		64
#define	DSCHED_SET_DEVICE_POLICY	_IOWR('d', 1, struct dsched_ioctl)
#define	DSCHED_LIST_DISKS		_IOWR('d', 2, struct dsched_ioctl)
#define	DSCHED_LIST_DISK		_IOWR('d', 3, struct dsched_ioctl)
#define	DSCHED_LIST_POLICIES		_IOWR('d', 4, struct dsched_ioctl)

struct dsched_ioctl {
	uint16_t	num_elem;
	char		dev_name[DSCHED_NAME_LENGTH];
	char		pol_name[DSCHED_NAME_LENGTH];
};

struct dsched_stats {
	int32_t	tdctx_allocations;
	int32_t	tdio_allocations;
	int32_t	diskctx_allocations;

	int32_t	no_tdctx;

	int32_t	nthreads;
	int32_t	nprocs;
};

#endif /* !_SYS_DSCHED_H_ */
