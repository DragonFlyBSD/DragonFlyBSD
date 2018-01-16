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

/*
 * The kernel representation of ipfw rules is made of a list of
 * 'instructions' (for all practical purposes equivalent to BPF
 * instructions), which specify which fields of the packet
 * (or its metatada) should be analysed.
 *
 * Each instruction is stored in a structure which begins with
 * "ipfw_insn", and can contain extra fields depending on the
 * instruction type (listed below).
 *
 * "enum ipfw_opcodes" are the opcodes supported. We can have up
 * to 256 different opcodes.
 */

enum ipfw_opcodes {		/* arguments (4 byte each)	*/
	O_NOP,

	O_IP_SRC,		/* u32 = IP			*/
	O_IP_SRC_MASK,		/* ip = IP/mask			*/
	O_IP_SRC_ME,		/* none				*/
	O_IP_SRC_SET,		/* u32=base, arg1=len, bitmap	*/

	O_IP_DST,		/* u32 = IP			*/
	O_IP_DST_MASK,		/* ip = IP/mask			*/
	O_IP_DST_ME,		/* none				*/
	O_IP_DST_SET,		/* u32=base, arg1=len, bitmap	*/

	O_IP_SRCPORT,		/* (n)port list:mask 4 byte ea	*/
	O_IP_DSTPORT,		/* (n)port list:mask 4 byte ea	*/
	O_PROTO,		/* arg1=protocol		*/

	O_MACADDR2,		/* 2 mac addr:mask		*/
	O_MAC_TYPE,		/* same as srcport		*/

	O_LAYER2,		/* none				*/
	O_IN,			/* none				*/
	O_FRAG,			/* none				*/

	O_RECV,			/* none				*/
	O_XMIT,			/* none				*/
	O_VIA,			/* none				*/

	O_IPOPT,		/* arg1 = 2*u8 bitmap		*/
	O_IPLEN,		/* arg1 = len			*/
	O_IPID,			/* arg1 = id			*/

	O_IPTOS,		/* arg1 = id			*/
	O_IPPRECEDENCE,		/* arg1 = precedence << 5	*/
	O_IPTTL,		/* arg1 = TTL			*/

	O_IPVER,		/* arg1 = version		*/
	O_UID,			/* u32 = id			*/
	O_GID,			/* u32 = id			*/
	O_ESTAB,		/* none (tcp established)	*/
	O_TCPFLAGS,		/* arg1 = 2*u8 bitmap		*/
	O_TCPWIN,		/* arg1 = desired win		*/
	O_TCPSEQ,		/* u32 = desired seq.		*/
	O_TCPACK,		/* u32 = desired seq.		*/
	O_ICMPTYPE,		/* 1*u32 = icmp type bitmap	*/
	O_TCPOPTS,		/* arg1 = 2*u8 bitmap		*/

	/* States. */
	O_PROBE_STATE,		/* none				*/
	O_KEEP_STATE,		/* none				*/
	O_LIMIT,		/* ipfw_insn_limit		*/
	O_LIMIT_PARENT,		/* dyn_type, not an opcode.	*/

	/* Actions. */
	O_LOG,			/* ipfw_insn_log		*/
	O_PROB,			/* u32 = match probability	*/
	O_CHECK_STATE,		/* none				*/
	O_ACCEPT,		/* none				*/
	O_DENY,			/* none 			*/
	O_REJECT,		/* arg1=icmp arg (same as deny)	*/
	O_COUNT,		/* none				*/
	O_SKIPTO,		/* arg1=next rule number	*/
	O_PIPE,			/* arg1=pipe number		*/
	O_QUEUE,		/* arg1=queue number		*/
	O_DIVERT,		/* arg1=port number		*/
	O_TEE,			/* arg1=port number		*/
	O_FORWARD_IP,		/* fwd sockaddr			*/
	O_FORWARD_MAC,		/* fwd mac			*/

	/* Table based filters. */
	O_IP_SRC_TABLE,		/* arg1 = tableid		*/
	O_IP_DST_TABLE,		/* arg1 = tableid		*/

	/* Action. */
	O_DEFRAG,		/* none				*/

	/* Filters. */
	O_IPFRAG,		/* none				*/
	O_IP_SRC_IFIP,		/* ipfw_insn_ifip		*/
	O_IP_DST_IFIP,		/* ipfw_insn_ifip		*/

