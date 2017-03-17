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
uint32_t iscsi_crc32(const void *buf, size_t size);
uint32_t iscsi_crc32_ext(const void *buf, size_t size, uint32_t ocrc);
#endif

#define hammer_datacrc(vers, buf, size)		\
	(((vers) >= HAMMER_VOL_VERSION_SEVEN) ? iscsi_crc32(buf, size) : crc32(buf, size))
#define hammer_datacrc_ext(vers, buf, size, ocrc)	\
	(((vers) >= HAMMER_VOL_VERSION_SEVEN) ? iscsi_crc32_ext(buf, size, ocrc) : \
						crc32_ext(buf, size, ocrc))

static __inline hammer_crc_t
hammer_crc_get_blockmap(uint32_t vol_version, hammer_blockmap_t blockmap)
{
	return(hammer_datacrc(vol_version, blockmap, HAMMER_BLOCKMAP_CRCSIZE));
}

static __inline void
hammer_crc_set_blockmap(uint32_t vol_version, hammer_blockmap_t blockmap)
{
	blockmap->entry_crc = hammer_crc_get_blockmap(vol_version, blockmap);
}

static __inline int
hammer_crc_test_blockmap(uint32_t vol_version, hammer_blockmap_t blockmap)
{
	if (blockmap->entry_crc == hammer_crc_get_blockmap(vol_version, blockmap))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (blockmap->entry_crc == hammer_crc_get_blockmap(HAMMER_VOL_VERSION_SIX,
								   blockmap)) {
			return 1;
		}
	}
	return 0;
}

static __inline hammer_crc_t
hammer_crc_get_layer1(uint32_t vol_version, hammer_blockmap_layer1_t layer1)
{
	return(hammer_datacrc(vol_version, layer1, HAMMER_LAYER1_CRCSIZE));
}

static __inline void
hammer_crc_set_layer1(uint32_t vol_version, hammer_blockmap_layer1_t layer1)
{
	layer1->layer1_crc = hammer_crc_get_layer1(vol_version, layer1);
}

static __inline int
hammer_crc_test_layer1(uint32_t vol_version, hammer_blockmap_layer1_t layer1)
{
	if (layer1->layer1_crc == hammer_crc_get_layer1(vol_version, layer1))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (layer1->layer1_crc == hammer_crc_get_layer1(HAMMER_VOL_VERSION_SIX, layer1))
			return 1;
	}
	return 0;
}

static __inline hammer_crc_t
hammer_crc_get_layer2(uint32_t vol_version, hammer_blockmap_layer2_t layer2)
{
	return(hammer_datacrc(vol_version, layer2, HAMMER_LAYER2_CRCSIZE));
}

static __inline void
hammer_crc_set_layer2(uint32_t vol_version, hammer_blockmap_layer2_t layer2)
{
	layer2->entry_crc = hammer_crc_get_layer2(vol_version, layer2);
}

static __inline int
hammer_crc_test_layer2(uint32_t vol_version, hammer_blockmap_layer2_t layer2)
{
	if (layer2->entry_crc == hammer_crc_get_layer2(vol_version, layer2))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (layer2->entry_crc == hammer_crc_get_layer2(HAMMER_VOL_VERSION_SIX, layer2))
			return 1;
	}
	return 0;
}

static __inline hammer_crc_t
hammer_crc_get_volume(uint32_t vol_version, hammer_volume_ondisk_t ondisk)
{
	return (hammer_datacrc(vol_version, ondisk, HAMMER_VOL_CRCSIZE1) ^
		hammer_datacrc(vol_version, &ondisk->vol_crc + 1, HAMMER_VOL_CRCSIZE2));
}

static __inline void
hammer_crc_set_volume(uint32_t vol_version, hammer_volume_ondisk_t ondisk)
{
	ondisk->vol_crc = hammer_crc_get_volume(vol_version, ondisk);
}

