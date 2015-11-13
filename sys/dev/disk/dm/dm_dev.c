/*        $NetBSD: dm_dev.c,v 1.8 2010/01/04 00:19:08 haad Exp $      */

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

#include <sys/param.h>
#include <machine/thread.h>
#include <sys/thread2.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/devicestat.h>
#include <sys/device.h>
#include <sys/udev.h>
#include <sys/devfs.h>
#include <sys/malloc.h>
#include <dev/disk/dm/dm.h>

#include "netbsd-dm.h"

extern struct dev_ops dm_ops;

static struct devfs_bitmap dm_minor_bitmap;
uint64_t dm_dev_counter;

static dm_dev_t *dm_dev_lookup_name(const char *);
static dm_dev_t *dm_dev_lookup_uuid(const char *);
static dm_dev_t *dm_dev_lookup_minor(int);
static int dm_dev_destroy(dm_dev_t *);
static dm_dev_t *dm_dev_alloc(const char *, const char *);
static int dm_dev_free(dm_dev_t *);

static TAILQ_HEAD(dm_dev_head, dm_dev) dm_dev_list;

static struct lock dm_dev_mutex;

static char dummy_uuid[DM_UUID_LEN];

/*
 * dm_dev_mutex must be held by caller before using disable_dev.
 */
static void
disable_dev(dm_dev_t *dmv)
{
	KKASSERT(lockstatus(&dm_dev_mutex, curthread) == LK_EXCLUSIVE);

	TAILQ_REMOVE(&dm_dev_list, dmv, next_devlist);
	dm_dev_counter--;

	lockmgr(&dmv->dev_mtx, LK_EXCLUSIVE);
	while (dmv->ref_cnt != 0)
		cv_wait(&dmv->dev_cv, &dmv->dev_mtx);
	lockmgr(&dmv->dev_mtx, LK_RELEASE);
}

static dm_dev_t *
_dm_dev_lookup(const char *name, const char *uuid, int minor)
{
	dm_dev_t *dmv;

	if (minor > 0) {
		if ((dmv = dm_dev_lookup_minor(minor)))
			return dmv;
	}
	if (name != NULL) {
		if ((dmv = dm_dev_lookup_name(name)))
			return dmv;
	}
	if (uuid != NULL) {
		if ((dmv = dm_dev_lookup_uuid(uuid)))
			return dmv;
	}

	return NULL;
}

/*
 * Generic function used to lookup dm_dev_t. Calling with name NULL
 * and uuid NULL is allowed.
 */
dm_dev_t *
dm_dev_lookup(const char *name, const char *uuid, int minor)
{
	dm_dev_t *dmv;

	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	dmv = _dm_dev_lookup(name, uuid, minor);
	if (dmv)
		dm_dev_busy(dmv);

	lockmgr(&dm_dev_mutex, LK_RELEASE);

	return dmv;
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
dm_dev_insert(dm_dev_t *dev)
{
	dm_dev_t *dmv;
	int r;

	dmv = NULL;
	r = 0;

	KKASSERT(dev != NULL);
	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	/*
	 * Ignore uuid lookup if dev->uuid is zero-filled.
	 */
	if (memcmp(dev->uuid, dummy_uuid, DM_UUID_LEN))
		dmv = dm_dev_lookup_uuid(dev->uuid);

	if ((dmv == NULL) &&
	    (_dm_dev_lookup(dev->name, NULL, dev->minor) == NULL)) {
		TAILQ_INSERT_TAIL(&dm_dev_list, dev, next_devlist);
		dm_dev_counter++;
	} else {
		KKASSERT(dmv != NULL);
		r = EEXIST;
	}

	lockmgr(&dm_dev_mutex, LK_RELEASE);
	return r;
}

#if 0
/*
 * Remove device selected with dm_dev from global list of devices.
 */
dm_dev_t *
dm_dev_lookup_evict(const char *name, const char *uuid, int minor)
{
	dm_dev_t *dmv;

	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	dmv = _dm_dev_lookup(name, uuid, minor);
	if (dmv)
		disable_dev(dmv);

	lockmgr(&dm_dev_mutex, LK_RELEASE);

	return dmv;
}
#endif

int
dm_dev_create(dm_dev_t **dmvp, const char *name, const char *uuid, int flags)
{
	dm_dev_t *dmv;
	char name_buf[MAXPATHLEN];
	int r, dm_minor;

	if ((dmv = dm_dev_alloc(name, uuid)) == NULL)
		return ENOMEM;

	dm_minor = devfs_clone_bitmap_get(&dm_minor_bitmap, 0);

	dm_table_head_init(&dmv->table_head);

	lockinit(&dmv->dev_mtx, "dmdev", 0, LK_CANRECURSE);
	cv_init(&dmv->dev_cv, "dm_dev");

	if (flags & DM_READONLY_FLAG)
		dmv->flags |= DM_READONLY_FLAG;

	dmdebug("Creating device dm/%s\n", name);
	ksnprintf(name_buf, sizeof(name_buf), "mapper/%s", dmv->name);

	devstat_add_entry(&dmv->stats, name, 0, DEV_BSIZE,
	    DEVSTAT_NO_ORDERED_TAGS,
	    DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_OTHER,
	    DEVSTAT_PRIORITY_DISK);

	dmv->devt = disk_create_named(name_buf, dm_minor, dmv->diskp, &dm_ops);
	reference_dev(dmv->devt);

	/* Make sure the device are immediately available */
	sync_devs();

	dmv->devt->si_drv1 = dmv;
	dmv->devt->si_drv2 = dmv->diskp;

	dmv->minor = minor(dmv->devt);
	udev_dict_set_cstr(dmv->devt, "subsystem", "disk");

	if ((r = dm_dev_insert(dmv)) != 0)
		dm_dev_destroy(dmv);

	*dmvp = dmv;

	return r;
}

static int
dm_dev_destroy(dm_dev_t *dmv)
{
	int minor;

	/* Destroy active table first.  */
	dm_table_destroy(&dmv->table_head, DM_TABLE_ACTIVE);

	/* Destroy inactive table if exits, too. */
	dm_table_destroy(&dmv->table_head, DM_TABLE_INACTIVE);

	dm_table_head_destroy(&dmv->table_head);

	minor = dkunit(dmv->devt);
	disk_destroy(dmv->diskp);
	devstat_remove_entry(&dmv->stats);

	release_dev(dmv->devt);
	devfs_clone_bitmap_put(&dm_minor_bitmap, minor);

	lockuninit(&dmv->dev_mtx);
	cv_destroy(&dmv->dev_cv);

	/* Destroy device */
	dm_dev_free(dmv);

	return 0;
}

/*
 * dm_dev_remove is called to completely destroy & remove a dm disk device.
 */
int
dm_dev_remove(dm_dev_t *dmv)
{
	/* Remove device from list and wait for refcnt to drop to zero */
	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);
	disable_dev(dmv);
	lockmgr(&dm_dev_mutex, LK_RELEASE);

	/* Destroy and free the device */
	dm_dev_destroy(dmv);

	return 0;
}

