/*
 * Copyright (c) 2002 Luigi Rizzo, Universita` di Pisa
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $DragonFly: src/sys/net/ipfw/ip_fw2.c,v 1.53 2008/07/31 11:36:38 sephe Exp $
 */

#define        DEB(x)
#define        DDB(x) x

/*
 * Implement IP packet firewall (new version)
 */

#ifndef KLD_MODULE
#include "opt_ipfw.h"
#include "opt_ipdn.h"
#include "opt_ipdivert.h"
#include "opt_inet.h"
#ifndef INET
#error IPFIREWALL requires INET.
#endif /* INET */
#endif

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
#include <sys/thread2.h>
#include <sys/ucred.h>
#include <sys/in_cksum.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include "ip_fw.h"
#include <net/dummynet/ip_dummynet.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <netinet/if_ether.h> /* XXX for ETHERTYPE_IP */

/*
 * set_disable contains one bit per set value (0..31).
 * If the bit is set, all rules with the corresponding set
 * are disabled. Set 31 is reserved for the default rule
 * and CANNOT be disabled.
 */
static uint32_t set_disable;

static int fw_verbose;
static int verbose_limit;

#ifdef KLD_MODULE
static int ipfw_refcnt;
#endif

static struct callout ipfw_timeout_h;
#define	IPFW_DEFAULT_RULE	65535

/*
 * list of rules for layer 3
 */
static struct ip_fw *layer3_chain;

MALLOC_DEFINE(M_IPFW, "IpFw/IpAcct", "IpFw/IpAcct chain's");

static int fw_debug = 1;
static int autoinc_step = 100; /* bounded to 1..1000 in ipfw_add_rule() */

#ifdef SYSCTL_NODE
SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw, CTLFLAG_RW, 0, "Firewall");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, enable, CTLFLAG_RW,
    &fw_enable, 0, "Enable ipfw");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, autoinc_step, CTLFLAG_RW,
    &autoinc_step, 0, "Rule number autincrement step");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO,one_pass,CTLFLAG_RW,
    &fw_one_pass, 0,
    "Only do a single pass through ipfw when using dummynet(4)");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, debug, CTLFLAG_RW,
    &fw_debug, 0, "Enable printing of debug ip_fw statements");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, verbose, CTLFLAG_RW,
    &fw_verbose, 0, "Log matches to ipfw rules");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, verbose_limit, CTLFLAG_RW,
    &verbose_limit, 0, "Set upper limit of matches of ipfw rules logged");

/*
 * Description of dynamic rules.
 *
 * Dynamic rules are stored in lists accessed through a hash table
 * (ipfw_dyn_v) whose size is curr_dyn_buckets. This value can
 * be modified through the sysctl variable dyn_buckets which is
 * updated when the table becomes empty.
 *
 * XXX currently there is only one list, ipfw_dyn.
 *
 * When a packet is received, its address fields are first masked
 * with the mask defined for the rule, then hashed, then matched
 * against the entries in the corresponding list.
 * Dynamic rules can be used for different purposes:
 *  + stateful rules;
 *  + enforcing limits on the number of sessions;
 *  + in-kernel NAT (not implemented yet)
 *
 * The lifetime of dynamic rules is regulated by dyn_*_lifetime,
 * measured in seconds and depending on the flags.
 *
 * The total number of dynamic rules is stored in dyn_count.
 * The max number of dynamic rules is dyn_max. When we reach
 * the maximum number of rules we do not create anymore. This is
 * done to avoid consuming too much memory, but also too much
 * time when searching on each packet (ideally, we should try instead
 * to put a limit on the length of the list on each bucket...).
 *
 * Each dynamic rule holds a pointer to the parent ipfw rule so
 * we know what action to perform. Dynamic rules are removed when
 * the parent rule is deleted. XXX we should make them survive.
 *
 * There are some limitations with dynamic rules -- we do not
 * obey the 'randomized match', and we do not do multiple
 * passes through the firewall. XXX check the latter!!!
 */
static ipfw_dyn_rule **ipfw_dyn_v = NULL;
static uint32_t dyn_buckets = 256; /* must be power of 2 */
static uint32_t curr_dyn_buckets = 256; /* must be power of 2 */

/*
 * Timeouts for various events in handing dynamic rules.
 */
static uint32_t dyn_ack_lifetime = 300;
static uint32_t dyn_syn_lifetime = 20;
static uint32_t dyn_fin_lifetime = 1;
static uint32_t dyn_rst_lifetime = 1;
static uint32_t dyn_udp_lifetime = 10;
static uint32_t dyn_short_lifetime = 5;

/*
 * Keepalives are sent if dyn_keepalive is set. They are sent every
 * dyn_keepalive_period seconds, in the last dyn_keepalive_interval
 * seconds of lifetime of a rule.
 * dyn_rst_lifetime and dyn_fin_lifetime should be strictly lower
 * than dyn_keepalive_period.
 */

static uint32_t dyn_keepalive_interval = 20;
static uint32_t dyn_keepalive_period = 5;
static uint32_t dyn_keepalive = 1;	/* do send keepalives */

static uint32_t static_count;		/* # of static rules */
static uint32_t static_ioc_len;	/* bytes of static rules */
static uint32_t dyn_count;		/* # of dynamic rules */
static uint32_t dyn_max = 4096;	/* max # of dynamic rules */

SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_buckets, CTLFLAG_RW,
    &dyn_buckets, 0, "Number of dyn. buckets");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, curr_dyn_buckets, CTLFLAG_RD,
    &curr_dyn_buckets, 0, "Current Number of dyn. buckets");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_count, CTLFLAG_RD,
    &dyn_count, 0, "Number of dyn. rules");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_max, CTLFLAG_RW,
    &dyn_max, 0, "Max number of dyn. rules");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, static_count, CTLFLAG_RD,
    &static_count, 0, "Number of static rules");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_ack_lifetime, CTLFLAG_RW,
    &dyn_ack_lifetime, 0, "Lifetime of dyn. rules for acks");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_syn_lifetime, CTLFLAG_RW,
    &dyn_syn_lifetime, 0, "Lifetime of dyn. rules for syn");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_fin_lifetime, CTLFLAG_RW,
    &dyn_fin_lifetime, 0, "Lifetime of dyn. rules for fin");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_rst_lifetime, CTLFLAG_RW,
    &dyn_rst_lifetime, 0, "Lifetime of dyn. rules for rst");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_udp_lifetime, CTLFLAG_RW,
    &dyn_udp_lifetime, 0, "Lifetime of dyn. rules for UDP");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_short_lifetime, CTLFLAG_RW,
    &dyn_short_lifetime, 0, "Lifetime of dyn. rules for other situations");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_keepalive, CTLFLAG_RW,
    &dyn_keepalive, 0, "Enable keepalives for dyn. rules");

#endif /* SYSCTL_NODE */

static struct ip_fw *ip_fw_default_rule;

static ip_fw_chk_t	ipfw_chk;

static __inline int
ipfw_free_rule(struct ip_fw *rule)
{
	KASSERT(rule->refcnt > 0, ("invalid refcnt %u\n", rule->refcnt));
	atomic_subtract_int(&rule->refcnt, 1);
	if (atomic_cmpset_int(&rule->refcnt, 0, 1)) {
		kfree(rule, M_IPFW);
		return 1;
	}
	return 0;
}

static void
ipfw_unref_rule(void *priv)
{
	ipfw_free_rule(priv);
#ifdef KLD_MODULE
	atomic_subtract_int(&ipfw_refcnt, 1);
#endif
}

static __inline void
ipfw_ref_rule(struct ip_fw *rule)
{
#ifdef KLD_MODULE
	atomic_add_int(&ipfw_refcnt, 1);
#endif
	atomic_add_int(&rule->refcnt, 1);
}

/*
 * This macro maps an ip pointer into a layer3 header pointer of type T
 */
#define	L3HDR(T, ip) ((T *)((uint32_t *)(ip) + (ip)->ip_hl))

static __inline int
icmptype_match(struct ip *ip, ipfw_insn_u32 *cmd)
{
	int type = L3HDR(struct icmp,ip)->icmp_type;

	return (type <= ICMP_MAXTYPE && (cmd->d[0] & (1<<type)) );
}

#define TT	( (1 << ICMP_ECHO) | (1 << ICMP_ROUTERSOLICIT) | \
    (1 << ICMP_TSTAMP) | (1 << ICMP_IREQ) | (1 << ICMP_MASKREQ) )

static int
is_icmp_query(struct ip *ip)
{
	int type = L3HDR(struct icmp, ip)->icmp_type;
	return (type <= ICMP_MAXTYPE && (TT & (1<<type)) );
}
#undef TT

/*
 * The following checks use two arrays of 8 or 16 bits to store the
 * bits that we want set or clear, respectively. They are in the
 * low and high half of cmd->arg1 or cmd->d[0].
 *
 * We scan options and store the bits we find set. We succeed if
 *
 *	(want_set & ~bits) == 0 && (want_clear & ~bits) == want_clear
 *
 * The code is sometimes optimized not to store additional variables.
 */

static int
flags_match(ipfw_insn *cmd, uint8_t bits)
{
	u_char want_clear;
	bits = ~bits;

	if ( ((cmd->arg1 & 0xff) & bits) != 0)
		return 0; /* some bits we want set were clear */
	want_clear = (cmd->arg1 >> 8) & 0xff;
	if ( (want_clear & bits) != want_clear)
		return 0; /* some bits we want clear were set */
	return 1;
}

static int
ipopts_match(struct ip *ip, ipfw_insn *cmd)
{
	int optlen, bits = 0;
	u_char *cp = (u_char *)(ip + 1);
	int x = (ip->ip_hl << 2) - sizeof (struct ip);

	for (; x > 0; x -= optlen, cp += optlen) {
		int opt = cp[IPOPT_OPTVAL];

		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			optlen = cp[IPOPT_OLEN];
			if (optlen <= 0 || optlen > x)
				return 0; /* invalid or truncated */
		}
		switch (opt) {

		default:
			break;

		case IPOPT_LSRR:
			bits |= IP_FW_IPOPT_LSRR;
			break;

		case IPOPT_SSRR:
			bits |= IP_FW_IPOPT_SSRR;
			break;

		case IPOPT_RR:
			bits |= IP_FW_IPOPT_RR;
			break;

		case IPOPT_TS:
			bits |= IP_FW_IPOPT_TS;
			break;
		}
	}
	return (flags_match(cmd, bits));
}

