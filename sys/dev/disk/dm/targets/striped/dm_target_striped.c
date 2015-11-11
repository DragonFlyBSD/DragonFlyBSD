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

#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include <dev/disk/dm/dm.h>
MALLOC_DEFINE(M_DMSTRIPE, "dm_striped", "Device Mapper Target Striped");

#define MAX_STRIPES 32
/* #define USE_NUM_ERROR */

struct target_stripe_dev {
	dm_pdev_t *pdev;
	uint64_t offset;
	int num_error;
};

typedef struct target_stripe_config {
	int stripe_num;
	uint64_t stripe_chunksize;
	struct target_stripe_dev stripe_devs[0];
} dm_target_stripe_config_t;

static void dm_target_stripe_destroy_config(dm_target_stripe_config_t *tsc);

/*
 * Init function called from dm_table_load_ioctl.
 *
 * Example line sent to dm from lvm tools when using striped target.
 * start length striped #stripes chunk_size device1 offset1 ... deviceN offsetN
 */
static int
dm_target_stripe_init(dm_table_entry_t *table_en, int argc, char **argv)
{
	dm_target_stripe_config_t *tsc;
	char *arg;
	int i, n, siz, chunksize;

	if (argc < 4) {
		kprintf("Striped target takes 4 or more args\n");
		return EINVAL;
	}

	n = (int)atoi64(argv[0]);
	if (n <= 0 || n > MAX_STRIPES) {
		kprintf("dm: Error %d stripes not supported (%d max)\n",
			n, MAX_STRIPES);
		return ENOTSUP;
	}
#if 0
	if (table_en->length % n) {
		kprintf("dm: Target device size not multiple of stripes\n");
		return EINVAL;
	}
#endif
	if (argc != (2 + n * 2)) {
		kprintf("dm: Invalid argc %d for %d stripe devices\n",
			argc, n);
		return EINVAL;
	}

	chunksize = atoi64(argv[1]);
	if (chunksize < 1 || chunksize * DEV_BSIZE > MAXPHYS) {
		kprintf("dm: Error unsupported chunk size %jdKB\n",
			(intmax_t)chunksize * DEV_BSIZE / 1024);
		return EINVAL;
	}
#if 0
	if ((table_en->length / n) % chunksize) {
		kprintf("dm: Stripe device size not multiple of chunk size\n");
		return EINVAL;
	}
#endif

	siz = sizeof(dm_target_stripe_config_t) +
		n * sizeof(struct target_stripe_dev);
	tsc = kmalloc(siz, M_DMSTRIPE, M_WAITOK | M_ZERO);
	tsc->stripe_num = n;
	tsc->stripe_chunksize = chunksize;

	/*
	 * Parse the devices
	 */

	kprintf("dm: Stripe %d devices chunk size %dKB\n",
		(int)tsc->stripe_num,
		(int)tsc->stripe_chunksize
	);

	argv += 2;
	for (n = 0, i = 0; n < tsc->stripe_num; ++n) {
		arg = argv[i++];
		KKASSERT(arg);
		tsc->stripe_devs[n].pdev = dm_pdev_insert(arg);
		if (tsc->stripe_devs[n].pdev == NULL)
			break;
		arg = argv[i++];
		KKASSERT(arg);
		tsc->stripe_devs[n].offset = atoi64(arg);
		dm_table_add_deps(table_en, tsc->stripe_devs[n].pdev);
	}
	if (n != tsc->stripe_num) {
		dm_target_stripe_destroy_config(tsc);
		return (ENOENT);
	}

	dm_table_init_target(table_en, DM_STRIPE_DEV, tsc);

	return 0;
}

/*
 * Info routine called to get params string.
 */
static char *
dm_target_stripe_info(void *target_config)
{
	dm_target_stripe_config_t *tsc;
	char *params;
	char *ptr;
	char buf[MAX_STRIPES + 1];
	size_t len;
	int ret;
	int i;

	tsc = target_config;

	len = DM_MAX_PARAMS_SIZE;
	params = dm_alloc_string(len);
	ptr = params;

	ret = ksnprintf(ptr, len, "%d ", tsc->stripe_num);
	ptr += ret;
	len -= ret;

	memset(buf, 0, sizeof(buf));
	for (i = 0; i < tsc->stripe_num; i++) {
		ret = ksnprintf(ptr, len, "%s ",
			tsc->stripe_devs[i].pdev->udev_name);
		if (tsc->stripe_devs[i].num_error) /* no lock */
			buf[i] = 'D';
		else
			buf[i] = 'A';
		ptr += ret;
		len -= ret;
	}

	ret = ksnprintf(ptr, len, "1 %s", buf);
	ptr += ret;
	len -= ret;

	return params;
}

/*
 * Table routine called to get params string.
 */
