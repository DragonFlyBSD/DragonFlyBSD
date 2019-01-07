/*
 * Copyright (c) 1993 Daniel Boulet
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 * Copyright (c) 2002 Luigi Rizzo, Universita` di Pisa
 * Copyright (c) 2015 - 2018 The DragonFly Project.  All rights reserved.
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

#ifndef _IP_FW3_H_
#define _IP_FW3_H_

/*
 * _IPFW2_H is from ipfw/ip_fw2.h, both cannot be included past this
 * point but we need both the IPFW2_LOADED and IPFW3_LOADED macros
 */
#ifndef _IPFW2_H


#include <net/bpf.h>


#define NEED1(msg)  {if (ac < 1) errx(EX_USAGE, msg);}
#define NEED2(msg)  {if (ac < 2) errx(EX_USAGE, msg);}
#define NEED(c, n, msg) {if (c < n) errx(EX_USAGE, msg);}

#define NEXT_ARG	ac--; if(ac > 0){av++;}
#define NEXT_ARG1 	(*ac)--; if(*ac > 0){(*av)++;}
#define SWAP_ARG				\
do { 						\
	if (ac > 2 && isdigit(*(av[1]))) {	\
		char *p = av[1];		\
		av[1] = av[2];			\
		av[2] = p;			\
	}					\
} while (0)

#define IPFW_RULE_SIZE_MAX	255	/* unit: uint32_t */

/*
 * type of the keyword, it indecates the position of the keyword in the rule
 *      BEFORE ACTION FROM TO FILTER OTHER
 */
#define NONE            0
#define BEFORE          1
#define ACTION          2
#define PROTO           3
#define FROM            4
#define TO              5
#define FILTER          6
#define AFTER           7

#define NOT_IN_USE      0
#define IN_USE          1

#define	SIZE_OF_IPFWINSN	8
#define	LEN_OF_IPFWINSN		2
#define	IPFW_DEFAULT_RULE	65535
#define	IPFW_DEFAULT_SET	0
#define	IPFW_ALL_SETS		0

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

#define ACTION_PTR(rule)	\
	(ipfw_insn *)((uint32_t *)((rule)->cmd) + ((rule)->act_ofs))

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
 * This is used for bpf filtering.
 */
typedef struct _ipfw_insn_bpf {
	ipfw_insn o;
	char bf_str[128];
	u_int bf_len;
	struct bpf_insn bf_insn[1];
} ipfw_insn_bpf;

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
#define LEN_FW3 sizeof(struct ip_fw)

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


/* ip_fw3_chk/ip_fw_chk_ptr return values */
#define IP_FW_PASS	0
#define IP_FW_DENY	1
#define IP_FW_DIVERT	2
#define IP_FW_TEE	3
#define IP_FW_DUMMYNET	4
#define IP_FW_NAT	5
#define IP_FW_ROUTE	6

/* ip_fw3_chk controller values */
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
 * arguments for calling ip_fw3_chk() and dummynet_io(). We put them
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
	 * Depend on the return value of ip_fw3_chk/ip_fw_chk_ptr
	 * 'cookie' field may save following information:
	 *
	 * IP_FW_DUMMYNET
	 *   The pipe or queue number
	 */
	uint32_t	cookie;
};

struct ipfw_ioc_rule {
	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t         insert;         /* insert or append     	*/

	/* Rule set information */
	uint32_t	sets;	/* disabled rule sets		*/

