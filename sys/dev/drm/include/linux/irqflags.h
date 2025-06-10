/*
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_IRQFLAGS_H_
#define _LINUX_IRQFLAGS_H_

#include <linux/typecheck.h>
#include <sys/thread2.h>

static inline void
local_irq_disable(void)
{
	crit_enter();
}

static inline void
local_irq_enable(void)
{
	crit_exit();
}

static inline bool
irqs_disabled(void)
{
	/* dillon: I don't like disabling interrupts.
	 * The reason we avoid actually hard-disabling interrupts is
	 * because if something goes wrong we have almost no chance of
	 * getting a crash dump or for the system to be able to operate while
	 * drm is stuck / blocked on something.
	 * XXX: better to use crit_enter/crit_exit
	 */
	return IN_CRITICAL_SECT(curthread);
}

#define local_irq_save(flags)	\
({				\
	flags = read_rflags();	\
	local_irq_disable();	\
})

static inline void
local_irq_restore(unsigned long flags)
{
	write_rflags(flags);
	local_irq_enable();
}

#endif	/* _LINUX_IRQFLAGS_H_ */
