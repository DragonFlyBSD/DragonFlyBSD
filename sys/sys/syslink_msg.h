/*
 * Copyright (c) 2004-2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/sys/syslink_msg.h,v 1.7 2007/04/26 02:11:00 dillon Exp $
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
typedef u_int32_t	sl_auxdata_t;	/* auxillary data element */
typedef u_int16_t	sl_cmd_t;	/* command or error */
typedef u_int16_t	sl_error_t;	
typedef u_int16_t	sl_itemid_t;	/* item id */
typedef u_int16_t	sl_reclen_t;	/* item length */

#define SL_ALIGN	8		/* 8-byte alignment */
#define SL_ALIGNMASK	(SL_ALIGN - 1)

/*
 * The msgid is used to control transaction sequencing within a session, but
 * also has a special meaning to the transport layer.  A msgid of 0 indicates
 * a PAD syslink message, used to pad FIFO buffers to prevent messages from
 * being bisected by the end of the buffer.  Since all structures are 8-byte
 * aligned, 8-byte PAD messages are allowed.  All other messages must be
 * at least sizeof(syslink_msg).
 *
 * The reclen is the actual record length in bytes prior to alignment.
 * The reclen must be aligned to obtain the actual size of a syslink_msg
 * or syslink_item structure.  Note that the reclen includes structural
 * headers (i.e. it does not represent just the data payload, it represents
 * the entire structure).
 *
 * Syslink messages allow special treatment for large data payloads, allowing
 * the transport mechanism to separate the data payload into its own buffer
 * or DMA area (for example, its own page), facilitating DMA and page-mapping
 * operations at the end points while allowing the message to be maximally
 * compressed during transport.  This is typically handled by special casing
 * a readv() or writev().
 *
 * Sessions are identified with a session id.  The session id is a rendezvous
 * id that associates physical and logical routing information with a single
 * sysid, allowing us to both avoid storing the source and target logical id
 * in the syslink message AND ALSO providing a unique session id and validator
 * which manages the abstracted 'connection' between two entities.  This
 * reduces bloat.
 *
 * The target physical address is deconstructed as the message hops across
 * the mesh.  All 0's, or all 0's remaining indicates a link layer message
 * to be processed by the syslink route node itself.  All 1's indicates
 * a broadcast message.  Broadcast messages also require special attention.
 * Sending a message to a target address of 0 basically sends it to the
 * nearest route node as a link layer message.
 *
 * The source physical address normally starts out as 0 and is constructed
 * as the message hops across the mesh.  The target can use the constructed
 * source address to respond to the originator of the message (as it must
 * if it has no knowledge about the session).  A target with knowledge
 * of the session id has the option of forging its own return path.
 *
 * Checksums are the responsibility of higher layers but message checking
 * elements can be negotiated or required as part of the syslink message's
 * structured data.
 */
struct syslink_msg {
	sl_msgid_t	sh_msgid;	/* message transaction control */
	sl_reclen_t	sh_payloadoff;	/* offset of payload as a DMA aid */
	sl_reclen_t	sh_bytes;       /* unaligned size of message */
	/* minimum syslink_msg size is 8 bytes (special PAD) */
	sysid_t		sh_sessid;	/* session id */
	sysid_t		sh_srcphysid;	/* transit routing */
	sysid_t		sh_dstphysid;	/* transit routing */
	/* 8-byte aligned structure */
	/* followed by structured data */
};

/*
 * MSGID handling.  This controls message transactions and PAD.  Terminal
 * nodes, such as filesystems, are state driven entities whos syslink
 * message transactions are directly supported by the local on-machine route
 * nodes they connect to.  The route nodes use various fields in the header,
 * particularly sm_msgid, sm_sessid, and sm_payloadoff, to optimally present
 * syslink messages to the terminal node.  In particular, a route node may
 * present the payload for a syslink message or the message itself through
 * some out-of-band means, such as by mapping it into memory.
 *
 * These route nodes also handle timeout and retry processing, providing
 * appropriate response messages to terminal nodes if the target never replies
 * to a transaction or some other exceptional condition occurs.  The route
 * node does not handle RETRY and other exceptional conditions itself..
 * that is, the route node is not responsible for storing the message, only
 * routing it.  The route node only tracks the related session(s).
 *
 * A route node only directly supports terminal nodes directly connected to
 * it.  Intermediate route nodes ignore the MSGID (other then the all 0's PAD
 * case) and do not track indirect sessions.  For example, a piece of
 * hardware doing syslink message routing does not have to mess with
 * any of this.
 *
 * A session id establishes a session between two entities.  One terminal node
 * is considered to be the originator of the session, the other terminal node
 * is the target.  However, once established, EITHER ENTITY may initiate
 * a transaction (or both simulataniously).  SH_MSGID_CMD_ORIGINATOR is used
 * in all messages and replies related to a transaction initiated by the
 * session originator, and SH_MSGID_CMD_TARGET is used in all messages and
 * replies related to a transaction initiated by the session target.
 * Establishment of new sessions uses SH_MSGID_CMD_FORGE.
 *
 * Parallel transactions are supported by using different transaction ids
 * amoungst the parallel transactions.  Once a transaction id is used, it
 * may not be reused until after the timeout period is exceeded.  With 23
 * transaction id bits we have 8 million transaction ids, supporting around
 * 26000 transactions per second with a 5 minute timeout.  Note that
 * multiple sessions may be established between any two entities, giving us
 * essentially an unlimited number of transactions per second.
 *
 * ENDIANESS - syslink messages may be transported with any endianess.  This
 * includes all fields including the syslink header and syslink element
 * header fields.  If upon reception SH_MSGID_ENDIAN_NORM is set in the msgid 
 * both end-points will have the same endianess and no translation is
 * required.  If SH_MSGID_ENDIAN_REV is set then the two end-points have
 * different endianess and translation is required.   Only little endian and
 * bit endian transport is supported (that is, a simple reversal of bytes for
 * each field).
 *
 * Intermediate route nodes (i.e. those not tracking the session) may NOT
 * translate the endianess of the message in any fashion.  The management
 * node that talks to the actual resource is responsible for doing the
 * endian translations for all the above fields... everything except the
 * syslink_elm payload, which is described later.
 */
