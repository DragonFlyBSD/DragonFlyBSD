/*        $NetBSD: dm_target_linear.c,v 1.9 2010/01/04 00:14:41 haad Exp $      */

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


/*
 * This file implements initial version of device-mapper dklinear target.
 */

#include <sys/types.h>
#include <sys/param.h>

#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include <dev/disk/dm/dm.h>
MALLOC_DEFINE(M_DMLINEAR, "dm_linear", "Device Mapper Target Linear");

/*
 * Allocate target specific config data, and link them to table.
 * This function is called only when, flags is not READONLY and
 * therefore we can add things to pdev list. This should not a
 * problem because this routine is called only from dm_table_load_ioctl.
 * @argv[0] is name,
 * @argv[1] is physical data offset.
 */
static int
dm_target_linear_init(dm_dev_t * dmv, void **target_config, char *params)
{
	dm_target_linear_config_t *tlc;
	dm_pdev_t *dmp;

	char **ap, *argv[3];

	if (params == NULL)
		return EINVAL;

	/*
	 * Parse a string, containing tokens delimited by white space,
	 * into an argument vector
	 */
	for (ap = argv; ap < &argv[2] &&
	    (*ap = strsep(&params, " \t")) != NULL;) {
		if (**ap != '\0')
			ap++;
	}

	aprint_debug("Linear target init function called %s--%s!!\n",
	    argv[0], argv[1]);

	/* XXX: temp hack */
	if (argv[0] == NULL)
		return EINVAL;

	/* Insert dmp to global pdev list */
	if ((dmp = dm_pdev_insert(argv[0])) == NULL)
		return ENOENT;

	if ((tlc = kmalloc(sizeof(dm_target_linear_config_t), M_DMLINEAR, M_WAITOK))
	    == NULL)
		return ENOMEM;

	tlc->pdev = dmp;
	tlc->offset = 0;	/* default settings */

	/* Check user input if it is not leave offset as 0. */
	tlc->offset = atoi64(argv[1]);

	*target_config = tlc;

	dmv->dev_type = DM_LINEAR_DEV;

	return 0;
}
/*
 * Status routine is called to get params string, which is target
 * specific. When dm_table_status_ioctl is called with flag
 * DM_STATUS_TABLE_FLAG I have to sent params string back.
 */
static char *
dm_target_linear_status(void *target_config)
{
	dm_target_linear_config_t *tlc;
	char *params;
	tlc = target_config;

	aprint_debug("Linear target status function called\n");

	/* target expects use of M_DM */
	params = kmalloc(DM_MAX_PARAMS_SIZE, M_DM, M_WAITOK);

	aprint_normal("%s %" PRIu64, tlc->pdev->name, tlc->offset);
	ksnprintf(params, DM_MAX_PARAMS_SIZE, "%s %" PRIu64,
	    tlc->pdev->name, tlc->offset);

	return params;
}
/*
 * Do IO operation, called from dmstrategy routine.
 */
static int
dm_target_linear_strategy(dm_table_entry_t * table_en, struct buf * bp)
{
	dm_target_linear_config_t *tlc;

	tlc = table_en->target_config;

/*	printf("Linear target read function called %" PRIu64 "!!\n",
	tlc->offset);*/

#if 0
	bp->b_blkno += tlc->offset;
#endif
	bp->b_bio1.bio_offset += tlc->offset * DEV_BSIZE;

	vn_strategy(tlc->pdev->pdev_vnode, &bp->b_bio1);

	return 0;

}

static int
dm_target_linear_dump(dm_table_entry_t *table_en, void *data, size_t length, off_t offset)
{
	dm_target_linear_config_t *tlc;

	tlc = table_en->target_config;

	offset += tlc->offset * DEV_BSIZE;
	offset = dm_pdev_correct_dump_offset(tlc->pdev, offset);

	if (tlc->pdev->pdev_vnode->v_rdev == NULL)
		return ENXIO;

	return dev_ddump(tlc->pdev->pdev_vnode->v_rdev, data, 0, offset, length);
}

/*
 * Destroy target specific data. Decrement table pdevs.
 */
static int
dm_target_linear_destroy(dm_table_entry_t * table_en)
{
	dm_target_linear_config_t *tlc;

	/*
	 * Destroy function is called for every target even if it
	 * doesn't have target_config.
	 */

	if (table_en->target_config == NULL)
		return 0;

	tlc = table_en->target_config;

	/* Decrement pdev ref counter if 0 remove it */
	dm_pdev_decr(tlc->pdev);

	kfree(table_en->target_config, M_DMLINEAR);

	table_en->target_config = NULL;

	return 0;
}
/* Add this target pdev dependiences to prop_array_t */
static int
dm_target_linear_deps(dm_table_entry_t * table_en, prop_array_t prop_array)
{
	dm_target_linear_config_t *tlc;
	struct vattr va;

	int error;

	if (table_en->target_config == NULL)
		return ENOENT;

	tlc = table_en->target_config;

	if ((error = VOP_GETATTR(tlc->pdev->pdev_vnode, &va)) != 0)
		return error;

	prop_array_add_uint64(prop_array,
	    (uint64_t) makeudev(major(va.va_rdev), minor(va.va_rdev)));

	return 0;
}
/*
 * Register upcall device.
 * Linear target doesn't need any upcall devices but other targets like
 * mirror, snapshot, multipath, stripe will use this functionality.
 */
static int
dm_target_linear_upcall(dm_table_entry_t * table_en, struct buf * bp)
{
	return 0;
}

static int
dmtl_mod_handler(module_t mod, int type, void *unused)
{
	dm_target_t *dmt = NULL;
	int err = 0;

	switch(type) {
	case MOD_LOAD:
		if ((dmt = dm_target_lookup("linear")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("linear");
		dmt->version[0] = 1;
		dmt->version[1] = 0;
		dmt->version[2] = 2;
		strlcpy(dmt->name, "linear", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_linear_init;
		dmt->status = &dm_target_linear_status;
		dmt->strategy = &dm_target_linear_strategy;
		dmt->deps = &dm_target_linear_deps;
		dmt->destroy = &dm_target_linear_destroy;
		dmt->upcall = &dm_target_linear_upcall;
		dmt->dump = &dm_target_linear_dump;

		err = dm_target_insert(dmt);
		if (err == 0)
			kprintf("dm_target_linear: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_rem("linear");
		if (err == 0)
			kprintf("dm_target_linear: unloaded\n");
		break;

	default:
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_linear, dmtl_mod_handler);
