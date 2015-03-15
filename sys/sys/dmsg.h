/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
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
 *				CONN PROTOCOL
 *
 * The mesh is constructed via point-to-point streaming links with varying
 * levels of interconnectedness, forming a graph.  Leafs of the graph are
 * typically kernel devices (xdisk) or VFSs (HAMMER2).  Internal nodes are
 * usually (user level) hammer2 service demons.
 *
 * Upon connecting and after authentication, a LNK_CONN transaction is opened
 * to configure the link.  The SPAN protocol is then typically run over the
 * open LNK_CONN transaction.
 *
 * Terminating the LNK_CONN transaction terminates everything running over it
 * (typically open LNK_SPAN transactions), which in turn terminates everything
 * running over the LNK_SPANs.
 *
 *				SPAN PROTOCOL
 *
 * The SPAN protocol runs over an open LNK_CONN transaction and is used to
 * advertise any number of services.  For example, each PFS under a HAMMER2
 * mount will be advertised as an open LNK_SPAN transaction.
 *
 * Any network node on the graph running multiple connections is capable
 * of relaying LNK_SPANs from any connection to any other connection.  This
 * is typically done by the user-level hammer2 service demon, and typically
 * not done by kernel devices or VFSs (though these entities must be able
 * to manage multiple LNK_SPANs since they might advertise or need to talk
 * to multiple services).
 *
 * Relaying is not necessarily trivial as it requires internal nodes to
 * track two open transactions (on the two iocom interfaces) and translate
 * the msgid and circuit.  In addition, the relay may have to track multiple
 * SPANs from the same iocom or from multiple iocoms which represent the same
 * end-point and must select the best end-point, must send notifications when
 * a better path is available, and must allow (when connectivity is still
 * present) any existing, open, stacked sub-transactions to complete before
 * terminating the less efficient SPAN.
 *
 * Relaying is optional.  It is perfectly acceptable for the hammer2 service
 * to plug a received socket descriptor directly into the appropriate kernel
 * device driver.
 *
 *			       STACKED TRANSACTIONS
 *
 * Message transactions can be stacked.  That is, you can initiate a DMSG
 * transaction relative to another open transaction.  sub-transactions can
 * be initiate without waiting for the parent transaction to complete its
 * handshake.
 *
 * This is done by entering the open transaction's msgid as the circuit field
 * in the new transaction (typically by populating msg->parent).  The
 * transaction tracking structure will be referenced and will track the
 * sub-transaction.  Note that msgids must still be unique on an
 * iocom-by-iocom basis.
 *
 * Messages can race closing circuits.  When a circuit is lost,
 * messages are simulated to delete any sub-transactions.
 *
 *			    MESSAGE TRANSACTIONAL STATES
 *
 * Message transactions are handled by the CREATE, DELETE, REPLY, ABORT, and
 * CREPLY flags.  Message state is typically recorded at the end points and
 * will be maintained (preventing reuse of the transaction id) until a DELETE
 * is both sent and received.
 *
 * One-way messages such as those used for debug commands are not recorded
 * and do not require any transactional state.  These are sent without
 * the CREATE, DELETE, or ABORT flags set.  ABORT is not supported for
 * one-off messages.  The REPLY bit can be used to distinguish between
 * command and status if desired.
 *
 * Transactional messages are messages which require a reply to be
 * returned.  These messages can also consist of multiple message elements
 * for the command or reply or both (or neither).  The command message
 * sequence sets CREATE on the first message and DELETE on the last message.
 * A single message command sets both (CREATE|DELETE).  The reply message
 * sequence works the same way but of course also sets the REPLY bit.
 *
 * Tansactional messages can be aborted by sending a message element
 * with the ABORT flag set.  This flag can be combined with either or both
 * the CREATE and DELETE flags.  When combined with the CREATE flag the
 * command is treated as non-blocking but still executes.  Whem combined
 * with the DELETE flag no additional message elements are required.
 *
 * Transactions are terminated by sending a message with DELETE set.
 * Transactions must be CREATEd and DELETEd in both directions.  If a
 * transaction is governing stacked sub-transactions the sub-transactions
 * are automatically terminated before the governing transaction is terminated.
 * Terminates are handled by simulating a received DELETE and expecting the
 * normal function callback and state machine to (ultimately) issue a
 * terminating (DELETE) response.
 *
 * Transactions can operate in full-duplex as both sides are fully open
 * (i.e. CREATE sent, CREATE|REPLY returned, DELETE not sent by anyone).
 * Additional commands can be initiated from either side of the transaction.
 *
 * ABORT SPECIAL CASE - Mid-stream aborts.  A mid-stream abort can be sent
 * when supported by the sender by sending an ABORT message with neither
 * CREATE or DELETE set.  This effectively turns the message into a
 * non-blocking message (but depending on what is being represented can also
 * cut short prior data elements in the stream).
 *
 * ABORT SPECIAL CASE - Abort-after-DELETE.  Transactional messages have to be
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
 * and not the aligned length.  The crc is against the aligned length (so
 * a faster crc algorithm can be used, theoretically).
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
 */

