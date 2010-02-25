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
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_CONF_H_
#include <sys/conf.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
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


typedef int	dsched_prepare_t(struct disk *dp);
typedef void	dsched_teardown_t(struct disk *dp);
typedef void	dsched_flush_t(struct disk *dp, struct bio *bio);
typedef void	dsched_cancel_t(struct disk *dp);
typedef int	dsched_queue_t(struct disk *dp, struct bio *bio);
typedef	void	dsched_new_buf_t(struct buf *bp);
typedef	void	dsched_new_proc_t(struct proc *p);
typedef	void	dsched_new_thread_t(struct thread *td);
typedef	void	dsched_exit_proc_t(struct proc *p);
typedef	void	dsched_exit_thread_t(struct thread *td);

struct dsched_ops {
	struct {
		char		name[DSCHED_POLICY_NAME_LENGTH];
		uint64_t	uniq_id;
		int		ref_count;
	} head;

	dsched_prepare_t	*prepare;
	dsched_teardown_t	*teardown;
	dsched_flush_t		*flush;
	dsched_cancel_t		*cancel_all;
	dsched_queue_t		*bio_queue;

	dsched_new_buf_t	*new_buf;
	dsched_new_proc_t	*new_proc;
	dsched_new_thread_t	*new_thread;
	dsched_exit_proc_t	*exit_proc;
	dsched_exit_thread_t	*exit_thread;
};

struct dsched_policy {
	TAILQ_ENTRY(dsched_policy) link;

	struct dsched_ops	*d_ops;
};

struct dsched_object
{
	struct disk	*dp;
	struct bio	*bio;
	int		pid;
	struct thread	*thread;
	struct proc	*proc;
};

TAILQ_HEAD(dschedq, dsched_object);
TAILQ_HEAD(dsched_policy_head, dsched_policy);

void	dsched_create(struct disk *dp, const char *head_name, int unit);
void	dsched_destroy(struct disk *dp);
void	dsched_queue(struct disk *dp, struct bio *bio);
int	dsched_register(struct dsched_ops *d_ops);
int	dsched_unregister(struct dsched_ops *d_ops);
int	dsched_switch(struct disk *dp, struct dsched_ops *new_ops);
void	dsched_set_policy(struct disk *dp, struct dsched_ops *new_ops);
struct dsched_policy *dsched_find_policy(char *search);
struct disk	*dsched_find_disk(char *search);
struct dsched_policy *dsched_policy_enumerate(struct dsched_policy *pol);
struct disk	*dsched_disk_enumerate(struct disk *dp, struct dsched_ops *ops);
void	dsched_cancel_bio(struct bio *bp);
void	dsched_strategy_raw(struct disk *dp, struct bio *bp);
void	dsched_strategy_sync(struct disk *dp, struct bio *bp);
void	dsched_strategy_async(struct disk *dp, struct bio *bp, biodone_t *done, void *priv);
int	dsched_debug(int level, char *fmt, ...);
dsched_new_buf_t	dsched_new_buf;
dsched_new_proc_t	dsched_new_proc;
dsched_new_thread_t	dsched_new_thread;

dsched_exit_proc_t	dsched_exit_proc;
dsched_exit_thread_t	dsched_exit_thread;

#endif /* _KERNEL || _KERNEL_STRUCTURES */


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

#endif /* _SYS_DSCHED_H_ */
