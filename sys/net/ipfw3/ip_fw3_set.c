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

#include "ip_fw3.h"
#include "ip_fw3_set.h"


extern struct ipfw3_context 		*fw3_ctx[MAXCPU];

void
move_set_dispatch(netmsg_t smsg)
{
	struct netmsg_sets *msg = (struct netmsg_sets *)smsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule;
	for (rule = ctx->rules; rule; rule = rule->next) {
		if (rule->set == msg->set_from)
			rule->set = msg->set_to;
	}
	netisr_forwardmsg_all(&smsg->base, mycpuid + 1);
}

int
ip_fw3_ctl_set_move_set(struct sockopt *sopt)
{
	struct ipfw3_context *ctx;
	struct netmsg_sets msg;
	int *set1, *set2;

	ctx = fw3_ctx[mycpuid];
	set1 =(int *)sopt->sopt_val;
	set2 = set1 + 1;
	if (*set1 > 0 && *set1 < 32 && *set2 > 0 && *set2 < 32) {
		bzero(&msg, sizeof(msg));
		netmsg_init(&msg.base, NULL, &curthread->td_msgport,
				0, move_set_dispatch);
		msg.set_from = *set1;
		msg.set_to = *set2;

		netisr_domsg(&msg.base, 0);
	}
	return 0;
}

void
move_rule_dispatch(netmsg_t smsg)
{
	struct netmsg_sets *msg = (struct netmsg_sets *)smsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule;
	for (rule = ctx->rules; rule; rule = rule->next) {
		if (rule->rulenum == msg->rule_num)
			rule->set = msg->set_to;
	}
	netisr_forwardmsg_all(&smsg->base, mycpuid + 1);
}

int
ip_fw3_ctl_set_move_rule(struct sockopt *sopt)
{
	struct ipfw3_context *ctx;
	struct netmsg_sets msg;
	int *rule_num, *set;

	ctx = fw3_ctx[mycpuid];
	rule_num =(int *)sopt->sopt_val;
	set = rule_num + 1;
	if (*rule_num > 0 && *rule_num < 65535 && *set >= 0 && *set < 32) {
		bzero(&msg, sizeof(msg));
		netmsg_init(&msg.base, NULL, &curthread->td_msgport,
				0, move_rule_dispatch);
		msg.rule_num = *rule_num;
		msg.set_to = *set;
		netisr_domsg(&msg.base, 0);
	}
	return 0;
}

void
set_swap_dispatch(netmsg_t smsg)
{
	struct netmsg_sets *msg = (struct netmsg_sets *)smsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule;

	for (rule = ctx->rules; rule; rule = rule->next) {
		if (rule->set == msg->set_from)
			rule->set = msg->set_to;
		else if (rule->set == msg->set_to)
			rule->set = msg->set_from;
	}
	netisr_forwardmsg_all(&smsg->base, mycpuid + 1);
}

int
ip_fw3_ctl_set_swap(struct sockopt *sopt)
{
	struct ipfw3_context *ctx;
	struct netmsg_sets msg;
	int *set1, *set2;

	ctx = fw3_ctx[mycpuid];
	set1 =(int *)sopt->sopt_val;
	set2 = set1 + 1;
	if (*set1 > 0 && *set1 < 32 && *set2 > 0 && *set2 < 32) {
		bzero(&msg, sizeof(msg));
		netmsg_init(&msg.base, NULL, &curthread->td_msgport,
				0, set_swap_dispatch);
		msg.set_from = *set1;
		msg.set_to = *set2;

		netisr_domsg(&msg.base, 0);
	}
	return 0;
}

int
ip_fw3_ctl_set_toggle(struct sockopt *sopt)
{
	struct ipfw3_context *ctx;
	int *num;

	ctx = fw3_ctx[mycpuid];

	num =(int *)sopt->sopt_val;
	if (*num > 0 && *num < 32) {
		ctx->sets = ctx->sets ^ (1 << *num);
	}
	return 0;
}

int
ip_fw3_ctl_set_get(struct sockopt *sopt)
{
	struct ipfw3_context *ctx;

	ctx = fw3_ctx[mycpuid];

	bcopy(&ctx->sets, sopt->sopt_val, sopt->sopt_valsize);
	return 0;
}

void
set_flush_dispatch(netmsg_t smsg)
{
	struct netmsg_sets *msg = (struct netmsg_sets *)smsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *prev, *rule;

	prev = NULL;
	rule = ctx->rules;
	while (rule != NULL) {
		if (rule->set == msg->set_to) {
			rule = ip_fw3_delete_rule(ctx, prev, rule);
		} else {
			prev = rule;
			rule = rule->next;
		}
	}
	netisr_forwardmsg_all(&smsg->base, mycpuid + 1);
}

int
ip_fw3_ctl_set_flush(struct sockopt *sopt)
{
	struct netmsg_sets msg;
	int *num;

	num =(int *)sopt->sopt_val;
	if (*num > 0 && *num < 32) {
		bzero(&msg, sizeof(msg));
		netmsg_init(&msg.base, NULL, &curthread->td_msgport,
				0, set_flush_dispatch);
		msg.set_to = *num;

		netisr_domsg(&msg.base, 0);
	}
	return 0;
}

int
ip_fw3_ctl_set_sockopt(struct sockopt *sopt)
{
	int error = 0;
	switch (sopt->sopt_name) {
	case IP_FW_SET_GET:
		error = ip_fw3_ctl_set_get(sopt);
		break;
	case IP_FW_SET_TOGGLE:
		error = ip_fw3_ctl_set_toggle(sopt);
		break;
	case IP_FW_SET_SWAP:
		error = ip_fw3_ctl_set_swap(sopt);
		break;
	case IP_FW_SET_MOVE_RULE:
		error = ip_fw3_ctl_set_move_rule(sopt);
		break;
	case IP_FW_SET_MOVE_SET:
		error = ip_fw3_ctl_set_move_set(sopt);
		break;
	case IP_FW_SET_FLUSH:
		error = ip_fw3_ctl_set_flush(sopt);
		break;
	default:
		kprintf("ipfw3 set invalid socket option %d\n",
				sopt->sopt_name);
	}
	return error;
}

