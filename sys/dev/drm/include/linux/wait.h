/*
 * Copyright (c) 2014-2015 François Tigeot
 * Copyright (c) 2014 Imre Vadász
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

#ifndef _LINUX_WAIT_H_
#define _LINUX_WAIT_H_

#include <sys/spinlock2.h>
#include <sys/param.h>

typedef struct {
} wait_queue_t;

typedef struct {
	struct lock	lock;
} wait_queue_head_t;

static inline void
init_waitqueue_head(wait_queue_head_t *eq)
{
	lockinit(&eq->lock, "lwq", 0, LK_CANRECURSE);
}

#define wake_up(eq)		wakeup_one(eq)
#define wake_up_all(eq)		wakeup(eq)

/*
 * wait_event_interruptible_timeout:
 * - The process is put to sleep until the condition evaluates to true.
 * - The condition is checked each time the waitqueue wq is woken up.
 * - wake_up has to be called after changing any variable that could change
 * the result of the wait condition.
 *
 * returns:
 *   - 0 if the timeout elapsed
 *   - the remaining jiffies if the condition evaluated to true before
 *   the timeout elapsed.
 *   - remaining jiffies are always at least 1
 *   - -ERESTARTSYS if interrupted by a signal (when PCATCH is set in flags)
*/
#define __wait_event_common(wq, condition, timeout_jiffies, flags)	\
({									\
	int start_jiffies, elapsed_jiffies, remaining_jiffies, ret;	\
	bool timeout_expired = false;					\
	bool interrupted = false;					\
	long retval;							\
									\
	start_jiffies = ticks;						\
									\
	lockmgr(&wq.lock, LK_EXCLUSIVE);				\
	while (1) {							\
		if (condition)						\
			break;						\
									\
		ret = lksleep(&wq, &wq.lock, flags,			\
					"lwe", timeout_jiffies);	\
		if (ret == EINTR || ret == ERESTART) {			\
			interrupted = true;				\
			break;						\
		}							\
		if (ret == EWOULDBLOCK) {				\
			timeout_expired = true;				\
			break;						\
		}							\
	}								\
	lockmgr(&wq.lock, LK_RELEASE);					\
									\
	elapsed_jiffies = ticks - start_jiffies;			\
	remaining_jiffies = timeout_jiffies - elapsed_jiffies;		\
	if (remaining_jiffies <= 0)					\
		remaining_jiffies = 1;					\
									\
	if (timeout_expired)						\
		retval = 0;						\
	else if (interrupted)						\
		retval = -ERESTARTSYS;					\
	else if (timeout_jiffies > 0)					\
		retval = remaining_jiffies;				\
	else								\
		retval = 1;						\
									\
	retval;								\
})

#define wait_event(wq, condition)					\
		__wait_event_common(wq, condition, 0, 0)

#define wait_event_timeout(wq, condition, timeout)			\
		__wait_event_common(wq, condition, timeout, 0)

#define wait_event_interruptible(wq, condition)				\
({									\
	long retval;							\
									\
	retval = __wait_event_common(wq, condition, 0, PCATCH);		\
	if (retval != -ERESTARTSYS)					\
		retval = 0;						\
	retval;								\
})

#define wait_event_interruptible_timeout(wq, condition, timeout)	\
		__wait_event_common(wq, condition, timeout, PCATCH)

static inline int
waitqueue_active(wait_queue_head_t *q)
{
	return 0;	/* XXX: not really implemented */
}

#define DEFINE_WAIT(name)	\
	wait_queue_t name = {}

static inline void
prepare_to_wait(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
}

static inline void
finish_wait(wait_queue_head_t *q, wait_queue_t *wait)
{
}

#endif	/* _LINUX_WAIT_H_ */
