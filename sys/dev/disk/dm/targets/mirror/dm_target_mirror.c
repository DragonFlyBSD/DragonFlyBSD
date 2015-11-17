/*$NetBSD: dm_target_mirror.c,v 1.8 2010/01/04 00:12:22 haad Exp $*/

/*
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
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
 * This file implements initial version of device-mapper mirror target.
 */
#include <dev/disk/dm/dm.h>

typedef struct target_mirror_config {
#define MAX_MIRROR_COPIES 4
	dm_pdev_t *orig;
	dm_pdev_t *copies[MAX_MIRROR_COPIES];

	/* copied blocks bitmaps administration etc*/
	dm_pdev_t *log_pdev;	/* for administration */
	uint64_t log_regionsize;	/* blocksize of mirror */

	/* list of parts that still need copied etc.; run length encoded? */
} dm_target_mirror_config_t;

int dm_target_mirror_init(dm_table_entry_t *, int, char **);
char *dm_target_mirror_table(void *);
int dm_target_mirror_strategy(dm_table_entry_t *, struct buf *);
int dm_target_mirror_deps(dm_table_entry_t *, prop_array_t);
int dm_target_mirror_destroy(dm_table_entry_t *);

/*
 * Init function called from dm_table_load_ioctl.
 * start length mirror log_type #logargs logarg1 ... logargN #devs device1 offset1 ... deviceN offsetN
 * 0 52428800 mirror clustered_disk 4 253:2 1024 UUID block_on_error 3 253:3 0 253:4 0 253:5 0
 */
int
dm_target_mirror_init(dm_table_entry_t *table_en, int argc, char **argv)
{

	kprintf("Mirror target init function called!!\n");

	dm_table_init_target(table_en, DM_MIRROR_DEV, NULL);

	return ENOSYS;
}

/* Table routine called to get params string. */
char *
dm_target_mirror_table(void *target_config)
{
	return NULL;
}

/* Strategy routine called from dm_strategy. */
int
dm_target_mirror_strategy(dm_table_entry_t *table_en, struct buf *bp)
{

	kprintf("Mirror target read function called!!\n");

	bp->b_error = EIO;
	bp->b_resid = 0;

	biodone(bp);

	return 0;
}

/* Doesn't do anything here. */
int
dm_target_mirror_destroy(dm_table_entry_t *table_en)
{
	/* Unbusy target so we can unload it */
	dm_target_unbusy(table_en->target);

	return 0;
}

/* Doesn't not need to do anything here. */
int
dm_target_mirror_deps(dm_table_entry_t *table_en, prop_array_t prop_array)
{
	return 0;
}