	/* Statistics */
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

#define IOC_RULESIZE(rule)	\
	(sizeof(struct ipfw_ioc_rule) + (rule)->cmd_len * 4 - SIZE_OF_IPFWINSN)


/* IP_FW_X header/opcodes */
typedef struct _ip_fw_x_header {
	uint16_t opcode;	/* Operation opcode */
	uint16_t _pad;   	/* Opcode version */
} ip_fw_x_header;

/* IP_FW3 opcodes */
#define IP_FW_ADD		50   /* add a firewall rule to chain */
#define IP_FW_DEL		51   /* delete a firewall rule from chain */
#define IP_FW_FLUSH		52   /* flush firewall rule chain */
#define IP_FW_ZERO		53   /* clear single/all firewall counter(s) */
#define IP_FW_GET		54   /* get entire firewall rule chain */
#define IP_FW_RESETLOG		55   /* reset logging counters */

#define IP_FW_STATE_ADD		56   /* add one state */
#define IP_FW_STATE_DEL		57   /* delete states of one rulenum */
#define IP_FW_STATE_FLUSH	58   /* flush all states */
#define IP_FW_STATE_GET		59   /* get all states */

#define IP_DUMMYNET_CONFIGURE	60   /* add/configure a dummynet pipe */
#define IP_DUMMYNET_DEL		61   /* delete a dummynet pipe from chain */
#define IP_DUMMYNET_FLUSH	62   /* flush dummynet */
#define IP_DUMMYNET_GET		64   /* get entire dummynet pipes */

#define IP_FW_MODULE		67  /* get modules names */

#define IP_FW_NAT_ADD		68   /* add/config a nat rule */
#define IP_FW_NAT_DEL		69   /* delete a nat rule */
#define IP_FW_NAT_FLUSH		70   /* get configuration of a nat rule */
#define IP_FW_NAT_GET		71   /* get config of a nat rule */
#define IP_FW_NAT_GET_RECORD	72   /* get nat record of a nat rule */

#define IP_FW_TABLE_CREATE	73	/* table_create 	*/
#define IP_FW_TABLE_DELETE	74	/* table_delete 	*/
#define IP_FW_TABLE_APPEND	75	/* table_append 	*/
#define IP_FW_TABLE_REMOVE	76	/* table_remove 	*/
#define IP_FW_TABLE_LIST	77	/* table_list 		*/
#define IP_FW_TABLE_FLUSH	78	/* table_flush 		*/
#define IP_FW_TABLE_SHOW	79	/* table_show 		*/
#define IP_FW_TABLE_TEST	80	/* table_test 		*/
#define IP_FW_TABLE_RENAME	81	/* rename a table 	*/

/* opcodes for ipfw3sync */
#define IP_FW_SYNC_SHOW_CONF	82	/* show sync config */
#define IP_FW_SYNC_SHOW_STATUS	83	/* show edge & centre running status */

#define IP_FW_SYNC_EDGE_CONF	84	/* config sync edge */
#define IP_FW_SYNC_EDGE_START	85	/* start the edge */
#define IP_FW_SYNC_EDGE_STOP	86	/* stop the edge */
#define IP_FW_SYNC_EDGE_TEST	87	/* test sync edge */
#define IP_FW_SYNC_EDGE_CLEAR	88	/* stop and clear the edge */

#define IP_FW_SYNC_CENTRE_CONF	89	/* config sync centre */
#define IP_FW_SYNC_CENTRE_START	90	/* start the centre */
#define IP_FW_SYNC_CENTRE_STOP	91	/* stop the centre */
#define IP_FW_SYNC_CENTRE_TEST	92	/* test sync centre */
#define IP_FW_SYNC_CENTRE_CLEAR	93	/* stop and clear the centre */

#define IP_FW_SET_GET		95	/* get the set config */
#define IP_FW_SET_MOVE_RULE	96	/* move a rule to set */
#define IP_FW_SET_MOVE_SET	97	/* move all rules from set a to b */
#define IP_FW_SET_SWAP		98	/* swap 2 sets	*/
#define IP_FW_SET_TOGGLE	99	/* enable/disable a set	*/
#define IP_FW_SET_FLUSH		100	/* flush the rule of the set */

#endif /* _IPFW2_H */
#ifdef _KERNEL

#include <net/netisr2.h>

int     ip_fw3_sockopt(struct sockopt *);

extern int ip_fw3_loaded;

#define	IPFW3_LOADED	(ip_fw3_loaded)

#ifdef IPFIREWALL3_DEBUG
#define IPFW3_DEBUG1(str)		\
do { 					\
	kprintf(str); 			\
} while (0)
#define IPFW3_DEBUG(fmt, ...)		\
do { 					\
	kprintf(fmt, __VA_ARGS__); 	\
} while (0)
#else
#define IPFW3_DEBUG1(str)		((void)0)
#define IPFW3_DEBUG(fmt, ...)		((void)0)
#endif

typedef int	ip_fw_ctl_t(struct sockopt *);
typedef int	ip_fw_chk_t(struct ip_fw_args *);
typedef struct mbuf *ip_fw_dn_io_t(struct mbuf *, int, int, struct ip_fw_args *);
typedef void *ip_fw_log_t(struct mbuf *m, struct ether_header *eh, uint16_t id);

#ifndef _IPFW2_H

int	ip_fw_sockopt(struct sockopt *);

struct sockopt;
struct dn_flow_set;


extern ip_fw_chk_t	*ip_fw_chk_ptr;
extern ip_fw_ctl_t	*ip_fw_ctl_x_ptr;
extern ip_fw_dn_io_t	*ip_fw_dn_io_ptr;


#define	IPFW_TABLES_MAX		32
#define	IPFW_USR_F_NORULE	0x01
#define	IPFW_CFGCPUID		0
#define	IPFW_CFGPORT		netisr_cpuport(IPFW_CFGCPUID)
#define	IPFW_ASSERT_CFGPORT(msgport)				\
	KASSERT((msgport) == IPFW_CFGPORT, ("not IPFW CFGPORT"))


/* root of place holding all information, per-cpu */
struct ipfw3_context {
	struct ip_fw			*rules;    /* rules*/
	struct ip_fw			*default_rule;  /* default rule*/
	struct ipfw3_state_context	*state_ctx;
	struct ipfw3_table_context	*table_ctx;

