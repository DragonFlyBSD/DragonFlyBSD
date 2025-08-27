/*
 * Copyright (c) 1987, 1993, 2021
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)malloc.h	8.5 (Berkeley) 5/3/95
 * $FreeBSD: src/sys/sys/malloc.h,v 1.48.2.2 2002/03/16 02:19:16 archie Exp $
 */

#ifndef _SYS_MALLOC_H_
#define	_SYS_MALLOC_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>	/* vm_paddr_t and __* types */
#endif

/*
 * flags to malloc.
 */
#define	M_RNOWAIT	0x0001	/* do not block */
#define	M_WAITOK	0x0002	/* wait for resources / alloc from cache */
#define	M_ZERO		0x0100	/* bzero() the allocation */
#define	M_USE_RESERVE	0x0200	/* can eat into free list reserve */
#define	M_NULLOK	0x0400	/* ok to return NULL */
#define	M_PASSIVE_ZERO	0x0800	/* (internal to the slab code only) */
#define	M_USE_INTERRUPT_RESERVE \
			0x1000	/* can exhaust free list entirely */
#define	M_POWEROF2	0x2000	/* roundup size to the nearest power of 2 */
#define	M_CACHEALIGN	0x4000	/* force CPU cache line alignment */
/* GFP_DMA32 0x10000 reserved for drm layer (not handled by kmalloc) */

/*
 * M_NOWAIT has to be a set of flags for equivalence to prior use.
 *
 * M_SYSALLOC should be used for any critical infrastructure allocations
 * made by the kernel proper.
 *
 * M_INTNOWAIT should be used for any critical infrastructure allocations
 * made by interrupts.  Such allocations can still fail but will not fail
 * as often as M_NOWAIT.
 *
 * NOTE ON DRAGONFLY USE OF M_NOWAIT.  In FreeBSD M_NOWAIT allocations
 * almost always succeed.  In DragonFly, however, there is a good chance
 * that an allocation will fail.  M_NOWAIT should only be used when
 * allocations can fail without any serious detriment to the system.
 *
 * Note that allocations made from (preempted) interrupts will attempt to
 * use pages from the VM PAGE CACHE (PQ_CACHE) (i.e. those associated with
 * objects).  This is automatic.
 */

#define	M_INTNOWAIT	(M_RNOWAIT | M_NULLOK | 			\
			 M_USE_RESERVE | M_USE_INTERRUPT_RESERVE)
#define	M_SYSNOWAIT	(M_RNOWAIT | M_NULLOK | M_USE_RESERVE)
#define	M_INTWAIT	(M_WAITOK | M_USE_RESERVE | M_USE_INTERRUPT_RESERVE)
#define	M_SYSWAIT	(M_WAITOK | M_USE_RESERVE)

#define	M_NOWAIT	(M_RNOWAIT | M_NULLOK | M_USE_RESERVE)
#define	M_SYSALLOC	M_SYSWAIT

#define	M_MAGIC		877983977	/* time when first defined :-) */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#include <sys/_malloc.h>		/* struct malloc_type */
#ifndef NULL
#include <sys/_null.h>			/* ensure NULL is defined */
#endif
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define	MALLOC_DEFINE(type, shortdesc, longdesc)			\
	struct malloc_type type[1] = {					\
	    { NULL, 0, 0, 0, 0, M_MAGIC, shortdesc, 0,			\
	      &type[0].ks_use0, { 0, 0, 0, 0 } }			\
	};								\
	SYSINIT(type##_init, SI_BOOT1_KMALLOC, SI_ORDER_ANY,		\
	    malloc_init, type);						\
	SYSUNINIT(type##_uninit, SI_BOOT1_KMALLOC, SI_ORDER_ANY,	\
	    malloc_uninit, type)

