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
 * $DragonFly: src/sys/sys/syslink.h,v 1.6 2007/04/22 00:59:27 dillon Exp $
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

#ifndef _SYS_SYSLINK_H_
#define _SYS_SYSLINK_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_SOCKET_H_
#include <sys/socket.h>
#endif

/*
 * SYSIDs are 64 bit entities and come in two varieties.  Physical SYSIDs
 * are used to efficiently route messages across the mesh, while Logical
 * SYSIDs are persistently assigned identifiers representing specific devices
 * or specific media or named filesystems.  That is, the logical SYSID
 * assigned to a filesystem or ANVIL partition can be stored in that
 * filesystem's superblock and allows the filesystem to migrate or
 * be multi-homed (have multiple physical SYSIDs representing the same
 * logical entity).
 *
 * Physical SYSIDs can be ever-changing, and any given logical SYSID could
 * in fact have multiple physical SYSIDs associated with it.  The mesh is
 * self-healing and the combination of the logical and physical sysid
 * basically validates the message at the target and determines whether
 * the physical SYSID must be recalculated (looked up again) or not.
 */
typedef u_int64_t       sysid_t;

/***************************************************************************
 *				PROTOCOL API/ABI			   *
 ***************************************************************************
 *
 * These structures implement the programming interface for end-points and
 * RPC calls.
 */

struct syslink;
struct syslink_ops;
struct syslink_proto;
struct syslink_transport;
struct syslink_desc;
struct syslink_generic_args;

typedef int (*syslink_func_t)(struct syslink_generic_args *);

/*
 * Commands for the syslink system call.
 *
 * CREATE:	Create a new syslink route node with the route node sysid
 *		and label information specified in the passed info structure.
 *		0 on success, -1 on failure or if the route node already
 *		exists.  
 *
 *		The info structure must also contain the number of bits of
 *		address space you wish this route node to use.
 *
 * DESTROY:	Destroy the existing syslink route node with the route node
 *		sysid specified in the passed info structure.
 *		0 on success, -1 on failure.
 *
 * LOCATE:	Locate the first syslink route node with a sysid greater or
 *		equal to the sysid specified in the passed info structure.
 *		The info structure will be loaded on success and the linkid
 *		field will also be cleared.
 *
 *		To scan route nodes, start by specifying a sysid 0.  On
 *		success, increment the sysid in the info structure and loop.
 *
 *		You can also use the contents of the info structure to
 *		initiate a scan of links within the route node via the FIND
 *		command.  The sysid will remain unchanged through the scan
 *		and on completion you can increment it and loop up to 
 *		the next LOCATE.
 *
 * ADD:		Add a link to the route node specified in the info structure.
 *		The route node must exist and must contain sufficient
 *		address space to handle the new link.  If associating a file
 *		descriptor, 0 is returned on success and -1 on failure.
 *		If obtaining a direct syslink file descriptor, the new
 *		descriptor is returned on success or -1 on failure.
 *
 *		On return, the linkid in the info structure is filled in and
 *		other adjustments may also have been made.
 *
 *		The info structure must contain the number of bits of
 *		address space required by the link.  If 0 is specified,
 *		a direct stream or connected socket is expected.  If
 *		non-zero, a switch is assumed (typically a switched UDP
 *		socket).  An all 0's address is reserved and an all 1's
 *		address implies a broadcast.  All other addresses imply
 *		single targets within the switched infrastructure.
 *		
 *		FLAGS SUPPORTED:
 *
 *		SENDTO		This allows you to directly associate a
 *				UDP socket and subnet with a syslink route
 *				node.  To use this option the info structure
 *				must contain a sockaddr representing the
 *				broadcast address.  The low bits are adjusted
 *				based on the size of the subnet (as
 *				specified with PHYSBITS) to forward messages.
 *				If not specified, write() is used and the
 *				target is responsible for any switch
 *				demultiplexing.
 *
 *		PACKET		Indicates that messages are packetized.
 *				syslink will aggregate multiple syslink
 *				messages into a single packet if possible,
 *				but will not exceed 16384 bytes per packet
 *				and will not attempt to pad messages to
 *				align them for FIFO debuffering.
 *
 * REM:		Disassociate a link using the route node and linkid
 *		specified in the info structure. 
 *
 *		The syslink route node will close() the related descriptor
 *		(or cause an EOF to occur for any direct syslink descriptor).
 *
 * FIND:	Locate the first linkid greater or equal to the linkid
 *		in the passed info structure for the route node sysid 
 *		specified in the info structure, and fill in the rest of the
 *		structure as appropriate.
 *
 *		To locate the first link for any given route sysid, set
 *		linkid to 0.  To scan available links, increment the
 *		returned linkid before looping.
 */