#define DMSG_TERMINATE_STRING(ary)	\
	do { (ary)[sizeof(ary) - 1] = 0; } while (0)

/*
 * dmsg_hdr must be 64 bytes
 */
struct dmsg_hdr {
	uint16_t	magic;		/* 00 sanity, synchro, endian */
	uint16_t	reserved02;	/* 02 */
	uint32_t	salt;		/* 04 random salt helps w/crypto */

	uint64_t	msgid;		/* 08 message transaction id */
	uint64_t	circuit;	/* 10 circuit id or 0	*/
	uint64_t	reserved18;	/* 18 */

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
 *
 * NOTE: A dmsg header must completely fit in the (fifo) buffer, but
 *	 dmsg aux data does not have to completely fit.  The dmsg
 *	 structure allows headers up to 255*64 = 16320 bytes.  There
 *	 is no real limit on the aux_data other than what we deem
 *	 reasonable and defenseable (i.e. not run processes or the
 *	 kernel out of memory).  But it should be able to handle at
 *	 least MAXPHYS bytes which is typically 128KB or 256KB.
 */
#define DMSG_HDR_MAX		2048		/* <= 8192 */
#define DMSG_AUX_MAX		(1024*1024)	/* <= 1MB */
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
#define DMSGF_REVTRANS		0x08000000U	/* opposite direction msgid */
#define DMSGF_REVCIRC		0x04000000U	/* opposite direction circuit */
#define DMSGF_FLAG1		0x02000000U
#define DMSGF_FLAG0		0x01000000U

#define DMSGF_FLAGS		0xFF000000U	/* all flags */
#define DMSGF_PROTOS		0x00F00000U	/* all protos */
#define DMSGF_CMDS		0x000FFF00U	/* all cmds */
#define DMSGF_SIZE		0x000000FFU	/* N*32 */

/*
 * XXX Future, flag that an in-line (not part of a CREATE/DELETE) command
 *     expects some sort of acknowledgement.  Allows protocol mismatches to
 *     be detected.
 */
#define DMSGF_CMDF_EXPECT_ACK	0x00080000U	/* in-line command no-ack */

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

#define DMSGF_BASEFLAGS		(DMSGF_CREATE | DMSGF_DELETE | DMSGF_REPLY)

#define DMSG_PROTO_LNK		0x00000000U
#define DMSG_PROTO_DBG		0x00100000U
#define DMSG_PROTO_HM2		0x00200000U
#define DMSG_PROTO_XX3		0x00300000U
#define DMSG_PROTO_XX4		0x00400000U
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

#define DMSG_HM2(cmd, elm)	(DMSG_PROTO_HM2 |			\
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
 * LNK_PAD	- One-way message on circuit 0, ignored by target.  Used to
 *		  pad message buffers on shared-memory transports.  Not
 *		  typically used with TCP.
 *
 * LNK_PING	- One-way message on circuit-0, keep-alive, run by both sides
 *		  typically 1/sec on idle link, link is lost after 10 seconds
 *		  of inactivity.
 *
 * LNK_AUTH	- Authenticate the connection, negotiate administrative
 *		  rights & encryption, protocol class, etc.  Only PAD and
 *		  AUTH messages (not even PING) are accepted until
 *		  authentication is complete.  This message also identifies
 *		  the host.
 *
 * LNK_CONN	- Enable the SPAN protocol on circuit-0, possibly also
 *		  installing a PFS filter (by cluster id, unique id, and/or
 *		  wildcarded name).
 *
 * LNK_SPAN	- A SPAN transaction typically on iocom->state0 enables
 *		  messages to be relayed to/from a particular cluster node.
 *		  SPANs are received, sorted, aggregated, filtered, and
 *		  retransmitted back out across all applicable connections.
 *
 *		  The leaf protocol also uses this to make a PFS available
 *		  to the cluster (e.g. on-mount).
 */
#define DMSG_LNK_PAD		DMSG_LNK(0x000, dmsg_hdr)
#define DMSG_LNK_PING		DMSG_LNK(0x001, dmsg_hdr)
#define DMSG_LNK_AUTH		DMSG_LNK(0x010, dmsg_lnk_auth)
#define DMSG_LNK_CONN		DMSG_LNK(0x011, dmsg_lnk_conn)
#define DMSG_LNK_SPAN		DMSG_LNK(0x012, dmsg_lnk_span)
#define DMSG_LNK_ERROR		DMSG_LNK(0xFFF, dmsg_hdr)

/*
 * Reserved command codes for third party subsystems.  Structure size is
 * not known here so do not try to construct the full DMSG_LNK_ define.
 */
#define DMSG_LNK_CMD_HAMMER2_VOLCONF	0x20

#define DMSG_LABEL_SIZE		128	/* fixed at 128, do not change */

/*
 * LNK_AUTH - Authentication (often omitted)
 */
struct dmsg_lnk_auth {
	dmsg_hdr_t	head;
	char		dummy[64];
};

/*
 * LNK_CONN - Register connection info for SPAN protocol
 *	      (transaction, left open, iocom->state0 only).
 *
 * LNK_CONN identifies a streaming connection into the cluster and serves
 * to identify, enable, and specify filters for the SPAN protocol.
 *
 * peer_mask serves to filter the SPANs we receive by peer_type.  A cluster
 * controller typically sets this to (uint64_t)-1, indicating that it wants
 * everything.  A block devfs interface might set it to 1 << DMSG_PEER_DISK,
 * and a hammer2 mount might set it to 1 << DMSG_PEER_HAMMER2.
 *
 * mediaid allows multiple (e.g. HAMMER2) connections belonging to the same
 * media to transmit duplicative LNK_VOLCONF updates without causing
 * confusion in the cluster controller.
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
	uint32_t	rnss;		/* node's generated rnss */
	uint8_t		reserved02[8];
	uint32_t	reserved03[12];
	uint64_t	pfs_mask;	/* PFS mask for SPAN filtering */
	char		cl_label[DMSG_LABEL_SIZE]; /* cluster label */
	char		fs_label[DMSG_LABEL_SIZE]; /* PFS label */
};

