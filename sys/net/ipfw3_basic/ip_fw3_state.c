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
#include <sys/in_cksum.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/lock.h>

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
#include <net/ipfw3_basic/ip_fw3_state.h>
#include <net/ipfw3_basic/ip_fw3_table.h>
#include <net/ipfw3_basic/ip_fw3_sync.h>

MALLOC_DEFINE(M_IPFW3_STATE, "M_IPFW3_STATE", "mem for ipfw3 states");


struct ipfw3_state_context 		*fw3_state_ctx[MAXCPU];
extern struct ipfw3_context		*fw3_ctx[MAXCPU];
extern ip_fw_ctl_t 			*ip_fw3_ctl_state_ptr;
extern ipfw_sync_install_state_t 	*ipfw_sync_install_state_prt;

static struct callout 		ip_fw3_state_cleanup_callout;
static int 			sysctl_var_cleanup_interval = 1;

static int 			sysctl_var_state_max_tcp_in = 4096;
static int 			sysctl_var_state_max_udp_in = 4096;
static int 			sysctl_var_state_max_icmp_in = 10;

static int 			sysctl_var_state_max_tcp_out = 4096;
static int 			sysctl_var_state_max_udp_out = 4096;
static int 			sysctl_var_state_max_icmp_out = 10;

static int 			sysctl_var_icmp_timeout = 10;
static int 			sysctl_var_tcp_timeout = 60;
static int 			sysctl_var_udp_timeout = 30;


SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw3_basic, CTLFLAG_RW, 0, "Firewall Basic");

SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, state_max_tcp_in, CTLFLAG_RW,
		&sysctl_var_state_max_tcp_in, 0, "maximum of tcp state in");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, state_max_tcp_out, CTLFLAG_RW,
		&sysctl_var_state_max_tcp_out, 0, "maximum of tcp state out");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, state_max_udp_in, CTLFLAG_RW,
		&sysctl_var_state_max_udp_in, 0, "maximum of udp state in");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, state_max_udp_out, CTLFLAG_RW,
		&sysctl_var_state_max_udp_out, 0, "maximum of udp state out");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, state_max_icmp_in, CTLFLAG_RW,
		&sysctl_var_state_max_icmp_in, 0, "maximum of icmp state in");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, state_max_icmp_out, CTLFLAG_RW,
		&sysctl_var_state_max_icmp_out, 0, "maximum of icmp state out");

SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, cleanup_interval, CTLFLAG_RW,
		&sysctl_var_cleanup_interval, 0,
		"default state expiry check interval");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, icmp_timeout, CTLFLAG_RW,
		&sysctl_var_icmp_timeout, 0, "default icmp state life time");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, tcp_timeout, CTLFLAG_RW,
		&sysctl_var_tcp_timeout, 0, "default tcp state life time");
SYSCTL_INT(_net_inet_ip_fw3_basic, OID_AUTO, udp_timeout, CTLFLAG_RW,
		&sysctl_var_udp_timeout, 0, "default udp state life time");

RB_GENERATE(fw3_state_tree, ipfw3_state, entries, ip_fw3_state_cmp);


int
ip_fw3_state_cmp(struct ipfw3_state *s1, struct ipfw3_state *s2)
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

