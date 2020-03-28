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
#include <sys/tree.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <net/route.h>

#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip_divert.h>
#include <netinet/if_ether.h>

#include <net/ipfw3/ip_fw.h>
#include <net/ipfw3_basic/ip_fw3_table.h>
#include <net/ipfw3_basic/ip_fw3_sync.h>
#include <net/ipfw3_basic/ip_fw3_basic.h>
#include <net/ipfw3_basic/ip_fw3_state.h>

extern struct ipfw3_context		*fw3_ctx[MAXCPU];
extern struct ipfw3_sync_context 	fw3_sync_ctx;
extern struct ipfw3_state_context 	*fw3_state_ctx[MAXCPU];
extern ip_fw_ctl_t 			*ipfw_ctl_basic_ptr;

extern int 				sysctl_var_fw3_verbose;

extern int 			sysctl_var_state_max_tcp_in;
extern int 			sysctl_var_state_max_udp_in;
extern int 			sysctl_var_state_max_icmp_in;

extern int 			sysctl_var_state_max_tcp_out;
extern int 			sysctl_var_state_max_udp_out;
extern int 			sysctl_var_state_max_icmp_out;

extern int 			sysctl_var_icmp_timeout;
extern int 			sysctl_var_tcp_timeout;
extern int 			sysctl_var_udp_timeout;

static struct ip_fw *
lookup_next_rule(struct ip_fw *me)
{
	struct ip_fw *rule = NULL;
	ipfw_insn *cmd;

	/* look for action, in case it is a skipto */
	cmd = ACTION_PTR(me);
	if ((int)cmd->module == MODULE_BASIC_ID &&
		(int)cmd->opcode == O_BASIC_SKIPTO) {
		for (rule = me->next; rule; rule = rule->next) {
			if (rule->rulenum >= cmd->arg1)
				break;
		}
	}
	if (rule == NULL) /* failure or not a skipto */
		rule = me->next;

	me->next_rule = rule;
	return rule;
}


static int
iface_match(struct ifnet *ifp, ipfw_insn_if *cmd)
{
	if (ifp == NULL)	/* no iface with this packet, match fails */
		return 0;

	/* Check by name or by IP address */
	if (cmd->name[0] != '\0') { /* match by name */
		/* Check name */
		if (cmd->p.glob) {
			if (kfnmatch(cmd->name, ifp->if_xname, 0) == 0)
				return(1);
		} else {
			if (strncmp(ifp->if_xname, cmd->name, IFNAMSIZ) == 0)
				return(1);
		}
	} else {
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ia = ifac->ifa;

			if (ia->ifa_addr == NULL)
				continue;
			if (ia->ifa_addr->sa_family != AF_INET)
				continue;
			if (cmd->p.ip.s_addr ==
				((struct sockaddr_in *)
				(ia->ifa_addr))->sin_addr.s_addr)
					return(1);	/* match */

		}
	}
	return 0;	/* no match, fail ... */
}

/* implimentation of the checker functions */
void
check_count(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	(*f)->pcnt++;
	(*f)->bcnt += ip_len;
	(*f)->timestamp = time_second;
	*cmd_ctl = IP_FW_CTL_NEXT;
}

void
check_skipto(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	(*f)->pcnt++;
	(*f)->bcnt += ip_len;
	(*f)->timestamp = time_second;
	if ((*f)->next_rule == NULL)
		lookup_next_rule(*f);
	*f = (*f)->next_rule;
	*cmd_ctl = IP_FW_CTL_AGAIN;
}

void
check_forward(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct sockaddr_in *sin, *sa;
	struct m_tag *mtag;

	if ((*args)->eh) {	/* not valid on layer2 pkts */
		*cmd_ctl=IP_FW_CTL_NEXT;
		return;
	}

	(*f)->pcnt++;
	(*f)->bcnt += ip_len;
	(*f)->timestamp = time_second;
	if ((*f)->next_rule == NULL)
		lookup_next_rule(*f);

	mtag = m_tag_get(PACKET_TAG_IPFORWARD,
			sizeof(*sin), M_INTWAIT | M_NULLOK);
	if (mtag == NULL) {
		*cmd_val = IP_FW_DENY;
		*cmd_ctl = IP_FW_CTL_DONE;
		return;
	}
	sin = m_tag_data(mtag);
	sa = &((ipfw_insn_sa *)cmd)->sa;
	/* arg3: count of the dest, arg1: type of fwd */
	int i = 0;
	if(cmd->arg3 > 1) {
		if (cmd->arg1 == 0) {		/* type: random */
			i = krandom() % cmd->arg3;
		} else if (cmd->arg1 == 1) {	/* type: round-robin */
			i = cmd->arg2++ % cmd->arg3;
		} else if (cmd->arg1 == 2) {	/* type: sticky */
			struct ip *ip = mtod((*args)->m, struct ip *);
			i = ip->ip_src.s_addr & (cmd->arg3 - 1);
		}
		sa += i;
	}
	*sin = *sa;	/* apply the destination */
	m_tag_prepend((*args)->m, mtag);
	(*args)->m->m_pkthdr.fw_flags |= IPFORWARD_MBUF_TAGGED;
	(*args)->m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
	*cmd_ctl = IP_FW_CTL_DONE;
	*cmd_val = IP_FW_PASS;
}