static int
tcpopts_match(struct ip *ip, ipfw_insn *cmd)
{
	int optlen, bits = 0;
	struct tcphdr *tcp = L3HDR(struct tcphdr,ip);
	u_char *cp = (u_char *)(tcp + 1);
	int x = (tcp->th_off << 2) - sizeof(struct tcphdr);

	for (; x > 0; x -= optlen, cp += optlen) {
		int opt = cp[0];
		if (opt == TCPOPT_EOL)
			break;
		if (opt == TCPOPT_NOP)
			optlen = 1;
		else {
			optlen = cp[1];
			if (optlen <= 0)
				break;
		}

		switch (opt) {

		default:
			break;

		case TCPOPT_MAXSEG:
			bits |= IP_FW_TCPOPT_MSS;
			break;

		case TCPOPT_WINDOW:
			bits |= IP_FW_TCPOPT_WINDOW;
			break;

		case TCPOPT_SACK_PERMITTED:
		case TCPOPT_SACK:
			bits |= IP_FW_TCPOPT_SACK;
			break;

		case TCPOPT_TIMESTAMP:
			bits |= IP_FW_TCPOPT_TS;
			break;

		case TCPOPT_CC:
		case TCPOPT_CCNEW:
		case TCPOPT_CCECHO:
			bits |= IP_FW_TCPOPT_CC;
			break;
		}
	}
	return (flags_match(cmd, bits));
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
			if (cmd->p.ip.s_addr == ((struct sockaddr_in *)
			    (ia->ifa_addr))->sin_addr.s_addr)
				return(1);	/* match */
		}
	}
	return(0);	/* no match, fail ... */
}

static uint64_t norule_counter;	/* counter for ipfw_log(NULL...) */

#define SNPARGS(buf, len) buf + len, sizeof(buf) > len ? sizeof(buf) - len : 0

/*
 * We enter here when we have a rule with O_LOG.
 * XXX this function alone takes about 2Kbytes of code!
 */
static void
ipfw_log(struct ip_fw *f, u_int hlen, struct ether_header *eh,
	struct mbuf *m, struct ifnet *oif)
{
	char *action;
	int limit_reached = 0;
	char action2[40], proto[48], fragment[28];

	fragment[0] = '\0';
	proto[0] = '\0';

	if (f == NULL) {	/* bogus pkt */
		if (verbose_limit != 0 && norule_counter >= verbose_limit)
			return;
		norule_counter++;
		if (norule_counter == verbose_limit)
			limit_reached = verbose_limit;
		action = "Refuse";
	} else {	/* O_LOG is the first action, find the real one */
		ipfw_insn *cmd = ACTION_PTR(f);
		ipfw_insn_log *l = (ipfw_insn_log *)cmd;

		if (l->max_log != 0 && l->log_left == 0)
			return;
		l->log_left--;
		if (l->log_left == 0)
			limit_reached = l->max_log;
		cmd += F_LEN(cmd);	/* point to first action */
		if (cmd->opcode == O_PROB)
			cmd += F_LEN(cmd);

		action = action2;
		switch (cmd->opcode) {
		case O_DENY:
			action = "Deny";
			break;

		case O_REJECT:
			if (cmd->arg1==ICMP_REJECT_RST)
				action = "Reset";
			else if (cmd->arg1==ICMP_UNREACH_HOST)
				action = "Reject";
			else
				ksnprintf(SNPARGS(action2, 0), "Unreach %d",
					cmd->arg1);
			break;

		case O_ACCEPT:
			action = "Accept";
			break;
		case O_COUNT:
			action = "Count";
			break;
		case O_DIVERT:
			ksnprintf(SNPARGS(action2, 0), "Divert %d",
				cmd->arg1);
			break;
		case O_TEE:
			ksnprintf(SNPARGS(action2, 0), "Tee %d",
				cmd->arg1);
			break;
		case O_SKIPTO:
			ksnprintf(SNPARGS(action2, 0), "SkipTo %d",
				cmd->arg1);
			break;
		case O_PIPE:
			ksnprintf(SNPARGS(action2, 0), "Pipe %d",
				cmd->arg1);
			break;
		case O_QUEUE:
			ksnprintf(SNPARGS(action2, 0), "Queue %d",
				cmd->arg1);
			break;
		case O_FORWARD_IP: {
			ipfw_insn_sa *sa = (ipfw_insn_sa *)cmd;
			int len;

			len = ksnprintf(SNPARGS(action2, 0), "Forward to %s",
				inet_ntoa(sa->sa.sin_addr));
			if (sa->sa.sin_port)
				ksnprintf(SNPARGS(action2, len), ":%d",
				    sa->sa.sin_port);
			}
			break;
		default:
			action = "UNKNOWN";
			break;
		}
	}

	if (hlen == 0) {	/* non-ip */
		ksnprintf(SNPARGS(proto, 0), "MAC");
	} else {
		struct ip *ip = mtod(m, struct ip *);
		/* these three are all aliases to the same thing */
		struct icmp *const icmp = L3HDR(struct icmp, ip);
		struct tcphdr *const tcp = (struct tcphdr *)icmp;
		struct udphdr *const udp = (struct udphdr *)icmp;

		int ip_off, offset, ip_len;

		int len;

		if (eh != NULL) { /* layer 2 packets are as on the wire */
			ip_off = ntohs(ip->ip_off);
			ip_len = ntohs(ip->ip_len);
		} else {
			ip_off = ip->ip_off;
			ip_len = ip->ip_len;
		}
		offset = ip_off & IP_OFFMASK;
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			len = ksnprintf(SNPARGS(proto, 0), "TCP %s",
			    inet_ntoa(ip->ip_src));
			if (offset == 0)
				ksnprintf(SNPARGS(proto, len), ":%d %s:%d",
				    ntohs(tcp->th_sport),
				    inet_ntoa(ip->ip_dst),
				    ntohs(tcp->th_dport));
			else
				ksnprintf(SNPARGS(proto, len), " %s",
				    inet_ntoa(ip->ip_dst));
			break;

		case IPPROTO_UDP:
			len = ksnprintf(SNPARGS(proto, 0), "UDP %s",
				inet_ntoa(ip->ip_src));
			if (offset == 0)
				ksnprintf(SNPARGS(proto, len), ":%d %s:%d",
				    ntohs(udp->uh_sport),
				    inet_ntoa(ip->ip_dst),
				    ntohs(udp->uh_dport));
			else
				ksnprintf(SNPARGS(proto, len), " %s",
				    inet_ntoa(ip->ip_dst));
			break;

		case IPPROTO_ICMP:
			if (offset == 0)
				len = ksnprintf(SNPARGS(proto, 0),
				    "ICMP:%u.%u ",
				    icmp->icmp_type, icmp->icmp_code);
			else
				len = ksnprintf(SNPARGS(proto, 0), "ICMP ");
			len += ksnprintf(SNPARGS(proto, len), "%s",
			    inet_ntoa(ip->ip_src));
			ksnprintf(SNPARGS(proto, len), " %s",
			    inet_ntoa(ip->ip_dst));
			break;

		default:
			len = ksnprintf(SNPARGS(proto, 0), "P:%d %s", ip->ip_p,
			    inet_ntoa(ip->ip_src));
			ksnprintf(SNPARGS(proto, len), " %s",
			    inet_ntoa(ip->ip_dst));
			break;
		}

		if (ip_off & (IP_MF | IP_OFFMASK))
			ksnprintf(SNPARGS(fragment, 0), " (frag %d:%d@%d%s)",
			     ntohs(ip->ip_id), ip_len - (ip->ip_hl << 2),
			     offset << 3,
			     (ip_off & IP_MF) ? "+" : "");
	}
	if (oif || m->m_pkthdr.rcvif)
		log(LOG_SECURITY | LOG_INFO,
		    "ipfw: %d %s %s %s via %s%s\n",
		    f ? f->rulenum : -1,
		    action, proto, oif ? "out" : "in",
		    oif ? oif->if_xname : m->m_pkthdr.rcvif->if_xname,
		    fragment);
	else
		log(LOG_SECURITY | LOG_INFO,
		    "ipfw: %d %s %s [no if info]%s\n",
		    f ? f->rulenum : -1,
		    action, proto, fragment);
	if (limit_reached)
		log(LOG_SECURITY | LOG_NOTICE,
		    "ipfw: limit %d reached on entry %d\n",
		    limit_reached, f ? f->rulenum : -1);
}

#undef SNPARGS

/*
 * IMPORTANT: the hash function for dynamic rules must be commutative
 * in source and destination (ip,port), because rules are bidirectional
 * and we want to find both in the same bucket.
 */
static __inline int
hash_packet(struct ipfw_flow_id *id)
{
	uint32_t i;

	i = (id->dst_ip) ^ (id->src_ip) ^ (id->dst_port) ^ (id->src_port);
	i &= (curr_dyn_buckets - 1);
	return i;
}

/**
 * unlink a dynamic rule from a chain. prev is a pointer to
 * the previous one, q is a pointer to the rule to delete,
 * head is a pointer to the head of the queue.
 * Modifies q and potentially also head.
 */
#define UNLINK_DYN_RULE(prev, head, q) {				\
	ipfw_dyn_rule *old_q = q;					\
									\
	/* remove a refcount to the parent */				\
	if (q->dyn_type == O_LIMIT)					\
		q->parent->count--;					\
	DEB(kprintf("-- unlink entry 0x%08x %d -> 0x%08x %d, %d left\n",	\
		(q->id.src_ip), (q->id.src_port),			\
		(q->id.dst_ip), (q->id.dst_port), dyn_count-1 ); )	\
	if (prev != NULL)						\
		prev->next = q = q->next;				\
	else								\
		head = q = q->next;					\
	KASSERT(dyn_count > 0, ("invalid dyn count %u\n", dyn_count));	\
	dyn_count--;							\
	kfree(old_q, M_IPFW); }

#define TIME_LEQ(a,b)       ((int)((a)-(b)) <= 0)

/**
 * Remove dynamic rules pointing to "rule", or all of them if rule == NULL.
 *
 * If keep_me == NULL, rules are deleted even if not expired,
 * otherwise only expired rules are removed.
 *
 * The value of the second parameter is also used to point to identify
 * a rule we absolutely do not want to remove (e.g. because we are
 * holding a reference to it -- this is the case with O_LIMIT_PARENT
 * rules). The pointer is only used for comparison, so any non-null
 * value will do.
 */
static void
remove_dyn_rule(struct ip_fw *rule, ipfw_dyn_rule *keep_me)
{
	static uint32_t last_remove = 0;

#define FORCE (keep_me == NULL)

	ipfw_dyn_rule *prev, *q;
	int i, pass = 0, max_pass = 0;

	if (ipfw_dyn_v == NULL || dyn_count == 0)
		return;
	/* do not expire more than once per second, it is useless */
	if (!FORCE && last_remove == time_second)
		return;
	last_remove = time_second;

	/*
	 * because O_LIMIT refer to parent rules, during the first pass only
	 * remove child and mark any pending LIMIT_PARENT, and remove
	 * them in a second pass.
	 */
next_pass:
	for (i = 0 ; i < curr_dyn_buckets ; i++) {
		for (prev=NULL, q = ipfw_dyn_v[i] ; q ; ) {
			/*
			 * Logic can become complex here, so we split tests.
			 */
			if (q == keep_me)
				goto next;
			if (rule != NULL && rule != q->rule)
				goto next; /* not the one we are looking for */
			if (q->dyn_type == O_LIMIT_PARENT) {
				/*
				 * handle parent in the second pass,
				 * record we need one.
				 */
				max_pass = 1;
				if (pass == 0)
					goto next;
				if (FORCE && q->count != 0 ) {
					/* XXX should not happen! */
					kprintf( "OUCH! cannot remove rule,"
					     " count %d\n", q->count);
				}
			} else {
				if (!FORCE &&
				    !TIME_LEQ( q->expire, time_second ))
					goto next;
			}
			UNLINK_DYN_RULE(prev, ipfw_dyn_v[i], q);
			continue;
next:
			prev=q;
			q=q->next;
		}
	}
	if (pass++ < max_pass)
		goto next_pass;
}


