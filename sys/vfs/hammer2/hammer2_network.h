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
 * levels of interconnectedness, forming a graph.  The spanning tree protocol
 * running on each node transmits a LNK_SPAN transactional message to the
 * other end.  The protocol collects LNK_SPAN messages from all sources,
 * aggregates them using a shortest-distance-path algorithm, and transmits
 * them over each link as well, creating a multplication within the topology.
 *
 * Any node in the graph may transmit a message to any other node by using
 * the msgid of the LNK_SPAN open transaction as the message's 'linkid'.
 * This identifies both sides so there is no 'source' and 'target' per-say.
 *
 * Open transactions are recorded by the source and the target, but not by
 * intermediate nodes in the route.  Streaming protocols are used.  If a
 * span element is lost its transaction will be aborted automatically (even
 * if other routes to the same target are available), and any related
 * messages will be aborted.  If the span element was chosen for aggregation
 * this will propagate through the entire topology and thus ultimately reach
 * the target which used the aggregated span element, but does not
 * necessarily effect all paths in the topology.
 *
 * When a link failure occurs all SPANs related to that link are
 * transactionally closed.  The SPANs are not deleted until closed in
 * both directions, thus the spanid serves as a placeholder allowing all
 * in-transit messages being routed over that spanid to be properly thrown
 * out.  Once completely closed the spanid can be reused.
 *
 * NOTE: Multiple spans for the same physical {fsid,pfs_fsid} can be
 *	 forwarded, allowing concurrency within the topology.
 *
 * NOTE: It is important that messages in a lost route be aborted because
 *	 the messaging protocol expects serialization over any given route.
 *	 Only propagated spans are forwarded as spans to other nodes, so any
 *	 given open span transaction will represent a specific path.
 *
 *	 If a portion of the path in the middle of the topology is lost it
 *	 will propagate in both directions all the way to the ends that used
 *	 it.  Intermediate route nodes DO NOT silently re-route messages to
 *	 another span.  Messages in-flight will meet the updating SPAN and
 *	 simply be discarded by intermediate nodes.  Ultimately the updating
 *	 SPAN reaches all end-points and auto-aborts the open transaction.
 *
 *	 If another path is available the transaction can be instantly
 *	 retried.
 *
 * NOTE: It is possible to route messages virtually using the msgid of any
 *	 open transaction instead of the msgid of a SPAN transaction, but
 *	 not recommended and not currently coded.
 *
 * NOTE: Both the msgid and the spanid are 64-bit fields and may be populated
 *	 with actual memory pointers (which simplifies the end-points).
 *	 However, all such identifiers must be indexed as appropriate by the
 *	 nodes and verified as being valid before any memory dereference
 *	 occurs, for obvious reasons.
 *
 * All message responses follow the SAME PATH that the original message
 * followed, but in reverse.  This is an absolute requirement since messages
 * expecting replies record persistent state at each hop.  Sequencing must
 * be preserved.
 *
 *			MESSAGE TRANSACTIONAL STATES
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
 * --
 *
 * All base and extended message headers are 64-byte aligned, and all
 * transports must support extended message headers up to HAMMER2_MSGHDR_MAX.
 * Currently we allow extended message headers up to 2048 bytes.  Note
 * that the extended header size is encoded in the 'cmd' field of the header.
 *
 * Any in-band data is padded to a 64-byte alignment and placed directly
 * after the extended header (after the higher-level cmd/rep structure).
 * The actual unaligned size of the in-band data is encoded in the aux_bytes
 * field in this case.  Maximum data sizes are negotiated during registration.
 *
 * Auxillary data can be in-band or out-of-band.  In-band data sets aux_descr
 * equal to 0.  Any out-of-band data must be negotiated by the SPAN protocol.
 *
 * Auxillary data, whether in-band or out-of-band, must be at-least 64-byte
 * aligned.  The aux_bytes field contains the actual byte-granular length
 * and not the aligned length.
 *
 * hdr_crc is calculated over the entire, ALIGNED extended header.  For
 * the purposes of calculating the crc, the hdr_crc field is 0.  That is,
 * if calculating the crc in HW a 32-bit '0' must be inserted in place of
 * the hdr_crc field when reading the entire header and compared at the
 * end (but the actual hdr_crc must be left intact in memory).  A simple
 * counter to replace the field going into the CRC generator does the job
 * in HW.  The CRC endian is based on the magic number field and may have
 * to be byte-swapped, too (which is also easy to do in HW).
 *
 * aux_crc is calculated over the entire, ALIGNED auxillary data.
 *
 *			SHARED MEMORY IMPLEMENTATIONS
 *
 * Shared-memory implementations typically use a pipe to transmit the extended
 * message header and shared memory to store any auxilary data.  Auxillary
 * data in one-way (non-transactional) messages is typically required to be
 * inline.  CRCs are still recommended and required at the beginning, but
 * may be negotiated away later.
 *
 *			 MULTI-PATH MESSAGE DUPLICATION
 *
 * Redundancy can be negotiated but is not required in the current spec.
 * Basically you send the same message, with the same msgid, via several
 * paths to the target.  The msgid is the rendezvous.  The first copy that
 * makes it to the target is used, the second is ignored.  Similarly for
 * replies.  This can improve performance during span flapping.  Only
 * transactional messages will be serialized.  The target might receive
 * multiple copies of one-way messages in higher protocol layers (potentially
 * out of order, too).
 */
