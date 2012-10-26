/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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

#ifndef _SYS_DMSG_H_
#define _SYS_DMSG_H_

#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif

/*
 * Mesh network protocol structures.
 *
 *				SPAN PROTOCOL
 *
 * The mesh is constructed from point-to-point streaming links with varying
 * levels of interconnectedness, forming a graph.  Terminii in the graph
 * are entities such as a HAMMER2 PFS or a network mount or other types
 * of nodes.
 *
 * The spanning tree protocol runs symmetrically on every node. Each node
 * transmits a representitive LNK_SPAN out all available connections.  Nodes
 * also receive LNK_SPANs from other nodes (obviously), and must aggregate,
 * reduce, and relay those LNK_SPANs out all available connections, thus
 * propagating the spanning tree.  Any connection failure or topology change
 * causes changes in the LNK_SPAN propagation.
 *
 * Each LNK_SPAN or LNK_SPAN relay represents a virtual circuit for routing
 * purposes.  In addition, each relay is chained in one direction,
 * representing a 1:N fan-out (i.e. one received LNK_SPAN can be relayed out
 * multiple connections).  In order to be able to route a message via a
 * LNK_SPAN over a deterministic route THE MESSAGE CAN ONLY FLOW FROM A
 * REMOTE NODE TOWARDS OUR NODE (N:1 fan-in).
 *
 * This supports the requirement that we have both message serialization
 * and positive feedback if a topology change breaks the chain of VCs
 * the message is flowing over.  A remote node sending a message to us
 * will get positive feedback that the route was broken and can take suitable
 * action to terminate the transaction with an error.
 *
 *				TRANSACTIONAL REPLIES
 *
 * However, when we receive a command message from a remote node and we want
 * to reply to it, we have a problem.  We want the remote node to have
 * positive feedback if our reply fails to make it, but if we use a virtual
 * circuit based on the remote node's LNK_SPAN to us it will be a DIFFERENT
 * virtual circuit than the one the remote node used to message us.  That's
 * a problem because it means we have no reliable way to notify the remote
 * node if we get notified that our reply has failed.
 *
 * The solution is to first note the fact that the remote chose an optimal
 * route to get to us, so the reverse should be true. The reason the VC
 * might not exist over the same route in the reverse is because there may
 * be multiple paths available with the same distance metric.
 *
 * But this also means that we can adjust the messaging protocols to
 * propagate a LNK_SPAN from the remote to us WHILE the remote's command
 * message is being sent to us, and it will not only likely be optimal but
 * it might also already exist, and it will also guarantee that a reply
 * failure will propagate back to both sides (because even though each
 * direction is using a different VC chain, the two chains are still
 * going along the same path).
 *
 * We communicate the return VC by having the relay adjust both the target
 * and the source fields in the message, rather than just the target, on
 * each relay.  As of when the message gets to us the 'source' field will
 * represent the VC for the return direction (and of course also identify
 * the node the message came from).
 *
 * This way both sides get positive feedback if a topology change disrupts
 * the VC for the transaction.  We also get one additional guarantee, and
 * that is no spurious messages.  Messages simply die when the VC they are
 * traveling over is broken, in either direction, simple as that.
 * It makes managing message transactional states very easy.
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
 * transports must support extended message headers up to DMSG_HDR_MAX.
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
struct dmsg_hdr {
	uint16_t	magic;		/* 00 sanity, synchro, endian */
	uint16_t	reserved02;	/* 02 */
	uint32_t	salt;		/* 04 random salt helps w/crypto */

	uint64_t	msgid;		/* 08 message transaction id */
	uint64_t	source;		/* 10 originator or 0	*/
	uint64_t	target;		/* 18 destination or 0	*/

	uint32_t	cmd;		/* 20 flags | cmd | hdr_size / ALIGN */
	uint32_t	aux_crc;	/* 24 auxillary data crc */
	uint32_t	aux_bytes;	/* 28 auxillary data length (bytes) */
	uint32_t	error;		/* 2C error code or 0 */
	uint64_t	aux_descr;	/* 30 negotiated OOB data descr */
	uint32_t	reserved38;	/* 38 */
	uint32_t	hdr_crc;	/* 3C (aligned) extended header crc */
};