/**
 * lookup a dynamic rule.
 */
static ipfw_dyn_rule *
lookup_dyn_rule(struct ipfw_flow_id *pkt, int *match_direction,
	struct tcphdr *tcp)
{
	/*
	 * stateful ipfw extensions.
	 * Lookup into dynamic session queue
	 */
#define MATCH_REVERSE	0
#define MATCH_FORWARD	1
#define MATCH_NONE	2
#define MATCH_UNKNOWN	3
	int i, dir = MATCH_NONE;
	ipfw_dyn_rule *prev, *q=NULL;

	if (ipfw_dyn_v == NULL)
		goto done;	/* not found */
	i = hash_packet( pkt );
	for (prev=NULL, q = ipfw_dyn_v[i] ; q != NULL ; ) {
		if (q->dyn_type == O_LIMIT_PARENT)
			goto next;
		if (TIME_LEQ( q->expire, time_second)) { /* expire entry */
			UNLINK_DYN_RULE(prev, ipfw_dyn_v[i], q);
			continue;
		}
		if ( pkt->proto == q->id.proto) {
			if (pkt->src_ip == q->id.src_ip &&
			    pkt->dst_ip == q->id.dst_ip &&
			    pkt->src_port == q->id.src_port &&
			    pkt->dst_port == q->id.dst_port ) {
				dir = MATCH_FORWARD;
				break;
			}
			if (pkt->src_ip == q->id.dst_ip &&
			    pkt->dst_ip == q->id.src_ip &&
			    pkt->src_port == q->id.dst_port &&
			    pkt->dst_port == q->id.src_port ) {
				dir = MATCH_REVERSE;
				break;
			}
		}
next:
		prev = q;
		q = q->next;
	}
	if (q == NULL)
		goto done; /* q = NULL, not found */

	if ( prev != NULL) { /* found and not in front */
		prev->next = q->next;
		q->next = ipfw_dyn_v[i];
		ipfw_dyn_v[i] = q;
	}
	if (pkt->proto == IPPROTO_TCP) { /* update state according to flags */
		u_char flags = pkt->flags & (TH_FIN|TH_SYN|TH_RST);

#define BOTH_SYN	(TH_SYN | (TH_SYN << 8))
#define BOTH_FIN	(TH_FIN | (TH_FIN << 8))
		q->state |= (dir == MATCH_FORWARD ) ? flags : (flags << 8);
		switch (q->state) {
		case TH_SYN:				/* opening */
			q->expire = time_second + dyn_syn_lifetime;
			break;

		case BOTH_SYN:			/* move to established */
		case BOTH_SYN | TH_FIN :	/* one side tries to close */
		case BOTH_SYN | (TH_FIN << 8) :
 			if (tcp) {
#define _SEQ_GE(a,b) ((int)(a) - (int)(b) >= 0)
			    uint32_t ack = ntohl(tcp->th_ack);
			    if (dir == MATCH_FORWARD) {
				if (q->ack_fwd == 0 || _SEQ_GE(ack, q->ack_fwd))
				    q->ack_fwd = ack;
				else { /* ignore out-of-sequence */
				    break;
				}
			    } else {
				if (q->ack_rev == 0 || _SEQ_GE(ack, q->ack_rev))
				    q->ack_rev = ack;
				else { /* ignore out-of-sequence */
				    break;
				}
			    }
			}
			q->expire = time_second + dyn_ack_lifetime;
			break;

		case BOTH_SYN | BOTH_FIN:	/* both sides closed */
			if (dyn_fin_lifetime >= dyn_keepalive_period)
				dyn_fin_lifetime = dyn_keepalive_period - 1;
			q->expire = time_second + dyn_fin_lifetime;
			break;

		default:
#if 0
			/*
			 * reset or some invalid combination, but can also
			 * occur if we use keep-state the wrong way.
			 */
			if ( (q->state & ((TH_RST << 8)|TH_RST)) == 0)
				kprintf("invalid state: 0x%x\n", q->state);
#endif
			if (dyn_rst_lifetime >= dyn_keepalive_period)
				dyn_rst_lifetime = dyn_keepalive_period - 1;
			q->expire = time_second + dyn_rst_lifetime;
			break;
		}
	} else if (pkt->proto == IPPROTO_UDP) {
		q->expire = time_second + dyn_udp_lifetime;
	} else {
		/* other protocols */
		q->expire = time_second + dyn_short_lifetime;
	}
done:
	if (match_direction)
		*match_direction = dir;
	return q;
}

static void
realloc_dynamic_table(void)
{
	/*
	 * Try reallocation, make sure we have a power of 2 and do
	 * not allow more than 64k entries. In case of overflow,
	 * default to 1024.
	 */

	if (dyn_buckets > 65536)
		dyn_buckets = 1024;
	if ((dyn_buckets & (dyn_buckets-1)) != 0) { /* not a power of 2 */
		dyn_buckets = curr_dyn_buckets; /* reset */
		return;
	}
	curr_dyn_buckets = dyn_buckets;
	if (ipfw_dyn_v != NULL)
		kfree(ipfw_dyn_v, M_IPFW);
	for (;;) {
		ipfw_dyn_v = kmalloc(curr_dyn_buckets * sizeof(ipfw_dyn_rule *),
		       M_IPFW, M_INTWAIT | M_NULLOK | M_ZERO);
		if (ipfw_dyn_v != NULL || curr_dyn_buckets <= 2)
			break;
		curr_dyn_buckets /= 2;
	}
}

/**
 * Install state of type 'type' for a dynamic session.
 * The hash table contains two type of rules:
 * - regular rules (O_KEEP_STATE)
 * - rules for sessions with limited number of sess per user
 *   (O_LIMIT). When they are created, the parent is
 *   increased by 1, and decreased on delete. In this case,
 *   the third parameter is the parent rule and not the chain.
 * - "parent" rules for the above (O_LIMIT_PARENT).
 */
static ipfw_dyn_rule *
add_dyn_rule(struct ipfw_flow_id *id, uint8_t dyn_type, struct ip_fw *rule)
{
	ipfw_dyn_rule *r;
	int i;

	if (ipfw_dyn_v == NULL ||
	    (dyn_count == 0 && dyn_buckets != curr_dyn_buckets)) {
		realloc_dynamic_table();
		if (ipfw_dyn_v == NULL)
			return NULL; /* failed ! */
	}
	i = hash_packet(id);

	r = kmalloc(sizeof *r, M_IPFW, M_INTWAIT | M_NULLOK | M_ZERO);
	if (r == NULL) {
		kprintf ("sorry cannot allocate state\n");
		return NULL;
	}

	/* increase refcount on parent, and set pointer */
	if (dyn_type == O_LIMIT) {
		ipfw_dyn_rule *parent = (ipfw_dyn_rule *)rule;
		if ( parent->dyn_type != O_LIMIT_PARENT)
			panic("invalid parent");
		parent->count++;
		r->parent = parent;
		rule = parent->rule;
	}

	r->id = *id;
	r->expire = time_second + dyn_syn_lifetime;
	r->rule = rule;
	r->dyn_type = dyn_type;
	r->pcnt = r->bcnt = 0;
	r->count = 0;

	r->bucket = i;
	r->next = ipfw_dyn_v[i];
	ipfw_dyn_v[i] = r;
	dyn_count++;
	DEB(kprintf("-- add dyn entry ty %d 0x%08x %d -> 0x%08x %d, total %d\n",
	   dyn_type,
	   (r->id.src_ip), (r->id.src_port),
	   (r->id.dst_ip), (r->id.dst_port),
	   dyn_count ); )
	return r;
}

/**
 * lookup dynamic parent rule using pkt and rule as search keys.
 * If the lookup fails, then install one.
 */
static ipfw_dyn_rule *
lookup_dyn_parent(struct ipfw_flow_id *pkt, struct ip_fw *rule)
{
	ipfw_dyn_rule *q;
	int i;

	if (ipfw_dyn_v) {
		i = hash_packet( pkt );
		for (q = ipfw_dyn_v[i] ; q != NULL ; q=q->next)
			if (q->dyn_type == O_LIMIT_PARENT &&
			    rule== q->rule &&
			    pkt->proto == q->id.proto &&
			    pkt->src_ip == q->id.src_ip &&
			    pkt->dst_ip == q->id.dst_ip &&
			    pkt->src_port == q->id.src_port &&
			    pkt->dst_port == q->id.dst_port) {
				q->expire = time_second + dyn_short_lifetime;
				DEB(kprintf("lookup_dyn_parent found 0x%p\n",q);)
				return q;
			}
	}
	return add_dyn_rule(pkt, O_LIMIT_PARENT, rule);
}

/**
 * Install dynamic state for rule type cmd->o.opcode
 *
 * Returns 1 (failure) if state is not installed because of errors or because
 * session limitations are enforced.
 */
static int
install_state(struct ip_fw *rule, ipfw_insn_limit *cmd,
	struct ip_fw_args *args)
{
	static int last_log;

	ipfw_dyn_rule *q;

	DEB(kprintf("-- install state type %d 0x%08x %u -> 0x%08x %u\n",
	    cmd->o.opcode,
	    (args->f_id.src_ip), (args->f_id.src_port),
	    (args->f_id.dst_ip), (args->f_id.dst_port) );)

	q = lookup_dyn_rule(&args->f_id, NULL, NULL);

	if (q != NULL) { /* should never occur */
		if (last_log != time_second) {
			last_log = time_second;
			kprintf(" install_state: entry already present, done\n");
		}
		return 0;
	}

	if (dyn_count >= dyn_max)
		/*
		 * Run out of slots, try to remove any expired rule.
		 */
		remove_dyn_rule(NULL, (ipfw_dyn_rule *)1);

	if (dyn_count >= dyn_max) {
		if (last_log != time_second) {
			last_log = time_second;
			kprintf("install_state: Too many dynamic rules\n");
		}
		return 1; /* cannot install, notify caller */
	}

	switch (cmd->o.opcode) {
	case O_KEEP_STATE: /* bidir rule */
		add_dyn_rule(&args->f_id, O_KEEP_STATE, rule);
		break;

	case O_LIMIT: /* limit number of sessions */
	    {
		uint16_t limit_mask = cmd->limit_mask;
		struct ipfw_flow_id id;
		ipfw_dyn_rule *parent;

		DEB(kprintf("installing dyn-limit rule %d\n", cmd->conn_limit);)

		id.dst_ip = id.src_ip = 0;
		id.dst_port = id.src_port = 0;
		id.proto = args->f_id.proto;

		if (limit_mask & DYN_SRC_ADDR)
			id.src_ip = args->f_id.src_ip;
		if (limit_mask & DYN_DST_ADDR)
			id.dst_ip = args->f_id.dst_ip;
		if (limit_mask & DYN_SRC_PORT)
			id.src_port = args->f_id.src_port;
		if (limit_mask & DYN_DST_PORT)
			id.dst_port = args->f_id.dst_port;
		parent = lookup_dyn_parent(&id, rule);
		if (parent == NULL) {
			kprintf("add parent failed\n");
			return 1;
		}
		if (parent->count >= cmd->conn_limit) {
			/*
			 * See if we can remove some expired rule.
			 */
			remove_dyn_rule(rule, parent);
			if (parent->count >= cmd->conn_limit) {
				if (fw_verbose && last_log != time_second) {
					last_log = time_second;
					log(LOG_SECURITY | LOG_DEBUG,
					    "drop session, too many entries\n");
				}
				return 1;
			}
		}
		add_dyn_rule(&args->f_id, O_LIMIT, (struct ip_fw *)parent);
	    }
		break;
	default:
		kprintf("unknown dynamic rule type %u\n", cmd->o.opcode);
		return 1;
	}
	lookup_dyn_rule(&args->f_id, NULL, NULL); /* XXX just set lifetime */
	return 0;
}

