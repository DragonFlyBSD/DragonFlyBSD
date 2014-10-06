/*
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Hamsik.
 *
 * This code is further derived from software contributed to the
 * DragonFly project by Alex Hornung and Matthew Dillon
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
 *
 * $NetBSD: dm_target_stripe.c,v 1.9 2010/01/04 00:14:41 haad Exp $
 */

/*
 * This file implements initial version of device-mapper stripe target.
 *
 * DragonFly changes: Increase to an unlimited number of stripes
 */
#include <sys/types.h>
#include <sys/param.h>

#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include <dev/disk/dm/dm.h>
MALLOC_DEFINE(M_DMSTRIPE, "dm_stripe", "Device Mapper Target Stripe");

static void dm_target_stripe_destroy_config(dm_target_stripe_config_t *tsc);

/*
 * Init function called from dm_table_load_ioctl.
 *
 * Example line sent to dm from lvm tools when using striped target.
 * start length striped #stripes chunk_size device1 offset1 ... deviceN offsetN
 * 0 65536 striped 2 512 /dev/hda 0 /dev/hdb 0
 */
static int
dm_target_stripe_init(dm_dev_t *dmv, void **target_config, char *params)
{
	dm_target_stripe_config_t *tsc;
	int n;
	char *ap;

	if (params == NULL)
		return EINVAL;

	/*
	 * nstripes
	 */
	ap = strsep(&params, " \t");
	if (ap == NULL)
		return EINVAL;
	n = (int)atoi64(ap);
	if (n < 0 || n > MAX_STRIPES) {
		kprintf("dm: Error %d stripes not supported (%d max)\n",
			n, MAX_STRIPES);
		return ENOTSUP;
	}

	tsc = kmalloc(sizeof(dm_target_stripe_config_t),
		      M_DMSTRIPE, M_WAITOK | M_ZERO);
	tsc->stripe_num = n;

	ap = strsep(&params, " \t");
	if (ap == NULL) {
		dm_target_stripe_destroy_config(tsc);
		return EINVAL;
	}
	tsc->stripe_chunksize = atoi64(ap);
	if (tsc->stripe_chunksize < 1 ||
	    tsc->stripe_chunksize * DEV_BSIZE > MAXPHYS) {
		kprintf("dm: Error unsupported chunk size %jdKB\n",
			(intmax_t)tsc->stripe_chunksize * DEV_BSIZE / 1024);
		dm_target_stripe_destroy_config(tsc);
		return EINVAL;
	}

	/*
	 * Parse the devices
	 */

	kprintf("dm: Stripe %d devices chunk size %dKB\n",
		(int)tsc->stripe_num,
		(int)tsc->stripe_chunksize
	);

	for (n = 0; n < tsc->stripe_num; ++n) {
		ap = strsep(&params, " \t");
		if (ap == NULL)
			break;
		tsc->stripe_devs[n].pdev = dm_pdev_insert(ap);
		if (tsc->stripe_devs[n].pdev == NULL)
			break;
		ap = strsep(&params, " \t");
		if (ap == NULL)
			break;
		tsc->stripe_devs[n].offset = atoi64(ap);
	}
	if (n != tsc->stripe_num) {
		dm_target_stripe_destroy_config(tsc);
		return (ENOENT);
	}

	*target_config = tsc;

	dmv->dev_type = DM_STRIPE_DEV;

	return 0;
}

/*
 * Status routine called to get params string.
 */
static char *
dm_target_stripe_status(void *target_config)
{
	dm_target_stripe_config_t *tsc;
	char *params;
	char *ptr;
	size_t len;
	size_t nlen;
	int n;

	tsc = target_config;

	/* caller expects use of M_DM for returned params */
	nlen = DM_MAX_PARAMS_SIZE;
	params = kmalloc(nlen, M_DM, M_WAITOK);
	ptr = params;

	ksnprintf(ptr, nlen, "%d %jd",
		  tsc->stripe_num, (intmax_t)tsc->stripe_chunksize);
	len = strlen(params);
	ptr += len;
	nlen -= len;

	for (n = 0; n < tsc->stripe_num; ++n) {
		ksnprintf(ptr, nlen, " %s %jd",
			  tsc->stripe_devs[n].pdev->name,
			  (intmax_t)tsc->stripe_devs[n].offset);
		len = strlen(ptr);
		ptr += len;
		nlen -= len;
	}

	return params;
}

