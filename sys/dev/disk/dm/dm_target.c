/*        $NetBSD: dm_target.c,v 1.12 2010/01/04 00:14:41 haad Exp $      */

/*
 * Copyright (c) 2010-2011 Alex Hornung <alex@alexhornung.com>
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Hamsik.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code Must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <dev/disk/dm/dm.h>

#include "netbsd-dm.h"

static dm_target_t *dm_target_lookup_name(const char *);

static TAILQ_HEAD(, dm_target) dm_target_list;

static struct lock dm_target_mutex;

/*
 * Called indirectly from dm_table_load_ioctl to mark target as used.
 */
void
dm_target_busy(dm_target_t *target)
{
	atomic_add_int(&target->ref_cnt, 1);
}
/*
 * Release reference counter on target.
 */
void
dm_target_unbusy(dm_target_t *target)
{
	KKASSERT(target->ref_cnt > 0);
	atomic_subtract_int(&target->ref_cnt, 1);
}

/*
 * Try to autoload the module for the requested target.
 */
dm_target_t *
dm_target_autoload(const char *dm_target_name)
{
	char mod_name[128];
	dm_target_t *dmt;
	linker_file_t linker_file;
	int error;

	ksnprintf(mod_name, sizeof(mod_name), "dm_target_%s", dm_target_name);
	error = linker_reference_module(mod_name, NULL, &linker_file);
	if (error != 0) {
		kprintf("dm: could not autoload module for target %s\n",
		    dm_target_name);
		return NULL;
	}

	dmt = dm_target_lookup(dm_target_name);
	if (dmt == NULL) {
		linker_release_module(NULL, NULL, linker_file);
		return NULL;
	}

	/* XXX: extra-big hack to allow users to kldunload the module */
	linker_file->userrefs = 1;

	return dmt;
}

/*
 * Lookup for target in global target list.
 */
dm_target_t *
dm_target_lookup(const char *dm_target_name)
{
	dm_target_t *dmt;

	dmt = NULL;

	if (dm_target_name == NULL)
		return NULL;

	lockmgr(&dm_target_mutex, LK_EXCLUSIVE);

	dmt = dm_target_lookup_name(dm_target_name);
	if (dmt != NULL)
		dm_target_busy(dmt);

	lockmgr(&dm_target_mutex, LK_RELEASE);

	return dmt;
}
/*
 * Search for name in TAIL and return apropriate pointer.
 */
static dm_target_t *
dm_target_lookup_name(const char *dm_target_name)
{
	dm_target_t *dm_target;

	TAILQ_FOREACH(dm_target, &dm_target_list, dm_target_next) {
		if (strcmp(dm_target_name, dm_target->name) == 0)
			return dm_target;
	}

	return NULL;
}
/*
 * Insert new target struct into the TAIL.
 * dm_target
 *   contains name, version, function pointer to specifif target functions.
 */
int
dm_target_insert(dm_target_t *dm_target)
{
	dm_target_t *dmt;

	if (dm_target->init == NULL) {
		kprintf("dm: %s missing init\n", dm_target->name);
		return EINVAL;
	}
	if (dm_target->destroy == NULL) {
		kprintf("dm: %s missing destroy\n", dm_target->name);
		return EINVAL;
	}
	if (dm_target->strategy == NULL) {
		kprintf("dm: %s missing strategy\n", dm_target->name);
		return EINVAL;
	}

	lockmgr(&dm_target_mutex, LK_EXCLUSIVE);

	dmt = dm_target_lookup_name(dm_target->name);
	if (dmt != NULL) {
		kprintf("uhoh, target_insert EEXIST\n");
		lockmgr(&dm_target_mutex, LK_RELEASE);
		return EEXIST;
	}
	TAILQ_INSERT_TAIL(&dm_target_list, dm_target, dm_target_next);

	lockmgr(&dm_target_mutex, LK_RELEASE);

	return 0;
}


/*
 * Remove target from TAIL, target is selected with it's name.
 */
int
dm_target_remove(char *dm_target_name)
{
	dm_target_t *dmt;

	KKASSERT(dm_target_name != NULL);

	lockmgr(&dm_target_mutex, LK_EXCLUSIVE);

	dmt = dm_target_lookup_name(dm_target_name);
	if (dmt == NULL) {
		lockmgr(&dm_target_mutex, LK_RELEASE);
		return ENOENT;
	}
	if (dmt->ref_cnt > 0) {
		lockmgr(&dm_target_mutex, LK_RELEASE);
		return EBUSY;
	}
	TAILQ_REMOVE(&dm_target_list,
	    dmt, dm_target_next);

	lockmgr(&dm_target_mutex, LK_RELEASE);

	dm_target_free(dmt);

	return 0;
}

/*
 * Allocate new target entry.
 */
dm_target_t *
dm_target_alloc(const char *name)
{
	dm_target_t *dmt;

	dmt = kmalloc(sizeof(*dmt), M_DM, M_WAITOK | M_ZERO);
	if (dmt == NULL)
		return NULL;

	if (name)
		strlcpy(dmt->name, name, sizeof(dmt->name));

	return dmt;
}

int
dm_target_free(dm_target_t *dmt)
{
	KKASSERT(dmt != NULL);

	kfree(dmt, M_DM);

	return 0;
}

/*
 * Return prop_array of dm_target dictionaries.
 */
prop_array_t
dm_target_prop_list(void)
{
	prop_array_t target_array, ver;
	prop_dictionary_t target_dict;
	dm_target_t *dm_target;

	size_t i;

	target_array = prop_array_create();

	lockmgr(&dm_target_mutex, LK_EXCLUSIVE);

	TAILQ_FOREACH(dm_target, &dm_target_list, dm_target_next) {

		target_dict = prop_dictionary_create();
		ver = prop_array_create();
		prop_dictionary_set_cstring(target_dict, DM_TARGETS_NAME,
		    dm_target->name);

		for (i = 0; i < 3; i++)
			prop_array_add_uint32(ver, dm_target->version[i]);

		prop_dictionary_set(target_dict, DM_TARGETS_VERSION, ver);
		prop_array_add(target_array, target_dict);

		prop_object_release(ver);
		prop_object_release(target_dict);
	}

	lockmgr(&dm_target_mutex, LK_RELEASE);

	return target_array;
}

/* Initialize dm_target subsystem. */
int
dm_target_init(void)
{
	TAILQ_INIT(&dm_target_list);	/* initialize global target list */
	lockinit(&dm_target_mutex, "dmtrgt", 0, LK_CANRECURSE);

	return 0;
}

/*
 * Destroy all targets and remove them from queue.
 * This routine is called from dmdestroy, before module
 * is unloaded.
 */
int
dm_target_uninit(void)
{
	dm_target_t *dm_target;

	lockmgr(&dm_target_mutex, LK_EXCLUSIVE);

	while ((dm_target = TAILQ_FIRST(&dm_target_list)) != NULL) {
		TAILQ_REMOVE(&dm_target_list, dm_target, dm_target_next);
		dm_target_free(dm_target);
	}
	KKASSERT(TAILQ_EMPTY(&dm_target_list));

	lockmgr(&dm_target_mutex, LK_RELEASE);

	lockuninit(&dm_target_mutex);

	return 0;
}
