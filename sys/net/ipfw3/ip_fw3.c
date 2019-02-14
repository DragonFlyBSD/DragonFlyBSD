/*
 * Copyright (c) 1993 Daniel Boulet
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 * Copyright (c) 2002 Luigi Rizzo, Universita` di Pisa
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
 *
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

#include <net/if.h>
#include <net/radix.h>
#include <net/route.h>
#include <net/pfil.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
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
#include <net/ipfw3/ip_fw3_set.h>
#include <net/ipfw3_basic/ip_fw3_log.h>
#include <net/ipfw3_basic/ip_fw3_table.h>
#include <net/ipfw3_basic/ip_fw3_sync.h>
#include <net/ipfw3_basic/ip_fw3_basic.h>
#include <net/ipfw3_basic/ip_fw3_state.h>
#include <net/ipfw3_nat/ip_fw3_nat.h>
#include <net/dummynet3/ip_dummynet3.h>

MALLOC_DEFINE(M_IPFW3, "IPFW3", "ipfw3 module");

#define MAX_MODULE		10
#define MAX_OPCODE_PER_MODULE	100

#define IPFW_AUTOINC_STEP_MIN	1
#define IPFW_AUTOINC_STEP_MAX	1000
#define IPFW_AUTOINC_STEP_DEF	100


struct netmsg_ipfw {
	struct netmsg_base base;
	const struct ipfw_ioc_rule *ioc_rule;
	struct ip_fw	*rule;
	struct ip_fw	*next_rule;
	struct ip_fw	*prev_rule;
	struct ip_fw	*sibling;	/* sibling in prevous CPU */
};

struct netmsg_del {
	struct netmsg_base base;
	struct ip_fw	*rule;
	struct ip_fw	*start_rule;
	struct ip_fw	*prev_rule;
	struct ipfw_ioc_state *ioc_state;
	uint16_t	rulenum;
	uint8_t		set_from;
	uint8_t		set_to;
	int		kill_default;
};

struct netmsg_zent {
	struct netmsg_base base;
	struct ip_fw	*start_rule;
	uint16_t	rulenum;
	uint16_t	log_only;
};

ip_fw_ctl_t	*ip_fw3_ctl_nat_ptr = NULL;
ip_fw_ctl_t	*ip_fw3_ctl_state_ptr = NULL;
ip_fw_ctl_t	*ip_fw3_ctl_table_ptr = NULL;
ip_fw_ctl_t	*ip_fw3_ctl_sync_ptr = NULL;
ip_fw_log_t	*ip_fw3_log_ptr = NULL;

extern int ip_fw_loaded;
extern struct ipfw3_state_context 	*fw3_state_ctx[MAXCPU];
int 			sysctl_var_fw3_enable = 1;
int 			sysctl_var_fw3_one_pass = 1;
int 			sysctl_var_fw3_verbose = 0;
static int 		sysctl_var_fw3_flushing;
static int 		sysctl_var_fw3_debug;
static int 		sysctl_var_autoinc_step = IPFW_AUTOINC_STEP_DEF;

int	ip_fw3_sysctl_enable(SYSCTL_HANDLER_ARGS);
int	ip_fw3_sysctl_autoinc_step(SYSCTL_HANDLER_ARGS);

SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw3, CTLFLAG_RW, 0, "Firewall");
SYSCTL_PROC(_net_inet_ip_fw3, OID_AUTO, enable, CTLTYPE_INT | CTLFLAG_RW,
	&sysctl_var_fw3_enable, 0, ip_fw3_sysctl_enable, "I", "Enable ipfw");
SYSCTL_PROC(_net_inet_ip_fw3, OID_AUTO, sysctl_var_autoinc_step,
	CTLTYPE_INT | CTLFLAG_RW, &sysctl_var_autoinc_step, 0,
	ip_fw3_sysctl_autoinc_step, "I", "Rule number autincrement step");
SYSCTL_INT(_net_inet_ip_fw3, OID_AUTO,one_pass,CTLFLAG_RW,
	&sysctl_var_fw3_one_pass, 0,
	"Only do a single pass through ipfw3 when using dummynet(4)");
SYSCTL_INT(_net_inet_ip_fw3, OID_AUTO, debug, CTLFLAG_RW,
	&sysctl_var_fw3_debug, 0, "Enable printing of debug ip_fw statements");
SYSCTL_INT(_net_inet_ip_fw3, OID_AUTO, verbose, CTLFLAG_RW,
	&sysctl_var_fw3_verbose, 0, "Log matches to ipfw3 rules");


filter_func 			filter_funcs[MAX_MODULE][MAX_OPCODE_PER_MODULE];
struct ipfw3_module 		fw3_modules[MAX_MODULE];
struct ipfw3_context 		*fw3_ctx[MAXCPU];
struct ipfw3_sync_context 	fw3_sync_ctx;


void
ip_fw3_register_module(int module_id,char *module_name)
{
	struct ipfw3_module *tmp;
	int i;

	tmp = fw3_modules;
	for (i=0; i < MAX_MODULE; i++) {
		if (tmp->type == 0) {
			tmp->type = 1;
			tmp->id = module_id;
			strncpy(tmp->name, module_name, strlen(module_name));
			break;
		}
		tmp++;
	}
	kprintf("ipfw3 module %s loaded\n", module_name);
}

