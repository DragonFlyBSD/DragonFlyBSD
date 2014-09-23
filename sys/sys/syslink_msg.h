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
 */
/*
 * The syslink infrastructure implements an optimized RPC mechanism across a 
 * communications link.  Endpoints, defined by a session sysid, are typically 
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
#include <machine/atomic.h>

typedef int32_t		sl_auxdata_t;	/* auxillary data element */
typedef u_int32_t	sl_rlabel_t;	/* reply label routing id */
typedef u_int16_t	sl_proto_t;	/* protocol control field */
typedef u_int16_t	sl_cmd_t;	/* command/status id */
typedef u_int16_t	sl_reclen_t;	/* item length */

#define SL_ALIGN	8		/* 8-byte alignment */
#define SL_ALIGNMASK	(SL_ALIGN - 1)

/*
 * SYSLINK_ELM - structured data element.
 *
 * syslink_msg's have zero or more syslink_elm's arranged as an array.
 * Each syslink_elm may represent opaque data or recursively structured
 * data.
 *
 * SE_CMD field - identify RPC command (at the top level) or RPC data element
 *		  in deeper recursions.
 *
 *	Please note that while bits have individual meanings, command switches
 *	should universally compare all 16 bits against the command.  This
 *	guarentees that commands will not be misinterpreted (e.g. reply vs
 *	command, or data which has not been endian converted).
 *
 *	SE_CMDF_REPLY - is usually set in the top level syslink_elm embedded
 *	in syslink message replies as a safety in order to prevent a reply
 *	from being misinterpreted as a command.
 *
 *	SE_CMDF_STRUCTURED - indicates that the payload is an array of
 *	structured syslink_elm's, otherwise the payload is considered to
 *	be opaque.
 *
 *	SE_CMDF_GLOBAL - indicates that the command is globally defined by the
 *	syslink standard and is not protocol-specific.  Note that PADs
 *	are not global commands.
 *
 *	SE_CMDF_UNTRANSLATED - indicates that the syslink_elm structure had
 *	to be translated into host endian format but any directly or
 *	indirectly represented opaque data has not been.  This bit is used
 *	by the protocol layer to properly endian-translate protocol-specific
 *	opaque data.
 *
 *	SE_CMDF_ASIZE* - These 2 bits can encode the size for simple elments.
 *	The size is verified prior to delivery so command switches on simple
 *	elements need not check se_bytes.  This also makes it easier for
 *	the protocol code to do endian conversions.  These bits are not used
 *	for this purpose in sm_head.
 *
 *	SE_CMDF_DMAR
 *	SE_CMDF_DMAW - These bits share the same bit positions as the ASIZE
 *	bits and are used in sm_head to indicate the presence of an out of
 *	band DMA buffer associated with the message.  DMAW indicates that
 *	the originator is passing data to the target, DMAR indicates that
 *	the target is passing data to the originator.  Both bits may be set.
 *
 * SE_AUX field - auxillary data field (signed 32 bit integer)
 *
 *	This field contains protocol and command/element specific data.
 *	This typically contains an error code in replies (at least in
 *	sm_head).
 */
struct syslink_elm {
	sl_cmd_t	se_cmd;		/* syslink element command/status id */
	sl_reclen_t	se_bytes;	/* unaligned record size */
	sl_auxdata_t	se_aux;		/* auxillary data always present */
	/* extended by data */
};

#define SE_CMDF_REPLY		0x8000	/* safety feature */
#define SE_CMDF_STRUCTURED	0x4000	/* payload is structured */
#define SE_CMDF_GLOBAL		0x2000	/* non-proto-specific global cmd */
#define SE_CMDF_UNTRANSLATED	0x1000	/* needs endian translation */

#define SE_CMDF_ASIZEMASK	0x0C00	/* auto-size mask */
#define SE_CMDF_ASIZEX		0x0000	/* N bytes of extended data */
#define SE_CMDF_ASIZE_RESERVED	0x0400	/* reserved for future use */
#define SE_CMDF_ASIZE4		0x0800	/* 4 bytes of extended data */
#define SE_CMDF_ASIZE8		0x0C00	/* 8 bytes of extended data */

