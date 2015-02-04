/*
 * Copyright (c) 2002 Luigi Rizzo
 * Copyright (c) 1996 Alex Nash, Paul Traina, Poul-Henning Kamp
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Idea and grammar partially left from:
 * Copyright (c) 1993 Daniel Boulet
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 *
 * NEW command line interface for IP firewall facility
 *
 * $FreeBSD: src/sbin/ipfw/ipfw2.c,v 1.4.2.13 2003/05/27 22:21:11 gshapiro Exp $
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <sysexits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <net/ipfw2/ip_fw.h>
#include <net/dummynet2/ip_dummynet.h>

#include "ipfw.h"
#include "../../sys/net/libalias/alias.h"
#include "../../sys/net/ipfw_basic/ip_fw_basic.h"
#include "../../sys/net/ipfw_nat/ip_fw_nat.h"


#define KEYWORD_SIZE	256
#define MAPPING_SIZE	256

#define MAX_KEYWORD_LEN	20
#define MAX_ARGS	32
#define WHITESP		" \t\f\v\n\r"
#define IPFW_LIB_PATH	"/usr/lib/lib_ipfw_%s.so"

#define	IP_MASK_ALL	0xffffffff
#define NAT_BUF_LEN	1024
/*
 * we use IPPROTO_ETHERTYPE as a fake protocol id to call the print routines
 * This is only used in this code.
 */
#define IPPROTO_ETHERTYPE	0x1000

/*
 * This macro returns the size of a struct sockaddr when passed
 * through a routing socket. Basically we round up sa_len to
 * a multiple of sizeof(long), with a minimum of sizeof(long).
 * The check for a NULL pointer is just a convenience, probably never used.
 * The case sa_len == 0 should only apply to empty structures.
 */