void
check_in(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = ((*args)->oif == NULL);
}

void
check_out(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = ((*args)->oif != NULL);
}

void
check_via(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = iface_match((*args)->oif ?
			(*args)->oif : (*args)->m->m_pkthdr.rcvif,
			(ipfw_insn_if *)cmd);
}

void
check_proto(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = ((*args)->f_id.proto == cmd->arg1);
}

void
check_prob(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = (krandom() % 100) < cmd->arg1;
}

void
check_from(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr src_ip = ip->ip_src;

	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}
	*cmd_val = (hlen > 0 &&
			((ipfw_insn_ip *)cmd)->addr.s_addr == src_ip.s_addr);
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_from_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ipfw3_table_context *table_ctx;
	struct radix_node_head *rnh;
	struct sockaddr_in sa;

	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr src_ip = ip->ip_src;

	*cmd_val = IP_FW_NOT_MATCH;

	table_ctx = ctx->table_ctx;
	table_ctx += cmd->arg1;

        if (table_ctx->type != 0) {
                rnh = table_ctx->node;
                sa.sin_len = 8;
                sa.sin_addr.s_addr = src_ip.s_addr;
                if(rnh->rnh_lookup((char *)&sa, NULL, rnh) != NULL)
                        *cmd_val = IP_FW_MATCH;
        }
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_from_me(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr src_ip = ip->ip_src;

	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}
	*cmd_ctl = IP_FW_CTL_NO;
	if (hlen > 0) {
		struct ifnet *tif;
		tif = INADDR_TO_IFP(&src_ip);
		*cmd_val = (tif != NULL);
	} else {
		*cmd_val = IP_FW_NOT_MATCH;
	}
}

void
check_from_mask(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr src_ip = ip->ip_src;

	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}

	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = (hlen > 0 &&
			((ipfw_insn_ip *)cmd)->addr.s_addr ==
			(src_ip.s_addr &
			((ipfw_insn_ip *)cmd)->mask.s_addr));
}

void
check_to(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr dst_ip = ip->ip_dst;

	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		 ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}
	*cmd_val = (hlen > 0 &&
			((ipfw_insn_ip *)cmd)->addr.s_addr == dst_ip.s_addr);
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_to_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ipfw3_table_context *table_ctx;
	struct radix_node_head *rnh;
	struct sockaddr_in sa;

	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr dst_ip = ip->ip_dst;

	*cmd_val = IP_FW_NOT_MATCH;

	table_ctx = ctx->table_ctx;
	table_ctx += cmd->arg1;

        if (table_ctx->type != 0) {
                rnh = table_ctx->node;
                sa.sin_len = 8;
                sa.sin_addr.s_addr = dst_ip.s_addr;
                if(rnh->rnh_lookup((char *)&sa, NULL, rnh) != NULL)
                        *cmd_val = IP_FW_MATCH;
        }
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_to_me(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr dst_ip = ip->ip_dst;

	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}
	*cmd_ctl = IP_FW_CTL_NO;
	if (hlen > 0) {
		struct ifnet *tif;
		tif = INADDR_TO_IFP(&dst_ip);
		*cmd_val = (tif != NULL);
	} else {
		*cmd_val = IP_FW_NOT_MATCH;
	}
}

void
check_to_mask(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	struct in_addr dst_ip = ip->ip_dst;

	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}

	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = (hlen > 0 &&
			((ipfw_insn_ip *)cmd)->addr.s_addr ==
			(dst_ip.s_addr &
			((ipfw_insn_ip *)cmd)->mask.s_addr));
}

void
check_tag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct m_tag *mtag = m_tag_locate((*args)->m,
			MTAG_IPFW, cmd->arg1, NULL);
	if (mtag == NULL) {
		mtag = m_tag_alloc(MTAG_IPFW,cmd->arg1, 0, M_NOWAIT);
		if (mtag != NULL)
			m_tag_prepend((*args)->m, mtag);

	}
	(*f)->pcnt++;
	(*f)->bcnt += ip_len;
	(*f)->timestamp = time_second;
	*cmd_ctl = IP_FW_CTL_NEXT;
}

void
check_untag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct m_tag *mtag = m_tag_locate((*args)->m,
			MTAG_IPFW, cmd->arg1, NULL);
	if (mtag != NULL)
		m_tag_delete((*args)->m, mtag);

	(*f)->pcnt++;
	(*f)->bcnt += ip_len;
	(*f)->timestamp = time_second;
	*cmd_ctl = IP_FW_CTL_NEXT;
}