typedef struct dmsg_hdr dmsg_hdr_t;

#define DMSG_HDR_MAGIC		0x4832
#define DMSG_HDR_MAGIC_REV	0x3248
#define DMSG_HDR_CRCOFF		offsetof(dmsg_hdr_t, salt)
#define DMSG_HDR_CRCBYTES	(sizeof(dmsg_hdr_t) - DMSG_HDR_CRCOFF)

/*
 * Administrative protocol limits.
 */
#define DMSG_HDR_MAX		2048	/* <= 65535 */
#define DMSG_AUX_MAX		65536	/* <= 1MB */
#define DMSG_BUF_SIZE		(DMSG_HDR_MAX * 4)
#define DMSG_BUF_MASK		(DMSG_BUF_SIZE - 1)

/*
 * The message (cmd) field also encodes various flags and the total size
 * of the message header.  This allows the protocol processors to validate
 * persistency and structural settings for every command simply by
 * switch()ing on the (cmd) field.
 */
#define DMSGF_CREATE		0x80000000U	/* msg start */
#define DMSGF_DELETE		0x40000000U	/* msg end */
#define DMSGF_REPLY		0x20000000U	/* reply path */
#define DMSGF_ABORT		0x10000000U	/* abort req */
#define DMSGF_AUXOOB		0x08000000U	/* aux-data is OOB */
#define DMSGF_FLAG2		0x04000000U
#define DMSGF_FLAG1		0x02000000U
#define DMSGF_FLAG0		0x01000000U

#define DMSGF_FLAGS		0xFF000000U	/* all flags */
#define DMSGF_PROTOS		0x00F00000U	/* all protos */
#define DMSGF_CMDS		0x000FFF00U	/* all cmds */
#define DMSGF_SIZE		0x000000FFU	/* N*32 */

#define DMSGF_CMDSWMASK		(DMSGF_CMDS |	\
					 DMSGF_SIZE |	\
					 DMSGF_PROTOS |	\
					 DMSGF_REPLY)

#define DMSGF_BASECMDMASK	(DMSGF_CMDS |	\
					 DMSGF_SIZE |	\
					 DMSGF_PROTOS)

#define DMSGF_TRANSMASK		(DMSGF_CMDS |	\
					 DMSGF_SIZE |	\
					 DMSGF_PROTOS |	\
					 DMSGF_REPLY |	\
					 DMSGF_CREATE |	\
					 DMSGF_DELETE)

#define DMSG_PROTO_LNK		0x00000000U
#define DMSG_PROTO_DBG		0x00100000U
#define DMSG_PROTO_DOM		0x00200000U
#define DMSG_PROTO_CAC		0x00300000U
#define DMSG_PROTO_QRM		0x00400000U
#define DMSG_PROTO_BLK		0x00500000U
#define DMSG_PROTO_VOP		0x00600000U

/*
 * Message command constructors, sans flags
 */
#define DMSG_ALIGN		64
#define DMSG_ALIGNMASK		(DMSG_ALIGN - 1)
#define DMSG_DOALIGN(bytes)	(((bytes) + DMSG_ALIGNMASK) &		\
				 ~DMSG_ALIGNMASK)

#define DMSG_HDR_ENCODE(elm)	(((uint32_t)sizeof(struct elm) +	\
				  DMSG_ALIGNMASK) /			\
				 DMSG_ALIGN)

#define DMSG_LNK(cmd, elm)	(DMSG_PROTO_LNK |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

#define DMSG_DBG(cmd, elm)	(DMSG_PROTO_DBG |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

#define DMSG_DOM(cmd, elm)	(DMSG_PROTO_DOM |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

#define DMSG_CAC(cmd, elm)	(DMSG_PROTO_CAC |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

#define DMSG_QRM(cmd, elm)	(DMSG_PROTO_QRM |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

#define DMSG_BLK(cmd, elm)	(DMSG_PROTO_BLK |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

#define DMSG_VOP(cmd, elm)	(DMSG_PROTO_VOP |			\
					 ((cmd) << 8) | 		\
					 DMSG_HDR_ENCODE(elm))

