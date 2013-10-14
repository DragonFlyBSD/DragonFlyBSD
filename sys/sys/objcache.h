/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_OBJCACHE_H_
#define _SYS_OBJCACHE_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif

#define OC_MFLAGS	0x0000ffff	/* same as malloc flags */

typedef __boolean_t (objcache_ctor_fn)(void *obj, void *privdata, int ocflags);
typedef void (objcache_dtor_fn)(void *obj, void *privdata);

/*
 * Underlying allocator.
 */
typedef void *(objcache_alloc_fn)(void *allocator_args, int ocflags);
typedef void (objcache_free_fn)(void *obj, void *allocator_args);

#ifdef	_KERNEL

struct objcache;

struct objcache
	*objcache_create(const char *name, int cluster_limit, int nom_cache,
			 objcache_ctor_fn *ctor, objcache_dtor_fn *dtor,
			 void *privdata,
			 objcache_alloc_fn *alloc, objcache_free_fn *free,
			 void *allocator_args);
struct objcache
	*objcache_create_simple(malloc_type_t mtype, size_t objsize);
struct objcache
	*objcache_create_mbacked(malloc_type_t mtype, size_t objsize,
			  int cluster_limit, int nom_cache,
			  objcache_ctor_fn *ctor, objcache_dtor_fn *dtor,
			  void *privdata);
void	*objcache_get(struct objcache *oc, int ocflags);
void	 objcache_put(struct objcache *oc, void *obj);
void	 objcache_dtor(struct objcache *oc, void *obj);
void	 objcache_populate_linear(struct objcache *oc, void *elts, int nelts,
				  int size);
__boolean_t objcache_reclaimlist(struct objcache *oc[], int nlist, int ocflags);
void	 objcache_destroy(struct objcache *oc);

#endif

/*
 * Common underlying allocators.
 */
struct objcache_malloc_args {
	size_t		objsize;
	malloc_type_t	mtype;
};

#ifdef	_KERNEL

void	*objcache_malloc_alloc(void *allocator_args, int ocflags);
void	 objcache_malloc_free(void *obj, void *allocator_args);

void	*objcache_nop_alloc(void *allocator_args, int ocflags);
void	 objcache_nop_free(void *obj, void *allocator_args);

#endif	/* _KERNEL */

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_OBJCACHE_H_ */
