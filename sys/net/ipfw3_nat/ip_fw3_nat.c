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

#include "opt_ipfw.h"
#include "opt_inet.h"
#ifndef INET
#error IPFIREWALL3 requires INET.
#endif /* INET */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/in_cksum.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/lock.h>

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

MALLOC_DEFINE(M_IPFW3_NAT, "IP_FW3_NAT", "ipfw3_nat module");

/*
 * Highspeed Lockless Kernel NAT
 *
 * Kernel NAT
 * The network address translation (NAT) will replace the `src` of the packet
 * with an `alias` (alias_addr & alias_port). Accordingt to the configuration,
 * The alias will be randomly picked from the configured range.
 *
 * Highspeed
 * The first outgoing packet should trigger the creation of the `net_state`,
 * and the `net_state` will keep in a RB-Tree for the subsequent outgoing
 * packets.
 * The first returning packet will trigger the creation of the `net_state2`,
 * which will be stored in a multidimensional array of points ( of net_state2 ).
 *
 * Lockless
 * The `net_state` for outgoing packet will be stored in the nat_context of
 * current CPU. But due to the nature of the NAT, the returning packet may be
 * handled by another CPU. Hence, The `net_state2` for the returning packet
 * will be prepared and stored into the nat_context of the right CPU.
 */

struct ip_fw3_nat_context	*ip_fw3_nat_ctx[MAXCPU];
static struct callout 		ip_fw3_nat_cleanup_callout;
extern struct ipfw3_context 	*fw3_ctx[MAXCPU];
extern ip_fw_ctl_t 		*ip_fw3_ctl_nat_ptr;

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