/*
 * Link layer ops basically talk to just the other side of a direct
 * connection.
 *
 * LNK_PAD	- One-way message on link-0, ignored by target.  Used to
 *		  pad message buffers on shared-memory transports.  Not
 *		  typically used with TCP.
 *
 * LNK_PING	- One-way message on link-0, keep-alive, run by both sides
 *		  typically 1/sec on idle link, link is lost after 10 seconds
 *		  of inactivity.
 *
 * LNK_AUTH	- Authenticate the connection, negotiate administrative
 *		  rights & encryption, protocol class, etc.  Only PAD and
 *		  AUTH messages (not even PING) are accepted until
 *		  authentication is complete.  This message also identifies
 *		  the host.
 *
 * LNK_CONN	- Enable the SPAN protocol on link-0, possibly also installing
 *		  a PFS filter (by cluster id, unique id, and/or wildcarded
 *		  name).
 *
 * LNK_SPAN	- A SPAN transaction on link-0 enables messages to be relayed
 *		  to/from a particular cluster node.  SPANs are received,
 *		  sorted, aggregated, and retransmitted back out across all
 *		  applicable connections.
 *
 *		  The leaf protocol also uses this to make a PFS available
 *		  to the cluster (e.g. on-mount).
 *
 * LNK_VOLCONF	- Volume header configuration change.  All hammer2
 *		  connections (hammer2 connect ...) stored in the volume
 *		  header are spammed at the link level to the hammer2
 *		  service daemon, and any live configuration change
 *		  thereafter.
 */
#define DMSG_LNK_PAD		DMSG_LNK(0x000, dmsg_hdr)
#define DMSG_LNK_PING		DMSG_LNK(0x001, dmsg_hdr)
#define DMSG_LNK_AUTH		DMSG_LNK(0x010, dmsg_lnk_auth)
#define DMSG_LNK_CONN		DMSG_LNK(0x011, dmsg_lnk_conn)
#define DMSG_LNK_SPAN		DMSG_LNK(0x012, dmsg_lnk_span)
#define DMSG_LNK_VOLCONF	DMSG_LNK(0x020, dmsg_lnk_volconf)
#define DMSG_LNK_ERROR		DMSG_LNK(0xFFF, dmsg_hdr)

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
struct dmsg_lnk_auth {
	dmsg_hdr_t	head;
	char		dummy[64];
};

/*
 * LNK_CONN identifies a streaming connection into the cluster.  The other
 * fields serve as a filter when supported for a particular peer and are
 * not necessarily all used.
 *
 * peer_mask serves to filter the SPANs we receive by peer.  A cluster
 * controller typically sets this to (uint64_t)-1, a block devfs
 * interface might set it to 1 << DMSG_PEER_DISK, and a hammer2
 * mount might set it to 1 << DMSG_PEER_HAMMER2.
 *
 * mediaid allows multiple (e.g. HAMMER2) connections belonging to the same
 * media, in terms of LNK_VOLCONF updates.
 *
 * pfs_clid, pfs_fsid, pfs_type, and label are peer-specific and must be
 * left empty (zero-fill) if not supported by a particular peer.
 *
 * DMSG_PEER_CLUSTER		filter: none
 * DMSG_PEER_BLOCK		filter: label
 * DMSG_PEER_HAMMER2		filter: pfs_clid if not empty, and label
 */
struct dmsg_lnk_conn {
	dmsg_hdr_t	head;
	uuid_t		mediaid;	/* media configuration id */
	uuid_t		pfs_clid;	/* rendezvous pfs uuid */
	uuid_t		pfs_fsid;	/* unique pfs uuid */
	uint64_t	peer_mask;	/* PEER mask for SPAN filtering */
	uint8_t		peer_type;	/* see DMSG_PEER_xxx */
	uint8_t		pfs_type;	/* pfs type */
	uint16_t	proto_version;	/* high level protocol support */
	uint32_t	status;		/* status flags */
	uint8_t		reserved02[8];
	int32_t		dist;		/* span distance */
	uint32_t	reserved03[14];
	char		cl_label[128];	/* cluster label (for PEER_BLOCK) */
	char		fs_label[128];	/* PFS label (for PEER_HAMMER2) */
};