struct hammer2_msg_hdr {
	uint16_t	magic;		/* 00 sanity, synchro, endian */
	uint16_t	reserved02;	/* 02 size of header in bytes */
	uint32_t	salt;		/* 04 random salt helps w/crypto */

	uint64_t	msgid;		/* 08 message transaction id */
	uint64_t	spanid;		/* 10 message routing id or 0 */

	uint32_t	cmd;		/* 18 flags | cmd | hdr_size / ALIGN */
	uint32_t	aux_crc;	/* 1C auxillary data crc */
	uint32_t	aux_bytes;	/* 20 auxillary data length (bytes) */
	uint32_t	error;		/* 24 error code or 0 */
	uint64_t	aux_descr;	/* 28 negotiated OOB data descr */
	uint64_t	reserved30;	/* 30 */
	uint32_t	reserved38;	/* 38 */
	uint32_t	hdr_crc;	/* 3C (aligned) extended header crc */
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
#define HAMMER2_MSGHDR_MAX		2048	/* <= 65535 */
#define HAMMER2_MSGAUX_MAX		65536	/* <= 1MB */
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
#define HAMMER2_MSG_ALIGN		64
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
 * PING		- One-way message on link-0, keep-alive, run by both sides
 *		  typically 1/sec on idle link, link is lost after 10 seconds
 *		  of inactivity.
 *
 * AUTH		- Authenticate the connection, negotiate administrative
 *		  rights & encryption, protocol class, etc.  Only PAD and
 *		  AUTH messages (not even PING) are accepted until
 *		  authentication is complete.  This message also identifies
 *		  the host.
 *
 * CONN		- Enable the SPAN protocol on link-0, possibly also installing
 *		  a PFS filter (by cluster id, unique id, and/or wildcarded
 *		  name).
 *
 * SPAN		- A SPAN transaction on link-0 enables messages to be relayed
 *		  to/from a particular cluster node.  SPANs are received,
 *		  sorted, aggregated, and retransmitted back out across all
 *		  applicable connections.
 *
 *		  The leaf protocol also uses this to make a PFS available
 *		  to the cluster (e.g. on-mount).
 */
#define HAMMER2_LNK_PAD		HAMMER2_MSG_LNK(0x000, hammer2_msg_hdr)
#define HAMMER2_LNK_PING	HAMMER2_MSG_LNK(0x001, hammer2_msg_hdr)
#define HAMMER2_LNK_AUTH	HAMMER2_MSG_LNK(0x010, hammer2_lnk_auth)
#define HAMMER2_LNK_CONN	HAMMER2_MSG_LNK(0x011, hammer2_lnk_conn)
#define HAMMER2_LNK_SPAN	HAMMER2_MSG_LNK(0x012, hammer2_lnk_span)
#define HAMMER2_LNK_ERROR	HAMMER2_MSG_LNK(0xFFF, hammer2_msg_hdr)

/*
 * LNK_CONN - Register connection for SPAN (transaction, left open)
 *
 * One LNK_CONN transaction may be opened on a stream connection, registering
 * the connection with the SPAN subsystem and allowing the subsystem to
 * accept and relay SPANs to this connection.
 *
 * The LNK_CONN message may contain a filter, limiting the desireable SPANs.
 *
 * This message contains a lot of the same info that a SPAN message contains,
 * but is not a SPAN.  That is, without this message the SPAN subprotocol will
 * not be executed on the connection, nor is this message a promise that the
 * sending end is a client or node of a cluster.
 */
struct hammer2_lnk_auth {
	hammer2_msg_hdr_t head;
	char		dummy[64];
};

struct hammer2_lnk_conn {
	hammer2_msg_hdr_t head;
	uuid_t		pfs_clid;	/* rendezvous pfs uuid */
	uuid_t		pfs_fsid;	/* unique pfs uuid */
	uint8_t		pfs_type;	/* peer type */
	uint8_t		reserved01;
	uint16_t	proto_version;	/* high level protocol support */
	uint32_t	status;		/* status flags */
	uint8_t		reserved02[8];
	int32_t		dist;		/* span distance */
	uint32_t	reserved03[15];
	char		label[256];	/* PFS label (can be wildcard) */
};

typedef struct hammer2_lnk_conn hammer2_lnk_conn_t;

/*
 * LNK_SPAN - Relay a SPAN (transaction, left open)
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
	uuid_t		pfs_clid;	/* rendezvous pfs uuid */
	uuid_t		pfs_fsid;	/* unique pfs uuid */
	uint8_t		pfs_type;	/* peer type */
	uint8_t		reserved01;
	uint16_t	proto_version;	/* high level protocol support */
	uint32_t	status;		/* status flags */
	uint8_t		reserved02[8];
	int32_t		dist;		/* span distance */
	uint32_t	reserved03[15];
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
 * NOTE!!!! ALL EXTENDED HEADER STRUCTURES MUST BE 64-BYTE ALIGNED!!!
 *
 * General message errors
 *
 *	0x00 - 0x1F	Local iocomm errors
 *	0x20 - 0x2F	Global errors
 */
#define HAMMER2_MSG_ERR_NOSUPP		0x20

union hammer2_msg_any {
	char			buf[HAMMER2_MSGHDR_MAX];
	hammer2_msg_hdr_t	head;
	hammer2_lnk_span_t	lnk_span;
	hammer2_lnk_conn_t	lnk_conn;
};

typedef union hammer2_msg_any hammer2_msg_any_t;

#endif
