/*
 * Copyright (c) 2020 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2020 The DragonFly Project
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/objcache.h>
#include <sys/lock.h>

#include "hammer2.h"

#define hprintf(X, ...)	kprintf("hammer2_ondisk: " X, ## __VA_ARGS__)

static int
hammer2_lookup_device(const char *path, int rootmount, struct vnode **devvp)
{
	struct vnode *vp = NULL;
	struct nlookupdata nd;
	int error = 0;

	KKASSERT(path);
	KKASSERT(*path != '\0');

	if (rootmount) {
		error = bdevvp(kgetdiskbyname(path), &vp);
		if (error)
			hprintf("cannot find %s %d\n", path, error);
	} else {
		error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error == 0)
			error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
		if (error)
			hprintf("failed to nlookup %s %d\n", path, error);
		nlookup_done(&nd);
	}

	if (error == 0) {
		KKASSERT(vp);
		if (!vn_isdisk(vp, &error)) {
			KKASSERT(error);
			hprintf("%s not a block device %d\n", path, error);
		}
	}

	if (error && vp) {
		vrele(vp);
		vp = NULL;
	}

	*devvp = vp;
	return error;
}

int
hammer2_open_devvp(const hammer2_devvp_list_t *devvpl, int ronly)
{
	hammer2_devvp_t *e;
	struct vnode *devvp;
	const char *path;
	int count, error;

	TAILQ_FOREACH(e, devvpl, entry) {
		devvp = e->devvp;
		path = e->path;
		KKASSERT(devvp);
		count = vcount(devvp);
		if (count > 0) {
			hprintf("%s already has %d references\n", path, count);
			return EBUSY;
		}
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(devvp, V_SAVE, 0, 0);
		if (error == 0) {
			KKASSERT(!e->open);
			error = VOP_OPEN(devvp, (ronly ? FREAD : FREAD|FWRITE),
					 FSCRED, NULL);
			if (error == 0)
				e->open = 1;
			else
				hprintf("failed to open %s %d\n", path, error);
		}
		vn_unlock(devvp);
		if (error)
			return error;
		KKASSERT(e->open);
	}

	return 0;
}

int
hammer2_close_devvp(const hammer2_devvp_list_t *devvpl, int ronly)
{
	hammer2_devvp_t *e;
	struct vnode *devvp;

	TAILQ_FOREACH(e, devvpl, entry) {
		devvp = e->devvp;
		KKASSERT(devvp);
		if (e->open) {
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
			VOP_CLOSE(devvp, (ronly ? FREAD : FREAD|FWRITE), NULL);
			vn_unlock(devvp);
			e->open = 0;
		}
	}

	return 0;
}

int
hammer2_init_devvp(const char *blkdevs, int rootmount,
		   hammer2_devvp_list_t *devvpl)
{
	hammer2_devvp_t *e;
	struct vnode *devvp;
	const char *p;
	char *path;
	int i, error = 0;

	KKASSERT(TAILQ_EMPTY(devvpl));
	KKASSERT(blkdevs); /* could be empty string */
	p = blkdevs;

	path = objcache_get(namei_oc, M_WAITOK);
	while (1) {
		strcpy(path, "");
		if (*p != '/') {
			strcpy(path, "/dev/"); /* relative path */
		}
		/* scan beyond "/dev/" */
		for (i = strlen(path); i < MAXPATHLEN-1; ++i) {
			if (*p == '\0') {
				break;
			} else if (*p == ':') {
				p++;
				break;
			} else {
				path[i] = *p;
				p++;
			}
		}
		path[i] = '\0';
		/* path shorter than "/dev/" means invalid or done */
		if (strlen(path) <= strlen("/dev/")) {
			if (strlen(p)) {
				hprintf("ignore incomplete path %s\n", path);
				continue;
			} else {
				/* end of string */
				KKASSERT(*p == '\0');
				break;
			}
		}
		/* lookup path from above */
		devvp = NULL;
		error = hammer2_lookup_device(path, rootmount, &devvp);
		if (error) {
			KKASSERT(!devvp);
			hprintf("failed to lookup %s %d\n", path, error);
			break;
		}
		KKASSERT(devvp);
		e = kmalloc(sizeof(*e), M_HAMMER2, M_WAITOK | M_ZERO);
		e->devvp = devvp;
		e->path = kstrdup(path, M_HAMMER2);
		TAILQ_INSERT_TAIL(devvpl, e, entry);
	}
	objcache_put(namei_oc, path);

	return error;
}

