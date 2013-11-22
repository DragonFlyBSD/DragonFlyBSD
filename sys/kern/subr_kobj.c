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
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#ifndef TEST
#include <sys/systm.h>
#endif
#include <sys/kobj.h>

#ifdef TEST
#include "usertest.h"
#endif

static MALLOC_DEFINE(M_KOBJ, "kobj", "Kernel object structures");

static struct lwkt_token kobj_token;
static int kobj_next_id = 1;

static void
kobj_init_token(void *arg)
{
	lwkt_token_init(&kobj_token, "kobj");
}

SYSINIT(kobj, SI_BOOT1_LOCK, SI_ORDER_ANY, kobj_init_token, NULL);

/*
 * This method structure is used to initialise new caches. Since the
 * desc pointer is NULL, it is guaranteed never to match any real
 * descriptors.
 */
static const struct kobj_method null_method = {
	0, 0,
};

int
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
	kobj_ops_t ops;
	int i;

	/*
	 * Don't do anything if we are already compiled.
	 */
	if (cls->ops)
		return;

	/*
	 * Allocate space for the compiled ops table.
	 */
	ops = kmalloc(sizeof(struct kobj_ops), M_KOBJ, M_INTWAIT);
	for (i = 0; i < KOBJ_CACHE_SIZE; i++)
		ops->cache[i] = &null_method;
	if (cls->ops) {
		/*
		 * In case of preemption, another thread might have been faster,
		 * but that's fine for us.
		 */
		kfree(ops, M_KOBJ);
		return;
	}

	ops->cls = cls;
	cls->ops = ops;

	/*
	 * Afterwards register any methods which need it.
	 */
	for (m = cls->methods; m->desc; m++)
		kobj_register_method(m->desc);
}

static kobj_method_t *
kobj_lookup_method_class(kobj_class_t cls, kobjop_desc_t desc)
{
	kobj_method_t *methods = cls->methods;
	kobj_method_t *ce;

	for (ce = methods; ce && ce->desc; ce++)
		if (ce->desc == desc)
			return(ce);

	return(0);
}

static kobj_method_t *
kobj_lookup_method_mi(kobj_class_t cls, kobjop_desc_t desc)
{
	kobj_method_t *ce;
	kobj_class_t *basep;

	ce = kobj_lookup_method_class(cls, desc);
	if (ce)
		return(ce);

	basep = cls->baseclasses;
	if (basep) {
		for (; *basep; basep++) {
			ce = kobj_lookup_method_mi(*basep, desc);
			if (ce)
				return(ce);
		}
	}

	return(0);
}

kobj_method_t*
kobj_lookup_method(kobj_class_t cls,
		   kobj_method_t **cep,
		   kobjop_desc_t desc)
{
	kobj_method_t *ce;

	ce = kobj_lookup_method_mi(cls, desc);
	if (!ce)
		ce = &desc->deflt;
	*cep = ce;
	return(ce);
}

/*
 * This is called from the KOBJOPLOOKUP() macro in sys/kobj.h and
 * replaces the original large body of the macro with a single
 * procedure call.
 */
kobjop_t
kobj_lookup_method_cache(kobj_class_t cls, kobj_method_t **cep,
			 kobjop_desc_t desc)
{
	kobj_method_t *ce;

	cep = &cep[desc->id & (KOBJ_CACHE_SIZE-1)];
	ce = *cep;
	if (ce->desc != desc)
		ce = kobj_lookup_method(cls, cep, desc);
	return(ce->func);
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
	kfree(cls->ops, M_KOBJ);
	cls->ops = 0;
}

void
kobj_class_instantiate(kobj_class_t cls)
{
	lwkt_gettoken(&kobj_token);
	crit_enter();

	if (!cls->ops)
		kobj_class_compile(cls);
	cls->refs++;

	crit_exit();
	lwkt_reltoken(&kobj_token);
}

void
kobj_class_uninstantiate(kobj_class_t cls)
{
	lwkt_gettoken(&kobj_token);
	crit_enter();

	cls->refs--;
	if (cls->refs == 0)
		kobj_class_free(cls);

	crit_exit();
	lwkt_reltoken(&kobj_token);
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
	obj = kmalloc(cls->size, mtype, mflags | M_ZERO);
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
		kfree(obj, mtype);
}