typedef struct dmsg_lnk_conn dmsg_lnk_conn_t;

#define DMSG_PFSTYPE_NONE	0
#define DMSG_PFSTYPE_ADMIN	1
#define DMSG_PFSTYPE_CLIENT	2
#define DMSG_PFSTYPE_CACHE	3
#define DMSG_PFSTYPE_COPY	4
#define DMSG_PFSTYPE_SLAVE	5
#define DMSG_PFSTYPE_SOFT_SLAVE	6
#define DMSG_PFSTYPE_SOFT_MASTER 7
#define DMSG_PFSTYPE_MASTER	8
#define DMSG_PFSTYPE_MAX	9       /* 0-8 */

#define DMSG_PEER_NONE		0
#define DMSG_PEER_CLUSTER	1	/* a cluster controller */
#define DMSG_PEER_BLOCK		2	/* block devices */
#define DMSG_PEER_HAMMER2	3	/* hammer2-mounted volumes */

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
struct dmsg_lnk_span {
	dmsg_hdr_t	head;
	uuid_t		pfs_clid;	/* rendezvous pfs uuid */
	uuid_t		pfs_fsid;	/* unique pfs uuid */
	uint8_t		pfs_type;	/* PFS type */
	uint8_t		peer_type;	/* PEER type */
	uint16_t	proto_version;	/* high level protocol support */
	uint32_t	status;		/* status flags */
	uint8_t		reserved02[8];
	int32_t		dist;		/* span distance */
	uint32_t	reserved03[15];
	char		cl_label[128];	/* cluster label (for PEER_BLOCK) */
	char		fs_label[128];	/* PFS label (for PEER_HAMMER2) */
};

typedef struct dmsg_lnk_span dmsg_lnk_span_t;

#define DMSG_SPAN_PROTO_1	1

/*
 * LNK_VOLCONF
 */

/*
 * All HAMMER2 directories directly under the super-root on your local
 * media can be mounted separately, even if they share the same physical
 * device.
 *
 * When you do a HAMMER2 mount you are effectively tying into a HAMMER2
 * cluster via local media.  The local media does not have to participate
 * in the cluster, other than to provide the dmsg_vol_data[] array and
 * root inode for the mount.
 *
 * This is important: The mount device path you specify serves to bootstrap
 * your entry into the cluster, but your mount will make active connections
 * to ALL copy elements in the dmsg_vol_data[] array which match the
 * PFSID of the directory in the super-root that you specified.  The local
 * media path does not have to be mentioned in this array but becomes part
 * of the cluster based on its type and access rights.  ALL ELEMENTS ARE
 * TREATED ACCORDING TO TYPE NO MATTER WHICH ONE YOU MOUNT FROM.
 *
 * The actual cluster may be far larger than the elements you list in the
 * dmsg_vol_data[] array.  You list only the elements you wish to
 * directly connect to and you are able to access the rest of the cluster
 * indirectly through those connections.
 *
 * This structure must be exactly 128 bytes long.
 *
 * WARNING!  dmsg_vol_data is embedded in the hammer2 media volume header
 */
struct dmsg_vol_data {
	uint8_t	copyid;		/* 00	 copyid 0-255 (must match slot) */
	uint8_t inprog;		/* 01	 operation in progress, or 0 */
	uint8_t chain_to;	/* 02	 operation chaining to, or 0 */
	uint8_t chain_from;	/* 03	 operation chaining from, or 0 */
	uint16_t flags;		/* 04-05 flags field */
	uint8_t error;		/* 06	 last operational error */
	uint8_t priority;	/* 07	 priority and round-robin flag */
	uint8_t remote_pfs_type;/* 08	 probed direct remote PFS type */
	uint8_t reserved08[23];	/* 09-1F */
	uuid_t	pfs_clid;	/* 20-2F copy target must match this uuid */
	uint8_t label[16];	/* 30-3F import/export label */
	uint8_t path[64];	/* 40-7F target specification string or key */
};

typedef struct dmsg_vol_data dmsg_vol_data_t;

