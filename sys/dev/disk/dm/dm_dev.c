/*        $NetBSD: dm_dev.c,v 1.8 2010/01/04 00:19:08 haad Exp $      */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Hamsik.
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
#include <sys/param.h>

#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/dm.h>

#include "netbsd-dm.h"

static dm_dev_t *dm_dev_lookup_name(const char *);
static dm_dev_t *dm_dev_lookup_uuid(const char *);
static dm_dev_t *dm_dev_lookup_minor(int);

static struct dm_dev_head dm_dev_list =
TAILQ_HEAD_INITIALIZER(dm_dev_list);

struct lock dm_dev_mutex;

/* dm_dev_mutex must be holdby caller before using disable_dev. */
static void
disable_dev(dm_dev_t * dmv)
{
	TAILQ_REMOVE(&dm_dev_list, dmv, next_devlist);
	lockmgr(&dmv->dev_mtx, LK_EXCLUSIVE);
	lockmgr(&dm_dev_mutex, LK_RELEASE);
	while (dmv->ref_cnt != 0)
		cv_wait(&dmv->dev_cv, &dmv->dev_mtx);
	lockmgr(&dmv->dev_mtx, LK_RELEASE);
}
/*
 * Generic function used to lookup dm_dev_t. Calling with dm_dev_name
 * and dm_dev_uuid NULL is allowed.
 */
dm_dev_t *
dm_dev_lookup(const char *dm_dev_name, const char *dm_dev_uuid,
    int dm_dev_minor)
{
	dm_dev_t *dmv;

	dmv = NULL;
	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	/* KKASSERT(dm_dev_name != NULL && dm_dev_uuid != NULL && dm_dev_minor
	 * > 0); */
	if (dm_dev_minor > 0)
		if ((dmv = dm_dev_lookup_minor(dm_dev_minor)) != NULL) {
			dm_dev_busy(dmv);
			lockmgr(&dm_dev_mutex, LK_RELEASE);
			return dmv;
		}
	if (dm_dev_name != NULL)
		if ((dmv = dm_dev_lookup_name(dm_dev_name)) != NULL) {
			dm_dev_busy(dmv);
			lockmgr(&dm_dev_mutex, LK_RELEASE);
			return dmv;
		}
	if (dm_dev_uuid != NULL)
		if ((dmv = dm_dev_lookup_uuid(dm_dev_uuid)) != NULL) {
			dm_dev_busy(dmv);
			lockmgr(&dm_dev_mutex, LK_RELEASE);
			return dmv;
		}
	lockmgr(&dm_dev_mutex, LK_RELEASE);
	return NULL;
}


/*
 * Lookup device with its minor number.
 */
static dm_dev_t *
dm_dev_lookup_minor(int dm_dev_minor)
{
	dm_dev_t *dmv;

	TAILQ_FOREACH(dmv, &dm_dev_list, next_devlist) {
		if (dm_dev_minor == dmv->minor)
			return dmv;
	}

	return NULL;
}
/*
 * Lookup device with it's device name.
 */
static dm_dev_t *
dm_dev_lookup_name(const char *dm_dev_name)
{
	dm_dev_t *dmv;

	TAILQ_FOREACH(dmv, &dm_dev_list, next_devlist) {
		if (strcmp(dm_dev_name, dmv->name) == 0)
			return dmv;
	}

	return NULL;
}
/*
 * Lookup device with it's device uuid. Used mostly by LVM2tools.
 */
static dm_dev_t *
dm_dev_lookup_uuid(const char *dm_dev_uuid)
{
	dm_dev_t *dmv;

	TAILQ_FOREACH(dmv, &dm_dev_list, next_devlist) {
		if (strcmp(dm_dev_uuid, dmv->uuid) == 0)
			return dmv;
	}

	return NULL;
}
/*
 * Insert new device to the global list of devices.
 */
int
dm_dev_insert(dm_dev_t * dev)
{
	dm_dev_t *dmv;
	int r;

	dmv = NULL;
	r = 0;

	KKASSERT(dev != NULL);
	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);
	if (((dmv = dm_dev_lookup_uuid(dev->uuid)) == NULL) &&
	    ((dmv = dm_dev_lookup_name(dev->name)) == NULL) &&
	    ((dmv = dm_dev_lookup_minor(dev->minor)) == NULL)) {

		TAILQ_INSERT_TAIL(&dm_dev_list, dev, next_devlist);

	} else
		r = EEXIST;

	lockmgr(&dm_dev_mutex, LK_RELEASE);
	return r;
}

/*
 * Remove device selected with dm_dev from global list of devices.
 */