void
check_check_state(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	/* state_tree 1 same direction, state_tree2 opposite direction */
	struct fw3_state_tree *state_tree1, *state_tree2;
	struct ip *ip = mtod((*args)->m, struct ip *);
	struct ipfw3_state_context *state_ctx = fw3_state_ctx[mycpuid];
	struct ipfw3_state *s, *k, key;

	k = &key;
	memset(k, 0, LEN_FW3_STATE);

	if ((*args)->oif == NULL) {
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			state_tree1 = &state_ctx->rb_tcp_in;
			state_tree2 = &state_ctx->rb_tcp_out;
		break;
		case IPPROTO_UDP:
			state_tree1 = &state_ctx->rb_udp_in;
			state_tree2 = &state_ctx->rb_udp_out;
		break;
		case IPPROTO_ICMP:
			state_tree1 = &state_ctx->rb_icmp_in;
			state_tree2 = &state_ctx->rb_icmp_out;
		break;
		default:
			goto oops;
		}
	} else {
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			state_tree1 = &state_ctx->rb_tcp_out;
			state_tree2 = &state_ctx->rb_tcp_in;
		break;
		case IPPROTO_UDP:
			state_tree1 = &state_ctx->rb_udp_out;
			state_tree2 = &state_ctx->rb_udp_in;
		break;
		case IPPROTO_ICMP:
			state_tree1 = &state_ctx->rb_icmp_out;
			state_tree2 = &state_ctx->rb_icmp_in;
		break;
		default:
			goto oops;
		}
	}

	k->src_addr = (*args)->f_id.src_ip;
	k->dst_addr = (*args)->f_id.dst_ip;
	k->src_port = (*args)->f_id.src_port;
	k->dst_port = (*args)->f_id.dst_port;
	s = RB_FIND(fw3_state_tree, state_tree1, k);
	if (s != NULL) {
		(*f)->pcnt++;
		(*f)->bcnt += ip_len;
		(*f)->timestamp = time_second;
		*f = s->stub;
		s->timestamp = time_uptime;
		*cmd_val = IP_FW_PASS;
		*cmd_ctl = IP_FW_CTL_CHK_STATE;
		return;
	}
	k->dst_addr = (*args)->f_id.src_ip;
	k->src_addr = (*args)->f_id.dst_ip;
	k->dst_port = (*args)->f_id.src_port;
	k->src_port = (*args)->f_id.dst_port;
	s = RB_FIND(fw3_state_tree, state_tree2, k);
	if (s != NULL) {
		(*f)->pcnt++;
		(*f)->bcnt += ip_len;
		(*f)->timestamp = time_second;
		*f = s->stub;
		s->timestamp = time_uptime;
		*cmd_val = IP_FW_PASS;
		*cmd_ctl = IP_FW_CTL_CHK_STATE;
		return;
	}
oops:
	*cmd_val = IP_FW_NOT_MATCH;
	*cmd_ctl = IP_FW_CTL_NEXT;
}

void
check_keep_state(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	/* state_tree 1 same direction, state_tree2 opposite direction */
	struct fw3_state_tree *the_tree = NULL;
	struct ip *ip = mtod((*args)->m, struct ip *);
	struct ipfw3_state_context *state_ctx = fw3_state_ctx[mycpuid];
	struct ipfw3_state *s, *k, key;
	int states_matched = 0, *the_count, the_max;

	k = &key;
	memset(k, 0, LEN_FW3_STATE);
	if ((*args)->oif == NULL) {
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			the_tree = &state_ctx->rb_tcp_in;
			the_count = &state_ctx->count_tcp_in;
			the_max = sysctl_var_state_max_tcp_in;
		break;
		case IPPROTO_UDP:
			the_tree = &state_ctx->rb_udp_in;
			the_count = &state_ctx->count_udp_in;
			the_max = sysctl_var_state_max_udp_in;
		break;
		case IPPROTO_ICMP:
			the_tree = &state_ctx->rb_icmp_in;
			the_count = &state_ctx->count_icmp_in;
			the_max = sysctl_var_state_max_icmp_in;
		break;
		default:
			goto done;
		}
	} else {
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			the_tree = &state_ctx->rb_tcp_out;
			the_count = &state_ctx->count_tcp_out;
			the_max = sysctl_var_state_max_tcp_out;
		break;
		case IPPROTO_UDP:
			the_tree = &state_ctx->rb_udp_out;
			the_count = &state_ctx->count_udp_out;
			the_max = sysctl_var_state_max_udp_out;
		break;
		case IPPROTO_ICMP:
			the_tree = &state_ctx->rb_icmp_out;
			the_count = &state_ctx->count_icmp_out;
			the_max = sysctl_var_state_max_icmp_out;
		break;
		default:
			goto done;
		}
	}
	*cmd_ctl = IP_FW_CTL_NO;
	k->src_addr = (*args)->f_id.src_ip;
	k->dst_addr = (*args)->f_id.dst_ip;
	k->src_port = (*args)->f_id.src_port;
	k->dst_port = (*args)->f_id.dst_port;
	/* cmd->arg3 is `limit type` */
	if (cmd->arg3 == 0) {
		s = RB_FIND(fw3_state_tree, the_tree, k);
		if (s != NULL) {
			goto done;
		}
	} else {
		RB_FOREACH(s, fw3_state_tree, the_tree) {
			if (cmd->arg3 == 1 && s->src_addr == k->src_addr) {
				states_matched++;
			} else if (cmd->arg3 == 2 && s->src_port == k->src_port) {
				states_matched++;
			} else if (cmd->arg3 == 3 && s->dst_addr == k->dst_addr) {
				states_matched++;
			} else if (cmd->arg3 == 4 && s->dst_port == k->dst_port) {
				states_matched++;
			}
		}
		if (states_matched >= cmd->arg1) {
			goto done;
		}
	}
	if (*the_count <= the_max) {
		(*the_count)++;
		s = kmalloc(LEN_FW3_STATE, M_IPFW3_STATE,
				M_INTWAIT | M_NULLOK | M_ZERO);
		s->src_addr = k->src_addr;
		s->dst_addr = k->dst_addr;
		s->src_port = k->src_port;
		s->dst_port = k->dst_port;
		s->stub = *f;
		s->timestamp = time_uptime;
		if (RB_INSERT(fw3_state_tree, the_tree, s)) {
			kprintf("oops\n");
		}
	}
