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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/thread2.h>
#include <sys/in_cksum.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/lock.h>
#include <sys/mplock2.h>

#include <net/ethernet.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_divert.h>

#include <net/ipfw3/ip_fw.h>
#include "ip_fw3_nat.h"

/*
 * Lockless Kernel NAT
 *
 * The `src` will be replaced by `alias` when a packet is leaving the system.
 * Hence, the packet is from `src` to `dst` before been translated. And after
 * been translated, the packet is from `alias` to `dst`.
 *
 * The state for outgoing packet will be stored in the nat_context of current
 * CPU. But due to the nature of the NAT, the returning packet may be handled
 * by another CPU. Hence, a state for the returning packet will be prepared and
 * store into the nat_context of the right CPU.
 */

struct ip_fw3_nat_context	*ip_fw3_nat_ctx[MAXCPU];
static struct callout 		ip_fw3_nat_cleanup_callout;
extern struct ipfw_context 	*ipfw_ctx[MAXCPU];
extern ip_fw_ctl_t 		*ipfw_ctl_nat_ptr;
static int 			sysctl_var_cleanup_interval = 1;
static int 			sysctl_var_icmp_timeout = 10;
static int 			sysctl_var_tcp_timeout = 60;
static int 			sysctl_var_udp_timeout = 30;

SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw3_nat, CTLFLAG_RW, 0, "ipfw3 NAT");
SYSCTL_INT(_net_inet_ip_fw3_nat, OID_AUTO, cleanup_interval, CTLFLAG_RW,
		&sysctl_var_cleanup_interval, 0, "default life time");
SYSCTL_INT(_net_inet_ip_fw3_nat, OID_AUTO, icmp_timeout, CTLFLAG_RW,
		&sysctl_var_icmp_timeout, 0, "default icmp state life time");
SYSCTL_INT(_net_inet_ip_fw3_nat, OID_AUTO, tcp_timeout, CTLFLAG_RW,
		&sysctl_var_tcp_timeout, 0, "default tcp state life time");
SYSCTL_INT(_net_inet_ip_fw3_nat, OID_AUTO, udp_timeout, CTLFLAG_RW,
		&sysctl_var_udp_timeout, 0, "default udp state life time");

RB_PROTOTYPE(state_tree, nat_state, entries, nat_state_cmp);
RB_GENERATE(state_tree, nat_state, entries, nat_state_cmp);

static __inline uint16_t
fix_cksum(uint16_t cksum, uint16_t old_info, uint16_t new_info, uint8_t is_udp)
{
	uint32_t tmp;

	if (is_udp && !cksum)
		return (0x0000);
	tmp = cksum + old_info - new_info;
	tmp = (tmp >> 16) + (tmp & 65535);
	tmp = tmp & 65535;
	if (is_udp && !tmp)
		return (0xFFFF);
	return tmp;
}

void
check_nat(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	if ((*args)->eh != NULL) {
		*cmd_ctl = IP_FW_CTL_NO;
		*cmd_val = IP_FW_NOT_MATCH;
		return;
	}

	struct ip_fw3_nat_context *nat_ctx;
	struct cfg_nat *nat;
	int nat_id;

	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	(*args)->rule = *f;
	nat = ((ipfw_insn_nat *)cmd)->nat;
	if (nat == NULL) {
		nat_id = cmd->arg1;
		nat = nat_ctx->nats[nat_id - 1];
		if (nat == NULL) {
			*cmd_val = IP_FW_DENY;
			*cmd_ctl = IP_FW_CTL_DONE;
			return;
		}
		((ipfw_insn_nat *)cmd)->nat = nat;
	}
	*cmd_val = ip_fw3_nat(*args, nat, (*args)->m);
	*cmd_ctl = IP_FW_CTL_NAT;
}

