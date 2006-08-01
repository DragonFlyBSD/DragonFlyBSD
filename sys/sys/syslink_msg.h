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
 * $DragonFly: src/sys/sys/syslink_msg.h,v 1.1 2006/08/01 16:21:19 dillon Exp $
 */
/*
 * The syslink infrastructure implements an optimized RPC mechanism across a 
 * communications link.  Endpoints, defined by a sysid, are typically 
 * associated with system structures but do not have to be.
 *
 * syslink 	- Implements a communications end-point and protocol.  A
 *		  syslink is typically directly embedded in a related
 *		  structure.
 *
 * syslink_proto- Specifies a set of RPC functions.
 *
 * syslink_desc - Specifies a single RPC function within a protocol.
 */

#ifndef _SYS_SYSLINK_MSG_H_
#define _SYS_SYSLINK_MSG_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _MACHINE_ATOMIC_H_
#include <machine/atomic.h>
#endif

typedef u_int32_t	sl_cookie_t;
typedef u_int32_t	sl_msgid_t;
typedef u_int16_t	sl_cid_t;	/* command or item id, or error */

#define SL_ALIGN	8		/* 8-byte alignment */
#define SL_ALIGNMASK	(SL_ALIGN - 1)

/*
 * Stream or FIFO based messaging structures.
 * 
 * The reclen is the actual record length in bytes prior to alignment.
 * The reclen must be aligned to obtain the actual size of a syslink_msg
 * or syslink_item structure.  Note that the reclen includes structural
 * headers (i.e. it does not represent just the data payload, it represents
 * the entire structure).
 *
 * A message transaction consists of a command message and a reply message.
 * A message is uniquely identified by (src_sysid, dst_sysid, msgid).  Note
 * that both ends can initiate a unique command using the same msgid at 
 * the same time, there is no confusion between (src,dst,id) and (dst,src,id).
 * The reply message will have bit 31 set in the msgid.  Transactions occur
 * over reliable links and use a single-link abstraction (even if the
 * underlying topology is a mesh).  Therefore, there is no need to have
 * timeouts or retries.  If a link is lost the transport layer is responsible
 * for aborting any transactions which have not been replied to.
 *
 * Multiple transactions may run in parallel between two entities.  Only bit
 * 31 of the msgid is reserved (to indicate a reply).  Bits 30-0 may be
 * specified by the originator.  It is often convenient, but not required,
 * to shift the pointer to the internal representation of the command on the
 * originator a few bits and use that as the message id, removing the need
 * to keep track of unique ids.
 *
 * Both the governing syslink_msg and embedded syslink_items are limited to
 * 65535 bytes.  When pure data is passed in a message it is typically
 * limited to 32KB.  If transport is via a memory-FIFO or might pass through
 * a memory-FIFO, pad is usually added to prevent a message from wrapping
 * around the FIFO boundary, allowing the FIFO to be memory mapped.  This
 * is typically negotiated by the transport layer.
 *
 * In a command initiation the cid is the command.  The command format is
 * not specified over the wire (other then 0x0000 being reserved), but
 * typically the high 8 bits specify a protocol and the low 8 bits specify
 * an index into the protocol's function array.  Even though most sysid's
 * implement only a single protocol and overloading is possible, using
 * unique command id's across all protocols can be thought of as a safety.
 *
 * The cid field represents an error code in the response.
 *
 * The source and destination sysid is relative to the originator.   The
 * transport layer checks the high bits of the source sysid matches its
 * expectations.  These bits may be shortcut to a 0.  If the message is
 * transported to a non-local domain the transport layer must replace the 
 * 0 with the correct coordinates within the topology (insofar as it knows
 * them).  Since locality is layered not all high bits have to be replaced.
 * The transport for a higher layer will continue to adjust segments of 0's
 * as required to identify the message's source.
 *
 * PAD messages may be inserted in the stream by the transport layer(s) and
 * are always stripped on the receiving end.  However, they may be visible
 * to the receiver (e.g. if the transport is a memory mapped FIFO).  If
 * multiple transport hops occur PAD is typically reformulated and it is
 * up to the transport layer to decide whether it has to strip and
 * reformulate it, or whether it can just pass it through, or negotiate
 * a buffer alignment such that no reformulation is required.
 *
 * A PAD message uses a msgid of 0.  As a special case, PAD messages can be
 * as small as 8 bytes (msgid, cid, and reclen fields only).  The sysid
 * fields, if present, are ignored.
 */
struct syslink_msg {
	sl_msgid_t	msgid;		/* (32) transaction id for (src,dst) */
	sl_cid_t	cid;		/* (16) command or error code */
	sl_cid_t	reclen;		/* (16) */
	sysid_t		src_sysid;	/* (64) end-point identifier */
	sysid_t		dst_sysid;	/* (64) end-point identifier */
	/* 8-byte aligned */
	/* one or more embedded syslink_item structures */
};

#define SLMSGF_RESPONSE	((sl_msgid_t)0x80000000)

/*
 * A transaction contains any number of item structures.  Each item
 * structure may represent leaf data or a recursion into an array of
 * additional item structures.  The data type or field id is specified
 * by the low 14 bits of the 16 bit itemid.
 *
 * If the RECURSION bit (15) is set in the itemid, the item's data
 * represents any number of recursively embedded items.  
 * 
 * If the REFID bit (14) is set, it indicates that a leaf item is a 
 * sysid with a payload of exactly 8 bytes, and indicates that a recursion
 * contains at least one leaf item somewhere within it that is a sysid.
 *
 * The reserved bits are still counted in the 16 bit itemid so, for example,
 * an identifier of 0x0001 is different from 0x8001.  However, it is not
 * recommended that this sort of field id overloading be done.
 *
 * Both recursive and non-recursive items may use the aux field to hold
 * information.  Recursive items ONLY have the aux field available since
 * the data payload is the recursed item array.
 *
 * SYSID's passed as data (i.e. the REFID bit is set in the itemid) are
 * ref-counted by the transport layer.  XXX (need to flesh this out some
 * more w/ regard to temporary cache id's verses more persistent objects,
 * and how to deal with disconnection and reconnection).
 *
 * NOTE! The transport layer usually validates that the item data structure
 * is entirely correct before transmitting the message.  Additionally,
 * locally specified sysid's may have to be modified by the transport layer
 * if the message exits the local domain.
 */
struct syslink_item {
	sl_cid_t	itemid;
	sl_cid_t	reclen;
	int32_t		auxdata;
	/* 8-byte aligned */
	/* may be extended to include more data */
};

#define SLIF_RECURSION	((sl_itemid_t)0x8000)
#define SLIF_REFID	((sl_itemid_t)0x4000)

typedef struct syslink_msg	*syslink_msg_t;
typedef struct syslink_item	*syslink_item_t;

#endif

