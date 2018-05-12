/*
 * Copyright (c) 2014 - 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@dragonflybsd.org>
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

#ifndef _IP_FW3_NAT_H
#define _IP_FW3_NAT_H

#define MODULE_NAT_ID		4
#define MODULE_NAT_NAME		"nat"
#define NAT_ID_MAX		16

#define ALIAS_RANGE		64511
#define ALIAS_BEGIN		1024

#define LEN_IN_ADDR		sizeof(struct in_addr)

enum ipfw_nat_opcodes {
	O_NAT_NAT,
};

struct ioc_nat_state {
	struct in_addr		src_addr;
	struct in_addr		dst_addr;
	struct in_addr		alias_addr;
	u_short			src_port;
	u_short			dst_port;
	u_short			alias_port;
	int			nat_id;
	int			cpu_id;
	int			proto;
	int			direction;
	time_t			life;
};
#define LEN_IOC_NAT_STATE sizeof(struct ioc_nat_state)

struct ioc_nat {
	int			id;
	int			count;
	struct in_addr 		ip;
};
#define LEN_IOC_NAT sizeof(struct ioc_nat)

typedef struct	_ipfw_insn_nat {
	ipfw_insn		o;
	struct cfg_nat  	*nat;
} ipfw_insn_nat;


#ifdef _KERNEL

/*
 * Each NAT state contains the tuple (saddr,sport,daddr,dport,proto) and a pair
 * of alias(alias_addr & alias_port).
 * For outgoing TCP & UDP packets, the alias will be the after NAT src
 * For incoming TCP & UDP packets, its alias will be the original src info.
 * For ICMP packets, the icmp_id will be stored in the alias.
 */
struct nat_state {
	RB_ENTRY(nat_state)	entries;
	uint32_t		src_addr;
	uint32_t		dst_addr;
	uint32_t		alias_addr;
	uint16_t		src_port;
	uint16_t		dst_port;
	uint16_t		alias_port;
	time_t			timestamp;
};
#define LEN_NAT_STATE sizeof(struct nat_state)

/* nat_state for the incoming packets */
struct nat_state2 {
	uint32_t		src_addr;
	uint32_t		dst_addr;
	uint32_t		alias_addr;
	uint16_t		src_port;
	uint16_t		dst_port;
	uint16_t		alias_port;
	time_t			timestamp;
};
#define LEN_NAT_STATE2 sizeof(struct nat_state2)

int 	ip_fw3_nat_state_cmp(struct nat_state *s1, struct nat_state *s2);

RB_HEAD(state_tree, nat_state);

struct cfg_nat {
	int			id;
	int			count;
	LIST_HEAD(, cfg_alias)	alias;	/* list of the alias IP */

	struct state_tree	rb_tcp_out;
	struct state_tree	rb_udp_out;
	struct state_tree	rb_icmp_out;
};

#define LEN_CFG_NAT sizeof(struct cfg_nat)

struct cfg_alias {
	LIST_ENTRY(cfg_alias)	next;
	struct in_addr 		ip;
	struct nat_state2	*tcp_in[ALIAS_RANGE];
	struct nat_state2	*udp_in[ALIAS_RANGE];
	struct nat_state2	*icmp_in[ALIAS_RANGE];
};
#define LEN_CFG_ALIAS sizeof(struct cfg_alias)

/* place to hold the nat conf */
struct ip_fw3_nat_context {
	struct cfg_nat 		*nats[NAT_ID_MAX];
};

struct netmsg_nat_del {
	struct netmsg_base 	base;
	int 			id;
};

struct netmsg_nat_add {
	struct netmsg_base 	base;
	struct ioc_nat 		ioc_nat;
};

struct netmsg_nat_state_add {
	struct netmsg_base 	base;
	struct nat_state2 	*state;
	struct in_addr		alias_addr;
	uint16_t		alias_port;
	int			proto;
	int			nat_id;
};
#define LEN_NMSG_NAT_STATE_ADD sizeof(struct netmsg_nat_state_add)

void 	check_nat(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

int 	ip_fw3_nat(struct ip_fw_args *, struct cfg_nat *, struct mbuf *);

void	pick_alias_port(struct nat_state *s, struct state_tree *tree);

void 	nat_state_add_dispatch(netmsg_t msg);
void 	nat_add_dispatch(netmsg_t msg);
int 	ip_fw3_ctl_nat_add(struct sockopt *sopt);
void 	nat_del_dispatch(netmsg_t msg);
int 	ip_fw3_ctl_nat_del(struct sockopt *sopt);
int 	ip_fw3_ctl_nat_flush(struct sockopt *sopt);
void 	nat_init_ctx_dispatch(netmsg_t msg);
void 	nat_fnit_ctx_dispatch(netmsg_t msg);
int 	ip_fw3_ctl_nat_sockopt(struct sockopt *sopt);
int 	ip_fw3_ctl_nat_get_cfg(struct sockopt *sopt);
int 	ip_fw3_ctl_nat_get_record(struct sockopt *sopt);

#endif
#endif
