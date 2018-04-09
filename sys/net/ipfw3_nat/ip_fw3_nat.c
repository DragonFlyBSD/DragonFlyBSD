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

struct ip_fw3_nat_context	*ip_fw3_nat_ctx[MAXCPU];
static struct callout 		ip_fw3_nat_cleanup_callout;
extern struct ipfw_context 	*ipfw_ctx[MAXCPU];
extern ip_fw_ctl_t 		*ipfw_ctl_nat_ptr;
static int 			fw3_nat_cleanup_interval = 1;

SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw3_nat, CTLFLAG_RW, 0, "ipfw3 NAT");
SYSCTL_INT(_net_inet_ip_fw3_nat, OID_AUTO, cleanup_interval, CTLFLAG_RW,
		&fw3_nat_cleanup_interval, 0, "default life time");

RB_PROTOTYPE(state_tree, nat_state, entries, nat_state_cmp);
RB_GENERATE(state_tree, nat_state, entries, nat_state_cmp);

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
ip_fw3_nat(struct ip_fw_args *args, struct cfg_nat *t, struct mbuf *m)
{
	/* TODO */
	return IP_FW_NAT;
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
	/* TODO */
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
	/* TODO */
}

int
ip_fw3_ctl_nat_del(struct sockopt *sopt)
{
	struct netmsg_nat_del nat_del_msg;
	struct netmsg_nat_del *msg;

	/* TODO */
	msg = &nat_del_msg;
	netmsg_init(&msg->base, NULL, &curthread->td_msgport,
			0, nat_del_dispatch);

	netisr_domsg(&msg->base, 0);
	return 0;
}

int
ip_fw3_ctl_nat_flush(struct sockopt *sopt)
{
	/* TODO */
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
			fw3_nat_cleanup_interval * hz,
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
			fw3_nat_cleanup_interval * hz,
			ipfw3_nat_cleanup_func,
			NULL);
	return 0;
}

static int
ip_fw3_nat_fini(void)
{
	/* TODO */
	callout_stop(&ip_fw3_nat_cleanup_callout);
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
