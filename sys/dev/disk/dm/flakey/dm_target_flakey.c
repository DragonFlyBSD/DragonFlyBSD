/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Tomohiro Kusumi <kusumi.tomohiro@gmail.com>
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

#include <dev/disk/dm/dm.h>

MALLOC_DEFINE(M_DMFLAKEY, "dm_flakey", "Device Mapper Target Flakey");

/* dm_flakey never updates any field after initialization */
typedef struct target_flakey_config {
	dm_pdev_t *pdev;
	uint64_t offset;
	int up_int;
	int down_int;
	int offset_time;

	/* drop_writes feature */
	int drop_writes;

	/* corrupt_bio_byte feature */
	unsigned int corrupt_buf_byte;
	unsigned int corrupt_buf_rw;
	unsigned int corrupt_buf_value;
	unsigned int corrupt_buf_flags;  /* for B_XXX flags */
} dm_target_flakey_config_t;

#define FLAKEY_CORRUPT_DIR(tfc) \
	((tfc)->corrupt_buf_rw == BUF_CMD_READ ? 'r' : 'w')

static int _init_features(dm_target_flakey_config_t*, int, char**);
static __inline void _submit(dm_target_flakey_config_t*, struct bio*);
static int _flakey_read(dm_target_flakey_config_t*, struct buf*);
static int _flakey_write(dm_target_flakey_config_t*, struct buf*);
static int _flakey_corrupt_buf(dm_target_flakey_config_t*, struct bio*);

static int
dm_target_flakey_init(dm_table_entry_t *table_en, int argc, char **argv)
{
	dm_target_flakey_config_t *tfc;
	dm_pdev_t *dmp;
	int err;

	dmdebug("Flakey target init: argc=%d\n", argc);

	if (argc < 4) {
		kprintf("Flakey target takes 4 or more args\n");
		return EINVAL;
	}

	tfc = kmalloc(sizeof(*tfc), M_DMFLAKEY, M_WAITOK | M_ZERO);
	if (tfc == NULL)
		return ENOMEM;

	if ((dmp = dm_pdev_insert(argv[0])) == NULL) {
		err = ENOENT;
		goto fail;
	}
	tfc->pdev = dmp;
	tfc->offset = atoi64(argv[1]);
	tfc->up_int = atoi64(argv[2]);
	tfc->down_int = atoi64(argv[3]);
	tfc->offset_time = ticks;

	if ((tfc->up_int + tfc->down_int) == 0) {
		kprintf("Sum of up/down interval is 0\n");
		err = EINVAL;
		goto fail;
	}

	if (tfc->up_int + tfc->down_int < tfc->up_int) {
		kprintf("Interval time overflow\n");
		err = EINVAL;
		goto fail;
	}

	err = _init_features(tfc, argc - 4, argv + 4);
	if (err)
		goto fail;

	dm_table_add_deps(table_en, dmp);

	dm_table_init_target(table_en, DM_FLAKEY_DEV, tfc);

	return 0;
fail:
	kfree(tfc, M_DMFLAKEY);
	return err;
}