/*
 * Transmit a TCP packet, containing either a RST or a keepalive.
 * When flags & TH_RST, we are sending a RST packet, because of a
 * "reset" action matched the packet.
 * Otherwise we are sending a keepalive, and flags & TH_
 */
static void
send_pkt(struct ipfw_flow_id *id, uint32_t seq, uint32_t ack, int flags)
{
	struct mbuf *m;
	struct ip *ip;
	struct tcphdr *tcp;
	struct route sro;	/* fake route */

	MGETHDR(m, MB_DONTWAIT, MT_HEADER);
	if (m == 0)
		return;
	m->m_pkthdr.rcvif = NULL;
	m->m_pkthdr.len = m->m_len = sizeof(struct ip) + sizeof(struct tcphdr);
	m->m_data += max_linkhdr;

	ip = mtod(m, struct ip *);
	bzero(ip, m->m_len);
	tcp = (struct tcphdr *)(ip + 1); /* no IP options */
	ip->ip_p = IPPROTO_TCP;
	tcp->th_off = 5;

	/*
	 * Assume we are sending a RST (or a keepalive in the reverse
	 * direction), swap src and destination addresses and ports.
	 */
	ip->ip_src.s_addr = htonl(id->dst_ip);
	ip->ip_dst.s_addr = htonl(id->src_ip);
	tcp->th_sport = htons(id->dst_port);
	tcp->th_dport = htons(id->src_port);
	if (flags & TH_RST) {	/* we are sending a RST */
		if (flags & TH_ACK) {
			tcp->th_seq = htonl(ack);
			tcp->th_ack = htonl(0);
			tcp->th_flags = TH_RST;
		} else {
			if (flags & TH_SYN)
				seq++;
			tcp->th_seq = htonl(0);
			tcp->th_ack = htonl(seq);
			tcp->th_flags = TH_RST | TH_ACK;
		}
	} else {
		/*
		 * We are sending a keepalive. flags & TH_SYN determines
		 * the direction, forward if set, reverse if clear.
		 * NOTE: seq and ack are always assumed to be correct
		 * as set by the caller. This may be confusing...
		 */
		if (flags & TH_SYN) {
			/*
			 * we have to rewrite the correct addresses!
			 */
			ip->ip_dst.s_addr = htonl(id->dst_ip);
			ip->ip_src.s_addr = htonl(id->src_ip);
			tcp->th_dport = htons(id->dst_port);
			tcp->th_sport = htons(id->src_port);
		}
		tcp->th_seq = htonl(seq);
		tcp->th_ack = htonl(ack);
		tcp->th_flags = TH_ACK;
	}

	/*
	 * set ip_len to the payload size so we can compute
	 * the tcp checksum on the pseudoheader
	 * XXX check this, could save a couple of words ?
	 */
	ip->ip_len = htons(sizeof(struct tcphdr));
	tcp->th_sum = in_cksum(m, m->m_pkthdr.len);

	/*
	 * now fill fields left out earlier
	 */
	ip->ip_ttl = ip_defttl;
	ip->ip_len = m->m_pkthdr.len;

	bzero(&sro, sizeof(sro));
	ip_rtaddr(ip->ip_dst, &sro);

	m->m_pkthdr.fw_flags |= IPFW_MBUF_GENERATED;
	ip_output(m, NULL, &sro, 0, NULL, NULL);
	if (sro.ro_rt)
		RTFREE(sro.ro_rt);
}

/*
 * sends a reject message, consuming the mbuf passed as an argument.
 */
static void
send_reject(struct ip_fw_args *args, int code, int offset, int ip_len)
{

	if (code != ICMP_REJECT_RST) { /* Send an ICMP unreach */
		/* We need the IP header in host order for icmp_error(). */
		if (args->eh != NULL) {
			struct ip *ip = mtod(args->m, struct ip *);
			ip->ip_len = ntohs(ip->ip_len);
			ip->ip_off = ntohs(ip->ip_off);
		}
		icmp_error(args->m, ICMP_UNREACH, code, 0L, 0);
	} else if (offset == 0 && args->f_id.proto == IPPROTO_TCP) {
		struct tcphdr *const tcp =
		    L3HDR(struct tcphdr, mtod(args->m, struct ip *));
		if ( (tcp->th_flags & TH_RST) == 0)
			send_pkt(&(args->f_id), ntohl(tcp->th_seq),
				ntohl(tcp->th_ack),
				tcp->th_flags | TH_RST);
		m_freem(args->m);
	} else
		m_freem(args->m);
	args->m = NULL;
}

/**
 *
 * Given an ip_fw *, lookup_next_rule will return a pointer
 * to the next rule, which can be either the jump
 * target (for skipto instructions) or the next one in the list (in
 * all other cases including a missing jump target).
 * The result is also written in the "next_rule" field of the rule.
 * Backward jumps are not allowed, so start looking from the next
 * rule...
 *
 * This never returns NULL -- in case we do not have an exact match,
 * the next rule is returned. When the ruleset is changed,
 * pointers are flushed so we are always correct.
 */

static struct ip_fw *
lookup_next_rule(struct ip_fw *me)
{
	struct ip_fw *rule = NULL;
	ipfw_insn *cmd;

	/* look for action, in case it is a skipto */
	cmd = ACTION_PTR(me);
	if (cmd->opcode == O_LOG)
		cmd += F_LEN(cmd);
	if ( cmd->opcode == O_SKIPTO )
		for (rule = me->next; rule ; rule = rule->next)
			if (rule->rulenum >= cmd->arg1)
				break;
	if (rule == NULL)			/* failure or not a skipto */
		rule = me->next;
	me->next_rule = rule;
	return rule;
}

/*
 * The main check routine for the firewall.
 *
 * All arguments are in args so we can modify them and return them
 * back to the caller.
 *
 * Parameters:
 *
 *	args->m	(in/out) The packet; we set to NULL when/if we nuke it.
 *		Starts with the IP header.
 *	args->eh (in)	Mac header if present, or NULL for layer3 packet.
 *	args->oif	Outgoing interface, or NULL if packet is incoming.
 *		The incoming interface is in the mbuf. (in)
 *
 *	args->rule	Pointer to the last matching rule (in/out)
 *	args->next_hop	Socket we are forwarding to (out).
 *	args->f_id	Addresses grabbed from the packet (out)
 *
 * Return value:
 *
 *	IP_FW_PORT_DENY_FLAG	the packet must be dropped.
 *	0	The packet is to be accepted and routed normally OR
 *      	the packet was denied/rejected and has been dropped;
 *		in the latter case, *m is equal to NULL upon return.
 *	port	Divert the packet to port, with these caveats:
 *
 *		- If IP_FW_PORT_TEE_FLAG is set, tee the packet instead
 *		  of diverting it (ie, 'ipfw tee').
 *
 *		- If IP_FW_PORT_DYNT_FLAG is set, interpret the lower
 *		  16 bits as a dummynet pipe number instead of diverting
 */