void
hammer2_cleanup_devvp(hammer2_devvp_list_t *devvpl)
{
	hammer2_devvp_t *e;

	while (!TAILQ_EMPTY(devvpl)) {
		e = TAILQ_FIRST(devvpl);
		TAILQ_REMOVE(devvpl, e, entry);
		/* devvp */
		KKASSERT(e->devvp);
		if (e->devvp->v_rdev)
			e->devvp->v_rdev->si_mountpoint = NULL;
		vrele(e->devvp);
		e->devvp = NULL;
		/* path */
		KKASSERT(e->path);
		kfree(e->path, M_HAMMER2);
		e->path = NULL;
		kfree(e, M_HAMMER2);
	}
}

static int
hammer2_verify_volumes_common(const hammer2_volume_t *volumes)
{
	const hammer2_volume_t *vol;
	struct partinfo part;
	const char *path;
	int i;

	for (i = 0; i < HAMMER2_MAX_VOLUMES; ++i) {
		vol = &volumes[i];
		if (vol->id == -1)
			continue;
		path = vol->dev->path;
		/* check volume fields are initialized */
		if (!vol->dev->devvp) {
			hprintf("%s has NULL devvp\n", path);
			return EINVAL;
		}
		if (vol->offset == (hammer2_off_t)-1) {
			hprintf("%s has bad offset 0x%016jx\n", path,
				(intmax_t)vol->offset);
			return EINVAL;
		}
		if (vol->size == (hammer2_off_t)-1) {
			hprintf("%s has bad size 0x%016jx\n", path,
				(intmax_t)vol->size);
			return EINVAL;
		}
		/* check volume size vs block device size */
		if (VOP_IOCTL(vol->dev->devvp, DIOCGPART, (void*)&part, 0,
			      curthread->td_ucred , NULL) == 0) {
			if (vol->size > part.media_size) {
				hprintf("%s's size 0x%016jx exceeds device size "
					"0x%016jx\n", path, (intmax_t)vol->size,
					part.media_size);
				return EINVAL;
			}
		}
	}

	return 0;
}

static int
hammer2_verify_volumes_1(const hammer2_volume_t *volumes,
		         const hammer2_volume_data_t *rootvoldata)
{
	const hammer2_volume_t *vol;
	hammer2_off_t off;
	const char *path;
	int i, nvolumes = 0;

	/* check initialized volume count */
	for (i = 0; i < HAMMER2_MAX_VOLUMES; ++i) {
		vol = &volumes[i];
		if (vol->id != -1)
			nvolumes++;
	}
	if (nvolumes != 1) {
		hprintf("only 1 volume supported\n");
		return EINVAL;
	}

	/* check volume header */
	if (rootvoldata->volu_id) {
		hprintf("volume id %d must be 0\n", rootvoldata->volu_id);
		return EINVAL;
	}
	if (rootvoldata->nvolumes) {
		hprintf("volume count %d must be 0\n", rootvoldata->nvolumes);
		return EINVAL;
	}
	if (rootvoldata->total_size) {
		hprintf("total size 0x%016jx must be 0\n",
			(intmax_t)rootvoldata->total_size);
		return EINVAL;
	}
	for (i = 0; i < HAMMER2_MAX_VOLUMES; ++i) {
		off = rootvoldata->volu_loff[i];
		if (off) {
			hprintf("volume offset[%d] 0x%016jx must be 0\n", i,
				(intmax_t)off);
			return EINVAL;
		}
	}

	/* check volume */
	vol = &volumes[0];
	path = vol->dev->path;
	if (vol->id) {
		hprintf("%s has non zero id %d\n", path, vol->id);
		return EINVAL;
	}
	if (vol->offset) {
		hprintf("%s has non zero offset 0x%016jx\n", path,
			(intmax_t)vol->offset);
		return EINVAL;
	}
	if (vol->size & HAMMER2_VOLUME_ALIGNMASK64) {
		hprintf("%s's size is not 0x%016jx aligned\n", path,
			(intmax_t)HAMMER2_VOLUME_ALIGN);
		return EINVAL;
	}

	return 0;
}