#define SYSLINK_CMD_CREATE	0x00000001	/* create route node */
#define SYSLINK_CMD_DESTROY	0x00000002	/* destroy route node */
#define SYSLINK_CMD_LOCATE	0x00000003	/* locate first route node */
#define SYSLINK_CMD_ADD		0x00000004	/* add link */
#define SYSLINK_CMD_REM		0x00000005	/* remove link */
#define SYSLINK_CMD_FIND	0x00000006	/* locate link info */

#define SYSLINK_LABEL_SIZE	32
#define SYSLINK_ROUTER_MAXBITS	20
/*
 * syslink_info structure
 *
 * This structure contains information about a syslink route node or 
 * linkage.
 */
struct syslink_info {
	int version;			/* info control structure version */
	int fd;				/* file descriptor (CMD_ADD) */
	int linkid;			/* linkid (base physical address) */
	int bits;			/* physical address bits if switched */
	int flags;			/* message control/switch flags */
	int reserved01;
	sysid_t	sysid;			/* route node sysid */
	char label[SYSLINK_LABEL_SIZE];	/* symbolic name */
	char reserved[32];
	union {
		struct sockaddr sa;	/* required for SLIF_SENDTO */
	} u;
};

#define SLIF_PACKET	0x0001		/* packetized, else stream */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#define SLIF_RQUIT	0x0400
#define SLIF_WQUIT	0x0800
#define SLIF_RDONE	0x1000
#define SLIF_WDONE	0x2000
#define SLIF_DESTROYED	0x4000
#define SLIF_ERROR	0x8000
#endif

#define SLIF_USERFLAGS		(SLIF_PACKET)


/*
 * A syslink structure represents an end-point for communications.  System
 * structures such as vnodes are typically associated with end-points and
 * usually embed a syslink structure.  There is typically one master
 * structure (sl_remote_id == 0) and any number of slave structures at
 * remote locations (sl_remote_id on slaves point to master).
 *
 * A ref counter is integrated into the structure and used by SYSLINK to
 * keep track of sysid references sent to remote targets.  This counter
 * may also be used by the governing structure (e.g. vnode) so long as
 * the SYSLINK API is used to manipulate it.
 *
 * An operations vector implements the ABI for the numerous functions
 * associated with the system structure.  E.G. VOPs for vnodes.  The
 * ops structure also references the transport and protocol layers.  Using
 * vnodes as an example, the ops structure would be replicated from a
 * template on a per-mount basis.
 */
struct syslink {
	sysid_t		sl_source;
	sysid_t		sl_target;
	int		sl_refs;	/* track references */
	struct syslink_ops *sl_ops;	/* operations vector */
};

/*
 * The syslink_ops structure is typically embedded as part of a larger system
 * structure.  It conatins a reference to the transport layer (if any),
 * protocol, and a structural offset range specifying the function vectors
 * in the larger system structure.
 *
 * For example, vnode operations (VOPs) embed this structure in the vop_ops
 * structure.
 *
 * The syslink_ops structure may be replaced as necessary.  The VFS subsystem
 * typically replicates syslink_ops on a per-mount basis and stores a pointer
 * to the mount point in the larger system structure (vop_ops).
 */
struct syslink_ops {
	struct syslink_proto *proto;
	void *transport;	/* FUTURE USE (transport layer) */
	int beg_offset;
	int end_offset;
};

/*
 * The syslink_desc structure describes a function vector in the protocol.
 * This structure may be extended by the protocol to contain additional
 * information.
 */
struct syslink_desc {
	int sd_offset;		/* offset into ops structure */
	const char *sd_name;	/* name for debugging */
};

/*
 * The syslink_proto structure describes a protocol.  The structure contains
 * templates for the various ops structures required to implement the
 * protocol.
 */
struct syslink_proto {
 	const char	*sp_name;	/* identifying name */
	int		sp_flags;
	int		sp_opssize;	/* structure embedding syslink_ops */
	struct syslink_ops *sp_call_encode;	/* encode call */
	struct syslink_ops *sp_call_decode;	/* decode call */
	struct syslink_ops *sp_reply_encode;	/* encode reply */
	struct syslink_ops *sp_reply_decode;	/* decode reply */
	struct syslink_ops *sp_ops; 		/* direct ABI calls */
};

#define SPF_ALLOCATED	0x00000001

/*
 * The syslink_generic_args structure contains the base data required in
 * the arguments structure passed to any given ops function.  This structure
 * is typically extended with the actual call arguments.
 */
struct syslink_generic_args {
	struct syslink_desc	*a_desc;	/* ABI method description */
	struct syslink		*a_syslink;	/* original syslink */
	/* extend arguments */
};

typedef struct syslink *syslink_t;
typedef struct syslink_ops *syslink_ops_t;
typedef struct syslink_desc *syslink_desc_t;
typedef struct syslink_proto *syslink_proto_t;
typedef struct syslink_generic_args *syslink_generic_args_t;

#if !defined(_KERNEL)
int syslink(int, struct syslink_info *, size_t);
#endif

#endif