#define DMSG_VOLF_ENABLED	0x0001
#define DMSG_VOLF_INPROG	0x0002
#define DMSG_VOLF_CONN_RR	0x80	/* round-robin at same priority */
#define DMSG_VOLF_CONN_EF	0x40	/* media errors flagged */
#define DMSG_VOLF_CONN_PRI	0x0F	/* select priority 0-15 (15=best) */

#define DMSG_COPYID_COUNT	256	/* WARNING! embedded in hammer2 vol */

struct dmsg_lnk_volconf {
	dmsg_hdr_t		head;
	dmsg_vol_data_t		copy;	/* copy spec */
	int32_t			index;
	int32_t			unused01;
	uuid_t			mediaid;
	int64_t			reserved02[32];
};

typedef struct dmsg_lnk_volconf dmsg_lnk_volconf_t;

/*
 * Debug layer ops operate on any link
 *
 * SHELL	- Persist stream, access the debug shell on the target
 *		  registration.  Multiple shells can be operational.
 */
#define DMSG_DBG_SHELL		DMSG_DBG(0x001, dmsg_dbg_shell)

struct dmsg_dbg_shell {
	dmsg_hdr_t	head;
};
typedef struct dmsg_dbg_shell dmsg_dbg_shell_t;

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
#define DMSG_CAC_LOCK		DMSG_CAC(0x001, dmsg_cac_lock)

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
#define DMSG_QRM_COMMIT		DMSG_QRM(0x001, dmsg_qrm_commit)

/*
 * NOTE!!!! ALL EXTENDED HEADER STRUCTURES MUST BE 64-BYTE ALIGNED!!!
 *
 * General message errors
 *
 *	0x00 - 0x1F	Local iocomm errors
 *	0x20 - 0x2F	Global errors
 */
#define DMSG_ERR_NOSUPP		0x20

union dmsg_any {
	char			buf[DMSG_HDR_MAX];
	dmsg_hdr_t		head;
	dmsg_lnk_span_t		lnk_span;
	dmsg_lnk_conn_t		lnk_conn;
	dmsg_lnk_volconf_t	lnk_volconf;
};

typedef union dmsg_any dmsg_any_t;

/*
 * Kernel iocom structures and prototypes for kern/kern_dmsg.c
 */
#ifdef _KERNEL

struct hammer2_pfsmount;
struct kdmsg_router;
struct kdmsg_iocom;
struct kdmsg_state;
struct kdmsg_msg;

/*
 * Structure used to represent a virtual circuit for a messaging
 * route.  Typically associated from hammer2_state but the hammer2_pfsmount
 * structure also has one to represent the point-to-point link.
 */
struct kdmsg_router {
	struct kdmsg_iocom	*iocom;
	struct kdmsg_state	*state;		/* received LNK_SPAN state */
	uint64_t		target;		/* target */
};

typedef struct kdmsg_router kdmsg_router_t;

/*
 * msg_ctl flags (atomic)
 */
#define KDMSG_CLUSTERCTL_KILL		0x00000001
#define KDMSG_CLUSTERCTL_KILLRX		0x00000002 /* staged helper exit */
#define KDMSG_CLUSTERCTL_KILLTX		0x00000004 /* staged helper exit */
#define KDMSG_CLUSTERCTL_SLEEPING	0x00000008 /* interlocked w/msglk */

/*
 * Transactional state structure, representing an open transaction.  The
 * transaction might represent a cache state (and thus have a chain
 * association), or a VOP op, LNK_SPAN, or other things.
 */
struct kdmsg_state {
	RB_ENTRY(kdmsg_state) rbnode;		/* indexed by msgid */
	struct kdmsg_router *router;		/* related LNK_SPAN route */
	uint32_t	txcmd;			/* mostly for CMDF flags */
	uint32_t	rxcmd;			/* mostly for CMDF flags */
	uint64_t	msgid;			/* {spanid,msgid} uniq */
	int		flags;
	int		error;
	void		*chain;			/* (caller's state) */
	struct kdmsg_msg *msg;
	int (*func)(struct kdmsg_state *, struct kdmsg_msg *);
	union {
		void *any;
		struct hammer2_pfsmount *pmp;
	} any;
};

#define KDMSG_STATE_INSERTED	0x0001
#define KDMSG_STATE_DYNAMIC	0x0002
#define KDMSG_STATE_DELPEND	0x0004		/* transmit delete pending */

