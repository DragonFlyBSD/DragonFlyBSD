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
 */

/*
 * Implement IP packet firewall (new version)
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

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>
#include <net/dummynet/ip_dummynet.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>
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
#include <netinet/if_ether.h> /* XXX for ETHERTYPE_IP */

#include <net/ipfw/ip_fw2.h>

#ifdef IPFIREWALL_DEBUG
#define DPRINTF(fmt, ...) \
do { \
	if (fw_debug > 0) \
		kprintf(fmt, __VA_ARGS__); \
} while (0)
#else
#define DPRINTF(fmt, ...)	((void)0)
#endif

/*
 * Description about per-CPU rule duplication:
 *
 * Module loading/unloading and all ioctl operations are serialized
 * by netisr0, so we don't have any ordering or locking problems.
 *
 * Following graph shows how operation on per-CPU rule list is
 * performed [2 CPU case]:
 *
 *   CPU0                 CPU1
 *
 * netisr0 <------------------------------------+
 *  domsg                                       |
 *    |                                         |
 *    | netmsg                                  |
 *    |                                         |
 *    V                                         |
 *  ifnet0                                      |
 *    :                                         | netmsg
 *    :(delete/add...)                          |
 *    :                                         |
 *    :         netmsg                          |
 *  forwardmsg---------->ifnet1                 |
 *                          :                   |
 *                          :(delete/add...)    |
 *                          :                   |
 *                          :                   |
 *                        replymsg--------------+
 *
 *
 *
 *
 * Rules which will not create states (dyn rules) [2 CPU case]
 *
 *    CPU0               CPU1
 * layer3_chain       layer3_chain
 *     |                  |
 *     V                  V
 * +-------+ sibling  +-------+ sibling
 * | rule1 |--------->| rule1 |--------->NULL
 * +-------+          +-------+
 *     |                  |
 *     |next              |next
 *     V                  V
 * +-------+ sibling  +-------+ sibling
 * | rule2 |--------->| rule2 |--------->NULL
 * +-------+          +-------+
 *
 * ip_fw.sibling:
 * 1) Ease statistics calculation during IP_FW_GET.  We only need to
 *    iterate layer3_chain on CPU0; the current rule's duplication on
 *    the other CPUs could safely be read-only accessed by using
 *    ip_fw.sibling
 * 2) Accelerate rule insertion and deletion, e.g. rule insertion:
 *    a) In netisr0 (on CPU0) rule3 is determined to be inserted between
 *       rule1 and rule2.  To make this decision we need to iterate the
 *       layer3_chain on CPU0.  The netmsg, which is used to insert the
 *       rule, will contain rule1 on CPU0 as prev_rule and rule2 on CPU0
 *       as next_rule
 *    b) After the insertion on CPU0 is done, we will move on to CPU1.
 *       But instead of relocating the rule3's position on CPU1 by
 *       iterating the layer3_chain on CPU1, we set the netmsg's prev_rule
 *       to rule1->sibling and next_rule to rule2->sibling before the
 *       netmsg is forwarded to CPU1 from CPU0
 *       
 *    
 *
 * Rules which will create states (dyn rules) [2 CPU case]
 * (unnecessary parts are omitted; they are same as in the previous figure)
 *
 *   CPU0                       CPU1
 * 
 * +-------+                  +-------+
 * | rule1 |                  | rule1 |
 * +-------+                  +-------+
 *   ^   |                      |   ^
 *   |   |stub              stub|   |
 *   |   |                      |   |
 *   |   +----+            +----+   |
 *   |        |            |        |
 *   |        V            V        |
 *   |    +--------------------+    |
 *   |    |     rule_stub      |    |
 *   |    | (read-only shared) |    |
 *   |    |                    |    |
 *   |    | back pointer array |    |
 *   |    | (indexed by cpuid) |    |
 *   |    |                    |    |
 *   +----|---------[0]        |    |
 *        |         [1]--------|----+
 *        |                    |
 *        +--------------------+
 *          ^            ^
 *          |            |
 *  ........|............|............
 *  :       |            |           :
 *  :       |stub        |stub       :
 *  :       |            |           :
 *  :  +---------+  +---------+      :
 *  :  | state1a |  | state1b | .... :
 *  :  +---------+  +---------+      :
 *  :                                :
 *  :           states table         :
 *  :            (shared)            :
 *  :      (protected by dyn_lock)   :
 *  ..................................
 * 
 * [state1a and state1b are states created by rule1]
 *
 * ip_fw_stub:
 * This structure is introduced so that shared (locked) state table could
 * work with per-CPU (duplicated) static rules.  It mainly bridges states
 * and static rules and serves as static rule's place holder (a read-only
 * shared part of duplicated rules) from states point of view.
 *
 * IPFW_RULE_F_STATE (only for rules which create states):
 * o  During rule installation, this flag is turned on after rule's
 *    duplications reach all CPUs, to avoid at least following race:
 *    1) rule1 is duplicated on CPU0 and is not duplicated on CPU1 yet
 *    2) rule1 creates state1
 *    3) state1 is located on CPU1 by check-state
 *    But rule1 is not duplicated on CPU1 yet
 * o  During rule deletion, this flag is turned off before deleting states
 *    created by the rule and before deleting the rule itself, so no
 *    more states will be created by the to-be-deleted rule even when its
 *    duplication on certain CPUs are not eliminated yet.
 */

#define IPFW_AUTOINC_STEP_MIN	1
#define IPFW_AUTOINC_STEP_MAX	1000
#define IPFW_AUTOINC_STEP_DEF	100

#define	IPFW_DEFAULT_RULE	65535	/* rulenum for the default rule */
#define IPFW_DEFAULT_SET	31	/* set number for the default rule */

struct netmsg_ipfw {
	struct netmsg_base base;
	const struct ipfw_ioc_rule *ioc_rule;
	struct ip_fw	*next_rule;
	struct ip_fw	*prev_rule;
	struct ip_fw	*sibling;
	struct ip_fw_stub *stub;
};

struct netmsg_del {
	struct netmsg_base base;
	struct ip_fw	*start_rule;
	struct ip_fw	*prev_rule;
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

struct ipfw_context {
	struct ip_fw	*ipfw_layer3_chain;	/* list of rules for layer3 */
	struct ip_fw	*ipfw_default_rule;	/* default rule */
	uint64_t	ipfw_norule_counter;	/* counter for ipfw_log(NULL) */

	/*
	 * ipfw_set_disable contains one bit per set value (0..31).
	 * If the bit is set, all rules with the corresponding set
	 * are disabled.  Set IPDW_DEFAULT_SET is reserved for the
	 * default rule and CANNOT be disabled.
	 */
	uint32_t	ipfw_set_disable;
	uint32_t	ipfw_gen;		/* generation of rule list */
};

static struct ipfw_context	*ipfw_ctx[MAXCPU];

#ifdef KLD_MODULE
/*
 * Module can not be unloaded, if there are references to
 * certains rules of ipfw(4), e.g. dummynet(4)
 */
static int ipfw_refcnt;
#endif

MALLOC_DEFINE(M_IPFW, "IpFw/IpAcct", "IpFw/IpAcct chain's");

/*
 * Following two global variables are accessed and
 * updated only on CPU0
 */
static uint32_t static_count;	/* # of static rules */
static uint32_t static_ioc_len;	/* bytes of static rules */

/*
 * If 1, then ipfw static rules are being flushed,
 * ipfw_chk() will skip to the default rule.
 */
static int ipfw_flushing;

static int fw_verbose;
static int verbose_limit;

static int fw_debug;
static int autoinc_step = IPFW_AUTOINC_STEP_DEF;

static int	ipfw_sysctl_enable(SYSCTL_HANDLER_ARGS);
static int	ipfw_sysctl_autoinc_step(SYSCTL_HANDLER_ARGS);
static int	ipfw_sysctl_dyn_buckets(SYSCTL_HANDLER_ARGS);
static int	ipfw_sysctl_dyn_fin(SYSCTL_HANDLER_ARGS);
static int	ipfw_sysctl_dyn_rst(SYSCTL_HANDLER_ARGS);

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
 *
 * NOTE about the SHARED LOCKMGR LOCK during dynamic rule looking up:
 * Only TCP state transition will change dynamic rule's state and ack
 * sequences, while all packets of one TCP connection only goes through
 * one TCP thread, so it is safe to use shared lockmgr lock during dynamic
 * rule looking up.  The keep alive callout uses exclusive lockmgr lock
 * when it tries to find suitable dynamic rules to send keep alive, so
 * it will not see half updated state and ack sequences.  Though the expire
 * field updating looks racy for other protocols, the resolution (second)
 * of expire field makes this kind of race harmless.
 * XXX statistics' updating is _not_ MPsafe!!!
 * XXX once UDP output path is fixed, we could use lockless dynamic rule
 *     hash table
 */
static ipfw_dyn_rule **ipfw_dyn_v = NULL;
static uint32_t dyn_buckets = 256; /* must be power of 2 */
static uint32_t curr_dyn_buckets = 256; /* must be power of 2 */
static uint32_t dyn_buckets_gen; /* generation of dyn buckets array */
static struct lock dyn_lock; /* dynamic rules' hash table lock */

static struct netmsg_base ipfw_timeout_netmsg; /* schedule ipfw timeout */
static struct callout ipfw_timeout_h;

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

static uint32_t dyn_count;		/* # of dynamic rules */
static uint32_t dyn_max = 4096;		/* max # of dynamic rules */

SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, dyn_buckets, CTLTYPE_INT | CTLFLAG_RW,
    &dyn_buckets, 0, ipfw_sysctl_dyn_buckets, "I", "Number of dyn. buckets");
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
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, dyn_fin_lifetime,
    CTLTYPE_INT | CTLFLAG_RW, &dyn_fin_lifetime, 0, ipfw_sysctl_dyn_fin, "I",
    "Lifetime of dyn. rules for fin");
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, dyn_rst_lifetime,
    CTLTYPE_INT | CTLFLAG_RW, &dyn_rst_lifetime, 0, ipfw_sysctl_dyn_rst, "I",
    "Lifetime of dyn. rules for rst");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_udp_lifetime, CTLFLAG_RW,
    &dyn_udp_lifetime, 0, "Lifetime of dyn. rules for UDP");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_short_lifetime, CTLFLAG_RW,
    &dyn_short_lifetime, 0, "Lifetime of dyn. rules for other situations");