#define SE_CMDF_DMAR		0x0800	/* (structured only) originator reads */
#define SE_CMDF_DMAW		0x0400	/* (structured only) originator writes*/

#define SE_CMD_MASK		0x03FF

#define SE_CMD_PAD		0x0000	/* always reserved to mean PAD */

/*
 * SYSLINK_MSG - Syslink transactional command or response
 *
 * This structure represents a syslink transactional command or response
 * between two end-points identified by the session id.  Either end may
 * initiate a command independant of the other.  A transaction consists of
 * the sending of a command and the reception of a response.
 *
 * Multiple transactions in each direction (and both directions at once)
 * may occur in parallel.  The command/reply transaction space in one
 * direction is independant of the command/reply transaction space in the
 * other direction.
 *
 * SM_PROTO	rppppppx-ppppppx
 *
 *	r	0 = Command, 1 = Reply
 *
 *	x	Used to detect endian reversal.  The protocol id is OR'd
 *		with 0x0100 on transmission.  If we find bit 0 set to 1 on
 *		reception, endian translation must occur.
 *
 *	-	Reserved, must be 0
 *
 *	p12	Encoded protocol number.  Protocol 0 indicates PAD (r must
 *		be 0 as well).  Protocols 0-63 are reserved and may only be
 *		used when officially recognized by the DragonFly project.
 *		64-4095 are user defined.
 *
 * SM_BYTES	bbbbbbbbbbbbbbbb
 *
 *	b16	This is the size of the whole message, including headers
 *		but not including out-of-band DMA.  All messages must
 *		be 8-byte aligned.  Unlike syslink_elm structures, sm_bytes
 *		must be properly aligned.
 *
 * SM_RLABEL	llllllllllllllllllllllllllllllll
 *
 *	l32	This is a 32 bit reply label routing id.  The format of
 *		this field is defined by the transport layer.  The field
 *		is typically assigned in the command message as it passes
 *		through the transport layer and is retained verbatim in
 *		the reply message.
 *
 *		The most typical use of this field is as an aid to direct
 *		messages in a multi-threaded environment.  For example,
 *		a kernel talking to a filesystem over a syslink might
 *		identify the thread originating the command in this field
 *		in order to allow the reply to be routed directly back to
 *		that thread.
 *
 *		The field can also be used in crossbar switching meshes
 *		to identify both the originator and the target, but it
 *		should be noted that the verbatim requirement means the
 *		mesh must pick out the proper field based on the 'r'eply
 *		bit in sm_proto.
 *
 * SM_MSGID	m64
 *
 *	m64	This 64 bit message id combined with the mesh id and the
 *		'r'eply bit (and also the direction of the message when
 *		operating over a full-duplex syslink) uniquely identifies
 *		a syslink message.
 *
 *		The message id is typically set to the address of the
 *		syslink message or control structure used by the originator,
 *		or obtained from a 64 bit counter.  This way the originator
 *		can guarentee uniqueness without actually having to track
 *		message id allocations.
 *
 * SM_SESSID	s64
 *
 *	s64	This is a 64 bit session id key whos primary purpose is to
 *		validate a link and prevent improperly routed or stale
 *		messages from having an adverse effect on the cluster.  The
 *		field is typically left 0 for intra-host links.
 *
 * SM_HEAD	(structure)
 *
 *	All syslink messages other then PAD messages must contain at least
 *	one whole syslink_elm.  Elements are arranged in an array until
 *	the syslink message space is exhausted.  Each element may represent
 *	opaque data or recursively structured data.  Structured data consists
 *	of an array of 0 or more elements embedded in the parent element.
 *
 *
 * ENDIAN TRANSLATION - endian translation occurs when a message is received
 * with bit 0 set in sm_proto, indicating that the native endian mode of
 * the sender is different from the native endian mode of the receiver.
 * Endian translation is NOT specific to little or big endian formatting
 * but instead occurs only when the two sides have different native endian
 * modes.  All fields are interpreted structurally.  Only little and big
 * endian formats are supported (i.e. simple byte reversal).
 *
 * Translation consists of reversing the byte ordering for each structural
 * field.  Any syslink_elm structures are recursively translated as well,
 * but opaque data contained within is not.  The SE_CMDF_UNTRANSLATED bit
 * in each translated syslink_elm structure is flipped.
 *
 * Syslink routers and switches may or may not translate a syslink_msg (but
 * they must still properly interpret the syslink_msg header as the
 * message passes through).  It is possible for a message to be translated
 * multiple times while it transits the network so it is important when
 * translation occurs that the SE_CMDF_UNTRANSLATED bit in the syslink_elm
 * structures gets flipped rather then simply set.
 */