#define SA_SIZE(sa)						\
	( (!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?	\
	sizeof(long)		:				\
	1 + ( (((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )

/*
 * show_rules() prints the body of an ipfw rule.
 * Because the standard rule has at least proto src_ip dst_ip, we use
 * a helper function to produce these entries if not provided explicitly.
 * The first argument is the list of fields we have, the second is
 * the list of fields we want to be printed.
 *
 * Special cases if we have provided a MAC header:
 * + if the rule does not contain IP addresses/ports, do not print them;
 * + if the rule does not contain an IP proto, print "all" instead of "ip";
 *
 */
#define	HAVE_PROTO	0x0001
#define	HAVE_SRCIP	0x0002
#define	HAVE_DSTIP	0x0004
#define	HAVE_MAC	0x0008
#define	HAVE_MACTYPE	0x0010
#define	HAVE_OPTIONS	0x8000

#define	HAVE_IP		(HAVE_PROTO | HAVE_SRCIP | HAVE_DSTIP)

/*
 * Definition of a port range, and macros to deal with values.
 * FORMAT: HI 16-bits == first port in range, 0 == all ports.
 *		 LO 16-bits == number of ports in range
 * NOTES: - Port values are not stored in network byte order.
 */

#define port_range u_long

#define GETLOPORT(x)	((x) >> 0x10)
#define GETNUMPORTS(x)	((x) & 0x0000ffff)
#define GETHIPORT(x)	(GETLOPORT((x)) + GETNUMPORTS((x)))

/* Set y to be the low-port value in port_range variable x. */
#define SETLOPORT(x, y) ((x) = ((x) & 0x0000ffff) | ((y) << 0x10))

/* Set y to be the number of ports in port_range variable x. */
#define SETNUMPORTS(x, y) ((x) = ((x) & 0xffff0000) | (y))

#define INC_ARGCV() do {			\
	(*_av)++;				\
	(*_ac)--;				\
	av = *_av;				\
	ac = *_ac;				\
} while (0)


int		ipfw_socket = -1;	/* main RAW socket */
int		do_resolv, 		/* Would try to resolve all */
		do_acct, 		/* Show packet/byte count */
		do_time, 		/* Show time stamps */
		do_quiet = 1,		/* Be quiet , default is quiet*/
		do_force, 		/* Don't ask for confirmation */
		do_pipe, 		/* this cmd refers to a pipe */
		do_nat, 		/* Nat configuration. */
		do_sort, 		/* field to sort results (0 = no) */
		do_dynamic, 		/* display dynamic rules */
		do_expired, 		/* display expired dynamic rules */
		do_compact, 		/* show rules in compact mode */
		show_sets, 		/* display rule sets */
		verbose;

enum tokens {
	TOK_NULL=0,

	TOK_IP,
	TOK_IF,
	TOK_ALOG,
	TOK_DENY_INC,
	TOK_SAME_PORTS,
	TOK_UNREG_ONLY,
	TOK_RESET_ADDR,
	TOK_ALIAS_REV,
	TOK_PROXY_ONLY,
	TOK_REDIR_ADDR,
	TOK_REDIR_PORT,
	TOK_REDIR_PROTO,

	TOK_PIPE,
	TOK_QUEUE,
	TOK_PLR,
	TOK_NOERROR,
	TOK_BUCKETS,
	TOK_DSTIP,
	TOK_SRCIP,
	TOK_DSTPORT,
	TOK_SRCPORT,
	TOK_ALL,
	TOK_MASK,
	TOK_BW,
	TOK_DELAY,
	TOK_RED,
	TOK_GRED,
	TOK_DROPTAIL,
	TOK_PROTO,
	TOK_WEIGHT,
};

struct char_int_map dummynet_params[] = {
	{ "plr", 		TOK_PLR },
	{ "noerror", 		TOK_NOERROR },
	{ "buckets", 		TOK_BUCKETS },
	{ "dst-ip", 		TOK_DSTIP },
	{ "src-ip", 		TOK_SRCIP },
	{ "dst-port", 		TOK_DSTPORT },
	{ "src-port", 		TOK_SRCPORT },
	{ "proto", 		TOK_PROTO },
	{ "weight", 		TOK_WEIGHT },
	{ "all", 		TOK_ALL },
	{ "mask", 		TOK_MASK },
	{ "droptail", 		TOK_DROPTAIL },
	{ "red", 		TOK_RED },
	{ "gred", 		TOK_GRED },
	{ "bw",			TOK_BW },
	{ "bandwidth", 		TOK_BW },
	{ "delay", 		TOK_DELAY },
	{ "pipe", 		TOK_PIPE },
	{ "queue", 		TOK_QUEUE },
	{ "dummynet-params", 	TOK_NULL },
	{ NULL, 0 }
};

struct char_int_map nat_params[] = {
	{ "ip", 		TOK_IP },
	{ "if", 		TOK_IF },
	{ "log", 		TOK_ALOG },
	{ "deny_in", 		TOK_DENY_INC },
	{ "same_ports", 	TOK_SAME_PORTS },
	{ "unreg_only", 	TOK_UNREG_ONLY },
	{ "reset", 		TOK_RESET_ADDR },
	{ "reverse", 		TOK_ALIAS_REV },
	{ "proxy_only", 	TOK_PROXY_ONLY },
	{ "redirect_addr", 	TOK_REDIR_ADDR },
	{ "redirect_port", 	TOK_REDIR_PORT },
	{ "redirect_proto", 	TOK_REDIR_PROTO },
	{ NULL, 0 },
};

struct ipfw_keyword {
	int type;
	char word[MAX_KEYWORD_LEN];
	int module;
	int opcode;
};

struct ipfw_mapping {
	int type;
	int module;
	int opcode;
	parser_func parser;
	shower_func shower;
};

static uint32_t new_rule_buf[IPFW_RULE_SIZE_MAX];	/* buf use in do_get/set_x */

struct ipfw_keyword keywords[KEYWORD_SIZE];
struct ipfw_mapping mappings[MAPPING_SIZE];

static int
match_token(struct char_int_map *table, char *string)
{
	while (table->key) {
		if (strcmp(table->key, string) == 0) {
			return table->val;
		}
		table++;
	}
	return 0;
}

static void
get_modules(char *modules_str, int len)
{
	if (do_get_x(IP_FW_MODULE, modules_str, &len) < 0)
		errx(EX_USAGE, "ipfw2 not loaded.");
}

static void
list_modules(int ac, char *av[])
{
	void *module_str = NULL;
	int len = 1024;
	if ((module_str = realloc(module_str, len)) == NULL)
		err(EX_OSERR, "realloc");

	get_modules(module_str, len);
	printf("%s", (char *)module_str);
}
void
parse_accept(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_BASIC_ACCEPT;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = (*cmd)->len|LEN_OF_IPFWINSN;
	NEXT_ARG1;
}

void
parse_deny(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_BASIC_DENY;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = (*cmd)->len|LEN_OF_IPFWINSN;
	NEXT_ARG1;
}

void
show_accept(ipfw_insn *cmd)
{
	printf(" allow");
}

void
show_deny(ipfw_insn *cmd)
{
	printf(" deny");
}

static void
load_modules()
{
	const char *error;
	init_module mod_init_func;
	void *module_lib;
	char module_lib_file[50];
	void *module_str = NULL;
	int len = 1024;

	if ((module_str = realloc(module_str, len)) == NULL)
		err(EX_OSERR, "realloc");

	get_modules(module_str, len);

	const char s[2] = ",";
	char *token;
	token = strtok(module_str, s);
	while (token != NULL) {
		sprintf(module_lib_file, IPFW_LIB_PATH, token);
		token = strtok(NULL, s);
		module_lib = dlopen(module_lib_file, RTLD_LAZY);
		if (!module_lib) {
			fprintf(stderr, "Couldn't open %s: %s\n",
				module_lib_file, dlerror());
			exit(EX_SOFTWARE);
		}
		mod_init_func = dlsym(module_lib, "load_module");
		if ((error = dlerror()))
		{
			fprintf(stderr, "Couldn't find init function: %s\n", error);
			exit(EX_SOFTWARE);
		}
		(*mod_init_func)((register_func)register_ipfw_func,
				(register_keyword)register_ipfw_keyword);
	}
}

void
prepare_default_funcs()
{
	/* register allow*/
	register_ipfw_keyword(MODULE_BASIC_ID, O_BASIC_ACCEPT,
			"allow", IPFW_KEYWORD_TYPE_ACTION);
	register_ipfw_keyword(MODULE_BASIC_ID, O_BASIC_ACCEPT,
			"accept", IPFW_KEYWORD_TYPE_ACTION);
	register_ipfw_func(MODULE_BASIC_ID, O_BASIC_ACCEPT,
			(parser_func)parse_accept, (shower_func)show_accept);
	/* register deny*/
	register_ipfw_keyword(MODULE_BASIC_ID, O_BASIC_DENY, "deny",
			IPFW_KEYWORD_TYPE_ACTION);
	register_ipfw_keyword(MODULE_BASIC_ID, O_BASIC_DENY, "reject",
			IPFW_KEYWORD_TYPE_ACTION);
	register_ipfw_func(MODULE_BASIC_ID, O_BASIC_DENY,
			(parser_func)parse_deny, (shower_func)show_deny);
}

void
register_ipfw_keyword(int module, int opcode, char *word, int type)
{
	struct ipfw_keyword *tmp;

	tmp=keywords;
	for(;;) {
		if (tmp->type == IPFW_KEYWORD_TYPE_NONE) {
			strcpy(tmp->word, word);
			tmp->module = module;
			tmp->opcode = opcode;
			tmp->type = type;
			break;
		} else {
			if (strcmp(tmp->word, word) == 0)
				errx(EX_USAGE, "keyword `%s' exists", word);
			else
				tmp++;
		}
	}
}

void
register_ipfw_func(int module, int opcode, parser_func parser, shower_func shower)
{
	struct ipfw_mapping *tmp;

	tmp = mappings;
	while (1) {
		if (tmp->type == IPFW_MAPPING_TYPE_NONE) {
			tmp->module = module;
			tmp->opcode = opcode;
			tmp->parser = parser;
			tmp->shower = shower;
			tmp->type = IPFW_MAPPING_TYPE_IN_USE;
			break;
		} else {
			if (tmp->opcode == opcode && tmp->module == module) {
				errx(EX_USAGE, "func `%d' of module `%d' exists",
					opcode, module);
				break;
			} else {
				tmp++;
			}
		}
	}
}

static void
show_rules(struct ipfw_ioc_rule *rule, int pcwidth, int bcwidth)
{
	static int twidth = 0;
	ipfw_insn *cmd;
	int l, or_block = 0; 	/* we are in an or block */

	u_int32_t set_disable = rule->set_disable;

	if (set_disable & (1 << rule->set)) { /* disabled */
		if (!show_sets)
			return;
		else
			printf("# DISABLED ");
	}
	printf("%05u ", rule->rulenum);

	if (do_acct)
		printf("%*ju %*ju ", pcwidth, (uintmax_t)rule->pcnt, bcwidth,
			(uintmax_t)rule->bcnt);

	if (do_time == 1) {
		char timestr[30];

		if (twidth == 0) {
			strcpy(timestr, ctime((time_t *)&twidth));
			*strchr(timestr, '\n') = '\0';
			twidth = strlen(timestr);
		}
		if (rule->timestamp) {
			time_t t = _long_to_time(rule->timestamp);

			strcpy(timestr, ctime(&t));
			*strchr(timestr, '\n') = '\0';
			printf("%s ", timestr);
		} else {
			printf("%*s ", twidth, " ");
		}
	} else if (do_time == 2) {
		printf( "%10u ", rule->timestamp);
	}

	if (show_sets)
		printf("set %d ", rule->set);


	struct ipfw_keyword *k;
	struct ipfw_mapping *m;
	shower_func fn, comment_fn = NULL;
	ipfw_insn *comment_cmd;
	int i, j, changed;

	/*
	 * show others and actions
	 */
	for (l = rule->cmd_len - rule->act_ofs, cmd = ACTION_PTR(rule);
		l > 0; l -= F_LEN(cmd),
		cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		k = keywords;
		m = mappings;
		for (i = 1; i< KEYWORD_SIZE; i++, k++) {
			if ( k->module == cmd->module && k->opcode == cmd->opcode ) {
				for (j = 1; j< MAPPING_SIZE; j++, m++) {
					if (m->type == IPFW_MAPPING_TYPE_IN_USE &&
						m->module == cmd->module &&
						m->opcode == cmd->opcode) {
						if (cmd->module == MODULE_BASIC_ID &&
							cmd->opcode == O_BASIC_COMMENT) {
							comment_fn = m->shower;
							comment_cmd = cmd;
						} else {
							fn = m->shower;
							(*fn)(cmd);
						}
						if (cmd->module == MODULE_BASIC_ID &&
							cmd->opcode ==
								O_BASIC_CHECK_STATE) {
							goto done;
						}
						break;
					}
				}
				break;
			}
		}
	}

	/*
	 * show proto
	 */
	changed=0;
	for (l = rule->act_ofs, cmd = rule->cmd;
		l > 0;l -= F_LEN(cmd) ,
		cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		k = keywords;
		m = mappings;
		for (i = 1; i< KEYWORD_SIZE; i++, k++) {
			if (k->type == IPFW_KEYWORD_TYPE_FILTER &&
				strcmp(k->word, "proto") == 0) {
				if (k->module == cmd->module &&
					k->opcode == cmd->opcode) {
					for (j = 1; j< MAPPING_SIZE; j++, m++) {
						if (m->type == IPFW_MAPPING_TYPE_IN_USE &&
							k->module == m->module &&
							k->opcode == m->opcode) {
							if (cmd->len & F_NOT) {
								printf(" not");
							}
							fn = m->shower;
							(*fn)(cmd);
							changed = 1;
							goto show_from;
						}
					}
				}
			}
		}
	}
	if (!changed && !do_quiet)
		printf(" ip");

	/*
	 * show from
	 */
show_from:
	changed = 0;
	for (l = rule->act_ofs, cmd = rule->cmd;
		l > 0; l -= F_LEN(cmd),
		cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		k = keywords;
		m = mappings;
		for (i = 1; i< KEYWORD_SIZE; i++, k++) {
			if (k->type == IPFW_KEYWORD_TYPE_FILTER &&
				strcmp(k->word, "from") == 0) {
				if (k->module == cmd->module &&
					k->opcode == cmd->opcode) {
					for (j = 1; j< MAPPING_SIZE; j++, m++) {
						if (m->type == IPFW_MAPPING_TYPE_IN_USE &&
							k->module == m->module &&
							k->opcode == m->opcode) {
							if (cmd->len & F_NOT)
								printf(" not");

							fn = m->shower;
							(*fn)(cmd);
							changed = 1;
							goto show_to;
						}
					}
				}
			}
		}
	}
	if (!changed && !do_quiet)
		printf(" from any");

	/*
	 * show to
	 */
show_to:
	changed = 0;
	for (l = rule->act_ofs, cmd = rule->cmd;
		l > 0; l -= F_LEN(cmd),
		cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		k = keywords;
		m = mappings;
		for (i = 1; i< KEYWORD_SIZE; i++, k++) {
			if (k->type == IPFW_KEYWORD_TYPE_FILTER &&
				strcmp(k->word, "to") == 0) {
				if (k->module == cmd->module &&
					k->opcode == cmd->opcode) {
					for (j = 1; j < MAPPING_SIZE; j++, m++) {
						if (m->type == IPFW_MAPPING_TYPE_IN_USE &&
							k->module == m->module &&
							k->opcode == m->opcode ) {
							if (cmd->len & F_NOT)
								printf(" not");

							fn = m->shower;
							(*fn)(cmd);
							changed = 1;
							goto show_filter;
						}
					}
				}
			}
		}
	}
	if (!changed && !do_quiet)
		printf(" to any");

	/*
	 * show other filters
	 */
show_filter:
	for (l = rule->act_ofs, cmd = rule->cmd, m = mappings;
		l > 0; l -= F_LEN(cmd),
		cmd=(ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		k = keywords;
		m = mappings;
		for (i = 1; i< KEYWORD_SIZE; i++, k++) {
			if (k->module == cmd->module && k->opcode == cmd->opcode) {
				if (strcmp(k->word, "proto") != 0 &&
					strcmp(k->word, "from") !=0 &&
					strcmp(k->word, "to") !=0) {
					for (j = 1; j < MAPPING_SIZE; j++, m++) {
						if (m->module == cmd->module &&
							m->opcode == cmd->opcode) {
							if (cmd->len & F_NOT)
								printf(" not");

							fn = m->shower;
							(*fn)(cmd);
							break;
						}
					}
				}
			}
		}
	}

	if (cmd->len & F_OR) {
		printf(" or");
		or_block = 1;
	} else if (or_block) {
		printf(" }");
		or_block = 0;
	}

	/* show the comment in the end */
	if (comment_fn != NULL) {
		(*comment_fn)(comment_cmd);
	}
done:
	printf("\n");
}

static void
show_states(struct ipfw_ioc_state *d, int pcwidth, int bcwidth)
{
	struct protoent *pe;
	struct in_addr a;

	printf("%05u ", d->rulenum);
	if (do_acct) {
		printf("%*ju %*ju ", pcwidth, (uintmax_t)d->pcnt,
				bcwidth, (uintmax_t)d->bcnt);
	}

	if (do_time == 1) {
		/* state->timestamp */
		char timestr[30];
		time_t t = _long_to_time(d->timestamp);
		strcpy(timestr, ctime(&t));
		*strchr(timestr, '\n') = '\0';
		printf(" (%s", timestr);

		/* state->lifetime */
		printf(" %ds", d->lifetime);

		/* state->expiry */
		if (d->expiry !=0) {
			t = _long_to_time(d->expiry);
			strcpy(timestr, ctime(&t));
			*strchr(timestr, '\n') = '\0';
			printf(" %s)", timestr);
		} else {
			printf(" 0)");
		}

	} else if (do_time == 2) {
		printf("(%u %ds %u) ", d->timestamp, d->lifetime, d->expiry);
	}

	if ((pe = getprotobynumber(d->flow_id.proto)) != NULL)
		printf(" %s", pe->p_name);
	else
		printf(" proto %u", d->flow_id.proto);

	a.s_addr = htonl(d->flow_id.src_ip);
	printf(" %s %d", inet_ntoa(a), d->flow_id.src_port);

	a.s_addr = htonl(d->flow_id.dst_ip);
	printf(" <-> %s %d", inet_ntoa(a), d->flow_id.dst_port);
	printf(" CPU %d", d->cpuid);
	printf("\n");
}

int
sort_q(const void *pa, const void *pb)
{
	int rev = (do_sort < 0);
	int field = rev ? -do_sort : do_sort;
	long long res = 0;
	const struct dn_ioc_flowqueue *a = pa;
	const struct dn_ioc_flowqueue *b = pb;

	switch(field) {
	case 1: /* pkts */
		res = a->len - b->len;
		break;
	case 2: /* bytes */
		res = a->len_bytes - b->len_bytes;
		break;

	case 3: /* tot pkts */
		res = a->tot_pkts - b->tot_pkts;
		break;

	case 4: /* tot bytes */
		res = a->tot_bytes - b->tot_bytes;
		break;
	}
	if (res < 0)
		res = -1;
	if (res > 0)
		res = 1;
	return (int)(rev ? res : -res);
}

static void
show_queues(struct dn_ioc_flowset *fs, struct dn_ioc_flowqueue *q)
{
	int l;

	printf("mask: 0x%02x 0x%08x/0x%04x -> 0x%08x/0x%04x\n",
		fs->flow_mask.u.ip.proto,
		fs->flow_mask.u.ip.src_ip, fs->flow_mask.u.ip.src_port,
		fs->flow_mask.u.ip.dst_ip, fs->flow_mask.u.ip.dst_port);
	if (fs->rq_elements == 0)
		return;

	printf("BKT Prot ___Source IP/port____ "
		"____Dest. IP/port____ Tot_pkt/bytes Pkt/Byte Drp\n");
	if (do_sort != 0)
		heapsort(q, fs->rq_elements, sizeof(*q), sort_q);
	for (l = 0; l < fs->rq_elements; l++) {
		struct in_addr ina;
		struct protoent *pe;

		ina.s_addr = htonl(q[l].id.u.ip.src_ip);
		printf("%3d ", q[l].hash_slot);
		pe = getprotobynumber(q[l].id.u.ip.proto);
		if (pe)
			printf("%-4s ", pe->p_name);
		else
			printf("%4u ", q[l].id.u.ip.proto);
		printf("%15s/%-5d ",
			inet_ntoa(ina), q[l].id.u.ip.src_port);
		ina.s_addr = htonl(q[l].id.u.ip.dst_ip);
		printf("%15s/%-5d ",
			inet_ntoa(ina), q[l].id.u.ip.dst_port);
		printf("%4ju %8ju %2u %4u %3u\n",
			(uintmax_t)q[l].tot_pkts, (uintmax_t)q[l].tot_bytes,
			q[l].len, q[l].len_bytes, q[l].drops);
		if (verbose)
			printf(" S %20ju F %20ju\n",
				(uintmax_t)q[l].S, (uintmax_t)q[l].F);
	}
}

static void
show_flowset_parms(struct dn_ioc_flowset *fs, char *prefix)
{
	char qs[30];
	char plr[30];
	char red[90]; 	/* Display RED parameters */
	int l;

	l = fs->qsize;
	if (fs->flags_fs & DN_QSIZE_IS_BYTES) {
		if (l >= 8192)
			sprintf(qs, "%d KB", l / 1024);
		else
			sprintf(qs, "%d B", l);
	} else
		sprintf(qs, "%3d sl.", l);
	if (fs->plr)
		sprintf(plr, "plr %f", 1.0 * fs->plr / (double)(0x7fffffff));
	else
		plr[0] = '\0';
	if (fs->flags_fs & DN_IS_RED)	/* RED parameters */
		sprintf(red,
			"\n\t %cRED w_q %f min_th %d max_th %d max_p %f",
			(fs->flags_fs & DN_IS_GENTLE_RED) ? 'G' : ' ',
			1.0 * fs->w_q / (double)(1 << SCALE_RED),
			SCALE_VAL(fs->min_th),
			SCALE_VAL(fs->max_th),
			1.0 * fs->max_p / (double)(1 << SCALE_RED));
	else
		sprintf(red, "droptail");

	printf("%s %s%s %d queues (%d buckets) %s\n",
		prefix, qs, plr, fs->rq_elements, fs->rq_size, red);
}

static void
show_pipes(void *data, int nbytes, int ac, char *av[])
{
	u_long rulenum;
	void *next = data;
	struct dn_ioc_pipe *p = (struct dn_ioc_pipe *)data;
	struct dn_ioc_flowset *fs;
	struct dn_ioc_flowqueue *q;
	int l;

	if (ac > 0)
		rulenum = strtoul(*av++, NULL, 10);
	else
		rulenum = 0;
	for (; nbytes >= sizeof(*p); p = (struct dn_ioc_pipe *)next) {
		double b = p->bandwidth;
		char buf[30];
		char prefix[80];

		if (p->fs.fs_type != DN_IS_PIPE)
			break; 	/* done with pipes, now queues */

		/*
		 * compute length, as pipe have variable size
		 */
		l = sizeof(*p) + p->fs.rq_elements * sizeof(*q);
		next = (void *)p + l;
		nbytes -= l;

		if (rulenum != 0 && rulenum != p->pipe_nr)
			continue;

		/*
		 * Print rate
		 */
		if (b == 0)
			sprintf(buf, "unlimited");
		else if (b >= 1000000)
			sprintf(buf, "%7.3f Mbit/s", b/1000000);
		else if (b >= 1000)
			sprintf(buf, "%7.3f Kbit/s", b/1000);
		else
			sprintf(buf, "%7.3f bit/s ", b);

		sprintf(prefix, "%05d: %s %4d ms ",
			p->pipe_nr, buf, p->delay);
		show_flowset_parms(&p->fs, prefix);
		if (verbose)
			printf(" V %20ju\n", (uintmax_t)p->V >> MY_M);

		q = (struct dn_ioc_flowqueue *)(p+1);
		show_queues(&p->fs, q);
	}

	for (fs = next; nbytes >= sizeof(*fs); fs = next) {
		char prefix[80];

		if (fs->fs_type != DN_IS_QUEUE)
			break;
		l = sizeof(*fs) + fs->rq_elements * sizeof(*q);
		next = (void *)fs + l;
		nbytes -= l;
		q = (struct dn_ioc_flowqueue *)(fs+1);
		sprintf(prefix, "q%05d: weight %d pipe %d ",
			fs->fs_nr, fs->weight, fs->parent_nr);
		show_flowset_parms(fs, prefix);
		show_queues(fs, q);
	}
}

/*
 * This one handles all set-related commands
 * 	ipfw set { show | enable | disable }
 * 	ipfw set swap X Y
 * 	ipfw set move X to Y
 * 	ipfw set move rule X to Y
 */
static void
sets_handler(int ac, char *av[])
{
	u_int32_t set_disable, masks[2];
	u_int16_t rulenum;
	u_int8_t cmd, new_set;
	int i, nbytes;

	NEXT_ARG;
	if (!ac)
		errx(EX_USAGE, "set needs command");
	if (!strncmp(*av, "show", strlen(*av)) ) {
		void *data = NULL;
		char *msg;
		int nalloc=1000;
		nbytes = nalloc;

		while (nbytes >= nalloc) {
			nalloc = nalloc * 2+321;
			nbytes = nalloc;
			if (data == NULL) {
				if ((data = malloc(nbytes)) == NULL) {
					err(EX_OSERR, "malloc");
				}
			} else if ((data = realloc(data, nbytes)) == NULL) {
				err(EX_OSERR, "realloc");
			}
			if (do_get_x(IP_FW_GET, data, &nbytes) < 0) {
				err(EX_OSERR, "getsockopt(IP_FW_GET)");
			}
		}
		set_disable = ((struct ipfw_ioc_rule *)data)->set_disable;
		for (i = 0, msg = "disable" ; i < 31; i++)
			if ( (set_disable & (1<<i))) {
				printf("%s %d", msg, i);
				msg = "";
			}
		msg = (set_disable) ? " enable" : "enable";
		for (i = 0; i < 31; i++)
			if ( !(set_disable & (1<<i))) {
				printf("%s %d", msg, i);
				msg = "";
			}
		printf("\n");
	} else if (!strncmp(*av, "swap", strlen(*av))) {
		NEXT_ARG;
		if (ac != 2)
			errx(EX_USAGE, "set swap needs 2 set numbers\n");
		rulenum = atoi(av[0]);
		new_set = atoi(av[1]);
		if (!isdigit(*(av[0])) || rulenum > 30)
			errx(EX_DATAERR, "invalid set number %s\n", av[0]);
		if (!isdigit(*(av[1])) || new_set > 30)
			errx(EX_DATAERR, "invalid set number %s\n", av[1]);
		masks[0] = (4 << 24) | (new_set << 16) | (rulenum);
		i = do_set_x(IP_FW_DEL, masks, sizeof(u_int32_t));
	} else if (!strncmp(*av, "move", strlen(*av))) {
		NEXT_ARG;
		if (ac && !strncmp(*av, "rule", strlen(*av))) {
			cmd = 2;
			NEXT_ARG;
		} else
			cmd = 3;
		if (ac != 3 || strncmp(av[1], "to", strlen(*av)))
			errx(EX_USAGE, "syntax: set move [rule] X to Y\n");
		rulenum = atoi(av[0]);
		new_set = atoi(av[2]);
		if (!isdigit(*(av[0])) || (cmd == 3 && rulenum > 30) ||
				(cmd == 2 && rulenum == 65535) )
			errx(EX_DATAERR, "invalid source number %s\n", av[0]);
		if (!isdigit(*(av[2])) || new_set > 30)
			errx(EX_DATAERR, "invalid dest. set %s\n", av[1]);
		masks[0] = (cmd << 24) | (new_set << 16) | (rulenum);
		i = do_set_x(IP_FW_DEL, masks, sizeof(u_int32_t));
	} else if (!strncmp(*av, "disable", strlen(*av)) ||
			!strncmp(*av, "enable", strlen(*av)) ) {
		int which = !strncmp(*av, "enable", strlen(*av)) ? 1 : 0;

		NEXT_ARG;
		masks[0] = masks[1] = 0;

		while (ac) {
			if (isdigit(**av)) {
				i = atoi(*av);
				if (i < 0 || i > 30)
					errx(EX_DATAERR, "invalid set number %d\n", i);
				masks[which] |= (1<<i);
			} else if (!strncmp(*av, "disable", strlen(*av)))
				which = 0;
			else if (!strncmp(*av, "enable", strlen(*av)))
				which = 1;
			else
				errx(EX_DATAERR, "invalid set command %s\n", *av);
			NEXT_ARG;
		}
		if ( (masks[0] & masks[1]) != 0 )
			errx(EX_DATAERR, "cannot enable and disable the same set\n");
		i = do_set_x(IP_FW_DEL, masks, sizeof(masks));
		if (i)
			warn("set enable/disable: setsockopt(IP_FW_DEL)");
	} else
		errx(EX_USAGE, "invalid set command %s\n", *av);
}

static void
sysctl_handler(int ac, char *av[], int which)
{
	NEXT_ARG;

	if (*av == NULL) {
		warnx("missing keyword to enable/disable\n");
	} else if (strncmp(*av, "firewall", strlen(*av)) == 0) {
		sysctlbyname("net.inet.ip.fw.enable", NULL, 0,
			&which, sizeof(which));
	} else if (strncmp(*av, "one_pass", strlen(*av)) == 0) {
		sysctlbyname("net.inet.ip.fw.one_pass", NULL, 0,
			&which, sizeof(which));
	} else if (strncmp(*av, "debug", strlen(*av)) == 0) {
		sysctlbyname("net.inet.ip.fw.debug", NULL, 0,
			&which, sizeof(which));
	} else if (strncmp(*av, "verbose", strlen(*av)) == 0) {
		sysctlbyname("net.inet.ip.fw.verbose", NULL, 0,
			&which, sizeof(which));
	} else if (strncmp(*av, "dyn_keepalive", strlen(*av)) == 0) {
		sysctlbyname("net.inet.ip.fw.dyn_keepalive", NULL, 0,
			&which, sizeof(which));
	} else {
		warnx("unrecognize enable/disable keyword: %s\n", *av);
	}
}


static void
add_state(int ac, char *av[])
{
	struct ipfw_ioc_state ioc_state;
	ioc_state.expiry = 0;
	ioc_state.lifetime = 0;
	NEXT_ARG;
	if (strcmp(*av, "rulenum") == 0) {
		NEXT_ARG;
		ioc_state.rulenum = atoi(*av);
	} else {
		errx(EX_USAGE, "ipfw state add rule");
	}
	NEXT_ARG;
	struct protoent *pe;
	pe = getprotobyname(*av);
	ioc_state.flow_id.proto = pe->p_proto;

	NEXT_ARG;
	ioc_state.flow_id.src_ip = inet_addr(*av);

	NEXT_ARG;
	ioc_state.flow_id.src_port = atoi(*av);

	NEXT_ARG;
	ioc_state.flow_id.dst_ip = inet_addr(*av);

	NEXT_ARG;
	ioc_state.flow_id.dst_port = atoi(*av);

	NEXT_ARG;
	if (strcmp(*av, "live") == 0) {
		NEXT_ARG;
		ioc_state.lifetime = atoi(*av);
		NEXT_ARG;
	}

	if (strcmp(*av, "expiry") == 0) {
		NEXT_ARG;
		ioc_state.expiry = strtoul(*av, NULL, 10);
		printf("ioc_state.expiry=%d\n", ioc_state.expiry);
	}

	if (do_set_x(IP_FW_STATE_ADD, &ioc_state, sizeof(struct ipfw_ioc_state)) < 0 ) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_STATE_ADD)");
	}
	if (!do_quiet) {
		printf("Flushed all states.\n");
	}
}

static void
delete_state(int ac, char *av[])
{
	int rulenum;
	NEXT_ARG;
	if (ac == 1 && isdigit(**av))
		rulenum = atoi(*av);
	if (do_set_x(IP_FW_STATE_DEL, &rulenum, sizeof(int)) < 0 )
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_STATE_DEL)");
	if (!do_quiet)
		printf("Flushed all states.\n");
}

static void
flush_state(int ac, char *av[])
{
	if (!do_force) {
		int c;

		printf("Are you sure? [yn] ");
		fflush(stdout);
		do {
			c = toupper(getc(stdin));
			while (c != '\n' && getc(stdin) != '\n')
				if (feof(stdin))
					return; /* and do not flush */
		} while (c != 'Y' && c != 'N');
		if (c == 'N')	/* user said no */
			return;
	}
	if (do_set_x(IP_FW_STATE_FLUSH, NULL, 0) < 0 )
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_STATE_FLUSH)");
	if (!do_quiet)
		printf("Flushed all states.\n");
}

static void
list(int ac, char *av[])
{
	struct ipfw_ioc_state *dynrules, *d;
	struct ipfw_ioc_rule *r;

	u_long rnum;
	void *data = NULL;
	int bcwidth, n, nbytes, nstat, ndyn, pcwidth, width;
	int exitval = EX_OK, lac;
	char **lav, *endptr;
	int seen = 0;
	int nalloc = 1024;

	NEXT_ARG;

	/* get rules or pipes from kernel, resizing array as necessary */
	nbytes = nalloc;

	while (nbytes >= nalloc) {
		nalloc = nalloc * 2 ;
		nbytes = nalloc;
		if ((data = realloc(data, nbytes)) == NULL)
			err(EX_OSERR, "realloc");
		if (do_get_x(IP_FW_GET, data, &nbytes) < 0)
			err(EX_OSERR, "do_get_x(IP_FW_GET)");
	}

	/*
	 * Count static rules.
	 */
	r = data;
	nstat = r->static_count;

	/*
	 * Count dynamic rules. This is easier as they have
	 * fixed size.
	 */
	dynrules = (struct ipfw_ioc_state *)((void *)r + r->static_len);
	ndyn = (nbytes - r->static_len) / sizeof(*dynrules);

	/* if showing stats, figure out column widths ahead of time */
	bcwidth = pcwidth = 0;
	if (do_acct) {
		for (n = 0, r = data; n < nstat;
			n++, r = (void *)r + IOC_RULESIZE(r)) {
			/* packet counter */
			width = snprintf(NULL, 0, "%ju", (uintmax_t)r->pcnt);
			if (width > pcwidth)
				pcwidth = width;

			/* byte counter */
			width = snprintf(NULL, 0, "%ju", (uintmax_t)r->bcnt);
			if (width > bcwidth)
				bcwidth = width;
		}
	}
	if (do_dynamic && ndyn) {
		for (n = 0, d = dynrules; n < ndyn; n++, d++) {
			width = snprintf(NULL, 0, "%ju", (uintmax_t)d->pcnt);
			if (width > pcwidth)
				pcwidth = width;

			width = snprintf(NULL, 0, "%ju", (uintmax_t)d->bcnt);
			if (width > bcwidth)
				bcwidth = width;
		}
	}

	/* if no rule numbers were specified, list all rules */
	if (ac == 0) {
		if (do_dynamic != 2) {
			for (n = 0, r = data; n < nstat; n++,
				r = (void *)r + IOC_RULESIZE(r)) {
				show_rules(r, pcwidth, bcwidth);
			}
		}
		if (do_dynamic && ndyn) {
			if (do_dynamic != 2) {
				printf("## States (%d):\n", ndyn);
			}
			for (n = 0, d = dynrules; n < ndyn; n++, d++)
				show_states(d, pcwidth, bcwidth);
		}
		goto done;
	}

	/* display specific rules requested on command line */

	if (do_dynamic != 2) {
		for (lac = ac, lav = av; lac != 0; lac--) {
			/* convert command line rule # */
			rnum = strtoul(*lav++, &endptr, 10);
			if (*endptr) {
				exitval = EX_USAGE;
				warnx("invalid rule number: %s", *(lav - 1));
				continue;
			}
			for (n = seen = 0, r = data; n < nstat;
				n++, r = (void *)r + IOC_RULESIZE(r) ) {
				if (r->rulenum > rnum)
					break;
				if (r->rulenum == rnum) {
					show_rules(r, pcwidth, bcwidth);
					seen = 1;
				}
			}
			if (!seen) {
				/* give precedence to other error(s) */
				if (exitval == EX_OK)
					exitval = EX_UNAVAILABLE;
				warnx("rule %lu does not exist", rnum);
			}
		}
	}

	if (do_dynamic && ndyn) {
		if (do_dynamic != 2) {
			printf("## States (%d):\n", ndyn);
		}
		for (lac = ac, lav = av; lac != 0; lac--) {
			rnum = strtoul(*lav++, &endptr, 10);
			if (*endptr)
				/* already warned */
				continue;
			for (n = 0, d = dynrules; n < ndyn; n++, d++) {
				if (d->rulenum > rnum)
					break;
				if (d->rulenum == rnum)
					show_states(d, pcwidth, bcwidth);
			}
		}
	}

	ac = 0;

done:
	free(data);

	if (exitval != EX_OK)
		exit(exitval);
}

static void
show_dummynet(int ac, char *av[])
{
	void *data = NULL;
	int nbytes;
	int nalloc = 1024; 	/* start somewhere... */

	NEXT_ARG;

	nbytes = nalloc;
	while (nbytes >= nalloc) {
		nalloc = nalloc * 2 + 200;
		nbytes = nalloc;
		if ((data = realloc(data, nbytes)) == NULL)
			err(EX_OSERR, "realloc");
		if (do_get_x(IP_DUMMYNET_GET, data, &nbytes) < 0) {
			err(EX_OSERR, "do_get_x(IP_%s_GET)",
				do_pipe ? "DUMMYNET" : "FW");
		}
	}

	show_pipes(data, nbytes, ac, av);
	free(data);
}

static void
help(void)
{
	fprintf(stderr, "usage: ipfw [options]\n"
			"	ipfw add [rulenum] [set id] action filters\n"
			"	ipfw delete [rulenum]\n"
			"	ipfw flush\n"
			"	ipfw list [rulenum]\n"
			"	ipfw show [rulenum]\n"
			"	ipfw zero [rulenum]\n"
			"	ipfw set [show|enable|disable]\n"
			"	ipfw module\n"
			"	ipfw [enable|disable]\n"
			"	ipfw log [reset|off|on]\n"
			"	ipfw nat [config|show|delete]\n"
			"	ipfw pipe [config|show|delete]\n"
			"	ipfw state [add|delete|list|show]"
			"\nsee ipfw manpage for details\n");
	exit(EX_USAGE);
}

static void
delete_nat_config(int ac, char *av[])
{
	NEXT_ARG;
	int i = 0;
	if (ac > 0) {
		i = atoi(*av);
	}
	if (do_set_x(IP_FW_NAT_DEL, &i, sizeof(i)) == -1)
		err(EX_UNAVAILABLE, "getsockopt(%s)", "IP_FW_NAT_DEL");
}

static void
delete_rules(int ac, char *av[])
{
	struct dn_ioc_pipe pipe;
	u_int32_t rulenum;
	int exitval = EX_OK;
	int do_set = 0;
	int i;

	memset(&pipe, 0, sizeof pipe);

	NEXT_ARG;
	if (ac > 0 && !strncmp(*av, "set", strlen(*av))) {
		do_set = 1; 	/* delete set */
		NEXT_ARG;
	}

	/* Rule number */
	while (ac && isdigit(**av)) {
		i = atoi(*av);
		NEXT_ARG;
		if (do_pipe) {
			if (do_pipe == 1)
				pipe.pipe_nr = i;
			else
				pipe.fs.fs_nr = i;

			i = do_set_x(IP_DUMMYNET_DEL, &pipe, sizeof pipe);
			if (i) {
				exitval = 1;
				warn("rule %u: setsockopt(IP_DUMMYNET_DEL)",
					do_pipe == 1 ? pipe.pipe_nr : pipe.fs.fs_nr);
			}
		} else {
			rulenum = (i & 0xffff) | (do_set << 24);
			i = do_set_x(IP_FW_DEL, &rulenum, sizeof rulenum);
			if (i) {
				exitval = EX_UNAVAILABLE;
				warn("rule %u: setsockopt(IP_FW_DEL)",
					rulenum);
			}
		}
	}
	if (exitval != EX_OK)
		exit(exitval);
}


static unsigned long
getbw(const char *str, u_short *flags, int kb)
{
	unsigned long val;
	int inbytes = 0;
	char *end;

	val = strtoul(str, &end, 0);
	if (*end == 'k' || *end == 'K') {
		++end;
		val *= kb;
	} else if (*end == 'm' || *end == 'M') {
		++end;
		val *= kb * kb;
	}

	/*
	 * Deal with bits or bytes or b(bits) or B(bytes). If there is no
	 * trailer assume bits.
	 */
	if (strncasecmp(end, "bit", 3) == 0) {
		;
	} else if (strncasecmp(end, "byte", 4) == 0) {
		inbytes = 1;
	} else if (*end == 'b') {
		;
	} else if (*end == 'B') {
		inbytes = 1;
	}

	/*
	 * Return in bits if flags is NULL, else flag bits
	 * or bytes in flags and return the unconverted value.
	 */
	if (inbytes && flags)
		*flags |= DN_QSIZE_IS_BYTES;
	else if (inbytes && flags == NULL)
		val *= 8;

	return(val);
}

/*
 * config dummynet pipe/queue
 */
static void
config_dummynet(int ac, char **av)
{
	struct dn_ioc_pipe pipe;
	u_int32_t a;
	void *par = NULL;
	int i;
	char *end;

	NEXT_ARG;
	memset(&pipe, 0, sizeof pipe);
	/* Pipe number */
	if (ac && isdigit(**av)) {
		i = atoi(*av);
		NEXT_ARG;
		if (do_pipe == 1)
			pipe.pipe_nr = i;
		else
			pipe.fs.fs_nr = i;
	}

	while (ac > 0) {
		double d;

		int tok = match_token(dummynet_params, *av);
		NEXT_ARG;

		switch(tok) {
		case TOK_NOERROR:
			pipe.fs.flags_fs |= DN_NOERROR;
			break;

		case TOK_PLR:
			NEED1("plr needs argument 0..1\n");
			d = strtod(av[0], NULL);
			if (d > 1)
				d = 1;
			else if (d < 0)
				d = 0;
			pipe.fs.plr = (int)(d*0x7fffffff);
			NEXT_ARG;
			break;

		case TOK_QUEUE:
			NEED1("queue needs queue size\n");
			end = NULL;
			pipe.fs.qsize = getbw(av[0], &pipe.fs.flags_fs, 1024);
			NEXT_ARG;
			break;

		case TOK_BUCKETS:
			NEED1("buckets needs argument\n");
			pipe.fs.rq_size = strtoul(av[0], NULL, 0);
			NEXT_ARG;
			break;

		case TOK_MASK:
			NEED1("mask needs mask specifier\n");
			/*
			 * per-flow queue, mask is dst_ip, dst_port,
			 * src_ip, src_port, proto measured in bits
			 */
			par = NULL;

			pipe.fs.flow_mask.type = ETHERTYPE_IP;
			pipe.fs.flow_mask.u.ip.dst_ip = 0;
			pipe.fs.flow_mask.u.ip.src_ip = 0;
			pipe.fs.flow_mask.u.ip.dst_port = 0;
			pipe.fs.flow_mask.u.ip.src_port = 0;
			pipe.fs.flow_mask.u.ip.proto = 0;
			end = NULL;

			while (ac >= 1) {
				u_int32_t *p32 = NULL;
				u_int16_t *p16 = NULL;

				tok = match_token(dummynet_params, *av);
				NEXT_ARG;
				switch(tok) {
				case TOK_ALL:
					/*
					 * special case, all bits significant
					 */
					pipe.fs.flow_mask.u.ip.dst_ip = ~0;
					pipe.fs.flow_mask.u.ip.src_ip = ~0;
					pipe.fs.flow_mask.u.ip.dst_port = ~0;
					pipe.fs.flow_mask.u.ip.src_port = ~0;
					pipe.fs.flow_mask.u.ip.proto = ~0;
					pipe.fs.flags_fs |= DN_HAVE_FLOW_MASK;
					goto end_mask;

				case TOK_DSTIP:
					p32 = &pipe.fs.flow_mask.u.ip.dst_ip;
					break;

				case TOK_SRCIP:
					p32 = &pipe.fs.flow_mask.u.ip.src_ip;
					break;

				case TOK_DSTPORT:
					p16 = &pipe.fs.flow_mask.u.ip.dst_port;
					break;

				case TOK_SRCPORT:
					p16 = &pipe.fs.flow_mask.u.ip.src_port;
					break;

				case TOK_PROTO:
					break;

				default:
					NEXT_ARG;
					goto end_mask;
				}
				if (ac < 1)
					errx(EX_USAGE, "mask: value missing");
				if (*av[0] == '/') {
					a = strtoul(av[0]+1, &end, 0);
					a = (a == 32) ? ~0 : (1 << a) - 1;
				} else
					a = strtoul(av[0], &end, 0);
				if (p32 != NULL)
					*p32 = a;
				else if (p16 != NULL) {
					if (a > 65535)
						errx(EX_DATAERR,
						"mask: must be 16 bit");
					*p16 = (u_int16_t)a;
				} else {
					if (a > 255)
						errx(EX_DATAERR,
						"mask: must be 8 bit");
					pipe.fs.flow_mask.u.ip.proto = (uint8_t)a;
				}
				if (a != 0)
					pipe.fs.flags_fs |= DN_HAVE_FLOW_MASK;
				NEXT_ARG;
			} /* end while, config masks */

end_mask:
			break;

		case TOK_RED:
		case TOK_GRED:
			NEED1("red/gred needs w_q/min_th/max_th/max_p\n");
			pipe.fs.flags_fs |= DN_IS_RED;
			if (tok == TOK_GRED)
				pipe.fs.flags_fs |= DN_IS_GENTLE_RED;
			/*
			 * the format for parameters is w_q/min_th/max_th/max_p
			 */
			if ((end = strsep(&av[0], "/"))) {
				double w_q = strtod(end, NULL);
				if (w_q > 1 || w_q <= 0)
				errx(EX_DATAERR, "0 < w_q <= 1");
				pipe.fs.w_q = (int) (w_q * (1 << SCALE_RED));
			}
			if ((end = strsep(&av[0], "/"))) {
				pipe.fs.min_th = strtoul(end, &end, 0);
				if (*end == 'K' || *end == 'k')
				pipe.fs.min_th *= 1024;
			}
			if ((end = strsep(&av[0], "/"))) {
				pipe.fs.max_th = strtoul(end, &end, 0);
				if (*end == 'K' || *end == 'k')
				pipe.fs.max_th *= 1024;
			}
			if ((end = strsep(&av[0], "/"))) {
				double max_p = strtod(end, NULL);
				if (max_p > 1 || max_p <= 0)
				errx(EX_DATAERR, "0 < max_p <= 1");
				pipe.fs.max_p = (int)(max_p * (1 << SCALE_RED));
			}
			NEXT_ARG;
			break;

		case TOK_DROPTAIL:
			pipe.fs.flags_fs &= ~(DN_IS_RED|DN_IS_GENTLE_RED);
			break;

		case TOK_BW:
			NEED1("bw needs bandwidth\n");
			if (do_pipe != 1)
				errx(EX_DATAERR, "bandwidth only valid for pipes");
			/*
			 * set bandwidth value
			 */
			pipe.bandwidth = getbw(av[0], NULL, 1000);
			if (pipe.bandwidth < 0)
				errx(EX_DATAERR, "bandwidth too large");
			NEXT_ARG;
			break;

		case TOK_DELAY:
			if (do_pipe != 1)
				errx(EX_DATAERR, "delay only valid for pipes");
			NEED1("delay needs argument 0..10000ms\n");
			pipe.delay = strtoul(av[0], NULL, 0);
			NEXT_ARG;
			break;

		case TOK_WEIGHT:
			if (do_pipe == 1)
				errx(EX_DATAERR, "weight only valid for queues");
			NEED1("weight needs argument 0..100\n");
			pipe.fs.weight = strtoul(av[0], &end, 0);
			NEXT_ARG;
			break;

		case TOK_PIPE:
			if (do_pipe == 1)
				errx(EX_DATAERR, "pipe only valid for queues");
			NEED1("pipe needs pipe_number\n");
			pipe.fs.parent_nr = strtoul(av[0], &end, 0);
			NEXT_ARG;
			break;

		default:
			errx(EX_DATAERR, "unrecognised option ``%s''", *av);
		}
	}
	if (do_pipe == 1) {
		if (pipe.pipe_nr == 0)
			errx(EX_DATAERR, "pipe_nr must be > 0");
		if (pipe.delay > 10000)
			errx(EX_DATAERR, "delay must be < 10000");
	} else { /* do_pipe == 2, queue */
		if (pipe.fs.parent_nr == 0)
			errx(EX_DATAERR, "pipe must be > 0");
		if (pipe.fs.weight >100)
			errx(EX_DATAERR, "weight must be <= 100");
	}
	if (pipe.fs.flags_fs & DN_QSIZE_IS_BYTES) {
		if (pipe.fs.qsize > 1024*1024)
			errx(EX_DATAERR, "queue size must be < 1MB");
	} else {
		if (pipe.fs.qsize > 100)
			errx(EX_DATAERR, "2 <= queue size <= 100");
	}
	if (pipe.fs.flags_fs & DN_IS_RED) {
		size_t len;
		int lookup_depth, avg_pkt_size;
		double s, idle, weight, w_q;
		int clock_hz;
		int t;

		if (pipe.fs.min_th >= pipe.fs.max_th)
			errx(EX_DATAERR, "min_th %d must be < than max_th %d",
			pipe.fs.min_th, pipe.fs.max_th);
		if (pipe.fs.max_th == 0)
			errx(EX_DATAERR, "max_th must be > 0");

		len = sizeof(int);
		if (sysctlbyname("net.inet.ip.dummynet.red_lookup_depth",
			&lookup_depth, &len, NULL, 0) == -1)

			errx(1, "sysctlbyname(\"%s\")",
				"net.inet.ip.dummynet.red_lookup_depth");
		if (lookup_depth == 0)
			errx(EX_DATAERR, "net.inet.ip.dummynet.red_lookup_depth"
				" must be greater than zero");

		len = sizeof(int);
		if (sysctlbyname("net.inet.ip.dummynet.red_avg_pkt_size",
			&avg_pkt_size, &len, NULL, 0) == -1)

			errx(1, "sysctlbyname(\"%s\")",
				"net.inet.ip.dummynet.red_avg_pkt_size");
		if (avg_pkt_size == 0)
			errx(EX_DATAERR,
				"net.inet.ip.dummynet.red_avg_pkt_size must"
				" be greater than zero");

		len = sizeof(clock_hz);
		if (sysctlbyname("net.inet.ip.dummynet.hz", &clock_hz, &len,
				 NULL, 0) == -1) {
			errx(1, "sysctlbyname(\"%s\")",
				 "net.inet.ip.dummynet.hz");
		}

		/*
		 * Ticks needed for sending a medium-sized packet.
		 * Unfortunately, when we are configuring a WF2Q+ queue, we
		 * do not have bandwidth information, because that is stored
		 * in the parent pipe, and also we have multiple queues
		 * competing for it. So we set s=0, which is not very
		 * correct. But on the other hand, why do we want RED with
		 * WF2Q+ ?
		 */
		if (pipe.bandwidth == 0) /* this is a WF2Q+ queue */
			s = 0;
		else
			s = clock_hz * avg_pkt_size * 8 / pipe.bandwidth;

		/*
		 * max idle time (in ticks) before avg queue size becomes 0.
		 * NOTA: (3/w_q) is approx the value x so that
		 * (1-w_q)^x < 10^-3.
		 */
		w_q = ((double)pipe.fs.w_q) / (1 << SCALE_RED);
		idle = s * 3. / w_q;
		pipe.fs.lookup_step = (int)idle / lookup_depth;
		if (!pipe.fs.lookup_step)
			pipe.fs.lookup_step = 1;
		weight = 1 - w_q;
		for (t = pipe.fs.lookup_step; t > 0; --t)
			weight *= weight;
		pipe.fs.lookup_weight = (int)(weight * (1 << SCALE_RED));
	}
	i = do_set_x(IP_DUMMYNET_CONFIGURE, &pipe, sizeof pipe);
	if (i)
		err(1, "do_set_x(%s)", "IP_DUMMYNET_CONFIGURE");
}

/*
 * helper function, updates the pointer to cmd with the length
 * of the current command, and also cleans up the first word of
 * the new command in case it has been clobbered before.
 */
static ipfw_insn*
next_cmd(ipfw_insn *cmd)
{
	cmd += F_LEN(cmd);
	bzero(cmd, sizeof(*cmd));
	return cmd;
}

/*
 * Parse arguments and assemble the microinstructions which make up a rule.
 * Rules are added into the 'rulebuf' and then copied in the correct order
 * into the actual rule.
 *
 *
 */
static void
add(int ac, char *av[])
{
	/*
	 * rules are added into the 'rulebuf' and then copied in
	 * the correct order into the actual rule.
	 * Some things that need to go out of order (prob, action etc.)
	 * go into actbuf[].
	 */
	static uint32_t rulebuf[IPFW_RULE_SIZE_MAX];
	static uint32_t actbuf[IPFW_RULE_SIZE_MAX];
	static uint32_t othbuf[IPFW_RULE_SIZE_MAX];
	static uint32_t cmdbuf[IPFW_RULE_SIZE_MAX];

	ipfw_insn *src, *dst, *cmd, *action, *other;
	ipfw_insn *the_comment = NULL;
	struct ipfw_ioc_rule *rule;
	struct ipfw_keyword *key;
	struct ipfw_mapping *map;
	parser_func fn;
	int i, j;

	bzero(actbuf, sizeof(actbuf)); 		/* actions go here */
	bzero(othbuf, sizeof(actbuf)); 		/* others */
	bzero(cmdbuf, sizeof(cmdbuf)); 		/* filters */
	bzero(rulebuf, sizeof(rulebuf));

	rule = (struct ipfw_ioc_rule *)rulebuf;
	cmd = (ipfw_insn *)cmdbuf;
	action = (ipfw_insn *)actbuf;
	other = (ipfw_insn *)othbuf;

	NEED2("need more parameters");
	NEXT_ARG;

	/* [rule N]	-- Rule number optional */
	if (ac && isdigit(**av)) {
		rule->rulenum = atoi(*av);
		NEXT_ARG;
	}

	/* [set N]	-- set number (0..30), optional */
	if (ac > 1 && !strncmp(*av, "set", strlen(*av))) {
		int set = strtoul(av[1], NULL, 10);
		if (set < 0 || set > 30)
			errx(EX_DATAERR, "illegal set %s", av[1]);
		rule->set = set;
		av += 2; ac -= 2;
	}

	/*
	 * parse others
	 */
	for (;;) {
		for (i = 0, key = keywords; i < KEYWORD_SIZE; i++, key++) {
			if (key->type == IPFW_KEYWORD_TYPE_OTHERS &&
				strcmp(key->word, *av) == 0) {
				for (j = 0, map = mappings;
					j < MAPPING_SIZE; j++, map++) {
					if (map->type == IPFW_MAPPING_TYPE_IN_USE &&
						map->module == key->module &&
						map->opcode == key->opcode ) {
						fn = map->parser;
						(*fn)(&other, &ac, &av);
						break;
					}
				}
				break;
			}
		}
		if (i >= KEYWORD_SIZE) {
			break;
		} else if (F_LEN(other) > 0) {
			if (other->module == MODULE_BASIC_ID &&
				other->opcode == O_BASIC_CHECK_STATE) {
				other = next_cmd(other);
				goto done;
			}
			other = next_cmd(other);
		}
	}

	/*
	 * parse actions
	 *
	 * only accept 1 action
	 */
	NEED1("missing action");
	for (i = 0, key = keywords; i < KEYWORD_SIZE; i++, key++) {
		if (ac > 0 && key->type == IPFW_KEYWORD_TYPE_ACTION &&
			strcmp(key->word, *av) == 0) {
			for (j = 0, map = mappings; j<MAPPING_SIZE; j++, map++) {
				if (map->type == IPFW_MAPPING_TYPE_IN_USE &&
					map->module == key->module &&
					map->opcode == key->opcode) {
					fn = map->parser;
					(*fn)(&action, &ac, &av);
					break;
				}
			}
			break;
		}
	}
	if (F_LEN(action) > 0)
		action = next_cmd(action);

	/*
	 * parse protocol
	 */
	if (strcmp(*av, "proto") == 0){
		NEXT_ARG;
	}

	NEED1("missing protocol");
	for (i = 0, key = keywords; i < KEYWORD_SIZE; i++, key++) {
		if (key->type == IPFW_KEYWORD_TYPE_FILTER &&
			strcmp(key->word, "proto") == 0) {
			for (j = 0, map = mappings; j<MAPPING_SIZE; j++, map++) {
				if (map->type == IPFW_MAPPING_TYPE_IN_USE &&
					map->module == key->module &&
					map->opcode == key->opcode ) {
					fn = map->parser;
					(*fn)(&cmd, &ac, &av);
					break;
				}
			}
			break;
		}
	}
	if (F_LEN(cmd) > 0)
		cmd = next_cmd(cmd);

	/*
	 * other filters
	 */
	while (ac > 0) {
		char *s;
		ipfw_insn_u32 *cmd32; 	/* alias for cmd */

		s = *av;
		cmd32 = (ipfw_insn_u32 *)cmd;
		if (strcmp(*av, "not") == 0) {
			if (cmd->len & F_NOT)
				errx(EX_USAGE, "double \"not\" not allowed\n");
			cmd->len = F_NOT;
			NEXT_ARG;
			continue;
		}
		if (*s == '!') {	/* alternate syntax for NOT */
			if (cmd->len & F_NOT)
				errx(EX_USAGE, "double \"not\" not allowed");
			cmd->len = F_NOT;
			s++;
		}
		for (i = 0, key = keywords; i < KEYWORD_SIZE; i++, key++) {
			if (key->type == IPFW_KEYWORD_TYPE_FILTER &&
				strcmp(key->word, s) == 0) {
				for (j = 0, map = mappings;
					j< MAPPING_SIZE; j++, map++) {
					if (map->type == IPFW_MAPPING_TYPE_IN_USE &&
						map->module == key->module &&
						map->opcode == key->opcode ) {
						fn = map->parser;
						(*fn)(&cmd, &ac, &av);
						break;
					}
				}
				break;
			} else if (i == KEYWORD_SIZE-1) {
				errx(EX_USAGE, "bad command `%s'", s);
			}
		}
		if (i >= KEYWORD_SIZE) {
			break;
		} else if (F_LEN(cmd) > 0) {
			cmd = next_cmd(cmd);
		}
	}

done:
	if (ac>0)
		errx(EX_USAGE, "bad command `%s'", *av);

	/*
	 * Now copy stuff into the rule.
	 * [filters][others][action][comment]
	 */
	dst = (ipfw_insn *)rule->cmd;
	/*
	 * copy all filters, except comment
	 */
	src = (ipfw_insn *)cmdbuf;
	for (src = (ipfw_insn *)cmdbuf; src != cmd; src += i) {
		/* pick comment out */
		i = F_LEN(src);
		if (src->module == MODULE_BASIC_ID && src->opcode == O_BASIC_COMMENT) {
			the_comment=src;
		} else {
			bcopy(src, dst, i * sizeof(u_int32_t));
			dst = (ipfw_insn *)((uint32_t *)dst + i);
		}
	}

	/*
	 * start action section, it begin with others
	 */
	rule->act_ofs = (uint32_t *)dst - (uint32_t *)(rule->cmd);

	/*
	 * copy all other others
	 */
	for (src = (ipfw_insn *)othbuf; src != other; src += i) {
		i = F_LEN(src);
		bcopy(src, dst, i * sizeof(u_int32_t));
		dst = (ipfw_insn *)((uint32_t *)dst + i);
	}

	/* copy the action to the end of rule */
	src = (ipfw_insn *)actbuf;
	i = F_LEN(src);
	bcopy(src, dst, i * sizeof(u_int32_t));
	dst = (ipfw_insn *)((uint32_t *)dst + i);

	/*
	 * comment place behind the action
	 */
	if (the_comment != NULL) {
		i = F_LEN(the_comment);
		bcopy(the_comment, dst, i * sizeof(u_int32_t));
		dst = (ipfw_insn *)((uint32_t *)dst + i);
	}

	rule->cmd_len = (u_int32_t *)dst - (u_int32_t *)(rule->cmd);
	i = (void *)dst - (void *)rule;
	if (do_set_x(IP_FW_ADD, (void *)rule, i) == -1) {
		err(EX_UNAVAILABLE, "getsockopt(%s)", "IP_FW_ADD");
	}
	if (!do_quiet)
		show_rules(rule, 10, 10);
}

static void
zero(int ac, char *av[])
{
	int rulenum;
	int failed = EX_OK;

	NEXT_ARG;

	if (!ac) {
		/* clear all entries */
		if (do_set_x(IP_FW_ZERO, NULL, 0) < 0)
			err(EX_UNAVAILABLE, "do_set_x(IP_FW_ZERO)");
		if (!do_quiet)
			printf("Accounting cleared.\n");
		return;
	}

	while (ac) {
		/* Rule number */
		if (isdigit(**av)) {
			rulenum = atoi(*av);
			NEXT_ARG;
			if (do_set_x(IP_FW_ZERO, &rulenum, sizeof rulenum)) {
				warn("rule %u: do_set_x(IP_FW_ZERO)", rulenum);
				failed = EX_UNAVAILABLE;
			} else if (!do_quiet)
				printf("Entry %d cleared\n", rulenum);
		} else {
			errx(EX_USAGE, "invalid rule number ``%s''", *av);
		}
	}
	if (failed != EX_OK)
		exit(failed);
}

static void
resetlog(int ac, char *av[])
{
	int rulenum;
	int failed = EX_OK;

	NEXT_ARG;

	if (!ac) {
		/* clear all entries */
		if (setsockopt(ipfw_socket, IPPROTO_IP, IP_FW_RESETLOG, NULL, 0) < 0)
			err(EX_UNAVAILABLE, "setsockopt(IP_FW_RESETLOG)");
		if (!do_quiet)
			printf("Logging counts reset.\n");

		return;
	}

	while (ac) {
		/* Rule number */
		if (isdigit(**av)) {
			rulenum = atoi(*av);
			NEXT_ARG;
			if (setsockopt(ipfw_socket, IPPROTO_IP,
				IP_FW_RESETLOG, &rulenum, sizeof rulenum)) {
				warn("rule %u: setsockopt(IP_FW_RESETLOG)", rulenum);
				failed = EX_UNAVAILABLE;
			} else if (!do_quiet)
				printf("Entry %d logging count reset\n", rulenum);
		} else {
			errx(EX_DATAERR, "invalid rule number ``%s''", *av);
		}
	}
	if (failed != EX_OK)
		exit(failed);
}

static void
flush(void)
{
	int cmd = IP_FW_FLUSH;
	if (do_pipe) {
		cmd = IP_DUMMYNET_FLUSH;
	} else if (do_nat) {
		cmd = IP_FW_NAT_FLUSH;
	}
	if (!do_force) {
		int c;

		printf("Are you sure? [yn] ");
		fflush(stdout);
		do {
			c = toupper(getc(stdin));
			while (c != '\n' && getc(stdin) != '\n')
				if (feof(stdin))
					return; /* and do not flush */
		} while (c != 'Y' && c != 'N');
		if (c == 'N')	/* user said no */
			return;
	}
	if (do_set_x(cmd, NULL, 0) < 0 ) {
		err(EX_UNAVAILABLE, "do_set_x(%s)",
			do_pipe? "IP_DUMMYNET_FLUSH":
			(do_nat? "IP_FW_NAT_FLUSH": "IP_FW_FLUSH"));
	}
	if (!do_quiet) {
		printf("Flushed all %s.\n", do_pipe ? "pipes":
				(do_nat?"nat configurations":"rules"));
	}
}

static void
str2addr(const char* str, struct in_addr* addr)
{
	struct hostent* hp;

	if (inet_aton (str, addr))
		return;

	hp = gethostbyname (str);
	if (!hp)
		errx (1, "unknown host %s", str);

	memcpy (addr, hp->h_addr, sizeof (struct in_addr));
}

static int
str2portrange(const char* str, const char* proto, port_range *portRange)
{
	struct servent*	sp;
	char*	sep;
	char*	end;
	u_short	loPort, hiPort;

	/* First see if this is a service, return corresponding port if so. */
	sp = getservbyname (str, proto);
	if (sp) {
		SETLOPORT(*portRange, ntohs(sp->s_port));
		SETNUMPORTS(*portRange, 1);
		return 0;
	}

	/* Not a service, see if it's a single port or port range. */
	sep = strchr (str, '-');
	if (sep == NULL) {
		SETLOPORT(*portRange, strtol(str, &end, 10));
		if (end != str) {
			/* Single port. */
			SETNUMPORTS(*portRange, 1);
			return 0;
		}

		/* Error in port range field. */
		errx (EX_DATAERR, "%s/%s: unknown service", str, proto);
	}

	/* Port range, get the values and sanity check. */
	sscanf (str, "%hu-%hu", &loPort, &hiPort);
	SETLOPORT(*portRange, loPort);
	SETNUMPORTS(*portRange, 0); 	/* Error by default */
	if (loPort <= hiPort)
		SETNUMPORTS(*portRange, hiPort - loPort + 1);

	if (GETNUMPORTS(*portRange) == 0)
		errx (EX_DATAERR, "invalid port range %s", str);

	return 0;
}

static int
str2proto(const char* str)
{
	if (!strcmp (str, "tcp"))
		return IPPROTO_TCP;
	if (!strcmp (str, "udp"))
		return IPPROTO_UDP;
	errx (EX_DATAERR, "unknown protocol %s. Expected tcp or udp", str);
}

static int
str2addr_portrange (const char* str, struct in_addr* addr,
	char* proto, port_range *portRange)
{
	char*	ptr;

	ptr = strchr (str, ':');
	if (!ptr)
		errx (EX_DATAERR, "%s is missing port number", str);

	*ptr = '\0';
	++ptr;

	str2addr (str, addr);
	return str2portrange (ptr, proto, portRange);
}

/*
 * Search for interface with name "ifn", and fill n accordingly:
 *
 * n->ip		ip address of interface "ifn"
 * n->if_name copy of interface name "ifn"
 */
static void
set_addr_dynamic(const char *ifn, struct cfg_nat *n)
{
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
	struct sockaddr_dl *sdl;
	struct sockaddr_in *sin;
	char *buf, *lim, *next;
	size_t needed;
	int mib[6];
	int ifIndex, ifMTU;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;

	/*
	 * Get interface data.
	 */
	if (sysctl(mib, 6, NULL, &needed, NULL, 0) == -1)
		err(1, "iflist-sysctl-estimate");
	if ((buf = malloc(needed)) == NULL)
		errx(1, "malloc failed");
	if (sysctl(mib, 6, buf, &needed, NULL, 0) == -1)
		err(1, "iflist-sysctl-get");
	lim = buf + needed;
	/*
	 * Loop through interfaces until one with
	 * given name is found. This is done to
	 * find correct interface index for routing
	 * message processing.
	 */
	ifIndex	= 0;
	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		next += ifm->ifm_msglen;
		if (ifm->ifm_version != RTM_VERSION) {
			if (verbose)
				warnx("routing message version %d "
					"not understood", ifm->ifm_version);
			continue;
		}
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			if (strlen(ifn) == sdl->sdl_nlen &&
				strncmp(ifn, sdl->sdl_data, sdl->sdl_nlen) == 0) {
				ifIndex = ifm->ifm_index;
				ifMTU = ifm->ifm_data.ifi_mtu;
				break;
			}
		}
	}
	if (!ifIndex)
		errx(1, "unknown interface name %s", ifn);
	/*
	 * Get interface address.
	 */
	sin = NULL;
	while (next < lim) {
		ifam = (struct ifa_msghdr *)next;
		next += ifam->ifam_msglen;
		if (ifam->ifam_version != RTM_VERSION) {
			if (verbose)
				warnx("routing message version %d "
					"not understood", ifam->ifam_version);
			continue;
		}
		if (ifam->ifam_type != RTM_NEWADDR)
			break;
		if (ifam->ifam_addrs & RTA_IFA) {
			int i;
			char *cp = (char *)(ifam + 1);

			for (i = 1; i < RTA_IFA; i <<= 1) {
				if (ifam->ifam_addrs & i)
					cp += SA_SIZE((struct sockaddr *)cp);
			}
			if (((struct sockaddr *)cp)->sa_family == AF_INET) {
				sin = (struct sockaddr_in *)cp;
				break;
			}
		}
	}
	if (sin == NULL)
		errx(1, "%s: cannot get interface address", ifn);

	n->ip = sin->sin_addr;
	strncpy(n->if_name, ifn, IF_NAMESIZE);

	free(buf);
}