int
ip_fw3_unregister_module(int module_id)
{
	struct ipfw3_module *tmp;
	struct ip_fw *fw;
	ipfw_insn *cmd;
	int i, len, cmdlen, found;

	found = 0;
	tmp = fw3_modules;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	fw = ctx->rules;
	for (; fw; fw = fw->next) {
		for (len = fw->cmd_len, cmd = fw->cmd; len > 0;
			len -= cmdlen,
			cmd = (ipfw_insn *)((uint32_t *)cmd + cmdlen)) {
			cmdlen = F_LEN(cmd);
			if (cmd->module == 0 &&
				(cmd->opcode == 0 || cmd->opcode == 1)) {
				//action accept or deny
			} else if (cmd->module == module_id) {
				found = 1;
				goto decide;
			}
		}
	}
decide:
	if (found) {
		return 1;
	} else {
		for (i = 0; i < MAX_MODULE; i++) {
			if (tmp->type == 1 && tmp->id == module_id) {
				tmp->type = 0;
				kprintf("ipfw3 module %s unloaded\n",
						tmp->name);
				break;
			}
			tmp++;
		}

		for (i = 0; i < MAX_OPCODE_PER_MODULE; i++) {
			if (module_id == 0) {
				if (i ==0 || i == 1) {
					continue;
				}
			}
			filter_funcs[module_id][i] = NULL;
		}
		return 0;
	}
}

void
ip_fw3_register_filter_funcs(int module, int opcode, filter_func func)
{
	filter_funcs[module][opcode] = func;
}

void
check_accept(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = IP_FW_PASS;
	*cmd_ctl = IP_FW_CTL_DONE;
	if (cmd->arg3 && ip_fw3_log_ptr != NULL) {
		ip_fw3_log_ptr((*args)->m, (*args)->eh, cmd->arg1);
	}
}

void
check_deny(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = IP_FW_DENY;
	*cmd_ctl = IP_FW_CTL_DONE;
	if (cmd->arg3 && ip_fw3_log_ptr != NULL) {
		ip_fw3_log_ptr((*args)->m, (*args)->eh, cmd->arg1);
	}
}

void
init_module(void)
{
	memset(fw3_modules, 0, sizeof(struct ipfw3_module) * MAX_MODULE);
	memset(filter_funcs, 0, sizeof(filter_func) *
			MAX_OPCODE_PER_MODULE * MAX_MODULE);
	ip_fw3_register_filter_funcs(0, O_BASIC_ACCEPT,
			(filter_func)check_accept);
	ip_fw3_register_filter_funcs(0, O_BASIC_DENY, (filter_func)check_deny);
}

int
ip_fw3_free_rule(struct ip_fw *rule)
{
	kfree(rule, M_IPFW3);
	rule = NULL;
	return 1;
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
	if (rule == NULL) {	/* failure or not a skipto */
		rule = me->next;
	}
	me->next_rule = rule;
	return rule;
}

/*
 * rules are stored in ctx->ipfw_rule_chain.
 * and each rule is combination of multiple cmds.(ipfw_insn)
 * in each rule, it begin with filter cmds. and end with action cmds.
 * 'outer/inner loop' are looping the rules/cmds.
 * it will invoke the cmds relatived function according to the cmd's
 * module id and opcode id. and process according to return value.
 */
int
ip_fw3_chk(struct ip_fw_args *args)
{
	struct tcphdr *tcp;
	struct udphdr *udp;
	struct icmp *icmp;

	struct mbuf *m = args->m;
	struct ip *ip = mtod(m, struct ip *);
	struct ip_fw *f = NULL;		/* matching rule */
	int cmd_val = IP_FW_PASS;
	struct m_tag *mtag;
	struct divert_info *divinfo;

	/*
	 * hlen	The length of the IPv4 header.
	 *	hlen >0 means we have an IPv4 packet.
	 */
	u_int hlen = 0;		/* hlen >0 means we have an IP pkt */

	/*
	 * offset	The offset of a fragment. offset != 0 means that
	 *	we have a fragment at this offset of an IPv4 packet.
	 *	offset == 0 means that (if this is an IPv4 packet)
	 *	this is the first or only fragment.
	 */
	u_short offset = 0;

	uint8_t proto;
	uint16_t src_port = 0, dst_port = 0;	/* NOTE: host format	*/
	struct in_addr src_ip, dst_ip;		/* NOTE: network format	*/
	uint16_t ip_len = 0;
	uint8_t prev_module = -1, prev_opcode = -1; /* previous module & opcode */
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];

	if (m->m_pkthdr.fw_flags & IPFW_MBUF_GENERATED)
		return IP_FW_PASS;	/* accept */

	if (args->eh == NULL ||		/* layer 3 packet */
		(m->m_pkthdr.len >= sizeof(struct ip) &&
		 ntohs(args->eh->ether_type) == ETHERTYPE_IP))
		hlen = ip->ip_hl << 2;

	/*
	 * Collect parameters into local variables for faster matching.
	 */
	if (hlen == 0) {	/* do not grab addresses for non-ip pkts */
		proto = args->f_id.proto = 0;	/* mark f_id invalid */
		goto after_ip_checks;
	}

	proto = args->f_id.proto = ip->ip_p;
	src_ip = ip->ip_src;
	dst_ip = ip->ip_dst;
	if (args->eh != NULL) { /* layer 2 packets are as on the wire */
		offset = ntohs(ip->ip_off) & IP_OFFMASK;
		ip_len = ntohs(ip->ip_len);
	} else {
		offset = ip->ip_off & IP_OFFMASK;
		ip_len = ip->ip_len;
	}

