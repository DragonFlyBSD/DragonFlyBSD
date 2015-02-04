/*
 * Copyright (c) 2002 Luigi Rizzo, Universita` di Pisa
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netinet/ip_fw2.c,v 1.6.2.12 2003/04/08 10:42:32 maxim Exp $
 */

#include "opt_ipfw.h"
#include "opt_inet.h"
#ifndef INET
#error IPFIREWALL requires INET.
#endif /* INET */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/in_cksum.h>
#include <sys/lock.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

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

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>
#include <net/netmsg2.h>
#include <net/dummynet2/ip_dummynet.h>
#include <net/ipfw2/ip_fw2.h>
#include <net/ipfw_basic/ip_fw_basic.h>
#include <net/ipfw_nat/ip_fw_nat.h>

MALLOC_DEFINE(M_IPFW2, "IPFW2", "ip_fw2 default module");

#ifdef IPFIREWALL_DEBUG
#define DPRINTF(fmt, ...)			\
do { 						\
	if (fw_debug > 0) 			\
		kprintf(fmt, __VA_ARGS__); 	\
} while(0)
#else
#define DPRINTF(fmt, ...)	((void)0)
#endif

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
	uint8_t		from_set;
	uint8_t		to_set;
};

struct netmsg_zent {
	struct netmsg_base base;
	struct ip_fw	*start_rule;
	uint16_t	rulenum;
	uint16_t	log_only;
};

ipfw_nat_cfg_t *ipfw_nat_cfg_ptr;
ipfw_nat_cfg_t *ipfw_nat_del_ptr;
ipfw_nat_cfg_t *ipfw_nat_flush_ptr;
ipfw_nat_cfg_t *ipfw_nat_get_cfg_ptr;
ipfw_nat_cfg_t *ipfw_nat_get_log_ptr;

/* handlers which implemented in ipfw_basic module */
ipfw_basic_delete_state_t *ipfw_basic_flush_state_prt = NULL;
ipfw_basic_append_state_t *ipfw_basic_append_state_prt = NULL;

static struct ipfw_context	*ipfw_ctx[MAXCPU];
static struct ipfw_nat_context *ipfw_nat_ctx;

static uint32_t static_count;	/* # of static rules */
static uint32_t static_ioc_len;	/* bytes of static rules */
static int ipfw_flushing;
static int fw_verbose;
static int verbose_limit;
static int fw_debug;
static int autoinc_step = IPFW_AUTOINC_STEP_DEF;

static int	ipfw_sysctl_enable(SYSCTL_HANDLER_ARGS);
static int	ipfw_sysctl_autoinc_step(SYSCTL_HANDLER_ARGS);

SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw, CTLFLAG_RW, 0, "Firewall");
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, enable, CTLTYPE_INT | CTLFLAG_RW,
	&fw_enable, 0, ipfw_sysctl_enable, "I", "Enable ipfw");
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, autoinc_step, CTLTYPE_INT | CTLFLAG_RW,
	&autoinc_step, 0, ipfw_sysctl_autoinc_step, "I",
	"Rule number autincrement step");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO,one_pass,CTLFLAG_RW,
	&fw_one_pass, 0,
	"Only do a single pass through ipfw when using dummynet(4)");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, debug, CTLFLAG_RW,
	&fw_debug, 0, "Enable printing of debug ip_fw statements");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, verbose, CTLFLAG_RW,
	&fw_verbose, 0, "Log matches to ipfw rules");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, verbose_limit, CTLFLAG_RW,
	&verbose_limit, 0, "Set upper limit of matches of ipfw rules logged");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, static_count, CTLFLAG_RD,
	&static_count, 0, "Number of static rules");

filter_func filter_funcs[MAX_MODULE][MAX_OPCODE_PER_MODULE];
struct ipfw_module ipfw_modules[MAX_MODULE];
static int ipfw_ctl(struct sockopt *sopt);