static int
setup_redir_addr(char *spool_buf, int len, int *_ac, char ***_av)
{
	struct cfg_redir *r;
	struct cfg_spool *tmp;
	char **av, *sep;
	char tmp_spool_buf[NAT_BUF_LEN];
	int ac, i, space, lsnat;

	i=0;
	av = *_av;
	ac = *_ac;
	space = 0;
	lsnat = 0;
	if (len >= SOF_REDIR) {
		r = (struct cfg_redir *)spool_buf;
		/* Skip cfg_redir at beginning of buf. */
		spool_buf = &spool_buf[SOF_REDIR];
		space = SOF_REDIR;
		len -= SOF_REDIR;
	} else {
		goto nospace;
	}

	r->mode = REDIR_ADDR;
	/* Extract local address. */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_addr: missing local address");

	sep = strchr(*av, ',');
	if (sep) {		/* LSNAT redirection syntax. */
		r->laddr.s_addr = INADDR_NONE;
		/* Preserve av, copy spool servers to tmp_spool_buf. */
		strncpy(tmp_spool_buf, *av, strlen(*av)+1);
		lsnat = 1;
	} else {
		str2addr(*av, &r->laddr);
	}
	INC_ARGCV();

	/* Extract public address. */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_addr: missing public address");

	str2addr(*av, &r->paddr);
	INC_ARGCV();

	/* Setup LSNAT server pool. */
	if (sep) {
		sep = strtok(tmp_spool_buf, ", ");
		while (sep != NULL) {
			tmp = (struct cfg_spool *)spool_buf;
			if (len < SOF_SPOOL)
				goto nospace;

			len -= SOF_SPOOL;
			space += SOF_SPOOL;
			str2addr(sep, &tmp->addr);
			tmp->port = ~0;
			r->spool_cnt++;
			/* Point to the next possible cfg_spool. */
			spool_buf = &spool_buf[SOF_SPOOL];
			sep = strtok(NULL, ", ");
		}
	}
	return(space);

nospace:
	errx(EX_DATAERR, "redirect_addr: buf is too small\n");
}

