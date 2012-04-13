/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
#ifndef VFS_HAMMER2_NETWORK_H_
#define VFS_HAMMER2_NETWORK_H_

#ifndef _VFS_HAMMER2_DISK_H_
#include "hammer2_disk.h"
#endif

/*
 * Mesh network protocol structures.
 *
 * The mesh is constructed from point-to-point streaming links with varying
 * levels of interconnectedness, forming a graph.  When a link is established
 * link id #0 is reserved for link-level communications.  This link is used
 * for authentication, registration, ping, further link id negotiations,
 * spanning tree, and so on.
 *
 * The spanning tree forms a weighted shortest-path-first graph amongst
 * those nodes with sufficient administrative rights to relay between
 * registrations.  Each link maintains a full reachability set, aggregates
 * it, and retransmits via the shortest path.  However, leaf nodes (even leaf
 * nodes with multiple connections) can opt not to be part of the spanning
 * tree and typically (due to administrative rights) their registrations
 * are not reported to other leafs.
 *
 * All message responses follow the SAME PATH that the original message
 * followed, but in reverse.  This is an absolute requirement since messages
 * expecting replies record persistent state at each hop.
 *
 * Message state is handled by the CREATE, DELETE, REPLY, and ABORT
 * flags.  Message state is typically recorded at the end points and
 * at each hop until a DELETE is received from both sides.
 *
 * One-way messages such as those used by spanning tree commands are not
 * recorded.  These are sent with no flags set.  Aborts and replies are not
 * possible.
 *
 * A normal message with no persistent state is sent with CREATE|DELETE and
 * the response is returned with REPLY|CREATE|DELETE.  A normal command can
 * be aborted by sending an ABORT message to the msgid that is in progress.
 * An ABORT sent by the originator must still wait for the reply from the
 * target and, since we've already sent the DELETE with our CREATE|DELETE,
 * may also cross the REPLY|CREATE|DELETE message in the opposite direction.
 * In this situation the message state has been destroyed on the target and
 * the target ignores the ABORT (because CREATE is not set, and
 * differentiated from one-way messages because ABORT is set).
 *
 * A command which has persistent state must maintain a persistent message.
 * For example, a lock or cache state request.  A persistent message is
 * initiated with just CREATE and the initial response is returned with
 * REPLY|CREATE.  Successive messages are sent with no flags and responses
 * with just REPLY.  The DELETE flag acts like a half-close (and degenerately
 * works the same as it does for normal messages).  This flag can be set
 * in the initial command or any successive command, and in the initial reply
 * or in any successive reply.  The recorded message state is destroyed when
 * both sides have sent a DELETE.
 *
 * Aborts for persistent messages work in the same fashion as they do for
 * normal messages, except that the target can also initiate an ABORT
 * by using ABORT|REPLY.  The target has one restriction, however, it cannot
 * send an ABORT with the CREATE flag set (i.e. as the initial reply),
 * because if the originator reuses the msgid the originator would not
 * then be able to determine that the ABORT is associated with the previous
 * session and not the new session.
 *
 * If a link failure occurs any active or persistent messages will be
 * auto-replied to the originator, and auto-aborted to the target.
 *
 * Additional features:
 *
 *	ABORT+CREATE	- This may be used to make a non-blocking request.
 *			  The target receives the normal command and is free
 *			  to ignore the ABORT flag, but may use it as an
 *			  indication that a non-blocking request is being
 *			  made.  The target must still reply the message of
 *			  course.  Works for normal and persistent messages
 *			  but does NOT work for one-way messages (because
 *			  ABORT alone without recorded msgid state has to be
 *			  ignored).
 *
 *	ABORT		- ABORT messages are allowed to bypass input queues.
 *			  Normal ABORTs are sent without the DELETE flag,
 *			  even for normal messages which had already set the
 *			  DELETE flag in the initial message.  This allows
 *			  the normal DELETE half-close operation to proceed
 *			  so an ABORT is basically advisory and the originator
 *			  must still wait for a reply.  Aborts are also
 *			  advisory when sent by targets.
 *
 *			  ABORT messages cannot be used with one-way messages
 *			  as this would cause such messages to be ignored.
 *
 *	ABORT+DELETE	- This is a special form of ABORT that allows the
 *			  recorded message state on the sender and on all
 *			  hops the message is relayed through to be destroyed
 *			  on the fly, as if a two-way DELETE had occurred.
 *			  It will cause an auto-reply or auto-abort to be
 *			  issued as if the link had been lost, but allows
 *			  the link to remain live.
 *
 *			  This form is basically like a socket close(),
 *			  where you aren't just sending an EOF but you are
 *			  completely aborting the request in both directions.
 *
 *			  This form cannot be used with CREATE as that could
 *			  generate a false reply if msgid is reused and
 *			  crosses the abort over the wire.
 *
 *			  ABORT messages cannot be used with one-way messages
 *			  as this would cause such messages to be ignored.
 *
 *	SUBSTREAMS	- Persistent messages coupled with the fact that
 *			  all commands and responses run through a single
 *			  chain of relays over reliable streams allows one
 *			  to treat persistent message updates as a data
 *			  stream and use the DELETE flag or an ABORT to
 *			  indicate EOF.
 *
 *
 *			NEGOTIATION OF {source} AND {target}
 *
 * In this discussion 'originator' describes the original sender of a message
 * and not the relays inbetween, while 'sender' describes the last relay.
 * The two mean the same thing only when the originator IS the last relay.
 *
 * The {source} field is sender-localized.  The sender assigns this field
 * based on which connection the message originally came from.  The initial
 * message as sent by the originator sets source=0.  This also means that a
 * leaf connection will always send messages with source=0.
 *
 * The {source} field must be re-localized at each hop, since messages
 * coming from multiple connections to a node will use conflicting
 * {source} values.  This can lead to linkid exhaustion which is discussed
 * a few paragraphs down.
 *
 * The {target} field is sender-allocated.  Messages sent to {target} are
 * preceeded by a FORGE message to {target} which associates a registration
 * with {target}, or UNFORGE to delete the associtation.
 *
 * The msgid field is 32 bits (remember some messages have long-lived
 * persistent state so this is important!).  One-way messages always use
 * msgid=0.
 *
 *				LINKID EXHAUSTION
 *
 * Because {source} must be re-localized at each hop it is possible to run
 * out of link identifiers.  At the same time we want to allow millions of
 * client/leaf connections, and 'millions' is a lot bigger than 65535.
 *
 * We also have a problem with the persistent message state... If a single
 * client's vnode cache has a million vnodes that can represent a million
 * persistent cache states.  Multiply by a million clients and ... oops!
 *
 * To solve these problems leafs connect into protocol-aggregators rather
 * than directly to the cluster.  The linkid and core message protocols only
 * occur within the cluster and not by the leafs.  A leaf can still connect
 * to multiple aggregators for redundancy if it desires but may have to
 * pick and choose which inodes go where since acquiring a cache state lock
 * over one connection will cause conflicts to be invalidated on the other.
 * In otherwords, there are limitations to this approach.
 *
 * A protocol aggregator takes any number of connections and aggregates
 * the operations down to a single linkid.  For example, this means that
 * the protocol aggregator is responsible for maintaining all the cache
 * state and performing crunches to reduce the overall amount of state
 * down to something the cluster core can handle.
 *
 * --
 *
 * All message headers are 32-byte aligned and sized (all command and
 * response structures must be 32-byte aligned), and all transports must
 * support message headers up to HAMMER2_MSGHDR_MAX.  The msg structure
 * can handle up to 8160 bytes but to keep things fairly clean we limit
 * message headers to 2048 bytes.
 *
 * Any in-band data is padded to a 32-byte alignment and placed directly
 * after the extended header (after the higher-level cmd/rep structure).
 * The actual unaligned size of the in-band data is encoded in the aux_bytes
 * field in this case.  Maximum data sizes are negotiated during registration.
 *
 * Use of out-of-band data must be negotiated.  In this case bit 31 of
 * aux_bytes will be set and the remaining bits will contain information
 * specific to the out-of-band transfer (such as DMA channel, slot, etc).
 *
 * (must be 32 bytes exactly to match the alignment requirement and to
 *  support pad records in shared-memory FIFO schemes)
 */
