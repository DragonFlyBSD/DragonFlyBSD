/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@gmail.com>
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

#ifndef _IP_FW_NAT_H
#define _IP_FW_NAT_H

#define MODULE_NAT_ID		4
#define MODULE_NAT_NAME		"nat"

#ifdef _KERNEL
MALLOC_DEFINE(M_IPFW_NAT, "IPFW3/NAT", "IPFW3/NAT 's");
#endif

enum ipfw_nat_opcodes {
	O_NAT_NAT,
};

/* Redirect modes id. */
#define REDIR_ADDR		0x01
#define REDIR_PORT		0x02
#define REDIR_PROTO		0x04

/* Server pool support (LSNAT). */
struct cfg_spool {
	LIST_ENTRY(cfg_spool)	_next;	/* chain of spool instances */
	struct in_addr		addr;
	u_short			port;
};

struct cfg_redir {
	LIST_ENTRY(cfg_redir)	_next;	/* chain of redir instances */
	u_int16_t		mode;	/* type of redirect mode */
	struct in_addr		laddr;	/* local ip address */
	struct in_addr		paddr;	/* public ip address */
	struct in_addr		raddr;	/* remote ip address */
	u_short			lport;	/* local port */
	u_short			pport;	/* public port */
	u_short			rport;	/* remote port */
	u_short			pport_cnt;	/* number of public ports */
	u_short			rport_cnt;	/* number of remote ports */
	int			proto;		/* protocol: tcp/udp */
	struct alias_link	**alink;
	/* num of entry in spool chain */
	u_int16_t		spool_cnt;
	/* chain of spool instances */
	LIST_HEAD(spool_chain, cfg_spool) spool_chain;
};

/* Nat configuration data struct. */
struct cfg_nat {
	/* chain of nat instances */
	LIST_ENTRY(cfg_nat)	_next;
	int			id;	/* nat id */
	struct in_addr		ip;	/* nat ip address */
	char	if_name[IF_NAMESIZE];	/* interface name */
	int	mode;			/* aliasing mode */
	struct libalias		*lib;	/* libalias instance */
	/* number of entry in spool chain */
	int	redir_cnt;
	/* chain of redir instances */
	LIST_HEAD(redir_chain, cfg_redir) redir_chain;
};

#define SOF_NAT			sizeof(struct cfg_nat)
#define SOF_REDIR		sizeof(struct cfg_redir)
#define SOF_SPOOL		sizeof(struct cfg_spool)

/* Nat command. */
typedef struct	_ipfw_insn_nat {
	ipfw_insn	o;
	struct cfg_nat *nat;
} ipfw_insn_nat;

#define LOOKUP_NAT(l, i, p) do {			\
	LIST_FOREACH((p), &(l.nat), _next){		\
		if((p)->id == (i)){			\
			break;				\
		}					\
	}						\
} while (0)

#define HOOK_NAT(b, p) do {				\
	LIST_INSERT_HEAD(b, p, _next);			\
} while (0)

#define UNHOOK_NAT(p) do {				\
	LIST_REMOVE(p, _next);				\
} while (0)

#define HOOK_REDIR(b, p) do {				\
	LIST_INSERT_HEAD(b, p, _next);			\
} while (0)

#define HOOK_SPOOL(b, p) do {				\
	LIST_INSERT_HEAD(b, p, _next);			\
} while (0)

#endif
