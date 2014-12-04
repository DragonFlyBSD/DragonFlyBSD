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
 * $FreeBSD: src/sys/netinet/ip_fw2.h,v 1.1.2.2 2002/08/16 11:03:11 luigi Exp $
 */

#ifndef _IPFW2_H
#define _IPFW2_H

#ifdef _KERNEL
#include <net/netisr2.h>
#endif

#define		RESERVED_SIZE		12
#define		SIZE_OF_IPFWINSN	8
#define		LEN_OF_IPFWINSN		2
#define		IPFW_DEFAULT_RULE	65535	/* rulenum for the default rule */
#define		IPFW_DEFAULT_SET	31	/* set number for the default rule  */

/*
 * Template for instructions.
 *
 * ipfw_insn is used for all instructions which require no operands,
 * a single 16-bit value (arg1), or a couple of 8-bit values.
 *
 * For other instructions which require different/larger arguments
 * we have derived structures, ipfw_insn_*.
 *
 * The size of the instruction (in 32-bit words) is in the low
 * 6 bits of "len". The 2 remaining bits are used to implement
 * NOT and OR on individual instructions. Given a type, you can
 * compute the length to be put in "len" using F_INSN_SIZE(t)
 *
 * F_NOT	negates the match result of the instruction.
 *
 * F_OR		is used to build or blocks. By default, instructions
 *		are evaluated as part of a logical AND. An "or" block
 *		{ X or Y or Z } contains F_OR set in all but the last
 *		instruction of the block. A match will cause the code
 *		to skip past the last instruction of the block.
 *
 * NOTA BENE: in a couple of places we assume that
 *	sizeof(ipfw_insn) == sizeof(uint32_t)
 * this needs to be fixed.
 *
 */

#define	F_NOT		0x80
#define	F_OR		0x40
#define	F_LEN_MASK	0x3f
#define	F_LEN(cmd)	((cmd)->len & F_LEN_MASK)

typedef struct	_ipfw_insn {	/* template for instructions */
	uint8_t		opcode;
	uint8_t		len;	/* numer of 32-byte words */
	uint16_t	arg1;

	uint8_t		module;
	uint8_t		arg3;
	uint16_t	arg2;
} ipfw_insn;

/*
 * The F_INSN_SIZE(type) computes the size, in 4-byte words, of
 * a given type.
 */
#define	F_INSN_SIZE(t)	((sizeof (t))/sizeof(uint32_t))

#define MTAG_IPFW	1148380143	/* IPFW-tagged cookie */

/*
 * This is used to store an array of 16-bit entries (ports etc.)
 */
typedef struct	_ipfw_insn_u16 {
	ipfw_insn o;
	uint16_t ports[2];	/* there may be more */
} ipfw_insn_u16;

/*
 * This is used to store an array of 32-bit entries
 * (uid, single IPv4 addresses etc.)
 */
typedef struct	_ipfw_insn_u32 {
	ipfw_insn o;
	uint32_t d[1];	/* one or more */
} ipfw_insn_u32;

/*
 * This is used to store IP addr-mask pairs.
 */
typedef struct	_ipfw_insn_ip {
	ipfw_insn o;
	struct in_addr	addr;
	struct in_addr	mask;
} ipfw_insn_ip;

/*
 * This is used to forward to a given address (ip)
 */
typedef struct  _ipfw_insn_sa {
	ipfw_insn o;
	struct sockaddr_in sa;
} ipfw_insn_sa;

/*
 * This is used for MAC addr-mask pairs.
 */
typedef struct	_ipfw_insn_mac {
	ipfw_insn o;
	u_char addr[12];	/* dst[6] + src[6] */
	u_char mask[12];	/* dst[6] + src[6] */
} ipfw_insn_mac;

/*
 * This is used for interface match rules (recv xx, xmit xx)
 */
typedef struct	_ipfw_insn_if {
	ipfw_insn o;
	union {
		struct in_addr ip;
		int glob;
	} p;
	char name[IFNAMSIZ];
} ipfw_insn_if;

/*
 * This is used for pipe and queue actions, which need to store
 * a single pointer (which can have different size on different
 * architectures.
 */
typedef struct	_ipfw_insn_pipe {
	ipfw_insn	o;
	void		*pipe_ptr;
} ipfw_insn_pipe;