struct hammer2_msg_hdr {
	uint16_t	magic;		/* sanity, synchronization, endian */
	uint16_t	icrc1;		/* base header crc &salt on */
	uint32_t	salt;		/* random salt helps crypto/replay */

	uint16_t	source;		/* source linkid */
	uint16_t	target;		/* target linkid */
	uint32_t	msgid;		/* message id */

	uint32_t	cmd;		/* flags | cmd | hdr_size / 32 */
	uint16_t	error;		/* error field */
	uint16_t	resv05;

	uint16_t	icrc2;		/* extended header crc (after base) */
	uint16_t	aux_bytes;	/* aux data descriptor or size / 32 */
	uint32_t	aux_icrc;	/* aux data iscsi crc */
};

typedef struct hammer2_msg_hdr hammer2_msg_hdr_t;

#define HAMMER2_MSGHDR_MAGIC		0x4832
#define HAMMER2_MSGHDR_MAGIC_REV	0x3248
#define HAMMER2_MSGHDR_CRCOFF		offsetof(hammer2_msg_hdr_t, salt)
#define HAMMER2_MSGHDR_CRCBYTES		(sizeof(hammer2_msg_hdr_t) - 	\
					 HAMMER2_MSGHDR_CRCOFF)

/*
 * Administrative protocol limits.
 */