static int
setup_redir_port(char *spool_buf, int len, int *_ac, char ***_av)
{
	char **av, *sep, *protoName;
	char tmp_spool_buf[NAT_BUF_LEN];
	int ac, space, lsnat;
	struct cfg_redir *r;
	struct cfg_spool *tmp;
	u_short numLocalPorts;
	port_range portRange;

	av = *_av;
	ac = *_ac;
	space = 0;
	lsnat = 0;
	numLocalPorts = 0;

	if (len >= SOF_REDIR) {
		r = (struct cfg_redir *)spool_buf;
		/* Skip cfg_redir at beginning of buf. */
		spool_buf = &spool_buf[SOF_REDIR];
		space = SOF_REDIR;
		len -= SOF_REDIR;
	} else {
		goto nospace;
	}

	r->mode = REDIR_PORT;
	/*
	 * Extract protocol.
	 */
	if (ac == 0)
		errx (EX_DATAERR, "redirect_port: missing protocol");

	r->proto = str2proto(*av);
	protoName = *av;
	INC_ARGCV();

	/*
	 * Extract local address.
	 */
	if (ac == 0)
		errx (EX_DATAERR, "redirect_port: missing local address");

	sep = strchr(*av, ',');
	/* LSNAT redirection syntax. */
	if (sep) {
		r->laddr.s_addr = INADDR_NONE;
		r->lport = ~0;
		numLocalPorts = 1;
		/* Preserve av, copy spool servers to tmp_spool_buf. */
		strncpy(tmp_spool_buf, *av, strlen(*av)+1);
		lsnat = 1;
	} else {
		if (str2addr_portrange (*av, &r->laddr, protoName, &portRange) != 0)
			errx(EX_DATAERR, "redirect_port:"
				"invalid local port range");

		r->lport = GETLOPORT(portRange);
		numLocalPorts = GETNUMPORTS(portRange);
	}
	INC_ARGCV();

	/*
	 * Extract public port and optionally address.
	 */
	if (ac == 0)
		errx (EX_DATAERR, "redirect_port: missing public port");

	sep = strchr (*av, ':');
	if (sep) {
		if (str2addr_portrange (*av, &r->paddr, protoName, &portRange) != 0)
			errx(EX_DATAERR, "redirect_port:"
				"invalid public port range");
	} else {
		r->paddr.s_addr = INADDR_ANY;
		if (str2portrange(*av, protoName, &portRange) != 0)
			errx(EX_DATAERR, "redirect_port:"
				"invalid public port range");
	}

	r->pport = GETLOPORT(portRange);
	r->pport_cnt = GETNUMPORTS(portRange);
	INC_ARGCV();

	/*
	 * Extract remote address and optionally port.
	 */
	/*
	 * NB: isalpha(**av) => we've to check that next parameter is really an
	 * option for this redirect entry, else stop here processing arg[cv].
	 */
	if (ac != 0 && !isalpha(**av)) {
		sep = strchr (*av, ':');
		if (sep) {
			if (str2addr_portrange (*av, &r->raddr,
				protoName, &portRange) != 0)
				errx(EX_DATAERR, "redirect_port:"
					"invalid remote port range");
		} else {
			SETLOPORT(portRange, 0);
			SETNUMPORTS(portRange, 1);
			str2addr (*av, &r->raddr);
		}
		INC_ARGCV();
	} else {
		SETLOPORT(portRange, 0);
		SETNUMPORTS(portRange, 1);
		r->raddr.s_addr = INADDR_ANY;
	}
	r->rport = GETLOPORT(portRange);
	r->rport_cnt = GETNUMPORTS(portRange);

	/*
	 * Make sure port ranges match up, then add the redirect ports.
	 */
	if (numLocalPorts != r->pport_cnt)
		errx(EX_DATAERR, "redirect_port:"
			"port ranges must be equal in size");

	/* Remote port range is allowed to be '0' which means all ports. */
	if (r->rport_cnt != numLocalPorts &&
		(r->rport_cnt != 1 || r->rport != 0))
			errx(EX_DATAERR, "redirect_port: remote port must"
				"be 0 or equal to local port range in size");

	/*
	 * Setup LSNAT server pool.
	 */
	if (lsnat) {
		sep = strtok(tmp_spool_buf, ", ");
		while (sep != NULL) {
			tmp = (struct cfg_spool *)spool_buf;
			if (len < SOF_SPOOL)
				goto nospace;

			len -= SOF_SPOOL;
			space += SOF_SPOOL;
			if (str2addr_portrange(sep,
				&tmp->addr, protoName, &portRange) != 0)
				errx(EX_DATAERR, "redirect_port:"
					"invalid local port range");
			if (GETNUMPORTS(portRange) != 1)
				errx(EX_DATAERR, "redirect_port: local port"
					"must be single in this context");
			tmp->port = GETLOPORT(portRange);
			r->spool_cnt++;
			/* Point to the next possible cfg_spool. */
			spool_buf = &spool_buf[SOF_SPOOL];
			sep = strtok(NULL, ", ");
		}
	}
	return (space);

nospace:
	errx(EX_DATAERR, "redirect_port: buf is too small\n");
}