done:
	*cmd_ctl = IP_FW_CTL_NO;
	*cmd_val = IP_FW_MATCH;
}

void
ip_fw3_state_append_dispatch(netmsg_t nmsg)
{
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

void
ip_fw3_state_delete_dispatch(netmsg_t nmsg)
{
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

int
ip_fw3_ctl_state_add(struct sockopt *sopt)
{
	return 0;
}

int
ip_fw3_ctl_state_delete(struct sockopt *sopt)
{
	return 0;
}

void
ip_fw3_state_flush_dispatch(netmsg_t nmsg)
{
	struct ipfw3_state_context *state_ctx = fw3_state_ctx[mycpuid];
	struct ipfw3_state *s, *tmp;

	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_icmp_in, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_icmp_in, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_icmp_out, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_icmp_out, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_tcp_in, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_tcp_in, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_tcp_out, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_tcp_out, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_udp_in, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_udp_in, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_udp_out, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_udp_out, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

void
ip_fw3_state_flush(struct ip_fw *rule)
{
	struct netmsg_base msg;
	netmsg_init(&msg, NULL, &curthread->td_msgport, 0,
			ip_fw3_state_flush_dispatch);
	netisr_domsg(&msg, 0);
}

int
ip_fw3_ctl_state_flush(struct sockopt *sopt)
{

	return 0;
}

int
ip_fw3_ctl_state_get(struct sockopt *sopt)
{
	struct ipfw3_state_context *state_ctx;
	struct ipfw3_state *s;

	size_t sopt_size, total_len = 0;
	struct ipfw3_ioc_state *ioc;
	int ioc_rule_id;

	ioc_rule_id = *((int *)(sopt->sopt_val));
	sopt_size = sopt->sopt_valsize;
	ioc = (struct ipfw3_ioc_state *)sopt->sopt_val;
	/* icmp states only in CPU 0 */
	int cpu = 0;

	/* icmp states */
	for (cpu = 0; cpu < ncpus; cpu++) {
		state_ctx = fw3_state_ctx[cpu];
		RB_FOREACH(s, fw3_state_tree, &state_ctx->rb_icmp_in) {
			total_len += LEN_IOC_FW3_STATE;
			if (total_len > sopt_size)
				goto nospace;
			ioc->src_addr.s_addr = ntohl(s->src_addr);
			ioc->dst_addr.s_addr = ntohl(s->dst_addr);
			ioc->src_port = ntohs(s->src_port);
			ioc->dst_port = ntohs(s->dst_port);
			ioc->cpu_id = cpu;
			ioc->rule_id = s->stub->rulenum;
			ioc->proto = IPPROTO_ICMP;
			ioc->life = s->timestamp +
				sysctl_var_udp_timeout - time_uptime;
			ioc++;
		}
		RB_FOREACH(s, fw3_state_tree, &state_ctx->rb_icmp_out) {
			total_len += LEN_IOC_FW3_STATE;
			if (total_len > sopt_size)
				goto nospace;
			ioc->src_addr.s_addr = ntohl(s->src_addr);
			ioc->dst_addr.s_addr = ntohl(s->dst_addr);
			ioc->src_port = ntohs(s->src_port);
			ioc->dst_port = ntohs(s->dst_port);
			ioc->cpu_id = cpu;
			ioc->rule_id = s->stub->rulenum;
			ioc->proto = IPPROTO_ICMP;
			ioc->life = s->timestamp +
				sysctl_var_udp_timeout - time_uptime;
			ioc++;
		}
		RB_FOREACH(s, fw3_state_tree, &state_ctx->rb_tcp_in) {
			total_len += LEN_IOC_FW3_STATE;
			if (total_len > sopt_size)
				goto nospace;
			ioc->src_addr.s_addr = ntohl(s->src_addr);
			ioc->dst_addr.s_addr = ntohl(s->dst_addr);
			ioc->src_port = ntohs(s->src_port);
			ioc->dst_port = ntohs(s->dst_port);
			ioc->cpu_id = cpu;
			ioc->rule_id = s->stub->rulenum;
			ioc->proto = IPPROTO_TCP;
			ioc->life = s->timestamp +
				sysctl_var_udp_timeout - time_uptime;
			ioc++;
		}
		RB_FOREACH(s, fw3_state_tree, &state_ctx->rb_tcp_out) {
			total_len += LEN_IOC_FW3_STATE;
			if (total_len > sopt_size)
				goto nospace;
			ioc->src_addr.s_addr = ntohl(s->src_addr);
			ioc->dst_addr.s_addr = ntohl(s->dst_addr);
			ioc->src_port = ntohs(s->src_port);
			ioc->dst_port = ntohs(s->dst_port);
			ioc->cpu_id = cpu;
			ioc->rule_id = s->stub->rulenum;
			ioc->proto = IPPROTO_TCP;
			ioc->life = s->timestamp +
				sysctl_var_udp_timeout - time_uptime;
			ioc++;
		}
		RB_FOREACH(s, fw3_state_tree, &state_ctx->rb_udp_in) {
			total_len += LEN_IOC_FW3_STATE;
			if (total_len > sopt_size)
				goto nospace;
			ioc->src_addr.s_addr = ntohl(s->src_addr);
			ioc->dst_addr.s_addr = ntohl(s->dst_addr);
			ioc->src_port = ntohs(s->src_port);
			ioc->dst_port = ntohs(s->dst_port);
			ioc->cpu_id = cpu;
			ioc->rule_id = s->stub->rulenum;
			ioc->proto = IPPROTO_UDP;
			ioc->life = s->timestamp +
				sysctl_var_udp_timeout - time_uptime;
			ioc++;
		}
		RB_FOREACH(s, fw3_state_tree, &state_ctx->rb_udp_out) {
			total_len += LEN_IOC_FW3_STATE;
			if (total_len > sopt_size)
				goto nospace;
			ioc->src_addr.s_addr = ntohl(s->src_addr);
			ioc->dst_addr.s_addr = ntohl(s->dst_addr);
			ioc->src_port = ntohs(s->src_port);
			ioc->dst_port = ntohs(s->dst_port);
			ioc->cpu_id = cpu;
			ioc->rule_id = s->stub->rulenum;
			ioc->proto = IPPROTO_UDP;
			ioc->life = s->timestamp +
				sysctl_var_udp_timeout - time_uptime;
			ioc++;
		}
	}

	sopt->sopt_valsize = total_len;
	return 0;
nospace:
	return 0;
}

void
ip_fw3_state_cleanup_dispatch(netmsg_t nmsg)
{

	struct ipfw3_state_context *state_ctx = fw3_state_ctx[mycpuid];
	struct ipfw3_state *s, *tmp;

	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_icmp_in, tmp) {
		if (time_uptime - s->timestamp > sysctl_var_icmp_timeout) {
			RB_REMOVE(fw3_state_tree, &state_ctx->rb_icmp_in, s);
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_icmp_out, tmp) {
		if (time_uptime - s->timestamp > sysctl_var_icmp_timeout) {
			RB_REMOVE(fw3_state_tree, &state_ctx->rb_icmp_out, s);
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_tcp_in, tmp) {
		if (time_uptime - s->timestamp > sysctl_var_tcp_timeout) {
			RB_REMOVE(fw3_state_tree, &state_ctx->rb_tcp_in, s);
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_tcp_out, tmp) {
		if (time_uptime - s->timestamp > sysctl_var_tcp_timeout) {
			RB_REMOVE(fw3_state_tree, &state_ctx->rb_tcp_out, s);
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_udp_in, tmp) {
		if (time_uptime - s->timestamp > sysctl_var_udp_timeout) {
			RB_REMOVE(fw3_state_tree, &state_ctx->rb_udp_in, s);
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_udp_out, tmp) {
		if (time_uptime - s->timestamp > sysctl_var_udp_timeout) {
			RB_REMOVE(fw3_state_tree, &state_ctx->rb_udp_out, s);
			kfree(s, M_IPFW3_STATE);
		}
	}
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

void
ip_fw3_state_cleanup(void *dummy __unused)
{
	struct netmsg_base msg;
	netmsg_init(&msg, NULL, &curthread->td_msgport, 0,
			ip_fw3_state_cleanup_dispatch);
	netisr_domsg(&msg, 0);

	callout_reset(&ip_fw3_state_cleanup_callout,
			sysctl_var_cleanup_interval * hz,
			ip_fw3_state_cleanup, NULL);
}

int
ip_fw3_ctl_state_sockopt(struct sockopt *sopt)
{
	int error = 0;
	switch (sopt->sopt_name) {
		case IP_FW_STATE_ADD:
			error = ip_fw3_ctl_state_add(sopt);
			break;
		case IP_FW_STATE_DEL:
			error = ip_fw3_ctl_state_delete(sopt);
			break;
		case IP_FW_STATE_FLUSH:
			error = ip_fw3_ctl_state_flush(sopt);
			break;
		case IP_FW_STATE_GET:
			error = ip_fw3_ctl_state_get(sopt);
			break;
	}
	return error;
}

void
ip_fw3_state_init_dispatch(netmsg_t msg)
{
	struct ipfw3_state_context *tmp;

	tmp = kmalloc(LEN_STATE_CTX, M_IPFW3_STATE, M_WAITOK | M_ZERO);
	RB_INIT(&tmp->rb_icmp_in);
	RB_INIT(&tmp->rb_icmp_out);
	RB_INIT(&tmp->rb_tcp_in);
	RB_INIT(&tmp->rb_tcp_out);
	RB_INIT(&tmp->rb_udp_in);
	RB_INIT(&tmp->rb_udp_out);
	fw3_state_ctx[mycpuid] = tmp;
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}

void
ip_fw3_state_fini_dispatch(netmsg_t msg)
{
	struct ipfw3_state_context *state_ctx = fw3_state_ctx[mycpuid];
	struct ipfw3_state *s, *tmp;

	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_icmp_in, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_icmp_in, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_icmp_out, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_icmp_out, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_tcp_in, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_tcp_in, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_tcp_out, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_tcp_out, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_udp_in, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_udp_in, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	RB_FOREACH_SAFE(s, fw3_state_tree, &state_ctx->rb_udp_out, tmp) {
		RB_REMOVE(fw3_state_tree, &state_ctx->rb_udp_out, s);
		if (s != NULL) {
			kfree(s, M_IPFW3_STATE);
		}
	}
	kfree(fw3_state_ctx[mycpuid], M_IPFW3_STATE);
	fw3_state_ctx[mycpuid] = NULL;
	netisr_forwardmsg_all(&msg->base, mycpuid + 1);
}


void
ip_fw3_state_fini(void)
{
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		0, ip_fw3_state_fini_dispatch);

	netisr_domsg(&msg, 0);
	callout_stop(&ip_fw3_state_cleanup_callout);
}

void
ip_fw3_state_init(void)
{
	struct netmsg_base msg;

	ip_fw3_ctl_state_ptr = ip_fw3_ctl_state_sockopt;
	callout_init_mp(&ip_fw3_state_cleanup_callout);
	callout_reset(&ip_fw3_state_cleanup_callout,
			sysctl_var_cleanup_interval * hz,
			ip_fw3_state_cleanup,
			NULL);
	netmsg_init(&msg, NULL, &curthread->td_msgport,
			0, ip_fw3_state_init_dispatch);
	netisr_domsg(&msg, 0);
}


void
ip_fw3_state_modevent(int type)
{
	switch (type) {
		case MOD_LOAD:
			ip_fw3_state_init();
			break;
		case MOD_UNLOAD:
			ip_fw3_state_fini();
			break;
	}
}

