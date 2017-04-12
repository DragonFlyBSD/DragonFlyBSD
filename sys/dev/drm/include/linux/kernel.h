/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
 * Copyright (c) 2014-2017 François Tigeot
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
#ifndef	_LINUX_KERNEL_H_
#define	_LINUX_KERNEL_H_

#include <sys/systm.h>
#include <sys/param.h>
#include <sys/libkern.h>
#include <sys/stat.h>
#include <sys/endian.h>

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/log2.h>
#include <linux/types.h>
#include <linux/printk.h>

#define U64_MAX		((u64)~0ULL)

#undef	ALIGN
#define	ALIGN(x, y)		roundup2((x), (y))
#define	IS_ALIGNED(x, y)	(((x) & ((y) - 1)) == 0)
#define	DIV_ROUND_UP		howmany
#define DIV_ROUND_UP_ULL(X, N)	DIV_ROUND_UP((unsigned long long)(X), (N))

#define udelay(t)       	DELAY(t)

#define container_of(ptr, type, member)				\
({								\
	__typeof(((type *)0)->member) *_p = (ptr);		\
	(type *)((char *)_p - offsetof(type, member));		\
})
  
#define	ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

#define	simple_strtoul	strtoul
#define	simple_strtol	strtol

#define min(x, y)			((x) < (y) ? (x) : (y))
#define max(x, y)			((x) > (y) ? (x) : (y))

#define min3(a, b, c)			min(a, min(b,c))
#define max3(a, b, c)			max(a, max(b,c))

#define min_t(type, _x, _y)		((type)(_x) < (type)(_y) ? (type)(_x) : (type)(_y))
#define max_t(type, _x, _y)		((type)(_x) > (type)(_y) ? (type)(_x) : (type)(_y))

#define clamp_t(type, _x, min, max)	min_t(type, max_t(type, _x, min), max)
#define clamp(x, lo, hi)		min( max(x,lo), hi)
#define clamp_val(val, lo, hi)		clamp_t(typeof(val), val, lo, hi)

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define	num_possible_cpus()	mp_ncpus

typedef struct pm_message {
        int event;
} pm_message_t;

/* Swap values of a and b */
#define swap(a, b)			\
({					\
	typeof(a) _swap_tmp = a;	\
	a = b;				\
	b = _swap_tmp;			\
})

#define DIV_ROUND_CLOSEST(x, divisor)	(((x) + ((divisor) /2)) / (divisor))

static inline uintmax_t
mult_frac(uintmax_t x, uintmax_t multiplier, uintmax_t divisor)
{
	uintmax_t q = (x / divisor);
	uintmax_t r = (x % divisor);

	return ((q * multiplier) + ((r * multiplier) / divisor));
}

static inline int64_t abs64(int64_t x)
{
	return (x < 0 ? -x : x);
}

#define DIV_ROUND_CLOSEST_ULL(ll, d)	\
 ({ unsigned long long _tmp = (ll)+(d)/2; do_div(_tmp, d); _tmp; })

#define	upper_32_bits(n)	((u32)(((n) >> 16) >> 16))
#define	lower_32_bits(n)	((u32)(n))

/* Byteorder compat layer */
#if _BYTE_ORDER == _BIG_ENDIAN
#define	__BIG_ENDIAN 4321
#else
#define	__LITTLE_ENDIAN 1234
#endif

#define	cpu_to_le16(x)	htole16(x)
#define	le16_to_cpu(x)	le16toh(x)
#define	cpu_to_le32(x)	htole32(x)
#define	le32_to_cpu(x)	le32toh(x)
#define	le32_to_cpup(x)	le32toh(*x)

#define	cpu_to_be16(x)	htobe16(x)
#define	be16_to_cpu(x)	be16toh(x)
#define	cpu_to_be32(x)	htobe32(x)
#define	be32_to_cpu(x)	be32toh(x)
#define	be32_to_cpup(x)	be32toh(*x)

static inline int __must_check
kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	*(res) = strtol(s,0,base);

	return 0;
}

#define kvasprintf drm_vasprintf
#define kasprintf drm_asprintf

static inline void __user *
u64_to_user_ptr(u64 address)
{
	return (void __user *)(uintptr_t)address;
}

#endif	/* _LINUX_KERNEL_H_ */
