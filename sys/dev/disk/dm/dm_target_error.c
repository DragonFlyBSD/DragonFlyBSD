/*        $NetBSD: dm_target_error.c,v 1.10 2010/01/04 00:12:22 haad Exp $      */

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
 * This file implements initial version of device-mapper error target.
 */
#include <sys/types.h>
#include <sys/param.h>

#include <sys/buf.h>

#include "dm.h"

/* Init function called from dm_table_load_ioctl. */
static int
dm_target_error_init(dm_dev_t * dmv, void **target_config, char *argv)
{

	kprintf("Error target init function called!!\n");

	*target_config = NULL;

	dmv->dev_type = DM_ERROR_DEV;

	return 0;
}

/* Status routine called to get params string. */
static char *
dm_target_error_status(void *target_config)
{
	return NULL;
}

/* Strategy routine called from dm_strategy. */
static int
dm_target_error_strategy(dm_table_entry_t * table_en, struct buf * bp)
{

	kprintf("Error target read function called!!\n");

	bp->b_error = EIO;
	bp->b_resid = 0;

	biodone(&bp->b_bio1);

	return 0;
}

/* Doesn't do anything here. */
static int
dm_target_error_destroy(dm_table_entry_t * table_en)
{
	table_en->target_config = NULL;

	/* Unbusy target so we can unload it */
	dm_target_unbusy(table_en->target);

	return 0;
}

/* Doesn't not need to do anything here. */
static int
dm_target_error_deps(dm_table_entry_t * table_en, prop_array_t prop_array)
{
	return 0;
}

/* Unsupported for this target. */
static int
dm_target_error_upcall(dm_table_entry_t * table_en, struct buf * bp)
{
	return 0;
}

static int
dmte_mod_handler(module_t mod, int type, void *unused)
{
	dm_target_t *dmt = NULL;
	int err = 0;

	switch(type) {
	case MOD_LOAD:
		if ((dmt = dm_target_lookup("error")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("error");
		dmt->version[0] = 1;
		dmt->version[1] = 0;
		dmt->version[2] = 0;
		strlcpy(dmt->name, "error", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_error_init;
		dmt->status = &dm_target_error_status;
		dmt->strategy = &dm_target_error_strategy;
		dmt->deps = &dm_target_error_deps;
		dmt->destroy = &dm_target_error_destroy;
		dmt->upcall = &dm_target_error_upcall;
		dmt->dump = NULL;

		err = dm_target_insert(dmt);
		if (err == 0)
			kprintf("dm_target_error: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_rem("error");
		if (err == 0)
			kprintf("dm_target_error: unloaded\n");
		break;

	default:
		break;
	}

	return err;
}
	
DM_TARGET_MODULE(dm_target_error, dmte_mod_handler);
