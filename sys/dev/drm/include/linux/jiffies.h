/*
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_JIFFIES_H_
#define _LINUX_JIFFIES_H_

#include <sys/kernel.h>
#include <machine/limits.h>

#include <linux/math64.h>
#include <linux/time.h>

#define HZ			hz

#define MAX_JIFFY_OFFSET	((LONG_MAX >> 1) - 1)

#define jiffies_to_msecs(x)	(((int64_t)(x)) * 1000 / hz)
#define msecs_to_jiffies(x)	(((int64_t)(x)) * hz / 1000)
#define jiffies			ticks
#define jiffies_64		ticks /* XXX hmmm */
#define time_after(a,b)		((long)(b) - (long)(a) < 0)
#define time_after32(a,b)       ((int32_t)((uint32_t)(b) - (uint32_t)(a)) < 0)
#define time_after_eq(a,b)	((long)(b) - (long)(a) <= 0)

#define time_before(a,b)	time_after(b,a)
#define time_before_eq(a,b)	time_after_eq(b,a)

#define time_in_range(a,b,c)	\
	time_after_eq(a,b) && time_before_eq(a,c)

static inline unsigned long
timespec_to_jiffies(const struct timespec *ts)
{
	unsigned long result;

	result = ((unsigned long)hz * ts->tv_sec) + (ts->tv_nsec / NSEC_PER_SEC);
	if (result > LONG_MAX)
		result = LONG_MAX;

	return result;
}

static inline
unsigned long usecs_to_jiffies(const unsigned int u)
{
	unsigned long jiffies;

	jiffies = (u * hz) / 1000000;
	if (jiffies < 1)
		return 1;
	else
		return jiffies;
}

static inline u64 get_jiffies_64(void)
{
	return (u64)ticks;
}

static inline u64 nsecs_to_jiffies(u64 n)
{
	return (n * hz) / NSEC_PER_SEC;
}

static inline u64 nsecs_to_jiffies64(u64 n)
{
	return nsecs_to_jiffies(n);
}

static inline
unsigned int jiffies_to_usecs(const unsigned long j)
{
	return j * (USEC_PER_SEC / HZ);
}

static inline u64
jiffies_to_nsecs(const unsigned long j)
{
	return j * (NSEC_PER_SEC / HZ);
}

#endif	/* _LINUX_JIFFIES_H_ */