dm_dev_t *
dm_dev_rem(dm_dev_t *dmv, const char *dm_dev_name, const char *dm_dev_uuid,
    int dm_dev_minor)
{
	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	if (dmv != NULL) {
		disable_dev(dmv);
		return dmv;
	}

	if (dm_dev_minor > 0)
		if ((dmv = dm_dev_lookup_minor(dm_dev_minor)) != NULL) {
			disable_dev(dmv);
			return dmv;
		}
	if (dm_dev_name != NULL)
		if ((dmv = dm_dev_lookup_name(dm_dev_name)) != NULL) {
			disable_dev(dmv);
			return dmv;
		}
	if (dm_dev_uuid != NULL)
		if ((dmv = dm_dev_lookup_name(dm_dev_uuid)) != NULL) {
			disable_dev(dmv);
			return dmv;
		}
	lockmgr(&dm_dev_mutex, LK_RELEASE);

	return NULL;
}
/*
 * Destroy all devices created in device-mapper. Remove all tables
 * free all allocated memmory.
 */
int
dm_dev_destroy(void)
{
	dm_dev_t *dmv;
	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	while (TAILQ_FIRST(&dm_dev_list) != NULL) {

		dmv = TAILQ_FIRST(&dm_dev_list);

		TAILQ_REMOVE(&dm_dev_list, TAILQ_FIRST(&dm_dev_list),
		    next_devlist);

		lockmgr(&dmv->dev_mtx, LK_EXCLUSIVE);

		while (dmv->ref_cnt != 0)
			cv_wait(&dmv->dev_cv, &dmv->dev_mtx);

		/* Destroy active table first.  */
		dm_table_destroy(&dmv->table_head, DM_TABLE_ACTIVE);

		/* Destroy inactive table if exits, too. */
		dm_table_destroy(&dmv->table_head, DM_TABLE_INACTIVE);

		dm_table_head_destroy(&dmv->table_head);

		lockmgr(&dmv->dev_mtx, LK_RELEASE);
		lockuninit(&dmv->dev_mtx);
		cv_destroy(&dmv->dev_cv);

		(void) kfree(dmv, M_DM);
	}
	lockmgr(&dm_dev_mutex, LK_RELEASE);

	lockuninit(&dm_dev_mutex);
	return 0;
}
/*
 * Allocate new device entry.
 */
dm_dev_t *
dm_dev_alloc(void)
{
	dm_dev_t *dmv;

	dmv = kmalloc(sizeof(dm_dev_t), M_DM, M_WAITOK | M_ZERO);

	if (dmv != NULL)
		dmv->diskp = kmalloc(sizeof(struct disk), M_DM, M_WAITOK | M_ZERO);

	return dmv;
}
/*
 * Freed device entry.
 */
int
dm_dev_free(dm_dev_t * dmv)
{
	KKASSERT(dmv != NULL);

	lockuninit(&dmv->dev_mtx);
	lockuninit(&dmv->diskp_mtx);
	cv_destroy(&dmv->dev_cv);

	if (dmv->diskp != NULL)
		(void) kfree(dmv->diskp, M_DM);

	(void) kfree(dmv, M_DM);

	return 0;
}

void
dm_dev_busy(dm_dev_t * dmv)
{
	lockmgr(&dmv->dev_mtx, LK_EXCLUSIVE);
	dmv->ref_cnt++;
	lockmgr(&dmv->dev_mtx, LK_RELEASE);
}

void
dm_dev_unbusy(dm_dev_t * dmv)
{
	KKASSERT(dmv->ref_cnt != 0);

	lockmgr(&dmv->dev_mtx, LK_EXCLUSIVE);
	if (--dmv->ref_cnt == 0)
		cv_broadcast(&dmv->dev_cv);
	lockmgr(&dmv->dev_mtx, LK_RELEASE);
}
/*
 * Return prop_array of dm_targer_list dictionaries.
 */
prop_array_t
dm_dev_prop_list(void)
{
	dm_dev_t *dmv;
	prop_array_t dev_array;
	prop_dictionary_t dev_dict;

	dev_array = prop_array_create();

	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	TAILQ_FOREACH(dmv, &dm_dev_list, next_devlist) {
		dev_dict = prop_dictionary_create();

		prop_dictionary_set_cstring(dev_dict, DM_DEV_NAME, dmv->name);
		prop_dictionary_set_uint32(dev_dict, DM_DEV_DEV, dmv->minor);

		prop_array_add(dev_array, dev_dict);
		prop_object_release(dev_dict);
	}

	lockmgr(&dm_dev_mutex, LK_RELEASE);
	return dev_array;
}
/*
 * Initialize global device mutex.
 */
int
dm_dev_init(void)
{
	TAILQ_INIT(&dm_dev_list);	/* initialize global dev list */
	lockinit(&dm_dev_mutex, "dmdevlist", 0, LK_CANRECURSE);
	return 0;
}