#define PULLUP_TO(len)					\
do {							\
	if (m->m_len < (len)) {				\
		args->m = m = m_pullup(m, (len));	\
			if (m == NULL)			\
				goto pullup_failed;	\
		ip = mtod(m, struct ip *);		\
	}						\
} while (0)

	if (offset == 0) {
		switch (proto) {
			case IPPROTO_TCP:
				PULLUP_TO(hlen + sizeof(struct tcphdr));
				tcp = L3HDR(struct tcphdr, ip);
				dst_port = tcp->th_dport;
				src_port = tcp->th_sport;
				args->f_id.flags = tcp->th_flags;
				break;
			case IPPROTO_UDP:
				PULLUP_TO(hlen + sizeof(struct udphdr));
				udp = L3HDR(struct udphdr, ip);
				dst_port = udp->uh_dport;
				src_port = udp->uh_sport;
				break;
			case IPPROTO_ICMP:
				PULLUP_TO(hlen + 4);
				icmp = L3HDR(struct icmp, ip);
  				args->f_id.flags = icmp->icmp_type;
				dst_port = icmp->icmp_id;
				src_port = dst_port;
				break;
			default:
				break;
		}
	}

#undef PULLUP_TO

	args->f_id.src_ip = ntohl(src_ip.s_addr);
	args->f_id.dst_ip = ntohl(dst_ip.s_addr);
	args->f_id.src_port = src_port = ntohs(src_port);
	args->f_id.dst_port = dst_port = ntohs(dst_port);

after_ip_checks:
	if (args->rule) {
		/*
		 * Packet has already been tagged. Look for the next rule
		 * to restart processing.
		 *
		 * If sysctl_var_fw3_one_pass != 0 then just accept it.
		 * XXX should not happen here, but optimized out in
		 * the caller.
		 */
		if (sysctl_var_fw3_one_pass)
			return IP_FW_PASS;

		/* This rule is being/has been flushed */
		if (sysctl_var_fw3_flushing)
			return IP_FW_DENY;

		f = args->rule->next_rule;
		if (f == NULL)
			f = lookup_next_rule(args->rule);
	} else {
		/*
		 * Find the starting rule. It can be either the first
		 * one, or the one after divert_rule if asked so.
		 */
		int skipto;

		mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL);
		if (mtag != NULL) {
			divinfo = m_tag_data(mtag);
			skipto = divinfo->skipto;
		} else {
			skipto = 0;
		}

		f = ctx->rules;
		if (args->eh == NULL && skipto != 0) {
			/* No skipto during rule flushing */
			if (sysctl_var_fw3_flushing) {
				return IP_FW_DENY;
			}
			if (skipto >= IPFW_DEFAULT_RULE) {
				return IP_FW_DENY; /* invalid */
			}
			while (f && f->rulenum <= skipto) {
				f = f->next;
			}
			if (f == NULL) {	/* drop packet */
				return IP_FW_DENY;
			}
		} else if (sysctl_var_fw3_flushing) {
			/* Rules are being flushed; skip to default rule */
			f = ctx->default_rule;
		}
	}
	if ((mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL)) != NULL) {
		m_tag_delete(m, mtag);
	}

	/*
	 * Now scan the rules, and parse microinstructions for each rule.
	 */
	int prev_val;	/*  previous result of 'or' filter */
	int l, cmdlen;
	ipfw_insn *cmd;
	int cmd_ctl;
	/* foreach rule in chain */
	for (; f; f = f->next) {
again:  /* check the rule again*/
		if (ctx->sets & (1 << f->set)) {
			continue;
		}

		prev_val = -1;
		 /* foreach cmd in rule */
		for (l = f->cmd_len, cmd = f->cmd; l > 0; l -= cmdlen,
			cmd = (ipfw_insn *)((uint32_t *)cmd+ cmdlen)) {
			cmdlen = F_LEN(cmd);

			/* skip 'or' filter when already match */
			if (cmd->len & F_OR &&
				cmd->module == prev_module &&
				cmd->opcode == prev_opcode &&
				prev_val == 1) {
				goto next_cmd;
			}

check_body: /* check the body of the rule again.*/
			(filter_funcs[cmd->module][cmd->opcode])
				(&cmd_ctl, &cmd_val, &args, &f, cmd, ip_len);
			switch(cmd_ctl) {
				case IP_FW_CTL_DONE:
					if (prev_val == 0) /* but 'or' failed */
						goto next_rule;
					goto done;
				case IP_FW_CTL_AGAIN:
					goto again;
				case IP_FW_CTL_NEXT:
					goto next_rule;
				case IP_FW_CTL_NAT:
					args->rule=f;
					goto done;
				case IP_FW_CTL_CHK_STATE:
					/* update the cmd and l */
					cmd = ACTION_PTR(f);
					l = f->cmd_len - f->act_ofs;
					goto check_body;
			}
			if (cmd->len & F_NOT)
				cmd_val= !cmd_val;

			if (cmd->len & F_OR) {	/* has 'or' */
				if (!cmd_val) {	/* not matched */
					if(prev_val == -1){	/* first 'or' */
						prev_val = 0;
						prev_module = cmd->module;
						prev_opcode = cmd->opcode;
					} else if (prev_module == cmd->module &&
						prev_opcode == cmd->opcode) {
						/* continuous 'or' filter */
					} else if (prev_module != cmd->module ||
						prev_opcode != cmd->opcode) {
						/* 'or' filter changed */
						if(prev_val == 0){
							goto next_rule;
						} else {
							prev_val = 0;
							prev_module = cmd->module;
							prev_opcode = cmd->opcode;
						}
					}
				} else { /* has 'or' and matched */
					prev_val = 1;
					prev_module = cmd->module;
					prev_opcode = cmd->opcode;
				}
			} else { /* no or */
				if (!cmd_val) {	/* not matched */
					goto next_rule;
				} else {
					if (prev_val == 0) {
						/* previous 'or' not matched */
						goto next_rule;
					} else {
						prev_val = -1;
					}
				}
			}
next_cmd:;
		}	/* end of inner for, scan opcodes */
next_rule:;		/* try next rule		*/
	}		/* end of outer for, scan rules */
	kprintf("+++ ipfw: ouch!, skip past end of rules, denying packet\n");
	return IP_FW_DENY;

