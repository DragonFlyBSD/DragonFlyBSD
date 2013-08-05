/**************************************************************************
 *
 * Copyright (c) 2007-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
/*
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * <kib@FreeBSD.org> under sponsorship from the FreeBSD Foundation.
 *
 * $FreeBSD: head/sys/dev/drm2/ttm/ttm_lock.c 247835 2013-03-05 09:49:34Z kib $
 */

#include <dev/drm2/ttm/ttm_lock.h>
#include <dev/drm2/ttm/ttm_module.h>

#define TTM_WRITE_LOCK_PENDING    (1 << 0)
#define TTM_VT_LOCK_PENDING       (1 << 1)
#define TTM_SUSPEND_LOCK_PENDING  (1 << 2)
#define TTM_VT_LOCK               (1 << 3)
#define TTM_SUSPEND_LOCK          (1 << 4)

void ttm_lock_init(struct ttm_lock *lock)
{
	lockinit(&lock->lock, "ttmlk", 0, LK_CANRECURSE);
	lock->rw = 0;
	lock->flags = 0;
	lock->kill_takers = false;
	lock->signal = SIGKILL;
}

static void
ttm_lock_send_sig(int signo)
{
	struct proc *p;

	p = curproc;	/* XXXKIB curthread ? */
	PROC_LOCK(p);
	kern_psignal(p, signo);
	PROC_UNLOCK(p);
}

void ttm_read_unlock(struct ttm_lock *lock)
{
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	if (--lock->rw == 0)
		wakeup(lock);
	lockmgr(&lock->lock, LK_RELEASE);
}

static bool __ttm_read_lock(struct ttm_lock *lock)
{
	bool locked = false;

	if (unlikely(lock->kill_takers)) {
		ttm_lock_send_sig(lock->signal);
		return false;
	}
	if (lock->rw >= 0 && lock->flags == 0) {
		++lock->rw;
		locked = true;
	}
	return locked;
}

int
ttm_read_lock(struct ttm_lock *lock, bool interruptible)
{
	const char *wmsg;
	int flags, ret;

	ret = 0;
	if (interruptible) {
		flags = PCATCH;
		wmsg = "ttmri";
	} else {
		flags = 0;
		wmsg = "ttmr";
	}
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	while (!__ttm_read_lock(lock)) {
		ret = msleep(lock, &lock->lock, flags, wmsg, 0);
		if (ret != 0)
			break;
	}
	return (-ret);
}

static bool __ttm_read_trylock(struct ttm_lock *lock, bool *locked)
{
	bool block = true;

	*locked = false;

	if (unlikely(lock->kill_takers)) {
		ttm_lock_send_sig(lock->signal);
		return false;
	}
	if (lock->rw >= 0 && lock->flags == 0) {
		++lock->rw;
		block = false;
		*locked = true;
	} else if (lock->flags == 0) {
		block = false;
	}

	return !block;
}

int ttm_read_trylock(struct ttm_lock *lock, bool interruptible)
{
	const char *wmsg;
	int flags, ret;
	bool locked;

	ret = 0;
	if (interruptible) {
		flags = PCATCH;
		wmsg = "ttmrti";
	} else {
		flags = 0;
		wmsg = "ttmrt";
	}
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	while (!__ttm_read_trylock(lock, &locked)) {
		ret = msleep(lock, &lock->lock, flags, wmsg, 0);
		if (ret != 0)
			break;
	}
	KKASSERT(!locked || ret == 0);
	lockmgr(&lock->lock, LK_RELEASE);

	return (locked) ? 0 : -EBUSY;
}

void ttm_write_unlock(struct ttm_lock *lock)
{
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	lock->rw = 0;
	wakeup(lock);
	lockmgr(&lock->lock, LK_RELEASE);
}

static bool __ttm_write_lock(struct ttm_lock *lock)
{
	bool locked = false;

	if (unlikely(lock->kill_takers)) {
		ttm_lock_send_sig(lock->signal);
		return false;
	}
	if (lock->rw == 0 && ((lock->flags & ~TTM_WRITE_LOCK_PENDING) == 0)) {
		lock->rw = -1;
		lock->flags &= ~TTM_WRITE_LOCK_PENDING;
		locked = true;
	} else {
		lock->flags |= TTM_WRITE_LOCK_PENDING;
	}
	return locked;
}

