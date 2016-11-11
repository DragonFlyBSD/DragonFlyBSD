/*
 * Copyright (c) 2015-2016 François Tigeot
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

#ifndef _LINUX_TIMEKEEPING_H_
#define _LINUX_TIMEKEEPING_H_

static inline u64 ktime_get_raw_ns(void)
{
	struct timespec ts;

	nanouptime(&ts);

	return (ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
}

static inline ktime_t ktime_mono_to_real(ktime_t mono)
{
	return mono;
}

static inline ktime_t ktime_get_real(void)
{
	ktime_t kt;

	kt.tv64 = ktime_get_raw_ns();
	return kt;
}

/* Include time spent in suspend state */
static inline ktime_t
ktime_get_boottime(void)
{
	return ktime_get_real();
}

static inline s64
ktime_ms_delta(const ktime_t later, const ktime_t earlier)
{
	return (later.tv64 - earlier.tv64) / NSEC_PER_MSEC;
}

#endif	/* _LINUX_TIMEKEEPING_H_ */