typedef struct dmsg_lnk_conn dmsg_lnk_conn_t;

/*
 * PFSTYPEs 0-15 used by sys/dmsg.h 16-31 reserved by hammer2.
 */
#define DMSG_PFSTYPE_NONE		0
#define DMSG_PFSTYPE_ADMIN		1
#define DMSG_PFSTYPE_CLIENT		2
#define DMSG_PFSTYPE_SERVER		3
#define DMSG_PFSTYPE_MAX		32

#define DMSG_PEER_NONE		0
#define DMSG_PEER_CLUSTER	1	/* a cluster controller */
#define DMSG_PEER_BLOCK		2	/* block devices */
#define DMSG_PEER_HAMMER2	3	/* hammer2-mounted volumes */

/*
 * Structures embedded in LNK_SPAN
 */
struct dmsg_media_block {
	uint64_t	bytes;		/* media size in bytes */
	uint32_t	blksize;	/* media block size */
};

typedef struct dmsg_media_block dmsg_media_block_t;

/*
 * LNK_SPAN - Initiate or relay a SPAN
 *	      (transaction, left open, typically only on iocom->state0)
 *
 * This message registers an end-point with the other end of the connection,
 * telling the other end who we are and what we can provide or intend to
 * consume.  Multiple registrations can be maintained as open transactions
 * with each one specifying a unique end-point.
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
 * registrations.  It typically filters what gets passed on based on what it
 * receives, passing on only the best candidates.
 *
 * If a symmetric spanning tree is desired additional candidates whos
 * {dist, rnss} fields match the last best candidate must also be propagated.
 * This feature is not currently enabled.
 *
 * STATUS UPDATES: Status updates use the same structure but typically
 *		   only contain incremental changes to e.g. pfs_type, with
 *		   a text description sent as out-of-band data.
 */