struct syslink_msg {
	sl_proto_t	sm_proto;	/* protocol id, endian, reply bit */
	sl_reclen_t	sm_bytes;       /* unaligned size of message */
	sl_rlabel_t	sm_rlabel;	/* reply label routing id */
	/* minimum syslink_msg size is 8 bytes (special PAD) */
	sysid_t		sm_msgid;	/* message id */
	sysid_t		sm_sessid;	/* session id */
	struct syslink_elm sm_head;	/* structured data */
};

/*
 * Minimum sizes for syslink pads and syslink messages.  Pads can be as
 * small as 8 bytes and are 8-byte aligned.  Syslink messages can be as
 * small as 16 bytes and are 8-byte aligned.
 */
#define SL_MIN_PAD_SIZE		offsetof(struct syslink_msg, sm_msgid)
#define SL_MIN_MSG_SIZE		sizeof(struct syslink_msg)
#define SL_MIN_ELM_SIZE		sizeof(struct syslink_elm)
#define SL_MSG_ALIGN(bytes)	(((bytes) + 7) & ~7)

/*
 * sm_proto field	rppppppx-PPPPPPx	encoded
 *			----ppppppPPPPPP	decoded
 *
 * Note: SMPROTO_ defines are in encoded form
 */
#define SM_PROTO_REPLY		0x8000
#define SM_PROTO_ENDIAN_NORM	0x0100
#define SM_PROTO_ENDIAN_REV	0x0001
#define SM_PROTO_ENCODE(n)	((((n) << 1) & ~127) | (((n) << 3) & 0x7E00) \
				 | SM_PROTO_ENDIAN_NORM)
#define SM_PROTO_DECODE(n)	((((n) >> 1) & 63) | (((n) >> 3) & )) 0x0FC0) \
				 | SM_PROTO_ENDIAN_NORM)

/*
 * Reserved protocol encodings 0-63
 */
#define SMPROTO_PAD		SM_PROTO_ENCODE(0x0000)

/*
 * high level protocol encodings
 */
#define SMPROTO_BSDVFS          SM_PROTO_ENCODE(0x0040)

/*
 * Syslink messages may contain recursive components.  The recursion depth
 * allowed is limited to SL_MAXDEPTH.
 *
 * Syslink messages, NON-inclusive of any DMA buffers, are limited to
 * SL_MAXSIZE bytes.  DMA buffer limitations are not defined here but
 * the expectation is that they can be fairly large.
 */
#define SL_MAXDEPTH	10
#define SL_MAXSIZE	4096

/*
 * slmsgalloc() sizes
 */
#define SLMSG_SMALL	256
#define SLMSG_BIG	SL_MAXSIZE


union syslink_small_msg {
	struct syslink_msg msg;
	char buf[SLMSG_SMALL];
};

union syslink_big_msg {
	struct syslink_msg msg;
	char buf[SLMSG_BIG];
};

typedef struct syslink_msg	*syslink_msg_t;
typedef struct syslink_elm	*syslink_elm_t;

#endif