RB_PROTOTYPE(state_tree, nat_state, entries, ip_fw3_nat_state_cmp);
RB_GENERATE(state_tree, nat_state, entries, ip_fw3_nat_state_cmp);

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
	struct state_tree *tree_out = NULL;
	struct nat_state *s = NULL, *dup, *k, key;
	struct nat_state2 *s2 = NULL;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr *old_addr = NULL, new_addr;
	uint16_t *old_port = NULL, new_port;
	uint16_t *csum = NULL, dlen = 0;
	uint8_t udp = 0;
	boolean_t pseudo = FALSE, need_return_state = FALSE;
	struct cfg_alias *alias;
	int i = 0, rand_n = 0;

	k = &key;
	memset(k, 0, LEN_NAT_STATE);
	if (args->oif == NULL) {
		old_addr = &ip->ip_dst;
		k->dst_addr = ntohl(args->f_id.dst_ip);
		LIST_FOREACH(alias, &nat->alias, next) {
			if (alias->ip.s_addr == ntohl(args->f_id.dst_ip)) {
				break;
			}
		}
		if (alias == NULL) {
			goto oops;
		}
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			old_port = &L3HDR(struct tcphdr, ip)->th_dport;
			s2 = alias->tcp_in[*old_port - ALIAS_BEGIN];
			csum = &L3HDR(struct tcphdr, ip)->th_sum;
			break;
		case IPPROTO_UDP:
			old_port = &L3HDR(struct udphdr, ip)->uh_dport;
			s2 = alias->udp_in[*old_port - ALIAS_BEGIN];
			csum = &L3HDR(struct udphdr, ip)->uh_sum;
			udp = 1;
			break;
		case IPPROTO_ICMP:
			old_port = &L3HDR(struct icmp, ip)->icmp_id;
			s2 = alias->icmp_in[*old_port];
			csum = &L3HDR(struct icmp, ip)->icmp_cksum;
			break;
		default:
			panic("ipfw3: unsupported proto %u", ip->ip_p);
		}
		if (s2 == NULL) {
			goto oops;
		}
	} else {
		old_addr = &ip->ip_src;
		k->src_addr = args->f_id.src_ip;
		k->dst_addr = args->f_id.dst_ip;
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			k->src_port = args->f_id.src_port;
			k->dst_port = args->f_id.dst_port;
			m->m_pkthdr.csum_flags = CSUM_TCP;
			tree_out = &nat->rb_tcp_out;
			old_port = &L3HDR(struct tcphdr, ip)->th_sport;
			csum = &L3HDR(struct tcphdr, ip)->th_sum;
			break;
		case IPPROTO_UDP:
			k->src_port = args->f_id.src_port;
			k->dst_port = args->f_id.dst_port;
			m->m_pkthdr.csum_flags = CSUM_UDP;
			tree_out = &nat->rb_udp_out;
			old_port = &L3HDR(struct udphdr, ip)->uh_sport;
			csum = &L3HDR(struct udphdr, ip)->uh_sum;
			udp = 1;
			break;
		case IPPROTO_ICMP:
			k->src_port = L3HDR(struct icmp, ip)->icmp_id;
			k->dst_port = k->src_port;
			tree_out = &nat->rb_icmp_out;
			old_port = &L3HDR(struct icmp, ip)->icmp_id;
			csum = &L3HDR(struct icmp, ip)->icmp_cksum;
			break;
		default:
			panic("ipfw3: unsupported proto %u", ip->ip_p);
		}
		s = RB_FIND(state_tree, tree_out, k);
		if (s == NULL) {
			/* pick an alias ip randomly when there are multiple */
			if (nat->count > 1) {
				rand_n = krandom() % nat->count;
			}
			LIST_FOREACH(alias, &nat->alias, next) {
				if (i++ == rand_n) {
					break;
				}
			}
			switch  (ip->ip_p) {
			case IPPROTO_TCP:
				m->m_pkthdr.csum_flags = CSUM_TCP;
				s = kmalloc(LEN_NAT_STATE, M_IPFW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);

				s->src_addr = args->f_id.src_ip;
				s->src_port = args->f_id.src_port;

				s->dst_addr = args->f_id.dst_ip;
				s->dst_port = args->f_id.dst_port;

				s->alias_addr = alias->ip.s_addr;
				pick_alias_port(s, tree_out);
				dup = RB_INSERT(state_tree, tree_out, s);
				need_return_state = TRUE;
				break;
			case IPPROTO_UDP:
				m->m_pkthdr.csum_flags = CSUM_UDP;
				s = kmalloc(LEN_NAT_STATE, M_IPFW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);

				s->src_addr = args->f_id.src_ip;
				s->src_port = args->f_id.src_port;

				s->dst_addr = args->f_id.dst_ip;
				s->dst_port = args->f_id.dst_port;

				s->alias_addr = alias->ip.s_addr;
				pick_alias_port(s, tree_out);
				dup = RB_INSERT(state_tree, tree_out, s);
				need_return_state = TRUE;
				break;
			case IPPROTO_ICMP:
				s = kmalloc(LEN_NAT_STATE, M_IPFW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);
				s->src_addr = args->f_id.src_ip;
				s->dst_addr = args->f_id.dst_ip;

				s->src_port = *old_port;
				s->dst_port = *old_port;

				s->alias_addr = alias->ip.s_addr;
				s->alias_port = htons(s->src_addr % ALIAS_RANGE);
				dup = RB_INSERT(state_tree, tree_out, s);

				s2 = kmalloc(LEN_NAT_STATE2, M_IPFW3_NAT,
						M_INTWAIT | M_NULLOK | M_ZERO);

				s2->src_addr = args->f_id.dst_ip;
				s2->dst_addr = alias->ip.s_addr;

				s2->src_port = s->alias_port;
				s2->dst_port = s->alias_port;

				s2->alias_addr = htonl(args->f_id.src_ip);
				s2->alias_port = *old_port;

				alias->icmp_in[s->alias_port] = s2;
				break;
			default :
				goto oops;
			}
		}
	}
	if (args->oif == NULL) {
		new_addr.s_addr = s2->src_addr;
		new_port = s2->src_port;
		s2->timestamp = time_uptime;
	} else {
		new_addr.s_addr = s->alias_addr;
		new_port = s->alias_port;
		s->timestamp = time_uptime;
	}

	/* replace src/dst and fix the checksum */
	if (m->m_pkthdr.csum_flags & (CSUM_UDP | CSUM_TCP | CSUM_TSO)) {
		if ((m->m_pkthdr.csum_flags & CSUM_TSO) == 0) {
			dlen = ip->ip_len - (ip->ip_hl << 2);
		}
		pseudo = TRUE;
	}
	if (!pseudo) {
		const uint16_t *oaddr, *naddr;
		oaddr = (const uint16_t *)&old_addr->s_addr;
		naddr = (const uint16_t *)&new_addr.s_addr;
		ip->ip_sum = fix_cksum(ip->ip_sum, oaddr[0], naddr[0], 0);
		ip->ip_sum = fix_cksum(ip->ip_sum, oaddr[1], naddr[1], 0);
		if (ip->ip_p != IPPROTO_ICMP) {
			*csum = fix_cksum(*csum, oaddr[0], naddr[0], udp);
			*csum = fix_cksum(*csum, oaddr[1], naddr[1], udp);
		}
	}
	old_addr->s_addr = new_addr.s_addr;
	if (!pseudo) {
		*csum = fix_cksum(*csum, *old_port, new_port, udp);
	}
	*old_port = new_port;

	if (pseudo) {
		*csum = in_pseudo(ip->ip_src.s_addr,
				ip->ip_dst.s_addr, htons(dlen + ip->ip_p));
	}

	/* prepare the state for return traffic */
	if (need_return_state) {
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);

		m->m_flags &= ~M_HASH;
		ip_hashfn(&m, 0);

		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_off = ntohs(ip->ip_off);

		int nextcpu = netisr_hashcpu(m->m_pkthdr.hash);
		if (nextcpu != mycpuid) {
			struct netmsg_nat_state_add *msg;
			msg = kmalloc(LEN_NMSG_NAT_STATE_ADD,
					M_LWKTMSG, M_NOWAIT | M_ZERO);
			netmsg_init(&msg->base, NULL, &curthread->td_msgport,
					0, nat_state_add_dispatch);
			s2 = kmalloc(LEN_NAT_STATE2, M_IPFW3_NAT,
					M_INTWAIT | M_NULLOK | M_ZERO);

			s2->src_addr = args->f_id.dst_ip;
			s2->src_port = args->f_id.dst_port;

			s2->dst_addr = alias->ip.s_addr;
			s2->dst_port = s->alias_port;

			s2->src_addr = htonl(args->f_id.src_ip);
			s2->src_port = htons(args->f_id.src_port);

			s2->timestamp = s->timestamp;
			msg->alias_addr.s_addr = alias->ip.s_addr;
			msg->alias_port = s->alias_port;
			msg->state = s2;
			msg->nat_id = nat->id;
			msg->proto = ip->ip_p;
			netisr_sendmsg(&msg->base, nextcpu);
		} else {
			s2 = kmalloc(LEN_NAT_STATE2, M_IPFW3_NAT,
					M_INTWAIT | M_NULLOK | M_ZERO);

			s2->src_addr = args->f_id.dst_ip;
			s2->dst_addr = alias->ip.s_addr;

			s2->src_port = s->alias_port;
			s2->dst_port = s->alias_port;

			s2->src_addr = htonl(args->f_id.src_ip);
			s2->src_port = htons(args->f_id.src_port);

			s2->timestamp = s->timestamp;
			if (ip->ip_p == IPPROTO_TCP) {
				alias->tcp_in[s->alias_port - ALIAS_BEGIN] = s2;
			} else {
				alias->udp_in[s->alias_port - ALIAS_BEGIN] = s2;
			}
		}
	}
	return IP_FW_NAT;
