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
 * $DragonFly: src/sys/sys/syslink.h,v 1.1 2006/07/19 06:08:07 dillon Exp $
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

/*
 * SYSIDs are 64 bit entities.
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
	sysid_t		sl_sysid;	/* syslink id of this end-point */
	sysid_t		sl_remote_id;	/* syslink id of remote end-point */
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

#endif