done:
	/* Update statistics */
	f->pcnt++;
	f->bcnt += ip_len;
	f->timestamp = time_second;
	return cmd_val;

pullup_failed:
	if (sysctl_var_fw3_verbose)
		kprintf("pullup failed\n");
	return IP_FW_DENY;
}

struct mbuf *
ip_fw3_dummynet_io(struct mbuf *m, int pipe_nr, int dir, struct ip_fw_args *fwa)
{
	struct m_tag *mtag;
	struct dn_pkt *pkt;
	ipfw_insn *cmd;
	const struct ipfw_flow_id *id;
	struct dn_flow_id *fid;

	M_ASSERTPKTHDR(m);

	mtag = m_tag_get(PACKET_TAG_DUMMYNET, sizeof(*pkt),
	    M_INTWAIT | M_NULLOK);
	if (mtag == NULL) {
		m_freem(m);
		return (NULL);
	}
	m_tag_prepend(m, mtag);

	pkt = m_tag_data(mtag);
	bzero(pkt, sizeof(*pkt));

	cmd = (ipfw_insn *)((uint32_t *)fwa->rule->cmd + fwa->rule->act_ofs);
	KASSERT(cmd->opcode == O_DUMMYNET_PIPE ||
			cmd->opcode == O_DUMMYNET_QUEUE,
			("Rule is not PIPE or QUEUE, opcode %d", cmd->opcode));

	pkt->dn_m = m;
	pkt->dn_flags = (dir & DN_FLAGS_DIR_MASK);
	pkt->ifp = fwa->oif;
	pkt->pipe_nr = pipe_nr;

	pkt->cpuid = mycpuid;
	pkt->msgport = netisr_curport();

	id = &fwa->f_id;
	fid = &pkt->id;
	fid->fid_dst_ip = id->dst_ip;
	fid->fid_src_ip = id->src_ip;
	fid->fid_dst_port = id->dst_port;
	fid->fid_src_port = id->src_port;
	fid->fid_proto = id->proto;
	fid->fid_flags = id->flags;

	pkt->dn_priv = fwa->rule;

	if ((int)cmd->opcode == O_DUMMYNET_PIPE)
		pkt->dn_flags |= DN_FLAGS_IS_PIPE;

	m->m_pkthdr.fw_flags |= DUMMYNET_MBUF_TAGGED;
	return (m);
}


void
add_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw *fwmsg = (struct netmsg_ipfw *)nmsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule, *prev,*next;
	const struct ipfw_ioc_rule *ioc_rule;

	ioc_rule = fwmsg->ioc_rule;
	 // create rule by ioc_rule
	rule = kmalloc(RULESIZE(ioc_rule), M_IPFW3, M_WAITOK | M_ZERO);
	rule->act_ofs = ioc_rule->act_ofs;
	rule->cmd_len = ioc_rule->cmd_len;
	rule->rulenum = ioc_rule->rulenum;
	rule->set = ioc_rule->set;
	bcopy(ioc_rule->cmd, rule->cmd, rule->cmd_len * 4);

	for (prev = NULL, next = ctx->rules;
		next; prev = next, next = next->next) {
		if (ioc_rule->insert) {
			if (next->rulenum >= ioc_rule->rulenum) {
				break;
			}
		} else {
			if (next->rulenum > ioc_rule->rulenum) {
				break;
			}
		}
	}
	KASSERT(next != NULL, ("no default rule?!"));

	/*
	 * Insert rule into the pre-determined position
	 */
	if (prev != NULL) {
		rule->next = next;
		prev->next = rule;
	} else {
		rule->next = ctx->rules;
		ctx->rules = rule;
	}

	/*
	 * if sibiling in last CPU is exists,
	 * then it's sibling should be current rule
	 */
	if (fwmsg->sibling != NULL) {
		fwmsg->sibling->sibling = rule;
	}
	/* prepare for next CPU */
	fwmsg->sibling = rule;

	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

/*
 * confirm the rulenumber
 * call dispatch function to add rule into the list
 * Update the statistic
 */
void
ip_fw3_add_rule(struct ipfw_ioc_rule *ioc_rule)
{
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct netmsg_ipfw fwmsg;
	struct netmsg_base *nmsg;
	struct ip_fw *f;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	/*
	 * If rulenum is 0, find highest numbered rule before the
	 * default rule, and add rule number incremental step.
	 */
	if (ioc_rule->rulenum == 0) {
		int step = sysctl_var_autoinc_step;

		KKASSERT(step >= IPFW_AUTOINC_STEP_MIN &&
				step <= IPFW_AUTOINC_STEP_MAX);

		/*
		 * Locate the highest numbered rule before default
		 */
		for (f = ctx->rules; f; f = f->next) {
			if (f->rulenum == IPFW_DEFAULT_RULE)
				break;
			ioc_rule->rulenum = f->rulenum;
		}
		if (ioc_rule->rulenum < IPFW_DEFAULT_RULE - step)
			ioc_rule->rulenum += step;
	}
	KASSERT(ioc_rule->rulenum != IPFW_DEFAULT_RULE &&
			ioc_rule->rulenum != 0,
			("invalid rule num %d", ioc_rule->rulenum));

	bzero(&fwmsg, sizeof(fwmsg));
	nmsg = &fwmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, add_rule_dispatch);
	fwmsg.ioc_rule = ioc_rule;

	netisr_domsg(nmsg, 0);

	IPFW3_DEBUG("++ installed rule %d, static count now %d\n",
			ioc_rule->rulenum, static_count);
}

/**
 * Free storage associated with a static rule (including derived
 * dynamic rules).
 * The caller is in charge of clearing rule pointers to avoid
 * dangling pointers.
 * @return a pointer to the next entry.
 * Arguments are not checked, so they better be correct.
 * Must be called at splimp().
 */
