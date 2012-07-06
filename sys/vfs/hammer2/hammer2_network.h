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
 * recorded.  These are sent without the CREATE, DELETE, or ABORT flags set.
 * ABORT is not supported for one-off messages.  The REPLY bit can be used
 * to distinguish between command and status if desired.
 *
 * Persistent-state messages are messages which require a reply to be
 * returned.  These messages can also consist of multiple message elements
 * for the command or reply or both (or neither).  The command message
 * sequence sets CREATE on the first message and DELETE on the last message.
 * A single message command sets both (CREATE|DELETE).  The reply message
 * sequence works the same way but of course also sets the REPLY bit.
 *
 * Persistent-state messages can be aborted by sending a message element
 * with the ABORT flag set.  This flag can be combined with either or both
 * the CREATE and DELETE flags.  When combined with the CREATE flag the
 * command is treated as non-blocking but still executes.  Whem combined
 * with the DELETE flag no additional message elements are required.
 *
 * ABORT SPECIAL CASE - Mid-stream aborts.  A mid-stream abort can be sent
 * when supported by the sender by sending an ABORT message with neither
 * CREATE or DELETE set.  This effectively turns the message into a
 * non-blocking message (but depending on what is being represented can also
 * cut short prior data elements in the stream).
 *
 * ABORT SPECIAL CASE - Abort-after-DELETE.  Persistent messages have to be
 * abortable if the stream/pipe/whatever is lost.  In this situation any
 * forwarding relay needs to unconditionally abort commands and replies that
 * are still active.  This is done by sending an ABORT|DELETE even in
 * situations where a DELETE has already been sent in that direction.  This
 * is done, for example, when links are in a half-closed state.  In this
 * situation it is possible for the abort request to race a transition to the
 * fully closed state.  ABORT|DELETE messages which race the fully closed
 * state are expected to be discarded by the other end.
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

	uint16_t	source;		/* command originator linkid */
	uint16_t	target;		/* reply originator linkid */
	uint32_t	msgid;		/* {source,target,msgid} unique */

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

#define HAMMER2_MSGF_BASECMDMASK	(HAMMER2_MSGF_CMDS |	\
					 HAMMER2_MSGF_SIZE |	\
					 HAMMER2_MSGF_PROTOS)

#define HAMMER2_MSGF_TRANSMASK		(HAMMER2_MSGF_CMDS |	\
					 HAMMER2_MSGF_SIZE |	\
					 HAMMER2_MSGF_PROTOS |	\
					 HAMMER2_MSGF_REPLY |	\
					 HAMMER2_MSGF_CREATE |	\
					 HAMMER2_MSGF_DELETE)

#define HAMMER2_MSG_PROTO_LNK		0x00000000U
#define HAMMER2_MSG_PROTO_DBG		0x00100000U
#define HAMMER2_MSG_PROTO_DOM		0x00200000U
#define HAMMER2_MSG_PROTO_CAC		0x00300000U
#define HAMMER2_MSG_PROTO_QRM		0x00400000U
#define HAMMER2_MSG_PROTO_BLK		0x00500000U
#define HAMMER2_MSG_PROTO_VOP		0x00600000U

/*
 * Message command constructors, sans flags
 */
#define HAMMER2_MSG_ALIGN		32
#define HAMMER2_MSG_ALIGNMASK		(HAMMER2_MSG_ALIGN - 1)
#define HAMMER2_MSG_DOALIGN(bytes)	(((bytes) + HAMMER2_MSG_ALIGNMASK) & \
					 ~HAMMER2_MSG_ALIGNMASK)
#define HAMMER2_MSG_HDR_ENCODE(elm)	(((uint32_t)sizeof(struct elm) + \
					  HAMMER2_MSG_ALIGNMASK) /	\
				         HAMMER2_MSG_ALIGN)

