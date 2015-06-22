/*
 * Copyright (c) 2015 François Tigeot
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

#ifndef _LINUX_KTIME_H_
#define _LINUX_KTIME_H_

#include <linux/time.h>
#include <linux/jiffies.h>

/* time values in nanoseconds */
union ktime {
	int64_t tv64;
};

typedef union ktime ktime_t;

static inline int64_t ktime_to_ns(ktime_t kt)
{
	return kt.tv64;
}

static inline struct timeval ktime_to_timeval(ktime_t kt)
{
	return ns_to_timeval(kt.tv64);
}

static inline ktime_t ktime_add_ns(ktime_t kt, int64_t ns)
{
	ktime_t res;

	res.tv64 = kt.tv64 + ns;
	return kt;
}

static inline ktime_t ktime_sub_ns(ktime_t kt, int64_t ns)
{
	ktime_t res;

	res.tv64 = kt.tv64 - ns;
	return kt;
}

#define NSEC_PER_SEC	1000000000L

static inline ktime_t ktime_get(void)
{
	struct timespec ts;
	ktime_t kt;

	nanouptime(&ts);
	kt.tv64 = (ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
	return kt;
}

#endif	/* _LINUX_KTIME_H_ */