SYSCTL_INT(_net_inet_ip_fw, OID_AUTO, dyn_keepalive, CTLFLAG_RW,
    &dyn_keepalive, 0, "Enable keepalives for dyn. rules");

static ip_fw_chk_t	ipfw_chk;
static void		ipfw_tick(void *);

static __inline int
ipfw_free_rule(struct ip_fw *rule)
{
	KASSERT(rule->cpuid == mycpuid, ("rule freed on cpu%d", mycpuid));
	KASSERT(rule->refcnt > 0, ("invalid refcnt %u", rule->refcnt));
	rule->refcnt--;
	if (rule->refcnt == 0) {
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
	KASSERT(rule->cpuid == mycpuid, ("rule used on cpu%d", mycpuid));
#ifdef KLD_MODULE
	atomic_add_int(&ipfw_refcnt, 1);
#endif
	rule->refcnt++;
}

/*
 * This macro maps an ip pointer into a layer3 header pointer of type T
 */
#define	L3HDR(T, ip) ((T *)((uint32_t *)(ip) + (ip)->ip_hl))

static __inline int
icmptype_match(struct ip *ip, ipfw_insn_u32 *cmd)
{
	int type = L3HDR(struct icmp,ip)->icmp_type;

	return (type <= ICMP_MAXTYPE && (cmd->d[0] & (1 << type)));
}

#define TT	((1 << ICMP_ECHO) | \
		 (1 << ICMP_ROUTERSOLICIT) | \
		 (1 << ICMP_TSTAMP) | \
		 (1 << ICMP_IREQ) | \
		 (1 << ICMP_MASKREQ))

static int
is_icmp_query(struct ip *ip)
{
	int type = L3HDR(struct icmp, ip)->icmp_type;

	return (type <= ICMP_MAXTYPE && (TT & (1 << type)));
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

	if (((cmd->arg1 & 0xff) & bits) != 0)
		return 0; /* some bits we want set were clear */

	want_clear = (cmd->arg1 >> 8) & 0xff;
	if ((want_clear & bits) != want_clear)
		return 0; /* some bits we want clear were set */
	return 1;
}

