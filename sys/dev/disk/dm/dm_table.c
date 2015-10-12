/*        $NetBSD: dm_table.c,v 1.5 2010/01/04 00:19:08 haad Exp $      */

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

#include <sys/malloc.h>

#include <dev/disk/dm/dm.h>

/*
 * There are two types of users of this interface:
 *
 * a) Readers such as
 *    dmstrategy, dmgetdisklabel, dmsize, dm_dev_status_ioctl,
 *    dm_table_deps_ioctl, dm_table_status_ioctl, dm_table_reload_ioctl
 *
 * b) Writers such as
 *    dm_dev_remove_ioctl, dm_dev_resume_ioctl, dm_table_clear_ioctl
 *
 * Writers can work with table_head only when there are no readers. We
 * simply use shared/exclusive locking to ensure this.
 */

/*
 * Function to increment table user reference counter. Return id
 * of table_id table.
 * DM_TABLE_ACTIVE will return active table id.
 * DM_TABLE_INACTIVE will return inactive table id.
 */
static int
dm_table_busy(dm_table_head_t *head, uint8_t table_id)
{
	uint8_t id;

	id = 0;

	lockmgr(&head->table_mtx, LK_SHARED);

	if (table_id == DM_TABLE_ACTIVE)
		id = head->cur_active_table;
	else
		id = 1 - head->cur_active_table;

	atomic_add_int(&head->io_cnt, 1);

	return id;
}
/*
 * Function release table lock and eventually wakeup all waiters.
 */
static void
dm_table_unbusy(dm_table_head_t *head)
{
	KKASSERT(head->io_cnt != 0);

	atomic_subtract_int(&head->io_cnt, 1);

	lockmgr(&head->table_mtx, LK_RELEASE);
}
/*
 * Return current active table to caller, increment io_cnt reference counter.
 */
dm_table_t *
dm_table_get_entry(dm_table_head_t *head, uint8_t table_id)
{
	uint8_t id;

	id = dm_table_busy(head, table_id);

	return &head->tables[id];
}
/*
 * Decrement io reference counter and release shared lock.
 */
void
dm_table_release(dm_table_head_t *head, uint8_t table_id)
{
	dm_table_unbusy(head);
}
/*
 * Switch table from inactive to active mode. Have to wait until io_cnt is 0.
 */
void
dm_table_switch_tables(dm_table_head_t *head)
{
	lockmgr(&head->table_mtx, LK_EXCLUSIVE);

	head->cur_active_table = 1 - head->cur_active_table;

	lockmgr(&head->table_mtx, LK_RELEASE);
}
/*
 * Destroy all table data. This function can run when there are no
 * readers on table lists.
 */
int
dm_table_destroy(dm_table_head_t *head, uint8_t table_id)
{
	dm_table_t *tbl;
	dm_table_entry_t *table_en;
	uint8_t id;

	lockmgr(&head->table_mtx, LK_EXCLUSIVE);

	aprint_debug("dm_table_destroy called with %d--%d\n", table_id, head->io_cnt);

	if (table_id == DM_TABLE_ACTIVE)
		id = head->cur_active_table;
	else
		id = 1 - head->cur_active_table;

	tbl = &head->tables[id];

	while ((table_en = SLIST_FIRST(tbl)) != NULL) {
		SLIST_REMOVE(tbl, table_en, dm_table_entry, next);
		/*
		 * Remove target specific config data. After successfull
		 * call table_en->target_config must be set to NULL.
		 */
		table_en->target->destroy(table_en);
		/* decrement the refcount for the target */
		dm_target_unbusy(table_en->target);

		kfree(table_en, M_DM);
	}
	KKASSERT(SLIST_EMPTY(tbl));

	lockmgr(&head->table_mtx, LK_RELEASE);

	return 0;
}
/*
 * Return length of active or inactive table in device.
 */
static uint64_t
_dm_table_size(dm_table_head_t *head, int table)
{
	dm_table_t *tbl;
	dm_table_entry_t *table_en;
	uint64_t length;

	length = 0;

	/* Select active table */
	tbl = dm_table_get_entry(head, table);

	/*
	 * Find out what tables I want to select.
	 * if length => rawblkno then we should used that table.
	 */
	SLIST_FOREACH(table_en, tbl, next) {
		length += table_en->length;
	}

	dm_table_unbusy(head);

	return length;
}

uint64_t
dm_table_size(dm_table_head_t *head)
{
	return _dm_table_size(head, DM_TABLE_ACTIVE);
}

uint64_t
dm_inactive_table_size(dm_table_head_t *head)
{
	return _dm_table_size(head, DM_TABLE_INACTIVE);
}

/*
 * Return > 0 if table is at least one table entry (returns number of entries)
 * and return 0 if there is not. Target count returned from this function
 * doesn't need to be true when userspace user receive it (after return
 * there can be dm_dev_resume_ioctl), therfore this isonly informative.
 */
int
dm_table_get_target_count(dm_table_head_t *head, uint8_t table_id)
{
	dm_table_entry_t *table_en;
	dm_table_t *tbl;
	uint32_t target_count;

	target_count = 0;

	tbl = dm_table_get_entry(head, table_id);

	SLIST_FOREACH(table_en, tbl, next)
	    target_count++;

	dm_table_unbusy(head);

	return target_count;
}


/*
 * Initialize table_head structures, I'm trying to keep this structure as
 * opaque as possible.
 */
void
dm_table_head_init(dm_table_head_t *head)
{
	head->cur_active_table = 0;
	head->io_cnt = 0;

	/* Initialize tables. */
	SLIST_INIT(&head->tables[0]);
	SLIST_INIT(&head->tables[1]);

	lockinit(&head->table_mtx, "dmtbl", 0, LK_CANRECURSE);
}
/*
 * Destroy all variables in table_head
 */
void
dm_table_head_destroy(dm_table_head_t *head)
{
	KKASSERT(lockcount(&head->table_mtx) == 0);

	/* tables doens't exists when I call this routine, therefore it
	 * doesn't make sense to have io_cnt != 0 */
	KKASSERT(head->io_cnt == 0);

	lockuninit(&head->table_mtx);
}