int
ttm_write_lock(struct ttm_lock *lock, bool interruptible)
{
	const char *wmsg;
	int flags, ret;

	ret = 0;
	if (interruptible) {
		flags = PCATCH;
		wmsg = "ttmwi";
	} else {
		flags = 0;
		wmsg = "ttmw";
	}
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	/* XXXKIB: linux uses __ttm_read_lock for uninterruptible sleeps */
	while (!__ttm_write_lock(lock)) {
		ret = msleep(lock, &lock->lock, flags, wmsg, 0);
		if (interruptible && ret != 0) {
			lock->flags &= ~TTM_WRITE_LOCK_PENDING;
			wakeup(lock);
			break;
		}
	}
	lockmgr(&lock->lock, LK_RELEASE);

	return (-ret);
}

void ttm_write_lock_downgrade(struct ttm_lock *lock)
{
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	lock->rw = 1;
	wakeup(lock);
	lockmgr(&lock->lock, LK_RELEASE);
}

static int __ttm_vt_unlock(struct ttm_lock *lock)
{
	int ret = 0;

	lockmgr(&lock->lock, LK_EXCLUSIVE);
	if (unlikely(!(lock->flags & TTM_VT_LOCK)))
		ret = -EINVAL;
	lock->flags &= ~TTM_VT_LOCK;
	wakeup(lock);
	lockmgr(&lock->lock, LK_RELEASE);

	return ret;
}

static void ttm_vt_lock_remove(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct ttm_lock *lock = container_of(base, struct ttm_lock, base);
	int ret;

	*p_base = NULL;
	ret = __ttm_vt_unlock(lock);
	KKASSERT(ret == 0);
}

static bool __ttm_vt_lock(struct ttm_lock *lock)
{
	bool locked = false;

	if (lock->rw == 0) {
		lock->flags &= ~TTM_VT_LOCK_PENDING;
		lock->flags |= TTM_VT_LOCK;
		locked = true;
	} else {
		lock->flags |= TTM_VT_LOCK_PENDING;
	}
	return locked;
}

int ttm_vt_lock(struct ttm_lock *lock,
		bool interruptible,
		struct ttm_object_file *tfile)
{
	const char *wmsg;
	int flags, ret;

	ret = 0;
	if (interruptible) {
		flags = PCATCH;
		wmsg = "ttmwi";
	} else {
		flags = 0;
		wmsg = "ttmw";
	}
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	while (!__ttm_vt_lock(lock)) {
		ret = msleep(lock, &lock->lock, flags, wmsg, 0);
		if (interruptible && ret != 0) {
			lock->flags &= ~TTM_VT_LOCK_PENDING;
			wakeup(lock);
			break;
		}
	}

	/*
	 * Add a base-object, the destructor of which will
	 * make sure the lock is released if the client dies
	 * while holding it.
	 */

	ret = ttm_base_object_init(tfile, &lock->base, false,
				   ttm_lock_type, &ttm_vt_lock_remove, NULL);
	if (ret)
		(void)__ttm_vt_unlock(lock);
	else
		lock->vt_holder = tfile;

	return (-ret);
}

int ttm_vt_unlock(struct ttm_lock *lock)
{
	return ttm_ref_object_base_unref(lock->vt_holder,
					 lock->base.hash.key, TTM_REF_USAGE);
}

void ttm_suspend_unlock(struct ttm_lock *lock)
{
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	lock->flags &= ~TTM_SUSPEND_LOCK;
	wakeup(lock);
	lockmgr(&lock->lock, LK_RELEASE);
}

static bool __ttm_suspend_lock(struct ttm_lock *lock)
{
	bool locked = false;

	if (lock->rw == 0) {
		lock->flags &= ~TTM_SUSPEND_LOCK_PENDING;
		lock->flags |= TTM_SUSPEND_LOCK;
		locked = true;
	} else {
		lock->flags |= TTM_SUSPEND_LOCK_PENDING;
	}
	return locked;
}

void ttm_suspend_lock(struct ttm_lock *lock)
{
	lockmgr(&lock->lock, LK_EXCLUSIVE);
	while (!__ttm_suspend_lock(lock))
		msleep(lock, &lock->lock, 0, "ttms", 0);
	lockmgr(&lock->lock, LK_RELEASE);
}