static int
ipopts_match(struct ip *ip, ipfw_insn *cmd)
{
	int optlen, bits = 0;
	u_char *cp = (u_char *)(ip + 1);
	int x = (ip->ip_hl << 2) - sizeof(struct ip);

	for (; x > 0; x -= optlen, cp += optlen) {
		int opt = cp[IPOPT_OPTVAL];

		if (opt == IPOPT_EOL)
			break;

		if (opt == IPOPT_NOP) {
			optlen = 1;
		} else {
			optlen = cp[IPOPT_OLEN];
			if (optlen <= 0 || optlen > x)
				return 0; /* invalid or truncated */
		}

		switch (opt) {
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

		default:
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

		if (opt == TCPOPT_NOP) {
			optlen = 1;
		} else {
			optlen = cp[1];
			if (optlen <= 0)
				break;
		}

		switch (opt) {
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

		default:
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
		struct ipfw_context *ctx = ipfw_ctx[mycpuid];

		if (verbose_limit != 0 &&
		    ctx->ipfw_norule_counter >= verbose_limit)
			return;
		ctx->ipfw_norule_counter++;
		if (ctx->ipfw_norule_counter == verbose_limit)
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
			if (cmd->arg1==ICMP_REJECT_RST) {
				action = "Reset";
			} else if (cmd->arg1==ICMP_UNREACH_HOST) {
				action = "Reject";
			} else {
				ksnprintf(SNPARGS(action2, 0), "Unreach %d",
					  cmd->arg1);
			}
			break;

		case O_ACCEPT:
			action = "Accept";
			break;

		case O_COUNT:
			action = "Count";
			break;

		case O_DIVERT:
			ksnprintf(SNPARGS(action2, 0), "Divert %d", cmd->arg1);
			break;

		case O_TEE:
			ksnprintf(SNPARGS(action2, 0), "Tee %d", cmd->arg1);
			break;

		case O_SKIPTO:
			ksnprintf(SNPARGS(action2, 0), "SkipTo %d", cmd->arg1);
			break;

		case O_PIPE:
			ksnprintf(SNPARGS(action2, 0), "Pipe %d", cmd->arg1);
			break;

		case O_QUEUE:
			ksnprintf(SNPARGS(action2, 0), "Queue %d", cmd->arg1);
			break;

		case O_FORWARD_IP:
			{
				ipfw_insn_sa *sa = (ipfw_insn_sa *)cmd;
				int len;

				len = ksnprintf(SNPARGS(action2, 0),
						"Forward to %s",
						inet_ntoa(sa->sa.sin_addr));
				if (sa->sa.sin_port) {
					ksnprintf(SNPARGS(action2, len), ":%d",
						  sa->sa.sin_port);
				}
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
			if (offset == 0) {
				ksnprintf(SNPARGS(proto, len), ":%d %s:%d",
					  ntohs(tcp->th_sport),
					  inet_ntoa(ip->ip_dst),
					  ntohs(tcp->th_dport));
			} else {
				ksnprintf(SNPARGS(proto, len), " %s",
					  inet_ntoa(ip->ip_dst));
			}
			break;

		case IPPROTO_UDP:
			len = ksnprintf(SNPARGS(proto, 0), "UDP %s",
					inet_ntoa(ip->ip_src));
			if (offset == 0) {
				ksnprintf(SNPARGS(proto, len), ":%d %s:%d",
					  ntohs(udp->uh_sport),
					  inet_ntoa(ip->ip_dst),
					  ntohs(udp->uh_dport));
			} else {
				ksnprintf(SNPARGS(proto, len), " %s",
					  inet_ntoa(ip->ip_dst));
			}
			break;

		case IPPROTO_ICMP:
			if (offset == 0) {
				len = ksnprintf(SNPARGS(proto, 0),
						"ICMP:%u.%u ",
						icmp->icmp_type,
						icmp->icmp_code);
			} else {
				len = ksnprintf(SNPARGS(proto, 0), "ICMP ");
			}
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

		if (ip_off & (IP_MF | IP_OFFMASK)) {
			ksnprintf(SNPARGS(fragment, 0), " (frag %d:%d@%d%s)",
				  ntohs(ip->ip_id), ip_len - (ip->ip_hl << 2),
				  offset << 3, (ip_off & IP_MF) ? "+" : "");
		}
	}

	if (oif || m->m_pkthdr.rcvif) {
		log(LOG_SECURITY | LOG_INFO,
		    "ipfw: %d %s %s %s via %s%s\n",
		    f ? f->rulenum : -1,
		    action, proto, oif ? "out" : "in",
		    oif ? oif->if_xname : m->m_pkthdr.rcvif->if_xname,
		    fragment);
	} else {
		log(LOG_SECURITY | LOG_INFO,
		    "ipfw: %d %s %s [no if info]%s\n",
		    f ? f->rulenum : -1,
		    action, proto, fragment);
	}

	if (limit_reached) {
		log(LOG_SECURITY | LOG_NOTICE,
		    "ipfw: limit %d reached on entry %d\n",
		    limit_reached, f ? f->rulenum : -1);
	}
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
#define UNLINK_DYN_RULE(prev, head, q)					\
do {									\
	ipfw_dyn_rule *old_q = q;					\
									\
	/* remove a refcount to the parent */				\
	if (q->dyn_type == O_LIMIT)					\
		q->parent->count--;					\
	DPRINTF("-- unlink entry 0x%08x %d -> 0x%08x %d, %d left\n",	\
		q->id.src_ip, q->id.src_port,				\
		q->id.dst_ip, q->id.dst_port, dyn_count - 1);		\
	if (prev != NULL)						\
		prev->next = q = q->next;				\
	else								\
		head = q = q->next;					\
	KASSERT(dyn_count > 0, ("invalid dyn count %u", dyn_count));	\
	dyn_count--;							\
	kfree(old_q, M_IPFW);						\
} while (0)

#define TIME_LEQ(a, b)	((int)((a) - (b)) <= 0)

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
remove_dyn_rule_locked(struct ip_fw *rule, ipfw_dyn_rule *keep_me)
{
	static time_t last_remove = 0; /* XXX */

#define FORCE	(keep_me == NULL)

	ipfw_dyn_rule *prev, *q;
	int i, pass = 0, max_pass = 0, unlinked = 0;

	if (ipfw_dyn_v == NULL || dyn_count == 0)
		return;
	/* do not expire more than once per second, it is useless */
	if (!FORCE && last_remove == time_uptime)
		return;
	last_remove = time_uptime;

	/*
	 * because O_LIMIT refer to parent rules, during the first pass only
	 * remove child and mark any pending LIMIT_PARENT, and remove
	 * them in a second pass.
	 */
next_pass:
	for (i = 0; i < curr_dyn_buckets; i++) {
		for (prev = NULL, q = ipfw_dyn_v[i]; q;) {
			/*
			 * Logic can become complex here, so we split tests.
			 */
			if (q == keep_me)
				goto next;
			if (rule != NULL && rule->stub != q->stub)
				goto next; /* not the one we are looking for */
			if (q->dyn_type == O_LIMIT_PARENT) {
				/*
				 * handle parent in the second pass,
				 * record we need one.
				 */
				max_pass = 1;
				if (pass == 0)
					goto next;
				if (FORCE && q->count != 0) {
					/* XXX should not happen! */
					kprintf("OUCH! cannot remove rule, "
						"count %d\n", q->count);
				}
			} else {
				if (!FORCE && !TIME_LEQ(q->expire, time_second))
					goto next;
			}
			unlinked = 1;
			UNLINK_DYN_RULE(prev, ipfw_dyn_v[i], q);
			continue;
next:
			prev = q;
			q = q->next;
		}
	}
	if (pass++ < max_pass)
		goto next_pass;

	if (unlinked)
		++dyn_buckets_gen;

#undef FORCE
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
	ipfw_dyn_rule *q=NULL;

	if (ipfw_dyn_v == NULL)
		goto done;	/* not found */

	i = hash_packet(pkt);
	for (q = ipfw_dyn_v[i]; q != NULL;) {
		if (q->dyn_type == O_LIMIT_PARENT)
			goto next;

		if (TIME_LEQ(q->expire, time_second)) {
			/*
			 * Entry expired; skip.
			 * Let ipfw_tick() take care of it
			 */
			goto next;
		}

		if (pkt->proto == q->id.proto) {
			if (pkt->src_ip == q->id.src_ip &&
			    pkt->dst_ip == q->id.dst_ip &&
			    pkt->src_port == q->id.src_port &&
			    pkt->dst_port == q->id.dst_port) {
				dir = MATCH_FORWARD;
				break;
			}
			if (pkt->src_ip == q->id.dst_ip &&
			    pkt->dst_ip == q->id.src_ip &&
			    pkt->src_port == q->id.dst_port &&
			    pkt->dst_port == q->id.src_port) {
				dir = MATCH_REVERSE;
				break;
			}
		}
next:
		q = q->next;
	}
	if (q == NULL)
		goto done; /* q = NULL, not found */

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
				uint32_t ack = ntohl(tcp->th_ack);

#define _SEQ_GE(a, b)	((int)(a) - (int)(b) >= 0)

				if (dir == MATCH_FORWARD) {
					if (q->ack_fwd == 0 ||
					    _SEQ_GE(ack, q->ack_fwd))
						q->ack_fwd = ack;
					else /* ignore out-of-sequence */
						break;
				} else {
					if (q->ack_rev == 0 ||
					    _SEQ_GE(ack, q->ack_rev))
						q->ack_rev = ack;
					else /* ignore out-of-sequence */
						break;
				}
#undef _SEQ_GE
			}
			q->expire = time_second + dyn_ack_lifetime;
			break;

		case BOTH_SYN | BOTH_FIN:	/* both sides closed */
			KKASSERT(dyn_fin_lifetime < dyn_keepalive_period);
			q->expire = time_second + dyn_fin_lifetime;
			break;

		default:
#if 0
			/*
			 * reset or some invalid combination, but can also
			 * occur if we use keep-state the wrong way.
			 */
			if ((q->state & ((TH_RST << 8) | TH_RST)) == 0)
				kprintf("invalid state: 0x%x\n", q->state);
#endif
			KKASSERT(dyn_rst_lifetime < dyn_keepalive_period);
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

static struct ip_fw *
lookup_rule(struct ipfw_flow_id *pkt, int *match_direction, struct tcphdr *tcp,
	    uint16_t len, int *deny)
{
	struct ip_fw *rule = NULL;
	ipfw_dyn_rule *q;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	uint32_t gen;

	*deny = 0;
	gen = ctx->ipfw_gen;

	lockmgr(&dyn_lock, LK_SHARED);

	if (ctx->ipfw_gen != gen) {
		/*
		 * Static rules had been change when we were waiting
		 * for the dynamic hash table lock; deny this packet,
		 * since it is _not_ known whether it is safe to keep
		 * iterating the static rules.
		 */
		*deny = 1;
		goto back;
	}

	q = lookup_dyn_rule(pkt, match_direction, tcp);
	if (q == NULL) {
		rule = NULL;
	} else {
		rule = q->stub->rule[mycpuid];
		KKASSERT(rule->stub == q->stub && rule->cpuid == mycpuid);

		/* XXX */
		q->pcnt++;
		q->bcnt += len;
	}
back:
	lockmgr(&dyn_lock, LK_RELEASE);
	return rule;
}

static void
realloc_dynamic_table(void)
{
	ipfw_dyn_rule **old_dyn_v;
	uint32_t old_curr_dyn_buckets;

	KASSERT(dyn_buckets <= 65536 && (dyn_buckets & (dyn_buckets - 1)) == 0,
		("invalid dyn_buckets %d", dyn_buckets));

	/* Save the current buckets array for later error recovery */
	old_dyn_v = ipfw_dyn_v;
	old_curr_dyn_buckets = curr_dyn_buckets;

	curr_dyn_buckets = dyn_buckets;
	for (;;) {
		ipfw_dyn_v = kmalloc(curr_dyn_buckets * sizeof(ipfw_dyn_rule *),
				     M_IPFW, M_NOWAIT | M_ZERO);
		if (ipfw_dyn_v != NULL || curr_dyn_buckets <= 2)
			break;

		curr_dyn_buckets /= 2;
		if (curr_dyn_buckets <= old_curr_dyn_buckets &&
		    old_dyn_v != NULL) {
			/*
			 * Don't try allocating smaller buckets array, reuse
			 * the old one, which alreay contains enough buckets
			 */
			break;
		}
	}

	if (ipfw_dyn_v != NULL) {
		if (old_dyn_v != NULL)
			kfree(old_dyn_v, M_IPFW);
	} else {
		/* Allocation failed, restore old buckets array */
		ipfw_dyn_v = old_dyn_v;
		curr_dyn_buckets = old_curr_dyn_buckets;
	}

	if (ipfw_dyn_v != NULL)
		++dyn_buckets_gen;
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

	r = kmalloc(sizeof(*r), M_IPFW, M_NOWAIT | M_ZERO);
	if (r == NULL)
		return NULL;

	/* increase refcount on parent, and set pointer */
	if (dyn_type == O_LIMIT) {
		ipfw_dyn_rule *parent = (ipfw_dyn_rule *)rule;

		if (parent->dyn_type != O_LIMIT_PARENT)
			panic("invalid parent");
		parent->count++;
		r->parent = parent;
		rule = parent->stub->rule[mycpuid];
		KKASSERT(rule->stub == parent->stub);
	}
	KKASSERT(rule->cpuid == mycpuid && rule->stub != NULL);

	r->id = *id;
	r->expire = time_second + dyn_syn_lifetime;
	r->stub = rule->stub;
	r->dyn_type = dyn_type;
	r->pcnt = r->bcnt = 0;
	r->count = 0;

	r->bucket = i;
	r->next = ipfw_dyn_v[i];
	ipfw_dyn_v[i] = r;
	dyn_count++;
	dyn_buckets_gen++;
	DPRINTF("-- add dyn entry ty %d 0x%08x %d -> 0x%08x %d, total %d\n",
		dyn_type,
		r->id.src_ip, r->id.src_port,
		r->id.dst_ip, r->id.dst_port, dyn_count);
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
		i = hash_packet(pkt);
		for (q = ipfw_dyn_v[i]; q != NULL; q = q->next) {
			if (q->dyn_type == O_LIMIT_PARENT &&
			    rule->stub == q->stub &&
			    pkt->proto == q->id.proto &&
			    pkt->src_ip == q->id.src_ip &&
			    pkt->dst_ip == q->id.dst_ip &&
			    pkt->src_port == q->id.src_port &&
			    pkt->dst_port == q->id.dst_port) {
				q->expire = time_second + dyn_short_lifetime;
				DPRINTF("lookup_dyn_parent found 0x%p\n", q);
				return q;
			}
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
install_state_locked(struct ip_fw *rule, ipfw_insn_limit *cmd,
		     struct ip_fw_args *args)
{
	static int last_log; /* XXX */

	ipfw_dyn_rule *q;

	DPRINTF("-- install state type %d 0x%08x %u -> 0x%08x %u\n",
		cmd->o.opcode,
		args->f_id.src_ip, args->f_id.src_port,
		args->f_id.dst_ip, args->f_id.dst_port);

	q = lookup_dyn_rule(&args->f_id, NULL, NULL);
	if (q != NULL) { /* should never occur */
		if (last_log != time_second) {
			last_log = time_second;
			kprintf(" install_state: entry already present, done\n");
		}
		return 0;
	}

	if (dyn_count >= dyn_max) {
		/*
		 * Run out of slots, try to remove any expired rule.
		 */
		remove_dyn_rule_locked(NULL, (ipfw_dyn_rule *)1);
		if (dyn_count >= dyn_max) {
			if (last_log != time_second) {
				last_log = time_second;
				kprintf("install_state: "
					"Too many dynamic rules\n");
			}
			return 1; /* cannot install, notify caller */
		}
	}

	switch (cmd->o.opcode) {
	case O_KEEP_STATE: /* bidir rule */
		if (add_dyn_rule(&args->f_id, O_KEEP_STATE, rule) == NULL)
			return 1;
		break;

	case O_LIMIT: /* limit number of sessions */
		{
			uint16_t limit_mask = cmd->limit_mask;
			struct ipfw_flow_id id;
			ipfw_dyn_rule *parent;

			DPRINTF("installing dyn-limit rule %d\n",
				cmd->conn_limit);

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
				remove_dyn_rule_locked(rule, parent);
				if (parent->count >= cmd->conn_limit) {
					if (fw_verbose &&
					    last_log != time_second) {
						last_log = time_second;
						log(LOG_SECURITY | LOG_DEBUG,
						    "drop session, "
						    "too many entries\n");
					}
					return 1;
				}
			}
			if (add_dyn_rule(&args->f_id, O_LIMIT,
					 (struct ip_fw *)parent) == NULL)
				return 1;
		}
		break;
	default:
		kprintf("unknown dynamic rule type %u\n", cmd->o.opcode);
		return 1;
	}
	lookup_dyn_rule(&args->f_id, NULL, NULL); /* XXX just set lifetime */
	return 0;
}

static int
install_state(struct ip_fw *rule, ipfw_insn_limit *cmd,
	      struct ip_fw_args *args, int *deny)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	uint32_t gen;
	int ret = 0;

	*deny = 0;
	gen = ctx->ipfw_gen;

	lockmgr(&dyn_lock, LK_EXCLUSIVE);
	if (ctx->ipfw_gen != gen) {
		/* See the comment in lookup_rule() */
		*deny = 1;
	} else {
		ret = install_state_locked(rule, cmd, args);
	}
	lockmgr(&dyn_lock, LK_RELEASE);

	return ret;
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

	MGETHDR(m, M_NOWAIT, MT_HEADER);
	if (m == NULL)
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

		if ((tcp->th_flags & TH_RST) == 0) {
			send_pkt(&args->f_id, ntohl(tcp->th_seq),
				 ntohl(tcp->th_ack), tcp->th_flags | TH_RST);
		}
		m_freem(args->m);
	} else {
		m_freem(args->m);
	}
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
	if (cmd->opcode == O_SKIPTO) {
		for (rule = me->next; rule; rule = rule->next) {
			if (rule->rulenum >= cmd->arg1)
				break;
		}
	}
	if (rule == NULL)			/* failure or not a skipto */
		rule = me->next;
	me->next_rule = rule;
	return rule;
}

static int
_ipfw_match_uid(const struct ipfw_flow_id *fid, struct ifnet *oif,
		enum ipfw_opcodes opcode, uid_t uid)
{
	struct in_addr src_ip, dst_ip;
	struct inpcbinfo *pi;
	boolean_t wildcard;
	struct inpcb *pcb;

	if (fid->proto == IPPROTO_TCP) {
		wildcard = FALSE;
		pi = &tcbinfo[mycpuid];
	} else if (fid->proto == IPPROTO_UDP) {
		wildcard = TRUE;
		pi = &udbinfo[mycpuid];
	} else {
		return 0;
	}

	/*
	 * Values in 'fid' are in host byte order
	 */
	dst_ip.s_addr = htonl(fid->dst_ip);
	src_ip.s_addr = htonl(fid->src_ip);
	if (oif) {
		pcb = in_pcblookup_hash(pi,
			dst_ip, htons(fid->dst_port),
			src_ip, htons(fid->src_port),
			wildcard, oif);
	} else {
		pcb = in_pcblookup_hash(pi,
			src_ip, htons(fid->src_port),
			dst_ip, htons(fid->dst_port),
			wildcard, NULL);
	}
	if (pcb == NULL || pcb->inp_socket == NULL)
		return 0;

	if (opcode == O_UID) {
#define socheckuid(a,b)	((a)->so_cred->cr_uid != (b))
		return !socheckuid(pcb->inp_socket, uid);
#undef socheckuid
	} else  {
		return groupmember(uid, pcb->inp_socket->so_cred);
	}
}

static int
ipfw_match_uid(const struct ipfw_flow_id *fid, struct ifnet *oif,
	       enum ipfw_opcodes opcode, uid_t uid, int *deny)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	uint32_t gen;
	int match = 0;

	*deny = 0;
	gen = ctx->ipfw_gen;

	if (gen != ctx->ipfw_gen) {
		/* See the comment in lookup_rule() */
		*deny = 1;
	} else {
		match = _ipfw_match_uid(fid, oif, opcode, uid);
	}
	return match;
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
 *	args->f_id	Addresses grabbed from the packet (out)
 *
 * Return value:
 *
 *	If the packet was denied/rejected and has been dropped, *m is equal
 *	to NULL upon return.
 *
 *	IP_FW_DENY	the packet must be dropped.
 *	IP_FW_PASS	The packet is to be accepted and routed normally.
 *	IP_FW_DIVERT	Divert the packet to port (args->cookie)
 *	IP_FW_TEE	Tee the packet to port (args->cookie)
 *	IP_FW_DUMMYNET	Send the packet to pipe/queue (args->cookie)
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
	int retval = IP_FW_PASS;
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

	/*
	 * dyn_dir = MATCH_UNKNOWN when rules unchecked,
	 * 	MATCH_NONE when checked and not matched (dyn_f = NULL),
	 *	MATCH_FORWARD or MATCH_REVERSE otherwise (dyn_f != NULL)
	 */
	int dyn_dir = MATCH_UNKNOWN;
	struct ip_fw *dyn_f = NULL;
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
			return IP_FW_PASS;

		/* This rule is being/has been flushed */
		if (ipfw_flushing)
			return IP_FW_DENY;

		KASSERT(args->rule->cpuid == mycpuid,
			("rule used on cpu%d", mycpuid));

		/* This rule was deleted */
		if (args->rule->rule_flags & IPFW_RULE_F_INVALID)
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

		f = ctx->ipfw_layer3_chain;
		if (args->eh == NULL && skipto != 0) {
			/* No skipto during rule flushing */
			if (ipfw_flushing)
				return IP_FW_DENY;

			if (skipto >= IPFW_DEFAULT_RULE)
				return IP_FW_DENY; /* invalid */

			while (f && f->rulenum <= skipto)
				f = f->next;
			if (f == NULL)	/* drop packet */
				return IP_FW_DENY;
		} else if (ipfw_flushing) {
			/* Rules are being flushed; skip to default rule */
			f = ctx->ipfw_default_rule;
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
		if (ctx->ipfw_set_disable & (1 << f->set))
			continue;

		skip_or = 0;
		for (l = f->cmd_len, cmd = f->cmd; l > 0;
		     l -= cmdlen, cmd += cmdlen) {
			int match, deny;

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

				match = ipfw_match_uid(&args->f_id, oif,
					cmd->opcode,
					(uid_t)((ipfw_insn_u32 *)cmd)->d[0],
					&deny);
				if (deny)
					return IP_FW_DENY;
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
					uint32_t *d = (uint32_t *)(cmd + 1);
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
			 *   the packet must be dropped ('goto done' after
			 *   setting retval).  If static rules are changed
			 *   during the state installation, the packet will
			 *   be dropped and rule's stats will not beupdated
			 *   ('return IP_FW_DENY').
			 *
			 * O_PROBE_STATE and O_CHECK_STATE: these opcodes
			 *   cause a lookup of the state table, and a jump
			 *   to the 'action' part of the parent rule
			 *   ('goto check_body') if an entry is found, or
			 *   (CHECK_STATE only) a jump to the next rule if
			 *   the entry is not found ('goto next_rule').
			 *   The result of the lookup is cached to make
			 *   further instances of these opcodes are
			 *   effectively NOPs.  If static rules are changed
			 *   during the state looking up, the packet will
			 *   be dropped and rule's stats will not be updated
			 *   ('return IP_FW_DENY').
			 */
			case O_LIMIT:
			case O_KEEP_STATE:
				if (!(f->rule_flags & IPFW_RULE_F_STATE)) {
					kprintf("%s rule (%d) is not ready "
						"on cpu%d\n",
						cmd->opcode == O_LIMIT ?
						"limit" : "keep state",
						f->rulenum, f->cpuid);
					goto next_rule;
				}
				if (install_state(f,
				    (ipfw_insn_limit *)cmd, args, &deny)) {
					if (deny)
						return IP_FW_DENY;

					retval = IP_FW_DENY;
					goto done; /* error/limit violation */
				}
				if (deny)
					return IP_FW_DENY;
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
				if (dyn_dir == MATCH_UNKNOWN) {
					dyn_f = lookup_rule(&args->f_id,
						&dyn_dir,
						proto == IPPROTO_TCP ?
						L3HDR(struct tcphdr, ip) : NULL,
						ip_len, &deny);
					if (deny)
						return IP_FW_DENY;
					if (dyn_f != NULL) {
						/*
						 * Found a rule from a dynamic
						 * entry; jump to the 'action'
						 * part of the rule.
						 */
						f = dyn_f;
						cmd = ACTION_PTR(f);
						l = f->cmd_len - f->act_ofs;
						goto check_body;
					}
				}
				/*
				 * Dynamic entry not found. If CHECK_STATE,
				 * skip to next rule, if PROBE_STATE just
				 * ignore and continue with next opcode.
				 */
				if (cmd->opcode == O_CHECK_STATE)
					goto next_rule;
				else if (!(f->rule_flags & IPFW_RULE_F_STATE))
					goto next_rule; /* not ready yet */
				match = 1;
				break;

			case O_ACCEPT:
				retval = IP_FW_PASS;	/* accept */
				goto done;

			case O_PIPE:
			case O_QUEUE:
				args->rule = f; /* report matching rule */
				args->cookie = cmd->arg1;
				retval = IP_FW_DUMMYNET;
				goto done;

			case O_DIVERT:
			case O_TEE:
				if (args->eh) /* not on layer 2 */
					break;

				mtag = m_tag_get(PACKET_TAG_IPFW_DIVERT,
						 sizeof(*divinfo), M_NOWAIT);
				if (mtag == NULL) {
					retval = IP_FW_DENY;
					goto done;
				}
				divinfo = m_tag_data(mtag);

				divinfo->skipto = f->rulenum;
				divinfo->port = cmd->arg1;
				divinfo->tee = (cmd->opcode == O_TEE);
				m_tag_prepend(m, mtag);

				args->cookie = cmd->arg1;
				retval = (cmd->opcode == O_DIVERT) ?
					 IP_FW_DIVERT : IP_FW_TEE;
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
					/*
					 * Update statistics before the possible
					 * blocking 'send_reject'
					 */
					f->pcnt++;
					f->bcnt += ip_len;
					f->timestamp = time_second;

					send_reject(args, cmd->arg1,
					    offset,ip_len);
					m = args->m;

					/*
					 * Return directly here, rule stats
					 * have been updated above.
					 */
					return IP_FW_DENY;
				}
				/* FALLTHROUGH */
			case O_DENY:
				retval = IP_FW_DENY;
				goto done;

			case O_FORWARD_IP:
				if (args->eh)	/* not valid on layer2 pkts */
					break;
				if (!dyn_f || dyn_dir == MATCH_FORWARD) {
					struct sockaddr_in *sin;

					mtag = m_tag_get(PACKET_TAG_IPFORWARD,
					       sizeof(*sin), M_NOWAIT);
					if (mtag == NULL) {
						retval = IP_FW_DENY;
						goto done;
					}
					sin = m_tag_data(mtag);

					/* Structure copy */
					*sin = ((ipfw_insn_sa *)cmd)->sa;

					m_tag_prepend(m, mtag);
					m->m_pkthdr.fw_flags |=
						IPFORWARD_MBUF_TAGGED;
					m->m_pkthdr.fw_flags &=
						~BRIDGE_MBUF_TAGGED;
				}
				retval = IP_FW_PASS;
				goto done;

			default:
				panic("-- unknown opcode %d", cmd->opcode);
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
	return IP_FW_DENY;

done:
	/* Update statistics */
	f->pcnt++;
	f->bcnt += ip_len;
	f->timestamp = time_second;
	return retval;

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

	mtag = m_tag_get(PACKET_TAG_DUMMYNET, sizeof(*pkt), M_NOWAIT);
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

	ipfw_ref_rule(fwa->rule);
	pkt->dn_priv = fwa->rule;
	pkt->dn_unref_priv = ipfw_unref_rule;

	if (cmd->opcode == O_PIPE)
		pkt->dn_flags |= DN_FLAGS_IS_PIPE;

	m->m_pkthdr.fw_flags |= DUMMYNET_MBUF_TAGGED;
}

/*
 * When a rule is added/deleted, clear the next_rule pointers in all rules.
 * These will be reconstructed on the fly as packets are matched.
 * Must be called at splimp().
 */
static void
ipfw_flush_rule_ptrs(struct ipfw_context *ctx)
{
	struct ip_fw *rule;

	for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next)
		rule->next_rule = NULL;
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
ipfw_link_sibling(struct netmsg_ipfw *fwmsg, struct ip_fw *rule)
{
	if (fwmsg->sibling != NULL) {
		KKASSERT(mycpuid > 0 && fwmsg->sibling->cpuid == mycpuid - 1);
		fwmsg->sibling->sibling = rule;
	}
	fwmsg->sibling = rule;
}

static struct ip_fw *
ipfw_create_rule(const struct ipfw_ioc_rule *ioc_rule, struct ip_fw_stub *stub)
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
	rule->cpuid = mycpuid;

	rule->stub = stub;
	if (stub != NULL)
		stub->rule[mycpuid] = rule;

	return rule;
}

static void
ipfw_add_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw *fwmsg = (struct netmsg_ipfw *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	rule = ipfw_create_rule(fwmsg->ioc_rule, fwmsg->stub);

	/*
	 * Bump generation after ipfw_create_rule(),
	 * since this function is blocking
	 */
	ctx->ipfw_gen++;

	/*
	 * Insert rule into the pre-determined position
	 */
	if (fwmsg->prev_rule != NULL) {
		struct ip_fw *prev, *next;

		prev = fwmsg->prev_rule;
		KKASSERT(prev->cpuid == mycpuid);

		next = fwmsg->next_rule;
		KKASSERT(next->cpuid == mycpuid);

		rule->next = next;
		prev->next = rule;

		/*
		 * Move to the position on the next CPU
		 * before the msg is forwarded.
		 */
		fwmsg->prev_rule = prev->sibling;
		fwmsg->next_rule = next->sibling;
	} else {
		KKASSERT(fwmsg->next_rule == NULL);
		rule->next = ctx->ipfw_layer3_chain;
		ctx->ipfw_layer3_chain = rule;
	}

	/* Link rule CPU sibling */
	ipfw_link_sibling(fwmsg, rule);

	ipfw_flush_rule_ptrs(ctx);

	if (mycpuid == 0) {
		/* Statistics only need to be updated once */
		ipfw_inc_static_count(rule);

		/* Return the rule on CPU0 */
		nmsg->lmsg.u.ms_resultp = rule;
	}

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
ipfw_enable_state_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	struct ip_fw *rule = lmsg->u.ms_resultp;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

	ctx->ipfw_gen++;

	KKASSERT(rule->cpuid == mycpuid);
	KKASSERT(rule->stub != NULL && rule->stub->rule[mycpuid] == rule);
	KKASSERT(!(rule->rule_flags & IPFW_RULE_F_STATE));
	rule->rule_flags |= IPFW_RULE_F_STATE;
	lmsg->u.ms_resultp = rule->sibling;

	ifnet_forwardmsg(lmsg, mycpuid + 1);
}

/*
 * Add a new rule to the list.  Copy the rule into a malloc'ed area,
 * then possibly create a rule number and add the rule to the list.
 * Update the rule_number in the input struct so the caller knows
 * it as well.
 */
static void
ipfw_add_rule(struct ipfw_ioc_rule *ioc_rule, uint32_t rule_flags)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct netmsg_ipfw fwmsg;
	struct netmsg_base *nmsg;
	struct ip_fw *f, *prev, *rule;
	struct ip_fw_stub *stub;

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
		for (f = ctx->ipfw_layer3_chain; f; f = f->next) {
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

	/*
	 * Now find the right place for the new rule in the sorted list.
	 */
	for (prev = NULL, f = ctx->ipfw_layer3_chain; f;
	     prev = f, f = f->next) {
		if (f->rulenum > ioc_rule->rulenum) {
			/* Found the location */
			break;
		}
	}
	KASSERT(f != NULL, ("no default rule?!"));

	if (rule_flags & IPFW_RULE_F_STATE) {
		int size;

		/*
		 * If the new rule will create states, then allocate
		 * a rule stub, which will be referenced by states
		 * (dyn rules)
		 */
		size = sizeof(*stub) + ((ncpus - 1) * sizeof(struct ip_fw *));
		stub = kmalloc(size, M_IPFW, M_WAITOK | M_ZERO);
	} else {
		stub = NULL;
	}

	/*
	 * Duplicate the rule onto each CPU.
	 * The rule duplicated on CPU0 will be returned.
	 */
	bzero(&fwmsg, sizeof(fwmsg));
	nmsg = &fwmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
		    0, ipfw_add_rule_dispatch);
	fwmsg.ioc_rule = ioc_rule;
	fwmsg.prev_rule = prev;
	fwmsg.next_rule = prev == NULL ? NULL : f;
	fwmsg.stub = stub;

	ifnet_domsg(&nmsg->lmsg, 0);
	KKASSERT(fwmsg.prev_rule == NULL && fwmsg.next_rule == NULL);

	rule = nmsg->lmsg.u.ms_resultp;
	KKASSERT(rule != NULL && rule->cpuid == mycpuid);

	if (rule_flags & IPFW_RULE_F_STATE) {
		/*
		 * Turn on state flag, _after_ everything on all
		 * CPUs have been setup.
		 */
		bzero(nmsg, sizeof(*nmsg));
		netmsg_init(nmsg, NULL, &curthread->td_msgport,
			    0, ipfw_enable_state_dispatch);
		nmsg->lmsg.u.ms_resultp = rule;

		ifnet_domsg(&nmsg->lmsg, 0);
		KKASSERT(nmsg->lmsg.u.ms_resultp == NULL);
	}

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
	struct ip_fw *n;
	struct ip_fw_stub *stub;

	ctx->ipfw_gen++;

	/* STATE flag should have been cleared before we reach here */
	KKASSERT((rule->rule_flags & IPFW_RULE_F_STATE) == 0);

	stub = rule->stub;
	n = rule->next;
	if (prev == NULL)
		ctx->ipfw_layer3_chain = n;
	else
		prev->next = n;

	/* Mark the rule as invalid */
	rule->rule_flags |= IPFW_RULE_F_INVALID;
	rule->next_rule = NULL;
	rule->sibling = NULL;
	rule->stub = NULL;
#ifdef foo
	/* Don't reset cpuid here; keep various assertion working */
	rule->cpuid = -1;
#endif

	/* Statistics only need to be updated once */
	if (mycpuid == 0)
		ipfw_dec_static_count(rule);

	/* Free 'stub' on the last CPU */
	if (stub != NULL && mycpuid == ncpus - 1)
		kfree(stub, M_IPFW);

	/* Try to free this rule */
	ipfw_free_rule(rule);

	/* Return the next rule */
	return n;
}

static void
ipfw_flush_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	int kill_default = lmsg->u.ms_result;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	ipfw_flush_rule_ptrs(ctx); /* more efficient to do outside the loop */

	while ((rule = ctx->ipfw_layer3_chain) != NULL &&
	       (kill_default || rule->rulenum != IPFW_DEFAULT_RULE))
		ipfw_delete_rule(ctx, NULL, rule);

	ifnet_forwardmsg(lmsg, mycpuid + 1);
}

