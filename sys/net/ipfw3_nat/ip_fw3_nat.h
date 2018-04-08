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

#ifndef _IP_FW_NAT_H
#define _IP_FW_NAT_H

#define MODULE_NAT_ID		4
#define MODULE_NAT_NAME		"nat"
#define NAT_ID_MAX		4

enum ipfw_nat_opcodes {
	O_NAT_NAT,
};

struct ipfw_ioc_nat_state {
	struct in_addr	src_addr;
	struct in_addr	dst_addr;
	struct in_addr	alias_addr;
	int		link_type;
	int		timestamp;
	int		expire_time;
	int		nat_id;
	int		cpuid;
	int		is_outgoing;
	u_short		src_port;
	u_short		dst_port;
	u_short		alias_port;
};

struct ioc_cfg_nat {
	int		id;
	struct in_addr 	ip;
};
#define LEN_IOC_NAT sizeof(struct ioc_cfg_nat)



#ifdef _KERNEL


struct nat_state {
	RB_ENTRY(nat_state)	entries;
	uint32_t		saddr;
	uint32_t		daddr;
	uint32_t		alias_addr;
	uint16_t		sport;
	uint16_t		dport;
	uint16_t		alias_port;
	uint8_t			proto;
	int			timestamp;
	int			expiry;
};
#define LEN_NAT_STATE sizeof(struct nat_state)

int 	nat_state_cmp(struct nat_state *s1, struct nat_state *s2);

RB_HEAD(state_tree, nat_state);
RB_PROTOTYPE(state_tree, nat_state, entries, nat_state_cmp);
RB_GENERATE(state_tree, nat_state, entries, nat_state_cmp);

struct cfg_nat {
	int			id;
	struct in_addr		ip;

	struct state_tree	tree_tcp_in;
	struct state_tree	tree_tcp_out;
	struct state_tree	tree_udp_in;
	struct state_tree	tree_udp_out;
	struct state_tree	tree_icmp_in;
	struct state_tree	tree_icmp_out;

	struct nat_state	tmp;
};
#define LEN_CFG_NAT sizeof(struct cfg_nat)




MALLOC_DEFINE(M_IPFW_NAT, "IPFW3/NAT", "IPFW3/NAT 's");

/* place to hold the nat conf */
struct ipfw_nat_context {
	struct cfg_nat		*nats[NAT_ID_MAX];
};

struct netmsg_nat_del {
	struct netmsg_base 	base;
	int 			id;
};

struct netmsg_nat_add {
	struct netmsg_base 	base;
	char 			*buf;
};

struct netmsg_alias_link_add {
	struct netmsg_base 	base;
	int 			id;
	int 			is_outgoing;
	int 			is_tcp;
};

#endif

typedef struct	_ipfw_insn_nat {
	ipfw_insn	o;
	struct cfg_nat *nat;
} ipfw_insn_nat;


#ifdef _KERNEL
void check_nat(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void add_alias_link_dispatch(netmsg_t nat_del_msg);
int ipfw_nat(struct ip_fw_args *args, struct cfg_nat *t, struct mbuf *m);
void nat_add_dispatch(netmsg_t msg);
int ipfw_ctl_nat_add(struct sockopt *sopt);
void nat_del_dispatch(netmsg_t msg);
int ipfw_ctl_nat_del(struct sockopt *sopt);
int ipfw_ctl_nat_flush(struct sockopt *sopt);
int ipfw_ctl_nat_sockopt(struct sockopt *sopt);
void nat_init_ctx_dispatch(netmsg_t msg);
int ipfw_ctl_nat_get_cfg(struct sockopt *sopt);
int ipfw_ctl_nat_get_record(struct sockopt *sopt);
#endif
#endif
