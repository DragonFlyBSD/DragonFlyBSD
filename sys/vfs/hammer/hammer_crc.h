/*
 * Copyright (c) 2007-2016 The DragonFly Project.  All rights reserved.
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
 */

#ifndef VFS_HAMMER_CRC_H_
#define VFS_HAMMER_CRC_H_

#include "hammer_disk.h"
#include "hammer_ioctl.h"

#ifndef _KERNEL
/*
 * These are only for userspace.
 * Userspace can't include sys/sys/systm.h.
 */
uint32_t crc32(const void *buf, size_t size);
uint32_t crc32_ext(const void *buf, size_t size, uint32_t ocrc);
#endif

static __inline hammer_crc_t
hammer_crc_get_blockmap(hammer_blockmap_t blockmap)
{
	return(crc32(blockmap, HAMMER_BLOCKMAP_CRCSIZE));
}

static __inline void
hammer_crc_set_blockmap(hammer_blockmap_t blockmap)
{
	blockmap->entry_crc = hammer_crc_get_blockmap(blockmap);
}

static __inline int
hammer_crc_test_blockmap(hammer_blockmap_t blockmap)
{
	return(blockmap->entry_crc == hammer_crc_get_blockmap(blockmap));
}

static __inline hammer_crc_t
hammer_crc_get_layer1(hammer_blockmap_layer1_t layer1)
{
	return(crc32(layer1, HAMMER_LAYER1_CRCSIZE));
}

static __inline void
hammer_crc_set_layer1(hammer_blockmap_layer1_t layer1)
{
	layer1->layer1_crc = hammer_crc_get_layer1(layer1);
}

static __inline int
hammer_crc_test_layer1(hammer_blockmap_layer1_t layer1)
{
	return(layer1->layer1_crc == hammer_crc_get_layer1(layer1));
}

static __inline hammer_crc_t
hammer_crc_get_layer2(hammer_blockmap_layer2_t layer2)
{
	return(crc32(layer2, HAMMER_LAYER2_CRCSIZE));
}

static __inline void
hammer_crc_set_layer2(hammer_blockmap_layer2_t layer2)
{
	layer2->entry_crc = hammer_crc_get_layer2(layer2);
}

static __inline int
hammer_crc_test_layer2(hammer_blockmap_layer2_t layer2)
{
	return(layer2->entry_crc == hammer_crc_get_layer2(layer2));
}

static __inline hammer_crc_t
hammer_crc_get_volume(hammer_volume_ondisk_t ondisk)
{
	return(crc32(ondisk, HAMMER_VOL_CRCSIZE1) ^
		crc32(&ondisk->vol_crc + 1, HAMMER_VOL_CRCSIZE2));
}

static __inline void
hammer_crc_set_volume(hammer_volume_ondisk_t ondisk)
{
	ondisk->vol_crc = hammer_crc_get_volume(ondisk);
}

static __inline int
hammer_crc_test_volume(hammer_volume_ondisk_t ondisk)
{
	return(ondisk->vol_crc == hammer_crc_get_volume(ondisk));
}

static __inline hammer_crc_t
hammer_crc_get_fifo_head(hammer_fifo_head_t head, int bytes)
{
	return(crc32(head, HAMMER_FIFO_HEAD_CRCOFF) ^
		crc32(head + 1, bytes - sizeof(*head)));
}

static __inline void
hammer_crc_set_fifo_head(hammer_fifo_head_t head, int bytes)
{
	head->hdr_crc = hammer_crc_get_fifo_head(head, bytes);
}

static __inline int
hammer_crc_test_fifo_head(hammer_fifo_head_t head, int bytes)
{
	return(head->hdr_crc == hammer_crc_get_fifo_head(head, bytes));
}

static __inline hammer_crc_t
hammer_crc_get_btree(hammer_node_ondisk_t node)
{
	return(crc32(&node->crc + 1, HAMMER_BTREE_CRCSIZE));
}

static __inline void
hammer_crc_set_btree(hammer_node_ondisk_t node)
{
	node->crc = hammer_crc_get_btree(node);
}

static __inline int
hammer_crc_test_btree(hammer_node_ondisk_t node)
{
	return(node->crc == hammer_crc_get_btree(node));
}

/*
 * Get the leaf->data_crc field.  Deal with any special cases given
 * a generic B-Tree leaf element and its data.
 *
 * NOTE: Inode-data: the atime and mtime fields are not CRCd,
 *       allowing them to be updated in-place.
 */
static __inline hammer_crc_t
hammer_crc_get_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
	hammer_crc_t crc;

	if (leaf->data_len == 0)
		return(0);

	switch(leaf->base.rec_type) {
	case HAMMER_RECTYPE_INODE:
		if (leaf->data_len != sizeof(struct hammer_inode_data))
			return(0);  /* This shouldn't happen */
		crc = crc32(data, HAMMER_INODE_CRCSIZE);
		break;
	default:
		crc = crc32(data, leaf->data_len);
		break;
	}
	return(crc);
}

static __inline void
hammer_crc_set_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
#ifdef _KERNEL
#ifdef INVARIANTS
	if (leaf->data_len && leaf->base.rec_type == HAMMER_RECTYPE_INODE)
		KKASSERT(leaf->data_len == sizeof(struct hammer_inode_data));
#endif
#endif
	leaf->data_crc = hammer_crc_get_leaf(data, leaf);
}

static __inline int
hammer_crc_test_leaf(void *data, hammer_btree_leaf_elm_t leaf)
{
	return(leaf->data_crc == hammer_crc_get_leaf(data, leaf));
}

static __inline hammer_crc_t
hammer_crc_get_mrec_head(hammer_ioc_mrecord_head_t head, int bytes)
{
	return(crc32(&head->rec_size, bytes - HAMMER_MREC_CRCOFF));
}

static __inline void
hammer_crc_set_mrec_head(hammer_ioc_mrecord_head_t head, int bytes)
{
	head->rec_crc = hammer_crc_get_mrec_head(head, bytes);
}

static __inline int
hammer_crc_test_mrec_head(hammer_ioc_mrecord_head_t head, int bytes)
{
	return(head->rec_crc == hammer_crc_get_mrec_head(head, bytes));
}

#endif /* !VFS_HAMMER_CRC_H_ */
