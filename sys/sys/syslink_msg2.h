/*
 * Copyright (c) 2004-2006 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/sys/syslink_msg2.h,v 1.1 2006/08/08 01:27:14 dillon Exp $
 */
/*
 * The syslink infrastructure implements an optimized RPC mechanism across a 
 * communications link.  RPC functions are grouped together into larger
 * protocols.  Prototypes are typically associated with system structures
 * but do not have to be.
 *
 * syslink 	- Implements a communications end-point and protocol.  A
 *		  syslink is typically directly embedded in a related
 *		  structure.
 *
 * syslink_proto- Specifies a set of RPC functions.
 *
 * syslink_desc - Specifies a single RPC function within a protocol.
 */

#ifndef _SYS_SYSLINK2_H_
#define _SYS_SYSLINK2_H_

#ifndef _SYS_SYSLINK_H_
#include <sys/syslink.h>
#endif
#ifndef _MACHINE_CPUFUNC_H_
#include <machine/cpufunc.h>
#endif

#ifndef _KERNEL
#ifndef _ASSERT_H_
#include <assert.h>
#endif
#define KKASSERT(exp)	assert(exp)
#endif

/*
 * Basic initialization of a message structure.  Returns a cursor.
 * 'bytes' represents the amount of reserved space in the message (or the
 * size of the message buffer) and must already be 8-byte aligned.
 */
static __inline
syslink_msg_t
syslink_msg_init(void *buf, sl_cid_t cmdid, int bytes, int *cursor)
{
	struct syslink_msg *msg = buf;

	msg->msgid = -1;		/* not yet assigned / placeholder */
	msg->cid = cmdid;		/* command identifier */
	msg->reclen = bytes;		/* reserved size, in bytes */
	*cursor = sizeof(*msg);
	return (msg);
}

/*
 * Basic completion of a constructed message buffer.  The cursor is used to
 * determine the actual size of the message and any remaining space is
 * converted to PAD.
 *
 * NOTE: The original reserved space in the message is required to have been
 * aligned.  We maintain FIFO atomicy by setting up the PAD before we fixup
 * the record length.
 */
static __inline
void
syslink_msg_done(syslink_msg_t msg, int cursor)
{
	syslink_msg_t tmp;
	int n;

	n = (cursor + SL_ALIGNMASK) & ~SL_ALIGNMASK;
	if (n != msg->reclen) {
		tmp = (syslink_msg_t)((char *)msg + n);
		tmp->msgid = 0;
		tmp->cid = 0;
		tmp->reclen = msg->reclen - n;
	}
	cpu_sfence();
	msg->reclen = cursor;
}

/*
 * Inline routines to help construct messages.
 */

/*
 * Push a recursive syslink_item.  SLIF_RECURSION must be set in the
 * passed itemid.  A pointer to the item will be returned and the cursor
 * will be updated.  The returned item must be used in a future call
 * to syslink_item_pop().
 */
static __inline
syslink_item_t
syslink_item_push(syslink_msg_t msg, sl_cid_t itemid, int *cursor)
{
	syslink_item_t item;

	item = (char *)msg + *cursor;
	item->itemid = itemid;
	item->auxdata = 0;
	KKASSERT(itemid & SLIF_RECURSION);
	*cursor += sizeof(struct syslink_item);
	KKASSERT(*cursor <= msg->reclen);

	return (item);
}

/*
 * Pop a previously pushed recursive item.  Use the cursor to figure out
 * the item's record length.
 */
static __inline
void
syslink_item_pop(syslink_msg_t msg, syslink_item_t item, int *cursor)
{
	item->reclen = *cursor - ((char *)item - (char *)msg);
}

/*
 * Construct a leaf node whos total size is 'bytes'.  The recursion bit
 * must not be set in the itemid.  The cursor will be updated.
 *
 * A pointer to the leaf item is returned as a void * so the caller can
 * assign it to an extended structural pointer.
 */
static __inline
void *
syslink_item_leaf(syslink_msg_t msg, sl_cid_t itemid, int bytes, int *cursor)
{
	syslink_item_t item;

	item = (char *)msg + *cursor;
	item->reclen = bytes;
	item->itemid = itemid;
	item->auxdata = 0;
	*cursor += (bytes + SL_ALIGNMASK) & ~SL_ALIGNMASK;
	KKASSERT(*cursor <= msg->reclen && (itemid & SLIF_RECURSION) == 0);
	return (item);
}

#endif

