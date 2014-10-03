/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/netproto/mpls/mpls.h,v 1.2 2008/07/07 23:11:54 nant Exp $
 */

#ifndef _NETMPLS_MPLS_H_
#define _NETMPLS_MPLS_H_

#include <arpa/inet.h>

#include <sys/types.h>

typedef u_int32_t	mpls_label_t;
typedef u_int8_t	mpls_exp_t;
typedef u_int8_t	mpls_s_t;
typedef u_int8_t	mpls_ttl_t;

struct mpls {
	u_int32_t	mpls_shim;
};
#define MPLS_LABEL_MASK		0xfffff000
#define MPLS_EXP_MASK		0x00000e00
#define MPLS_STACK_MASK		0x00000100
#define MPLS_TTL_MASK		0x000000ff
#define MPLS_LABEL(shim)	(((shim) & MPLS_LABEL_MASK) >> 12)
#define MPLS_EXP(shim)		(((shim) & MPLS_EXP_MASK) >> 9)
#define MPLS_STACK(shim)	(((shim) & MPLS_STACK_MASK) >> 8)
#define MPLS_TTL(shim)		((shim) & MPLS_TTL_MASK)
#define MPLS_SET_LABEL(shim, x)					\
		do {						\
			shim &= ~MPLS_LABEL_MASK;		\
			shim |= ((x) << 12) & MPLS_LABEL_MASK;	\
		} while(0)
#define MPLS_SET_EXP(shim, x)					\
		do {						\
			shim &= ~MPLS_EXP_MASK;			\
			shim |= ((x) << 9) & MPLS_EXP_MASK;	\
		} while(0)
#define MPLS_SET_STACK(shim, x)					\
		do {						\
			shim &= ~MPLS_STACK_MASK;			\
			shim |= ((x) << 8) & MPLS_STACK_MASK;	\
		} while(0)
#define MPLS_SET_TTL(shim, x)					\
		do {						\
			shim &= ~MPLS_TTL_MASK;			\
			shim |= (x) & MPLS_TTL_MASK;		\
		} while(0)

struct	mpls_addr {
	mpls_label_t	ma_label;
};

struct	sockaddr_mpls {
	u_int8_t		smpls_len;
	u_int8_t		smpls_family;
	u_int8_t		smpls_op;	/* label op. push, pop, swap */
	mpls_exp_t		smpls_exp;
	struct mpls_addr	smpls_addr;
};
#define	smpls_label	smpls_addr.ma_label

#define MPLSLOP_PUSH	1
#define MPLSLOP_POP	2
#define MPLSLOP_SWAP	3
#define MPLSLOP_POPALL	4

#define MPLS_MAXLOPS	3

/*
 * Definitions for mpls sysctl operations.
 */
#define CTL_MPLSPROTO_NAMES {		\
	{ "mpls", CTLTYPE_NODE },	\
	{0, 0}				\
}

/*
 * Names for MPLS sysctl objects.
 */
#define MPLSCTL_FORWARDING	1

#endif /* _NETMPLS_MPLS_H_ */