struct dmsg_lnk_span {
	dmsg_hdr_t	head;
	uuid_t		pfs_clid;	/* rendezvous pfs uuid */
	uuid_t		pfs_fsid;	/* unique pfs id (differentiate node) */
	uint8_t		pfs_type;	/* PFS type */
	uint8_t		peer_type;	/* PEER type */
	uint16_t	proto_version;	/* high level protocol support */
	uint32_t	status;		/* status flags */
	uint8_t		reserved02[8];
	uint32_t	dist;		/* span distance */
	uint32_t	rnss;		/* random number sub-sort */
	union {
		uint32_t	reserved03[14];
		dmsg_media_block_t block;
	} media;

	/*
	 * NOTE: for PEER_HAMMER2 cl_label is typically empty and fs_label
	 *	 is the superroot directory name.
	 *
	 *	 for PEER_BLOCK cl_label is typically host/device and
	 *	 fs_label is typically the serial number string.
	 */
	char		cl_label[DMSG_LABEL_SIZE]; /* cluster label */
	char		fs_label[DMSG_LABEL_SIZE]; /* PFS label */
};

typedef struct dmsg_lnk_span dmsg_lnk_span_t;

#define DMSG_SPAN_PROTO_1	1

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
 * Hammer2 layer ops (low-level chain manipulation used by cluster code)
 *
 * HM2_OPENPFS	- Attach a PFS
 * HM2_FLUSHPFS - Flush a PFS
 *
 * HM2_LOOKUP	- Lookup chain (parent-relative transaction)
 *		  (can request multiple chains)
 * HM2_NEXT	- Lookup next chain (parent-relative transaction)
 *		  (can request multiple chains)
 * HM2_LOCK	- [Re]lock a chain (chain-relative) (non-recursive)
 * HM2_UNLOCK	- Unlock a chain (chain-relative) (non-recursive)
 * HM2_RESIZE	- Resize a chain (chain-relative)
 * HM2_MODIFY	- Modify a chain (chain-relative)
 * HM2_CREATE	- Create a chain (parent-relative)
 * HM2_DUPLICATE- Duplicate a chain (target-parent-relative)
 * HM2_DELDUP	- Delete-Duplicate a chain (chain-relative)
 * HM2_DELETE	- Delete a chain (chain-relative)
 * HM2_SNAPSHOT	- Create a snapshot (snapshot-root-relative, w/clid override)
 */
#define DMSG_HM2_OPENPFS	DMSG_HM2(0x001, dmsg_hm2_openpfs)

/*
 * DMSG_PROTO_BLK Protocol
 *
 * BLK_OPEN	- Open device.  This transaction must be left open for the
 *		  duration and the returned keyid passed in all associated
 *		  BLK commands.  Multiple OPENs can be issued within the
 *		  transaction.
 *
 * BLK_CLOSE	- Close device.  This can be used to close one of the opens
 *		  within a BLK_OPEN transaction.  It may NOT initiate a
 *		  transaction.  Note that a termination of the transaction
 *		  (e.g. with LNK_ERROR or BLK_ERROR) closes all active OPENs
 *		  for that transaction.  XXX not well defined atm.
 *
 * BLK_READ	- Strategy read.  Not typically streaming.
 *
 * BLK_WRITE	- Strategy write.  Not typically streaming.
 *
 * BLK_FLUSH	- Strategy flush.  Not typically streaming.
 *
 * BLK_FREEBLKS	- Strategy freeblks.  Not typically streaming.
 */
