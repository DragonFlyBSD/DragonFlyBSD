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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systimer.h>
#include <sys/thread2.h>
#include <sys/in_cksum.h>

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

#include <net/ipfw2/ip_fw.h>

#include "ip_fw2_basic.h"

#define TIME_LEQ(a, b)	((int)((a) - (b)) <= 0)

extern struct ipfw_context	*ipfw_ctx[MAXCPU];
extern int fw_verbose;
extern ipfw_basic_delete_state_t *ipfw_basic_flush_state_prt;
extern ipfw_basic_append_state_t *ipfw_basic_append_state_prt;

static int ip_fw_basic_loaded;
static struct netmsg_base ipfw_timeout_netmsg;	/* schedule ipfw timeout */
static struct callout ipfw_tick_callout;
static int state_lifetime = 20;
static int state_expiry_check_interval = 10;
static int state_count_max = 4096;
static int state_hash_size_old = 0;
static int state_hash_size = 4096;


static int ipfw_sysctl_adjust_hash_size(SYSCTL_HANDLER_ARGS);
void adjust_hash_size_dispatch(netmsg_t nmsg);

SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw_basic,
		CTLFLAG_RW, 0, "Firewall Basic");
SYSCTL_PROC(_net_inet_ip_fw_basic, OID_AUTO, state_hash_size,
		CTLTYPE_INT | CTLFLAG_RW, &state_hash_size, 0,
		ipfw_sysctl_adjust_hash_size, "I", "Adjust hash size");

SYSCTL_INT(_net_inet_ip_fw_basic, OID_AUTO, state_lifetime, CTLFLAG_RW,
		&state_lifetime, 0, "default life time");
SYSCTL_INT(_net_inet_ip_fw_basic, OID_AUTO,
		state_expiry_check_interval, CTLFLAG_RW,
		&state_expiry_check_interval, 0,
		"default state expiry check interval");
SYSCTL_INT(_net_inet_ip_fw_basic, OID_AUTO, state_count_max, CTLFLAG_RW,
		&state_count_max, 0, "maximum of state");

static int
ipfw_sysctl_adjust_hash_size(SYSCTL_HANDLER_ARGS)
{
	int error, value = 0;

	state_hash_size_old = state_hash_size;
	value = state_hash_size;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr) {
		goto back;
	}
	/*
	 * Make sure we have a power of 2 and
	 * do not allow more than 64k entries.
	 */
	error = EINVAL;
	if (value <= 1 || value > 65536) {
		goto back;
	}
	if ((value & (value - 1)) != 0) {
		goto back;
	}

	error = 0;
	if (state_hash_size != value) {
		state_hash_size = value;

		struct netmsg_base *msg, the_msg;
		msg = &the_msg;
		bzero(msg,sizeof(struct netmsg_base));

		netmsg_init(msg, NULL, &curthread->td_msgport,
				0, adjust_hash_size_dispatch);
		ifnet_domsg(&msg->lmsg, 0);
	}
back:
	return error;
}

void
adjust_hash_size_dispatch(netmsg_t nmsg)
{
	struct ipfw_state_context *state_ctx;
	struct ip_fw_state *the_state, *state;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	int i;

	for (i = 0; i < state_hash_size_old; i++) {
		state_ctx = &ctx->state_ctx[i];
		if (state_ctx != NULL) {
			state = state_ctx->state;
			while (state != NULL) {
				the_state = state;
				state = state->next;
				kfree(the_state, M_IPFW2_BASIC);
				the_state = NULL;
			}
		}
	}
	kfree(ctx->state_ctx,M_IPFW2_BASIC);
	ctx->state_ctx = kmalloc(state_hash_size *
				sizeof(struct ipfw_state_context),
				M_IPFW2_BASIC, M_WAITOK | M_ZERO);
	ctx->state_hash_size = state_hash_size;
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}