/*
 * Strategy routine called from dm_strategy.
 */
static int
dm_target_stripe_strategy(dm_table_entry_t *table_en, struct buf *bp)
{
	dm_target_stripe_config_t *tsc;
	struct bio *bio = &bp->b_bio1;
	struct buf *nestbuf;
	uint64_t blkno, blkoff;
	uint64_t stripe, blknr;
	uint32_t stripe_off, stripe_rest, num_blks, issue_blks;
	int devnr;

	tsc = table_en->target_config;
	if (tsc == NULL)
		return 0;

	/* calculate extent of request */
	KKASSERT(bp->b_resid % DEV_BSIZE == 0);

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
	case BUF_CMD_WRITE:
	case BUF_CMD_FREEBLKS:
		/*
		 * Loop through to individual operations
		 */
		blkno = bp->b_bio1.bio_offset / DEV_BSIZE;
		blkoff = 0;
		num_blks = bp->b_resid / DEV_BSIZE;
		nestiobuf_init(bio);

		while (num_blks > 0) {
			/* blockno to strip piece nr */
			stripe = blkno / tsc->stripe_chunksize;
			stripe_off = blkno % tsc->stripe_chunksize;

			/* where we are inside the strip */
			devnr = stripe % tsc->stripe_num;
			blknr = stripe / tsc->stripe_num;

			/* how much is left before we hit a boundary */
			stripe_rest = tsc->stripe_chunksize - stripe_off;

			/* issue this piece on stripe `stripe' */
			issue_blks = MIN(stripe_rest, num_blks);
			nestbuf = getpbuf(NULL);
			nestbuf->b_flags |= bio->bio_buf->b_flags & B_HASBOGUS;

			nestiobuf_add(bio, nestbuf, blkoff,
					issue_blks * DEV_BSIZE, NULL);

			/* I need number of bytes. */
			nestbuf->b_bio1.bio_offset =
				blknr * tsc->stripe_chunksize + stripe_off;
			nestbuf->b_bio1.bio_offset +=
				tsc->stripe_devs[devnr].offset;
			nestbuf->b_bio1.bio_offset *= DEV_BSIZE;

			vn_strategy(tsc->stripe_devs[devnr].pdev->pdev_vnode,
				    &nestbuf->b_bio1);

			blkno += issue_blks;
			blkoff += issue_blks * DEV_BSIZE;
			num_blks -= issue_blks;
		}
		nestiobuf_start(bio);
		break;
	case BUF_CMD_FLUSH:
		nestiobuf_init(bio);
		for (devnr = 0; devnr < tsc->stripe_num; ++devnr) {
			nestbuf = getpbuf(NULL);
			nestbuf->b_flags |= bio->bio_buf->b_flags & B_HASBOGUS;

			nestiobuf_add(bio, nestbuf, 0, 0, NULL);
			nestbuf->b_bio1.bio_offset = 0;
			vn_strategy(tsc->stripe_devs[devnr].pdev->pdev_vnode,
				    &nestbuf->b_bio1);
		}
		nestiobuf_start(bio);
		break;
	default:
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		biodone(bio);
		break;
	}
	return 0;
}