#define DMSG_BLK_OPEN		DMSG_BLK(0x001, dmsg_blk_open)
#define DMSG_BLK_CLOSE		DMSG_BLK(0x002, dmsg_blk_open)
#define DMSG_BLK_READ		DMSG_BLK(0x003, dmsg_blk_read)
#define DMSG_BLK_WRITE		DMSG_BLK(0x004, dmsg_blk_write)
#define DMSG_BLK_FLUSH		DMSG_BLK(0x005, dmsg_blk_flush)
#define DMSG_BLK_FREEBLKS	DMSG_BLK(0x006, dmsg_blk_freeblks)
#define DMSG_BLK_ERROR		DMSG_BLK(0xFFF, dmsg_blk_error)

struct dmsg_blk_open {
	dmsg_hdr_t	head;
	uint32_t	modes;
	uint32_t	reserved01;
};

#define DMSG_BLKOPEN_RD		0x0001
#define DMSG_BLKOPEN_WR		0x0002

/*
 * DMSG_LNK_ERROR is returned for simple results,
 * DMSG_BLK_ERROR is returned for extended results.
 */
struct dmsg_blk_error {
	dmsg_hdr_t	head;
	uint64_t	keyid;
	uint32_t	resid;
	uint32_t	reserved02;
	char		buf[64];
};

struct dmsg_blk_read {
	dmsg_hdr_t	head;
	uint64_t	keyid;
	uint64_t	offset;
	uint32_t	bytes;
	uint32_t	flags;
	uint32_t	reserved01;
	uint32_t	reserved02;
};

struct dmsg_blk_write {
	dmsg_hdr_t	head;
	uint64_t	keyid;
	uint64_t	offset;
	uint32_t	bytes;
	uint32_t	flags;
	uint32_t	reserved01;
	uint32_t	reserved02;
};

struct dmsg_blk_flush {
	dmsg_hdr_t	head;
	uint64_t	keyid;
	uint64_t	offset;
	uint32_t	bytes;
	uint32_t	flags;
	uint32_t	reserved01;
	uint32_t	reserved02;
};

struct dmsg_blk_freeblks {
	dmsg_hdr_t	head;
	uint64_t	keyid;
	uint64_t	offset;
	uint32_t	bytes;
	uint32_t	flags;
	uint32_t	reserved01;
	uint32_t	reserved02;
};

typedef struct dmsg_blk_open		dmsg_blk_open_t;
typedef struct dmsg_blk_read		dmsg_blk_read_t;
typedef struct dmsg_blk_write		dmsg_blk_write_t;
typedef struct dmsg_blk_flush		dmsg_blk_flush_t;
typedef struct dmsg_blk_freeblks	dmsg_blk_freeblks_t;
typedef struct dmsg_blk_error		dmsg_blk_error_t;

/*
 * NOTE!!!! ALL EXTENDED HEADER STRUCTURES MUST BE 64-BYTE ALIGNED!!!
 *
 * General message errors
 *
 *	0x00 - 0x1F	Local iocomm errors
 *	0x20 - 0x2F	Global errors
 */
#define DMSG_ERR_NOSUPP		0x20
#define DMSG_ERR_LOSTLINK	0x21
#define DMSG_ERR_IO		0x22	/* generic */
#define DMSG_ERR_PARAM		0x23	/* generic */
#define DMSG_ERR_CANTCIRC	0x24	/* (typically means lost span) */

union dmsg_any {
	char			buf[DMSG_HDR_MAX];
	dmsg_hdr_t		head;

	dmsg_lnk_conn_t		lnk_conn;
	dmsg_lnk_span_t		lnk_span;