static void
ipfw_disable_rule_state_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	ctx->ipfw_gen++;

	rule = dmsg->start_rule;
	if (rule != NULL) {
		KKASSERT(rule->cpuid == mycpuid);

		/*
		 * Move to the position on the next CPU
		 * before the msg is forwarded.
		 */
		dmsg->start_rule = rule->sibling;
	} else {
		KKASSERT(dmsg->rulenum == 0);
		rule = ctx->ipfw_layer3_chain;
	}

	while (rule != NULL) {
		if (dmsg->rulenum && rule->rulenum != dmsg->rulenum)
			break;
		rule->rule_flags &= ~IPFW_RULE_F_STATE;
		rule = rule->next;
	}

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * Deletes all rules from a chain (including the default rule
 * if the second argument is set).
 * Must be called at splimp().
 */
static void
ipfw_flush(int kill_default)
{
	struct netmsg_del dmsg;
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg;
	struct ip_fw *rule;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

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
	 * Clear STATE flag on rules, so no more states (dyn rules)
	 * will be created.
	 */
	bzero(&dmsg, sizeof(dmsg));
	netmsg_init(&dmsg.base, NULL, &curthread->td_msgport,
		    0, ipfw_disable_rule_state_dispatch);
	ifnet_domsg(&dmsg.base.lmsg, 0);

	/*
	 * This actually nukes all states (dyn rules)
	 */
	lockmgr(&dyn_lock, LK_EXCLUSIVE);
	for (rule = ctx->ipfw_layer3_chain; rule != NULL; rule = rule->next) {
		/*
		 * Can't check IPFW_RULE_F_STATE here,
		 * since it has been cleared previously.
		 * Check 'stub' instead.
		 */
		if (rule->stub != NULL) {
			/* Force removal */
			remove_dyn_rule_locked(rule, NULL);
		}
	}
	lockmgr(&dyn_lock, LK_RELEASE);

	/*
	 * Press the 'flush' button
	 */
	bzero(&nmsg, sizeof(nmsg));
	netmsg_init(&nmsg, NULL, &curthread->td_msgport,
		    0, ipfw_flush_dispatch);
	lmsg = &nmsg.lmsg;
	lmsg->u.ms_result = kill_default;
	ifnet_domsg(lmsg, 0);

	KASSERT(dyn_count == 0, ("%u dyn rule remains", dyn_count));

	if (kill_default) {
		if (ipfw_dyn_v != NULL) {
			/*
			 * Free dynamic rules(state) hash table
			 */
			kfree(ipfw_dyn_v, M_IPFW);
			ipfw_dyn_v = NULL;
		}

		KASSERT(static_count == 0,
			("%u static rules remain", static_count));
		KASSERT(static_ioc_len == 0,
			("%u bytes of static rules remain", static_ioc_len));
	} else {
		KASSERT(static_count == 1,
			("%u static rules remain", static_count));
		KASSERT(static_ioc_len == IOC_RULESIZE(ctx->ipfw_default_rule),
			("%u bytes of static rules remain, should be %lu",
			 static_ioc_len,
			 (u_long)IOC_RULESIZE(ctx->ipfw_default_rule)));
	}

	/* Flush is done */
	ipfw_flushing = 0;
}