struct ip_fw *
ip_fw3_delete_rule(struct ipfw3_context *ctx,
		 struct ip_fw *prev, struct ip_fw *rule)
{
	if (prev == NULL)
		ctx->rules = rule->next;
	else
		prev->next = rule->next;

	kfree(rule, M_IPFW3);
	rule = NULL;
	return NULL;
}

void
flush_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule, *the_rule;
	int kill_default = dmsg->kill_default;

	rule = ctx->rules;
	while (rule != NULL) {
		if (rule->rulenum == IPFW_DEFAULT_RULE && kill_default == 0) {
			ctx->rules = rule;
			break;
		}
		the_rule = rule;
		rule = rule->next;

		kfree(the_rule, M_IPFW3);
	}

	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}


/*
 * Deletes all rules from a chain (including the default rule
 * if the second argument is set).
 * Must be called at splimp().
 */
void
ip_fw3_ctl_flush_rule(int kill_default)
{
	struct netmsg_del dmsg;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	if (!kill_default) {
		sysctl_var_fw3_flushing = 1;
		netmsg_service_sync();
	}
	/*
	 * Press the 'flush' button
	 */
	bzero(&dmsg, sizeof(dmsg));
	netmsg_init(&dmsg.base, NULL, &curthread->td_msgport,
			0, flush_rule_dispatch);
	dmsg.kill_default = kill_default;
	netisr_domsg(&dmsg.base, 0);

	/* Flush is done */
	sysctl_var_fw3_flushing = 0;
}

void
delete_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule, *prev = NULL;

	rule = ctx->rules;
	while (rule!=NULL) {
		if (rule->rulenum == dmsg->rulenum) {
			ip_fw3_delete_rule(ctx, prev, rule);
			break;
		}
		prev = rule;
		rule = rule->next;
	}

	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

int
ip_fw3_ctl_delete_rule(struct sockopt *sopt)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;
	int *rulenum;

	rulenum = (int *) sopt->sopt_val;


	/*
	 * Get rid of the rule duplications on all CPUs
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, delete_rule_dispatch);
	dmsg.rulenum = *rulenum;
	netisr_domsg(nmsg, 0);
	return 0;
}

/*
 * Clear counters for a specific rule.
 */
void
ip_fw3_clear_counters(struct ip_fw *rule)
{
	rule->bcnt = rule->pcnt = 0;
	rule->timestamp = 0;
}

void
ip_fw3_zero_entry_dispatch(netmsg_t nmsg)
{
	struct netmsg_zent *zmsg = (struct netmsg_zent *)nmsg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule;

	if (zmsg->rulenum == 0) {
		for (rule = ctx->rules; rule; rule = rule->next) {
			ip_fw3_clear_counters(rule);
		}
	} else {
		for (rule = ctx->rules; rule; rule = rule->next) {
			if (rule->rulenum == zmsg->rulenum) {
				ip_fw3_clear_counters(rule);
			}
		}
	}
	ip_fw3_clear_counters(ctx->default_rule);
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

/**
 * Reset some or all counters on firewall rules.
 * @arg frwl is null to clear all entries, or contains a specific
 * rule number.
 * @arg log_only is 1 if we only want to reset logs, zero otherwise.
 */
int
ip_fw3_ctl_zero_entry(int rulenum, int log_only)
{
	struct netmsg_zent zmsg;
	struct netmsg_base *nmsg;
	const char *msg;
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];

	bzero(&zmsg, sizeof(zmsg));
	nmsg = &zmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ip_fw3_zero_entry_dispatch);
	zmsg.log_only = log_only;

	if (rulenum == 0) {
		msg = log_only ? "ipfw: All logging counts reset.\n"
				   : "ipfw: Accounting cleared.\n";
	} else {
		struct ip_fw *rule;

		/*
		 * Locate the first rule with 'rulenum'
		 */
		for (rule = ctx->rules; rule; rule = rule->next) {
			if (rule->rulenum == rulenum)
				break;
		}
		if (rule == NULL) /* we did not find any matching rules */
			return (EINVAL);
		zmsg.start_rule = rule;
		zmsg.rulenum = rulenum;

		msg = log_only ? "ipfw: Entry %d logging count reset.\n"
				   : "ipfw: Entry %d cleared.\n";
	}
	netisr_domsg(nmsg, 0);
	KKASSERT(zmsg.start_rule == NULL);

	if (sysctl_var_fw3_verbose)
		log(LOG_SECURITY | LOG_NOTICE, msg, rulenum);
	return (0);
}

/*
 * Get the ioc_rule from the sopt
 * call ip_fw3_add_rule to add the rule
 */
int
ip_fw3_ctl_add_rule(struct sockopt *sopt)
{
	struct ipfw_ioc_rule *ioc_rule;
	size_t size;

	size = sopt->sopt_valsize;
	if (size > (sizeof(uint32_t) * IPFW_RULE_SIZE_MAX) ||
			size < sizeof(*ioc_rule) - sizeof(ipfw_insn)) {
		return EINVAL;
	}
	if (size != (sizeof(uint32_t) * IPFW_RULE_SIZE_MAX)) {
		sopt->sopt_val = krealloc(sopt->sopt_val, sizeof(uint32_t) *
				IPFW_RULE_SIZE_MAX, M_TEMP, M_WAITOK);
	}
	ioc_rule = sopt->sopt_val;

	ip_fw3_add_rule(ioc_rule);
	return 0;
}