static int
ipfw_chk(struct ip_fw_args *args)
{
	/*
	 * Local variables hold state during the processing of a packet.
	 *
	 * IMPORTANT NOTE: to speed up the processing of rules, there
	 * are some assumption on the values of the variables, which
	 * are documented here. Should you change them, please check
	 * the implementation of the various instructions to make sure
	 * that they still work.
	 *
	 * args->eh	The MAC header. It is non-null for a layer2
	 *	packet, it is NULL for a layer-3 packet.
	 *
	 * m | args->m	Pointer to the mbuf, as received from the caller.
	 *	It may change if ipfw_chk() does an m_pullup, or if it
	 *	consumes the packet because it calls send_reject().
	 *	XXX This has to change, so that ipfw_chk() never modifies
	 *	or consumes the buffer.
	 * ip	is simply an alias of the value of m, and it is kept
	 *	in sync with it (the packet is	supposed to start with
	 *	the ip header).
	 */
	struct mbuf *m = args->m;
	struct ip *ip = mtod(m, struct ip *);

	/*
	 * oif | args->oif	If NULL, ipfw_chk has been called on the
	 *	inbound path (ether_input, ip_input).
	 *	If non-NULL, ipfw_chk has been called on the outbound path
	 *	(ether_output, ip_output).
	 */
	struct ifnet *oif = args->oif;

	struct ip_fw *f = NULL;		/* matching rule */
	int retval = 0;
	struct m_tag *mtag;

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

	/*
	 * Local copies of addresses. They are only valid if we have
	 * an IP packet.
	 *
	 * proto	The protocol. Set to 0 for non-ip packets,
	 *	or to the protocol read from the packet otherwise.
	 *	proto != 0 means that we have an IPv4 packet.
	 *
	 * src_port, dst_port	port numbers, in HOST format. Only
	 *	valid for TCP and UDP packets.
	 *
	 * src_ip, dst_ip	ip addresses, in NETWORK format.
	 *	Only valid for IPv4 packets.
	 */
	uint8_t proto;
	uint16_t src_port = 0, dst_port = 0;	/* NOTE: host format	*/
	struct in_addr src_ip, dst_ip;		/* NOTE: network format	*/
	uint16_t ip_len = 0;
	int dyn_dir = MATCH_UNKNOWN;
	ipfw_dyn_rule *q = NULL;

	if (m->m_pkthdr.fw_flags & IPFW_MBUF_GENERATED)
		return 0;	/* accept */
	/*
	 * dyn_dir = MATCH_UNKNOWN when rules unchecked,
	 * 	MATCH_NONE when checked and not matched (q = NULL),
	 *	MATCH_FORWARD or MATCH_REVERSE otherwise (q != NULL)
	 */

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

#define PULLUP_TO(len)				\
do {						\
	if (m->m_len < (len)) {			\
		args->m = m = m_pullup(m, (len));\
		if (m == NULL)			\
			goto pullup_failed;	\
		ip = mtod(m, struct ip *);	\
	}					\
} while (0)

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
			return 0;

		/* This rule was deleted */
		if (args->rule->rule_flags & IPFW_RULE_F_INVALID)
			return IP_FW_PORT_DENY_FLAG;

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
		if (mtag != NULL)
			skipto = *(uint16_t *)m_tag_data(mtag);
		else
			skipto = 0;

		f = layer3_chain;
		if (args->eh == NULL && skipto != 0) {
			if (skipto >= IPFW_DEFAULT_RULE)
				return(IP_FW_PORT_DENY_FLAG); /* invalid */
			while (f && f->rulenum <= skipto)
				f = f->next;
			if (f == NULL)	/* drop packet */
				return(IP_FW_PORT_DENY_FLAG);
		}
	}
	if ((mtag = m_tag_find(m, PACKET_TAG_IPFW_DIVERT, NULL)) != NULL)
		m_tag_delete(m, mtag);

	/*
	 * Now scan the rules, and parse microinstructions for each rule.
	 */
	for (; f; f = f->next) {
		int l, cmdlen;
		ipfw_insn *cmd;
		int skip_or; /* skip rest of OR block */

again:
		if (set_disable & (1 << f->set))
			continue;

		skip_or = 0;
		for (l = f->cmd_len, cmd = f->cmd; l > 0;
		     l -= cmdlen, cmd += cmdlen) {
			int match;

			/*
			 * check_body is a jump target used when we find a
			 * CHECK_STATE, and need to jump to the body of
			 * the target rule.
			 */

check_body:
			cmdlen = F_LEN(cmd);
			/*
			 * An OR block (insn_1 || .. || insn_n) has the
			 * F_OR bit set in all but the last instruction.
			 * The first match will set "skip_or", and cause
			 * the following instructions to be skipped until
			 * past the one with the F_OR bit clear.
			 */
			if (skip_or) {		/* skip this instruction */
				if ((cmd->len & F_OR) == 0)
					skip_or = 0;	/* next one is good */
				continue;
			}
			match = 0; /* set to 1 if we succeed */

			switch (cmd->opcode) {
			/*
			 * The first set of opcodes compares the packet's
			 * fields with some pattern, setting 'match' if a
			 * match is found. At the end of the loop there is
			 * logic to deal with F_NOT and F_OR flags associated
			 * with the opcode.
			 */
			case O_NOP:
				match = 1;
				break;

			case O_FORWARD_MAC:
				kprintf("ipfw: opcode %d unimplemented\n",
					cmd->opcode);
				break;

			case O_GID:
			case O_UID:
				/*
				 * We only check offset == 0 && proto != 0,
				 * as this ensures that we have an IPv4
				 * packet with the ports info.
				 */
				if (offset!=0)
					break;
			    {
				struct inpcbinfo *pi;
				int wildcard;
				struct inpcb *pcb;

				if (proto == IPPROTO_TCP) {
					wildcard = 0;
					pi = &tcbinfo[mycpu->gd_cpuid];
				} else if (proto == IPPROTO_UDP) {
					wildcard = 1;
					pi = &udbinfo;
				} else
					break;

				pcb =  (oif) ?
					in_pcblookup_hash(pi,
					    dst_ip, htons(dst_port),
					    src_ip, htons(src_port),
					    wildcard, oif) :
					in_pcblookup_hash(pi,
					    src_ip, htons(src_port),
					    dst_ip, htons(dst_port),
					    wildcard, NULL);

				if (pcb == NULL || pcb->inp_socket == NULL)
					break;

				if (cmd->opcode == O_UID) {
#define socheckuid(a,b)	((a)->so_cred->cr_uid != (b))
					match =
					  !socheckuid(pcb->inp_socket,
					   (uid_t)((ipfw_insn_u32 *)cmd)->d[0]);
#undef socheckuid
				} else  {
					match = groupmember(
					    (uid_t)((ipfw_insn_u32 *)cmd)->d[0],
					    pcb->inp_socket->so_cred);
				}
			    }
				break;

			case O_RECV:
				match = iface_match(m->m_pkthdr.rcvif,
				    (ipfw_insn_if *)cmd);
				break;

			case O_XMIT:
				match = iface_match(oif, (ipfw_insn_if *)cmd);
				break;

			case O_VIA:
				match = iface_match(oif ? oif :
				    m->m_pkthdr.rcvif, (ipfw_insn_if *)cmd);
				break;

			case O_MACADDR2:
				if (args->eh != NULL) {	/* have MAC header */
					uint32_t *want = (uint32_t *)
						((ipfw_insn_mac *)cmd)->addr;
					uint32_t *mask = (uint32_t *)
						((ipfw_insn_mac *)cmd)->mask;
					uint32_t *hdr = (uint32_t *)args->eh;

					match =
					(want[0] == (hdr[0] & mask[0]) &&
					 want[1] == (hdr[1] & mask[1]) &&
					 want[2] == (hdr[2] & mask[2]));
				}
				break;

			case O_MAC_TYPE:
				if (args->eh != NULL) {
					uint16_t t =
					    ntohs(args->eh->ether_type);
					uint16_t *p =
					    ((ipfw_insn_u16 *)cmd)->ports;
					int i;

					/* Special vlan handling */
					if (m->m_flags & M_VLANTAG)
						t = ETHERTYPE_VLAN;

					for (i = cmdlen - 1; !match && i > 0;
					     i--, p += 2) {
						match =
						(t >= p[0] && t <= p[1]);
					}
				}
				break;

			case O_FRAG:
				match = (hlen > 0 && offset != 0);
				break;

			case O_IN:	/* "out" is "not in" */
				match = (oif == NULL);
				break;

			case O_LAYER2:
				match = (args->eh != NULL);
				break;

			case O_PROTO:
				/*
				 * We do not allow an arg of 0 so the
				 * check of "proto" only suffices.
				 */
				match = (proto == cmd->arg1);
				break;

			case O_IP_SRC:
				match = (hlen > 0 &&
				    ((ipfw_insn_ip *)cmd)->addr.s_addr ==
				    src_ip.s_addr);
				break;

			case O_IP_SRC_MASK:
				match = (hlen > 0 &&
				    ((ipfw_insn_ip *)cmd)->addr.s_addr ==
				     (src_ip.s_addr &
				     ((ipfw_insn_ip *)cmd)->mask.s_addr));
				break;

			case O_IP_SRC_ME:
				if (hlen > 0) {
					struct ifnet *tif;

					tif = INADDR_TO_IFP(&src_ip);
					match = (tif != NULL);
				}
				break;

			case O_IP_DST_SET:
			case O_IP_SRC_SET:
				if (hlen > 0) {
					uint32_t *d = (uint32_t *)(cmd+1);
					uint32_t addr =
					    cmd->opcode == O_IP_DST_SET ?
						args->f_id.dst_ip :
						args->f_id.src_ip;

					if (addr < d[0])
						break;
					addr -= d[0]; /* subtract base */
					match =
					(addr < cmd->arg1) &&
					 (d[1 + (addr >> 5)] &
					  (1 << (addr & 0x1f)));
				}
				break;

			case O_IP_DST:
				match = (hlen > 0 &&
				    ((ipfw_insn_ip *)cmd)->addr.s_addr ==
				    dst_ip.s_addr);
				break;

			case O_IP_DST_MASK:
				match = (hlen > 0) &&
				    (((ipfw_insn_ip *)cmd)->addr.s_addr ==
				     (dst_ip.s_addr &
				     ((ipfw_insn_ip *)cmd)->mask.s_addr));
				break;

			case O_IP_DST_ME:
				if (hlen > 0) {
					struct ifnet *tif;

					tif = INADDR_TO_IFP(&dst_ip);
					match = (tif != NULL);
				}
				break;

			case O_IP_SRCPORT:
			case O_IP_DSTPORT:
				/*
				 * offset == 0 && proto != 0 is enough
				 * to guarantee that we have an IPv4
				 * packet with port info.
				 */
				if ((proto==IPPROTO_UDP || proto==IPPROTO_TCP)
				    && offset == 0) {
					uint16_t x =
					    (cmd->opcode == O_IP_SRCPORT) ?
						src_port : dst_port ;
					uint16_t *p =
					    ((ipfw_insn_u16 *)cmd)->ports;
					int i;

					for (i = cmdlen - 1; !match && i > 0;
					     i--, p += 2) {
						match =
						(x >= p[0] && x <= p[1]);
					}
				}
				break;

			case O_ICMPTYPE:
				match = (offset == 0 && proto==IPPROTO_ICMP &&
				    icmptype_match(ip, (ipfw_insn_u32 *)cmd));
				break;

			case O_IPOPT:
				match = (hlen > 0 && ipopts_match(ip, cmd));
				break;

			case O_IPVER:
				match = (hlen > 0 && cmd->arg1 == ip->ip_v);
				break;

			case O_IPTTL:
				match = (hlen > 0 && cmd->arg1 == ip->ip_ttl);
				break;

			case O_IPID:
				match = (hlen > 0 &&
				    cmd->arg1 == ntohs(ip->ip_id));
				break;

			case O_IPLEN:
				match = (hlen > 0 && cmd->arg1 == ip_len);
				break;

			case O_IPPRECEDENCE:
				match = (hlen > 0 &&
				    (cmd->arg1 == (ip->ip_tos & 0xe0)));
				break;

			case O_IPTOS:
				match = (hlen > 0 &&
				    flags_match(cmd, ip->ip_tos));
				break;

			case O_TCPFLAGS:
				match = (proto == IPPROTO_TCP && offset == 0 &&
				    flags_match(cmd,
					L3HDR(struct tcphdr,ip)->th_flags));
				break;

			case O_TCPOPTS:
				match = (proto == IPPROTO_TCP && offset == 0 &&
				    tcpopts_match(ip, cmd));
				break;

			case O_TCPSEQ:
				match = (proto == IPPROTO_TCP && offset == 0 &&
				    ((ipfw_insn_u32 *)cmd)->d[0] ==
					L3HDR(struct tcphdr,ip)->th_seq);
				break;

			case O_TCPACK:
				match = (proto == IPPROTO_TCP && offset == 0 &&
				    ((ipfw_insn_u32 *)cmd)->d[0] ==
					L3HDR(struct tcphdr,ip)->th_ack);
				break;

			case O_TCPWIN:
				match = (proto == IPPROTO_TCP && offset == 0 &&
				    cmd->arg1 ==
					L3HDR(struct tcphdr,ip)->th_win);
				break;

			case O_ESTAB:
				/* reject packets which have SYN only */
				/* XXX should i also check for TH_ACK ? */
				match = (proto == IPPROTO_TCP && offset == 0 &&
				    (L3HDR(struct tcphdr,ip)->th_flags &
				     (TH_RST | TH_ACK | TH_SYN)) != TH_SYN);
				break;

			case O_LOG:
				if (fw_verbose)
					ipfw_log(f, hlen, args->eh, m, oif);
				match = 1;
				break;

			case O_PROB:
				match = (krandom() <
					((ipfw_insn_u32 *)cmd)->d[0]);
				break;

			/*
			 * The second set of opcodes represents 'actions',
			 * i.e. the terminal part of a rule once the packet
			 * matches all previous patterns.
			 * Typically there is only one action for each rule,
			 * and the opcode is stored at the end of the rule
			 * (but there are exceptions -- see below).
			 *
			 * In general, here we set retval and terminate the
			 * outer loop (would be a 'break 3' in some language,
			 * but we need to do a 'goto done').
			 *
			 * Exceptions:
			 * O_COUNT and O_SKIPTO actions:
			 *   instead of terminating, we jump to the next rule
			 *   ('goto next_rule', equivalent to a 'break 2'),
			 *   or to the SKIPTO target ('goto again' after
			 *   having set f, cmd and l), respectively.
			 *
			 * O_LIMIT and O_KEEP_STATE: these opcodes are
			 *   not real 'actions', and are stored right
			 *   before the 'action' part of the rule.
			 *   These opcodes try to install an entry in the
			 *   state tables; if successful, we continue with
			 *   the next opcode (match=1; break;), otherwise
			 *   the packet *   must be dropped
			 *   ('goto done' after setting retval);
			 *
			 * O_PROBE_STATE and O_CHECK_STATE: these opcodes
			 *   cause a lookup of the state table, and a jump
			 *   to the 'action' part of the parent rule
			 *   ('goto check_body') if an entry is found, or
			 *   (CHECK_STATE only) a jump to the next rule if
			 *   the entry is not found ('goto next_rule').
			 *   The result of the lookup is cached to make
			 *   further instances of these opcodes are
			 *   effectively NOPs.
			 */
			case O_LIMIT:
			case O_KEEP_STATE:
				if (install_state(f,
				    (ipfw_insn_limit *)cmd, args)) {
					retval = IP_FW_PORT_DENY_FLAG;
					goto done; /* error/limit violation */
				}
				match = 1;
				break;

			case O_PROBE_STATE:
			case O_CHECK_STATE:
				/*
				 * dynamic rules are checked at the first
				 * keep-state or check-state occurrence,
				 * with the result being stored in dyn_dir.
				 * The compiler introduces a PROBE_STATE
				 * instruction for us when we have a
				 * KEEP_STATE (because PROBE_STATE needs
				 * to be run first).
				 */
				if (dyn_dir == MATCH_UNKNOWN &&
				    (q = lookup_dyn_rule(&args->f_id,
				     &dyn_dir, proto == IPPROTO_TCP ?
					L3HDR(struct tcphdr, ip) : NULL))
					!= NULL) {
					/*
					 * Found dynamic entry, update stats
					 * and jump to the 'action' part of
					 * the parent rule.
					 */
					q->pcnt++;
					q->bcnt += ip_len;
					f = q->rule;
					cmd = ACTION_PTR(f);
					l = f->cmd_len - f->act_ofs;
					goto check_body;
				}
				/*
				 * Dynamic entry not found. If CHECK_STATE,
				 * skip to next rule, if PROBE_STATE just
				 * ignore and continue with next opcode.
				 */
				if (cmd->opcode == O_CHECK_STATE)
					goto next_rule;
				match = 1;
				break;

			case O_ACCEPT:
				retval = 0;	/* accept */
				goto done;

			case O_PIPE:
			case O_QUEUE:
				args->rule = f; /* report matching rule */
				retval = cmd->arg1 | IP_FW_PORT_DYNT_FLAG;
				goto done;

			case O_DIVERT:
			case O_TEE:
				if (args->eh) /* not on layer 2 */
					break;

				mtag = m_tag_get(PACKET_TAG_IPFW_DIVERT,
						 sizeof(uint16_t), MB_DONTWAIT);
				if (mtag == NULL) {
					retval = IP_FW_PORT_DENY_FLAG;
					goto done;
				}
				*(uint16_t *)m_tag_data(mtag) = f->rulenum;
				m_tag_prepend(m, mtag);
				retval = (cmd->opcode == O_DIVERT) ?
				    cmd->arg1 :
				    cmd->arg1 | IP_FW_PORT_TEE_FLAG;
				goto done;

			case O_COUNT:
			case O_SKIPTO:
				f->pcnt++;	/* update stats */
				f->bcnt += ip_len;
				f->timestamp = time_second;
				if (cmd->opcode == O_COUNT)
					goto next_rule;
				/* handle skipto */
				if (f->next_rule == NULL)
					lookup_next_rule(f);
				f = f->next_rule;
				goto again;

			case O_REJECT:
				/*
				 * Drop the packet and send a reject notice
				 * if the packet is not ICMP (or is an ICMP
				 * query), and it is not multicast/broadcast.
				 */
				if (hlen > 0 &&
				    (proto != IPPROTO_ICMP ||
				     is_icmp_query(ip)) &&
				    !(m->m_flags & (M_BCAST|M_MCAST)) &&
				    !IN_MULTICAST(ntohl(dst_ip.s_addr))) {
					send_reject(args, cmd->arg1,
					    offset,ip_len);
					m = args->m;
				}
				/* FALLTHROUGH */
			case O_DENY:
				retval = IP_FW_PORT_DENY_FLAG;
				goto done;

			case O_FORWARD_IP:
				if (args->eh)	/* not valid on layer2 pkts */
					break;
				if (!q || dyn_dir == MATCH_FORWARD) {
					args->next_hop =
					    &((ipfw_insn_sa *)cmd)->sa;
				}
				retval = 0;
				goto done;

			default:
				panic("-- unknown opcode %d\n", cmd->opcode);
			} /* end of switch() on opcodes */

			if (cmd->len & F_NOT)
				match = !match;

			if (match) {
				if (cmd->len & F_OR)
					skip_or = 1;
			} else {
				if (!(cmd->len & F_OR)) /* not an OR block, */
					break;		/* try next rule    */
			}

		}	/* end of inner for, scan opcodes */

next_rule:;		/* try next rule		*/

	}		/* end of outer for, scan rules */
	kprintf("+++ ipfw: ouch!, skip past end of rules, denying packet\n");
	return(IP_FW_PORT_DENY_FLAG);

