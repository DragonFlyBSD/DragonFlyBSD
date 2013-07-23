/*-
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/drm2/drm_gem_names.c,v 1.1 2012/05/22 11:07:44 kib Exp $
*/

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <dev/drm2/drm_gem_names.h>

MALLOC_DEFINE(M_GEM_NAMES, "gem_name", "Hash headers for the gem names");

static void drm_gem_names_delete_name(struct drm_gem_names *names,
    struct drm_gem_name *np);

void
drm_gem_names_init(struct drm_gem_names *names)
{

	names->unr = new_unrhdr(1, INT_MAX, NULL); /* XXXKIB */
	names->names_hash = hashinit(1000 /* XXXKIB */, M_GEM_NAMES,
	    &names->hash_mask);
	lockinit(&names->lock, "drmnames", 0, LK_CANRECURSE);
}

void
drm_gem_names_fini(struct drm_gem_names *names)
{
	struct drm_gem_name *np;
	int i;

	lockmgr(&names->lock, LK_EXCLUSIVE);
	for (i = 0; i <= names->hash_mask; i++) {
		while ((np = LIST_FIRST(&names->names_hash[i])) != NULL) {
			drm_gem_names_delete_name(names, np);
			lockmgr(&names->lock, LK_EXCLUSIVE);
		}
	}
	lockmgr(&names->lock, LK_RELEASE);
	lockuninit(&names->lock);
	hashdestroy(names->names_hash, M_GEM_NAMES, names->hash_mask);
	delete_unrhdr(names->unr);
}

static struct drm_gem_names_head *
gem_name_hash_index(struct drm_gem_names *names, int name)
{

	return (&names->names_hash[name & names->hash_mask]);
}

void *
drm_gem_name_ref(struct drm_gem_names *names, uint32_t name,
    void (*ref)(void *))
{
	struct drm_gem_name *n;

	lockmgr(&names->lock, LK_EXCLUSIVE);
	LIST_FOREACH(n, gem_name_hash_index(names, name), link) {
		if (n->name == name) {
			if (ref != NULL)
				ref(n->ptr);
			lockmgr(&names->lock, LK_RELEASE);
			return (n->ptr);
		}
	}
	lockmgr(&names->lock, LK_RELEASE);
	return (NULL);
}

struct drm_gem_ptr_match_arg {
	uint32_t res;
	void *ptr;
};

static int
drm_gem_ptr_match(uint32_t name, void *ptr, void *arg)
{
	struct drm_gem_ptr_match_arg *a;

	a = arg;
	if (ptr == a->ptr) {
		a->res = name;
		return (1);
	} else
		return (0);
}

uint32_t
drm_gem_find_name(struct drm_gem_names *names, void *ptr)
{
	struct drm_gem_ptr_match_arg arg;

	arg.res = 0;
	arg.ptr = ptr;
	drm_gem_names_foreach(names, drm_gem_ptr_match, &arg);
	return (arg.res);
}

int
drm_gem_name_create(struct drm_gem_names *names, void *p, uint32_t *name)
{
	struct drm_gem_name *np;

	np = kmalloc(sizeof(struct drm_gem_name), M_GEM_NAMES, M_WAITOK);
	lockmgr(&names->lock, LK_EXCLUSIVE);
	if (*name != 0) {
		lockmgr(&names->lock, LK_RELEASE);
		return (EALREADY);
	}
	np->name = alloc_unr(names->unr);
	if (np->name == -1) {
		lockmgr(&names->lock, LK_RELEASE);
		free(np, M_GEM_NAMES);
		return (ENOMEM);
	}
	*name = np->name;
	np->ptr = p;
	LIST_INSERT_HEAD(gem_name_hash_index(names, np->name), np, link);
	lockmgr(&names->lock, LK_RELEASE);
	return (0);
}

static void
drm_gem_names_delete_name(struct drm_gem_names *names, struct drm_gem_name *np)
{
	KKASSERT(lockstatus(&names->lock, curthread) != 0);
	LIST_REMOVE(np, link);
	lockmgr(&names->lock, LK_RELEASE);
	free_unr(names->unr, np->name);
	kfree(np, M_GEM_NAMES);
}

void *
drm_gem_names_remove(struct drm_gem_names *names, uint32_t name)
{
	struct drm_gem_name *n;
	void *res;

	lockmgr(&names->lock, LK_EXCLUSIVE);
	LIST_FOREACH(n, gem_name_hash_index(names, name), link) {
		if (n->name == name) {
			res = n->ptr;
			drm_gem_names_delete_name(names, n);
			return (res);
		}
	}
	lockmgr(&names->lock, LK_RELEASE);
	return (NULL);
}

void
drm_gem_names_foreach(struct drm_gem_names *names,
    int (*f)(uint32_t, void *, void *), void *arg)
{
	struct drm_gem_name *np;
	struct drm_gem_name marker;
	int i, fres;

	bzero(&marker, sizeof(marker));
	marker.name = -1;
	lockmgr(&names->lock, LK_EXCLUSIVE);
	for (i = 0; i <= names->hash_mask; i++) {
		for (np = LIST_FIRST(&names->names_hash[i]); np != NULL; ) {
			if (np->name == -1) {
				np = LIST_NEXT(np, link);
				continue;
			}
			LIST_INSERT_AFTER(np, &marker, link);
			lockmgr(&names->lock, LK_RELEASE);
			fres = f(np->name, np->ptr, arg);
			lockmgr(&names->lock, LK_EXCLUSIVE);
			np = LIST_NEXT(&marker, link);
			LIST_REMOVE(&marker, link);
			if (fres)
				break;
		}
	}
	lockmgr(&names->lock, LK_RELEASE);
}