/*
 * This is used for limit rules.
 */
typedef struct	_ipfw_insn_limit {
	ipfw_insn o;
	uint8_t _pad;
	uint8_t limit_mask;	/* combination of DYN_* below	*/
#define	DYN_SRC_ADDR	0x1
#define	DYN_SRC_PORT	0x2
#define	DYN_DST_ADDR	0x4
#define	DYN_DST_PORT	0x8

	uint16_t conn_limit;
} ipfw_insn_limit;


/*
 * Here we have the structure representing an ipfw rule.
 *
 * It starts with a general area (with link fields and counters)
 * followed by an array of one or more instructions, which the code
 * accesses as an array of 32-bit values.
 *
 * Given a rule pointer  r:
 *
 *  r->cmd		is the start of the first instruction.
 *  ACTION_PTR(r)	is the start of the first action (things to do
 *			once a rule matched).
 *
 * When assembling instruction, remember the following:
 *
 *  + if a rule has a "keep-state" (or "limit") option, then the
 *	first instruction (at r->cmd) MUST BE an O_PROBE_STATE
 *  + if a rule has a "log" option, then the first action
 *	(at ACTION_PTR(r)) MUST be O_LOG
 *
 * NOTE: we use a simple linked list of rules because we never need
 * 	to delete a rule without scanning the list. We do not use
 *	queue(3) macros for portability and readability.
 */

struct ip_fw {
	struct ip_fw	*next;		/* linked list of rules		*/
	struct ip_fw	*next_rule;	/* ptr to next [skipto] rule	*/
	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t		flags;		/* IPFW_USR_F_			*/

	/* These fields are present in all rules.			*/
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/

	struct ip_fw 	*sibling;	/* pointer to the rule in next CPU */

	ipfw_insn	cmd[1];		/* storage for commands		*/
};


#define IPFW_RULE_F_INVALID	0x1
#define IPFW_RULE_F_STATE	0x2

#define RULESIZE(rule) (sizeof(struct ip_fw) + (rule)->cmd_len * 4 - SIZE_OF_IPFWINSN)

/*
 * This structure is used as a flow mask and a flow id for various
 * parts of the code.
 */
struct ipfw_flow_id {
	uint32_t	dst_ip;
	uint32_t	src_ip;
	uint16_t	dst_port;
	uint16_t	src_port;
	uint8_t		proto;
	uint8_t		flags;	/* protocol-specific flags */
};

struct ip_fw_state {
	struct ip_fw_state	*next;
	struct ipfw_flow_id	flow_id;
	struct ip_fw	*stub;

	uint64_t	pcnt;	   /* packet match counter	 */
	uint64_t	bcnt;	   /* byte match counter	   */

	uint16_t	lifetime;
	uint32_t	timestamp;
	uint32_t	expiry;
};


/* ipfw_chk/ip_fw_chk_ptr return values */
#define IP_FW_PASS	0
#define IP_FW_DENY	1
#define IP_FW_DIVERT	2
#define IP_FW_TEE	3
#define IP_FW_DUMMYNET	4
#define IP_FW_NAT	5
#define IP_FW_ROUTE	6

/* ipfw_chk controller values */
#define IP_FW_CTL_NO		0
#define IP_FW_CTL_DONE		1
#define IP_FW_CTL_AGAIN		2
#define IP_FW_CTL_NEXT		3
#define IP_FW_CTL_NAT		4
#define IP_FW_CTL_LOOP		5
#define IP_FW_CTL_CHK_STATE	6

#define IP_FW_NOT_MATCH		0
#define IP_FW_MATCH		1

/*
 * arguments for calling ipfw_chk() and dummynet_io(). We put them
 * all into a structure because this way it is easier and more
 * efficient to pass variables around and extend the interface.
 */
struct ip_fw_args {
	struct mbuf	*m;		/* the mbuf chain		*/
	struct ifnet	*oif;		/* output interface		*/
	struct ip_fw	*rule;		/* matching rule		*/
	struct ether_header *eh;	/* for bridged packets		*/

	struct ipfw_flow_id f_id;	/* grabbed from IP header	*/