done:
	/* Update statistics */
	f->pcnt++;
	f->bcnt += ip_len;
	f->timestamp = time_second;
	return retval;

pullup_failed:
	if (fw_verbose)
		kprintf("pullup failed\n");
	return(IP_FW_PORT_DENY_FLAG);
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
	if (cmd->opcode == O_LOG)
		cmd += F_LEN(cmd);
	KASSERT(cmd->opcode == O_PIPE || cmd->opcode == O_QUEUE,
		("Rule is not PIPE or QUEUE, opcode %d\n", cmd->opcode));

	pkt->dn_m = m;
	pkt->dn_flags = (dir & DN_FLAGS_DIR_MASK);
	pkt->ifp = fwa->oif;
	pkt->cpuid = mycpu->gd_cpuid;
	pkt->pipe_nr = pipe_nr;

	id = &fwa->f_id;
	fid = &pkt->id;
	fid->fid_dst_ip = id->dst_ip;
	fid->fid_src_ip = id->src_ip;
	fid->fid_dst_port = id->dst_port;
	fid->fid_src_port = id->src_port;
	fid->fid_proto = id->proto;
	fid->fid_flags = id->flags;

	ipfw_ref_rule(fwa->rule);
	pkt->dn_priv = fwa->rule;
	pkt->dn_unref_priv = ipfw_unref_rule;

	if (cmd->opcode == O_PIPE)
		pkt->dn_flags |= DN_FLAGS_IS_PIPE;

	if (dir == DN_TO_IP_OUT) {
		/*
		 * We need to copy *ro because for ICMP pkts (and maybe
		 * others) the caller passed a pointer into the stack;
		 * dst might also be a pointer into *ro so it needs to
		 * be updated.
		 */
		pkt->ro = *(fwa->ro);
		if (fwa->ro->ro_rt)
			fwa->ro->ro_rt->rt_refcnt++;
		if (fwa->dst == (struct sockaddr_in *)&fwa->ro->ro_dst) {
			/* 'dst' points into 'ro' */
			fwa->dst = (struct sockaddr_in *)&(pkt->ro.ro_dst);
		}
		pkt->dn_dst = fwa->dst;
		pkt->flags = fwa->flags;
	}

	m->m_pkthdr.fw_flags |= DUMMYNET_MBUF_TAGGED;
	ip_dn_queue(m);
}

/*
 * When a rule is added/deleted, clear the next_rule pointers in all rules.
 * These will be reconstructed on the fly as packets are matched.
 * Must be called at splimp().
 */
static void
flush_rule_ptrs(void)
{
	struct ip_fw *rule;

	for (rule = layer3_chain; rule; rule = rule->next)
		rule->next_rule = NULL;
}

static __inline void
ipfw_inc_static_count(struct ip_fw *rule)
{
	static_count++;
	static_ioc_len += IOC_RULESIZE(rule);
}

static __inline void
ipfw_dec_static_count(struct ip_fw *rule)
{
	int l = IOC_RULESIZE(rule);

	KASSERT(static_count > 0, ("invalid static count %u\n", static_count));
	static_count--;

	KASSERT(static_ioc_len >= l,
		("invalid static len %u\n", static_ioc_len));
	static_ioc_len -= l;
}

static struct ip_fw *
ipfw_create_rule(const struct ipfw_ioc_rule *ioc_rule)
{
	struct ip_fw *rule;

	rule = kmalloc(RULESIZE(ioc_rule), M_IPFW, M_WAITOK | M_ZERO);

	rule->act_ofs = ioc_rule->act_ofs;
	rule->cmd_len = ioc_rule->cmd_len;
	rule->rulenum = ioc_rule->rulenum;
	rule->set = ioc_rule->set;
	rule->usr_flags = ioc_rule->usr_flags;

	bcopy(ioc_rule->cmd, rule->cmd, rule->cmd_len * 4 /* XXX */);

	rule->refcnt = 1;

	return rule;
}

/*
 * Add a new rule to the list. Copy the rule into a malloc'ed area, then
 * possibly create a rule number and add the rule to the list.
 * Update the rule_number in the input struct so the caller knows it as well.
 */