	/* Translates. */
	O_REDIRECT,		/* ipfw_insn_rdr		*/
	O_RESERVED1,		/* reserved for NAT		*/

	O_ICMPCODE,		/* 1*u32 = icmp code bitmap	*/

	O_LAST_OPCODE		/* not an opcode!		*/
};
#ifdef _KERNEL
CTASSERT(O_LAST_OPCODE <= 256);
#endif

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
typedef struct	_ipfw_insn {	/* template for instructions */
	enum ipfw_opcodes	opcode:8;
	uint8_t		len;	/* numer of 32-byte words */
#define	F_NOT		0x80
#define	F_OR		0x40
#define	F_LEN_MASK	0x3f
#define	F_LEN(cmd)	((cmd)->len & F_LEN_MASK)

	uint16_t	arg1;
} ipfw_insn;

#define IPFW_INSN_SIZE_MAX	63	/* unit: uint32_t */

/*
 * The F_INSN_SIZE(type) computes the size, in 4-byte words, of
 * a given type.
 */
#define	F_INSN_SIZE(t)	((sizeof (t))/sizeof(uint32_t))

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
 * This is used for log instructions
 */
typedef struct  _ipfw_insn_log {
        ipfw_insn o;
	uint32_t max_log;	/* how many do we log -- 0 = all */
	uint32_t log_left;	/* how many left to log 	*/
} ipfw_insn_log;

/*
 * This is used by O_IP_{SRC,DST}_IFIP.
 */
typedef struct _ipfw_insn_ifip {
	ipfw_insn o;		/* arg1 & 0x1, addr is valid */
#define IPFW_IFIP_VALID		0x0001
#define IPFW_IFIP_NET		0x0002
#define IPFW_IFIP_SETTINGS	IPFW_IFIP_NET
	char ifname[IFNAMSIZ];
	struct in_addr addr;
	struct in_addr mask;
} ipfw_insn_ifip;

/*
 * This is used by O_REDIRECT.
 */
typedef struct _ipfw_insn_rdr {
	ipfw_insn o;
	struct in_addr addr;
	uint16_t port;		/* network byte order, 0 = same port */
	uint16_t set;		/* reserved for set, 0xffff */
} ipfw_insn_rdr;

#ifdef _KERNEL

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
	uint8_t		usr_flags;	/* IPFW_USR_F_			*/

	/* These fields are present in all rules.			*/
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/

	int		cpuid;		/* owner cpu			*/
	struct ip_fw	*sibling;	/* clone on next cpu		*/

	struct ip_fw	**cross_rules;	/* cross referenced rules	*/
	volatile uint64_t cross_refs;	/* cross references		*/

	uint32_t	refcnt;		/* Ref count for transit pkts	*/
	uint32_t	rule_flags;	/* IPFW_RULE_F_			*/
	uintptr_t	track_ruleid;	/* ruleid for src/dst tracks	*/

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

#define IPFW_RULE_F_INVALID	0x1
/* unused			0x2 */
#define IPFW_RULE_F_GENSTATE	0x4
#define IPFW_RULE_F_GENTRACK	0x8
#define IPFW_RULE_F_CROSSREF	0x10
#define IPFW_RULE_F_DYNIFADDR	0x20

#define RULESIZE(rule)	(sizeof(struct ip_fw) + (rule)->cmd_len * 4 - 4)

/*
 * This structure is used as a flow mask and a flow id for various
 * parts of the code.
 */
struct ipfw_flow_id {
	uint32_t	dst_ip;		/* host byte order */
	uint32_t	src_ip;		/* host byte order */
	uint16_t	dst_port;	/* host byte order */
	uint16_t	src_port;	/* host byte order */
	uint8_t		proto;
	uint8_t		flags;		/* protocol-specific flags */
};

/*
 * Main firewall chains definitions and global var's definitions.
 */

/* ipfw_chk/ip_fw_chk_ptr return values */
#define IP_FW_PASS		0
#define IP_FW_DENY		1
#define IP_FW_DIVERT		2
#define IP_FW_TEE		3
#define IP_FW_DUMMYNET		4
#define IP_FW_REDISPATCH	6

/*
 * arguments for calling ipfw_chk() and dummynet_io(). We put them
 * all into a structure because this way it is easier and more
 * efficient to pass variables around and extend the interface.
 */