int
ip_fw3_nat(struct ip_fw_args *args, struct cfg_nat *nat, struct mbuf *m)
{
	struct nat_state *s, *k;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr *addr;
	struct in_addr *old_addr = NULL;
	uint16_t *old_port = NULL;
	uint16_t *port = NULL, *csum = NULL, dlen = 0;
	uint8_t udp = 0;
	boolean_t pseudo = FALSE;
	struct state_tree *tree_in = NULL, *tree_out = NULL;
	struct nat_state *s1 = NULL, *s2, *dup;

	struct in_addr oaddr;
	uint16_t oport;

	old_addr = &oaddr;
	old_port = &oport;

	k = &nat->tmp;
	if (args->oif == NULL) {
		/* for outgoing packets */
		addr = &ip->ip_dst;
		k->saddr = args->f_id.src_ip;
		k->daddr = ntohl(args->f_id.dst_ip);
		k->proto = args->f_id.proto;
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			k->sport = args->f_id.src_port;
			k->dport = ntohs(args->f_id.dst_port);
			tree_in = &nat->tree_tcp_in;
			port = &L3HDR(struct tcphdr, ip)->th_dport;
			csum = &L3HDR(struct tcphdr, ip)->th_sum;
			break;
		case IPPROTO_UDP:
			k->sport = args->f_id.src_port;
			k->dport = ntohs(args->f_id.dst_port);
			tree_in = &nat->tree_udp_in;
			port = &L3HDR(struct udphdr, ip)->uh_dport;
			csum = &L3HDR(struct udphdr, ip)->uh_sum;
			udp = 1;
			break;
		case IPPROTO_ICMP:
			k->sport = L3HDR(struct icmp, ip)->icmp_id;;
			k->dport = L3HDR(struct icmp, ip)->icmp_id;;
			tree_in = &nat->tree_icmp_in;
			port = &L3HDR(struct icmp, ip)->icmp_id;
			csum = &L3HDR(struct icmp, ip)->icmp_cksum;
			break;
		default:
			panic("ipfw3: unsupported proto %u", ip->ip_p);
		}
		s = RB_FIND(state_tree, tree_in, k);
		if (s == NULL) {
			goto oops;
		}
	} else {
		/* for incoming packets */
		addr = &ip->ip_src;
		k->saddr = args->f_id.src_ip;
		k->daddr = args->f_id.dst_ip;
		k->proto = args->f_id.proto;
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			k->sport = args->f_id.src_port;
			k->dport = args->f_id.dst_port;
			m->m_pkthdr.csum_flags = CSUM_TCP;
			tree_in = &nat->tree_tcp_in;
			tree_out = &nat->tree_tcp_out;
			port = &L3HDR(struct tcphdr, ip)->th_sport;
			csum = &L3HDR(struct tcphdr, ip)->th_sum;
			break;
		case IPPROTO_UDP:
			k->sport = args->f_id.src_port;
			k->dport = args->f_id.dst_port;
			m->m_pkthdr.csum_flags = CSUM_UDP;
			tree_in = &nat->tree_udp_in;
			tree_out = &nat->tree_udp_out;
			port = &L3HDR(struct udphdr, ip)->uh_sport;
			csum = &L3HDR(struct udphdr, ip)->uh_sum;
			udp = 1;
			break;
		case IPPROTO_ICMP:
			k->sport = 0;
			k->dport = 0;
			tree_in = &nat->tree_icmp_in;
			tree_out = &nat->tree_icmp_out;
			port = &L3HDR(struct icmp, ip)->icmp_id;
			csum = &L3HDR(struct icmp, ip)->icmp_cksum;
			break;
		default:
			panic("ipfw3: unsupported proto %u", ip->ip_p);
		}
		s = RB_FIND(state_tree, tree_out, k);
		if (s == NULL) {
			switch  (ip->ip_p) {
			case IPPROTO_TCP:
				m->m_pkthdr.csum_flags = CSUM_TCP;
				s1 = kmalloc(LEN_NAT_STATE, M_IP_FW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s1->saddr = args->f_id.src_ip;
				s1->daddr = args->f_id.dst_ip;
				s1->proto = args->f_id.proto;

				s1->sport = args->f_id.src_port;
				s1->dport = args->f_id.dst_port;

				nat_state_get_alias(s1, nat, tree_out);
				/* TODO */
				dup = RB_INSERT(state_tree, tree_out, s1);
				s2 = kmalloc(LEN_NAT_STATE, M_IP_FW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s2->saddr = args->f_id.dst_ip;
				s2->daddr = nat->ip.s_addr;
				s2->proto = args->f_id.proto;

				s2->sport = s1->dport;
				s2->dport = s1->alias_port;
				s2->alias_addr = htonl(args->f_id.src_ip);
				s2->alias_port = htons(args->f_id.src_port);
				dup = RB_INSERT(state_tree, tree_in, s2);
				break;
			case IPPROTO_UDP:
				m->m_pkthdr.csum_flags = CSUM_UDP;
				s1 = kmalloc(LEN_NAT_STATE, M_IP_FW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s1->saddr = args->f_id.src_ip;
				s1->daddr = args->f_id.dst_ip;
				s1->proto = args->f_id.proto;

				s1->sport = args->f_id.src_port;
				s1->dport = args->f_id.dst_port;

				nat_state_get_alias(s1, nat, tree_out);
				dup = RB_INSERT(state_tree, tree_out, s1);
				s2 = kmalloc(LEN_NAT_STATE, M_IP_FW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s2->saddr = args->f_id.dst_ip;
				s2->daddr = nat->ip.s_addr;
				s2->proto = args->f_id.proto;

				s2->sport = s1->dport;
				s2->dport = s1->alias_port;

				s2->alias_addr = htonl(args->f_id.src_ip);
				s2->alias_port = htons(args->f_id.src_port);
				dup = RB_INSERT(state_tree, tree_in, s2);
				break;
			case IPPROTO_ICMP:
				s1 = kmalloc(LEN_NAT_STATE, M_IP_FW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s1->saddr = args->f_id.src_ip;
				s1->daddr = args->f_id.dst_ip;
				s1->proto = args->f_id.proto;

				s1->sport = *port;
				s1->dport = *port;

				s1->alias_addr = nat->ip.s_addr;
				s1->alias_port = htons(s1->saddr % 1024);

				dup = RB_INSERT(state_tree, tree_out, s1);

				s2 = kmalloc(LEN_NAT_STATE, M_IP_FW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s2->saddr = args->f_id.dst_ip;
				s2->daddr = nat->ip.s_addr;
				s2->proto = args->f_id.proto;

				s2->sport = s1->alias_port;
				s2->dport = s1->alias_port;

				s2->alias_addr = htonl(args->f_id.src_ip);
				s2->alias_port = *port;

				dup = RB_INSERT(state_tree, tree_in, s2);
				break;
			default :
				goto oops;
			}
			s = s1;
		}
	}
	*old_addr = *addr;
	*old_port = *port;
	if (m->m_pkthdr.csum_flags & (CSUM_UDP | CSUM_TCP | CSUM_TSO)) {
		if ((m->m_pkthdr.csum_flags & CSUM_TSO) == 0) {
			dlen = ip->ip_len - (ip->ip_hl << 2);
		}
		pseudo = TRUE;
	}
	if (!pseudo) {
		const uint16_t *oaddr, *naddr;
		oaddr = (const uint16_t *)&old_addr->s_addr;
		naddr = (const uint16_t *)&s->alias_addr;
		ip->ip_sum = fix_cksum(ip->ip_sum, oaddr[0], naddr[0], 0);
		ip->ip_sum = fix_cksum(ip->ip_sum, oaddr[1], naddr[1], 0);
		if (ip->ip_p != IPPROTO_ICMP) {
			*csum = fix_cksum(*csum, oaddr[0], naddr[0], udp);
			*csum = fix_cksum(*csum, oaddr[1], naddr[1], udp);
		}
	}
	addr->s_addr = s->alias_addr;
	if (!pseudo) {
		*csum = fix_cksum(*csum, *port, s->alias_port, udp);
	}
	*port = s->alias_port;

	if (pseudo) {
		*csum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr, htons(dlen + ip->ip_p));
	}
	return IP_FW_NAT;
oops:
	return IP_FW_DENY;
}

int
nat_state_get_alias(struct nat_state *s, struct cfg_nat *nat,
		struct state_tree *tree)
{
	s->alias_addr = nat->ip.s_addr;
	do {
		s->alias_port = htons(krandom() % 64511 + 1024);
	}
	while (RB_FIND(state_tree, tree, s) != NULL);
	return 0;
}

int
nat_state_cmp(struct nat_state *s1, struct nat_state *s2)
{
	if (s1->saddr > s2->saddr)
		return 1;
	if (s1->saddr < s2->saddr)
		return -1;

	if (s1->daddr > s2->daddr)
		return 1;
	if (s1->daddr < s2->daddr)
		return -1;

	if (s1->sport > s2->sport)
		return 1;
	if (s1->sport < s2->sport)
		return -1;

	if (s1->dport > s2->dport)
		return 1;
	if (s1->dport < s2->dport)
		return -1;

	return 0;
}

int
ip_fw3_ctl_nat_get_cfg(struct sockopt *sopt)
{
	struct ip_fw3_nat_context *nat_ctx;
	struct ioc_nat *ioc;
	struct cfg_nat *nat;
	size_t valsize;
	uint8_t *val;
	int i, len;

	len = 0;
	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	valsize = sopt->sopt_valsize;
	val = sopt->sopt_val;

	ioc = (struct ioc_nat *)val;
	for (i = 0; i < NAT_ID_MAX; i++) {
		nat = nat_ctx->nats[i];
		if (nat != NULL) {
			len += LEN_IOC_NAT;
			if (len <= valsize) {
				ioc->id = nat->id;
				bcopy(&nat->ip, &ioc->ip, LEN_IN_ADDR);
				ioc++;
			} else {
				goto nospace;
			}
		}
	}
	sopt->sopt_valsize = len;
	return 0;
nospace:
	bzero(sopt->sopt_val, sopt->sopt_valsize);
	sopt->sopt_valsize = 0;
	return 0;
}

int
ip_fw3_ctl_nat_get_record(struct sockopt *sopt)
{
	/* TODO */
	return 0;
}

/*
 * Init the RB trees only when the NAT is configured.
 */
void
nat_add_dispatch(netmsg_t nat_add_msg)
{
	struct ip_fw3_nat_context *nat_ctx;
	struct netmsg_nat_add *msg;
	struct ioc_nat *ioc;
	struct cfg_nat *nat;

	msg = (struct netmsg_nat_add *)nat_add_msg;
	ioc = &msg->ioc_nat;
	nat_ctx = ip_fw3_nat_ctx[mycpuid];

	if (nat_ctx->nats[ioc->id - 1] == NULL) {
		nat = kmalloc(LEN_CFG_NAT, M_IP_FW3_NAT, M_WAITOK | M_ZERO);
		RB_INIT(&nat->tree_tcp_in);
		RB_INIT(&nat->tree_tcp_out);
		RB_INIT(&nat->tree_udp_in);
		RB_INIT(&nat->tree_udp_out);
		RB_INIT(&nat->tree_icmp_in);
		RB_INIT(&nat->tree_icmp_out);
		nat->id = ioc->id;
		memcpy(&nat->ip, &ioc->ip, LEN_IN_ADDR);
		nat_ctx->nats[ioc->id - 1] = nat;
	}
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

int
ip_fw3_ctl_nat_add(struct sockopt *sopt)
{
	struct ip_fw3_nat_context *nat_ctx;
	struct netmsg_nat_add nat_add_msg, *msg;
	struct ioc_nat *ioc;

	msg = &nat_add_msg;
	ioc = (struct ioc_nat *)(sopt->sopt_val);
	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	if (nat_ctx->nats[ioc->id - 1] != NULL) {
		return 1;
	}
	sooptcopyin(sopt, &msg->ioc_nat, sopt->sopt_valsize,
			sizeof(struct ioc_nat));
	netmsg_init(&msg->base, NULL, &curthread->td_msgport, 0,
			nat_add_dispatch);
	netisr_domsg(&msg->base, 0);
	return 0;
}

void
nat_del_dispatch(netmsg_t nat_del_msg)
{
	struct ip_fw3_nat_context *nat_ctx;
	struct netmsg_nat_del *msg;
	struct cfg_nat *nat;
	struct nat_state *s, *tmp;

	msg = (struct netmsg_nat_del *)nat_del_msg;

	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	nat = nat_ctx->nats[msg->id - 1];
	if (nat != NULL) {
		if (mycpuid == 0) {
			RB_FOREACH_SAFE(s, state_tree, &nat->tree_icmp_in, tmp) {
				RB_REMOVE(state_tree, &nat->tree_icmp_in, s);
				if (s != NULL) {
					kfree(s, M_IP_FW3_NAT);
				}
			}
			RB_FOREACH_SAFE(s, state_tree, &nat->tree_icmp_out, tmp) {
				RB_REMOVE(state_tree, &nat->tree_icmp_out, s);
				if (s != NULL) {
					kfree(s, M_IP_FW3_NAT);
				}
			}
		}
		RB_FOREACH_SAFE(s, state_tree, &nat->tree_tcp_in, tmp) {
			RB_REMOVE(state_tree, &nat->tree_tcp_in, s);
			if (s != NULL) {
				kfree(s, M_IP_FW3_NAT);
			}
		}
		RB_FOREACH_SAFE(s, state_tree, &nat->tree_tcp_out, tmp) {
			RB_REMOVE(state_tree, &nat->tree_tcp_out, s);
			if (s != NULL) {
				kfree(s, M_IP_FW3_NAT);
			}
		}
		RB_FOREACH_SAFE(s, state_tree, &nat->tree_udp_in, tmp) {
			RB_REMOVE(state_tree, &nat->tree_udp_in, s);
			if (s != NULL) {
				kfree(s, M_IP_FW3_NAT);
			}
		}
		RB_FOREACH_SAFE(s, state_tree, &nat->tree_udp_out, tmp) {
			RB_REMOVE(state_tree, &nat->tree_icmp_in, s);
			if (s != NULL) {
				kfree(s, M_IP_FW3_NAT);
			}
		}
		kfree(nat, M_IP_FW3_NAT);
		nat_ctx->nats[msg->id - 1] = NULL;
	}
	netisr_forwardmsg_all(&nat_del_msg->base, mycpuid + 1);
}

int
ip_fw3_ctl_nat_del(struct sockopt *sopt)
{
	struct netmsg_nat_del nat_del_msg, *msg;

	msg = &nat_del_msg;
	msg->id = *((int *)sopt->sopt_val);
	netmsg_init(&msg->base, NULL, &curthread->td_msgport,
			0, nat_del_dispatch);

	netisr_domsg(&msg->base, 0);
	return 0;
}

int
ip_fw3_ctl_nat_flush(struct sockopt *sopt)
{
	struct netmsg_nat_del nat_del_msg, *msg;
	int i;
	msg = &nat_del_msg;
	for (i = 0; i < NAT_ID_MAX; i++) {
		msg->id = i + 1;
		netmsg_init(&msg->base, NULL, &curthread->td_msgport,
				0, nat_del_dispatch);

		netisr_domsg(&msg->base, 0);
	}
	return 0;
}

int
ip_fw3_ctl_nat_sockopt(struct sockopt *sopt)
{
	int error = 0;
	switch (sopt->sopt_name) {
	case IP_FW_NAT_ADD:
		error = ip_fw3_ctl_nat_add(sopt);
		break;
	case IP_FW_NAT_DEL:
		error = ip_fw3_ctl_nat_del(sopt);
		break;
	case IP_FW_NAT_FLUSH:
		error = ip_fw3_ctl_nat_flush(sopt);
		break;
	case IP_FW_NAT_GET:
		error = ip_fw3_ctl_nat_get_cfg(sopt);
		break;
	case IP_FW_NAT_GET_RECORD:
		error = ip_fw3_ctl_nat_get_record(sopt);
		break;
	default:
		kprintf("ipfw3 nat invalid socket option %d\n",
				sopt->sopt_name);
	}
	return error;
}

void
nat_init_ctx_dispatch(netmsg_t msg)
{
	struct ip_fw3_nat_context *tmp;
	tmp = kmalloc(sizeof(struct ip_fw3_nat_context),
				M_IP_FW3_NAT, M_WAITOK | M_ZERO);

	ip_fw3_nat_ctx[mycpuid] = tmp;
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

void
nat_fnit_ctx_dispatch(netmsg_t msg)
{
	kfree(ip_fw3_nat_ctx[mycpuid], M_IP_FW3_NAT);
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

static void
ipfw3_nat_cleanup_func_dispatch(netmsg_t nmsg)
{
	/* TODO */
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

static void
ipfw3_nat_cleanup_func(void *dummy __unused)
{
	struct netmsg_base msg;
	netmsg_init(&msg, NULL, &curthread->td_msgport, 0,
			ipfw3_nat_cleanup_func_dispatch);
	netisr_domsg(&msg, 0);

	callout_reset(&ip_fw3_nat_cleanup_callout,
			sysctl_var_cleanup_interval * hz,
			ipfw3_nat_cleanup_func,
			NULL);
}

static int
ip_fw3_nat_init(void)
{
	struct netmsg_base msg;
	register_ipfw_module(MODULE_NAT_ID, MODULE_NAT_NAME);
	register_ipfw_filter_funcs(MODULE_NAT_ID, O_NAT_NAT,
			(filter_func)check_nat);
	ipfw_ctl_nat_ptr = ip_fw3_ctl_nat_sockopt;
	netmsg_init(&msg, NULL, &curthread->td_msgport,
			0, nat_init_ctx_dispatch);
	netisr_domsg(&msg, 0);

	callout_init_mp(&ip_fw3_nat_cleanup_callout);
	callout_reset(&ip_fw3_nat_cleanup_callout,
			sysctl_var_cleanup_interval * hz,
			ipfw3_nat_cleanup_func,
			NULL);
	return 0;
}

static int
ip_fw3_nat_fini(void)
{
	struct netmsg_base msg;
	struct netmsg_nat_del nat_del_msg, *msg1;
	int i;

	callout_stop(&ip_fw3_nat_cleanup_callout);

	msg1 = &nat_del_msg;
	for (i = 0; i < NAT_ID_MAX; i++) {
		msg1->id = i + 1;
		netmsg_init(&msg1->base, NULL, &curthread->td_msgport,
				0, nat_del_dispatch);

		netisr_domsg(&msg1->base, 0);
	}

	netmsg_init(&msg, NULL, &curthread->td_msgport,
			0, nat_fnit_ctx_dispatch);
	netisr_domsg(&msg, 0);

	return unregister_ipfw_module(MODULE_NAT_ID);
}

static int
ip_fw3_nat_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		return ip_fw3_nat_init();
	case MOD_UNLOAD:
		return ip_fw3_nat_fini();
	default:
		break;
	}
	return 0;
}

moduledata_t ip_fw3_nat_mod = {
	"ipfw3_nat",
	ip_fw3_nat_modevent,
	NULL
};

DECLARE_MODULE(ipfw3_nat, ip_fw3_nat_mod,
		SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY);
MODULE_DEPEND(ipfw3_nat, ipfw3_basic, 1, 1, 1);
MODULE_VERSION(ipfw3_nat, 1);