static int
ipfw_add_rule(struct ip_fw **head, struct ipfw_ioc_rule *ioc_rule)
{
	struct ip_fw *rule, *f, *prev;

	KKASSERT(*head != NULL);

	rule = ipfw_create_rule(ioc_rule);

	crit_enter();

	/*
	 * If rulenum is 0, find highest numbered rule before the
	 * default rule, and add autoinc_step
	 */
	if (autoinc_step < 1)
		autoinc_step = 1;
	else if (autoinc_step > 1000)
		autoinc_step = 1000;
	if (rule->rulenum == 0) {
		/*
		 * locate the highest numbered rule before default
		 */
		for (f = *head; f; f = f->next) {
			if (f->rulenum == IPFW_DEFAULT_RULE)
				break;
			rule->rulenum = f->rulenum;
		}
		if (rule->rulenum < IPFW_DEFAULT_RULE - autoinc_step)
			rule->rulenum += autoinc_step;
		ioc_rule->rulenum = rule->rulenum;
	}

	/*
	 * Now insert the new rule in the right place in the sorted list.
	 */
	for (prev = NULL, f = *head; f; prev = f, f = f->next) {
		if (f->rulenum > rule->rulenum) { /* found the location */
			if (prev) {
				rule->next = f;
				prev->next = rule;
			} else { /* head insert */
				rule->next = *head;
				*head = rule;
			}
			break;
		}
	}

	flush_rule_ptrs();
	ipfw_inc_static_count(rule);

	crit_exit();

	DEB(kprintf("++ installed rule %d, static count now %d\n",
		rule->rulenum, static_count);)
	return (0);
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
delete_rule(struct ip_fw **head, struct ip_fw *prev, struct ip_fw *rule)
{
	struct ip_fw *n;

	n = rule->next;
	remove_dyn_rule(rule, NULL /* force removal */);
	if (prev == NULL)
		*head = n;
	else
		prev->next = n;
	ipfw_dec_static_count(rule);

	/* Mark the rule as invalid */
	rule->rule_flags |= IPFW_RULE_F_INVALID;
	rule->next_rule = NULL;

	/* Try to free this rule */
	ipfw_free_rule(rule);

	return n;
}

/*
 * Deletes all rules from a chain (including the default rule
 * if the second argument is set).
 * Must be called at splimp().
 */
static void
free_chain(struct ip_fw **chain, int kill_default)
{
	struct ip_fw *rule;

	flush_rule_ptrs(); /* more efficient to do outside the loop */

	while ((rule = *chain) != NULL &&
	       (kill_default || rule->rulenum != IPFW_DEFAULT_RULE))
		delete_rule(chain, NULL, rule);

	KASSERT(dyn_count == 0, ("%u dyn rule remains\n", dyn_count));

	if (kill_default) {
		ip_fw_default_rule = NULL;	/* Reset default rule */

		if (ipfw_dyn_v != NULL) {
			/*
			 * Free dynamic rules(state) hash table
			 */
			kfree(ipfw_dyn_v, M_IPFW);
			ipfw_dyn_v = NULL;
		}

		KASSERT(static_count == 0,
			("%u static rules remains\n", static_count));
		KASSERT(static_ioc_len == 0,
			("%u bytes of static rules remains\n", static_ioc_len));
	} else {
		KASSERT(static_count == 1,
			("%u static rules remains\n", static_count));
		KASSERT(static_ioc_len == IOC_RULESIZE(ip_fw_default_rule),
			("%u bytes of static rules remains, should be %u\n",
			 static_ioc_len, IOC_RULESIZE(ip_fw_default_rule)));
	}
}

/**
 * Remove all rules with given number, and also do set manipulation.
 *
 * The argument is an uint32_t. The low 16 bit are the rule or set number,
 * the next 8 bits are the new set, the top 8 bits are the command:
 *
 *	0	delete rules with given number
 *	1	delete rules with given set number
 *	2	move rules with given number to new set
 *	3	move rules with given set number to new set
 *	4	swap sets with given numbers
 */
static int
del_entry(struct ip_fw **chain, uint32_t arg)
{
	struct ip_fw *prev, *rule;
	uint16_t rulenum;
	uint8_t cmd, new_set;

	rulenum = arg & 0xffff;
	cmd = (arg >> 24) & 0xff;
	new_set = (arg >> 16) & 0xff;

	if (cmd > 4)
		return EINVAL;
	if (new_set > 30)
		return EINVAL;
	if (cmd == 0 || cmd == 2) {
		if (rulenum == IPFW_DEFAULT_RULE)
			return EINVAL;
	} else {
		if (rulenum > 30)
			return EINVAL;
	}

	switch (cmd) {
	case 0:	/* delete rules with given number */
		/*
		 * locate first rule to delete
		 */
		for (prev = NULL, rule = *chain;
		     rule && rule->rulenum < rulenum;
		     prev = rule, rule = rule->next)
			;
		if (rule->rulenum != rulenum)
			return EINVAL;

		crit_enter(); /* no access to rules while removing */
		/*
		 * flush pointers outside the loop, then delete all matching
		 * rules. prev remains the same throughout the cycle.
		 */
		flush_rule_ptrs();
		while (rule && rule->rulenum == rulenum)
			rule = delete_rule(chain, prev, rule);
		crit_exit();
		break;

	case 1:	/* delete all rules with given set number */
		crit_enter();
		flush_rule_ptrs();
		for (prev = NULL, rule = *chain; rule;) {
			if (rule->set == rulenum) {
				rule = delete_rule(chain, prev, rule);
			} else {
				prev = rule;
				rule = rule->next;
			}
		}
		crit_exit();
		break;

	case 2:	/* move rules with given number to new set */
		crit_enter();
		for (rule = *chain; rule; rule = rule->next) {
			if (rule->rulenum == rulenum)
				rule->set = new_set;
		}
		crit_exit();
		break;

	case 3: /* move rules with given set number to new set */
		crit_enter();
		for (rule = *chain; rule; rule = rule->next) {
			if (rule->set == rulenum)
				rule->set = new_set;
		}
		crit_exit();
		break;

	case 4: /* swap two sets */
		crit_enter();
		for (rule = *chain; rule; rule = rule->next) {
			if (rule->set == rulenum)
				rule->set = new_set;
			else if (rule->set == new_set)
				rule->set = rulenum;
		}
		crit_exit();
		break;
	}
	return 0;
}

/*
 * Clear counters for a specific rule.
 */
static void
clear_counters(struct ip_fw *rule, int log_only)
{
	ipfw_insn_log *l = (ipfw_insn_log *)ACTION_PTR(rule);

	if (log_only == 0) {
		rule->bcnt = rule->pcnt = 0;
		rule->timestamp = 0;
	}
	if (l->o.opcode == O_LOG)
		l->log_left = l->max_log;
}

/**
 * Reset some or all counters on firewall rules.
 * @arg frwl is null to clear all entries, or contains a specific
 * rule number.
 * @arg log_only is 1 if we only want to reset logs, zero otherwise.
 */
static int
zero_entry(int rulenum, int log_only)
{
	struct ip_fw *rule;
	char *msg;

	if (rulenum == 0) {
		crit_enter();
		norule_counter = 0;
		for (rule = layer3_chain; rule; rule = rule->next)
			clear_counters(rule, log_only);
		crit_exit();
		msg = log_only ? "ipfw: All logging counts reset.\n"
			       : "ipfw: Accounting cleared.\n";
	} else {
		int cleared = 0;

		/*
		 * We can have multiple rules with the same number, so we
		 * need to clear them all.
		 */
		for (rule = layer3_chain; rule; rule = rule->next) {
			if (rule->rulenum == rulenum) {
				crit_enter();
				while (rule && rule->rulenum == rulenum) {
					clear_counters(rule, log_only);
					rule = rule->next;
				}
				crit_exit();
				cleared = 1;
				break;
			}
		}
		if (!cleared)	/* we did not find any matching rules */
			return (EINVAL);
		msg = log_only ? "ipfw: Entry %d logging count reset.\n"
			       : "ipfw: Entry %d cleared.\n";
	}
	if (fw_verbose)
		log(LOG_SECURITY | LOG_NOTICE, msg, rulenum);
	return (0);
}

/*
 * Check validity of the structure before insert.
 * Fortunately rules are simple, so this mostly need to check rule sizes.
 */
static int
ipfw_ctl_check_rule(struct ipfw_ioc_rule *rule, int size)
{
	int l, cmdlen = 0;
	int have_action = 0;
	ipfw_insn *cmd;

	/* Check for valid size */
	if (size < sizeof(*rule)) {
		kprintf("ipfw: rule too short\n");
		return EINVAL;
	}
	l = IOC_RULESIZE(rule);
	if (l != size) {
		kprintf("ipfw: size mismatch (have %d want %d)\n", size, l);
		return EINVAL;
	}

	/*
	 * Now go for the individual checks. Very simple ones, basically only
	 * instruction sizes.
	 */
	for (l = rule->cmd_len, cmd = rule->cmd; l > 0;
	     l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);
		if (cmdlen > l) {
			kprintf("ipfw: opcode %d size truncated\n",
				cmd->opcode);
			return EINVAL;
		}
		DEB(kprintf("ipfw: opcode %d\n", cmd->opcode);)
		switch (cmd->opcode) {
		case O_NOP:
		case O_PROBE_STATE:
		case O_KEEP_STATE:
		case O_PROTO:
		case O_IP_SRC_ME:
		case O_IP_DST_ME:
		case O_LAYER2:
		case O_IN:
		case O_FRAG:
		case O_IPOPT:
		case O_IPLEN:
		case O_IPID:
		case O_IPTOS:
		case O_IPPRECEDENCE:
		case O_IPTTL:
		case O_IPVER:
		case O_TCPWIN:
		case O_TCPFLAGS:
		case O_TCPOPTS:
		case O_ESTAB:
			if (cmdlen != F_INSN_SIZE(ipfw_insn))
				goto bad_size;
			break;

		case O_UID:
		case O_GID:
		case O_IP_SRC:
		case O_IP_DST:
		case O_TCPSEQ:
		case O_TCPACK:
		case O_PROB:
		case O_ICMPTYPE:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_u32))
				goto bad_size;
			break;

		case O_LIMIT:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_limit))
				goto bad_size;
			break;

		case O_LOG:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_log))
				goto bad_size;

			((ipfw_insn_log *)cmd)->log_left =
			    ((ipfw_insn_log *)cmd)->max_log;

			break;

		case O_IP_SRC_MASK:
		case O_IP_DST_MASK:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_ip))
				goto bad_size;
			if (((ipfw_insn_ip *)cmd)->mask.s_addr == 0) {
				kprintf("ipfw: opcode %d, useless rule\n",
					cmd->opcode);
				return EINVAL;
			}
			break;

		case O_IP_SRC_SET:
		case O_IP_DST_SET:
			if (cmd->arg1 == 0 || cmd->arg1 > 256) {
				kprintf("ipfw: invalid set size %d\n",
					cmd->arg1);
				return EINVAL;
			}
			if (cmdlen != F_INSN_SIZE(ipfw_insn_u32) +
			    (cmd->arg1+31)/32 )
				goto bad_size;
			break;

		case O_MACADDR2:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_mac))
				goto bad_size;
			break;

		case O_MAC_TYPE:
		case O_IP_SRCPORT:
		case O_IP_DSTPORT: /* XXX artificial limit, 30 port pairs */
			if (cmdlen < 2 || cmdlen > 31)
				goto bad_size;
			break;

		case O_RECV:
		case O_XMIT:
		case O_VIA:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_if))
				goto bad_size;
			break;

		case O_PIPE:
		case O_QUEUE:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_pipe))
				goto bad_size;
			goto check_action;

		case O_FORWARD_IP:
			if (cmdlen != F_INSN_SIZE(ipfw_insn_sa))
				goto bad_size;
			goto check_action;

		case O_FORWARD_MAC: /* XXX not implemented yet */
		case O_CHECK_STATE:
		case O_COUNT:
		case O_ACCEPT:
		case O_DENY:
		case O_REJECT:
		case O_SKIPTO:
		case O_DIVERT:
		case O_TEE:
			if (cmdlen != F_INSN_SIZE(ipfw_insn))
				goto bad_size;
check_action:
			if (have_action) {
				kprintf("ipfw: opcode %d, multiple actions"
					" not allowed\n",
					cmd->opcode);
				return EINVAL;
			}
			have_action = 1;
			if (l != cmdlen) {
				kprintf("ipfw: opcode %d, action must be"
					" last opcode\n",
					cmd->opcode);
				return EINVAL;
			}
			break;
		default:
			kprintf("ipfw: opcode %d, unknown opcode\n",
				cmd->opcode);
			return EINVAL;
		}
	}
	if (have_action == 0) {
		kprintf("ipfw: missing action\n");
		return EINVAL;
	}
	return 0;

bad_size:
	kprintf("ipfw: opcode %d size %d wrong\n",
		cmd->opcode, cmdlen);
	return EINVAL;
}

