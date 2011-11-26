/*-
 * Copyright (c) 2000 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/kobj.h,v 1.8 2003/09/22 21:32:49 peter Exp $
 * $DragonFly: src/sys/sys/kobj.h,v 1.11 2007/10/03 18:58:20 dillon Exp $
 */

#ifndef _SYS_KOBJ_H_
#define _SYS_KOBJ_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

/*
 * Forward declarations
 */
typedef struct kobj		*kobj_t;
typedef struct kobj_class	*kobj_class_t;
typedef struct kobj_method	kobj_method_t;
typedef int			(*kobjop_t)(void);
typedef struct kobj_ops		*kobj_ops_t;
typedef struct kobjop_desc	*kobjop_desc_t;
struct malloc_type;

struct kobj_method {
	kobjop_desc_t	desc;
	kobjop_t	func;
};

/*
 * A class is simply a method table and a sizeof value. When the first
 * instance of the class is created, the method table will be compiled
 * into a form more suited to efficient method dispatch. This compiled
 * method table is always the first field of the object.
 */
#define KOBJ_CLASS_FIELDS						\
	const char	*name;		/* class name */		\
	kobj_method_t	*methods;	/* method table */		\
	size_t		size;		/* object size */		\
	kobj_class_t	*baseclasses;	/* base classes */		\
	u_int		refs;		/* reference count */		\
	kobj_ops_t	ops		/* compiled method table */

struct kobj_class {
	KOBJ_CLASS_FIELDS;
};

/*
 * Implementation of kobj.
 */
#define KOBJ_FIELDS				\
	kobj_ops_t	ops

struct kobj {
	KOBJ_FIELDS;
};

/*
 * The ops table is used as a cache of results from kobj_lookup_method().
 */

#define KOBJ_CACHE_SIZE	256

struct kobj_ops {
	kobj_method_t	*cache[KOBJ_CACHE_SIZE];
	kobj_class_t	cls;
};

struct kobjop_desc {
	unsigned int	id;	/* unique ID */
	kobj_method_t	*deflt;	/* default implementation */
};

/*
 * Shorthand for constructing method tables.
 */
#define KOBJMETHOD(NAME, FUNC) { &NAME##_desc, (kobjop_t) FUNC }

/*
 * Declare a class (which should be defined in another file.
 */
#define DECLARE_CLASS(name) extern struct kobj_class name

/*
 * Define a class with no base classes (api backward-compatible. with
 * FreeBSD-5.1 and earlier).
 */
#define DEFINE_CLASS(name, methods, size)     		\
DEFINE_CLASS_0(name, name ## _class, methods, size)

/*
 * Define a class with no base classes. Use like this:
 *
 * DEFINE_CLASS_0(foo, foo_class, foo_methods, sizeof(foo_softc));
 */
#define DEFINE_CLASS_0(name, classvar, methods, size)	\
							\
struct kobj_class classvar = {				\
	#name, methods, size, NULL, 0, NULL		\
}

/*
 * Define a class with no base classes using the named structure
 * as an extension of the kobj_class structure.
 */
#define DEFINE_CLASS_EXT(name, classvar, methods, size, extname)	\
							\
struct extname classvar = {				\
	#name, methods, size, NULL, 0, NULL		\
}

/*
 * Define a class inheriting a single base class. Use like this:
 *
 * DEFINE_CLASS1(foo, foo_class, foo_methods, sizeof(foo_softc),
 *			  bar);
 */
#define DEFINE_CLASS_1(name, classvar, methods, size,	\
		       base1)				\
							\
static kobj_class_t name ## _baseclasses[] = {		\
	&base1, 0					\
};							\
struct kobj_class classvar = {				\
	#name, methods, size, name ## _baseclasses	\
}

/*
 * Define a class inheriting two base classes. Use like this:
 *
 * DEFINE_CLASS2(foo, foo_class, foo_methods, sizeof(foo_softc),
 *			  bar, baz);
 */
#define DEFINE_CLASS_2(name, methods, size,		\
	               base1, base2)			\
							\
static kobj_class_t name ## _baseclasses[] = {		\
	&base1,						\
	&base2, 0					\
};							\
struct kobj_class name ## _class = {			\
	#name, methods, size, name ## _baseclasses	\
}
 
/*
 * Define a class inheriting three base classes. Use like this:
 *
 * DEFINE_CLASS3(foo, foo_class, foo_methods, sizeof(foo_softc),
 *			  bar, baz, foobar);
 */
#define DEFINE_CLASS_3(name, methods, size,		\
		       base1, base2, base3)		\
							\
static kobj_class_t name ## _baseclasses[] = {		\
	&base1,						\
	&base2,						\
	&base3, 0					\
};							\
struct kobj_class name ## _class = {			\
	#name, methods, size, name ## _baseclasses	\
}

#ifdef _KERNEL

/*
 * Compile class for the first instance and add a reference.
 */
void		kobj_class_instantiate(kobj_class_t cls);

/*
 * Remove a reference and free method table with the last instance.
 */
void		kobj_class_uninstantiate(kobj_class_t cls);

/*
 * Allocate memory for and initialise a new object.
 */
kobj_t		kobj_create(kobj_class_t cls,
			    struct malloc_type *mtype,
			    int mflags);

/*
 * Initialise a pre-allocated object.
 */
void		kobj_init(kobj_t obj, kobj_class_t cls);

/*
 * Delete an object. If mtype is non-zero, free the memory.
 */
void		kobj_delete(kobj_t obj, struct malloc_type *mtype);

/*
 * Lookup the method in the cache and if it isn't there look it up the
 * slow way.
 *
 * We do the cache inside kobj_lookup_method() now, we don't try to
 * expand it because it's really silly.  These lookups are not in the
 * critical path.
 */

#define KOBJOPLOOKUP(OPS,OP) do {					\
	_m = kobj_lookup_method_cache(OPS->cls, &OPS->cache[0],		\
				       &OP##_##desc);			\
} while(0)

kobj_method_t *kobj_lookup_method(kobj_class_t cls,
				  kobj_method_t **cep,
				  kobjop_desc_t desc);

kobjop_t kobj_lookup_method_cache(kobj_class_t cls,
				  kobj_method_t **cep,
				  kobjop_desc_t desc);

/*
 * Default method implementation. Returns ENXIO.
 */
int kobj_error_method(void);

#endif /* _KERNEL */

#endif /* !_SYS_KOBJ_H_ */