#define HAMMER2_MSGHDR_MAX		2048	/* msg struct max is 8192-32 */
#define HAMMER2_MSGAUX_MAX		65536	/* msg struct max is 2MB-32 */
#define HAMMER2_MSGBUF_SIZE		(HAMMER2_MSGHDR_MAX * 4)
#define HAMMER2_MSGBUF_MASK		(HAMMER2_MSGBUF_SIZE - 1)

/*
 * The message (cmd) field also encodes various flags and the total size
 * of the message header.  This allows the protocol processors to validate
 * persistency and structural settings for every command simply by
 * switch()ing on the (cmd) field.
 */
#define HAMMER2_MSGF_CREATE		0x80000000U	/* msg start */
#define HAMMER2_MSGF_DELETE		0x40000000U	/* msg end */
#define HAMMER2_MSGF_REPLY		0x20000000U	/* reply path */
#define HAMMER2_MSGF_ABORT		0x10000000U	/* abort req */
#define HAMMER2_MSGF_AUXOOB		0x08000000U	/* aux-data is OOB */
#define HAMMER2_MSGF_FLAG2		0x04000000U
#define HAMMER2_MSGF_FLAG1		0x02000000U
#define HAMMER2_MSGF_FLAG0		0x01000000U

#define HAMMER2_MSGF_FLAGS		0xFF000000U	/* all flags */
#define HAMMER2_MSGF_PROTOS		0x00F00000U	/* all protos */
#define HAMMER2_MSGF_CMDS		0x000FFF00U	/* all cmds */
#define HAMMER2_MSGF_SIZE		0x000000FFU	/* N*32 */

#define HAMMER2_MSGF_CMDSWMASK		(HAMMER2_MSGF_CMDS |	\
					 HAMMER2_MSGF_SIZE |	\
					 HAMMER2_MSGF_PROTOS |	\
					 HAMMER2_MSGF_REPLY)

#define HAMMER2_MSG_PROTO_LNK		0x00000000U
#define HAMMER2_MSG_PROTO_DBG		0x00100000U
#define HAMMER2_MSG_PROTO_CAC		0x00200000U
#define HAMMER2_MSG_PROTO_QRM		0x00300000U
#define HAMMER2_MSG_PROTO_BLK		0x00400000U
#define HAMMER2_MSG_PROTO_VOP		0x00500000U

/*
 * Message command constructors, sans flags
 */
#define HAMMER2_MSG_ALIGN		32
#define HAMMER2_MSG_ALIGNMASK		(HAMMER2_MSG_ALIGN - 1)
#define HAMMER2_MSG_DOALIGN(bytes)	(((bytes) + HAMMER2_MSG_ALIGNMASK) & \
					 ~HAMMER2_MSG_ALIGNMASK)
#define HAMMER2_MSG_HDR_ENCODE(elm)	((sizeof(struct elm) + 		\
					  HAMMER2_MSG_ALIGNMASK) /	\
				         HAMMER2_MSG_ALIGN)

#define HAMMER2_MSG_LNK(cmd, elm)	(HAMMER2_MSG_PROTO_LNK |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_DBG(cmd, elm)	(HAMMER2_MSG_PROTO_DBG |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_CAC(cmd, elm)	(HAMMER2_MSG_PROTO_CAC |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_QRM(cmd, elm)	(HAMMER2_MSG_PROTO_QRM |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_BLK(cmd, elm)	(HAMMER2_MSG_PROTO_BLK |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_VOP(cmd, elm)	(HAMMER2_MSG_PROTO_VOP |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

/*
 * Link layer ops basically talk to just the other side of a direct
 * connection.
 *
 * PAD		- One-way message on link-0, ignored by target.  Used to
 *		  pad message buffers on shared-memory transports.  Not
 *		  typically used with TCP.
 *
 * AUTHn	- Authenticate the connection, negotiate administrative
 *		  rights & encryption, protocol class, etc.  Only PAD and
 *		  AUTH messages (not even PING) are accepted until
 *		  authentication is complete.  This message also identifies
 *		  the host.
 *
 * PING		- One-way message on link-0, keep-alive, run by both sides
 *		  typically 1/sec on idle link, link is lost after 10 seconds
 *		  of inactivity.
 *
 * HSPAN	- One-way message on link-0, host-spanning tree message.
 *		  Connection and authentication status is propagated using
 *		  these messages on a per-connection basis.  Works like SPAN
 *		  but is only used for general status.  See the hammer2
 *		  'rinfo' command.
 *
 * SPAN		- One-way message on link-0, spanning tree message adds,
 *		  drops, or updates a remote registration.  Sent by both
 *		  sides, delta changes only.  Visbility into remote
 *		  registrations may be limited and received registrations
 *		  may be filtered depending on administrative controls.
 *
 *		  A multiply-connected node maintains SPAN information on
 *		  each link independently and then retransmits an aggregation
 *		  of the shortest-weighted path for each registration to
 *		  all links when a received change adjusts the path.
 *
 *		  The leaf protocol also uses this to make a PFS available
 *		  to the cluster (e.g. on-mount).
 */