static int
ipfw_ctl_add_rule(struct sockopt *sopt)
{
	struct ipfw_ioc_rule *ioc_rule;
	size_t size;
	int error;
	
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

	error = ipfw_ctl_check_rule(ioc_rule, size);
	if (error)
		return error;

	error = ipfw_add_rule(&layer3_chain, ioc_rule);
	if (error)
		return error;

	if (sopt->sopt_dir == SOPT_GET)
		sopt->sopt_valsize = IOC_RULESIZE(ioc_rule);
	return 0;
}

static void *
ipfw_copy_rule(const struct ip_fw *rule, struct ipfw_ioc_rule *ioc_rule)
{
	ioc_rule->act_ofs = rule->act_ofs;
	ioc_rule->cmd_len = rule->cmd_len;
	ioc_rule->rulenum = rule->rulenum;
	ioc_rule->set = rule->set;
	ioc_rule->usr_flags = rule->usr_flags;

	ioc_rule->set_disable = set_disable;
	ioc_rule->static_count = static_count;
	ioc_rule->static_len = static_ioc_len;

	ioc_rule->pcnt = rule->pcnt;
	ioc_rule->bcnt = rule->bcnt;
	ioc_rule->timestamp = rule->timestamp;

	bcopy(rule->cmd, ioc_rule->cmd, ioc_rule->cmd_len * 4 /* XXX */);

	return ((uint8_t *)ioc_rule + IOC_RULESIZE(ioc_rule));
}

static void
ipfw_copy_state(const ipfw_dyn_rule *dyn_rule,
		struct ipfw_ioc_state *ioc_state)
{
	const struct ipfw_flow_id *id;
	struct ipfw_ioc_flowid *ioc_id;

	ioc_state->expire = TIME_LEQ(dyn_rule->expire, time_second) ?
			    0 : dyn_rule->expire - time_second;
	ioc_state->pcnt = dyn_rule->pcnt;
	ioc_state->bcnt = dyn_rule->bcnt;

	ioc_state->dyn_type = dyn_rule->dyn_type;
	ioc_state->count = dyn_rule->count;

	ioc_state->rulenum = dyn_rule->rule->rulenum;

	id = &dyn_rule->id;
	ioc_id = &ioc_state->id;

	ioc_id->type = ETHERTYPE_IP;
	ioc_id->u.ip.dst_ip = id->dst_ip;
	ioc_id->u.ip.src_ip = id->src_ip;
	ioc_id->u.ip.dst_port = id->dst_port;
	ioc_id->u.ip.src_port = id->src_port;
	ioc_id->u.ip.proto = id->proto;
}

static int
ipfw_ctl_get_rules(struct sockopt *sopt)
{
	struct ip_fw *rule;
	void *bp;
	size_t size;

	/*
	 * pass up a copy of the current rules. Static rules
	 * come first (the last of which has number IPFW_DEFAULT_RULE),
	 * followed by a possibly empty list of dynamic rule.
	 */
	crit_enter();

	size = static_ioc_len;	/* size of static rules */
	if (ipfw_dyn_v)		/* add size of dyn.rules */
		size += (dyn_count * sizeof(struct ipfw_ioc_state));

	if (sopt->sopt_valsize < size) {
		/* short length, no need to return incomplete rules */
		/* XXX: if superuser, no need to zero buffer */
		bzero(sopt->sopt_val, sopt->sopt_valsize); 
		return 0;
	}
	bp = sopt->sopt_val;

	for (rule = layer3_chain; rule; rule = rule->next)
		bp = ipfw_copy_rule(rule, bp);

	if (ipfw_dyn_v) {
		struct ipfw_ioc_state *ioc_state;
		int i;

		ioc_state = bp;
		for (i = 0; i < curr_dyn_buckets; i++) {
			ipfw_dyn_rule *p;

			for (p = ipfw_dyn_v[i]; p != NULL;
			     p = p->next, ioc_state++)
				ipfw_copy_state(p, ioc_state);
		}
	}

	crit_exit();

	sopt->sopt_valsize = size;
	return 0;
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
	case IP_FW_GET:
		error = ipfw_ctl_get_rules(sopt);
		break;

	case IP_FW_FLUSH:
		/*
		 * Normally we cannot release the lock on each iteration.
		 * We could do it here only because we start from the head all
		 * the times so there is no risk of missing some entries.
		 * On the other hand, the risk is that we end up with
		 * a very inconsistent ruleset, so better keep the lock
		 * around the whole cycle.
		 *
		 * XXX this code can be improved by resetting the head of
		 * the list to point to the default rule, and then freeing
		 * the old list without the need for a lock.
		 */

		crit_enter();
		free_chain(&layer3_chain, 0 /* keep default rule */);
		crit_exit();
		break;

	case IP_FW_ADD:
		error = ipfw_ctl_add_rule(sopt);
		break;

	case IP_FW_DEL:
		/*
		 * IP_FW_DEL is used for deleting single rules or sets,
		 * and (ab)used to atomically manipulate sets. Argument size
		 * is used to distinguish between the two:
		 *    sizeof(uint32_t)
		 *	delete single rule or set of rules,
		 *	or reassign rules (or sets) to a different set.
		 *    2*sizeof(uint32_t)
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
			error = del_entry(&layer3_chain, masks[0]);
		} else if (size == (2 * sizeof(*masks))) {
			/*
			 * Set enable/disable
			 */
			crit_enter();

			set_disable =
			    (set_disable | masks[0]) & ~masks[1] &
			    ~(1 << 31); /* set 31 always enabled */

			crit_exit();
		} else {
			error = EINVAL;
		}
		break;

	case IP_FW_ZERO:
	case IP_FW_RESETLOG: /* argument is an int, the rule number */
		rulenum=0;

		if (sopt->sopt_val != 0) {
		    error = soopt_to_kbuf(sopt, &rulenum,
			    sizeof(int), sizeof(int));
		    if (error)
			break;
		}
		error = zero_entry(rulenum, sopt->sopt_name == IP_FW_RESETLOG);
		break;

	default:
		kprintf("ipfw_ctl invalid option %d\n", sopt->sopt_name);
		error = EINVAL;
	}
	return error;
}

/*
 * This procedure is only used to handle keepalives. It is invoked
 * every dyn_keepalive_period
 */
static void
ipfw_tick(void *unused __unused)
{
	int i;
	ipfw_dyn_rule *q;

	if (dyn_keepalive == 0 || ipfw_dyn_v == NULL || dyn_count == 0)
		goto done;

	crit_enter();
	for (i = 0; i < curr_dyn_buckets; i++) {
		for (q = ipfw_dyn_v[i]; q; q = q->next) {
			if (q->dyn_type == O_LIMIT_PARENT)
				continue;
			if (q->id.proto != IPPROTO_TCP)
				continue;
			if ((q->state & BOTH_SYN) != BOTH_SYN)
				continue;
			if (TIME_LEQ(time_second + dyn_keepalive_interval,
			    q->expire))
				continue;	/* too early */
			if (TIME_LEQ(q->expire, time_second))
				continue;	/* too late, rule expired */

			send_pkt(&q->id, q->ack_rev - 1, q->ack_fwd, TH_SYN);
			send_pkt(&q->id, q->ack_fwd - 1, q->ack_rev, 0);
		}
	}
	crit_exit();
done:
	callout_reset(&ipfw_timeout_h, dyn_keepalive_period * hz,
		      ipfw_tick, NULL);
}

static void
ipfw_init_default_rule(struct ip_fw **head)
{
	struct ip_fw *def_rule;

	KKASSERT(*head == NULL);

	def_rule = kmalloc(sizeof(*def_rule), M_IPFW, M_WAITOK | M_ZERO);

	def_rule->act_ofs = 0;
	def_rule->rulenum = IPFW_DEFAULT_RULE;
	def_rule->cmd_len = 1;
	def_rule->set = 31;

	def_rule->cmd[0].len = 1;
#ifdef IPFIREWALL_DEFAULT_TO_ACCEPT
	def_rule->cmd[0].opcode = O_ACCEPT;
#else
	def_rule->cmd[0].opcode = O_DENY;
#endif

	def_rule->refcnt = 1;

	*head = def_rule;
	ipfw_inc_static_count(def_rule);

	/* Install the default rule */
	ip_fw_default_rule = def_rule;
}

static void
ipfw_init_dispatch(struct netmsg *nmsg)
{
	int error = 0;

	crit_enter();

	if (IPFW_LOADED) {
		kprintf("IP firewall already loaded\n");
		error = EEXIST;
		goto reply;
	}

	ip_fw_chk_ptr = ipfw_chk;
	ip_fw_ctl_ptr = ipfw_ctl;
	ip_fw_dn_io_ptr = ipfw_dummynet_io;

	layer3_chain = NULL;
	ipfw_init_default_rule(&layer3_chain);

	kprintf("ipfw2 initialized, divert %s, "
		"rule-based forwarding enabled, default to %s, logging ",
#ifdef IPDIVERT
		"enabled",
#else
		"disabled",
#endif
		ip_fw_default_rule->cmd[0].opcode == O_ACCEPT ?
		"accept" : "deny");

#ifdef IPFIREWALL_VERBOSE
	fw_verbose = 1;
#endif
#ifdef IPFIREWALL_VERBOSE_LIMIT
	verbose_limit = IPFIREWALL_VERBOSE_LIMIT;
#endif
	if (fw_verbose == 0) {
		kprintf("disabled\n");
	} else if (verbose_limit == 0) {
		kprintf("unlimited\n");
	} else {
		kprintf("limited to %d packets/entry by default\n",
			verbose_limit);
	}
	callout_init(&ipfw_timeout_h);

	ip_fw_loaded = 1;
	callout_reset(&ipfw_timeout_h, hz, ipfw_tick, NULL);
reply:
	crit_exit();
	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static int
ipfw_init(void)
{
	struct netmsg smsg;

	netmsg_init(&smsg, &curthread->td_msgport, 0, ipfw_init_dispatch);
	return lwkt_domsg(cpu_portfn(0), &smsg.nm_lmsg, 0);
}

#ifdef KLD_MODULE

static void
ipfw_fini_dispatch(struct netmsg *nmsg)
{
	int error = 0;

	crit_enter();

	if (ipfw_refcnt != 0) {
		error = EBUSY;
		goto reply;
	}

	callout_stop(&ipfw_timeout_h);

	ip_fw_loaded = 0;
	netmsg_service_sync();

	ip_fw_chk_ptr = NULL;
	ip_fw_ctl_ptr = NULL;
	ip_fw_dn_io_ptr = NULL;
	free_chain(&layer3_chain, 1 /* kill default rule */);

	kprintf("IP firewall unloaded\n");
reply:
	crit_exit();
	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static int
ipfw_fini(void)
{
	struct netmsg smsg;

	netmsg_init(&smsg, &curthread->td_msgport, 0, ipfw_fini_dispatch);
	return lwkt_domsg(cpu_portfn(0), &smsg.nm_lmsg, 0);
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
	"ipfw",
	ipfw_modevent,
	0
};
DECLARE_MODULE(ipfw, ipfwmod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_VERSION(ipfw, 1);
