/*
 * Copyright (c) 2015-2019 François Tigeot <ftigeot@wolfpond.org>
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
typedef s64	ktime_t;

static inline ktime_t
ktime_set(const s64 secs, const unsigned long nsecs)
{
	return (secs * NSEC_PER_SEC) + (s64)nsecs;
}

static inline s64
ktime_to_us(const ktime_t kt)
{
	return kt / NSEC_PER_USEC;
}

static inline s64
ktime_us_delta(const ktime_t later, const ktime_t earlier)
{
	return later - earlier;
}

static inline int64_t ktime_to_ns(ktime_t kt)
{
	return kt;
}

static inline struct timeval ktime_to_timeval(ktime_t kt)
{
	return ns_to_timeval(kt);
}

static inline ktime_t ktime_add_ns(ktime_t kt, int64_t ns)
{
	return kt + ns;
}

static inline ktime_t ktime_sub_ns(ktime_t kt, int64_t ns)
{
	return kt - ns;
}

static inline ktime_t ktime_get(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
}

static inline ktime_t
ktime_sub(ktime_t lhs, ktime_t rhs)
{
	return lhs - rhs;
}

static inline ktime_t
ns_to_ktime(u64 ns)
{
	return ns;
}

#include <linux/timekeeping.h>

#endif	/* _LINUX_KTIME_H_ */