void
check_tagged(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	if (m_tag_locate( (*args)->m, MTAG_IPFW,cmd->arg1, NULL) != NULL )
		*cmd_val = IP_FW_MATCH;
	else
		*cmd_val = IP_FW_NOT_MATCH;
}

void
check_src_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
        struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
        *cmd_ctl = IP_FW_CTL_NO;
        if ((*args)->f_id.src_port == cmd->arg1)
                *cmd_val = IP_FW_MATCH;
        else
                *cmd_val = IP_FW_NOT_MATCH;
}

void
check_dst_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
        struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
        *cmd_ctl = IP_FW_CTL_NO;
        if ((*args)->f_id.dst_port == cmd->arg1)
                *cmd_val = IP_FW_MATCH;
        else
                *cmd_val = IP_FW_NOT_MATCH;
}

void
check_src_n_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct in_addr src_ip;
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	src_ip = ip->ip_src;
	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}
	*cmd_val = (hlen > 0 && ((ipfw_insn_ip *)cmd)->addr.s_addr == src_ip.s_addr);
	*cmd_ctl = IP_FW_CTL_NO;
	if (*cmd_val && (*args)->f_id.src_port == cmd->arg1)
		*cmd_val = IP_FW_MATCH;
	else
		*cmd_val = IP_FW_NOT_MATCH;
}

void
check_dst_n_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct in_addr dst_ip;
	u_int hlen = 0;
	struct mbuf *m = (*args)->m;
	struct ip *ip = mtod(m, struct ip *);
	dst_ip = ip->ip_dst;
	if ((*args)->eh == NULL ||
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		 ntohs((*args)->eh->ether_type) == ETHERTYPE_IP)) {
		hlen = ip->ip_hl << 2;
	}
	*cmd_val = (hlen > 0 && ((ipfw_insn_ip *)cmd)->addr.s_addr == dst_ip.s_addr);
	*cmd_ctl = IP_FW_CTL_NO;
	if (*cmd_val && (*args)->f_id.dst_port == cmd->arg1)
		*cmd_val = IP_FW_MATCH;
	else
		*cmd_val = IP_FW_NOT_MATCH;
}

int
ip_fw3_basic_init(void)
{
	ip_fw3_register_module(MODULE_BASIC_ID, MODULE_BASIC_NAME);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID, O_BASIC_COUNT,
			(filter_func)check_count);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID, O_BASIC_SKIPTO,
			(filter_func)check_skipto);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID, O_BASIC_FORWARD,
			(filter_func)check_forward);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID, O_BASIC_KEEP_STATE,
			(filter_func)check_keep_state);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID, O_BASIC_CHECK_STATE,
			(filter_func)check_check_state);

	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IN, (filter_func)check_in);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_OUT, (filter_func)check_out);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_VIA, (filter_func)check_via);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_XMIT, (filter_func)check_via);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_RECV, (filter_func)check_via);

	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_PROTO, (filter_func)check_proto);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_PROB, (filter_func)check_prob);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRC, (filter_func)check_from);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRC_LOOKUP, (filter_func)check_from_lookup);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRC_ME, (filter_func)check_from_me);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRC_MASK, (filter_func)check_from_mask);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DST, (filter_func)check_to);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DST_LOOKUP, (filter_func)check_to_lookup);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DST_ME, (filter_func)check_to_me);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DST_MASK, (filter_func)check_to_mask);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_TAG, (filter_func)check_tag);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_UNTAG, (filter_func)check_untag);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_TAGGED, (filter_func)check_tagged);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRCPORT, (filter_func)check_src_port);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DSTPORT, (filter_func)check_dst_port);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRC_N_PORT, (filter_func)check_src_n_port);
	ip_fw3_register_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DST_N_PORT, (filter_func)check_dst_n_port);

	return 0;
}

int
ip_fw3_basic_fini(void)
{
	return ip_fw3_unregister_module(MODULE_BASIC_ID);
}

static int
ipfw3_basic_modevent(module_t mod, int type, void *data)
{
	int err;
	switch (type) {
		case MOD_LOAD:
			err = ip_fw3_basic_init();
			break;
		case MOD_UNLOAD:
			err = ip_fw3_basic_fini();
			break;
		default:
			err = 1;
	}
	ip_fw3_state_modevent(type);
	return err;
}

static moduledata_t ipfw3_basic_mod = {
	"ipfw3_basic",
	ipfw3_basic_modevent,
	NULL
};
DECLARE_MODULE(ipfw3_basic, ipfw3_basic_mod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_DEPEND(ipfw3_basic, ipfw3, 1, 1, 1);
MODULE_VERSION(ipfw3_basic, 1);