static void
ipfw_alt_delete_rule_dispatch(netmsg_t nmsg)
{
	struct netmsg_del *dmsg = (struct netmsg_del *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule, *prev;

	rule = dmsg->start_rule;
	KKASSERT(rule->cpuid == mycpuid);
	dmsg->start_rule = rule->sibling;

	prev = dmsg->prev_rule;
	if (prev != NULL) {
		KKASSERT(prev->cpuid == mycpuid);

		/*
		 * Move to the position on the next CPU
		 * before the msg is forwarded.
		 */
		dmsg->prev_rule = prev->sibling;
	}

	/*
	 * flush pointers outside the loop, then delete all matching
	 * rules.  'prev' remains the same throughout the cycle.
	 */
	ipfw_flush_rule_ptrs(ctx);
	while (rule && rule->rulenum == dmsg->rulenum)
		rule = ipfw_delete_rule(ctx, prev, rule);

	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static int
ipfw_alt_delete_rule(uint16_t rulenum)
{
	struct ip_fw *prev, *rule, *f;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct netmsg_del dmsg;
	struct netmsg_base *nmsg;
	int state;

	/*
	 * Locate first rule to delete
	 */
	for (prev = NULL, rule = ctx->ipfw_layer3_chain;
	     rule && rule->rulenum < rulenum;
	     prev = rule, rule = rule->next)
		; /* EMPTY */
	if (rule->rulenum != rulenum)
		return EINVAL;

	/*
	 * Check whether any rules with the given number will
	 * create states.
	 */
	state = 0;
	for (f = rule; f && f->rulenum == rulenum; f = f->next) {
		if (f->rule_flags & IPFW_RULE_F_STATE) {
			state = 1;
			break;
		}
	}

	if (state) {
		/*
		 * Clear the STATE flag, so no more states will be
		 * created based the rules numbered 'rulenum'.
		 */
		bzero(&dmsg, sizeof(dmsg));
		nmsg = &dmsg.base;
		netmsg_init(nmsg, NULL, &curthread->td_msgport,
			    0, ipfw_disable_rule_state_dispatch);
		dmsg.start_rule = rule;
		dmsg.rulenum = rulenum;

		ifnet_domsg(&nmsg->lmsg, 0);
		KKASSERT(dmsg.start_rule == NULL);

		/*
		 * Nuke all related states
		 */
		lockmgr(&dyn_lock, LK_EXCLUSIVE);
		for (f = rule; f && f->rulenum == rulenum; f = f->next) {
			/*
			 * Can't check IPFW_RULE_F_STATE here,
			 * since it has been cleared previously.
			 * Check 'stub' instead.
			 */
			if (f->stub != NULL) {
				/* Force removal */
				remove_dyn_rule_locked(f, NULL);
			}
		}
		lockmgr(&dyn_lock, LK_RELEASE);
	}

	/*
	 * Get rid of the rule duplications on all CPUs
	 */
	bzero(&dmsg, sizeof(dmsg));
	nmsg = &dmsg.base;
	netmsg_init(nmsg, NULL, &curthread->td_msgport,
		    0, ipfw_alt_delete_rule_dispatch);
	dmsg.prev_rule = prev;
	dmsg.start_rule = rule;
	dmsg.rulenum = rulenum;

	ifnet_domsg(&nmsg->lmsg, 0);
	KKASSERT(dmsg.prev_rule == NULL && dmsg.start_rule == NULL);
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

	ipfw_flush_rule_ptrs(ctx);

	prev = NULL;
	rule = ctx->ipfw_layer3_chain;
	while (rule != NULL) {
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

	ctx->ipfw_gen++;

	for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next) {
		if (rule->set == dmsg->from_set) {
#ifdef INVARIANTS
			cleared = 1;
#endif
			rule->rule_flags &= ~IPFW_RULE_F_STATE;
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
	for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next) {
		if (rule->set == set) {
			del = 1;
			if (rule->rule_flags & IPFW_RULE_F_STATE) {
				state = 1;
				break;
			}
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

		/*
		 * Nuke all related states
		 */
		lockmgr(&dyn_lock, LK_EXCLUSIVE);
		for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next) {
			if (rule->set != set)
				continue;

			/*
			 * Can't check IPFW_RULE_F_STATE here,
			 * since it has been cleared previously.
			 * Check 'stub' instead.
			 */
			if (rule->stub != NULL) {
				/* Force removal */
				remove_dyn_rule_locked(rule, NULL);
			}
		}
		lockmgr(&dyn_lock, LK_RELEASE);
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
	KKASSERT(rule->cpuid == mycpuid);

	/*
	 * Move to the position on the next CPU
	 * before the msg is forwarded.
	 */
	dmsg->start_rule = rule->sibling;

	while (rule && rule->rulenum <= dmsg->rulenum) {
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
	for (rule = ctx->ipfw_layer3_chain; rule && rule->rulenum <= rulenum;
	     rule = rule->next) {
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

	for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next) {
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

	for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next) {
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

static void
ipfw_zero_entry_dispatch(netmsg_t nmsg)
{
	struct netmsg_zent *zmsg = (struct netmsg_zent *)nmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;

	if (zmsg->rulenum == 0) {
		KKASSERT(zmsg->start_rule == NULL);

		ctx->ipfw_norule_counter = 0;
		for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next)
			clear_counters(rule, zmsg->log_only);
	} else {
		struct ip_fw *start = zmsg->start_rule;

		KKASSERT(start->cpuid == mycpuid);
		KKASSERT(start->rulenum == zmsg->rulenum);

		/*
		 * We can have multiple rules with the same number, so we
		 * need to clear them all.
		 */
		for (rule = start; rule && rule->rulenum == zmsg->rulenum;
		     rule = rule->next)
			clear_counters(rule, zmsg->log_only);

		/*
		 * Move to the position on the next CPU
		 * before the msg is forwarded.
		 */
		zmsg->start_rule = start->sibling;
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
		for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next) {
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

/*
 * Check validity of the structure before insert.
 * Fortunately rules are simple, so this mostly need to check rule sizes.
 */
static int
ipfw_check_ioc_rule(struct ipfw_ioc_rule *rule, int size, uint32_t *rule_flags)
{
	int l, cmdlen = 0;
	int have_action = 0;
	ipfw_insn *cmd;

	*rule_flags = 0;

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

	/* Check rule number */
	if (rule->rulenum == IPFW_DEFAULT_RULE) {
		kprintf("ipfw: invalid rule number\n");
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

		DPRINTF("ipfw: opcode %d\n", cmd->opcode);

		if (cmd->opcode == O_KEEP_STATE || cmd->opcode == O_LIMIT) {
			/* This rule will create states */
			*rule_flags |= IPFW_RULE_F_STATE;
		}

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
			if (cmdlen != F_INSN_SIZE(ipfw_insn_sa)) {
				goto bad_size;
			} else {
				in_addr_t fwd_addr;

				fwd_addr = ((ipfw_insn_sa *)cmd)->
					   sa.sin_addr.s_addr;
				if (IN_MULTICAST(ntohl(fwd_addr))) {
					kprintf("ipfw: try forwarding to "
						"multicast address\n");
					return EINVAL;
				}
			}
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
	uint32_t rule_flags;
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

	error = ipfw_check_ioc_rule(ioc_rule, size, &rule_flags);
	if (error)
		return error;

	ipfw_add_rule(ioc_rule, rule_flags);

	if (sopt->sopt_dir == SOPT_GET)
		sopt->sopt_valsize = IOC_RULESIZE(ioc_rule);
	return 0;
}

static void *
ipfw_copy_rule(const struct ip_fw *rule, struct ipfw_ioc_rule *ioc_rule)
{
	const struct ip_fw *sibling;
#ifdef INVARIANTS
	int i;
#endif

	KKASSERT(rule->cpuid == IPFW_CFGCPUID);

	ioc_rule->act_ofs = rule->act_ofs;
	ioc_rule->cmd_len = rule->cmd_len;
	ioc_rule->rulenum = rule->rulenum;
	ioc_rule->set = rule->set;
	ioc_rule->usr_flags = rule->usr_flags;

	ioc_rule->set_disable = ipfw_ctx[mycpuid]->ipfw_set_disable;
	ioc_rule->static_count = static_count;
	ioc_rule->static_len = static_ioc_len;

	/*
	 * Visit (read-only) all of the rule's duplications to get
	 * the necessary statistics
	 */
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

	ioc_state->rulenum = dyn_rule->stub->rule[mycpuid]->rulenum;

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
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ip_fw *rule;
	void *bp;
	size_t size;
	uint32_t dcount = 0;

	/*
	 * pass up a copy of the current rules. Static rules
	 * come first (the last of which has number IPFW_DEFAULT_RULE),
	 * followed by a possibly empty list of dynamic rule.
	 */

	size = static_ioc_len;	/* size of static rules */
	if (ipfw_dyn_v) {	/* add size of dyn.rules */
		dcount = dyn_count;
		size += dcount * sizeof(struct ipfw_ioc_state);
	}

	if (sopt->sopt_valsize < size) {
		/* short length, no need to return incomplete rules */
		/* XXX: if superuser, no need to zero buffer */
		bzero(sopt->sopt_val, sopt->sopt_valsize); 
		return 0;
	}
	bp = sopt->sopt_val;

	for (rule = ctx->ipfw_layer3_chain; rule; rule = rule->next)
		bp = ipfw_copy_rule(rule, bp);

	if (ipfw_dyn_v && dcount != 0) {
		struct ipfw_ioc_state *ioc_state = bp;
		uint32_t dcount2 = 0;
#ifdef INVARIANTS
		size_t old_size = size;
#endif
		int i;

		lockmgr(&dyn_lock, LK_SHARED);

		/* Check 'ipfw_dyn_v' again with lock held */
		if (ipfw_dyn_v == NULL)
			goto skip;

		for (i = 0; i < curr_dyn_buckets; i++) {
			ipfw_dyn_rule *p;

			/*
			 * The # of dynamic rules may have grown after the
			 * snapshot of 'dyn_count' was taken, so we will have
			 * to check 'dcount' (snapshot of dyn_count) here to
			 * make sure that we don't overflow the pre-allocated
			 * buffer.
			 */
			for (p = ipfw_dyn_v[i]; p != NULL && dcount != 0;
			     p = p->next, ioc_state++, dcount--, dcount2++)
				ipfw_copy_state(p, ioc_state);
		}
skip:
		lockmgr(&dyn_lock, LK_RELEASE);

		/*
		 * The # of dynamic rules may be shrinked after the
		 * snapshot of 'dyn_count' was taken.  To give user a
		 * correct dynamic rule count, we use the 'dcount2'
		 * calculated above (with shared lockmgr lock held).
		 */
		size = static_ioc_len +
		       (dcount2 * sizeof(struct ipfw_ioc_state));
		KKASSERT(size <= old_size);
	}

	sopt->sopt_valsize = size;
	return 0;
}

static void
ipfw_set_disable_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];

	ctx->ipfw_gen++;
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
		ipfw_flush(0 /* keep default rule */);
		break;

	case IP_FW_ADD:
		error = ipfw_ctl_add_rule(sopt);
		break;

	case IP_FW_DEL:
		/*
		 * IP_FW_DEL is used for deleting single rules or sets,
		 * and (ab)used to atomically manipulate sets.
		 * Argument size is used to distinguish between the two:
		 *    sizeof(uint32_t)
		 *	delete single rule or set of rules,
		 *	or reassign rules (or sets) to a different set.
		 *    2 * sizeof(uint32_t)
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

		if (sopt->sopt_val != 0) {
		    error = soopt_to_kbuf(sopt, &rulenum,
			    sizeof(int), sizeof(int));
		    if (error)
			break;
		}
		error = ipfw_ctl_zero_entry(rulenum,
			sopt->sopt_name == IP_FW_RESETLOG);
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
ipfw_tick_dispatch(netmsg_t nmsg)
{
	time_t keep_alive;
	uint32_t gen;
	int i;

	IPFW_ASSERT_CFGPORT(&curthread->td_msgport);
	KKASSERT(IPFW_LOADED);

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&nmsg->lmsg, 0);
	crit_exit();

	if (ipfw_dyn_v == NULL || dyn_count == 0)
		goto done;

	keep_alive = time_second;

	lockmgr(&dyn_lock, LK_EXCLUSIVE);
again:
	if (ipfw_dyn_v == NULL || dyn_count == 0) {
		lockmgr(&dyn_lock, LK_RELEASE);
		goto done;
	}
	gen = dyn_buckets_gen;

	for (i = 0; i < curr_dyn_buckets; i++) {
		ipfw_dyn_rule *q, *prev;

		for (prev = NULL, q = ipfw_dyn_v[i]; q != NULL;) {
			uint32_t ack_rev, ack_fwd;
			struct ipfw_flow_id id;

			if (q->dyn_type == O_LIMIT_PARENT)
				goto next;

			if (TIME_LEQ(q->expire, time_second)) {
				/* State expired */
				UNLINK_DYN_RULE(prev, ipfw_dyn_v[i], q);
				continue;
			}

			/*
			 * Keep alive processing
			 */

			if (!dyn_keepalive)
				goto next;
			if (q->id.proto != IPPROTO_TCP)
				goto next;
			if ((q->state & BOTH_SYN) != BOTH_SYN)
				goto next;
			if (TIME_LEQ(time_second + dyn_keepalive_interval,
			    q->expire))
				goto next;	/* too early */
			if (q->keep_alive == keep_alive)
				goto next;	/* alreay done */

			/*
			 * Save necessary information, so that they could
			 * survive after possible blocking in send_pkt()
			 */
			id = q->id;
			ack_rev = q->ack_rev;
			ack_fwd = q->ack_fwd;

			/* Sending has been started */
			q->keep_alive = keep_alive;

			/* Release lock to avoid possible dead lock */
			lockmgr(&dyn_lock, LK_RELEASE);
			send_pkt(&id, ack_rev - 1, ack_fwd, TH_SYN);
			send_pkt(&id, ack_fwd - 1, ack_rev, 0);
			lockmgr(&dyn_lock, LK_EXCLUSIVE);

			if (gen != dyn_buckets_gen) {
				/*
				 * Dyn bucket array has been changed during
				 * the above two sending; reiterate.
				 */
				goto again;
			}
next:
			prev = q;
			q = q->next;
		}
	}
	lockmgr(&dyn_lock, LK_RELEASE);
done:
	callout_reset(&ipfw_timeout_h, dyn_keepalive_period * hz,
		      ipfw_tick, NULL);
}

/*
 * This procedure is only used to handle keepalives. It is invoked
 * every dyn_keepalive_period
 */
static void
ipfw_tick(void *dummy __unused)
{
	struct lwkt_msg *lmsg = &ipfw_timeout_netmsg.lmsg;

	KKASSERT(mycpuid == IPFW_CFGCPUID);

	crit_enter();

	KKASSERT(lmsg->ms_flags & MSGF_DONE);
	if (IPFW_LOADED) {
		lwkt_sendmsg_oncpu(IPFW_CFGPORT, lmsg);
		/* ipfw_timeout_netmsg's handler reset this callout */
	}

	crit_exit();
}

static int
ipfw_check_in(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir)
{
	struct ip_fw_args args;
	struct mbuf *m = *m0;
	struct m_tag *mtag;
	int tee = 0, error = 0, ret;

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

static int
ipfw_sysctl_dyn_buckets(SYSCTL_HANDLER_ARGS)
{
	int error, value;

	lockmgr(&dyn_lock, LK_EXCLUSIVE);

	value = dyn_buckets;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr)
		goto back;

	/*
	 * Make sure we have a power of 2 and
	 * do not allow more than 64k entries.
	 */
	error = EINVAL;
	if (value <= 1 || value > 65536)
		goto back;
	if ((value & (value - 1)) != 0)
		goto back;

	error = 0;
	dyn_buckets = value;
back:
	lockmgr(&dyn_lock, LK_RELEASE);
	return error;
}

static int
ipfw_sysctl_dyn_fin(SYSCTL_HANDLER_ARGS)
{
	return sysctl_int_range(oidp, arg1, arg2, req,
				1, dyn_keepalive_period - 1);
}

static int
ipfw_sysctl_dyn_rst(SYSCTL_HANDLER_ARGS)
{
	return sysctl_int_range(oidp, arg1, arg2, req,
				1, dyn_keepalive_period - 1);
}

static void
ipfw_ctx_init_dispatch(netmsg_t nmsg)
{
	struct netmsg_ipfw *fwmsg = (struct netmsg_ipfw *)nmsg;
	struct ipfw_context *ctx;
	struct ip_fw *def_rule;

	ctx = kmalloc(sizeof(*ctx), M_IPFW, M_WAITOK | M_ZERO);
	ipfw_ctx[mycpuid] = ctx;

	def_rule = kmalloc(sizeof(*def_rule), M_IPFW, M_WAITOK | M_ZERO);

	def_rule->act_ofs = 0;
	def_rule->rulenum = IPFW_DEFAULT_RULE;
	def_rule->cmd_len = 1;
	def_rule->set = IPFW_DEFAULT_SET;

	def_rule->cmd[0].len = 1;
#ifdef IPFIREWALL_DEFAULT_TO_ACCEPT
	def_rule->cmd[0].opcode = O_ACCEPT;
#else
	if (filters_default_to_accept)
		def_rule->cmd[0].opcode = O_ACCEPT;
	else
		def_rule->cmd[0].opcode = O_DENY;
#endif

	def_rule->refcnt = 1;
	def_rule->cpuid = mycpuid;

	/* Install the default rule */
	ctx->ipfw_default_rule = def_rule;
	ctx->ipfw_layer3_chain = def_rule;

	/* Link rule CPU sibling */
	ipfw_link_sibling(fwmsg, def_rule);

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
	ip_fw_ctl_ptr = ipfw_ctl;
	ip_fw_dn_io_ptr = ipfw_dummynet_io;

	kprintf("ipfw2 initialized, default to %s, logging ",
		ipfw_ctx[mycpuid]->ipfw_default_rule->cmd[0].opcode ==
		O_ACCEPT ? "accept" : "deny");

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

	callout_init_mp(&ipfw_timeout_h);
	netmsg_init(&ipfw_timeout_netmsg, NULL, &netisr_adone_rport,
		    MSGF_DROPABLE | MSGF_PRIORITY,
		    ipfw_tick_dispatch);
	lockinit(&dyn_lock, "ipfw_dyn", 0, 0);

	ip_fw_loaded = 1;
	callout_reset(&ipfw_timeout_h, hz, ipfw_tick, NULL);

	if (fw_enable)
		ipfw_hook();
reply:
	lwkt_replymsg(&nmsg->lmsg, error);
}

static int
ipfw_init(void)
{
	struct netmsg_base smsg;

	netmsg_init(&smsg, NULL, &curthread->td_msgport,
		    0, ipfw_init_dispatch);
	return lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
}

#ifdef KLD_MODULE

static void
ipfw_fini_dispatch(netmsg_t nmsg)
{
	int error = 0, cpu;

	if (ipfw_refcnt != 0) {
		error = EBUSY;
		goto reply;
	}

	ip_fw_loaded = 0;

	ipfw_dehook();
	callout_stop(&ipfw_timeout_h);

	netmsg_service_sync();

	crit_enter();
	lwkt_dropmsg(&ipfw_timeout_netmsg.lmsg);
	crit_exit();

	ip_fw_chk_ptr = NULL;
	ip_fw_ctl_ptr = NULL;
	ip_fw_dn_io_ptr = NULL;
	ipfw_flush(1 /* kill default rule */);

	/* Free pre-cpu context */
	for (cpu = 0; cpu < ncpus; ++cpu)
		kfree(ipfw_ctx[cpu], M_IPFW);

	kprintf("IP firewall unloaded\n");
reply:
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
	"ipfw",
	ipfw_modevent,
	0
};
DECLARE_MODULE(ipfw, ipfwmod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_VERSION(ipfw, 1);
