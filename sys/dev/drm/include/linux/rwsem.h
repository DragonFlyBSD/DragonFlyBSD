/*
 * Copyright (c) 2018-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
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

#ifndef _LINUX_RWSEM_H_
#define _LINUX_RWSEM_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#include <sys/lock.h>

#define down_read(semaphore)	lockmgr((semaphore), LK_SHARED)
#define up_read(semaphore)	lockmgr((semaphore), LK_RELEASE)

#define down_write(semaphore)	lockmgr((semaphore), LK_EXCLUSIVE)
#define up_write(semaphore)	lockmgr((semaphore), LK_RELEASE)

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static inline int
down_read_trylock(struct lock *sem) {
	return !lockmgr(sem, LK_EXCLUSIVE|LK_NOWAIT);
}

static inline int
down_write_killable(struct lock *sem)
{
	if (lockmgr(sem, LK_EXCLUSIVE|LK_SLEEPFAIL|LK_PCATCH))
		return -EINTR;

	return 0;
}

#endif	/* _LINUX_RWSEM_H_ */
