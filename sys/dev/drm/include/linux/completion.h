/*
 * Copyright (c) 2014 François Tigeot
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
#ifndef	_LINUX_COMPLETION_H_
#define	_LINUX_COMPLETION_H_

#include <linux/wait.h>

struct completion {
	unsigned int done;
	wait_queue_head_t wait;
};

static inline void
init_completion(struct completion *c)
{
	c->done = 0;
	init_waitqueue_head(&c->wait);
}

#define	INIT_COMPLETION(c)	(c.done = 0)

/*
 * Completion interlock and wakeup.  Be careful not to execute the wakeup
 * from inside the spinlock as this can deadlock if the IPIQ fifo is full.
 * (also note that wakeup() is asynchronous anyway, so no point doing that).
 */
static inline void
complete(struct completion *c)
{
	spin_lock(&c->wait.lock);
	c->done++;
	spin_unlock(&c->wait.lock);
	wakeup_one(&c->wait);
}

static inline void
complete_all(struct completion *c)
{
	spin_lock(&c->wait.lock);
	c->done++;
	spin_unlock(&c->wait.lock);
	wakeup(&c->wait);
}

static inline long
wait_for_completion_interruptible_timeout(struct completion *x,
		unsigned long timeout)
{
	int start_jiffies, elapsed_jiffies, remaining_jiffies;
	bool timeout_expired = false, awakened = false;
	long ret = 1;

	start_jiffies = ticks;

	spin_lock(&x->wait.lock);
	while (x->done == 0 && !timeout_expired) {
		ret = ssleep(&x->wait, &x->wait.lock, PCATCH, "wfcit", timeout);
		switch(ret) {
		case EWOULDBLOCK:
			timeout_expired = true;
			ret = 0;
			break;
		case ERESTART:
			ret = -ERESTARTSYS;
			break;
		case 0:
			awakened = true;
			break;
		}
	}
	spin_unlock(&x->wait.lock);

	if (awakened) {
		elapsed_jiffies = ticks - start_jiffies;
		remaining_jiffies = timeout - elapsed_jiffies;
		if (remaining_jiffies > 0)
			ret = remaining_jiffies;
	}

	return ret;
}

#endif	/* _LINUX_COMPLETION_H_ */