#define HAMMER2_MSG_LNK(cmd, elm)	(HAMMER2_MSG_PROTO_LNK |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_DBG(cmd, elm)	(HAMMER2_MSG_PROTO_DBG |	\
					 ((cmd) << 8) | 		\
					 HAMMER2_MSG_HDR_ENCODE(elm))

#define HAMMER2_MSG_DOM(cmd, elm)	(HAMMER2_MSG_PROTO_DOM |	\
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
 * AUTH		- Authenticate the connection, negotiate administrative
 *		  rights & encryption, protocol class, etc.  Only PAD and
 *		  AUTH messages (not even PING) are accepted until
 *		  authentication is complete.  This message also identifies
 *		  the host.
 *
 * PING		- One-way message on link-0, keep-alive, run by both sides
 *		  typically 1/sec on idle link, link is lost after 10 seconds
 *		  of inactivity.
 *
 * STATUS	- One-way message on link-0, host-spanning tree message.
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
#define HAMMER2_LNK_SPAN	HAMMER2_MSG_LNK(0x011, hammer2_lnk_span)
#define HAMMER2_LNK_ERROR	HAMMER2_MSG_LNK(0xFFF, hammer2_msg_hdr)

/*
 * SPAN - Registration (transaction, left open)
 *
 * This message registers a PFS/PFS_TYPE with the other end of the connection,
 * telling the other end who we are and what we can provide or what we want
 * to consume.  Multiple registrations can be maintained as open transactions
 * with each one specifying a unique {source} linkid.
 *
 * Registrations are sent from {source}=S {1...n} to {target}=0 and maintained
 * as open transactions.  Registrations are also received and maintains as
 * open transactions, creating a matrix of linkid's.
 *
 * While these transactions are open additional transactions can be executed
 * between any two linkid's {source}=S (registrations we sent) to {target}=T
 * (registrations we received).
 *
 * Closure of any registration transaction will automatically abort any open
 * transactions using the related linkids.  Closure can be initiated
 * voluntarily from either side with either end issuing a DELETE, or they
 * can be ABORTed.
 *
 * Status updates are performed via the open transaction.
 *
 * --
 *
 * A registration identifies a node and its various PFS parameters including
 * the PFS_TYPE.  For example, a diskless HAMMER2 client typically identifies
 * itself as PFSTYPE_CLIENT.
 *
 * Any node may serve as a cluster controller, aggregating and passing
 * on received registrations, but end-points do not have to implement this
 * ability.  Most end-points typically implement a single client-style or
 * server-style PFS_TYPE and rendezvous at a cluster controller.
 *
 * The cluster controller does not aggregate/pass-on all received
 * registrations.  It typically filters what gets passed on based on
 * what it receives.
 *
 * STATUS UPDATES: Status updates use the same structure but typically
 *		   only contain incremental changes to pfs_type, with the
 *		   label field containing a text status.
 */
struct hammer2_lnk_span {
	hammer2_msg_hdr_t head;
	uuid_t		pfs_id;		/* rendezvous pfs uuid */
	uuid_t		pfs_fsid;	/* unique pfs uuid */
	uint8_t		pfs_type;	/* peer type */
	uint8_t		reserved01;
	uint16_t	proto_version;	/* high level protocol support */
	uint32_t	status;		/* status flags */
	uint8_t		reserved02[8];
	uint32_t	reserved03[16];
	char		label[256];	/* PFS label (can be wildcard) */
};

typedef struct hammer2_lnk_span hammer2_lnk_span_t;

#define HAMMER2_SPAN_PROTO_1	1

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
 * Domain layer ops operate on any link, link-0 may be used when the
 * directory connected target is the desired registration.
 *
 * (nothing defined)
 */

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

union hammer2_msg_any {
	char			buf[HAMMER2_MSGHDR_MAX];
	hammer2_msg_hdr_t	head;
	hammer2_lnk_span_t	lnk_span;
};

typedef union hammer2_msg_any hammer2_msg_any_t;

#endif