static int
setup_redir_proto(char *spool_buf, int len, int *_ac, char ***_av)
{
	struct protoent *protoent;
	struct cfg_redir *r;
	int ac, i, space;
	char **av;

	i=0;
	av = *_av;
	ac = *_ac;
	if (len >= SOF_REDIR) {
		r = (struct cfg_redir *)spool_buf;
		/* Skip cfg_redir at beginning of buf. */
		spool_buf = &spool_buf[SOF_REDIR];
		space = SOF_REDIR;
		len -= SOF_REDIR;
	} else {
		goto nospace;
	}
	r->mode = REDIR_PROTO;
	/*
	 * Extract protocol.
	 */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_proto: missing protocol");

	protoent = getprotobyname(*av);
	if (protoent == NULL)
		errx(EX_DATAERR, "redirect_proto: unknown protocol %s", *av);
	else
		r->proto = protoent->p_proto;

	INC_ARGCV();

	/*
	 * Extract local address.
	 */
	if (ac == 0)
		errx(EX_DATAERR, "redirect_proto: missing local address");
	else
		str2addr(*av, &r->laddr);
	INC_ARGCV();

	/*
	 * Extract optional public address.
	 */
	if (ac == 0) {
		r->paddr.s_addr = INADDR_ANY;
		r->raddr.s_addr = INADDR_ANY;
	} else {
		/* see above in setup_redir_port() */
		if (!isalpha(**av)) {
			str2addr(*av, &r->paddr);
			INC_ARGCV();

			/*
			 * Extract optional remote address.
			 */
			/* see above in setup_redir_port() */
			if (ac != 0 && !isalpha(**av)) {
				str2addr(*av, &r->raddr);
				INC_ARGCV();
			}
		}
	}
	return (space);

nospace:
	errx(EX_DATAERR, "redirect_proto: buf is too small\n");
}

