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

#define __printf(a,b)			__printflike(a,b)


#define barrier()	cpu_ccfence()

#define ACCESS_ONCE(x)	(*(volatile typeof(x) *)&(x))

#ifdef _KERNEL		/* This file is included by kdump(1) */

#include <sys/param.h>

/*
 * The READ_ONCE() and WRITE_ONCE() macros force volatile accesses to
 * various data types.
 * Their complexity is caused by the necessity to work-around
 * compiler cleverness and bugs.
 * Some GCC versions drop the volatile modifier if the variable used
 * is not of a scalar type.
 */
static inline void
__volatile_read(const volatile void *x, int size, void *result)
{
	switch(size) {
	case 8:
		*(uint64_t *)result = *(const volatile uint64_t *)x;
		break;
	case 4:
		*(uint32_t *)result = *(const volatile uint32_t *)x;
		break;
	case 2:
		*(uint16_t *)result = *(const volatile uint16_t *)x;
		break;
	case 1:
		*(uint8_t *)result = *(const volatile uint8_t *)x;
		break;
	default:
		panic("__volatile_read called with size %d\n", size);
	}
}

static inline void
__volatile_write(volatile void *var, int size, void *value)
{
	switch(size) {
	case 8:
		*(volatile uint64_t *)var = *(uint64_t *)value;
		break;
	case 4:
		*(volatile uint32_t *)var = *(uint32_t *)value;
		break;
	case 2:
		*(volatile uint16_t *)var = *(uint16_t *)value;
		break;
	case 1:
		*(volatile uint8_t *)var = *(uint8_t *)value;
		break;
	default:
		panic("__volatile_write called with size %d\n", size);
	}

}

#define READ_ONCE(x) ({						\
	union {							\
		__typeof(x) initial_type;			\
		uint8_t nc_type;				\
	} result;						\
								\
	result.nc_type = 0;					\
	__volatile_read(&(x), sizeof(x), &result.nc_type);	\
	result.initial_type;					\
})

#define WRITE_ONCE(var, value) ({				\
	union {							\
		__typeof(var) initial_type;			\
		uint8_t nc_type;				\
	} result;						\
								\
	result.initial_type = value;				\
	__volatile_write(&(var), sizeof(var), &result.nc_type);	\
	result.initial_type;					\
})

#define __rcu

#endif	/* __KERNEL__ */

#define GCC_VERSION	\
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#endif	/* _LINUX_COMPILER_H_ */