static int
hammer2_verify_volumes_2(const hammer2_volume_t *volumes,
		         const hammer2_volume_data_t *rootvoldata)
{
	const hammer2_volume_t *vol;
	hammer2_off_t off, total_size = 0;
	const char *path;
	int i, nvolumes = 0;

	/* check initialized volume count */
	for (i = 0; i < HAMMER2_MAX_VOLUMES; ++i) {
		vol = &volumes[i];
		if (vol->id != -1) {
			nvolumes++;
			total_size += vol->size;
		}
	}

	/* check volume header */
	if (rootvoldata->volu_id != HAMMER2_ROOT_VOLUME) {
		hprintf("volume id %d must be %d\n", rootvoldata->volu_id,
			HAMMER2_ROOT_VOLUME);
		return EINVAL;
	}
	if (rootvoldata->nvolumes != nvolumes) {
		hprintf("volume header requires %d devices, %d specified\n",
			rootvoldata->nvolumes, nvolumes);
		return EINVAL;
	}
	if (rootvoldata->total_size != total_size) {
		hprintf("total size 0x%016jx does not equal sum of volumes 0x%016jx\n",
			rootvoldata->total_size, total_size);
		return EINVAL;
	}
	for (i = 0; i < nvolumes; ++i) {
		off = rootvoldata->volu_loff[i];
		if (off == (hammer2_off_t)-1) {
			hprintf("volume offset[%d] 0x%016jx must not be -1\n",
				i, (intmax_t)off);
			return EINVAL;
		}
	}
	for (i = nvolumes; i < HAMMER2_MAX_VOLUMES; ++i) {
		off = rootvoldata->volu_loff[i];
		if (off != (hammer2_off_t)-1) {
			hprintf("volume offset[%d] 0x%016jx must be -1\n",
				i, (intmax_t)off);
			return EINVAL;
		}
	}

	/* check volumes */
	for (i = 0; i < HAMMER2_MAX_VOLUMES; ++i) {
		vol = &volumes[i];
		if (vol->id == -1)
			continue;
		path = vol->dev->path;
		/* check offset */
		if (vol->offset & HAMMER2_FREEMAP_LEVEL1_MASK) {
			hprintf("%s's offset 0x%016jx not 0x%016jx aligned\n",
				path, (intmax_t)vol->offset,
				HAMMER2_FREEMAP_LEVEL1_SIZE);
			return EINVAL;
		}
		/* check vs previous volume */
		if (i) {
			if (vol->id <= (vol-1)->id) {
				hprintf("%s has inconsistent id %d\n", path,
					vol->id);
				return EINVAL;
			}
			if (vol->offset != (vol-1)->offset + (vol-1)->size) {
				hprintf("%s has inconsistent offset 0x%016jx\n",
					path, (intmax_t)vol->offset);
				return EINVAL;
			}
		} else { /* first */
			if (vol->offset) {
				hprintf("%s has non zero offset 0x%016jx\n",
					path, (intmax_t)vol->offset);
				return EINVAL;
			}
		}
		/* check size for non-last and last volumes */
		if (i != rootvoldata->nvolumes - 1) {
			if (vol->size < HAMMER2_FREEMAP_LEVEL1_SIZE) {
				hprintf("%s's size must be >= 0x%016jx\n", path,
					(intmax_t)HAMMER2_FREEMAP_LEVEL1_SIZE);
				return EINVAL;
			}
			if (vol->size & HAMMER2_FREEMAP_LEVEL1_MASK) {
				hprintf("%s's size is not 0x%016jx aligned\n",
					path,
					(intmax_t)HAMMER2_FREEMAP_LEVEL1_SIZE);
				return EINVAL;
			}
		} else { /* last */
			if (vol->size & HAMMER2_VOLUME_ALIGNMASK64) {
				hprintf("%s's size is not 0x%016jx aligned\n",
					path,
					(intmax_t)HAMMER2_VOLUME_ALIGN);
				return EINVAL;
			}
		}
	}

	return 0;
}

static int
hammer2_verify_volumes(const hammer2_volume_t *volumes,
		       const hammer2_volume_data_t *rootvoldata)
{
	int error;

	error = hammer2_verify_volumes_common(volumes);
	if (error)
		return error;

	if (rootvoldata->version >= HAMMER2_VOL_VERSION_MULTI_VOLUMES)
		return hammer2_verify_volumes_2(volumes, rootvoldata);
	else
		return hammer2_verify_volumes_1(volumes, rootvoldata);
}

/*
 * Returns zone# of returned volume header or < 0 on failure.
 */