#define	MALLOC_DEFINE_OBJ(type, size, shortdesc, longdesc)		\
	struct malloc_type type##_obj[1] = {				\
	    { NULL, 0, 0, 0, KSF_OBJSIZE, M_MAGIC, shortdesc,		\
	      __VM_CACHELINE_ALIGN((size)),				\
	      &type##_obj[0].ks_use0, { 0, 0, 0, 0 } }			\
	};								\
	SYSINIT(type##_init, SI_BOOT1_KMALLOC, SI_ORDER_ANY,		\
	    malloc_init, type##_obj);					\
	SYSUNINIT(type##_uninit, SI_BOOT1_KMALLOC, SI_ORDER_ANY,	\
	    malloc_uninit, type##_obj)

#else
#define	MALLOC_DEFINE(type, shortdesc, longdesc)			\
	struct malloc_type type[1] = {					\
	    { NULL, 0, 0, 0, 0, M_MAGIC, shortdesc, 0,			\
	      &type[0].ks_use0, { 0, 0, 0, 0 } 				\
	}

#define	MALLOC_DEFINE_OBJ(type, size, shortdesc, longdesc)		\
	struct malloc_type type##_obj[1] = {				\
	    { NULL, 0, 0, 0, KSF_OBJSIZE, M_MAGIC, shortdesc,		\
	      __VM_CACHELINE_ALIGN((size)),				\
	      &type##_obj[0].ks_use0, { 0, 0, 0, 0 } 			\
	}
#endif

#ifdef _KERNEL

MALLOC_DECLARE(M_CACHE);
MALLOC_DECLARE(M_DEVBUF);
MALLOC_DECLARE(M_TEMP);
MALLOC_DECLARE(M_FPUCTX);

MALLOC_DECLARE(M_IP6OPT); /* for INET6 */
MALLOC_DECLARE(M_IP6NDP); /* for INET6 */

#endif /* _KERNEL */

#ifdef _KERNEL

#define	MINALLOCSIZE	sizeof(void *)

struct globaldata;

/* XXX struct malloc_type is unused for contig*(). */
size_t  kmem_lim_size(void);
void	*kmem_slab_alloc(vm_size_t bytes, vm_offset_t align, int flags);
void	kmem_slab_free(void *ptr, vm_size_t bytes);

void	contigfree(void *addr, unsigned long size, struct malloc_type *type)
	    __nonnull(1);
void	*contigmalloc(unsigned long size, struct malloc_type *type, int flags,
		      vm_paddr_t low, vm_paddr_t high, unsigned long alignment,
		      unsigned long boundary) __malloclike __heedresult
		      __alloc_size(1) __alloc_align(6);
void	malloc_init(void *);
void	malloc_uninit(void *);
void	malloc_kmemstats_poll(void);
void	malloc_mgt_init(struct malloc_type *type, struct kmalloc_mgt *mgt,
			size_t bytes);
void	malloc_mgt_uninit(struct malloc_type *type, struct kmalloc_mgt *mgt);
void	malloc_mgt_relocate(struct kmalloc_mgt *smgt, struct kmalloc_mgt *dmgt);
int	malloc_mgt_poll(struct malloc_type *type);
void	malloc_reinit_ncpus(void);
void	kmalloc_raise_limit(struct malloc_type *type, size_t bytes);
void	kmalloc_set_unlimited(struct malloc_type *type);
void	kmalloc_create(struct malloc_type **typep, const char *descr);
void	kmalloc_destroy(struct malloc_type **typep);

/*
 * NOTE: kmalloc_obj*() functiions use distinct malloc_type structures
 *	 which should not be mixed with non-obj functions.  For this reason,
 *	 all kmalloc_obj*() functions postpend the '_obj' to the variable
 *	 name passed into them.  This guarantees that a programmer mistake
 *	 will cause the compile to fail.
 */
void	_kmalloc_create_obj(struct malloc_type **typep, const char *descr,
			size_t objsize);
#define kmalloc_create_obj(typep, descr, objsize)	\
		_kmalloc_create_obj((typep##_obj), (descr), (objsize))
#define kmalloc_destroy_obj(type) kmalloc_destroy((type##_obj))

/*
 * Debug and non-debug kmalloc() prototypes.
 *
 * The kmalloc() macro allows M_ZERO to be optimized external to
 * the kmalloc() function.  When combined with the use a builtin
 * for bzero() this can get rid of a considerable amount of overhead
 * for M_ZERO based kmalloc() calls.
 */
#ifdef SLAB_DEBUG
void	*_kmalloc_debug(unsigned long size, struct malloc_type *type,
			int flags, const char *file, int line)
			__malloclike __heedresult __alloc_size(1);
void	*_kmalloc_obj_debug(unsigned long size, struct malloc_type *type,
			int flags, const char *file, int line)
			__malloclike __heedresult __alloc_size(1);
void	*krealloc_debug(void *addr, unsigned long size,
			struct malloc_type *type, int flags,
			const char *file, int line) __heedresult __alloc_size(2);
char	*kstrdup_debug(const char *, struct malloc_type *,
			const char *file, int line) __malloclike __heedresult;
char	*kstrndup_debug(const char *, size_t maxlen, struct malloc_type *,
			const char *file, int line) __malloclike __heedresult;

#define __kmalloc(size, type, flags) ({					\
	void *_malloc_item;						\
	size_t _size = (size);						\
									\
	if (__builtin_constant_p(size) &&				\
	    __builtin_constant_p(flags) &&				\
	    ((flags) & M_ZERO)) {					\
		_malloc_item = _kmalloc_debug(_size, type,		\
					    (flags) & ~M_ZERO,		\
					    __FILE__, __LINE__);	\
		if (((flags) & (M_WAITOK|M_NULLOK)) == M_WAITOK ||	\
		    __predict_true(_malloc_item != NULL)) {		\
			__builtin_memset(_malloc_item, 0, _size);	\
		}							\
	} else {							\
	    _malloc_item = _kmalloc_debug(_size, type, flags,		\
				   __FILE__, __LINE__);			\
	}								\
	_malloc_item;							\
})

#define __kmalloc_obj(size, type, flags) ({				\
	void *_malloc_item;						\
	size_t _size = __VM_CACHELINE_ALIGN(size);			\
									\
	if (__builtin_constant_p(size) &&				\
	    __builtin_constant_p(flags) &&				\
	    ((flags) & M_ZERO)) {					\
		_malloc_item = _kmalloc_obj_debug(_size, type,		\
					    (flags) & ~M_ZERO,		\
					    __FILE__, __LINE__);	\
		if (((flags) & (M_WAITOK|M_NULLOK)) == M_WAITOK ||	\
		    __predict_true(_malloc_item != NULL)) {		\
			__builtin_memset(_malloc_item, 0, _size);	\
		}							\
	} else {							\
	    _malloc_item = _kmalloc_obj_debug(_size, type, flags,	\
				   __FILE__, __LINE__);			\
	}								\
	_malloc_item;							\
})

#define kmalloc(size, type, flags)	__kmalloc(size, type, flags)
#define kmalloc_obj(size, type, flags)	__kmalloc_obj(size, type##_obj, flags)

/*
 * These only operate on normal mixed-size zones
 */
#define krealloc(addr, size, type, flags)	\
	krealloc_debug(addr, size, type, flags, __FILE__, __LINE__)
#define kstrdup(str, type)			\
	kstrdup_debug(str, type, __FILE__, __LINE__)
#define kstrndup(str, maxlen, type)			\
	kstrndup_debug(str, maxlen, type, __FILE__, __LINE__)

#else	/* !SLAB_DEBUG */

void	*_kmalloc(unsigned long size, struct malloc_type *type, int flags)
		 __malloclike __heedresult __alloc_size(1);
void	*_kmalloc_obj(unsigned long size, struct malloc_type *type, int flags)
		 __malloclike __heedresult __alloc_size(1);

static __always_inline void *
__kmalloc(size_t _size, struct malloc_type *_type, int _flags)
{
	if (__builtin_constant_p(_size) && __builtin_constant_p(_flags) &&
	    (_flags & M_ZERO)) {
		void *_malloc_item;
		_malloc_item = _kmalloc(_size, _type, _flags & ~M_ZERO);
		if ((_flags & (M_WAITOK|M_NULLOK)) == M_WAITOK ||
		    __predict_true(_malloc_item != NULL)) {
			__builtin_memset(_malloc_item, 0, _size);
		}
		return _malloc_item;
	}
	return (_kmalloc(_size, _type, _flags));
}

static __always_inline void *
__kmalloc_obj(size_t _size, struct malloc_type *_type, int _flags)
{
	if (__builtin_constant_p(_size) && __builtin_constant_p(_flags) &&
	    (_flags & M_ZERO)) {
		void *_malloc_item;
		_malloc_item = _kmalloc_obj(__VM_CACHELINE_ALIGN(_size),
					   _type, _flags & ~M_ZERO);
		if ((_flags & (M_WAITOK|M_NULLOK)) == M_WAITOK ||
		    __predict_true(_malloc_item != NULL)) {
			__builtin_memset(_malloc_item, 0, _size);
		}
		return _malloc_item;
	}
	return (_kmalloc_obj(__VM_CACHELINE_ALIGN(_size), _type, _flags));
}

#define kmalloc(size, type, flags)	\
		__kmalloc((size), type, (flags))
#define kmalloc_obj(size, type, flags)	\
		__kmalloc_obj((size), type##_obj, (flags))

/*
 * These only operate on normal mixed-size zones
 */
void	*krealloc(void *addr, unsigned long size, struct malloc_type *type,
		  int flags) __heedresult __alloc_size(2);
char	*kstrdup(const char *, struct malloc_type *)
		 __malloclike __heedresult;
char	*kstrndup(const char *, size_t maxlen, struct malloc_type *)
		  __malloclike __heedresult;

/*
 * Just macro the debug versions over to the non-debug versions, this
 * reduces the need for #ifdef's in kern_slaballoc.c and kern_kmalloc.c.
 */
#define _kmalloc_debug(size, type, flags, file, line)		\
	__kmalloc((size), type, (flags))
#define _kmalloc_obj_debug(size, type, flags, file, line)	\
	__kmalloc_obj((size), type##_obj, (flags))
#define krealloc_debug(addr, size, type, flags, file, line)	\
	krealloc(addr, size, type, flags)
#define kstrdup_debug(str, type, file, line)			\
	kstrdup(str, type)
#define kstrndup_debug(str, maxlen, type, file, line)		\
	kstrndup(str, maxlen, type)
#endif /* SLAB_DEBUG */

#define kmalloc_obj_raise_limit(type, bytes)	\
		kmalloc_raise_limit(type##_obj, bytes)
#define kmalloc_obj_set_unlimited(type)		\
		kmalloc_set_unlimited(type##_obj)

void	_kfree(void *addr, struct malloc_type *type) __nonnull(2);
void	_kfree_obj(void *addr, struct malloc_type *type) __nonnull(2);
size_t	kmalloc_usable_size(const void *ptr);
long	kmalloc_limit(struct malloc_type *type);
void	slab_cleanup(void);

#define kfree(addr, type)	_kfree(addr, type)
#define kfree_obj(addr, type)	_kfree_obj(addr, type##_obj)

#endif /* _KERNEL */

#endif /* !_SYS_MALLOC_H_ */