struct ip_fw_args {
	struct mbuf	*m;		/* the mbuf chain		*/
	struct ifnet	*oif;		/* output interface		*/
	struct ip_fw	*rule;		/* matching rule		*/
	struct ipfw_xlat *xlat;		/* matching xlate		*/
	struct ether_header *eh;	/* for bridged packets		*/

	struct ipfw_flow_id f_id;	/* grabbed from IP header	*/
	uint8_t		flags;
#define IP_FWARG_F_CONT		0x01
#define IP_FWARG_F_XLATINS	0x02
#define IP_FWARG_F_XLATFWD	0x04

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

/*
 * Function definitions.
 */
int	ip_fw_sockopt(struct sockopt *);

/* Firewall hooks */
struct sockopt;
struct dn_flow_set;

typedef int	ip_fw_chk_t(struct ip_fw_args *);
typedef int	ip_fw_ctl_t(struct sockopt *);
typedef struct mbuf
		*ip_fw_dn_io_t(struct mbuf *, int, int, struct ip_fw_args *);

extern ip_fw_chk_t	*ip_fw_chk_ptr;
extern ip_fw_ctl_t	*ip_fw_ctl_ptr;
extern ip_fw_dn_io_t	*ip_fw_dn_io_ptr;

extern int fw_one_pass;
extern int fw_enable;

extern int ip_fw_loaded;
#define	IPFW_LOADED	(ip_fw_loaded)

#endif /* _KERNEL */

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

	uint8_t		reserved[16];

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

#define IPFW_USR_F_NORULE	0x01

#define IPFW_RULE_SIZE_MAX	255	/* unit: uint32_t */

#define IOC_RULESIZE(rule)	\
	(sizeof(struct ipfw_ioc_rule) + (rule)->cmd_len * 4 - 4)

struct ipfw_ioc_flowid {
	uint16_t	type;	/* ETHERTYPE_ */
	uint16_t	pad;
	union {
		struct {
			uint32_t dst_ip;	/* host byte order */
			uint32_t src_ip;	/* host byte order */
			uint16_t dst_port;	/* host byte order */
			uint16_t src_port;	/* host byte order */
			uint8_t proto;
		} ip;
		uint8_t pad[64];
	} u;
};

struct ipfw_ioc_state {
	uint32_t	expire;		/* expire time			*/
	uint64_t	pcnt;		/* packet match counter		*/
	uint64_t	bcnt;		/* byte match counter		*/

	uint16_t	dyn_type;	/* rule type			*/
	uint16_t	count;		/* refcount			*/

	uint16_t	rulenum;	/* rule number			*/

	uint16_t	xlat_port;	/* xlate port, host byte order	*/
	uint32_t	xlat_addr;	/* xlate addr, host byte order	*/

	struct ipfw_ioc_flowid id;	/* (masked) flow id		*/
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

/*
 * IP_FW_TBL_CREATE, tableid >= 0.
 * IP_FW_TBL_FLUSH, tableid >= 0.
 * IP_FW_TBL_FLUSH, tableid < 0, flush all tables.
 * IP_FW_TBL_DESTROY, tableid >= 0.
 * IP_FW_TBL_ZERO, tableid >= 0.
 * IP_FW_TBL_ZERO, tableid < 0, zero all tables' counters.
 */
struct ipfw_ioc_table {
	int		tableid;
};

struct ipfw_ioc_tblent {
	struct sockaddr_in key;
	struct sockaddr_in netmask;
	u_long		use;
	time_t		last_used;
	long		unused[2];
};

/*
 * IP_FW_TBL_GET, tableid < 0, list of all tables.
 */
struct ipfw_ioc_tbllist {
	int		tableid;	/* MUST be the first field */
	int		tablecnt;
	uint16_t	tables[];
};

/*
 * IP_FW_TBL_GET, tableid >= 0, entries in the table.
 * IP_FW_TBL_ADD, tableid >= 0, entcnt == 1.
 * IP_FW_TBL_DEL, tableid >= 0, entcnt == 1.
 */
struct ipfw_ioc_tblcont {
	int		tableid;	/* MUST be the first field */
	int		entcnt;
	struct ipfw_ioc_tblent ent[1];
};

/*
 * IP_FW_TBL_EXPIRE, tableid < 0, expire all tables.
 * IP_FW_TBL_EXPIRE, tableid >= 0.
 */
struct ipfw_ioc_tblexp {
	int		tableid;
	int		expcnt;
	time_t		expire;
	u_long		unused1[2];
};

#endif /* _IPFW2_H */
