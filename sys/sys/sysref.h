/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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
 * 
 * $DragonFly: src/sys/sys/sysref.h,v 1.4 2007/05/29 17:01:02 dillon Exp $
 */
/*
 * System resource registration, reference counter, and allocation
 * management subsystem.
 *
 * All major system resources use this structure and API to associate
 * a machine-unique sysid with a major system resource structure, to
 * reference count that structure, to register the structure and provide
 * a dictionary lookup feature for remote access.
 *
 * System resources are backed by the objcache.
 */

#ifndef _SYS_SYSREF_H_
#define _SYS_SYSREF_H_

#ifndef _SYS_SYSID_H_
#include <sys/sysid.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_OBJCACHE_H_
#include <sys/objcache.h>
#endif
#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Register a resource structure type.  Note that the destroy function
 * will be called on 1->0 transitions (really 1->-0x40000000 transitions),
 * and the free function 
 * but the free function can be called via an IPI, without the BGL, and
 * must be carefully coded if it does anything more complex then objcache_put
 */
typedef void (*sysref_terminate_func_t)(void *);
typedef void (*sysref_lock_func_t)(void *);
typedef void (*sysref_unlock_func_t)(void *);

struct sysref_class {
	const char *name;		/* name of the system resource */
	malloc_type_t mtype;		/* malloc backing store */
	int	proto;			/* RPC protocol id */
	int	offset;			/* offset of sysref in resource */
	int	objsize;		/* size of the resource structure */
	int	nom_cache;		/* nominal objects to cache */
	int	flags;
	struct objcache *oc;		/* object cache */
	objcache_ctor_fn *ctor;		/* objcache ctor chaining */
	objcache_dtor_fn *dtor;		/* objcache dtor chaining */
	struct sysref_ops {
		sysref_terminate_func_t terminate;
		sysref_lock_func_t lock;
		sysref_unlock_func_t unlock;
	} ops;
};

#define SRC_MANAGEDINIT	0x0001		/* do not auto-zero the resource */

/*
 * sysref - embedded in resource structures.
 *
 * NOTE: The cpuid determining which cpu's RB tree the sysref is
 * associated with is integrated into the sysref.
 *
 * NOTE: A resource can be in varying states of construction or
 * deconstruction, denoted by having a negative refcnt.  To keep
 * performance nominal we reuse sysids that are NOT looked up via
 * syslink (meaning we don't have to adjust their location in the
 * RB tree).  The objcache is used to cache the RB tree linkage.
 */
struct sysref {
	RB_ENTRY(sysref) rbnode;	/* per-cpu red-black tree node */
	sysid_t	sysid;			/* machine-wide unique sysid */
	int	refcnt;			/* normal reference count */
	int	flags;
	struct sysref_class *srclass;	/* type of resource and API */
};

#define SRF_SYSIDUSED	0x0001		/* sysid was used for access */
#define SRF_ALLOCATED	0x0002		/* sysref_alloc used to allocate */
#define SRF_PUTAWAY	0x0004		/* in objcache */

RB_HEAD(sysref_rb_tree, sysref);
RB_PROTOTYPE2(sysref_rb_tree, sysref, rbnode, rb_sysref_compare, sysid_t);

#endif

/*
 * Protocol numbers
 */
#define SYSREF_PROTO_VMSPACE	0x0020
#define SYSREF_PROTO_VMOBJECT	0x0021
#define SYSREF_PROTO_VNODE	0x0022
#define SYSREF_PROTO_PROCESS	0x0023
#define SYSREF_PROTO_LWP	0x0024
#define SYSREF_PROTO_FD		0x0025
#define SYSREF_PROTO_FP		0x0026
#define SYSREF_PROTO_SOCKET	0x0027
#define SYSREF_PROTO_NCP	0x0028
#define SYSREF_PROTO_BUF	0x0029
#define SYSREF_PROTO_FSMOUNT	0x002A
#define SYSREF_PROTO_DEV	0x002B

#ifdef _KERNEL

sysid_t allocsysid(void);

#endif

#endif