int
dm_dev_remove_all(int gentle)
{
	dm_dev_t *dmv, *dmv2;
	int r;

	r = 0;

	lockmgr(&dm_dev_mutex, LK_EXCLUSIVE);

	/*
	 * Process in reverse order so that it can deal with inter-depentent
	 * devices.
	 */
	TAILQ_FOREACH_REVERSE_MUTABLE(dmv, &dm_dev_list, dm_dev_head,
	    next_devlist, dmv2) {
		if (gentle && dmv->is_open) {
			r = EBUSY;
			continue;
		}

		disable_dev(dmv);
		dm_dev_destroy(dmv);
	}
	lockmgr(&dm_dev_mutex, LK_RELEASE);

	return r;
}

/*
 * Allocate new device entry.
 */
static dm_dev_t *
dm_dev_alloc(const char *name, const char*uuid)
{
	dm_dev_t *dmv;

	dmv = kmalloc(sizeof(*dmv), M_DM, M_WAITOK | M_ZERO);
	if (dmv == NULL)
		return NULL;

	dmv->diskp = kmalloc(sizeof(*dmv->diskp), M_DM, M_WAITOK | M_ZERO);
	if (dmv->diskp == NULL) {
		kfree(dmv, M_DM);
		return NULL;
	}

	if (name)
		strlcpy(dmv->name, name, sizeof(dmv->name));
	if (uuid)
		strncpy(dmv->uuid, uuid, sizeof(dmv->uuid));

	return dmv;
}

/*
 * Freed device entry.
 */
static int
dm_dev_free(dm_dev_t *dmv)
{
	KKASSERT(dmv != NULL);

	if (dmv->diskp != NULL)
		kfree(dmv->diskp, M_DM);

	kfree(dmv, M_DM);

	return 0;
}

void
dm_dev_busy(dm_dev_t *dmv)
{
	lockmgr(&dmv->dev_mtx, LK_EXCLUSIVE);
	dmv->ref_cnt++;
	lockmgr(&dmv->dev_mtx, LK_RELEASE);
}

void
dm_dev_unbusy(dm_dev_t *dmv)
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
	devfs_clone_bitmap_init(&dm_minor_bitmap);

	memset(dummy_uuid, 0, sizeof(dummy_uuid));
	return 0;
}

/*
 * Destroy all devices created in device-mapper. Remove all tables
 * free all allocated memmory.
 */
int
dm_dev_uninit(void)
{
	/* Force removal of all devices */
	dm_dev_remove_all(0);

	lockuninit(&dm_dev_mutex);
	return 0;
}
