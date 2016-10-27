/*
 * Copyright (c) 2016 François Tigeot
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

#ifndef _LINUX_PWM_H_
#define _LINUX_PWM_H_

#include <linux/device.h>

struct pwm_device;

static inline int
pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	kprintf("Stub: %s\n", __FUNCTION__);
	return -EINVAL;
}

static inline unsigned int
pwm_get_duty_cycle(const struct pwm_device *pwm)
{
	kprintf("Stub: %s\n", __FUNCTION__);
	return 0;
}

static inline void
pwm_set_duty_cycle(struct pwm_device *pwm, unsigned int duty)
{
	kprintf("Stub: %s\n", __FUNCTION__);
}

static inline int
pwm_enable(struct pwm_device *pwm)
{
	kprintf("Stub: %s\n", __FUNCTION__);
	return -EINVAL;
}

static inline void
pwm_disable(struct pwm_device *pwm)
{
	kprintf("Stub: %s\n", __FUNCTION__);
}

static inline struct pwm_device *
pwm_get(struct device *dev, const char *consumer)
{
	kprintf("Stub: %s\n", __FUNCTION__);
	return NULL;
}

static inline void
pwm_put(struct pwm_device *pwm)
{
	kprintf("Stub: %s\n", __FUNCTION__);
}

#endif	/* _LINUX_PWM_H_ */