/*	prototype of the checker functions	*/
void check_count(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_skipto(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_forward(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_check_state(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

void check_in(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_out(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_via(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_proto(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_prob(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_from(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_to(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_keep_state(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_tag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_untag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_tagged(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

/*	prototype of the utility functions	*/
static struct ip_fw *lookup_next_rule(struct ip_fw *me);
static int iface_match(struct ifnet *ifp, ipfw_insn_if *cmd);
static __inline int hash_packet(struct ipfw_flow_id *id);

static __inline int
hash_packet(struct ipfw_flow_id *id)
{
	uint32_t i;
	i = (id->proto) ^ (id->dst_ip) ^ (id->src_ip) ^
		(id->dst_port) ^ (id->src_port);
	i &= state_hash_size - 1;
	return i;
}

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

/*
 * when all = 1, it will check all the state_ctx
 */
static struct ip_fw_state *
lookup_state(struct ip_fw_args *args, ipfw_insn *cmd, int *limited, int all)
{
	struct ip_fw_state *state = NULL;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_state_context *state_ctx;
	int start, end, i, count = 0;

	if (all && cmd->arg1) {
		start = 0;
		end = state_hash_size - 1;
	} else {
		start = hash_packet(&args->f_id);
		end = hash_packet(&args->f_id);
	}
	for (i = start; i <= end; i++) {
		state_ctx = &ctx->state_ctx[i];
		if (state_ctx != NULL) {
			state = state_ctx->state;
			struct ipfw_flow_id	*fid = &args->f_id;
			while (state != NULL) {
				if (cmd->arg1) {
					if ((cmd->arg3 == 1 &&
						fid->src_ip ==
						state->flow_id.src_ip) ||
						(cmd->arg3 == 2 &&
						fid->src_port ==
						state->flow_id.src_port) ||
						(cmd->arg3 == 3 &&
						fid->dst_ip ==
						state->flow_id.dst_ip) ||
						(cmd->arg3 == 4 &&
						fid->dst_port ==
						state->flow_id.dst_port)) {

						count++;
						if (count >= cmd->arg1) {
							*limited = 1;
							goto done;
						}
					}
				}

				if (fid->proto == state->flow_id.proto) {
					if (fid->src_ip ==
					state->flow_id.src_ip &&
					fid->dst_ip ==
					state->flow_id.dst_ip &&
					(fid->src_port ==
					state->flow_id.src_port ||
					state->flow_id.src_port == 0) &&
					(fid->dst_port ==
					state->flow_id.dst_port ||
					state->flow_id.dst_port == 0)) {
						goto done;
					}
					if (fid->src_ip ==
					state->flow_id.dst_ip &&
					fid->dst_ip ==
					state->flow_id.src_ip &&
					(fid->src_port ==
					state->flow_id.dst_port ||
					state->flow_id.dst_port == 0) &&
					(fid->dst_port ==
					state->flow_id.src_port ||
					state->flow_id.src_port == 0)) {
						goto done;
					}
				}
				state = state->next;
			}
		}
	}
done:
	return state;
}

static struct ip_fw_state *
install_state(struct ip_fw *rule, ipfw_insn *cmd, struct ip_fw_args *args)
{
	struct ip_fw_state *state;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_state_context *state_ctx;
	state_ctx = &ctx->state_ctx[hash_packet(&args->f_id)];
	state = kmalloc(sizeof(struct ip_fw_state),
			M_IPFW2_BASIC, M_NOWAIT | M_ZERO);
	if (state == NULL) {
		return NULL;
	}
	state->stub = rule;
	state->lifetime = cmd->arg2 == 0 ? state_lifetime : cmd->arg2 ;
	state->timestamp = time_second;
	state->expiry = 0;
	bcopy(&args->f_id,&state->flow_id,sizeof(struct ipfw_flow_id));
	//append the state into the state chian
	if (state_ctx->last != NULL)
		state_ctx->last->next = state;
	else
		state_ctx->state = state;
	state_ctx->last = state;
	state_ctx->count++;
	return state;
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
			sizeof(*sin), M_NOWAIT);
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
check_check_state(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct ip_fw_state *state=NULL;
	int limited = 0 ;
	state = lookup_state(*args, cmd, &limited, 0);
	if (state != NULL) {
		state->pcnt++;
		state->bcnt += ip_len;
		state->timestamp = time_second;
		(*f)->pcnt++;
		(*f)->bcnt += ip_len;
		(*f)->timestamp = time_second;
		*f = state->stub;
		*cmd_ctl = IP_FW_CTL_CHK_STATE;
	} else {
		*cmd_ctl = IP_FW_CTL_NEXT;
	}
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
	*cmd_val = (hlen > 0 &&
			((ipfw_insn_ip *)cmd)->addr.s_addr == src_ip.s_addr);
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_to(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
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
	*cmd_val = (hlen > 0 &&
			((ipfw_insn_ip *)cmd)->addr.s_addr == dst_ip.s_addr);
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_keep_state(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
	struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	struct ip_fw_state *state;
	int limited = 0;

	*cmd_ctl = IP_FW_CTL_NO;
	state = lookup_state(*args, cmd, &limited, 1);
	if (limited == 1) {
		*cmd_val = IP_FW_NOT_MATCH;
	} else {
		if (state == NULL)
			state = install_state(*f, cmd, *args);

		if (state != NULL) {
			state->pcnt++;
			state->bcnt += ip_len;
			state->timestamp = time_second;
			*cmd_val = IP_FW_MATCH;
		} else {
			*cmd_val = IP_FW_NOT_MATCH;
		}
	}
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

static void
ipfw_basic_add_state(struct ipfw_ioc_state *ioc_state)
{
	struct ip_fw_state *state;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_state_context *state_ctx;
	state_ctx = &ctx->state_ctx[hash_packet(&(ioc_state->flow_id))];
	state = kmalloc(sizeof(struct ip_fw_state),
			M_IPFW2_BASIC, M_WAITOK | M_ZERO);
	struct ip_fw *rule = ctx->ipfw_rule_chain;
	while (rule != NULL) {
		if (rule->rulenum == ioc_state->rulenum) {
			break;
		}
		rule = rule->next;
	}
	if (rule == NULL)
		return;

	state->stub = rule;

	state->lifetime = ioc_state->lifetime == 0 ?
		state_lifetime : ioc_state->lifetime ;
	state->timestamp = time_second;
	state->expiry = ioc_state->expiry;
	bcopy(&ioc_state->flow_id, &state->flow_id,
			sizeof(struct ipfw_flow_id));
	//append the state into the state chian
	if (state_ctx->last != NULL)
		state_ctx->last->next = state;
	else
		state_ctx->state = state;

	state_ctx->last = state;
	state_ctx->count++;
}

/*
 * if rule is NULL
 * 		flush all states
 * else
 * 		flush states which stub is the rule
 */
static void
ipfw_basic_flush_state(struct ip_fw *rule)
{
	struct ipfw_state_context *state_ctx;
	struct ip_fw_state *state,*the_state, *prev_state;
	struct ipfw_context *ctx;
	int i;

	ctx = ipfw_ctx[mycpuid];
	for (i = 0; i < state_hash_size; i++) {
		state_ctx = &ctx->state_ctx[i];
		if (state_ctx != NULL) {
			state = state_ctx->state;
			prev_state = NULL;
			while (state != NULL) {
				if (rule != NULL && state->stub != rule) {
					prev_state = state;
					state = state->next;
				} else {
					if (prev_state == NULL)
						state_ctx->state = state->next;
					else
						prev_state->next = state->next;

					the_state = state;
					state = state->next;
					kfree(the_state, M_IPFW2_BASIC);
					state_ctx->count--;
					if (state == NULL)
						state_ctx->last = prev_state;

				}
			}
		}
	}
}

/*
 * clean up expired state in every tick
 */
static void
ipfw_cleanup_expired_state(netmsg_t nmsg)
{
	struct ip_fw_state *state,*the_state,*prev_state;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_state_context *state_ctx;
	int i;

	for (i = 0; i < state_hash_size; i++) {
		prev_state = NULL;
		state_ctx = &(ctx->state_ctx[i]);
		if (ctx->state_ctx != NULL) {
			state = state_ctx->state;
			while (state != NULL) {
				if (IS_EXPIRED(state)) {
					if (prev_state == NULL)
						state_ctx->state = state->next;
					else
						prev_state->next = state->next;

					the_state =state;
					state = state->next;

					if (the_state == state_ctx->last)
						state_ctx->last = NULL;


					kfree(the_state, M_IPFW2_BASIC);
					state_ctx->count--;
				} else {
					prev_state = state;
					state = state->next;
				}
			}
		}
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
ipfw_tick(void *dummy __unused)
{
	struct lwkt_msg *lmsg = &ipfw_timeout_netmsg.lmsg;
	KKASSERT(mycpuid == IPFW_CFGCPUID);

	crit_enter();
	KKASSERT(lmsg->ms_flags & MSGF_DONE);
	if (IPFW_BASIC_LOADED) {
		lwkt_sendmsg_oncpu(IPFW_CFGPORT, lmsg);
		/* ipfw_timeout_netmsg's handler reset this callout */
	}
	crit_exit();

	struct netmsg_base *msg;
	struct netmsg_base the_msg;
	msg = &the_msg;
	bzero(msg,sizeof(struct netmsg_base));

	netmsg_init(msg, NULL, &curthread->td_msgport, 0,
			ipfw_cleanup_expired_state);
	ifnet_domsg(&msg->lmsg, 0);
}

static void
ipfw_tick_dispatch(netmsg_t nmsg)
{
	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);
	KKASSERT(IPFW_BASIC_LOADED);

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&nmsg->lmsg, 0);
	crit_exit();

	callout_reset(&ipfw_tick_callout,
			state_expiry_check_interval * hz, ipfw_tick, NULL);
}

static void
ipfw_basic_init_dispatch(netmsg_t nmsg)
{
	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);
	KKASSERT(IPFW_LOADED);

	int error = 0;
	callout_init_mp(&ipfw_tick_callout);
	netmsg_init(&ipfw_timeout_netmsg, NULL, &netisr_adone_rport,
			MSGF_DROPABLE | MSGF_PRIORITY, ipfw_tick_dispatch);
	callout_reset(&ipfw_tick_callout,
			state_expiry_check_interval * hz, ipfw_tick, NULL);
	lwkt_replymsg(&nmsg->lmsg, error);
	ip_fw_basic_loaded=1;
}

static int
ipfw_basic_init(void)
{
	ipfw_basic_flush_state_prt = ipfw_basic_flush_state;
	ipfw_basic_append_state_prt = ipfw_basic_add_state;

	register_ipfw_module(MODULE_BASIC_ID, MODULE_BASIC_NAME);
	register_ipfw_filter_funcs(MODULE_BASIC_ID, O_BASIC_COUNT,
			(filter_func)check_count);
	register_ipfw_filter_funcs(MODULE_BASIC_ID, O_BASIC_SKIPTO,
			(filter_func)check_skipto);
	register_ipfw_filter_funcs(MODULE_BASIC_ID, O_BASIC_FORWARD,
			(filter_func)check_forward);
	register_ipfw_filter_funcs(MODULE_BASIC_ID, O_BASIC_KEEP_STATE,
			(filter_func)check_keep_state);
	register_ipfw_filter_funcs(MODULE_BASIC_ID, O_BASIC_CHECK_STATE,
			(filter_func)check_check_state);

	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IN, (filter_func)check_in);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_OUT, (filter_func)check_out);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_VIA, (filter_func)check_via);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_XMIT, (filter_func)check_via);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_RECV, (filter_func)check_via);

	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_PROTO, (filter_func)check_proto);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_PROB, (filter_func)check_prob);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_SRC, (filter_func)check_from);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_IP_DST, (filter_func)check_to);

	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_TAG, (filter_func)check_tag);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_UNTAG, (filter_func)check_untag);
	register_ipfw_filter_funcs(MODULE_BASIC_ID,
			O_BASIC_TAGGED, (filter_func)check_tagged);

	int cpu;
	struct ipfw_context *ctx;

	for (cpu = 0; cpu < ncpus; cpu++) {
		ctx = ipfw_ctx[cpu];
		if (ctx != NULL) {
			ctx->state_ctx = kmalloc(state_hash_size *
					sizeof(struct ipfw_state_context),
					M_IPFW2_BASIC, M_WAITOK | M_ZERO);
			ctx->state_hash_size = state_hash_size;
		}
	}

	struct netmsg_base smsg;
	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, ipfw_basic_init_dispatch);
	lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
	return 0;
}

static void
ipfw_basic_stop_dispatch(netmsg_t nmsg)
{
	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);
	KKASSERT(IPFW_LOADED);
	int error = 0;
	callout_stop(&ipfw_tick_callout);
	netmsg_service_sync();
	crit_enter();
	lwkt_dropmsg(&ipfw_timeout_netmsg.lmsg);
	crit_exit();
	lwkt_replymsg(&nmsg->lmsg, error);
	ip_fw_basic_loaded=0;
}

static int
ipfw_basic_stop(void)
{
	int cpu,i;
	struct ipfw_state_context *state_ctx;
	struct ip_fw_state *state,*the_state;
	struct ipfw_context *ctx;
	if (unregister_ipfw_module(MODULE_BASIC_ID) ==0 ) {
		ipfw_basic_flush_state_prt = NULL;
		ipfw_basic_append_state_prt = NULL;

		for (cpu = 0; cpu < ncpus; cpu++) {
			ctx = ipfw_ctx[cpu];
			if (ctx != NULL) {
				for (i = 0; i < state_hash_size; i++) {
					state_ctx = &ctx->state_ctx[i];
					if (state_ctx != NULL) {
						state = state_ctx->state;
						while (state != NULL) {
							the_state = state;
							state = state->next;
							if (the_state ==
								state_ctx->last)
							state_ctx->last = NULL;

							kfree(the_state,
								M_IPFW2_BASIC);
						}
					}
				}
				ctx->state_hash_size = 0;
				kfree(ctx->state_ctx, M_IPFW2_BASIC);
				ctx->state_ctx = NULL;
			}
		}
		struct netmsg_base smsg;
		netmsg_init(&smsg, NULL, &curthread->td_msgport,
				0, ipfw_basic_stop_dispatch);
		return lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
	}
	return 1;
}


static int
ipfw2_basic_modevent(module_t mod, int type, void *data)
{
	int err;
	switch (type) {
		case MOD_LOAD:
			err = ipfw_basic_init();
			break;
		case MOD_UNLOAD:
			err = ipfw_basic_stop();
			break;
		default:
			err = 1;
	}
	return err;
}

static moduledata_t ipfw2_basic_mod = {
	"ipfw2_basic",
	ipfw2_basic_modevent,
	NULL
};
DECLARE_MODULE(ipfw2_basic, ipfw2_basic_mod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_DEPEND(ipfw2_basic, ipfw2, 1, 1, 1);
MODULE_VERSION(ipfw2_basic, 1);
