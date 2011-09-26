/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sbin/hammer/misc.c,v 1.5 2008/06/26 04:07:57 dillon Exp $
 */

#include "hammer.h"

/*
 * (taken from /usr/src/sys/vfs/hammer/hammer_btree.c)
 *
 * Compare two B-Tree elements, return -N, 0, or +N (e.g. similar to strcmp).
 *
 * Note that for this particular function a return value of -1, 0, or +1
 * can denote a match if delete_tid is otherwise discounted.  A delete_tid
 * of zero is considered to be 'infinity' in comparisons.
 *
 * See also hammer_rec_rb_compare() and hammer_rec_cmp() in hammer_object.c.
 */
int
hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2)
{
	if (key1->localization < key2->localization)
		return(-5);
	if (key1->localization > key2->localization)
		return(5);

	if (key1->obj_id < key2->obj_id)
		return(-4);
	if (key1->obj_id > key2->obj_id)
		return(4);

	if (key1->rec_type < key2->rec_type)
		return(-3);
	if (key1->rec_type > key2->rec_type)
		return(3);

	if (key1->key < key2->key)
		return(-2);
	if (key1->key > key2->key)
		return(2);

	if (key1->create_tid == 0) {
		if (key2->create_tid == 0)
			return(0);
		return(1);
	}
	if (key2->create_tid == 0)
		return(-1);
	if (key1->create_tid < key2->create_tid)
		return(-1);
	if (key1->create_tid > key2->create_tid)
		return(1);
	return(0);
}

void
hammer_key_beg_init(hammer_base_elm_t base)
{
	bzero(base, sizeof(*base));

	base->localization = HAMMER_MIN_LOCALIZATION;
	base->obj_id = HAMMER_MIN_OBJID;
	base->key = HAMMER_MIN_KEY;
	base->create_tid = 1;
	base->rec_type = HAMMER_MIN_RECTYPE;
}

void
hammer_key_end_init(hammer_base_elm_t base)
{
	bzero(base, sizeof(*base));

	base->localization = HAMMER_MAX_LOCALIZATION;
	base->obj_id = HAMMER_MAX_OBJID;
	base->key = HAMMER_MAX_KEY;
	base->create_tid = HAMMER_MAX_TID;
	base->rec_type = HAMMER_MAX_RECTYPE;
}

int
hammer_crc_test_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
	hammer_crc_t crc;

	if (leaf->data_len == 0) {
		crc = 0;
	} else {
		switch(leaf->base.rec_type) {
		case HAMMER_RECTYPE_INODE:
			if (leaf->data_len != sizeof(struct hammer_inode_data))
				return(0);
			crc = crc32(data, HAMMER_INODE_CRCSIZE);
			break;
		default:
			crc = crc32(data, leaf->data_len);
			break;
		}
	}
	return (leaf->data_crc == crc);
}

