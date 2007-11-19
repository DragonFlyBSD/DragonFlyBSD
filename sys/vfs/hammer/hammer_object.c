/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_object.c,v 1.2 2007/11/19 00:53:40 dillon Exp $
 */

#include "hammer.h"

static int hammer_add_record(hammer_transaction_t trans,
			     hammer_record_t record);

/*
 * Red-black tree support.
 */
static int
hammer_rec_rb_compare(struct hammer_record *rec1, struct hammer_record *rec2)
{
	if (rec1->rec.base.base.rec_type < rec2->rec.base.base.rec_type)
		return(-1);
	if (rec1->rec.base.base.rec_type > rec2->rec.base.base.rec_type)
		return(1);

	if (rec1->rec.base.base.key < rec2->rec.base.base.key)
		return(-1);
	if (rec1->rec.base.base.key > rec2->rec.base.base.key)
		return(1);

	if (rec1->rec.base.base.create_tid < rec2->rec.base.base.create_tid)
		return(-1);
	if (rec1->rec.base.base.create_tid > rec2->rec.base.base.create_tid)
		return(1);
        return(0);
}

static int
hammer_rec_compare(struct hammer_base_elm *info, struct hammer_record *rec)
{
        /*
         * A key1->rec_type of 0 matches any record type.
         */
        if (info->rec_type) {
                if (info->rec_type < rec->rec.base.base.rec_type)
                        return(-3);
                if (info->rec_type > rec->rec.base.base.rec_type)
                        return(3);
        }

        /*
         * There is no special case for key.  0 means 0.
         */
        if (info->key < rec->rec.base.base.key)
                return(-2);
        if (info->key > rec->rec.base.base.key)
                return(2);

        /*
         * This test has a number of special cases.  create_tid in key1 is
         * the as-of transction id, and delete_tid in key1 is NOT USED.
         *
         * A key1->create_tid of 0 matches any record regardles of when
         * it was created or destroyed.  0xFFFFFFFFFFFFFFFFULL should be
         * used to search for the most current state of the object.
         *
         * key2->create_tid is a HAMMER record and will never be
         * 0.   key2->delete_tid is the deletion transaction id or 0 if
         * the record has not yet been deleted.
         */
        if (info->create_tid) {
                if (info->create_tid < rec->rec.base.base.create_tid)
                        return(-1);
                if (rec->rec.base.base.delete_tid &&
		    info->create_tid >= rec->rec.base.base.delete_tid) {
                        return(1);
		}
        }
        return(0);
}

RB_GENERATE(hammer_rec_rb_tree, hammer_record, rb_node, hammer_rec_rb_compare);
RB_GENERATE_XLOOKUP(hammer_rec_rb_tree, INFO, hammer_record, rb_node,
		    hammer_rec_compare, hammer_base_elm_t);

/*
 * Add a directory entry (dip,ncp) which references inode (ip).
 *
 * Note that the low 32 bits of the namekey are set temporarily to create
 * a unique in-memory record, and may be modified a second time when the
 * record is synchronized to disk.  In particular, the low 32 bits cannot be
 * all 0's when synching to disk, which is not handled here.
 */
int
hammer_add_directory(struct hammer_transaction *trans,
		     struct hammer_inode *dip, struct namecache *ncp,
		     struct hammer_inode *ip)
{
	struct hammer_record *record;
	int error;
	int bytes;

	record = hammer_alloc_ip_record(trans, dip);

	bytes = ncp->nc_nlen + 1;

	record->rec.entry.base.base.obj_id = dip->obj_id;
	record->rec.entry.base.base.key = hammer_directory_namekey(ncp->nc_name, bytes - 1);
	record->rec.entry.base.base.key += trans->hmp->namekey_iterator++;
	record->rec.entry.base.base.create_tid = trans->tid;
	record->rec.entry.base.base.rec_type = HAMMER_RECTYPE_DIRENTRY;
	record->rec.entry.base.base.obj_type = ip->ino_rec.base.base.obj_type;
	record->rec.entry.base.base.obj_id = ip->obj_id;
	if (bytes <= sizeof(record->rec.entry.den_name)) {
		record->data = (void *)record->rec.entry.den_name;
	} else {
		record->data = kmalloc(bytes, M_HAMMER, M_WAITOK);
		record->flags |= HAMMER_RECF_ALLOCDATA;
		bcopy(ncp->nc_name, record->data, bytes);
	}
	record->data_len = bytes;
	++dip->ino_rec.ino_nlinks;
	hammer_modify_inode(trans, dip, HAMMER_INODE_RDIRTY);
	error = hammer_add_record(trans, record);
	return(error);
}

/*
 * Allocate a record for the caller to finish filling in
 */
struct hammer_record *
hammer_alloc_ip_record(struct hammer_transaction *trans, hammer_inode_t ip)
{
	hammer_record_t record;

	record = kmalloc(sizeof(*record), M_HAMMER, M_WAITOK|M_ZERO);
	record->last_tid = trans->tid;
	record->ip = ip;
	return (record);
}

/*
 * Free a record.  Clean the structure up even though we are throwing it
 * away as a sanity check.
 */
void
hammer_free_ip_record(struct hammer_record *record)
{
	if (record->flags & HAMMER_RECF_ONRBTREE) {
		RB_REMOVE(hammer_rec_rb_tree, &record->ip->rec_tree, record);
		record->flags &= ~HAMMER_RECF_ONRBTREE;
	}
	if (record->flags & HAMMER_RECF_ALLOCDATA) {
		kfree(record->data, M_HAMMER);
		record->flags &= ~HAMMER_RECF_ALLOCDATA;
	}
	record->data = NULL;
	kfree(record, M_HAMMER);
}

/*
 * Add the record to the inode's rec_tree.  Directory entries
 */
static
int
hammer_add_record(struct hammer_transaction *trans, hammer_record_t record)
{
	while (RB_INSERT(hammer_rec_rb_tree, &record->ip->rec_tree, record)) {
		if (record->rec.base.base.rec_type != HAMMER_RECTYPE_DIRENTRY){
			hammer_free_ip_record(record);
			return (EEXIST);
		}
		record->rec.base.base.key &= ~(0xFFFFFFFFLL);
		record->rec.base.base.key |= trans->hmp->namekey_iterator++;
	}
	record->flags |= HAMMER_RECF_ONRBTREE;
	return(0);
}

#if 0
/*
 * Delete records belonging to the specified range.  Deal with edge and
 * overlap cases.  This function sets the delete tid and breaks adds
 * up to two records to deal with edge cases, leaving the range as a gap.
 * The caller will then add records as appropriate.
 */
int
hammer_delete_records(struct hammer_transaction *trans,
		       struct hammer_inode *ip,
		       hammer_base_elm_t ran_beg, hammer_base_elm_t ran_end)
{
}

#endif