static void
show_nat_config(char *buf) {
	struct cfg_nat *n;
	struct cfg_redir *t;
	struct cfg_spool *s;
	struct protoent *p;
	int i, cnt, flag, off;

	n = (struct cfg_nat *)buf;
	flag = 1;
	off = sizeof(*n);
	printf("ipfw nat %u config", n->id);
	if (strlen(n->if_name) != 0)
		printf(" if %s", n->if_name);
	else if (n->ip.s_addr != 0)
		printf(" ip %s", inet_ntoa(n->ip));
	while (n->mode != 0) {
		if (n->mode & PKT_ALIAS_LOG) {
			printf(" log");
			n->mode &= ~PKT_ALIAS_LOG;
		} else if (n->mode & PKT_ALIAS_DENY_INCOMING) {
			printf(" deny_in");
			n->mode &= ~PKT_ALIAS_DENY_INCOMING;
		} else if (n->mode & PKT_ALIAS_SAME_PORTS) {
			printf(" same_ports");
			n->mode &= ~PKT_ALIAS_SAME_PORTS;
		} else if (n->mode & PKT_ALIAS_UNREGISTERED_ONLY) {
			printf(" unreg_only");
			n->mode &= ~PKT_ALIAS_UNREGISTERED_ONLY;
		} else if (n->mode & PKT_ALIAS_RESET_ON_ADDR_CHANGE) {
			printf(" reset");
			n->mode &= ~PKT_ALIAS_RESET_ON_ADDR_CHANGE;
		} else if (n->mode & PKT_ALIAS_REVERSE) {
			printf(" reverse");
			n->mode &= ~PKT_ALIAS_REVERSE;
		} else if (n->mode & PKT_ALIAS_PROXY_ONLY) {
			printf(" proxy_only");
			n->mode &= ~PKT_ALIAS_PROXY_ONLY;
		}
	}
	/* Print all the redirect's data configuration. */
	for (cnt = 0; cnt < n->redir_cnt; cnt++) {
		t = (struct cfg_redir *)&buf[off];
		off += SOF_REDIR;
		switch (t->mode) {
		case REDIR_ADDR:
			printf(" redirect_addr");
			if (t->spool_cnt == 0)
				printf(" %s", inet_ntoa(t->laddr));
			else
				for (i = 0; i < t->spool_cnt; i++) {
					s = (struct cfg_spool *)&buf[off];
					if (i)
						printf(", ");
					else
						printf(" ");
					printf("%s", inet_ntoa(s->addr));
					off += SOF_SPOOL;
				}
			printf(" %s", inet_ntoa(t->paddr));
			break;
		case REDIR_PORT:
			p = getprotobynumber(t->proto);
			printf(" redirect_port %s ", p->p_name);
			if (!t->spool_cnt) {
				printf("%s:%u", inet_ntoa(t->laddr), t->lport);
				if (t->pport_cnt > 1)
					printf("-%u", t->lport + t->pport_cnt - 1);
			} else
				for (i=0; i < t->spool_cnt; i++) {
					s = (struct cfg_spool *)&buf[off];
					if (i)
						printf(", ");
					printf("%s:%u", inet_ntoa(s->addr), s->port);
					off += SOF_SPOOL;
				}

			printf(" ");
			if (t->paddr.s_addr)
				printf("%s:", inet_ntoa(t->paddr));
			printf("%u", t->pport);
			if (!t->spool_cnt && t->pport_cnt > 1)
				printf("-%u", t->pport + t->pport_cnt - 1);

			if (t->raddr.s_addr) {
				printf(" %s", inet_ntoa(t->raddr));
				if (t->rport) {
					printf(":%u", t->rport);
					if (!t->spool_cnt && t->rport_cnt > 1)
						printf("-%u", t->rport +
							t->rport_cnt - 1);
				}
			}
			break;
		case REDIR_PROTO:
			p = getprotobynumber(t->proto);
			printf(" redirect_proto %s %s", p->p_name,
				inet_ntoa(t->laddr));
			if (t->paddr.s_addr != 0) {
				printf(" %s", inet_ntoa(t->paddr));
				if (t->raddr.s_addr)
					printf(" %s", inet_ntoa(t->raddr));
			}
			break;
		default:
			errx(EX_DATAERR, "unknown redir mode");
			break;
		}
	}
	printf("\n");
}