	dmsg_blk_open_t		blk_open;
	dmsg_blk_error_t	blk_error;
	dmsg_blk_read_t		blk_read;
	dmsg_blk_write_t	blk_write;
	dmsg_blk_flush_t	blk_flush;
	dmsg_blk_freeblks_t	blk_freeblks;
};

typedef union dmsg_any dmsg_any_t;

/*
 * Kernel iocom structures and prototypes for kern/kern_dmsg.c
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct hammer2_mount;
struct xa_softc;
struct kdmsg_iocom;
struct kdmsg_state;
struct kdmsg_msg;
struct kdmsg_data;

/*
 * msg_ctl flags (atomic)
 */
#define KDMSG_CLUSTERCTL_UNUSED01	0x00000001
#define KDMSG_CLUSTERCTL_KILLRX		0x00000002 /* staged helper exit */
#define KDMSG_CLUSTERCTL_KILLTX		0x00000004 /* staged helper exit */
#define KDMSG_CLUSTERCTL_SLEEPING	0x00000008 /* interlocked w/msglk */

/*
 * Transactional state structure, representing an open transaction.  The
 * transaction might represent a cache state (and thus have a chain
 * association), or a VOP op, LNK_SPAN, or other things.
 *
 * NOTE: A non-empty subq represents one ref.
 *	 If we are inserted on a parent's subq, that's one ref (SUBINSERTED).
 *	 If we are inserted on a RB tree, that's one ref (RBINSERTED).
 *	 msg->state represents a ref.
 *	 Other code references may hold refs.
 *
 * NOTE: The parent association stays intact as long as a state has a
 *	 non-empty subq.  Otherwise simulated failures might not be able
 *	 to reach the children.
 */
TAILQ_HEAD(kdmsg_state_list, kdmsg_state);

struct kdmsg_state {
	RB_ENTRY(kdmsg_state) rbnode;		/* indexed by msgid */
	struct kdmsg_state	*scan;		/* scan check */
	struct kdmsg_state_list	subq;		/* active stacked states */
	TAILQ_ENTRY(kdmsg_state) entry;		/* on parent subq */
	TAILQ_ENTRY(kdmsg_state) user_entry;	/* available to devices */
	struct kdmsg_iocom *iocom;
	struct kdmsg_state *parent;
	int		refs;			/* refs */
	uint32_t	icmd;			/* record cmd creating state */
	uint32_t	txcmd;			/* mostly for CMDF flags */
	uint32_t	rxcmd;			/* mostly for CMDF flags */
	uint64_t	msgid;			/* {parent,msgid} uniq */
	int		flags;
	int		error;
	void		*chain;			/* (caller's state) */
	int (*func)(struct kdmsg_state *, struct kdmsg_msg *);
	union {
		void *any;
		struct hammer2_mount *hmp;
		struct xa_softc *xa_sc;
	} any;
};

#define KDMSG_STATE_SUBINSERTED	0x0001
#define KDMSG_STATE_DYNAMIC	0x0002
#define KDMSG_STATE_UNUSED0004	0x0004
#define KDMSG_STATE_ABORTING	0x0008		/* avoids recursive abort */
#define KDMSG_STATE_OPPOSITE	0x0010		/* opposite direction */
#define KDMSG_STATE_DYING	0x0020		/* atomic recursive circ fail */
#define KDMSG_STATE_INTERLOCK	0x0040
#define KDMSG_STATE_RBINSERTED	0x0080
#define KDMSG_STATE_SIGNAL	0x0400
#define KDMSG_STATE_NEW		0x0800		/* defer abort processing */

struct kdmsg_msg {
	TAILQ_ENTRY(kdmsg_msg) qentry;		/* serialized queue */
	struct kdmsg_state *state;
	size_t		hdr_size;
	size_t		aux_size;
	char		*aux_data;
	uint32_t	flags;
	uint32_t	tcmd;			/* outer transaction cmd */
	dmsg_any_t	any;			/* variable sized */
};

struct kdmsg_data {
	char		*aux_data;
	size_t		aux_size;
	struct kdmsg_iocom *iocom;
};