static int
_init_features(dm_target_flakey_config_t *tfc, int argc, char **argv)
{
	char *arg;
	unsigned int value;

	if (argc == 0)
		return 0;

	argc = atoi64(*argv++);  /* # of args for features */
	if (argc > 6) {
		kprintf("Invalid # of feature args %d\n", argc);
		return EINVAL;
	}

	while (argc) {
		argc--;
		arg = *argv++;

		/* drop_writes */
		if (strcmp(arg, "drop_writes") == 0) {
			tfc->drop_writes = 1;
			continue;
		}

		/* corrupt_bio_byte <Nth_byte> <direction> <value> <flags> */
		if (strcmp(arg, "corrupt_bio_byte") == 0) {
			if (argc < 4) {
				kprintf("Invalid # of feature args %d for "
					"corrupt_bio_byte\n", argc);
				return EINVAL;
			}

			/* <Nth_byte> */
			argc--;
			value = atoi64(*argv++);
			if (value < 1) {
				kprintf("Invalid corrupt_bio_byte "
					"<Nth_byte> arg %u\n", value);
				return EINVAL;
			}
			tfc->corrupt_buf_byte = value;

			/* <direction> */
			argc--;
			arg = *argv++;
			if (strcmp(arg, "r") == 0) {
				tfc->corrupt_buf_rw = BUF_CMD_READ;
			} else if (strcmp(arg, "w") == 0) {
				tfc->corrupt_buf_rw = BUF_CMD_WRITE;
			} else {
				kprintf("Invalid corrupt_bio_byte "
					"<direction> arg %s\n", arg);
				return EINVAL;
			}

			/* <value> */
			argc--;
			value = atoi64(*argv++);
			if (value > 0xff) {
				kprintf("Invalid corrupt_bio_byte "
					"<value> arg %u\n", value);
				return EINVAL;
			}
			tfc->corrupt_buf_value = value;

			/* <flags> */
			argc--;
			tfc->corrupt_buf_flags = atoi64(*argv++);

			continue;
		}

		kprintf("Unknown Flakey target feature %s\n", arg);
		return EINVAL;
	}

	if (tfc->drop_writes && (tfc->corrupt_buf_rw == BUF_CMD_WRITE)) {
		kprintf("Flakey target doesn't allow drop_writes feature "
			"and corrupt_bio_byte feature with 'w' set\n");
		return EINVAL;
	}

	return 0;
}

static int
dm_target_flakey_destroy(dm_table_entry_t *table_en)
{
	dm_target_flakey_config_t *tfc;

	tfc = table_en->target_config;
	if (tfc == NULL)
		return 0;

	dm_pdev_decr(tfc->pdev);

	kfree(tfc, M_DMFLAKEY);

	return 0;
}

static int
dm_target_flakey_strategy(dm_table_entry_t *table_en, struct buf *bp)
{
	dm_target_flakey_config_t *tfc;
	int elapsed;

	tfc = table_en->target_config;

	elapsed = (ticks - tfc->offset_time) / hz;
	if (elapsed % (tfc->up_int + tfc->down_int) >= tfc->up_int) {
		switch (bp->b_cmd) {
		case BUF_CMD_READ:
			return _flakey_read(tfc, bp);
		case BUF_CMD_WRITE:
			return _flakey_write(tfc, bp);
		default:
			break;
		}
	}

	/* This is what linear target does */
	_submit(tfc, &bp->b_bio1);

	return 0;
}

static __inline
void
_submit(dm_target_flakey_config_t *tfc, struct bio *bio)
{
	bio->bio_offset += tfc->offset * DEV_BSIZE;
	vn_strategy(tfc->pdev->pdev_vnode, bio);
}

static __inline
void
_flakey_eio_buf(struct buf *bp)
{
	bp->b_error = EIO;
	bp->b_resid = 0;
}

static void
_flakey_read_iodone(struct bio *bio)
{
	struct bio *obio;
	dm_target_flakey_config_t *tfc;

	tfc = bio->bio_caller_info1.ptr;
	obio = pop_bio(bio);

	/*
	 * Linux dm-flakey has changed its read behavior in 2016.
	 * This conditional is to sync with that change.
	 */
	if (tfc->corrupt_buf_byte && tfc->corrupt_buf_rw == BUF_CMD_READ)
		_flakey_corrupt_buf(tfc, obio);
	else if (!tfc->drop_writes)
		_flakey_eio_buf(bio->bio_buf);

	biodone(obio);
}

static int
_flakey_read(dm_target_flakey_config_t *tfc, struct buf *bp)
{
	struct bio *bio = &bp->b_bio1;
	struct bio *nbio;

	/*
	 * Linux dm-flakey has changed its read behavior in 2016.
	 * This conditional is to sync with that change.
	 */
	if (!tfc->corrupt_buf_byte && !tfc->drop_writes) {
		_flakey_eio_buf(bp);
		biodone(bio);
		return 0;
	}

	nbio = push_bio(bio);
	nbio->bio_done = _flakey_read_iodone;
	nbio->bio_caller_info1.ptr = tfc;
	nbio->bio_offset = pop_bio(nbio)->bio_offset;

	_submit(tfc, nbio);

	return 0;
}