	/*
	 * Depend on the return value of ipfw_chk/ip_fw_chk_ptr
	 * 'cookie' field may save following information:
	 *
	 * IP_FW_TEE or IP_FW_DIVERT
	 *   The divert port number
	 *
	 * IP_FW_DUMMYNET
	 *   The pipe or queue number
	 */
	uint32_t	cookie;
};

#ifdef _KERNEL
/*
 * Function definitions.
 */
int	ip_fw_sockopt(struct sockopt *);
int	ipfw_ctl_x(struct sockopt *sopt);

/* Firewall hooks */
struct sockopt;
struct dn_flow_set;

typedef int	ip_fw_chk_t(struct ip_fw_args *);
typedef int	ip_fw2_ctl_t(struct sockopt *);
typedef int	ipfw_nat_cfg_t(struct sockopt *);
typedef void ip_fw_dn_io_t(struct mbuf *, int, int, struct ip_fw_args *);


extern ip_fw_chk_t	*ip_fw_chk_ptr;
extern ip_fw2_ctl_t	*ip_fw_ctl_x_ptr;
extern ip_fw_dn_io_t	*ip_fw_dn_io_ptr;

extern int fw_one_pass;
extern int fw_enable;

extern int ip_fw_loaded;
#define	IPFW_LOADED	(ip_fw_loaded)

#define IPFW_CFGCPUID	0
#define IPFW_CFGPORT	netisr_cpuport(IPFW_CFGCPUID)
#define IPFW_ASSERT_CFGPORT(msgport)				\
	KASSERT((msgport) == IPFW_CFGPORT, ("not IPFW CFGPORT"))


struct ipfw_context {
	struct ip_fw	*ipfw_rule_chain;		/* list of rules*/
	struct ip_fw	*ipfw_default_rule;	 /* default rule */
	struct ipfw_state_context *state_ctx;
	uint16_t		state_hash_size;
	uint32_t		ipfw_set_disable;
};

struct ipfw_state_context {
	struct ip_fw_state *state;
	struct ip_fw_state *last;
	int	count;
};

struct ipfw_nat_context {
	LIST_HEAD(, cfg_nat) nat;		/* list of nat entries */
};

typedef void (*filter_func)(int *cmd_ctl,int *cmd_val,struct ip_fw_args **args,
struct ip_fw **f,ipfw_insn *cmd,uint16_t ip_len);
void register_ipfw_filter_funcs(int module,int opcode,filter_func func);
void unregister_ipfw_filter_funcs(int module,filter_func func);
void register_ipfw_module(int module_id,char *module_name);
int unregister_ipfw_module(int module_id);

#endif

#define ACTION_PTR(rule)	\
	(ipfw_insn *)((uint32_t *)((rule)->cmd) + ((rule)->act_ofs))



struct ipfw_ioc_rule {
	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t		usr_flags;	/* IPFW_USR_F_ 			*/

	/* Rule set information */
	uint32_t	set_disable;	/* disabled rule sets		*/
	uint32_t	static_count;	/* # of static rules		*/
	uint32_t	static_len;	/* total length of static rules	*/

	/* Statistics */
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/

	uint8_t		reserved[RESERVED_SIZE];

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

#define IPFW_USR_F_NORULE	0x01

#define IPFW_RULE_SIZE_MAX	255	/* unit: uint32_t */

#define IOC_RULESIZE(rule)	\
	(sizeof(struct ipfw_ioc_rule) + (rule)->cmd_len * 4 - SIZE_OF_IPFWINSN)

struct ipfw_ioc_flowid {
	uint16_t	type;	/* ETHERTYPE_ */
	uint16_t	pad;
	union {
		struct {
			uint32_t dst_ip;
			uint32_t src_ip;
			uint16_t dst_port;
			uint16_t src_port;
			uint8_t proto;
		} ip;
		uint8_t pad[64];
	} u;
};

struct ipfw_ioc_state {
	uint64_t	pcnt;		/* packet match counter		*/
	uint64_t	bcnt;		/* byte match counter		*/
	uint16_t 	lifetime;
	uint32_t	timestamp;	/* alive time				*/
	uint32_t	expiry;		/* expire time				*/