static void
show_nat(int ac, char **av) {
	struct cfg_nat *n;
	struct cfg_redir *e;
	int i, nbytes, nalloc, size;
	int nat_cnt, redir_cnt, nat_id;
	uint8_t *data;

	nalloc = 1024;
	size = 0;
	data = NULL;

	NEXT_ARG;

	if (ac == 0)
		nat_id = 0;
	else
		nat_id = strtoul(*av, NULL, 10);

	nbytes = nalloc;
	while (nbytes >= nalloc) {
		nalloc = nalloc * 2;
		nbytes = nalloc;
		if ((data = realloc(data, nbytes)) == NULL) {
			err(EX_OSERR, "realloc");
		}
		if (do_get_x(IP_FW_NAT_GET, data, &nbytes) < 0) {
			err(EX_OSERR, "do_get_x(IP_FW_NAT_GET)");
		}
	}

	if (nbytes == 0) {
		exit(EX_OK);
	}

	nat_cnt = *((int *)data);
	for (i = sizeof(nat_cnt); nat_cnt; nat_cnt--) {
		n = (struct cfg_nat *)&data[i];
		if (n->id >= 0 && n->id <= IPFW_DEFAULT_RULE) {
			if (nat_id == 0 || n->id == nat_id)
				show_nat_config(&data[i]);
		}
		i += sizeof(struct cfg_nat);
		for (redir_cnt = 0; redir_cnt < n->redir_cnt; redir_cnt++) {
			e = (struct cfg_redir *)&data[i];
			i += sizeof(struct cfg_redir) +
				e->spool_cnt * sizeof(struct cfg_spool);
		}
	}
}

/*
 * do_set_x - extended version og do_set
 * insert a x_header in the beginning of the rule buf
 * and call setsockopt() with IP_FW_X.
 */
int
do_set_x(int optname, void *rule, int optlen)
{
	int len;

	ip_fw_x_header *x_header;
	if (ipfw_socket < 0) {
		err(EX_UNAVAILABLE, "socket");
	}
	bzero(new_rule_buf, IPFW_RULE_SIZE_MAX);
	x_header = (ip_fw_x_header *)new_rule_buf;
	x_header->opcode = optname;
	/* copy the rule into the newbuf, just after the x_header*/
	bcopy(rule, ++x_header, optlen);
	len = optlen + sizeof(ip_fw_x_header);
	return setsockopt(ipfw_socket, IPPROTO_IP, IP_FW_X, new_rule_buf, len);
}

/*
 * same as do_set_x
 */
int
do_get_x(int optname, void *rule, int *optlen)
{
	int len, retval;

	ip_fw_x_header *x_header;
	if (ipfw_socket < 0) {
		err(EX_UNAVAILABLE, "socket");
	}
	bzero(new_rule_buf, IPFW_RULE_SIZE_MAX);
	x_header = (ip_fw_x_header *)new_rule_buf;
	x_header->opcode = optname;
	/* copy the rule into the newbuf, just after the x_header*/
	bcopy(rule, ++x_header, *optlen);
	len = *optlen + sizeof(ip_fw_x_header);
	retval = getsockopt(ipfw_socket, IPPROTO_IP, IP_FW_X, new_rule_buf, &len);
	bcopy(new_rule_buf, rule, len);
	*optlen=len;
	return retval;
}

