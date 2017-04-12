/*-
 * Copyright (c) 2015-2017 François Tigeot
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

#ifndef	_LINUX_SCHED_H_
#define	_LINUX_SCHED_H_

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/errno.h>

#include <asm/page.h>

#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/rculist.h>

#include <linux/time.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/gfp.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal2.h>

#define	TASK_RUNNING		0
#define	TASK_INTERRUPTIBLE	1
#define	TASK_UNINTERRUPTIBLE	2

/*
 * schedule_timeout - sleep until timeout
 * @timeout: timeout value in jiffies
 */
static inline long
schedule_timeout(signed long timeout)
{
	static int dummy;

	if (timeout < 0) {
		kprintf("schedule_timeout(): timeout cannot be negative\n");
		return 0;
	}

	tsleep(&dummy, 0, "lstim", timeout);

	return 0;
}

#define TASK_COMM_LEN	MAXCOMLEN

#define signal_pending(lp)	CURSIG(lp)

/*
 * local_clock: fast time source, monotonic on the same cpu
 */
static inline uint64_t
local_clock(void)
{
	struct timespec ts;

	getnanouptime(&ts);
	return (ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
}

#endif	/* _LINUX_SCHED_H_ */
