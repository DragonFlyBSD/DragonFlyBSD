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
 * $FreeBSD: src/sys/kern/subr_kobj.c,v 1.4.2.1 2001/02/02 19:49:13 cg Exp $
 * $DragonFly: src/sys/kern/subr_kobj.c,v 1.5 2004/04/01 08:41:24 joerg Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#ifndef TEST
#include <sys/systm.h>
#endif
#include <sys/kobj.h>

#ifdef TEST
#include "usertest.h"
#endif

static MALLOC_DEFINE(M_KOBJ, "kobj", "Kernel object structures");

#ifdef KOBJ_STATS

#include <sys/sysctl.h>

u_int kobj_lookup_hits;
u_int kobj_lookup_misses;

SYSCTL_UINT(_kern, OID_AUTO, kobj_hits, CTLFLAG_RD,
	   &kobj_lookup_hits, 0, "")
SYSCTL_UINT(_kern, OID_AUTO, kobj_misses, CTLFLAG_RD,
	   &kobj_lookup_misses, 0, "")

#endif

static int kobj_next_id = 1;

static int
kobj_error_method(void)
{
	return ENXIO;
}

static void
kobj_register_method(struct kobjop_desc *desc)
{
	if (desc->id == 0)
		desc->id = kobj_next_id++;
}

static void
kobj_unregister_method(struct kobjop_desc *desc)
{
}

static void
kobj_class_compile(kobj_class_t cls)
{
	kobj_method_t *m;

	/*
	 * Don't do anything if we are already compiled.
	 */
	if (cls->ops)
		return;

	/*
	 * Allocate space for the compiled ops table.
	 */
	cls->ops = malloc(sizeof(struct kobj_ops), M_KOBJ, M_INTWAIT | M_ZERO);
	if (!cls->ops)
		panic("kobj_compile_methods: out of memory");
	cls->ops->cls = cls;

	/*
	 * Afterwards register any methods which need it.
	 */
	for (m = cls->methods; m->desc; m++)
		kobj_register_method(m->desc);
}

void
kobj_lookup_method(kobj_method_t *methods,
		   kobj_method_t *ce,
		   kobjop_desc_t desc)
{
	ce->desc = desc;
	for (; methods && methods->desc; methods++) {
		if (methods->desc == desc) {
			ce->func = methods->func;
			return;
		}
	}
	if (desc->deflt)
		ce->func = desc->deflt;
	else
		ce->func = kobj_error_method;
	return;
}

static void
kobj_class_free(kobj_class_t cls)
{
	int i;
	kobj_method_t *m;

	/*
	 * Unregister any methods which are no longer used.
	 */
	for (i = 0, m = cls->methods; m->desc; i++, m++)
		kobj_unregister_method(m->desc);

	/*
	 * Free memory and clean up.
	 */
	free(cls->ops, M_KOBJ);
	cls->ops = 0;
}

void
kobj_class_instantiate(kobj_class_t cls)
{
	if (!cls->ops)
		kobj_class_compile(cls);
	cls->refs++;
}

void
kobj_class_uninstantiate(kobj_class_t cls)
{
	cls->refs--;
	if (cls->refs == 0)
		kobj_class_free(cls);
}

kobj_t
kobj_create(kobj_class_t cls,
	    struct malloc_type *mtype,
	    int mflags)
{
	kobj_t obj;

	/*
	 * Allocate and initialise the new object.
	 */
	obj = malloc(cls->size, mtype, mflags | M_ZERO);
	if (!obj)
		return 0;
	kobj_init(obj, cls);

	return obj;
}

void
kobj_init(kobj_t obj, kobj_class_t cls)
{
	kobj_class_instantiate(cls);
	obj->ops = cls->ops;
}

void
kobj_delete(kobj_t obj, struct malloc_type *mtype)
{
	kobj_class_t cls = obj->ops->cls;

	kobj_class_uninstantiate(cls);

	obj->ops = 0;
	if (mtype)
		free(obj, mtype);
}