#define SL_MIN_MESSAGE_SIZE	offsetof(struct syslink_msg, sm_sessid)
#define SL_MSG_ALIGN(bytes)	(((bytes) + 7) & ~7)

#define SH_MSGID_CMD_MASK	0xF0000000
#define SH_MSGID_CMD_HEARTBEAT	0x60000000	/* seed heartbeat broadcast */
#define SH_MSGID_CMD_TIMESYNC	0x50000000	/* timesync broadcast */
#define SH_MSGID_CMD_ALLOCATE	0x40000000	/* allocate session id space */
#define SH_MSGID_CMD_ORIGINATOR	0x30000000	/* origin initiated trans */
#define SH_MSGID_CMD_TARGET	0x20000000	/* target initiated trans */
#define SH_MSGID_CMD_ESTABLISH	0x10000000	/* establish session */
#define SH_MSGID_CMD_PAD	0x00000000

#define SH_MSGID_REPLY		0x08000000
#define SH_MSGID_ENDIAN_NORM	0x01000000
#define SH_MSGID_ENDIAN_REV	0x00000001
#define SM_MSGID_TRANS_MASK	0x00FFFFFE	/* 23 bits */

/*
 * A syslink message is broken up into three pieces: (1) The headers, (2) The
 * message elements, and (3) DMA payload.
 *
 * A non-PAD syslink message contains a single top-level message element.
 * Unlike recursive message elements which can be iterated, the top level
 * element is never iterated.  There is always only one.  The top level
 * element is usually structured but does not have to be.  The top level
 * element's aux field represents the RPC protocol id for the command.
 *
 * A PAD syslink message contains no message elements.  The entire syslink
 * message is considered pad based on the header.
 *
 * A structured syslink message element may be specified by setting
 * SE_CMDF_STRUCTURED.  The data payload for a structured message element
 * is a sequence of ZERO or MORE message elements until the payload size is
 * reached.  Each message element may be opaque or structured.  Fully
 * recursive message elements are supported in this manner.
 *
 * A syslink message element with SE_CMDF_MASTERPAYLOAD set is associated
 * with the master payload for the syslink message as a whole.  This field
 * is only interpreted by terminal nodes and does not have to be used this
 * way, but its a good idea to for debugging purposes.
 *
 * Syslink message elements are always 8-byte aligned.  In order to
 * guarentee an 8-byte alignment for our extended data, a 32 bit auxillary
 * field is always included as part of the official syslink_elm structure
 * definition.  This field is actually part of the element command's data
 * and its use, if any, depends on the element command.
 *
 * Syslink message elements do not have to be validated by intermediate
 * route nodes but must ALWAYS be validated by the route node that connects
 * to the terminal node intended to receive the syslink message.
 *
 * Only the header fields of a syslink_elm are translated for endianess
 * by the management node.  If the management node does have to do an
 * endian conversion it will also set SE_CMDF_UNTRANSLATED in se_cmd (all
 * of them, recursively, since it has to validate and translate the entire
 * hierarchy anyway) and the rpc mechanism will be responsible for doing
 * the conversion and clearing the flag.  The seu_proto field IS always
 * translated, which means that when used as aux data it must be referenced
 * as a 32 bit field.
 *
 * As a fringe benefit, since the RPC command is the entire se_cmd field,
 * flags and all, an untranslated element will wind up with an unrecognized
 * command code and be reported as an error rather then being mis-executed.
 */
struct syslink_elm {
	sl_cmd_t	se_cmd;
	sl_reclen_t	se_bytes;
	union {
		sl_auxdata_t	seu_aux;	/* aux data */
		sl_auxdata_t	seu_proto;	/* protocol field */
	} u;
	/* extended by data */
};

#define SE_CMDF_STRUCTURED	0x8000		/* structured, else opaque */
#define SE_CMDF_RESERVED4000	0x4000
#define SE_CMDF_MASTERPAYLOAD	0x2000		/* DMA payload association */
#define SE_CMDF_UNTRANSLATED	0x1000		/* needs endian translation */

#define SE_CMD_PAD		0x0000		/* CMD 0 is always PAD */

typedef struct syslink_msg	*syslink_msg_t;
typedef struct syslink_elm	*syslink_elm_t;

#endif