static char *
dm_target_stripe_table(void *target_config)
{
	dm_target_stripe_config_t *tsc;
	char *params;
	char *ptr;
	size_t len;
	int ret;
	int i;

	tsc = target_config;

	len = DM_MAX_PARAMS_SIZE;
	params = dm_alloc_string(len);
	ptr = params;

	ret = ksnprintf(ptr, len, "%d %jd",
		tsc->stripe_num,
		(intmax_t)tsc->stripe_chunksize);
	ptr += ret;
	len -= ret;

	for (i = 0; i < tsc->stripe_num; i++) {
		ret = ksnprintf(ptr, len, " %s %jd",
			tsc->stripe_devs[i].pdev->udev_name,
			(intmax_t)tsc->stripe_devs[i].offset);
		ptr += ret;
		len -= ret;
	}

	return params;
}

#ifdef USE_NUM_ERROR
static void
dm_target_stripe_iodone(struct bio *bio)
{
	struct bio *obio;
	struct buf *bp;
	dm_target_stripe_config_t *tsc;

	bp = bio->bio_buf;
	tsc = bio->bio_caller_info1.ptr;

	if (bp->b_error) {
		int devnr;
		uint64_t blkno, stripe;

		blkno = bio->bio_offset / DEV_BSIZE;
		stripe = blkno / tsc->stripe_chunksize;
		devnr = stripe % tsc->stripe_num;
		KKASSERT(devnr < MAX_STRIPES);
		tsc->stripe_devs[devnr].num_error++;

		dmdebug("stripe_iodone: device=%d error=%d\n",
			devnr, bp->b_error);
	}

	obio = pop_bio(bio);
	biodone(obio);
}

static __inline
struct bio *get_stripe_bio(struct bio *bio, void *priv)
{
	struct bio *nbio;

	nbio = push_bio(bio);
	nbio->bio_caller_info1.ptr = priv;
	nbio->bio_done = dm_target_stripe_iodone;

	return nbio;
}
#else
static __inline
struct bio *get_stripe_bio(struct bio *bio, void *priv __unused)
{
	return bio;
}
#endif

/*
 * Strategy routine called from dm_strategy.
 */
static int
dm_target_stripe_strategy(dm_table_entry_t *table_en, struct buf *bp)
{
	dm_target_stripe_config_t *tsc;
	struct bio *bio = &bp->b_bio1;
	struct bio *nbio;
	struct buf *nestbuf;
	struct target_stripe_dev *dev;
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
		blkno = bio->bio_offset / DEV_BSIZE;
		blkoff = 0;
		num_blks = bp->b_resid / DEV_BSIZE;
		nestiobuf_init(bio);

		while (num_blks > 0) {
			/* blockno to stripe piece nr */
			stripe = blkno / tsc->stripe_chunksize;
			stripe_off = blkno % tsc->stripe_chunksize;

			/* where we are inside the stripe */
			devnr = stripe % tsc->stripe_num;
			blknr = stripe / tsc->stripe_num;
			dev = &tsc->stripe_devs[devnr];

			/* how much is left before we hit a boundary */
			stripe_rest = tsc->stripe_chunksize - stripe_off;

			/* issue this piece on stripe `stripe' */
			issue_blks = MIN(stripe_rest, num_blks);
			nestbuf = getpbuf(NULL);
			nestbuf->b_flags |= bio->bio_buf->b_flags & B_HASBOGUS;

			nestiobuf_add(bio, nestbuf, blkoff,
					issue_blks * DEV_BSIZE, NULL);

			nbio = get_stripe_bio(&nestbuf->b_bio1, tsc);
			nbio->bio_offset = blknr * tsc->stripe_chunksize;
			nbio->bio_offset += stripe_off;
			nbio->bio_offset += dev->offset;
			nbio->bio_offset *= DEV_BSIZE;

			vn_strategy(dev->pdev->pdev_vnode, nbio);

			blkno += issue_blks;
			blkoff += issue_blks * DEV_BSIZE;
			num_blks -= issue_blks;
		}
		nestiobuf_start(bio);
		break;
	case BUF_CMD_FLUSH:
		nestiobuf_init(bio);
		for (devnr = 0; devnr < tsc->stripe_num; ++devnr) {
			dev = &tsc->stripe_devs[devnr];
			nestbuf = getpbuf(NULL);
			nestbuf->b_flags |= bio->bio_buf->b_flags & B_HASBOGUS;

			nestiobuf_add(bio, nestbuf, 0, 0, NULL);

			nbio = get_stripe_bio(&nestbuf->b_bio1, tsc);
			nbio->bio_offset = 0;

			vn_strategy(dev->pdev->pdev_vnode, nbio);
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
		/* blockno to stripe piece nr */
		stripe = blkno / tsc->stripe_chunksize;
		stripe_off = blkno % tsc->stripe_chunksize;

		/* where we are inside the stripe */
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
		dmt->init = &dm_target_stripe_init;
		dmt->destroy = &dm_target_stripe_destroy;
		dmt->strategy = &dm_target_stripe_strategy;
		dmt->table = &dm_target_stripe_table;
		dmt->info = &dm_target_stripe_info;
		dmt->dump = &dm_target_stripe_dump;
		dmt->max_argc = 2 + (MAX_STRIPES * 2);

		err = dm_target_insert(dmt);
		if (err == 0)
			kprintf("dm_target_striped: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_remove("striped");
		if (err == 0)
			kprintf("dm_target_striped: unloaded\n");
		break;

	default:
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_striped, dmts_mod_handler);