	uint16_t	rulenum;
	uint16_t	cpuid;		
	struct ipfw_flow_id 	flow_id;	/* proto +src/dst ip/port */
	uint8_t		reserved[16];
};

/*
 * Definitions for IP option names.
 */
#define	IP_FW_IPOPT_LSRR	0x01
#define	IP_FW_IPOPT_SSRR	0x02
#define	IP_FW_IPOPT_RR		0x04
#define	IP_FW_IPOPT_TS		0x08

/*
 * Definitions for TCP option names.
 */
#define	IP_FW_TCPOPT_MSS	0x01
#define	IP_FW_TCPOPT_WINDOW	0x02
#define	IP_FW_TCPOPT_SACK	0x04
#define	IP_FW_TCPOPT_TS		0x08
#define	IP_FW_TCPOPT_CC		0x10

#define	ICMP_REJECT_RST		0x100	/* fake ICMP code (send a TCP RST) */

struct ipfw_module{
	int type;
	int id;
	char name[20];
};

#define IPFW_KEYWORD_TYPE_NONE		0
#define IPFW_KEYWORD_TYPE_ACTION	1
#define IPFW_KEYWORD_TYPE_FILTER	2
#define IPFW_KEYWORD_TYPE_OTHERS	3

#define IPFW_MAPPING_TYPE_NONE		0
#define IPFW_MAPPING_TYPE_IN_USE	1

#define NEED1(msg)  {if (ac < 1) errx(EX_USAGE, msg);}
#define NEED2(msg)  {if (ac < 2) errx(EX_USAGE, msg);}
#define NEED(c, n, msg) {if (c < n) errx(EX_USAGE, msg);}

#define NEXT_ARG	ac--; if(ac > 0){av++;}
#define NEXT_ARG1 	(*ac)--; if(*ac > 0){(*av)++;}

#ifdef IPFIREWALL_DEBUG
#define DPRINTF(fmt, ...)			\
do {						\
	if (fw_debug > 0)			\
		kprintf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define DPRINTF(fmt, ...)   ((void)0)
#endif

#define MATCH_REVERSE	0
#define MATCH_FORWARD	1
#define MATCH_NONE	2
#define MATCH_UNKNOWN	3

#define BOTH_SYN	(TH_SYN | (TH_SYN << 8))
#define BOTH_FIN	(TH_FIN | (TH_FIN << 8))

#define TIME_LEQ(a, b)  ((int)((a) - (b)) <= 0)
#define L3HDR(T, ip) ((T *)((uint32_t *)(ip) + (ip)->ip_hl))

/* IP_FW_X header/opcodes */
typedef struct _ip_fw_x_header {
	uint16_t opcode;	/* Operation opcode */
	uint16_t _pad;   	/* Opcode version */
} ip_fw_x_header;

typedef void ipfw_basic_delete_state_t(struct ip_fw *);
typedef void ipfw_basic_append_state_t(struct ipfw_ioc_state *);

/* IP_FW2 opcodes */

#define IP_FW_ADD		50   /* add a firewall rule to chain */
#define IP_FW_DEL		51   /* delete a firewall rule from chain */
#define IP_FW_FLUSH		52   /* flush firewall rule chain */
#define IP_FW_ZERO		53   /* clear single/all firewall counter(s) */
#define IP_FW_GET		54   /* get entire firewall rule chain */
#define IP_FW_RESETLOG		55   /* reset logging counters */

#define IP_DUMMYNET_CONFIGURE	60   /* add/configure a dummynet pipe */
#define IP_DUMMYNET_DEL		61   /* delete a dummynet pipe from chain */
#define IP_DUMMYNET_FLUSH	62   /* flush dummynet */
#define IP_DUMMYNET_GET		64   /* get entire dummynet pipes */

#define IP_FW_MODULE		67  /* get modules names */

#define IP_FW_NAT_CFG		68   /* add/config a nat rule */
#define IP_FW_NAT_DEL		69   /* delete a nat rule */
#define IP_FW_NAT_FLUSH		70   /* get configuration of a nat rule */
#define IP_FW_NAT_GET		71   /* get log of a nat rule */
#define IP_FW_NAT_LOG		72   /* get log of a nat rule */

#define IP_FW_STATE_ADD		56   /* add one state */
#define IP_FW_STATE_DEL		57   /* delete states of one rulenum */
#define IP_FW_STATE_FLUSH	58   /* flush all states */
#endif /* _IPFW2_H */