void
check_accept(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_deny(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void init_module(void);


void
register_ipfw_module(int module_id,char *module_name)
{
	struct ipfw_module *tmp;
	int i;

	tmp = ipfw_modules;
	for (i=0; i < MAX_MODULE; i++) {
		if (tmp->type == 0) {
			tmp->type = 1;
			tmp->id = module_id;
			strncpy(tmp->name, module_name, strlen(module_name));
			break;
		}
		tmp++;
	}
	kprintf("ipfw2 module %s loaded ", module_name);
}

int
unregister_ipfw_module(int module_id)
{
	struct ipfw_module *tmp;
	struct ip_fw *fw;
	ipfw_insn *cmd;
	int i, len, cmdlen, found;

	found = 0;
	tmp = ipfw_modules;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	fw = ctx->ipfw_rule_chain;
	for (; fw; fw = fw->next) {
		for (len = fw->cmd_len, cmd = fw->cmd; len > 0;
			len -= cmdlen, cmd = (ipfw_insn *)((uint32_t *)cmd + cmdlen)) {
			cmdlen = F_LEN(cmd);
			if (cmd->module == 0 &&(cmd->opcode == 0 || cmd->opcode == 1)) {
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
	} else{
		for (i = 0; i < MAX_MODULE; i++) {
			if (tmp->type == 1 && tmp->id == module_id) {
				tmp->type = 0;
				kprintf("ipfw2 module %s unloaded ", tmp->name);
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
register_ipfw_filter_funcs(int module, int opcode, filter_func func)
{
	filter_funcs[module][opcode] = func;
}

void
check_accept(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = IP_FW_PASS;
	*cmd_ctl = IP_FW_CTL_DONE;
}

void
check_deny(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = IP_FW_DENY;
	*cmd_ctl = IP_FW_CTL_DONE;
}

void
init_module(void)
{
	memset(ipfw_modules, 0, sizeof(struct ipfw_module) * MAX_MODULE);
	memset(filter_funcs, 0, sizeof(filter_func) * MAX_OPCODE_PER_MODULE * MAX_MODULE);
	register_ipfw_filter_funcs(0, O_BASIC_ACCEPT, (filter_func)check_accept);
	register_ipfw_filter_funcs(0, O_BASIC_DENY, (filter_func)check_deny);
}

static __inline int
ipfw_free_rule(struct ip_fw *rule)
{
	kfree(rule, M_IPFW2);
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
	if (rule == NULL) {			/* failure or not a skipto */
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
static int
ipfw_chk(struct ip_fw_args *args)
{
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

	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

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
} while(0)

	if (offset == 0) {
		switch (proto) {
			case IPPROTO_TCP:
				{
					struct tcphdr *tcp;

					PULLUP_TO(hlen + sizeof(struct tcphdr));
					tcp = L3HDR(struct tcphdr, ip);
					dst_port = tcp->th_dport;
					src_port = tcp->th_sport;
					args->f_id.flags = tcp->th_flags;
				}
				break;

			case IPPROTO_UDP:
				{
					struct udphdr *udp;

					PULLUP_TO(hlen + sizeof(struct udphdr));
					udp = L3HDR(struct udphdr, ip);
					dst_port = udp->uh_dport;
					src_port = udp->uh_sport;
				}
				break;

			case IPPROTO_ICMP:
				PULLUP_TO(hlen + 4);	/* type, code and checksum. */
				args->f_id.flags = L3HDR(struct icmp, ip)->icmp_type;
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
		 * If fw_one_pass != 0 then just accept it.
		 * XXX should not happen here, but optimized out in
		 * the caller.
		 */
		if (fw_one_pass)
			return IP_FW_PASS;

		/* This rule is being/has been flushed */
		if (ipfw_flushing)
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

		f = ctx->ipfw_rule_chain;
		if (args->eh == NULL && skipto != 0) {
			/* No skipto during rule flushing */
			if (ipfw_flushing) {
				return IP_FW_DENY;
			}
			if (skipto >= IPFW_DEFAULT_RULE) {
				return IP_FW_DENY; /* invalid */
			}
			while(f && f->rulenum <= skipto) {
				f = f->next;
			}
			if (f == NULL) {	/* drop packet */
				return IP_FW_DENY;
			}
		} else if (ipfw_flushing) {
			/* Rules are being flushed; skip to default rule */
			f = ctx->ipfw_default_rule;
		}
	}
	if ((mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL)) != NULL) {
		m_tag_delete(m, mtag);
	}

	/*
	 * Now scan the rules, and parse microinstructions for each rule.
	 */
	for (; f; f = f->next) {
		int l, cmdlen;
		ipfw_insn *cmd;
		int cmd_ctl;
again:  /* check the rule again*/
		if (ctx->ipfw_set_disable & (1 << f->set)) {
			continue;
		}

		for (l = f->cmd_len, cmd = f->cmd; l > 0; l -= cmdlen,
			cmd=(ipfw_insn *)((uint32_t *)cmd+ cmdlen)) {

check_body: /* check the body of the rule again.*/
			cmdlen = F_LEN(cmd);
			(filter_funcs[cmd->module][cmd->opcode])
				(&cmd_ctl, &cmd_val, &args, &f, cmd, ip_len);
			switch(cmd_ctl) {
				case IP_FW_CTL_DONE:
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
			if (cmd->len & F_NOT) {
				cmd_val= !cmd_val;
			}
			if (!cmd_val) {
				break;
			}
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
	if (fw_verbose)
		kprintf("pullup failed\n");
	return IP_FW_DENY;
}

static void
ipfw_dummynet_io(struct mbuf *m, int pipe_nr, int dir, struct ip_fw_args *fwa)
{
	struct m_tag *mtag;
	struct dn_pkt *pkt;
	ipfw_insn *cmd;
	const struct ipfw_flow_id *id;
	struct dn_flow_id *fid;

	M_ASSERTPKTHDR(m);

	mtag = m_tag_get(PACKET_TAG_DUMMYNET, sizeof(*pkt), MB_DONTWAIT);
	if (mtag == NULL) {
		m_freem(m);
		return;
	}
	m_tag_prepend(m, mtag);

	pkt = m_tag_data(mtag);
	bzero(pkt, sizeof(*pkt));

	cmd = fwa->rule->cmd + fwa->rule->act_ofs;
	KASSERT(cmd->opcode == O_DUMMYNET_PIPE || cmd->opcode == O_DUMMYNET_QUEUE,
			("Rule is not PIPE or QUEUE, opcode %d", cmd->opcode));

	pkt->dn_m = m;
	pkt->dn_flags = (dir & DN_FLAGS_DIR_MASK);
	pkt->ifp = fwa->oif;
	pkt->pipe_nr = pipe_nr;

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
}

static __inline void
ipfw_inc_static_count(struct ip_fw *rule)
{
	/* Static rule's counts are updated only on CPU0 */
	KKASSERT(mycpuid == 0);

	static_count++;
	static_ioc_len += IOC_RULESIZE(rule);
}

static __inline void
ipfw_dec_static_count(struct ip_fw *rule)
{
	int l = IOC_RULESIZE(rule);

	/* Static rule's counts are updated only on CPU0 */
	KKASSERT(mycpuid == 0);

	KASSERT(static_count > 0, ("invalid static count %u", static_count));
	static_count--;

	KASSERT(static_ioc_len >= l,
			("invalid static len %u", static_ioc_len));
	static_ioc_len -= l;
}

static void
ipfw_add_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw *fwmsg = (struct netmsg_ipfw *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule, *prev,*next;
	const struct ipfw_ioc_rule *ioc_rule;

	ioc_rule = fwmsg->ioc_rule;
	 // create rule by ioc_rule
	rule = kmalloc(RULESIZE(ioc_rule), M_IPFW2, M_WAITOK | M_ZERO);
	rule->act_ofs = ioc_rule->act_ofs;
	rule->cmd_len = ioc_rule->cmd_len;
	rule->rulenum = ioc_rule->rulenum;
	rule->set = ioc_rule->set;
	bcopy(ioc_rule->cmd, rule->cmd, rule->cmd_len * 4);

	for (prev = NULL, next = ctx->ipfw_rule_chain;
		next; prev = next, next = next->next) {
		if (next->rulenum > ioc_rule->rulenum) {
			break;
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
		rule->next = ctx->ipfw_rule_chain;
		ctx->ipfw_rule_chain = rule;
	}

	/* if sibiling in last CPU is exists, then it's sibling should be current rule */
	if (fwmsg->sibling != NULL) {
		fwmsg->sibling->sibling = rule;
	}
	/* prepare for next CPU */
	fwmsg->sibling = rule;

	if (mycpuid == 0) {
		/* Statistics only need to be updated once */
		ipfw_inc_static_count(rule);
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * confirm the rulenumber
 * call dispatch function to add rule into the list
 * Update the statistic
 */
static void
ipfw_add_rule(struct ipfw_ioc_rule *ioc_rule)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct netmsg_ipfw fwmsg;
	struct netmsg_base *nmsg;
	struct ip_fw *f;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	/*
	 * If rulenum is 0, find highest numbered rule before the
	 * default rule, and add rule number incremental step.
	 */
	if (ioc_rule->rulenum == 0) {
		int step = autoinc_step;

		KKASSERT(step >= IPFW_AUTOINC_STEP_MIN &&
				step <= IPFW_AUTOINC_STEP_MAX);

		/*
		 * Locate the highest numbered rule before default
		 */
		for (f = ctx->ipfw_rule_chain; f; f = f->next) {
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
			0, ipfw_add_rule_dispatch);
	fwmsg.ioc_rule = ioc_rule;

	ifnet_domsg(&nmsg->lmsg, 0);

	DPRINTF("++ installed rule %d, static count now %d\n",
			rule->rulenum, static_count);
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
static struct ip_fw *
ipfw_delete_rule(struct ipfw_context *ctx,
		 struct ip_fw *prev, struct ip_fw *rule)
{
	if (prev == NULL)
		ctx->ipfw_rule_chain = rule->next;
	else
		prev->next = rule->next;

	if (mycpuid == IPFW_CFGCPUID)
		ipfw_dec_static_count(rule);

	kfree(rule, M_IPFW2);
	rule = NULL;
	return NULL;
}

static void
ipfw_flush_rule_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule, *the_rule;
	int kill_default = lmsg->u.ms_result;

	rule = ctx->ipfw_rule_chain;
	while(rule != NULL) {
		if (rule->rulenum == IPFW_DEFAULT_RULE && kill_default == 0) {
			ctx->ipfw_rule_chain = rule;
			break;
		}
		the_rule = rule;
		rule = rule->next;
		if (mycpuid == IPFW_CFGCPUID)
			ipfw_dec_static_count(the_rule);

		kfree(the_rule, M_IPFW2);
	}

	ifnet_forwardmsg(lmsg, mycpuid + 1);
}

static void
ipfw_append_state_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_ioc_state *ioc_state = dmsg->ioc_state;
	(*ipfw_basic_append_state_prt)(ioc_state);
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
ipfw_delete_state_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule = ctx->ipfw_rule_chain;
	while(rule != NULL) {
		if (rule->rulenum == dmsg->rulenum) {
			break;
		}
		rule = rule->next;
	}

	(*ipfw_basic_flush_state_prt)(rule);
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * Deletes all rules from a chain (including the default rule
 * if the second argument is set).
 * Must be called at splimp().
 */
static void
ipfw_ctl_flush_rule(int kill_default)
{
	struct netmsg_del dmsg;
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	/*
	 * If 'kill_default' then caller has done the necessary
	 * msgport syncing; unnecessary to do it again.
	 */
	if (!kill_default) {
		/*
		 * Let ipfw_chk() know the rules are going to
		 * be flushed, so it could jump directly to
		 * the default rule.
		 */
		ipfw_flushing = 1;
		netmsg_service_sync();
	}

	/*
	 * if ipfw_basic_flush_state_prt
	 * flush all states in all CPU
	 */
	if (ipfw_basic_flush_state_prt != NULL) {
		bzero(&dmsg, sizeof(dmsg));
		netmsg_init(&dmsg.base, NULL, &curthread->td_msgport,
				0, ipfw_delete_state_dispatch);
		ifnet_domsg(&dmsg.base.lmsg, 0);
	}
	/*
	 * Press the 'flush' button
	 */
	bzero(&nmsg, sizeof(nmsg));
	netmsg_init(&nmsg, NULL, &curthread->td_msgport,
			0, ipfw_flush_rule_dispatch);
	lmsg = &nmsg.lmsg;
	lmsg->u.ms_result = kill_default;
	ifnet_domsg(lmsg, 0);

	if (kill_default) {
		KASSERT(static_count == 0,
				("%u static rules remain", static_count));
		KASSERT(static_ioc_len == 0,
				("%u bytes of static rules remain", static_ioc_len));
	}

	/* Flush is done */
	ipfw_flushing = 0;
}

static void
ipfw_delete_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule, *prev = NULL;

	rule = ctx->ipfw_rule_chain;
	while(rule!=NULL) {
		if (rule->rulenum == dmsg->rulenum) {
			ipfw_delete_rule(ctx, prev, rule);
			break;
		}
		prev = rule;
		rule = rule->next;
	}

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static int
ipfw_alt_delete_rule(uint16_t rulenum)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;

	/*
	 * delete the state which stub is the rule
	 * which belongs to the CPU and the rulenum
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_delete_state_dispatch);
	dmsg.rulenum = rulenum;
	ifnet_domsg(&nmsg->lmsg, 0);

	/*
	 * Get rid of the rule duplications on all CPUs
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_delete_rule_dispatch);
	dmsg.rulenum = rulenum;
	ifnet_domsg(&nmsg->lmsg, 0);
	return 0;
}

static void
ipfw_alt_delete_ruleset_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *prev, *rule;
#ifdef INVARIANTS
	int del = 0;
#endif

	prev = NULL;
	rule = ctx->ipfw_rule_chain;
	while(rule != NULL) {
		if (rule->set == dmsg->from_set) {
			rule = ipfw_delete_rule(ctx, prev, rule);
#ifdef INVARIANTS
			del = 1;
#endif
		} else {
			prev = rule;
			rule = rule->next;
		}
	}
	KASSERT(del, ("no match set?!"));

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
ipfw_disable_ruleset_state_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;
#ifdef INVARIANTS
	int cleared = 0;
#endif

	for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
		if (rule->set == dmsg->from_set) {
#ifdef INVARIANTS
			cleared = 1;
#endif
		}
	}
	KASSERT(cleared, ("no match set?!"));

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static int
ipfw_alt_delete_ruleset(uint8_t set)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;
	int state, del;
	struct ip_fw *rule;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

	/*
	 * Check whether the 'set' exists.  If it exists,
	 * then check whether any rules within the set will
	 * try to create states.
	 */
	state = 0;
	del = 0;
	for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
		if (rule->set == set) {
			del = 1;
		}
	}
	if (!del)
		return 0; /* XXX EINVAL? */

	if (state) {
		/*
		 * Clear the STATE flag, so no more states will be
		 * created based the rules in this set.
		 */
		bzero(&dmsg, sizeof(dmsg));
		nmsg = &dmsg.base;
		netmsg_init(nmsg, NULL, &curthread->td_msgport,
				0, ipfw_disable_ruleset_state_dispatch);
		dmsg.from_set = set;

		ifnet_domsg(&nmsg->lmsg, 0);
	}

	/*
	 * Delete this set
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_alt_delete_ruleset_dispatch);
	dmsg.from_set = set;

	ifnet_domsg(&nmsg->lmsg, 0);
	return 0;
}

static void
ipfw_alt_move_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ip_fw *rule;

	rule = dmsg->start_rule;

	/*
	 * Move to the position on the next CPU
	 * before the msg is forwarded.
	 */

	while(rule && rule->rulenum <= dmsg->rulenum) {
		if (rule->rulenum == dmsg->rulenum)
			rule->set = dmsg->to_set;
		rule = rule->next;
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static int
ipfw_alt_move_rule(uint16_t rulenum, uint8_t set)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;
	struct ip_fw *rule;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

	/*
	 * Locate first rule to move
	 */
	for (rule = ctx->ipfw_rule_chain;
		rule && rule->rulenum <= rulenum; rule = rule->next) {
		if (rule->rulenum == rulenum && rule->set != set)
			break;
	}
	if (rule == NULL || rule->rulenum > rulenum)
		return 0; /* XXX error? */

	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_alt_move_rule_dispatch);
	dmsg.start_rule = rule;
	dmsg.rulenum = rulenum;
	dmsg.to_set = set;

	ifnet_domsg(&nmsg->lmsg, 0);
	KKASSERT(dmsg.start_rule == NULL);
	return 0;
}

static void
ipfw_alt_move_ruleset_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
		if (rule->set == dmsg->from_set)
			rule->set = dmsg->to_set;
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static int
ipfw_alt_move_ruleset(uint8_t from_set, uint8_t to_set)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;

	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_alt_move_ruleset_dispatch);
	dmsg.from_set = from_set;
	dmsg.to_set = to_set;

	ifnet_domsg(&nmsg->lmsg, 0);
	return 0;
}

static void
ipfw_alt_swap_ruleset_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
		if (rule->set == dmsg->from_set)
			rule->set = dmsg->to_set;
		else if (rule->set == dmsg->to_set)
			rule->set = dmsg->from_set;
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static int
ipfw_alt_swap_ruleset(uint8_t set1, uint8_t set2)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;

	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_alt_swap_ruleset_dispatch);
	dmsg.from_set = set1;
	dmsg.to_set = set2;

	ifnet_domsg(&nmsg->lmsg, 0);
	return 0;
}


static int
ipfw_ctl_alter(uint32_t arg)
{
	uint16_t rulenum;
	uint8_t cmd, new_set;
	int error = 0;

	rulenum = arg & 0xffff;
	cmd = (arg >> 24) & 0xff;
	new_set = (arg >> 16) & 0xff;

	if (cmd > 4)
		return EINVAL;
	if (new_set >= IPFW_DEFAULT_SET)
		return EINVAL;
	if (cmd == 0 || cmd == 2) {
		if (rulenum == IPFW_DEFAULT_RULE)
			return EINVAL;
	} else {
		if (rulenum >= IPFW_DEFAULT_SET)
			return EINVAL;
	}

	switch (cmd) {
	case 0:	/* delete rules with given number */
		error = ipfw_alt_delete_rule(rulenum);
		break;

	case 1:	/* delete all rules with given set number */
		error = ipfw_alt_delete_ruleset(rulenum);
		break;

	case 2:	/* move rules with given number to new set */
		error = ipfw_alt_move_rule(rulenum, new_set);
		break;

	case 3: /* move rules with given set number to new set */
		error = ipfw_alt_move_ruleset(rulenum, new_set);
		break;

	case 4: /* swap two sets */
		error = ipfw_alt_swap_ruleset(rulenum, new_set);
		break;
	}
	return error;
}

/*
 * Clear counters for a specific rule.
 */
static void
clear_counters(struct ip_fw *rule)
{
	rule->bcnt = rule->pcnt = 0;
	rule->timestamp = 0;
}

static void
ipfw_zero_entry_dispatch(netmsg_t nmsg)
{
	struct netmsg_zent *zmsg = (struct netmsg_zent *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	if (zmsg->rulenum == 0) {
		for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
			clear_counters(rule);
		}
	} else {
		for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
			if (rule->rulenum == zmsg->rulenum) {
				clear_counters(rule);
			}
		}
	}
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/**
 * Reset some or all counters on firewall rules.
 * @arg frwl is null to clear all entries, or contains a specific
 * rule number.
 * @arg log_only is 1 if we only want to reset logs, zero otherwise.
 */
static int
ipfw_ctl_zero_entry(int rulenum, int log_only)
{
	struct netmsg_zent zmsg;
	struct netmsg_base *nmsg;
	const char *msg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

	bzero(&zmsg, sizeof(zmsg));
	nmsg = &zmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_zero_entry_dispatch);
	zmsg.log_only = log_only;

	if (rulenum == 0) {
		msg = log_only ? "ipfw: All logging counts reset.\n"
				   : "ipfw: Accounting cleared.\n";
	} else {
		struct ip_fw *rule;

		/*
		 * Locate the first rule with 'rulenum'
		 */
		for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
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
	ifnet_domsg(&nmsg->lmsg, 0);
	KKASSERT(zmsg.start_rule == NULL);

	if (fw_verbose)
		log(LOG_SECURITY | LOG_NOTICE, msg, rulenum);
	return (0);
}

static int
ipfw_ctl_add_state(struct sockopt *sopt)
{
	struct ipfw_ioc_state *ioc_state;
	ioc_state = sopt->sopt_val;
	if (ipfw_basic_append_state_prt != NULL) {
		struct netmsg_del dmsg;
		bzero(&dmsg, sizeof(dmsg));
		netmsg_init(&dmsg.base, NULL, &curthread->td_msgport,
			0, ipfw_append_state_dispatch);
		(&dmsg)->ioc_state = ioc_state;
		ifnet_domsg(&dmsg.base.lmsg, 0);
	}
	return 0;
}

static int
ipfw_ctl_delete_state(struct sockopt *sopt)
{
	int rulenum = 0, error;
	if (sopt->sopt_valsize != 0) {
		error = soopt_to_kbuf(sopt, &rulenum, sizeof(int), sizeof(int));
		if (error) {
			return -1;
		}
	}
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule = ctx->ipfw_rule_chain;

	while(rule!=NULL) {
		if (rule->rulenum == rulenum) {
			break;
		}
		rule = rule->next;
	}
	if (rule == NULL) {
		return -1;
	}

	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;
	/*
	 * delete the state which stub is the rule
	 * which belongs to the CPU and the rulenum
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_delete_state_dispatch);
	dmsg.rulenum = rulenum;
	ifnet_domsg(&nmsg->lmsg, 0);
	return 0;
}

static int
ipfw_ctl_flush_state(struct sockopt *sopt)
{
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;
	/*
	 * delete the state which stub is the rule
	 * which belongs to the CPU and the rulenum
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
			0, ipfw_delete_state_dispatch);
	dmsg.rulenum = 0;
	ifnet_domsg(&nmsg->lmsg, 0);
	return 0;
}

/*
 * Get the ioc_rule from the sopt
 * call ipfw_add_rule to add the rule
 */
static int
ipfw_ctl_add_rule(struct sockopt *sopt)
{
	struct ipfw_ioc_rule *ioc_rule;
	size_t size;

	size = sopt->sopt_valsize;
	if (size > (sizeof(uint32_t) * IPFW_RULE_SIZE_MAX) ||
			size < sizeof(*ioc_rule)) {
		return EINVAL;
	}
	if (size != (sizeof(uint32_t) * IPFW_RULE_SIZE_MAX)) {
		sopt->sopt_val = krealloc(sopt->sopt_val, sizeof(uint32_t) *
				IPFW_RULE_SIZE_MAX, M_TEMP, M_WAITOK);
	}
	ioc_rule = sopt->sopt_val;

	ipfw_add_rule(ioc_rule);
	return 0;
}

static void *
ipfw_copy_state(struct ip_fw_state *state, struct ipfw_ioc_state *ioc_state, int cpuid)
{
	ioc_state->pcnt = state->pcnt;
	ioc_state->bcnt = state->bcnt;
	ioc_state->lifetime = state->lifetime;
	ioc_state->timestamp = state->timestamp;
	ioc_state->cpuid = cpuid;
	ioc_state->expiry = state->expiry;
	ioc_state->rulenum = state->stub->rulenum;

	bcopy(&state->flow_id, &ioc_state->flow_id, sizeof(struct ipfw_flow_id));
	return ioc_state + 1;
}

static void *
ipfw_copy_rule(const struct ip_fw *rule, struct ipfw_ioc_rule *ioc_rule)
{
	const struct ip_fw *sibling;
#ifdef INVARIANTS
	int i;
#endif

	ioc_rule->act_ofs = rule->act_ofs;
	ioc_rule->cmd_len = rule->cmd_len;
	ioc_rule->rulenum = rule->rulenum;
	ioc_rule->set = rule->set;

	ioc_rule->set_disable = ipfw_ctx[mycpuid]->ipfw_set_disable;
	ioc_rule->static_count = static_count;
	ioc_rule->static_len = static_ioc_len;

	ioc_rule->pcnt = 1;
	ioc_rule->bcnt = 0;
	ioc_rule->timestamp = 0;

#ifdef INVARIANTS
	i = 0;
#endif
	ioc_rule->pcnt = 0;
	ioc_rule->bcnt = 0;
	ioc_rule->timestamp = 0;
	for (sibling = rule; sibling != NULL; sibling = sibling->sibling) {
		ioc_rule->pcnt += sibling->pcnt;
		ioc_rule->bcnt += sibling->bcnt;
		if (sibling->timestamp > ioc_rule->timestamp)
			ioc_rule->timestamp = sibling->timestamp;
#ifdef INVARIANTS
		++i;
#endif
	}

	KASSERT(i == ncpus, ("static rule is not duplicated on every cpu"));

	bcopy(rule->cmd, ioc_rule->cmd, ioc_rule->cmd_len * 4 /* XXX */);

	return ((uint8_t *)ioc_rule + IOC_RULESIZE(ioc_rule));
}

static int
ipfw_ctl_get_modules(struct sockopt *sopt)
{
	int i;
	struct ipfw_module *mod;
	char module_str[1024];
	memset(module_str,0,1024);
	for (i = 0, mod = ipfw_modules; i < MAX_MODULE; i++, mod++) {
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
static int
ipfw_ctl_get_rules(struct sockopt *sopt)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_state_context *state_ctx;
	struct ip_fw *rule;
	struct ip_fw_state *state;
	void *bp;
	size_t size;
	int i, j, state_count = 0;

	size = static_ioc_len;
	for (i = 0; i < ncpus; i++) {
		for (j = 0; j < ctx->state_hash_size; j++) {
			state_ctx = &ipfw_ctx[i]->state_ctx[j];
			state_count += state_ctx->count;
		}
	}
	if (state_count > 0) {
		size += state_count * sizeof(struct ipfw_ioc_state);
	}

	if (sopt->sopt_valsize < size) {
		/* XXX TODO sopt_val is not big enough */
		bzero(sopt->sopt_val, sopt->sopt_valsize);
		return 0;
	}

	sopt->sopt_valsize = size;
	bp = sopt->sopt_val;

	for (rule = ctx->ipfw_rule_chain; rule; rule = rule->next) {
		bp = ipfw_copy_rule(rule, bp);
	}
	if (state_count > 0 ) {
		for (i = 0; i < ncpus; i++) {
			for (j = 0; j < ctx->state_hash_size; j++) {
				state_ctx = &ipfw_ctx[i]->state_ctx[j];
				state = state_ctx->state;
				while(state != NULL) {
					bp = ipfw_copy_state(state, bp, i);
					state = state->next;
				}
			}
		}
	}
	return 0;
}

static void
ipfw_set_disable_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

	ctx->ipfw_set_disable = lmsg->u.ms_result32;

	ifnet_forwardmsg(lmsg, mycpuid + 1);
}

static void
ipfw_ctl_set_disable(uint32_t disable, uint32_t enable)
{
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg;
	uint32_t set_disable;

	/* IPFW_DEFAULT_SET is always enabled */
	enable |= (1 << IPFW_DEFAULT_SET);
	set_disable = (ipfw_ctx[mycpuid]->ipfw_set_disable | disable) & ~enable;

	bzero(&nmsg, sizeof(nmsg));
	netmsg_init(&nmsg, NULL, &curthread->td_msgport,
			0, ipfw_set_disable_dispatch);
	lmsg = &nmsg.lmsg;
	lmsg->u.ms_result32 = set_disable;

	ifnet_domsg(lmsg, 0);
}


/*
 * ipfw_ctl_x - extended version of ipfw_ctl
 * remove the x_header, and adjust the sopt_name,sopt_val and sopt_valsize.
 */
int
ipfw_ctl_x(struct sockopt *sopt)
{
	ip_fw_x_header *x_header;
	x_header = (ip_fw_x_header *)(sopt->sopt_val);
	sopt->sopt_name = x_header->opcode;
	sopt->sopt_valsize -= sizeof(ip_fw_x_header);
	bcopy(++x_header, sopt->sopt_val, sopt->sopt_valsize);
	return ipfw_ctl(sopt);
}


/**
 * {set|get}sockopt parser.
 */
static int
ipfw_ctl(struct sockopt *sopt)
{
	int error, rulenum;
	uint32_t *masks;
	size_t size;

	error = 0;
	switch (sopt->sopt_name) {
		case IP_FW_X:
			ipfw_ctl_x(sopt);
			break;
		case IP_FW_GET:
			error = ipfw_ctl_get_rules(sopt);
			break;
		case IP_FW_MODULE:
			error = ipfw_ctl_get_modules(sopt);
			break;

		case IP_FW_FLUSH:
			ipfw_ctl_flush_rule(0);
			break;

		case IP_FW_ADD:
			error = ipfw_ctl_add_rule(sopt);
			break;

		case IP_FW_DEL:
			/*
			 * IP_FW_DEL is used for deleting single rules or sets,
			 * and (ab)used to atomically manipulate sets.
			 * Argument size is used to distinguish between the two:
			 *	sizeof(uint32_t)
			 *	delete single rule or set of rules,
			 *	or reassign rules (or sets) to a different set.
			 *	2 * sizeof(uint32_t)
			 *	atomic disable/enable sets.
			 *	first uint32_t contains sets to be disabled,
			 *	second uint32_t contains sets to be enabled.
			 */
			masks = sopt->sopt_val;
			size = sopt->sopt_valsize;
			if (size == sizeof(*masks)) {
				/*
				 * Delete or reassign static rule
				 */
				error = ipfw_ctl_alter(masks[0]);
			} else if (size == (2 * sizeof(*masks))) {
				/*
				 * Set enable/disable
				 */
				ipfw_ctl_set_disable(masks[0], masks[1]);
			} else {
				error = EINVAL;
			}
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
			error = ipfw_ctl_zero_entry(rulenum,
					sopt->sopt_name == IP_FW_RESETLOG);
			break;
		case IP_FW_NAT_CFG:
			error = ipfw_nat_cfg_ptr(sopt);
			break;
		case IP_FW_NAT_DEL:
			error = ipfw_nat_del_ptr(sopt);
			break;
		case IP_FW_NAT_FLUSH:
			error = ipfw_nat_flush_ptr(sopt);
			break;
		case IP_FW_NAT_GET:
			error = ipfw_nat_get_cfg_ptr(sopt);
			break;
		case IP_FW_NAT_LOG:
			error = ipfw_nat_get_log_ptr(sopt);
			break;
		case IP_DUMMYNET_GET:
		case IP_DUMMYNET_CONFIGURE:
		case IP_DUMMYNET_DEL:
		case IP_DUMMYNET_FLUSH:
			error = ip_dn_sockopt(sopt);
			break;
		case IP_FW_STATE_ADD:
			error = ipfw_ctl_add_state(sopt);
			break;
		case IP_FW_STATE_DEL:
			error = ipfw_ctl_delete_state(sopt);
			break;
		case IP_FW_STATE_FLUSH:
			error = ipfw_ctl_flush_state(sopt);
			break;
		default:
			kprintf("ipfw_ctl invalid option %d\n", sopt->sopt_name);
			error = EINVAL;
	}
	return error;
}

static int
ipfw_check_in(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir)
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
	ret = ipfw_chk(&args);
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
			ipfw_dummynet_io(m, args.cookie, DN_TO_IP_IN, &args);
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
			panic("unknown ipfw return value: %d", ret);
	}
back:
	*m0 = m;
	return error;
}

static int
ipfw_check_out(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir)
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
	ret = ipfw_chk(&args);
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
			ipfw_dummynet_io(m, args.cookie, DN_TO_IP_OUT, &args);
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
			panic("unknown ipfw return value: %d", ret);
	}
back:
	*m0 = m;
	return error;
}

static void
ipfw_hook(void)
{
	struct pfil_head *pfh;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	pfh = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh == NULL)
		return;

	pfil_add_hook(ipfw_check_in, NULL, PFIL_IN | PFIL_MPSAFE, pfh);
	pfil_add_hook(ipfw_check_out, NULL, PFIL_OUT | PFIL_MPSAFE, pfh);
}

static void
ipfw_dehook(void)
{
	struct pfil_head *pfh;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);

	pfh = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh == NULL)
		return;

	pfil_remove_hook(ipfw_check_in, NULL, PFIL_IN, pfh);
	pfil_remove_hook(ipfw_check_out, NULL, PFIL_OUT, pfh);
}

static void
ipfw_sysctl_enable_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	int enable = lmsg->u.ms_result;

	if (fw_enable == enable)
		goto reply;

	fw_enable = enable;
	if (fw_enable)
		ipfw_hook();
	else
		ipfw_dehook();
reply:
	lwkt_replymsg(lmsg, 0);
}

static int
ipfw_sysctl_enable(SYSCTL_HANDLER_ARGS)
{
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg;
	int enable, error;

	enable = fw_enable;
	error = sysctl_handle_int(oidp, &enable, 0, req);
	if (error || req->newptr == NULL)
		return error;

	netmsg_init(&nmsg, NULL, &curthread->td_msgport,
			0, ipfw_sysctl_enable_dispatch);
	lmsg = &nmsg.lmsg;
	lmsg->u.ms_result = enable;

	return lwkt_domsg(IPFW_CFGPORT, lmsg, 0);
}

static int
ipfw_sysctl_autoinc_step(SYSCTL_HANDLER_ARGS)
{
	return sysctl_int_range(oidp, arg1, arg2, req,
			IPFW_AUTOINC_STEP_MIN, IPFW_AUTOINC_STEP_MAX);
}


static void
ipfw_ctx_init_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw *fwmsg = (struct netmsg_ipfw *)nmsg;
	struct ipfw_context *ctx;
	struct ip_fw *def_rule;

	if (mycpuid == 0 ) {
		ipfw_nat_ctx = kmalloc(sizeof(struct ipfw_nat_context),
				M_IPFW2, M_WAITOK | M_ZERO);
	}

	ctx = kmalloc(sizeof(struct ipfw_context), M_IPFW2, M_WAITOK | M_ZERO);
	ipfw_ctx[mycpuid] = ctx;

	def_rule = kmalloc(sizeof(struct ip_fw), M_IPFW2, M_WAITOK | M_ZERO);
	def_rule->act_ofs = 0;
	def_rule->rulenum = IPFW_DEFAULT_RULE;
	def_rule->cmd_len = 2;
	def_rule->set = IPFW_DEFAULT_SET;

	def_rule->cmd[0].len = LEN_OF_IPFWINSN;
	def_rule->cmd[0].module = MODULE_BASIC_ID;
#ifdef IPFIREWALL_DEFAULT_TO_ACCEPT
	def_rule->cmd[0].opcode = O_BASIC_ACCEPT;
#else
	def_rule->cmd[0].opcode = O_BASIC_DENY;
#endif

	/* Install the default rule */
	ctx->ipfw_default_rule = def_rule;
	ctx->ipfw_rule_chain = def_rule;

	/* if sibiling in last CPU is exists, then it's sibling should be current rule */
	if (fwmsg->sibling != NULL) {
		fwmsg->sibling->sibling = def_rule;
	}
	/* prepare for next CPU */
	fwmsg->sibling = def_rule;

	/* Statistics only need to be updated once */
	if (mycpuid == 0)
		ipfw_inc_static_count(def_rule);

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
ipfw_init_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw fwmsg;
	int error = 0;
	if (IPFW_LOADED) {
		kprintf("IP firewall already loaded\n");
		error = EEXIST;
		goto reply;
	}

	bzero(&fwmsg, sizeof(fwmsg));
	netmsg_init(&fwmsg.base, NULL, &curthread->td_msgport,
			0, ipfw_ctx_init_dispatch);
	ifnet_domsg(&fwmsg.base.lmsg, 0);

	ip_fw_chk_ptr = ipfw_chk;
	ip_fw_ctl_x_ptr = ipfw_ctl_x;
	ip_fw_dn_io_ptr = ipfw_dummynet_io;

	kprintf("ipfw2 initialized, default to %s, logging ",
		(int)(ipfw_ctx[mycpuid]->ipfw_default_rule->cmd[0].opcode) ==
		O_BASIC_ACCEPT ? "accept" : "deny");

#ifdef IPFIREWALL_VERBOSE
	fw_verbose = 1;
#endif
#ifdef IPFIREWALL_VERBOSE_LIMIT
	verbose_limit = IPFIREWALL_VERBOSE_LIMIT;
#endif
	if (fw_verbose == 0) {
		kprintf("disabled ");
	} else if (verbose_limit == 0) {
		kprintf("unlimited ");
	} else {
		kprintf("limited to %d packets/entry by default ",
				verbose_limit);
	}
	ip_fw_loaded = 1;
	if (fw_enable)
		ipfw_hook();
reply:
	lwkt_replymsg(&nmsg->lmsg, error);
}

static int
ipfw_init(void)
{
	struct netmsg_base smsg;
	init_module();
	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, ipfw_init_dispatch);
	return lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
}

