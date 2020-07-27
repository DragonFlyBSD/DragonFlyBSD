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

#ifndef _LINUX_SLAB_H_
#define _LINUX_SLAB_H_

#include <linux/gfp.h>
#include <linux/types.h>

MALLOC_DECLARE(M_DRM);

#define kzalloc(size, flags)	kmalloc(size, M_DRM, flags | M_ZERO)

#define kfree(ptr)	do {			\
	if (ptr != NULL)			\
		kfree((void *)ptr, M_DRM);	\
} while (0)

#define kcalloc(n, size, flags)	kzalloc((n) * (size), flags)

static inline void *
kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	return kmalloc(n * size, M_DRM, flags);
}

#include <linux/kasan.h>

struct kmem_cache {
	unsigned int size;
};

#define SLAB_HWCACHE_ALIGN	0x01UL
#define SLAB_RECLAIM_ACCOUNT	0x02UL
#define SLAB_TYPESAFE_BY_RCU	0x04UL

#define KMEM_CACHE(__struct, flags)				\
	kmem_cache_create(#__struct, sizeof(struct __struct),	\
	__alignof(struct __struct), (flags), NULL)

static inline struct kmem_cache *
kmem_cache_create(const char *name, size_t size, size_t align,
		  unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *kc;

	kc = kzalloc(sizeof(struct kmem_cache), flags);
	kc->size = size;

	return kc;
}

static inline void
kmem_cache_destroy(struct kmem_cache *kc)
{
	kfree(kc);
}

static inline void *
kmem_cache_alloc(struct kmem_cache *kc, gfp_t flags)
{
	return kmalloc(kc->size, M_DRM, flags);
}

static inline void *
kmem_cache_zalloc(struct kmem_cache *kc, gfp_t flags)
{
	return kzalloc(kc->size, flags);
}

static inline void
kmem_cache_free(struct kmem_cache *kc, void *ptr)
{
	kfree(ptr);
}

#endif	/* _LINUX_SLAB_H_ */