int
ip_fw3_ctl_get_modules(struct sockopt *sopt)
{
	int i;
	struct ipfw3_module *mod;
	char module_str[1024];
	memset(module_str,0,1024);
	for (i = 0, mod = fw3_modules; i < MAX_MODULE; i++, mod++) {
		if (mod->type != 0) {
			if (i > 0)
				strcat(module_str,",");
			strcat(module_str,mod->name);
		}
	}
	bzero(sopt->sopt_val, sopt->sopt_valsize);
	bcopy(module_str, sopt->sopt_val, strlen(module_str));
	sopt->sopt_valsize = strlen(module_str);
	return 0;
}

/*
 * Copy all static rules and states on all CPU
 */
int
ip_fw3_ctl_get_rules(struct sockopt *sopt)
{
	struct ipfw3_context *ctx = fw3_ctx[mycpuid];
	struct ip_fw *rule;
	struct ipfw_ioc_rule *ioc;
	const struct ip_fw *sibling;
	int total_len = 0;

	ioc = (struct ipfw_ioc_rule *)sopt->sopt_val;

	for (rule = ctx->rules; rule; rule = rule->next) {
		total_len += IOC_RULESIZE(rule);
		if (total_len > sopt->sopt_valsize) {
			bzero(sopt->sopt_val, sopt->sopt_valsize);
			return 0;
		}
		ioc->act_ofs = rule->act_ofs;
		ioc->cmd_len = rule->cmd_len;
		ioc->rulenum = rule->rulenum;
		ioc->set = rule->set;

		ioc->sets = fw3_ctx[mycpuid]->sets;
		ioc->pcnt = 0;
		ioc->bcnt = 0;
		ioc->timestamp = 0;
		for (sibling = rule; sibling != NULL; sibling = sibling->sibling) {
			ioc->pcnt += sibling->pcnt;
			ioc->bcnt += sibling->bcnt;
			if (sibling->timestamp > ioc->timestamp)
				ioc->timestamp = sibling->timestamp;
		}
		bcopy(rule->cmd, ioc->cmd, ioc->cmd_len * 4);
		ioc = (struct ipfw_ioc_rule *)((uint8_t *)ioc + IOC_RULESIZE(ioc));
	}
	sopt->sopt_valsize = total_len;
	return 0;
}


/*
 * ip_fw3_ctl_x - extended version of ip_fw3_ctl
 * remove the x_header, and adjust the sopt_name, sopt_val and sopt_valsize.
 */
int
ip_fw3_ctl_x(struct sockopt *sopt)
{
	ip_fw_x_header *x_header;
	x_header = (ip_fw_x_header *)(sopt->sopt_val);
	sopt->sopt_name = x_header->opcode;
	sopt->sopt_valsize -= sizeof(ip_fw_x_header);
	bcopy(++x_header, sopt->sopt_val, sopt->sopt_valsize);
	return ip_fw3_ctl(sopt);
}


/**
 * {set|get}sockopt parser.
 */
int
ip_fw3_ctl(struct sockopt *sopt)
{
	int error = 0;
	switch (sopt->sopt_name) {
		case IP_FW_X:
			ip_fw3_ctl_x(sopt);
			break;
		case IP_FW_GET:
		case IP_FW_MODULE:
		case IP_FW_FLUSH:
		case IP_FW_ADD:
		case IP_FW_DEL:
		case IP_FW_ZERO:
		case IP_FW_RESETLOG:
			error = ip_fw3_ctl_sockopt(sopt);
			break;
		case IP_FW_SET_GET:
		case IP_FW_SET_MOVE_RULE:
		case IP_FW_SET_MOVE_SET:
		case IP_FW_SET_SWAP:
		case IP_FW_SET_TOGGLE:
			error = ip_fw3_ctl_set_sockopt(sopt);
			break;
		case IP_FW_NAT_ADD:
		case IP_FW_NAT_DEL:
		case IP_FW_NAT_FLUSH:
		case IP_FW_NAT_GET:
		case IP_FW_NAT_GET_RECORD:
			if (ip_fw3_ctl_nat_ptr != NULL) {
				error = ip_fw3_ctl_nat_ptr(sopt);
			}
			break;
		case IP_DUMMYNET_GET:
		case IP_DUMMYNET_CONFIGURE:
		case IP_DUMMYNET_DEL:
		case IP_DUMMYNET_FLUSH:
			error = ip_dn_sockopt(sopt);
			break;
		case IP_FW_STATE_ADD:
		case IP_FW_STATE_DEL:
		case IP_FW_STATE_FLUSH:
		case IP_FW_STATE_GET:
			if (ip_fw3_ctl_state_ptr != NULL) {
				error = ip_fw3_ctl_state_ptr(sopt);
			}
			break;
		case IP_FW_TABLE_CREATE:
		case IP_FW_TABLE_DELETE:
		case IP_FW_TABLE_APPEND:
		case IP_FW_TABLE_REMOVE:
		case IP_FW_TABLE_LIST:
		case IP_FW_TABLE_FLUSH:
		case IP_FW_TABLE_SHOW:
		case IP_FW_TABLE_TEST:
		case IP_FW_TABLE_RENAME:
			if (ip_fw3_ctl_table_ptr != NULL) {
				error = ip_fw3_ctl_table_ptr(sopt);
			}
			break;
		case IP_FW_SYNC_SHOW_CONF:
		case IP_FW_SYNC_SHOW_STATUS:
		case IP_FW_SYNC_EDGE_CONF:
		case IP_FW_SYNC_EDGE_START:
		case IP_FW_SYNC_EDGE_STOP:
		case IP_FW_SYNC_EDGE_TEST:
		case IP_FW_SYNC_EDGE_CLEAR:
		case IP_FW_SYNC_CENTRE_CONF:
		case IP_FW_SYNC_CENTRE_START:
		case IP_FW_SYNC_CENTRE_STOP:
		case IP_FW_SYNC_CENTRE_TEST:
		case IP_FW_SYNC_CENTRE_CLEAR:
			if (ip_fw3_ctl_sync_ptr != NULL) {
				error = ip_fw3_ctl_sync_ptr(sopt);
			}
			break;
		default:
			kprintf("ip_fw3_ctl invalid option %d\n",
				sopt->sopt_name);
			error = EINVAL;
	}
	return error;
}

