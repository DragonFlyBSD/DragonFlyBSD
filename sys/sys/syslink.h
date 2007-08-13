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
 * $DragonFly: src/sys/sys/syslink.h,v 1.13 2007/08/13 17:47:20 dillon Exp $
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
 */

#ifndef _SYS_SYSLINK_H_
#define _SYS_SYSLINK_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_SYSID_H_
#include <sys/sysid.h>
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_SYSLINK_MSG_H_
#include <sys/syslink_msg.h>
#endif

/*
 * Additional headers are required to support kernel-only
 * data structures.
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _SYS_BUF_H_
#include <sys/buf.h>
#endif
#ifndef _SYS_BIO_H_
#include <sys/bio.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif
#endif

/***************************************************************************
 *				PROTOCOL API/ABI			   *
 ***************************************************************************
 *
 * These structures implement the programming interface for end-points and
 * RPC calls.
 */

struct syslink;
struct syslink_desc;
struct syslink_generic_args;

typedef int (*syslink_func_t)(struct syslink_generic_args *);

#define SYSLINK_CMD_NEW		0x00000001	/* create unassociated desc */

/*
 * Typically an extension of the syslink_info structure is passed
 * to the kernel, or NULL for commands that do not need one.
 */
struct syslink_info {
	int version;
	int wbflag;
	int reserved[2];
};

#define SYSLINK_INFO_VERSION	1

/*
 * SYSLINK_CMD_NEW
 *
 *	Create a pair of syslink descriptors representing a two-way
 *	communications channel.
 */
struct syslink_info_new {
	struct syslink_info head;
	int fds[2];
};

union syslink_info_all {
	struct syslink_info		head;
	struct syslink_info_new		cmd_new;
};

/*
 * Kernel-only data structures.
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * slmsg - internal kernel representation of a syslink message, organized
 *	   as a LWKT message, out of band DMA buffer(s), and related memory.
 *	   This structure is typically arranged via two object caches,
 *	   one which maintains buffers for short syslink messages and one
 *	   for long syslink messages.
 */
TAILQ_HEAD(slmsgq,slmsg);
RB_HEAD(slmsg_rb_tree, slmsg);
RB_PROTOTYPE2(slmsg_rb_tree, slmsg, entry.rbnode, rb_slmsg_compare, sysid_t);

struct slmsg {
	TAILQ_ENTRY(slmsg) tqnode;	/* inq */
	RB_ENTRY(slmsg) rbnode;		/* held for reply matching */
	int msgsize;
	int maxsize;
	int flags;
	caddr_t vmbase;			/* memory mapped */
	size_t vmsize;
	struct xio xio;			/* restricted xio */
	struct slmsg *rep;		/* reply (kernel backend) */
	struct objcache *oc;
	struct syslink_msg *msg;
	void (*callback_func)(struct slmsg *, void *, int);
	void *callback_data;
};

#define SLMSGF_ONINQ	0x0001
#define SLMSGF_HASXIO	0x0002
#define SLMSGF_LINMAP	0x0004

#endif

#if defined(_KERNEL)

struct sldesc;
int syslink_ukbackend(int *fdp, struct sldesc **kslp);
struct slmsg *syslink_kallocmsg(void);
int syslink_kdomsg(struct sldesc *ksl, struct slmsg *msg);
int syslink_ksendmsg(struct sldesc *ksl, struct slmsg *msg,
		     void (*func)(struct slmsg *, void *, int), void *arg);
int syslink_kwaitmsg(struct sldesc *ksl, struct slmsg *msg);
void syslink_kfreemsg(struct sldesc *ksl, struct slmsg *msg);
void syslink_kshutdown(struct sldesc *ksl, int how);
void syslink_kclose(struct sldesc *ksl);
int syslink_kdmabuf_pages(struct slmsg *slmsg, struct vm_page **mbase, int npages);
int syslink_kdmabuf_data(struct slmsg *slmsg, char *base, int bytes);



#else

int syslink(int, struct syslink_info *, size_t);

#endif

#endif
