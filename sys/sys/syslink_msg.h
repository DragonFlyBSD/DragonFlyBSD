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
 * $DragonFly: src/sys/sys/syslink_msg.h,v 1.6 2007/04/03 20:21:19 dillon Exp $
 */
/*
 * The syslink infrastructure implements an optimized RPC mechanism across a 
 * communications link.  Endpoints, defined by a sysid, are typically 
 * associated with system structures but do not have to be.
 *
 * This header file is primarily responsible for the formatting of message
 * traffic over a syslink.
 */

#ifndef _SYS_SYSLINK_MSG_H_
#define _SYS_SYSLINK_MSG_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _MACHINE_ATOMIC_H_
#include <machine/atomic.h>
#endif

typedef u_int32_t	sl_msgid_t;	/* transaction sequencing */
typedef u_int16_t	sl_cmd_t;	/* command or error */
typedef sl_cmd_t	sl_error_t;	
typedef u_int16_t	sl_itemid_t;	/* item id */
typedef u_int16_t	sl_reclen_t;	/* item length */

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
 * Sessions are identified with a session id.  The session id is a rendezvous
 * id that associates physical and logical routing information with a single
 * sysid, allowing us to both avoid storing the source and target logical id
 * in the syslink message AND ALSO providing a unique session id and validator
 * which manages the abstracted connection between two entities.  Otherwise
 * the syslink message would become bloated with five sysid fields instead
 * of the three we have now.
 *
 * Link layer communications is accomplished by specifying a target physical
 * address of 0.
 *
 * The target physical address is deconstructed as the message hops across
 * the mesh.  All 0's, or all 0's remaining indicates a link layer message
 * to be processed by the syslink route node itself.  All 1's indicates
 * a broadcast message.  Broadcast messages also require special attention.
 * Sending a message to a target address of 0 basically sends it to the
 * directly connected syslink node. 
 *
 * The source physical address normally starts out as 0 and is constructed
 * as the message hops across the mesh.  The target can use the constructed
 * source address to respond to the originator of the message (as it must
 * if it has not knowledge about the session id).  A target with knowledge
 * of the session id has the option of forging its own return both.
 */

/*
 * Raw protocol structures
 */
struct syslink_msg {
	sl_cmd_t	sm_cmd;         /* protocol command code */
	sl_reclen_t	sm_bytes;       /* unaligned size of message */
	sl_msgid_t	sm_msgid;	/* message transaction control */
	/* minimum syslink_msg size is 8 bytes (special PAD) */
	sysid_t		sm_sessid;	/* session id */
	sysid_t		sm_srcphysid;	/* originating physical id */
	sysid_t		sm_dstphysid;	/* target physical id */
};

#define SL_MIN_MESSAGE_SIZE	offsetof(struct syslink_msg, sm_sessid)

#define SL_MSGID_REPLY		0x80000000	/* command vs reply */
#define SL_MSGID_ORIG		0x40000000	/* originator transaction */
						/* (else target transaction) */
#define SL_MSGID_BEG		0x20000000	/* first msg in transaction */
#define SL_MSGID_END		0x10000000	/* last msg in transaction */
#define SL_MSGID_STRUCTURED	0x08000000	/* contains structured data */
#define SL_MSGID_COMPLETE	0x04000000	/* msg not under construction */
#define SL_MSGID_TRANS_MASK	0x00FFFF00	/* transaction id */
#define SL_MSGID_SEQ_MASK	0x000000FF	/* sequence no within trans */

#define SLMSG_ALIGN(bytes)	(((bytes) + 7) & ~7)

/*
 * Syslink message commands (16 bits, bit 15 must be 0)
 *
 * Commands 0x0000-0x001F are reserved for the universal link layer, but
 * except for 0x0000 (which is a PAD message), must still be properly
 * routed.
 *
 * Commands 0x0020-0x002F are reserved for the universal protocol
 * identification layer.
 *
 * Commands 0x0100-0x7FFF are protocol commands.
 *
 * The command field is the error return field with bit 15 set in the
 * reply.
 */
#define SL_CMD_PAD		0x0000
#define SL_CMD_LINK_MESH	0x0001	/* mesh construction */
#define SL_CMD_LINK_REG		0x0002	/* register logical id */
#define SL_CMD_LINK_DEREG	0x0003	/* unregister logical id */
#define SL_CMD_LINK_ID		0x0004	/* link level identification */

#define SL_CMD_PROT_ID		0x0010	/* protocol & device ident */

/*
 * Message elements for structured messages.  If SL_MSGID_STRUCTURED is
 * set the syslink message contains zero or more syslink_elm structures
 * laid side by side.  Each syslink_elm structure may or may not be 
 * structured (i.e. recursive).  
 *
 * Most of the same SL_MSGID_* bits apply.  The first structured element
 * will have SL_MSGID_BEG set, the last will have SL_MSGID_END set (if
 * there is only one element, both bits will be set in that element).  If
 * the payload is structured, SL_MSGID_STRUCTURED will be set.
 *
 * syslink_elm's may use the TRANS and SEQ bits in the msgid for other
 * purposes.   A syslink_elm is considered to be a PAD if se_cmd == 0.
 */
struct syslink_elm {
	sl_cmd_t	se_cmd;
	sl_reclen_t	se_bytes;
	sl_msgid_t	se_msgid;
	/* extended by data */
};

typedef struct syslink_msg	*syslink_msg_t;
typedef struct syslink_elm	*syslink_elm_t;

#endif