static int
hammer2_read_volume_header(struct vnode *devvp, const char *path,
			   hammer2_volume_data_t *voldata)
{
	hammer2_volume_data_t *vd;
	struct buf *bp = NULL;
	hammer2_crc32_t crc0, crc1;
	int zone = -1;
	int i;

	/*
	 * There are up to 4 copies of the volume header (syncs iterate
	 * between them so there is no single master).  We don't trust the
	 * volu_size field so we don't know precisely how large the filesystem
	 * is, so depend on the OS to return an error if we go beyond the
	 * block device's EOF.
	 */
	for (i = 0; i < HAMMER2_NUM_VOLHDRS; ++i) {
		if (bread(devvp, i * HAMMER2_ZONE_BYTES64, HAMMER2_VOLUME_BYTES,
			  &bp)) {
			brelse(bp);
			bp = NULL;
			continue;
		}

		vd = (struct hammer2_volume_data *)bp->b_data;
		/* verify volume header magic */
		if ((vd->magic != HAMMER2_VOLUME_ID_HBO) &&
		    (vd->magic != HAMMER2_VOLUME_ID_ABO)) {
			hprintf("%s #%d: bad magic\n", path, i);
			brelse(bp);
			bp = NULL;
			continue;
		}

		if (vd->magic == HAMMER2_VOLUME_ID_ABO) {
			/* XXX: Reversed-endianness filesystem */
			hprintf("%s #%d: reverse-endian filesystem detected\n",
				path, i);
			brelse(bp);
			bp = NULL;
			continue;
		}

		/* verify volume header CRC's */
		crc0 = vd->icrc_sects[HAMMER2_VOL_ICRC_SECT0];
		crc1 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRC0_OFF,
				      HAMMER2_VOLUME_ICRC0_SIZE);
		if (crc0 != crc1) {
			hprintf("%s #%d: volume header crc mismatch sect0 %08x/%08x\n",
				path, i, crc0, crc1);
			brelse(bp);
			bp = NULL;
			continue;
		}
		crc0 = vd->icrc_sects[HAMMER2_VOL_ICRC_SECT1];
		crc1 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRC1_OFF,
				      HAMMER2_VOLUME_ICRC1_SIZE);
		if (crc0 != crc1) {
			hprintf("%s #%d: volume header crc mismatch sect1 %08x/%08x\n",
				path, i, crc0, crc1);
			brelse(bp);
			bp = NULL;
			continue;
		}
		crc0 = vd->icrc_volheader;
		crc1 = hammer2_icrc32(bp->b_data + HAMMER2_VOLUME_ICRCVH_OFF,
				      HAMMER2_VOLUME_ICRCVH_SIZE);
		if (crc0 != crc1) {
			hprintf("%s #%d: volume header crc mismatch vh %08x/%08x\n",
				path, i, crc0, crc1);
			brelse(bp);
			bp = NULL;
			continue;
		}

		if (zone == -1 || voldata->mirror_tid < vd->mirror_tid) {
			*voldata = *vd;
			zone = i;
		}
		brelse(bp);
		bp = NULL;
	}

	if (zone == -1) {
		hprintf("%s has no valid volume headers\n", path);
		return -EINVAL;
	}
	return zone;
}

static void
hammer2_print_uuid_mismatch(uuid_t *uuid1, uuid_t *uuid2, const char *id)
{
	char buf1[64], buf2[64];

	snprintf_uuid(buf1, sizeof(buf1), uuid1);
	snprintf_uuid(buf2, sizeof(buf2), uuid2);

	hprintf("%s uuid mismatch %s vs %s\n", id, buf1, buf2);
}

int
hammer2_init_volumes(struct mount *mp, const hammer2_devvp_list_t *devvpl,
		     hammer2_volume_t *volumes,
		     hammer2_volume_data_t *rootvoldata,
		     int *rootvolzone,
		     struct vnode **rootvoldevvp)
{
	hammer2_devvp_t *e;
	hammer2_volume_data_t *voldata;
	hammer2_volume_t *vol;
	struct vnode *devvp;
	const char *path;
	uuid_t fsid, fstype;
	int i, zone, error = 0, version = -1, nvolumes = 0;

	for (i = 0; i < HAMMER2_MAX_VOLUMES; ++i) {
		vol = &volumes[i];
		vol->dev = NULL;
		vol->id = -1;
		vol->offset = (hammer2_off_t)-1;
		vol->size = (hammer2_off_t)-1;
	}