static int
_flakey_write(dm_target_flakey_config_t *tfc, struct buf *bp)
{
	struct bio *bio = &bp->b_bio1;

	if (tfc->drop_writes) {
		dmdebug("bio=%p drop_writes offset=%ju\n",
			bio, bio->bio_offset);
		biodone(bio);
		return 0;
	}

	if (tfc->corrupt_buf_byte && tfc->corrupt_buf_rw == BUF_CMD_WRITE) {
		_flakey_corrupt_buf(tfc, bio);
		_submit(tfc, bio);
		return 0;
	}

	/* Error all I/Os if neither of the above two */
	_flakey_eio_buf(bp);
	biodone(bio);

	return 0;
}

static int
_flakey_corrupt_buf(dm_target_flakey_config_t *tfc, struct bio *bio)
{
	struct buf *bp;

	bp = bio->bio_buf;

	if (bp->b_data == NULL)
		return 1;
	if (bp->b_error)
		return 1;  /* Don't corrupt on error */
	if (bp->b_bcount < tfc->corrupt_buf_byte)
		return 1;
	if ((bp->b_flags & tfc->corrupt_buf_flags) != tfc->corrupt_buf_flags)
		return 1;

	bp->b_data[tfc->corrupt_buf_byte - 1] = tfc->corrupt_buf_value;
	dmdebug("bio=%p dir=%c offset=%ju Nth=%u value=%u\n",
		bio,
		FLAKEY_CORRUPT_DIR(tfc),
		bio->bio_offset,
		tfc->corrupt_buf_byte,
		tfc->corrupt_buf_value);

	return 0;
}

static char *
dm_target_flakey_table(void *target_config)
{
	dm_target_flakey_config_t *tfc;
	char *params, *p;
	int drop_writes;

	tfc = target_config;
	KKASSERT(tfc != NULL);

	drop_writes = tfc->drop_writes;

	params = dm_alloc_string(DM_MAX_PARAMS_SIZE);
	p = params;
	p += ksnprintf(p, DM_MAX_PARAMS_SIZE, "%s %d %d %d %u ",
		tfc->pdev->udev_name, tfc->offset_time,
		tfc->up_int, tfc->down_int,
		drop_writes + (tfc->corrupt_buf_byte > 0) * 5);

	if (drop_writes)
		p += ksnprintf(p, DM_MAX_PARAMS_SIZE, "drop_writes ");

	if (tfc->corrupt_buf_byte)
		p += ksnprintf(p, DM_MAX_PARAMS_SIZE,
			"corrupt_bio_byte %u %c %u %u ",
			tfc->corrupt_buf_byte,
			FLAKEY_CORRUPT_DIR(tfc),
			tfc->corrupt_buf_value,
			tfc->corrupt_buf_flags);
	*(--p) = '\0';

	return params;
}

static int
dmtf_mod_handler(module_t mod, int type, void *unused)
{
	dm_target_t *dmt = NULL;
	int err = 0;

	switch(type) {
	case MOD_LOAD:
		if ((dmt = dm_target_lookup("flakey")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("flakey");
		dmt->version[0] = 1;
		dmt->version[1] = 0;
		dmt->version[2] = 0;
		strlcpy(dmt->name, "flakey", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_flakey_init;
		dmt->destroy = &dm_target_flakey_destroy;
		dmt->strategy = &dm_target_flakey_strategy;
		dmt->table = &dm_target_flakey_table;

		err = dm_target_insert(dmt);
		if (err == 0)
			kprintf("dm_target_flakey: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_remove("flakey");
		if (err == 0)
			kprintf("dm_target_flakey: unloaded\n");
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_flakey, dmtf_mod_handler);
