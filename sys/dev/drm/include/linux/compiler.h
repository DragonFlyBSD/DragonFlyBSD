/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
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

#ifndef	_LINUX_COMPILER_H_
#define	_LINUX_COMPILER_H_

#include <sys/cdefs.h>

#define __user
#define __kernel
#define __safe
#define __force
#define __nocast
#define __iomem
#define __chk_user_ptr(x)		0
#define __chk_io_ptr(x)			0
#define __builtin_warning(x, y...)	(1)
#define __acquires(x)
#define __releases(x)
#define __acquire(x)			0
#define __release(x)			0
#define __cond_lock(x,c)		(c)
#define	__bitwise
#define __devinitdata
#define __init
#define	__devinit
#define	__devexit
#define __exit
#define	__stringify(x)			#x
#define	__attribute_const__		__attribute__((__const__))
#undef __always_inline
#define	__always_inline			inline
#define noinline			__attribute__((noinline))

#define	likely(x)			__builtin_expect(!!(x), 1)
#define	unlikely(x)			__builtin_expect(!!(x), 0)
#define typeof(x)			__typeof(x)

#define __maybe_unused			__unused
#define __always_unused			__unused
#define __malloc
#define __must_check			__heedresult

#define __printf(a,b)			__printf0like(a,b)

#define	barrier()			__asm__ __volatile__("": : :"memory")

#ifdef _KERNEL		/* This file is included by kdump(1) */

#include <sys/param.h>

#define READ_ONCE(x) ({							\
	typeof(x) __tmp = *(volatile typeof(x) *)&(x);			\
	__tmp;								\
})

#define WRITE_ONCE(x, val) ({						\
	typeof(x) __tmp = (val);					\
	*(volatile typeof(x) *)(uintptr_t)&(x) = __tmp;				\
	__tmp;								\
})

#define __rcu

/* Workaround to protect from the 'DEBUG' kernel config option */
#undef DEBUG

#endif	/* __KERNEL__ */

#define GCC_VERSION	\
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#ifndef unreachable
#define unreachable()			\
do {					\
	__asm __volatile("");		\
	__builtin_unreachable();	\
} while (0)
#endif

#endif	/* _LINUX_COMPILER_H_ */