static __inline int
hammer_crc_test_volume(uint32_t vol_version, hammer_volume_ondisk_t ondisk)
{
	if (ondisk->vol_crc == hammer_crc_get_volume(vol_version, ondisk))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (ondisk->vol_crc == hammer_crc_get_volume(HAMMER_VOL_VERSION_SIX, ondisk))
			return 1;
	}
	return 0;
}

static __inline hammer_crc_t
hammer_crc_get_fifo_head(uint32_t vol_version, hammer_fifo_head_t head, int bytes)
{
	return(hammer_datacrc(vol_version, head, HAMMER_FIFO_HEAD_CRCOFF) ^
		hammer_datacrc(vol_version, head + 1, bytes - sizeof(*head)));
}

static __inline void
hammer_crc_set_fifo_head(uint32_t vol_version, hammer_fifo_head_t head, int bytes)
{
	head->hdr_crc = hammer_crc_get_fifo_head(vol_version, head, bytes);
}

static __inline int
hammer_crc_test_fifo_head(uint32_t vol_version, hammer_fifo_head_t head, int bytes)
{
	if (head->hdr_crc == hammer_crc_get_fifo_head(vol_version, head, bytes))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (head->hdr_crc == hammer_crc_get_fifo_head(HAMMER_VOL_VERSION_SIX,
							      head, bytes)) {
			return 1;
		}
	}
	return 0;
}

static __inline hammer_crc_t
hammer_crc_get_btree(uint32_t vol_version, hammer_node_ondisk_t node)
{
	return (hammer_datacrc(vol_version, &node->crc + 1, HAMMER_BTREE_CRCSIZE));
}

static __inline void
hammer_crc_set_btree(uint32_t vol_version, hammer_node_ondisk_t node)
{
	node->crc = hammer_crc_get_btree(vol_version, node);
}

static __inline int
hammer_crc_test_btree(uint32_t vol_version, hammer_node_ondisk_t node)
{
	if (node->crc == hammer_crc_get_btree(vol_version, node))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (node->crc == hammer_crc_get_btree(HAMMER_VOL_VERSION_SIX, node))
			return 1;
	}
	return 0;
}

/*
 * Get the leaf->data_crc field.  Deal with any special cases given
 * a generic B-Tree leaf element and its data.
 *
 * NOTE: Inode-data: the atime and mtime fields are not CRCd,
 *       allowing them to be updated in-place.
 */
static __inline hammer_crc_t
hammer_crc_get_leaf(uint32_t vol_version, void *data, hammer_btree_leaf_elm_t leaf)
{
	hammer_crc_t crc;

	if (leaf->data_len == 0)
		return(0);

	switch(leaf->base.rec_type) {
	case HAMMER_RECTYPE_INODE:
		if (leaf->data_len != sizeof(struct hammer_inode_data))
			return(0);  /* This shouldn't happen */
		crc = hammer_datacrc(vol_version, data, HAMMER_INODE_CRCSIZE);
		break;
	default:
		crc = hammer_datacrc(vol_version, data, leaf->data_len);
		break;
	}
	return(crc);
}

static __inline void
hammer_crc_set_leaf(uint32_t vol_version, void *data, hammer_btree_leaf_elm_t leaf)
{
#ifdef _KERNEL
#ifdef INVARIANTS
	if (leaf->data_len && leaf->base.rec_type == HAMMER_RECTYPE_INODE)
		KKASSERT(leaf->data_len == sizeof(struct hammer_inode_data));
#endif
#endif
	leaf->data_crc = hammer_crc_get_leaf(vol_version, data, leaf);
}

static __inline int
hammer_crc_test_leaf(uint32_t vol_version, void *data, hammer_btree_leaf_elm_t leaf)
{
	if (leaf->data_crc == hammer_crc_get_leaf(vol_version, data, leaf))
		return 1;
	if (vol_version >= HAMMER_VOL_VERSION_SEVEN) {
		if (leaf->data_crc == hammer_crc_get_leaf(HAMMER_VOL_VERSION_SIX, data, leaf))
			return 1;
	}
	return 0;
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
