/*        $NetBSD: dm_pdev.c,v 1.6 2010/01/04 00:19:08 haad Exp $      */

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

#include <sys/types.h>
#include <sys/param.h>

#include <sys/disk.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/nlookup.h>

#include <dev/disk/dm/dm.h>

SLIST_HEAD(dm_pdevs, dm_pdev) dm_pdev_list;

struct lock dm_pdev_mutex;

static dm_pdev_t *dm_pdev_alloc(const char *);
static int dm_pdev_rem(dm_pdev_t *);
static dm_pdev_t *dm_pdev_lookup_name(const char *);

/*
 * Find used pdev with name == dm_pdev_name.
 * needs to be called with the dm_pdev_mutex held.
 */
static dm_pdev_t *
dm_pdev_lookup_name(const char *dm_pdev_name)
{
	dm_pdev_t *dm_pdev;

	KKASSERT(dm_pdev_name != NULL);

	SLIST_FOREACH(dm_pdev, &dm_pdev_list, next_pdev) {
		if (strcmp(dm_pdev_name, dm_pdev->name) == 0)
			return dm_pdev;
	}

	return NULL;
}

static int
dm_dk_lookup(const char *dev_name, struct vnode **vpp)
{
	struct nlookupdata nd;
	int error;

	error = nlookup_init(&nd, dev_name, UIO_SYSSPACE, NLC_FOLLOW);
	if (error)
		return error;

	error = vn_open(&nd, NULL, FREAD|FWRITE, 0);
	if (error) {
		nlookup_done(&nd);
		return error;
	}

	*vpp = nd.nl_open_vp;
	nd.nl_open_vp = NULL;
	nlookup_done(&nd);

	return 0;
}

/*
 * Since dm can have arbitrary stacking on any number of disks and any dm
 * volume is at least stacked onto another disk, we need to adjust the
 * dumping offset (which is a raw offset from the beginning of the lowest
 * physical disk) taking into account the offset of the underlying device
 * which in turn takes into account the offset below it, etc.
 *
 * This function adjusts the dumping offset that is passed to the next
 * dev_ddump() so it is correct for that underlying device.
 */
off_t
dm_pdev_correct_dump_offset(dm_pdev_t *pdev, off_t offset)
{
	off_t noffset;

	noffset = pdev->pdev_pinfo.reserved_blocks +
	    pdev->pdev_pinfo.media_offset / pdev->pdev_pinfo.media_blksize;
	noffset *= DEV_BSIZE;
	noffset += offset;

	return noffset;
}

/*
 * Create entry for device with name dev_name and open vnode for it.
 * If entry already exists in global SLIST I will only increment
 * reference counter.
 */
dm_pdev_t *
dm_pdev_insert(const char *dev_name)
{
	dm_pdev_t *dmp;
	int error;

	KKASSERT(dev_name != NULL);

	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);
	dmp = dm_pdev_lookup_name(dev_name);

	if (dmp != NULL) {
		dmp->ref_cnt++;
		aprint_debug("dmp_pdev_insert pdev %s already in tree\n", dev_name);
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		return dmp;
	}
	lockmgr(&dm_pdev_mutex, LK_RELEASE);

	if ((dmp = dm_pdev_alloc(dev_name)) == NULL)
		return NULL;

	error = dm_dk_lookup(dev_name, &dmp->pdev_vnode);
	if (error) {
		aprint_debug("dk_lookup on device: %s failed with error %d!\n",
		    dev_name, error);
		kfree(dmp, M_DM);
		return NULL;
	}
	dmp->ref_cnt = 1;

	/*
	 * Get us the partinfo from the underlying device, it's needed for
	 * dumps.
	 */
	bzero(&dmp->pdev_pinfo, sizeof(dmp->pdev_pinfo));
	error = dev_dioctl(dmp->pdev_vnode->v_rdev, DIOCGPART,
	    (void *)&dmp->pdev_pinfo, 0, proc0.p_ucred, NULL, NULL);

	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);
	SLIST_INSERT_HEAD(&dm_pdev_list, dmp, next_pdev);
	lockmgr(&dm_pdev_mutex, LK_RELEASE);

	return dmp;
}

/*
 * Allocat new pdev structure if is not already present and
 * set name.
 */
static dm_pdev_t *
dm_pdev_alloc(const char *name)
{
	dm_pdev_t *dmp;

	if ((dmp = kmalloc(sizeof(dm_pdev_t), M_DM, M_WAITOK | M_ZERO)) == NULL)
		return NULL;

	strlcpy(dmp->name, name, MAX_DEV_NAME);

	dmp->ref_cnt = 0;
	dmp->pdev_vnode = NULL;

	return dmp;
}
/*
 * Destroy allocated dm_pdev.
 */
static int
dm_pdev_rem(dm_pdev_t * dmp)
{
	int err;

	KKASSERT(dmp != NULL);

	if (dmp->pdev_vnode != NULL) {
		err = vn_close(dmp->pdev_vnode, FREAD | FWRITE, NULL);
		if (err != 0)
			return err;
	}
	kfree(dmp, M_DM);
	dmp = NULL;

	return 0;
}

/*
 * This funcion is called from dm_dev_remove_ioctl.
 * When I'm removing device from list, I have to decrement
 * reference counter. If reference counter is 0 I will remove
 * dmp from global list and from device list to. And I will CLOSE
 * dmp vnode too.
 */

/*
 * Decrement pdev reference counter if 0 remove it.
 */
int
dm_pdev_decr(dm_pdev_t * dmp)
{
	KKASSERT(dmp != NULL);
	/*
	 * If this was last reference remove dmp from
	 * global list also.
	 */
	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);

	if (--dmp->ref_cnt == 0) {
		SLIST_REMOVE(&dm_pdev_list, dmp, dm_pdev, next_pdev);
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		dm_pdev_rem(dmp);
		return 0;
	}
	lockmgr(&dm_pdev_mutex, LK_RELEASE);
	return 0;
}

/*
 * Initialize pdev subsystem.
 */
int
dm_pdev_init(void)
{
	SLIST_INIT(&dm_pdev_list);	/* initialize global pdev list */
	lockinit(&dm_pdev_mutex, "dmpdev", 0, LK_CANRECURSE);

	return 0;
}

/*
 * Destroy all existing pdev's in device-mapper.
 */
int
dm_pdev_uninit(void)
{
	dm_pdev_t *dm_pdev;

	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);
	while (!SLIST_EMPTY(&dm_pdev_list)) {	/* List Deletion. */

		dm_pdev = SLIST_FIRST(&dm_pdev_list);

		SLIST_REMOVE_HEAD(&dm_pdev_list, next_pdev);

		dm_pdev_rem(dm_pdev);
	}
	lockmgr(&dm_pdev_mutex, LK_RELEASE);

	lockuninit(&dm_pdev_mutex);
	return 0;
}