static void
config_nat(int ac, char **av)
{
	struct cfg_nat *n; 			 /* Nat instance configuration. */
	int i, len, off, tok;
	char *id, buf[NAT_BUF_LEN]; 	/* Buffer for serialized data. */

	len = NAT_BUF_LEN;
	/* Offset in buf: save space for n at the beginning. */
	off = sizeof(struct cfg_nat);
	memset(buf, 0, sizeof(buf));
	n = (struct cfg_nat *)buf;

	NEXT_ARG;
	/* Nat id. */
	if (ac && isdigit(**av)) {
		id = *av;
		i = atoi(*av);
		NEXT_ARG;
		n->id = i;
	} else
		errx(EX_DATAERR, "missing nat id");
	if (ac == 0)
		errx(EX_DATAERR, "missing option");

	while (ac > 0) {
		tok = match_token(nat_params, *av);
		NEXT_ARG;
		switch (tok) {
		case TOK_IP:
			if (ac == 0)
				errx(EX_DATAERR, "missing option");
			if (!inet_aton(av[0], &(n->ip)))
				errx(EX_DATAERR, "bad ip address ``%s''",
					av[0]);
			NEXT_ARG;
			break;
		case TOK_IF:
			if (ac == 0)
				errx(EX_DATAERR, "missing option");
			set_addr_dynamic(av[0], n);
			NEXT_ARG;
			break;
		case TOK_ALOG:
			n->mode |= PKT_ALIAS_LOG;
			break;
		case TOK_DENY_INC:
			n->mode |= PKT_ALIAS_DENY_INCOMING;
			break;
		case TOK_SAME_PORTS:
			n->mode |= PKT_ALIAS_SAME_PORTS;
			break;
		case TOK_UNREG_ONLY:
			n->mode |= PKT_ALIAS_UNREGISTERED_ONLY;
			break;
		case TOK_RESET_ADDR:
			n->mode |= PKT_ALIAS_RESET_ON_ADDR_CHANGE;
			break;
		case TOK_ALIAS_REV:
			n->mode |= PKT_ALIAS_REVERSE;
			break;
		case TOK_PROXY_ONLY:
			n->mode |= PKT_ALIAS_PROXY_ONLY;
			break;
			/*
			 * All the setup_redir_* functions work directly in the final
			 * buffer, see above for details.
			 */
		case TOK_REDIR_ADDR:
		case TOK_REDIR_PORT:
		case TOK_REDIR_PROTO:
			switch (tok) {
			case TOK_REDIR_ADDR:
				i = setup_redir_addr(&buf[off], len, &ac, &av);
				break;
			case TOK_REDIR_PORT:
				i = setup_redir_port(&buf[off], len, &ac, &av);
				break;
			case TOK_REDIR_PROTO:
				i = setup_redir_proto(&buf[off], len, &ac, &av);
				break;
			}
			n->redir_cnt++;
			off += i;
			len -= i;
			break;
		default:
			errx(EX_DATAERR, "unrecognised option ``%s''", av[-1]);
		}
	}
	i = do_set_x(IP_FW_NAT_CFG, buf, off);
	if (i)
		err(1, "do_set_x(%s)", "IP_FW_NAT_CFG");

	/* After every modification, we show the resultant rule. */
	int _ac = 2;
	char *_av[] = {"config", id};
	show_nat(_ac, _av);
}


static int
ipfw_main(int ac, char **av)
{
	int ch;

	if (ac == 1)
		help();

	/* Set the force flag for non-interactive processes */
	do_force = !isatty(STDIN_FILENO);

	optind = optreset = 1;
	while ((ch = getopt(ac, av, "hs:acdDefNStTv")) != -1)
		switch (ch) {
		case 'h': /* help */
			help();
			break; 	/* NOTREACHED */

		case 's': /* sort */
			do_sort = atoi(optarg);
			break;
		case 'a':
			do_acct = 1;
			break;
		case 'c':
			do_compact = 1;
			break;
		case 'd':
			do_dynamic = 1;
			break;
		case 'D':
			do_dynamic = 2;
			break;
		case 'e':
			do_expired = 1;
			break;
		case 'f':
			do_force = 1;
			break;
		case 'N':
			do_resolv = 1;
			break;
		case 'S':
			show_sets = 1;
			break;
		case 't':
			do_time = 1;
			break;
		case 'T':
			do_time = 2;
			break;
		case 'v':
			do_quiet = 0;
			verbose++;
			break;
		default:
			help();
		}

	ac -= optind;
	av += optind;
	NEED1("bad arguments, for usage summary ``ipfw''");

	/*
	 * optional: pipe or queue or nat
	 */
	do_nat = 0;
	do_pipe = 0;
	if (!strncmp(*av, "nat", strlen(*av)))
		do_nat = 1;
	else if (!strncmp(*av, "pipe", strlen(*av))) {
		do_pipe = 1;
	} else if (!strncmp(*av, "queue", strlen(*av))) {
		do_pipe = 2;
	}
	NEED1("missing command");

	/*
	 * for pipes and queues and nat we normally say 'pipe NN config'
	 * but the code is easier to parse as 'pipe config NN'
	 * so we swap the two arguments.
	 */
	if ((do_pipe || do_nat) && ac > 2 && isdigit(*(av[1]))) {
		char *p = av[1];
		av[1] = av[2];
		av[2] = p;
	}

	if (!strncmp(*av, "add", strlen(*av))) {
		load_modules();
		add(ac, av);
	} else if (!strncmp(*av, "delete", strlen(*av))) {
		delete_rules(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		flush();
	} else if (!strncmp(*av, "list", strlen(*av))) {
		load_modules();
		list(ac, av);
	} else if (!strncmp(*av, "show", strlen(*av))) {
		do_acct++;
		load_modules();
		list(ac, av);
	} else if (!strncmp(*av, "zero", strlen(*av))) {
		zero(ac, av);
	} else if (!strncmp(*av, "set", strlen(*av))) {
		sets_handler(ac, av);
	} else if (!strncmp(*av, "module", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "show", strlen(*av)) ||
			!strncmp(*av, "show", strlen(*av))) {
			list_modules(ac, av);
		} else {
			errx(EX_USAGE, "bad ipfw module command `%s'", *av);
		}
	} else if (!strncmp(*av, "enable", strlen(*av))) {
		sysctl_handler(ac, av, 1);
	} else if (!strncmp(*av, "disable", strlen(*av))) {
		sysctl_handler(ac, av, 0);
	} else if (!strncmp(*av, "resetlog", strlen(*av))) {
		resetlog(ac, av);
	} else if (!strncmp(*av, "log", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "reset", strlen(*av))) {
			resetlog(ac, av);
		} else if (!strncmp(*av, "off", strlen(*av))) {

		} else if (!strncmp(*av, "on", strlen(*av))) {

		} else {
			errx(EX_USAGE, "bad command `%s'", *av);
		}
	} else if (!strncmp(*av, "nat", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "config", strlen(*av))) {
			config_nat(ac, av);
		} else if (!strncmp(*av, "flush", strlen(*av))) {
			flush();
		} else if (!strncmp(*av, "show", strlen(*av)) ||
				!strncmp(*av, "list", strlen(*av))) {
			show_nat(ac, av);
		} else if (!strncmp(*av, "delete", strlen(*av))) {
			delete_nat_config(ac, av);
		} else {
			errx(EX_USAGE, "bad ipfw nat command `%s'", *av);
		}
	} else if (!strncmp(*av, "pipe", strlen(*av)) ||
		!strncmp(*av, "queue", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "config", strlen(*av))) {
			config_dummynet(ac, av);
		} else if (!strncmp(*av, "flush", strlen(*av))) {
			flush();
		} else if (!strncmp(*av, "show", strlen(*av))) {
			show_dummynet(ac, av);
		} else {
			errx(EX_USAGE, "bad ipfw pipe command `%s'", *av);
		}
	} else if (!strncmp(*av, "state", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "add", strlen(*av))) {
			add_state(ac, av);
		} else if (!strncmp(*av, "delete", strlen(*av))) {
			delete_state(ac, av);
		} else if (!strncmp(*av, "flush", strlen(*av))) {
			flush_state(ac, av);
		} else if (!strncmp(*av, "list", strlen(*av))) {
			do_dynamic = 2;
			list(ac, av);
		} else if (!strncmp(*av, "show", strlen(*av))) {
			do_acct = 1;
			do_dynamic =2;
			list(ac, av);
		} else {
			errx(EX_USAGE, "bad ipfw state command `%s'", *av);
		}
	} else {
		errx(EX_USAGE, "bad ipfw command `%s'", *av);
	}
	return 0;
}

static void
ipfw_readfile(int ac, char *av[])
{
	char	buf[BUFSIZ];
	char	*a, *p, *args[MAX_ARGS], *cmd = NULL;
	char	linename[10];
	int	i=0, lineno=0, qflag=0, pflag=0, status;
	FILE	*f = NULL;
	pid_t	preproc = 0;
	int	c;

	while ((c = getopt(ac, av, "D:U:p:q")) != -1)
		switch (c) {
			case 'D':
				if (!pflag)
					errx(EX_USAGE, "-D requires -p");
				if (i > MAX_ARGS - 2)
					errx(EX_USAGE,
							"too many -D or -U options");
				args[i++] = "-D";
				args[i++] = optarg;
				break;

			case 'U':
				if (!pflag)
					errx(EX_USAGE, "-U requires -p");
				if (i > MAX_ARGS - 2)
					errx(EX_USAGE,
							"too many -D or -U options");
				args[i++] = "-U";
				args[i++] = optarg;
				break;

			case 'p':
				pflag = 1;
				cmd = optarg;
				args[0] = cmd;
				i = 1;
				break;

			case 'q':
				qflag = 1;
				break;

			default:
				errx(EX_USAGE, "bad arguments, for usage"
						" summary ``ipfw''");
		}

	av += optind;
	ac -= optind;
	if (ac != 1)
		errx(EX_USAGE, "extraneous filename arguments");

	if ((f = fopen(av[0], "r")) == NULL)
		err(EX_UNAVAILABLE, "fopen: %s", av[0]);

	if (pflag) {
		/* pipe through preprocessor (cpp or m4) */
		int pipedes[2];

		args[i] = NULL;

		if (pipe(pipedes) == -1)
			err(EX_OSERR, "cannot create pipe");

		switch ((preproc = fork())) {
			case -1:
				err(EX_OSERR, "cannot fork");

			case 0:
				/* child */
				if (dup2(fileno(f), 0) == -1 ||
					dup2(pipedes[1], 1) == -1) {
					err(EX_OSERR, "dup2()");
				}
				fclose(f);
				close(pipedes[1]);
				close(pipedes[0]);
				execvp(cmd, args);
				err(EX_OSERR, "execvp(%s) failed", cmd);

			default:
				/* parent */
				fclose(f);
				close(pipedes[1]);
				if ((f = fdopen(pipedes[0], "r")) == NULL) {
					int savederrno = errno;

					kill(preproc, SIGTERM);
					errno = savederrno;
					err(EX_OSERR, "fdopen()");
				}
		}
	}

	while (fgets(buf, BUFSIZ, f)) {
		lineno++;
		sprintf(linename, "Line %d", lineno);
		args[0] = linename;

		if (*buf == '#')
			continue;
		if ((p = strchr(buf, '#')) != NULL)
			*p = '\0';
		i = 1;
		if (qflag)
			args[i++] = "-q";
		for (a = strtok(buf, WHITESP); a && i < MAX_ARGS;
			a = strtok(NULL, WHITESP), i++) {
			args[i] = a;
		}

		if (i == (qflag? 2: 1))
			continue;
		if (i == MAX_ARGS)
			errx(EX_USAGE, "%s: too many arguments", linename);

		args[i] = NULL;
		ipfw_main(i, args);
	}
	fclose(f);
	if (pflag) {
		if (waitpid(preproc, &status, 0) == -1)
			errx(EX_OSERR, "waitpid()");
		if (WIFEXITED(status) && WEXITSTATUS(status) != EX_OK)
			errx(EX_UNAVAILABLE, "preprocessor exited with status %d",
				WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			errx(EX_UNAVAILABLE, "preprocessor exited with signal %d",
				WTERMSIG(status));
	}
}

int
main(int ac, char *av[])
{
	ipfw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (ipfw_socket < 0)
		err(EX_UNAVAILABLE, "socket");

	memset(keywords, 0, sizeof(struct ipfw_keyword) * KEYWORD_SIZE);
	memset(mappings, 0, sizeof(struct ipfw_mapping) * MAPPING_SIZE);

	prepare_default_funcs();

	if (ac > 1 && av[ac - 1][0] == '/' && access(av[ac - 1], R_OK) == 0)
		ipfw_readfile(ac, av);
	else
		ipfw_main(ac, av);
	return EX_OK;
}