static int
dm_target_stripe_dump(dm_table_entry_t *table_en, void *data, size_t length, off_t offset)
{
	dm_target_stripe_config_t *tsc;
	uint64_t blkno, blkoff;
	uint64_t stripe, blknr;
	uint32_t stripe_off, stripe_rest, num_blks, issue_blks;
	uint64_t off2, len2;
	int devnr;

	tsc = table_en->target_config;
	if (tsc == NULL)
		return 0;

	/* calculate extent of request */
	KKASSERT(length % DEV_BSIZE == 0);

	blkno = offset / DEV_BSIZE;
	blkoff = 0;
	num_blks = length / DEV_BSIZE;

	/*
	 * 0 length means flush buffers and return
	 */
	if (length == 0) {
		for (devnr = 0; devnr < tsc->stripe_num; ++devnr) {
			if (tsc->stripe_devs[devnr].pdev->pdev_vnode->v_rdev == NULL)
				return ENXIO;

			dev_ddump(tsc->stripe_devs[devnr].pdev->pdev_vnode->v_rdev,
			    data, 0, offset, 0);
		}
		return 0;
	}

	while (num_blks > 0) {
		/* blockno to strip piece nr */
		stripe = blkno / tsc->stripe_chunksize;
		stripe_off = blkno % tsc->stripe_chunksize;

		/* where we are inside the strip */
		devnr = stripe % tsc->stripe_num;
		blknr = stripe / tsc->stripe_num;

		/* how much is left before we hit a boundary */
		stripe_rest = tsc->stripe_chunksize - stripe_off;

		/* issue this piece on stripe `stripe' */
		issue_blks = MIN(stripe_rest, num_blks);

#if 0
		nestiobuf_add(bio, nestbuf, blkoff,
				issue_blks * DEV_BSIZE);
#endif
		len2 = issue_blks * DEV_BSIZE;

		/* I need number of bytes. */
		off2 = blknr * tsc->stripe_chunksize + stripe_off;
		off2 += tsc->stripe_devs[devnr].offset;
		off2 *= DEV_BSIZE;
		off2 = dm_pdev_correct_dump_offset(tsc->stripe_devs[devnr].pdev,
		    off2);

		if (tsc->stripe_devs[devnr].pdev->pdev_vnode->v_rdev == NULL)
			return ENXIO;

		dev_ddump(tsc->stripe_devs[devnr].pdev->pdev_vnode->v_rdev,
		    (char *)data + blkoff, 0, off2, len2);

		blkno += issue_blks;
		blkoff += issue_blks * DEV_BSIZE;
		num_blks -= issue_blks;
	}

	return 0;
}

/*
 * Destroy a dm table entry for stripes.
 */
static int
dm_target_stripe_destroy(dm_table_entry_t *table_en)
{
	dm_target_stripe_config_t *tsc;

	if ((tsc = table_en->target_config) != NULL) {
		table_en->target_config = NULL;
		dm_target_stripe_destroy_config(tsc);
	}

	return 0;
}

static void
dm_target_stripe_destroy_config(dm_target_stripe_config_t *tsc)
{
	int n;

	for (n = 0; n < tsc->stripe_num; ++n) {
		if (tsc->stripe_devs[n].pdev) {
			dm_pdev_decr(tsc->stripe_devs[n].pdev);
			tsc->stripe_devs[n].pdev = NULL;
		}
	}
	kfree(tsc, M_DMSTRIPE);
}

/*
 * Generate properties from stripe table entry.
 */
static int
dm_target_stripe_deps(dm_table_entry_t *table_en, prop_array_t prop_array)
{
	dm_target_stripe_config_t *tsc;
	struct vattr va;
	int error;
	int n;

	if (table_en->target_config == NULL)
		return ENOENT;

	tsc = table_en->target_config;
	error = 0;
	for (n = 0; n < tsc->stripe_num; ++n) {
		error = VOP_GETATTR(tsc->stripe_devs[n].pdev->pdev_vnode, &va);
		if (error)
			break;
		prop_array_add_uint64(prop_array,
				(uint64_t)makeudev(major(va.va_rdev), minor(va.va_rdev)));
	}
	return (error);
}

/*
 * Unsupported for this target.
 */
static int
dm_target_stripe_upcall(dm_table_entry_t * table_en, struct buf * bp)
{
	return 0;
}

static int
dmts_mod_handler(module_t mod, int type, void *unused)
{
        dm_target_t *dmt = NULL;
        int err = 0;

        switch(type) {
        case MOD_LOAD:
                if ((dmt = dm_target_lookup("striped")) != NULL) {
                        dm_target_unbusy(dmt);
                        return EEXIST;
                }
                dmt = dm_target_alloc("striped");
                dmt->version[0] = 1;
                dmt->version[1] = 0;
                dmt->version[2] = 3;
                strlcpy(dmt->name, "striped", DM_MAX_TYPE_NAME);
                dmt->init = &dm_target_stripe_init;
                dmt->status = &dm_target_stripe_status;
                dmt->strategy = &dm_target_stripe_strategy;
                dmt->deps = &dm_target_stripe_deps;
                dmt->destroy = &dm_target_stripe_destroy;
                dmt->upcall = &dm_target_stripe_upcall;
                dmt->dump = &dm_target_stripe_dump;

                err = dm_target_insert(dmt);
		if (err == 0)
			kprintf("dm_target_stripe: Successfully initialized\n");
                break;

        case MOD_UNLOAD:
                err = dm_target_rem("striped");
                if (err == 0)
                        kprintf("dm_target_stripe: unloaded\n");
                break;

	default:
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_striped, dmts_mod_handler);