#define KDMSG_FLAG_AUXALLOC	0x0001

typedef struct kdmsg_link kdmsg_link_t;
typedef struct kdmsg_state kdmsg_state_t;
typedef struct kdmsg_msg kdmsg_msg_t;
typedef struct kdmsg_data kdmsg_data_t;

struct kdmsg_state_tree;
int kdmsg_state_cmp(kdmsg_state_t *state1, kdmsg_state_t *state2);
RB_HEAD(kdmsg_state_tree, kdmsg_state);
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
	uint32_t		flags;
	struct lock		msglk;		/* lockmgr lock */
	TAILQ_HEAD(, kdmsg_msg) msgq;		/* transmit queue */
	void			*handle;
	void			(*auto_callback)(kdmsg_msg_t *);
	int			(*rcvmsg)(kdmsg_msg_t *);
	void			(*exit_func)(struct kdmsg_iocom *);
	struct kdmsg_state	state0;		/* root state for stacking */
	struct kdmsg_state	*conn_state;	/* active LNK_CONN state */
	struct kdmsg_state	*freerd_state;	/* allocation cache */
	struct kdmsg_state	*freewr_state;	/* allocation cache */
	struct kdmsg_state_tree staterd_tree;	/* active messages */
	struct kdmsg_state_tree statewr_tree;	/* active messages */
	dmsg_lnk_conn_t		auto_lnk_conn;
	dmsg_lnk_span_t		auto_lnk_span;
};

typedef struct kdmsg_iocom	kdmsg_iocom_t;

#define KDMSG_IOCOMF_AUTOCONN	0x0001	/* handle RX/TX LNK_CONN */
#define KDMSG_IOCOMF_AUTORXSPAN	0x0002	/* handle RX LNK_SPAN */
#define KDMSG_IOCOMF_AUTOTXSPAN	0x0008	/* handle TX LNK_SPAN */
#define KDMSG_IOCOMF_EXITNOACC	0x8000	/* cannot accept writes */

#define KDMSG_IOCOMF_AUTOANY	(KDMSG_IOCOMF_AUTOCONN |	\
				 KDMSG_IOCOMF_AUTORXSPAN |	\
				 KDMSG_IOCOMF_AUTOTXSPAN)

uint32_t kdmsg_icrc32(const void *buf, size_t size);
uint32_t kdmsg_icrc32c(const void *buf, size_t size, uint32_t crc);

/*
 * kern_dmsg.c
 */
void kdmsg_iocom_init(kdmsg_iocom_t *iocom, void *handle, u_int32_t flags,
			struct malloc_type *mmsg,
			int (*rcvmsg)(kdmsg_msg_t *msg));
void kdmsg_iocom_reconnect(kdmsg_iocom_t *iocom, struct file *fp,
			const char *subsysname);
void kdmsg_iocom_autoinitiate(kdmsg_iocom_t *iocom,
			void (*conn_callback)(kdmsg_msg_t *msg));
void kdmsg_iocom_uninit(kdmsg_iocom_t *iocom);
void kdmsg_drain_msgq(kdmsg_iocom_t *iocom);

void kdmsg_msg_free(kdmsg_msg_t *msg);
kdmsg_msg_t *kdmsg_msg_alloc(kdmsg_state_t *state, uint32_t cmd,
				int (*func)(kdmsg_state_t *, kdmsg_msg_t *),
				void *data);
void kdmsg_msg_write(kdmsg_msg_t *msg);
void kdmsg_msg_reply(kdmsg_msg_t *msg, uint32_t error);
void kdmsg_msg_result(kdmsg_msg_t *msg, uint32_t error);
void kdmsg_state_reply(kdmsg_state_t *state, uint32_t error);
void kdmsg_state_result(kdmsg_state_t *state, uint32_t error);
void kdmsg_detach_aux_data(kdmsg_msg_t *msg, kdmsg_data_t *data);
void kdmsg_free_aux_data(kdmsg_data_t *data);

#endif

#endif