#define HAMMER2_LNK_PAD		HAMMER2_MSG_LNK(0x000, hammer2_msg_hdr)
#define HAMMER2_LNK_PING	HAMMER2_MSG_LNK(0x001, hammer2_msg_hdr)
#define HAMMER2_LNK_AUTH	HAMMER2_MSG_LNK(0x010, hammer2_lnk_auth)
#define HAMMER2_LNK_HSPAN	HAMMER2_MSG_LNK(0x011, hammer2_lnk_hspan)
#define HAMMER2_LNK_SPAN	HAMMER2_MSG_LNK(0x012, hammer2_lnk_span)
#define HAMMER2_LNK_ERROR	HAMMER2_MSG_LNK(0xFFF, hammer2_msg_hdr)

/*
 * Debug layer ops operate on any link
 *
 * SHELL	- Persist stream, access the debug shell on the target
 *		  registration.  Multiple shells can be operational.
 */
#define HAMMER2_DBG_SHELL	HAMMER2_MSG_DBG(0x001, hammer2_dbg_shell)

struct hammer2_dbg_shell {
	hammer2_msg_hdr_t	head;
};
typedef struct hammer2_dbg_shell hammer2_dbg_shell_t;

/*
 * Cache layer ops operate on any link, link-0 may be used when the
 * directly connected target is the desired registration.
 *
 * LOCK		- Persist state, blockable, abortable.
 *
 *		  Obtain cache state (MODIFIED, EXCLUSIVE, SHARED, or INVAL)
 *		  in any of three domains (TREE, INUM, ATTR, DIRENT) for a
 *		  particular key relative to cache state already owned.
 *
 *		  TREE - Effects entire sub-tree at the specified element
 *			 and will cause existing cache state owned by
 *			 other nodes to be adjusted such that the request
 *			 can be granted.
 *
 *		  INUM - Only effects inode creation/deletion of an existing
 *			 element or a new element, by inumber and/or name.
 *			 typically can be held for very long periods of time
 *			 (think the vnode cache), directly relates to
 *			 hammer2_chain structures representing inodes.
 *
 *		  ATTR - Only effects an inode's attributes, such as
 *			 ownership, modes, etc.  Used for lookups, chdir,
 *			 open, etc.  mtime has no affect.
 *
 *		  DIRENT - Only affects an inode's attributes plus the
 *			 attributes or names related to any directory entry
 *			 directly under this inode (non-recursively).  Can
 *			 be retained for medium periods of time when doing
 *			 directory scans.
 *
 *		  This function may block and can be aborted.  You may be
 *		  granted cache state that is more broad than the state you
 *		  requested (e.g. a different set of domains and/or an element
 *		  at a higher layer in the tree).  When quorum operations
 *		  are used you may have to reconcile these grants to the
 *		  lowest common denominator.
 *
 *		  In order to grant your request either you or the target
 *		  (or both) may have to obtain a quorum agreement.  Deadlock
 *		  resolution may be required.  When doing it yourself you
 *		  will typically maintain an active message to each master
 *		  node in the system.  You can only grant the cache state
 *		  when a quorum of nodes agree.
 *
 *		  The cache state includes transaction id information which
 *		  can be used to resolve data requests.
 */
#define HAMMER2_CAC_LOCK	HAMMER2_MSG_CAC(0x001, hammer2_cac_lock)

/*
 * Quorum layer ops operate on any link, link-0 may be used when the
 * directly connected target is the desired registration.
 *
 * COMMIT	- Persist state, blockable, abortable
 *
 *		  Issue a COMMIT in two phases.  A quorum must acknowledge
 *		  the operation to proceed to phase-2.  Message-update to
 *		  proceed to phase-2.
 */
#define HAMMER2_QRM_COMMIT	HAMMER2_MSG_QRM(0x001, hammer2_qrm_commit)

/*
 * General message errors
 *
 *	0x00 - 0x1F	Local iocomm errors
 *	0x20 - 0x2F	Global errors
 */
#define HAMMER2_MSG_ERR_UNKNOWN		0x20

union hammer2_any {
	char			buf[HAMMER2_MSGHDR_MAX];
	hammer2_msg_hdr_t	head;
};

typedef union hammer2_any hammer2_any_t;

#endif