#ifdef KLD_MODULE

static void
ipfw_fini_dispatch(netmsg_t nmsg)
{
	int error = 0, cpu;

	ip_fw_loaded = 0;

	ipfw_dehook();
	netmsg_service_sync();
	ip_fw_chk_ptr = NULL;
	ip_fw_ctl_x_ptr = NULL;
	ip_fw_dn_io_ptr = NULL;
	ipfw_ctl_flush_rule(1 /* kill default rule */);
	/* Free pre-cpu context */
	for (cpu = 0; cpu < ncpus; ++cpu) {
		if (ipfw_ctx[cpu] != NULL) {
			kfree(ipfw_ctx[cpu], M_IPFW2);
			ipfw_ctx[cpu] = NULL;
		}
	}
	kfree(ipfw_nat_ctx,M_IPFW2);
	ipfw_nat_ctx = NULL;
	kprintf("\nIP firewall unloaded ");

	lwkt_replymsg(&nmsg->lmsg, error);
}

static int
ipfw_fini(void)
{
	struct netmsg_base smsg;
	netmsg_init(&smsg, NULL, &curthread->td_msgport,
			0, ipfw_fini_dispatch);
	return lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
}

#endif	/* KLD_MODULE */

static int
ipfw_modevent(module_t mod, int type, void *unused)
{
	int err = 0;

	switch (type) {
		case MOD_LOAD:
			err = ipfw_init();
			break;

		case MOD_UNLOAD:
#ifndef KLD_MODULE
			kprintf("ipfw statically compiled, cannot unload\n");
			err = EBUSY;
#else
			err = ipfw_fini();
#endif
			break;
		default:
			break;
	}
	return err;
}

static moduledata_t ipfwmod = {
	"ipfw2",
	ipfw_modevent,
	0
};
DECLARE_MODULE(ipfw2, ipfwmod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_VERSION(ipfw2, 1);