	voldata = kmalloc(sizeof(*voldata), M_HAMMER2, M_WAITOK | M_ZERO);
	bzero(&fsid, sizeof(fsid));
	bzero(&fstype, sizeof(fstype));
	bzero(rootvoldata, sizeof(*rootvoldata));

	TAILQ_FOREACH(e, devvpl, entry) {
		devvp = e->devvp;
		path = e->path;
		KKASSERT(devvp);

		/* returns negative error or positive zone# */
		error = hammer2_read_volume_header(devvp, path, voldata);
		if (error < 0) {
			hprintf("failed to read %s's volume header\n", path);
			error = -error;
			goto done;
		}
		zone = error;
		error = 0; /* reset error */

		if (voldata->volu_id >= HAMMER2_MAX_VOLUMES) {
			hprintf("%s has bad volume id %d\n", path,
				voldata->volu_id);
			error = EINVAL;
			goto done;
		}
		vol = &volumes[voldata->volu_id];
		if (vol->id != -1) {
			hprintf("volume id %d already initialized\n",
				voldata->volu_id);
			error = EINVAL;
			goto done;
		}
		/* all headers must have the same version, nvolumes and uuid */
		if (version == -1) {
			version = voldata->version;
			nvolumes = voldata->nvolumes;
			fsid = voldata->fsid;
			fstype = voldata->fstype;
		} else {
			if (version != (int)voldata->version) {
				hprintf("volume version mismatch %d vs %d\n",
					version, (int)voldata->version);
				error = ENXIO;
				goto done;
			}
			if (nvolumes != voldata->nvolumes) {
				hprintf("volume count mismatch %d vs %d\n",
					nvolumes, voldata->nvolumes);
				error = ENXIO;
				goto done;
			}
			if (bcmp(&fsid, &voldata->fsid, sizeof(fsid))) {
				hammer2_print_uuid_mismatch(&fsid,
							    &voldata->fsid, "fsid");
				error = ENXIO;
				goto done;
			}
			if (bcmp(&fstype, &voldata->fstype, sizeof(fstype))) {
				hammer2_print_uuid_mismatch(&fstype,
							    &voldata->fstype, "fstype");
				error = ENXIO;
				goto done;
			}
		}
		if (version < HAMMER2_VOL_VERSION_MIN ||
		    version > HAMMER2_VOL_VERSION_WIP) {
			hprintf("bad volume version %d\n", version);
			error = EINVAL;
			goto done;
		}
		/* all per-volume tests passed */
		vol->dev = e;
		vol->id = voldata->volu_id;
		vol->offset = voldata->volu_loff[vol->id];
		vol->size = voldata->volu_size;
		if (vol->id == HAMMER2_ROOT_VOLUME) {
			bcopy(voldata, rootvoldata, sizeof(*rootvoldata));
			*rootvolzone = zone;
			KKASSERT(*rootvoldevvp == NULL);
			*rootvoldevvp = e->devvp;
		}
		devvp->v_rdev->si_mountpoint = mp;
		hprintf("\"%s\" zone=%d id=%d offset=0x%016jx size=0x%016jx\n",
			path, zone, vol->id, (intmax_t)vol->offset,
			(intmax_t)vol->size);
	}
done:
	if (!error) {
		if (!rootvoldata->version) {
			hprintf("root volume not found\n");
			error = EINVAL;
		}
		if (!error)
			error = hammer2_verify_volumes(volumes, rootvoldata);
	}
	kfree(voldata, M_HAMMER2);

	return error;
}

hammer2_volume_t*
hammer2_get_volume(hammer2_dev_t *hmp, hammer2_off_t offset)
{
	hammer2_volume_t *vol, *ret = NULL;
	int i;

	offset &= ~HAMMER2_OFF_MASK_RADIX;

	/* locking is unneeded until volume-add support */
	//hammer2_voldata_lock(hmp);
	/* do binary search if users really use this many supported volumes */
	for (i = 0; i < hmp->nvolumes; ++i) {
		vol = &hmp->volumes[i];
		if ((offset >= vol->offset) &&
		    (offset < vol->offset + vol->size)) {
			ret = vol;
			break;
		}
	}
	//hammer2_voldata_unlock(hmp);

	if (!ret)
		panic("no volume for offset 0x%016jx", (intmax_t)offset);

	KKASSERT(ret);
	KKASSERT(ret->dev);
	KKASSERT(ret->dev->devvp);
	KKASSERT(ret->dev->path);

	return ret;
}