struct kdmsg_msg {
	TAILQ_ENTRY(kdmsg_msg) qentry;		/* serialized queue */
	struct kdmsg_router *router;
	struct kdmsg_state *state;
	size_t		hdr_size;
	size_t		aux_size;
	char		*aux_data;
	dmsg_any_t	any;
};

typedef struct kdmsg_link kdmsg_link_t;
typedef struct kdmsg_state kdmsg_state_t;
typedef struct kdmsg_msg kdmsg_msg_t;

struct kdmsg_state_tree;
RB_HEAD(kdmsg_state_tree, kdmsg_state);
int kdmsg_state_cmp(kdmsg_state_t *state1, kdmsg_state_t *state2);
RB_PROTOTYPE(kdmsg_state_tree, kdmsg_state, rbnode, kdmsg_state_cmp);

/*
 * Structure embedded in e.g. mount, master control structure for
 * DMSG stream handling.
 */
struct kdmsg_iocom {
	struct malloc_type	*mmsg;
	struct file		*msg_fp;	/* cluster pipe->userland */
	thread_t		msgrd_td;	/* cluster thread */
	thread_t		msgwr_td;	/* cluster thread */
	int			msg_ctl;	/* wakeup flags */
	int			msg_seq;	/* cluster msg sequence id */
	uint32_t		reserved01;
	struct lock		msglk;		/* lockmgr lock */
	TAILQ_HEAD(, kdmsg_msg) msgq;		/* transmit queue */
	void			*handle;
	int			(*lnk_rcvmsg)(kdmsg_msg_t *msg);
	int			(*dbg_rcvmsg)(kdmsg_msg_t *msg);
	int			(*misc_rcvmsg)(kdmsg_msg_t *msg);
	void			(*exit_func)(struct kdmsg_iocom *);
	struct kdmsg_state	*conn_state;	/* active LNK_CONN state */
	struct kdmsg_state	*freerd_state;	/* allocation cache */
	struct kdmsg_state	*freewr_state;	/* allocation cache */
	struct kdmsg_state_tree staterd_tree;	/* active messages */
	struct kdmsg_state_tree statewr_tree;	/* active messages */
	struct kdmsg_router	router;
};

typedef struct kdmsg_iocom	kdmsg_iocom_t;

uint32_t kdmsg_icrc32(const void *buf, size_t size);
uint32_t kdmsg_icrc32c(const void *buf, size_t size, uint32_t crc);

/*
 * kern_dmsg.c
 */
void kdmsg_iocom_init(kdmsg_iocom_t *iocom,
			void *handle,
			struct malloc_type *mmsg,
			int (*lnk_rcvmsg)(kdmsg_msg_t *msg),
			int (*dbg_rcvmsg)(kdmsg_msg_t *msg),
			int (*misc_rcvmsg)(kdmsg_msg_t *msg));
void kdmsg_iocom_reconnect(kdmsg_iocom_t *iocom, struct file *fp,
			const char *subsysname);
void kdmsg_iocom_uninit(kdmsg_iocom_t *iocom);
void kdmsg_drain_msgq(kdmsg_iocom_t *iocom);

int kdmsg_state_msgrx(kdmsg_msg_t *msg);
int kdmsg_state_msgtx(kdmsg_msg_t *msg);
void kdmsg_state_cleanuprx(kdmsg_msg_t *msg);
void kdmsg_state_cleanuptx(kdmsg_msg_t *msg);
int kdmsg_msg_execute(kdmsg_msg_t *msg);
void kdmsg_state_free(kdmsg_state_t *state);
void kdmsg_msg_free(kdmsg_msg_t *msg);
kdmsg_msg_t *kdmsg_msg_alloc(kdmsg_router_t *router, uint32_t cmd,
				int (*func)(kdmsg_state_t *, kdmsg_msg_t *),
				void *data);
void kdmsg_msg_write(kdmsg_msg_t *msg);
void kdmsg_msg_reply(kdmsg_msg_t *msg, uint32_t error);
void kdmsg_msg_result(kdmsg_msg_t *msg, uint32_t error);

#endif

#endif