oops:
	IPFW3_DEBUG1("oops\n");
	return IP_FW_DENY;
}

void
pick_alias_port(struct nat_state *s, struct state_tree *tree)
{
	do {
		s->alias_port = htons(krandom() % ALIAS_RANGE + ALIAS_BEGIN);
	} while (RB_FIND(state_tree, tree, s) != NULL);
}

int
ip_fw3_nat_state_cmp(struct nat_state *s1, struct nat_state *s2)
{
	if (s1->src_addr > s2->src_addr)
		return 1;
	if (s1->src_addr < s2->src_addr)
		return -1;

	if (s1->dst_addr > s2->dst_addr)
		return 1;
	if (s1->dst_addr < s2->dst_addr)
		return -1;

	if (s1->src_port > s2->src_port)
		return 1;
	if (s1->src_port < s2->src_port)
		return -1;

	if (s1->dst_port > s2->dst_port)
		return 1;
	if (s1->dst_port < s2->dst_port)
		return -1;

	return 0;
}

int
ip_fw3_ctl_nat_get_cfg(struct sockopt *sopt)
{
	struct ip_fw3_nat_context *nat_ctx;
	struct ioc_nat *ioc;
	struct cfg_nat *nat;
	struct cfg_alias *alias;
	struct in_addr *ip;
	size_t valsize;
	int i, len;

	len = 0;
	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	valsize = sopt->sopt_valsize;
	ioc = (struct ioc_nat *)sopt->sopt_val;

	for (i = 0; i < NAT_ID_MAX; i++) {
		nat = nat_ctx->nats[i];
		if (nat != NULL) {
			len += LEN_IOC_NAT;
			if (len >= valsize) {
				goto nospace;
			}
			ioc->id = nat->id;
			ioc->count = nat->count;
			ip = &ioc->ip;
			LIST_FOREACH(alias, &nat->alias, next) {
				len += LEN_IN_ADDR;
				if (len > valsize) {
					goto nospace;
				}
				bcopy(&alias->ip, ip, LEN_IN_ADDR);
				ip++;
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
	struct ip_fw3_nat_context *nat_ctx;
	struct cfg_nat *the;
	size_t sopt_size, total_len = 0;
	struct ioc_nat_state *ioc;
	int ioc_nat_id, i, n, cpu;
	struct nat_state 	*s;
	struct nat_state2 	*s2;
	struct cfg_alias	*a1;

	ioc_nat_id = *((int *)(sopt->sopt_val));
	sopt_size = sopt->sopt_valsize;
	ioc = (struct ioc_nat_state *)sopt->sopt_val;
	/* icmp states only in CPU 0 */
	cpu = 0;
	nat_ctx = ip_fw3_nat_ctx[cpu];
	for (n = 0; n < NAT_ID_MAX; n++) {
		if (ioc_nat_id == 0 || ioc_nat_id == n + 1) {
			if (nat_ctx->nats[n] == NULL)
				break;
			the = nat_ctx->nats[n];
			RB_FOREACH(s, state_tree, &the->rb_icmp_out) {
				total_len += LEN_IOC_NAT_STATE;
				if (total_len > sopt_size)
					goto nospace;
				ioc->src_addr.s_addr = ntohl(s->src_addr);
				ioc->dst_addr.s_addr = s->dst_addr;
				ioc->alias_addr.s_addr = s->alias_addr;
				ioc->src_port = s->src_port;
				ioc->dst_port = s->dst_port;
				ioc->alias_port = s->alias_port;
				ioc->nat_id = n + 1;
				ioc->cpu_id = cpu;
				ioc->proto = IPPROTO_ICMP;
				ioc->direction = 1;
				ioc->life = s->timestamp +
					sysctl_var_icmp_timeout - time_uptime;
				ioc++;
			}

			LIST_FOREACH(a1, &the->alias, next) {
			for (i = 0; i < ALIAS_RANGE; i++) {
				s2 = a1->icmp_in[i];
				if (s2 == NULL) {
					continue;
				}

				total_len += LEN_IOC_NAT_STATE;
				if (total_len > sopt_size)
					goto nospace;

				ioc->src_addr.s_addr = ntohl(s2->src_addr);
				ioc->dst_addr.s_addr = s2->dst_addr;
				ioc->alias_addr.s_addr = s2->alias_addr;
				ioc->src_port = s2->src_port;
				ioc->dst_port = s2->dst_port;
				ioc->alias_port = s2->alias_port;
				ioc->nat_id = n + 1;
				ioc->cpu_id = cpu;
				ioc->proto = IPPROTO_ICMP;
				ioc->direction = 0;
				ioc->life = s2->timestamp +
					sysctl_var_icmp_timeout - time_uptime;
				ioc++;
			}
			}
		}
	}

	/* tcp states */
	for (cpu = 0; cpu < ncpus; cpu++) {
		nat_ctx = ip_fw3_nat_ctx[cpu];
		for (n = 0; n < NAT_ID_MAX; n++) {
			if (ioc_nat_id == 0 || ioc_nat_id == n + 1) {
				if (nat_ctx->nats[n] == NULL)
					break;
				the = nat_ctx->nats[n];
				RB_FOREACH(s, state_tree, &the->rb_tcp_out) {
					total_len += LEN_IOC_NAT_STATE;
					if (total_len > sopt_size)
						goto nospace;
					ioc->src_addr.s_addr = ntohl(s->src_addr);
					ioc->dst_addr.s_addr = ntohl(s->dst_addr);
					ioc->alias_addr.s_addr = s->alias_addr;
					ioc->src_port = ntohs(s->src_port);
					ioc->dst_port = ntohs(s->dst_port);
					ioc->alias_port = s->alias_port;
					ioc->nat_id = n + 1;
					ioc->cpu_id = cpu;
					ioc->proto = IPPROTO_TCP;
					ioc->direction = 1;
					ioc->life = s->timestamp +
						sysctl_var_tcp_timeout - time_uptime;
					ioc++;
				}
				LIST_FOREACH(a1, &the->alias, next) {
					for (i = 0; i < ALIAS_RANGE; i++) {
						s2 = a1->tcp_in[i];
						if (s2 == NULL) {
							continue;
						}

						total_len += LEN_IOC_NAT_STATE;
						if (total_len > sopt_size)
							goto nospace;

						ioc->src_addr.s_addr = ntohl(s2->src_addr);
						ioc->dst_addr.s_addr = s2->dst_addr;
						ioc->alias_addr.s_addr = s2->alias_addr;
						ioc->src_port = s2->src_port;
						ioc->dst_port = s2->dst_port;
						ioc->alias_port = s2->alias_port;
						ioc->nat_id = n + 1;
						ioc->cpu_id = cpu;
						ioc->proto = IPPROTO_TCP;
						ioc->direction = 0;
						ioc->life = s2->timestamp +
							sysctl_var_icmp_timeout - time_uptime;
						ioc++;
					}
				}
			}
		}
	}

	/* udp states */
	for (cpu = 0; cpu < ncpus; cpu++) {
		nat_ctx = ip_fw3_nat_ctx[cpu];
		for (n = 0; n < NAT_ID_MAX; n++) {
			if (ioc_nat_id == 0 || ioc_nat_id == n + 1) {
				if (nat_ctx->nats[n] == NULL)
					break;
				the = nat_ctx->nats[n];
				RB_FOREACH(s, state_tree, &the->rb_udp_out) {
					total_len += LEN_IOC_NAT_STATE;
					if (total_len > sopt_size)
						goto nospace;
					ioc->src_addr.s_addr = ntohl(s->src_addr);
					ioc->dst_addr.s_addr = s->dst_addr;
					ioc->alias_addr.s_addr = s->alias_addr;
					ioc->src_port = s->src_port;
					ioc->dst_port = s->dst_port;
					ioc->alias_port = s->alias_port;
					ioc->nat_id = n + 1;
					ioc->cpu_id = cpu;
					ioc->proto = IPPROTO_UDP;
					ioc->direction = 1;
					ioc->life = s->timestamp +
						sysctl_var_udp_timeout - time_uptime;
					ioc++;
				}
				LIST_FOREACH(a1, &the->alias, next) {
					for (i = 0; i < ALIAS_RANGE; i++) {
						s2 = a1->udp_in[i];
						if (s2 == NULL) {
							continue;
						}

						total_len += LEN_IOC_NAT_STATE;
						if (total_len > sopt_size)
							goto nospace;

						ioc->src_addr.s_addr = ntohl(s2->src_addr);
						ioc->dst_addr.s_addr = s2->dst_addr;
						ioc->alias_addr.s_addr = s2->alias_addr;
						ioc->src_port = s2->src_port;
						ioc->dst_port = s2->dst_port;
						ioc->alias_port = s2->alias_port;
						ioc->nat_id = n + 1;
						ioc->cpu_id = cpu;
						ioc->proto = IPPROTO_UDP;
						ioc->direction = 0;
						ioc->life = s2->timestamp +
							sysctl_var_icmp_timeout - time_uptime;
						ioc++;
					}
				}
			}
		}
	}
	sopt->sopt_valsize = total_len;
	return 0;
nospace:
	return 0;
}

void
nat_state_add_dispatch(netmsg_t add_msg)
{
	struct ip_fw3_nat_context *nat_ctx;
	struct netmsg_nat_state_add *msg;
	struct cfg_nat *nat;
	struct nat_state2 *s2;
	struct cfg_alias *alias;

	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	msg = (struct netmsg_nat_state_add *)add_msg;
	nat = nat_ctx->nats[msg->nat_id - 1];

	LIST_FOREACH(alias, &nat->alias, next) {
		if (alias->ip.s_addr == msg->alias_addr.s_addr) {
			break;
		}
	}
	s2 = msg->state;
	if (msg->proto == IPPROTO_TCP) {
		alias->tcp_in[msg->alias_port - ALIAS_BEGIN] = s2;
	} else {
		alias->udp_in[msg->alias_port - ALIAS_BEGIN] = s2;
	}
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
	struct cfg_alias *alias;
	struct in_addr *ip;
	int n;

	msg = (struct netmsg_nat_add *)nat_add_msg;
	ioc = &msg->ioc_nat;
	nat_ctx = ip_fw3_nat_ctx[mycpuid];

	if (nat_ctx->nats[ioc->id - 1] == NULL) {
		/* op = set, and nat not exists */
		nat = kmalloc(LEN_CFG_NAT, M_IPFW3_NAT, M_WAITOK | M_ZERO);
		LIST_INIT(&nat->alias);
		RB_INIT(&nat->rb_tcp_out);
		RB_INIT(&nat->rb_udp_out);
		if (mycpuid == 0) {
			RB_INIT(&nat->rb_icmp_out);
		}
		nat->id = ioc->id;
		nat->count = ioc->count;
		ip = &ioc->ip;
		for (n = 0; n < ioc->count; n++) {
			alias = kmalloc(LEN_CFG_ALIAS,
					M_IPFW3_NAT, M_WAITOK | M_ZERO);
			memcpy(&alias->ip, ip, LEN_IN_ADDR);
			LIST_INSERT_HEAD((&nat->alias), alias, next);
			ip++;
		}
		nat_ctx->nats[ioc->id - 1] = nat;
	}
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

int
ip_fw3_ctl_nat_add(struct sockopt *sopt)
{
	struct netmsg_nat_add nat_add_msg, *msg;
	struct ioc_nat *ioc;
	msg = &nat_add_msg;

	ioc = (struct ioc_nat *)(sopt->sopt_val);
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
	struct cfg_alias *alias, *tmp3;

	msg = (struct netmsg_nat_del *)nat_del_msg;

	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	nat = nat_ctx->nats[msg->id - 1];
	if (nat != NULL) {
		/* the icmp states will only stored in cpu 0 */
		RB_FOREACH_SAFE(s, state_tree, &nat->rb_icmp_out, tmp) {
			RB_REMOVE(state_tree, &nat->rb_icmp_out, s);
			if (s != NULL) {
				kfree(s, M_IPFW3_NAT);
			}
		}
		/*
		LIST_FOREACH_MUTABLE(s2, &nat->alias->icmp_in, next, tmp2) {
			LIST_REMOVE(s2, next);
			if (s != NULL) {
				kfree(s, M_IPFW3_NAT);
			}
		}
		*/

		RB_FOREACH_SAFE(s, state_tree, &nat->rb_tcp_out, tmp) {
			RB_REMOVE(state_tree, &nat->rb_tcp_out, s);
			if (s != NULL) {
				kfree(s, M_IPFW3_NAT);
			}
		}
		/*
		LIST_FOREACH_MUTABLE(s2, &nat->alias->tcp_in, next, tmp2) {
			LIST_REMOVE(s2, next);
			if (s != NULL) {
				kfree(s, M_IPFW3_NAT);
			}
		}
		*/
		RB_FOREACH_SAFE(s, state_tree, &nat->rb_udp_out, tmp) {
			RB_REMOVE(state_tree, &nat->rb_udp_out, s);
			if (s != NULL) {
				kfree(s, M_IPFW3_NAT);
			}
		}
		/*
		LIST_FOREACH_MUTABLE(s2, &nat->alias->udp_in, next, tmp2) {
			LIST_REMOVE(s2, next);
			if (s != NULL) {
				kfree(s, M_IPFW3_NAT);
			}
		}
		*/
		LIST_FOREACH_MUTABLE(alias, &nat->alias, next, tmp3) {
			kfree(alias, M_IPFW3_NAT);
		}
		kfree(nat, M_IPFW3_NAT);
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
				M_IPFW3_NAT, M_WAITOK | M_ZERO);

	ip_fw3_nat_ctx[mycpuid] = tmp;
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

void
nat_fnit_ctx_dispatch(netmsg_t msg)
{
	kfree(ip_fw3_nat_ctx[mycpuid], M_IPFW3_NAT);
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

static void
nat_cleanup_func_dispatch(netmsg_t nmsg)
{
	struct nat_state *s, *tmp;
	struct ip_fw3_nat_context *nat_ctx;
	struct cfg_nat *nat;
	struct cfg_alias *a1, *tmp2;
	struct nat_state2 *s2;
	int i, j;

	nat_ctx = ip_fw3_nat_ctx[mycpuid];
	for (j = 0; j < NAT_ID_MAX; j++) {
		nat = nat_ctx->nats[j];
		if (nat == NULL)
			continue;
		/* check the nat_states, remove the expired state */
		/* the icmp states will only stored in cpu 0 */
		RB_FOREACH_SAFE(s, state_tree, &nat->rb_icmp_out, tmp) {
			if (time_uptime - s->timestamp > sysctl_var_icmp_timeout) {
				RB_REMOVE(state_tree, &nat->rb_icmp_out, s);
				kfree(s, M_IPFW3_NAT);
			}
		}
		LIST_FOREACH_MUTABLE(a1, &nat->alias, next, tmp2) {
			for (i = 0; i < ALIAS_RANGE; i++) {
				s2 = a1->icmp_in[i];
				if (s2 != NULL) {
					if (time_uptime - s2->timestamp > sysctl_var_icmp_timeout) {
						a1->icmp_in[i] = NULL;
						kfree(s2, M_IPFW3_NAT);
					}
				}

			}
		}

		RB_FOREACH_SAFE(s, state_tree, &nat->rb_tcp_out, tmp) {
			if (time_uptime - s->timestamp > sysctl_var_tcp_timeout) {
				RB_REMOVE(state_tree, &nat->rb_tcp_out, s);
				kfree(s, M_IPFW3_NAT);
			}
		}
		LIST_FOREACH_MUTABLE(a1, &nat->alias, next, tmp2) {
			for (i = 0; i < ALIAS_RANGE; i++) {
				s2 = a1->tcp_in[i];
				if (s2 != NULL) {
					if (time_uptime - s2->timestamp > sysctl_var_icmp_timeout) {
						a1->tcp_in[i] = NULL;
						kfree(s2, M_IPFW3_NAT);
					}
				}

			}
		}
		RB_FOREACH_SAFE(s, state_tree, &nat->rb_udp_out, tmp) {
			if (time_uptime - s->timestamp > sysctl_var_udp_timeout) {
				RB_REMOVE(state_tree, &nat->rb_udp_out, s);
				kfree(s, M_IPFW3_NAT);
			}
		}
		LIST_FOREACH_MUTABLE(a1, &nat->alias, next, tmp2) {
			for (i = 0; i < ALIAS_RANGE; i++) {
				s2 = a1->udp_in[i];
				if (s2 != NULL) {
					if (time_uptime - s2->timestamp > sysctl_var_icmp_timeout) {
						a1->udp_in[i] = NULL;
						kfree(s2, M_IPFW3_NAT);
					}
				}

			}
		}
	}
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

static void
ip_fw3_nat_cleanup_func(void *dummy __unused)
{
	struct netmsg_base msg;
	netmsg_init(&msg, NULL, &curthread->td_msgport, 0,
			nat_cleanup_func_dispatch);
	netisr_domsg(&msg, 0);

	callout_reset(&ip_fw3_nat_cleanup_callout,
			sysctl_var_cleanup_interval * hz,
			ip_fw3_nat_cleanup_func, NULL);
}

static
int ip_fw3_nat_init(void)
{
	struct netmsg_base msg;
	ip_fw3_register_module(MODULE_NAT_ID, MODULE_NAT_NAME);
	ip_fw3_register_filter_funcs(MODULE_NAT_ID, O_NAT_NAT,
			(filter_func)check_nat);
	ip_fw3_ctl_nat_ptr = ip_fw3_ctl_nat_sockopt;
	netmsg_init(&msg, NULL, &curthread->td_msgport,
			0, nat_init_ctx_dispatch);
	netisr_domsg(&msg, 0);

	callout_init_mp(&ip_fw3_nat_cleanup_callout);
	callout_reset(&ip_fw3_nat_cleanup_callout,
			sysctl_var_cleanup_interval * hz,
			ip_fw3_nat_cleanup_func,
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

	return ip_fw3_unregister_module(MODULE_NAT_ID);
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