int
ip_fw3_ctl_sockopt(struct sockopt *sopt)
{
	int error = 0, rulenum;

	switch (sopt->sopt_name) {
		case IP_FW_GET:
			error = ip_fw3_ctl_get_rules(sopt);
			break;
		case IP_FW_MODULE:
			error = ip_fw3_ctl_get_modules(sopt);
			break;
		case IP_FW_FLUSH:
			ip_fw3_ctl_flush_rule(0);
			break;
		case IP_FW_ADD:
			error = ip_fw3_ctl_add_rule(sopt);
			break;
		case IP_FW_DEL:
			error = ip_fw3_ctl_delete_rule(sopt);
			break;
		case IP_FW_ZERO:
		case IP_FW_RESETLOG: /* argument is an int, the rule number */
			rulenum = 0;
			if (sopt->sopt_valsize != 0) {
				error = soopt_to_kbuf(sopt, &rulenum,
						sizeof(int), sizeof(int));
				if (error) {
					break;
				}
			}
			error = ip_fw3_ctl_zero_entry(rulenum,
					sopt->sopt_name == IP_FW_RESETLOG);
			break;
		default:
			kprintf("ip_fw3_ctl invalid option %d\n",
				sopt->sopt_name);
			error = EINVAL;
	}
	return error;
}

int
ip_fw3_check_in(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir)
{
	struct ip_fw_args args;
	struct mbuf *m = *m0;
	struct m_tag *mtag;
	int tee = 0, error = 0, ret;
	// again:
	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
		/* Extract info from dummynet tag */
		mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
		KKASSERT(mtag != NULL);
		args.rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;
		KKASSERT(args.rule != NULL);

		m_tag_delete(m, mtag);
		m->m_pkthdr.fw_flags &= ~DUMMYNET_MBUF_TAGGED;
	} else {
		args.rule = NULL;
	}

	args.eh = NULL;
	args.oif = NULL;
	args.m = m;
	ret = ip_fw3_chk(&args);
	m = args.m;

	if (m == NULL) {
		error = EACCES;
		goto back;
	}
	switch (ret) {
		case IP_FW_PASS:
			break;

		case IP_FW_DENY:
			m_freem(m);
			m = NULL;
			error = EACCES;
			break;

		case IP_FW_DUMMYNET:
			/* Send packet to the appropriate pipe */
			m = ip_fw3_dummynet_io(m, args.cookie, DN_TO_IP_IN,
			    &args);
			break;
		case IP_FW_TEE:
			tee = 1;
			/* FALL THROUGH */
		case IP_FW_DIVERT:
			/*
			 * Must clear bridge tag when changing
			 */
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			if (ip_divert_p != NULL) {
				m = ip_divert_p(m, tee, 1);
			} else {
				m_freem(m);
				m = NULL;
				/* not sure this is the right error msg */
				error = EACCES;
			}
			break;
		case IP_FW_NAT:
			break;
		case IP_FW_ROUTE:
			break;
		default:
			panic("unknown ipfw3 return value: %d", ret);
	}
back:
	*m0 = m;
	return error;
}

int
ip_fw3_check_out(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir)
{
	struct ip_fw_args args;
	struct mbuf *m = *m0;
	struct m_tag *mtag;
	int tee = 0, error = 0, ret;
	// again:
	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
		/* Extract info from dummynet tag */
		mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
		KKASSERT(mtag != NULL);
		args.rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;
		KKASSERT(args.rule != NULL);

		m_tag_delete(m, mtag);
		m->m_pkthdr.fw_flags &= ~DUMMYNET_MBUF_TAGGED;
	} else {
		args.rule = NULL;
	}

	args.eh = NULL;
	args.m = m;
	args.oif = ifp;
	ret = ip_fw3_chk(&args);
	m = args.m;

	if (m == NULL) {
		error = EACCES;
		goto back;
	}

	switch (ret) {
		case IP_FW_PASS:
			break;

		case IP_FW_DENY:
			m_freem(m);
			m = NULL;
			error = EACCES;
			break;

		case IP_FW_DUMMYNET:
			m = ip_fw3_dummynet_io(m, args.cookie, DN_TO_IP_OUT,
			    &args);
			break;

		case IP_FW_TEE:
			tee = 1;
			/* FALL THROUGH */

		case IP_FW_DIVERT:
			if (ip_divert_p != NULL) {
				m = ip_divert_p(m, tee, 0);
			} else {
				m_freem(m);
				m = NULL;
				/* not sure this is the right error msg */
				error = EACCES;
			}
			break;

		case IP_FW_NAT:
			break;
		case IP_FW_ROUTE:
			break;
		default:
			panic("unknown ipfw3 return value: %d", ret);
	}
back:
	*m0 = m;
	return error;
}

void
ip_fw3_hook(void)
{
	struct pfil_head *pfh;
	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	pfh = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh == NULL)
		return;

	pfil_add_hook(ip_fw3_check_in, NULL, PFIL_IN, pfh);
	pfil_add_hook(ip_fw3_check_out, NULL, PFIL_OUT, pfh);
}

void
ip_fw3_dehook(void)
{
	struct pfil_head *pfh;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	pfh = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh == NULL)
		return;

	pfil_remove_hook(ip_fw3_check_in, NULL, PFIL_IN, pfh);
	pfil_remove_hook(ip_fw3_check_out, NULL, PFIL_OUT, pfh);
}

