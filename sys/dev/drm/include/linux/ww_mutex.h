/*
 * Copyright (c) 2015 Michael Neumann <mneumann@ntecs.de>
 * All rights reserved.
 * Copyright (c) 2003-2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Michael Neumann <mneumann@ntecs.de> and
 *    Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_WW_MUTEX_H_
#define _LINUX_WW_MUTEX_H_

#include <linux/mutex.h>

/*
 * A basic, unoptimized implementation of wound/wait mutexes for DragonFly
 * modelled after the Linux API [1].
 *
 * [1]: http://lxr.free-electrons.com/source/include/linux/ww_mutex.h
 */

#include <sys/errno.h>
#include <sys/types.h>
#include <machine/atomic.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>

struct ww_class {
	volatile u_long		stamp;
	const char		*name;
};

struct ww_acquire_ctx {
	u_long			stamp;
	int			acquired;
	int			unused01;
	struct ww_class		*ww_class;
};

struct ww_mutex {
	struct lock		base;
	struct ww_acquire_ctx	*ctx;
	u_long			stamp;	/* heuristic */
	int			blocked;
	int			unused01;
};

#define DEFINE_WW_CLASS(classname)	\
	struct ww_class classname = {	\
		.stamp = 0,		\
		.name = #classname	\
	}

extern void ww_acquire_init(struct ww_acquire_ctx *ctx,
			struct ww_class *ww_class);
extern void ww_acquire_done(struct ww_acquire_ctx *ctx);
extern void ww_acquire_fini(struct ww_acquire_ctx *ctx);
extern void ww_mutex_init(struct ww_mutex *ww, struct ww_class *ww_class);
extern int ww_mutex_lock(struct ww_mutex *ww, struct ww_acquire_ctx *ctx);
extern int ww_mutex_lock_slow(struct ww_mutex *ww, struct ww_acquire_ctx *ctx);
extern int ww_mutex_lock_interruptible(struct ww_mutex *ww,
			struct ww_acquire_ctx *ctx);
extern int ww_mutex_lock_slow_interruptible(struct ww_mutex *ww,
			struct ww_acquire_ctx *ctx);
extern void ww_mutex_unlock(struct ww_mutex *ww);
extern void ww_mutex_destroy(struct ww_mutex *ww);

/*
 * Returns 1 if locked, 0 otherwise
 */
static inline bool
ww_mutex_is_locked(struct ww_mutex *ww)
{
	return (lockstatus(&ww->base, NULL) != 0);
}

/*
 * Returns 1 on success, 0 if contended.
 *
 * This call has no context accounting.
 */
static inline int
ww_mutex_trylock(struct ww_mutex *ww)
{
	return (lockmgr(&ww->base, LK_EXCLUSIVE|LK_NOWAIT) == 0);
}

#endif	/* _LINUX_WW_MUTEX_H_ */
