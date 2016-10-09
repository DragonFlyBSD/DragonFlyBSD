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

#include <sys/disk.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/nlookup.h>

#include <dev/disk/dm/dm.h>

static TAILQ_HEAD(, dm_pdev) dm_pdev_list;

static struct lock dm_pdev_mutex;

static dm_pdev_t *dm_pdev_alloc(const char *);
static int dm_pdev_free(dm_pdev_t *);
static dm_pdev_t *dm_pdev_lookup_name(const char *);

/*
 * Find used pdev with name == dm_pdev_name.
 * needs to be called with the dm_pdev_mutex held.
 */
static dm_pdev_t *
dm_pdev_lookup_name(const char *dm_pdev_name)
{
	dm_pdev_t *dmp;

	KKASSERT(dm_pdev_name != NULL);

	TAILQ_FOREACH(dmp, &dm_pdev_list, next_pdev) {
		if (strcmp(dm_pdev_name, dmp->name) == 0)
			return dmp;
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
 * If entry already exists in global TAILQ I will only increment
 * reference counter.
 */
dm_pdev_t *
dm_pdev_insert(const char *dev_name)
{
	dm_pdev_t *dmp;
	struct vattr va;
	int error;

	KKASSERT(dev_name != NULL);

	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);
	dmp = dm_pdev_lookup_name(dev_name);

	if (dmp != NULL) {
		dmp->ref_cnt++;
		dmdebug("pdev %s already in tree\n", dev_name);
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		return dmp;
	}

	if ((dmp = dm_pdev_alloc(dev_name)) == NULL) {
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		return NULL;
	}

	error = dm_dk_lookup(dev_name, &dmp->pdev_vnode);
	if (error) {
		dmdebug("Lookup on %s failed with error %d!\n",
		    dev_name, error);
		dm_pdev_free(dmp);
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		return NULL;
	}
	dmp->ref_cnt = 1;

	if (dm_pdev_get_vattr(dmp, &va) == -1) {
		dmdebug("getattr on %s failed\n", dev_name);
		dm_pdev_free(dmp);
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		return NULL;
	}
	ksnprintf(dmp->udev_name, sizeof(dmp->udev_name),
		"%d:%d", va.va_rmajor, va.va_rminor);
	dmp->udev = dm_pdev_get_udev(dmp);

	/*
	 * Get us the partinfo from the underlying device, it's needed for
	 * dumps.
	 */
	bzero(&dmp->pdev_pinfo, sizeof(dmp->pdev_pinfo));
	error = dev_dioctl(dmp->pdev_vnode->v_rdev, DIOCGPART,
	    (void *)&dmp->pdev_pinfo, 0, proc0.p_ucred, NULL, NULL);
	if (!error) {
		struct partinfo *dpart = &dmp->pdev_pinfo;
		dmdebug("DIOCGPART offset=%ju size=%ju blocks=%ju blksize=%d\n",
			dpart->media_offset,
			dpart->media_size,
			dpart->media_blocks,
			dpart->media_blksize);
	} else {
		kprintf("dmp_pdev_insert DIOCGPART failed %d\n", error);
	}

	TAILQ_INSERT_TAIL(&dm_pdev_list, dmp, next_pdev);
	lockmgr(&dm_pdev_mutex, LK_RELEASE);

	dmdebug("pdev %s %s 0x%016jx\n",
		dmp->name, dmp->udev_name, (uintmax_t)dmp->udev);

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

	dmp = kmalloc(sizeof(*dmp), M_DM, M_WAITOK | M_ZERO);
	if (dmp == NULL)
		return NULL;

	if (name)
		strlcpy(dmp->name, name, DM_MAX_DEV_NAME);

	return dmp;
}

/*
 * Destroy allocated dm_pdev.
 */
static int
dm_pdev_free(dm_pdev_t *dmp)
{
	int err;

	KKASSERT(dmp != NULL);

	if (dmp->pdev_vnode != NULL) {
		err = vn_close(dmp->pdev_vnode, FREAD | FWRITE, NULL);
		if (err != 0) {
			kfree(dmp, M_DM);
			return err;
		}
	}
	kfree(dmp, M_DM);

	return 0;
}

/*
 * This funcion is called from targets' destroy() handler.
 * When I'm removing device from list, I have to decrement
 * reference counter. If reference counter is 0 I will remove
 * dmp from global list and from device list to. And I will CLOSE
 * dmp vnode too.
 */
/*
 * Decrement pdev reference counter if 0 remove it.
 */
int
dm_pdev_decr(dm_pdev_t *dmp)
{
	KKASSERT(dmp != NULL);
	/*
	 * If this was last reference remove dmp from
	 * global list also.
	 */
	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);

	if (--dmp->ref_cnt == 0) {
		TAILQ_REMOVE(&dm_pdev_list, dmp, next_pdev);
		lockmgr(&dm_pdev_mutex, LK_RELEASE);
		dm_pdev_free(dmp);
		return 0;
	}
	lockmgr(&dm_pdev_mutex, LK_RELEASE);
	return 0;
}

uint64_t
dm_pdev_get_udev(dm_pdev_t *dmp)
{
	struct vattr va;
	int ret;

	if (dmp->pdev_vnode == NULL)
		return (uint64_t)-1;

	ret = dm_pdev_get_vattr(dmp, &va);
	if (ret)
		return (uint64_t)-1;

	ret = makeudev(va.va_rmajor, va.va_rminor);

	return ret;
}

int
dm_pdev_get_vattr(dm_pdev_t *dmp, struct vattr *vap)
{
	int ret;

	if (dmp->pdev_vnode == NULL)
		return -1;

	KKASSERT(vap);
	ret = VOP_GETATTR(dmp->pdev_vnode, vap);
	if (ret)
		return -1;

	return 0;
}

/*
 * Initialize pdev subsystem.
 */
int
dm_pdev_init(void)
{
	TAILQ_INIT(&dm_pdev_list);	/* initialize global pdev list */
	lockinit(&dm_pdev_mutex, "dmpdev", 0, LK_CANRECURSE);

	return 0;
}

/*
 * Destroy all existing pdev's in device-mapper.
 */
int
dm_pdev_uninit(void)
{
	dm_pdev_t *dmp;

	lockmgr(&dm_pdev_mutex, LK_EXCLUSIVE);

	while ((dmp = TAILQ_FIRST(&dm_pdev_list)) != NULL) {
		TAILQ_REMOVE(&dm_pdev_list, dmp, next_pdev);
		dm_pdev_free(dmp);
	}
	KKASSERT(TAILQ_EMPTY(&dm_pdev_list));

	lockmgr(&dm_pdev_mutex, LK_RELEASE);

	lockuninit(&dm_pdev_mutex);
	return 0;
}