void
ip_fw3_sysctl_enable_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	int enable = lmsg->u.ms_result;

	if (sysctl_var_fw3_enable == enable)
		goto reply;

	sysctl_var_fw3_enable = enable;
	if (sysctl_var_fw3_enable)
		ip_fw3_hook();
	else
		ip_fw3_dehook();

reply:
	lwkt_replymsg(lmsg, 0);
}

int
ip_fw3_sysctl_enable(SYSCTL_HANDLER_ARGS)
{
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg;
	int enable, error;

	enable = sysctl_var_fw3_enable;
	error = sysctl_handle_int(oidp, &enable, 0, req);
	if (error || req->newptr == NULL)
		return error;

	netmsg_init(&nmsg, NULL, &curthread->td_msgport,
			0, ip_fw3_sysctl_enable_dispatch);
	lmsg = &nmsg.lmsg;
	lmsg->u.ms_result = enable;

	return lwkt_domsg(IPFW_CFGPORT, lmsg, 0);
}

int
ip_fw3_sysctl_autoinc_step(SYSCTL_HANDLER_ARGS)
{
	return sysctl_int_range(oidp, arg1, arg2, req,
			IPFW_AUTOINC_STEP_MIN, IPFW_AUTOINC_STEP_MAX);
}

void
ctx_init_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw *fwmsg = (struct netmsg_ipfw *)nmsg;
	struct ipfw3_context *ctx;
	struct ip_fw *def_rule;

	ctx = kmalloc(LEN_FW3_CTX, M_IPFW3, M_WAITOK | M_ZERO);
	fw3_ctx[mycpuid] = ctx;
	ctx->sets = IPFW_ALL_SETS;

	def_rule = kmalloc(LEN_FW3, M_IPFW3, M_WAITOK | M_ZERO);
	def_rule->act_ofs = 0;
	def_rule->rulenum = IPFW_DEFAULT_RULE;
	def_rule->cmd_len = 2;
	def_rule->set = IPFW_DEFAULT_SET;

	def_rule->cmd[0].len = LEN_OF_IPFWINSN;
	def_rule->cmd[0].module = MODULE_BASIC_ID;
#ifdef IPFIREWALL_DEFAULT_TO_ACCEPT
	def_rule->cmd[0].opcode = O_BASIC_ACCEPT;
#else
	if (filters_default_to_accept)
		def_rule->cmd[0].opcode = O_BASIC_ACCEPT;
	else
		def_rule->cmd[0].opcode = O_BASIC_DENY;
#endif

	/* Install the default rule */
	ctx->default_rule = def_rule;
	ctx->rules = def_rule;

	/*
	 * if sibiling in last CPU is exists,
	 * then it's sibling should be current rule
	 */
	if (fwmsg->sibling != NULL) {
		fwmsg->sibling->sibling = def_rule;
	}
	/* prepare for next CPU */
	fwmsg->sibling = def_rule;

	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

void
init_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw fwmsg;
	int error = 0;
	if (IPFW3_LOADED) {
		kprintf("ipfw3 already loaded\n");
		error = EEXIST;
		goto reply;
	}

	bzero(&fwmsg, sizeof(fwmsg));
	netmsg_init(&fwmsg.base, NULL, &curthread->td_msgport,
			0, ctx_init_dispatch);
	netisr_domsg(&fwmsg.base, 0);

	ip_fw_chk_ptr = ip_fw3_chk;
	ip_fw_ctl_x_ptr = ip_fw3_ctl_x;
	ip_fw_dn_io_ptr = ip_fw3_dummynet_io;

	kprintf("ipfw3 initialized, default to %s\n",
			filters_default_to_accept ? "accept" : "deny");

	ip_fw3_loaded = 1;
	if (sysctl_var_fw3_enable)
		ip_fw3_hook();
reply:
	lwkt_replymsg(&nmsg->lmsg, error);
}

int
ip_fw3_init(void)
{
	struct netmsg_base smsg;
	int error;

	init_module();
	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, init_dispatch);
	error = lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
	return error;
}

#ifdef KLD_MODULE

void
fini_dispatch(netmsg_t nmsg)
{
	int error = 0, cpu;

	ip_fw3_loaded = 0;

	ip_fw3_dehook();
	netmsg_service_sync();
	ip_fw_chk_ptr = NULL;
	ip_fw_ctl_x_ptr = NULL;
	ip_fw_dn_io_ptr = NULL;
	ip_fw3_ctl_flush_rule(1);
	/* Free pre-cpu context */
	for (cpu = 0; cpu < ncpus; ++cpu) {
		if (fw3_ctx[cpu] != NULL) {
			kfree(fw3_ctx[cpu], M_IPFW3);
			fw3_ctx[cpu] = NULL;
		}
	}
	kprintf("ipfw3 unloaded\n");

	lwkt_replymsg(&nmsg->lmsg, error);
}

int
ip_fw3_fini(void)
{
	struct netmsg_base smsg;

	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, fini_dispatch);
	return lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
}

#endif	/* KLD_MODULE */

static int
ip_fw3_modevent(module_t mod, int type, void *unused)
{
	int err = 0;

	switch (type) {
		case MOD_LOAD:
			err = ip_fw3_init();
			break;

		case MOD_UNLOAD:

#ifndef KLD_MODULE
			kprintf("ipfw3 statically compiled, cannot unload\n");
			err = EBUSY;
#else
			err = ip_fw3_fini();
#endif
			break;
		default:
			break;
	}
	return err;
}

static moduledata_t ipfw3mod = {
	"ipfw3",
	ip_fw3_modevent,
	0
};
/* ipfw3 must init before ipfw3_basic */
DECLARE_MODULE(ipfw3, ipfw3mod, SI_SUB_PROTO_END, SI_ORDER_FIRST);
MODULE_VERSION(ipfw3, 1);