	/* each bit represents a disabled set, 0 is the default set */
	uint32_t			sets;
};
#define LEN_FW3_CTX sizeof(struct ipfw3_context)

struct ipfw3_module{
	int 	type;
	int 	id;
	char 	name[20];
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

#define MATCH_REVERSE	0
#define MATCH_FORWARD	1
#define MATCH_NONE	2
#define MATCH_UNKNOWN	3

#define L3HDR(T, ip) ((T *)((uint32_t *)(ip) + (ip)->ip_hl))


typedef void (*filter_func)(int *cmd_ctl,int *cmd_val,struct ip_fw_args **args,
			struct ip_fw **f,ipfw_insn *cmd, uint16_t ip_len);

void	check_accept(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_deny(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

void    ip_fw3_register_module(int module_id,char *module_name);
int     ip_fw3_unregister_module(int module_id);
void    ip_fw3_register_filter_funcs(int module, int opcode, filter_func func);
void 	ip_fw3_unregister_filter_funcs(int module,filter_func func);

void	init_module(void);
int	ip_fw3_free_rule(struct ip_fw *rule);
int	ip_fw3_chk(struct ip_fw_args *args);
struct mbuf *ip_fw3_dummynet_io(struct mbuf *m, int pipe_nr, int dir, struct ip_fw_args *fwa);
void	add_rule_dispatch(netmsg_t nmsg);
void	ip_fw3_add_rule(struct ipfw_ioc_rule *ioc_rule);
struct ip_fw *ip_fw3_delete_rule(struct ipfw3_context *ctx,
		 struct ip_fw *prev, struct ip_fw *rule);
void	flush_rule_dispatch(netmsg_t nmsg);
void	ip_fw3_ctl_flush_rule(int);
void	delete_rule_dispatch(netmsg_t nmsg);
int	ip_fw3_ctl_delete_rule(struct sockopt *sopt);
void	ip_fw3_clear_counters(struct ip_fw *rule);
void	ip_fw3_zero_entry_dispatch(netmsg_t nmsg);
int	ip_fw3_ctl_zero_entry(int rulenum, int log_only);
int	ip_fw3_ctl_add_rule(struct sockopt *sopt);
int	ip_fw3_ctl_get_modules(struct sockopt *sopt);
int	ip_fw3_ctl_get_rules(struct sockopt *sopt);
int	ip_fw3_ctl_x(struct sockopt *sopt);
int	ip_fw3_ctl(struct sockopt *sopt);
int	ip_fw3_ctl_sockopt(struct sockopt *sopt);
int	ip_fw3_check_in(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir);
int	ip_fw3_check_out(void *arg, struct mbuf **m0, struct ifnet *ifp, int dir);
void	ip_fw3_hook(void);
void	ip_fw3_dehook(void);
void	ip_fw3_sysctl_enable_dispatch(netmsg_t nmsg);
void	ctx_init_dispatch(netmsg_t nmsg);
void	init_dispatch(netmsg_t nmsg);
int	ip_fw3_init(void);
void	fini_dispatch(netmsg_t nmsg);
int	ip_fw3_fini(void);

#endif /* _KERNEL */
#endif /* _IPFW2_H */
#endif /* _IP_FW3_H_ */
