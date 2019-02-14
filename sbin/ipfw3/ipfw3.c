/*
 * Copyright (c) 2014 - 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@dragonflybsd.org>
 *
 * Copyright (c) 2002 Luigi Rizzo
 * Copyright (c) 1996 Alex Nash, Paul Traina, Poul-Henning Kamp
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Idea and grammar partially left from:
 * Copyright (c) 1993 Daniel Boulet
 *
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


#include <net/ipfw3/ip_fw3.h>
#include <net/ipfw3_basic/ip_fw3_table.h>
#include <net/ipfw3_basic/ip_fw3_state.h>
#include <net/ipfw3_basic/ip_fw3_sync.h>
#include <net/ipfw3_basic/ip_fw3_basic.h>
#include <net/ipfw3_nat/ip_fw3_nat.h>
#include <net/dummynet3/ip_dummynet3.h>

#include "ipfw3.h"
#include "ipfw3basic.h"
#include "ipfw3log.h"
#include "ipfw3set.h"
#include "ipfw3table.h"
#include "ipfw3dummynet.h"
#include "ipfw3state.h"
#include "ipfw3sync.h"
#include "ipfw3nat.h"

#define MAX_ARGS	32
#define WHITESP		" \t\f\v\n\r"
#define IPFW3_LIB_PATH	"/usr/lib/libipfw3%s.so"

int		fw3_socket = -1;	/* main RAW socket */
int		do_acct, 		/* Show packet/byte count */
		do_time, 		/* Show time stamps */
		do_quiet = 1,		/* Be quiet , default is quiet*/
		do_force, 		/* Don't ask for confirmation */
		do_pipe, 		/* this cmd refers to a pipe */
		do_nat, 		/* Nat configuration. */
		do_sort, 		/* field to sort results (0 = no) */
		do_expired, 		/* display expired dynamic rules */
		do_compact, 		/* show rules in compact mode */
		show_sets, 		/* display rule sets */
		verbose;

struct ipfw3_keyword keywords[KEYWORD_SIZE];
struct ipfw3_mapping mappings[MAPPING_SIZE];

int
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

void
module_get(char *modules_str, int len)
{
	if (do_get_x(IP_FW_MODULE, modules_str, &len) < 0)
		errx(EX_USAGE, "ipfw3 not loaded.");
}

void
module_list(int ac, char *av[])
{
	void *module_str = NULL;
	int len = 1024;
	if ((module_str = realloc(module_str, len)) == NULL)
		err(EX_OSERR, "realloc");

	module_get(module_str, len);
	printf("%s\n", (char *)module_str);
}

void
module_load(void)
{
	const char *error;
	init_module mod_init_func;
	void *module_lib;
	char module_lib_file[50];
	void *module_str = NULL;
	int len = 1024;

	if ((module_str = realloc(module_str, len)) == NULL)
		err(EX_OSERR, "realloc");

	module_get(module_str, len);

	const char s[2] = ",";
	char *token;
	token = strtok(module_str, s);
	while (token != NULL) {
		sprintf(module_lib_file, IPFW3_LIB_PATH, token);
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
register_ipfw_keyword(int module, int opcode, char *word, int type)
{
	struct ipfw3_keyword *tmp;

	tmp=keywords;
	for (;;) {
		if (tmp->type == NONE) {
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
	struct ipfw3_mapping *tmp;

	tmp = mappings;
	while (1) {
		if (tmp->type == NONE) {
			tmp->module = module;
			tmp->opcode = opcode;
			tmp->parser = parser;
			tmp->shower = shower;
			tmp->type = IN_USE;
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

/*
 * this func need to check whether 'or' need to be printed,
 * when the filter is the first filter with 'or' when dont print
 * when not first and same as previous, then print or and no filter name
 * when not first but different from previous, print name without 'or'
 * show_or = 1: show or and ignore filter name
 * show_or = 0: show filter name ignore or
 */
void prev_show_chk(ipfw_insn *cmd, uint8_t *prev_module, uint8_t *prev_opcode,
		int *show_or)
{
	if (cmd->len & F_OR) {
		if (*prev_module == 0 && *prev_opcode == 0) {
			/* first cmd with 'or' flag */
			*show_or = 0;
			*prev_module = cmd->module;
			*prev_opcode = cmd->opcode;
		} else if (cmd->module == *prev_module &&
				cmd->opcode == *prev_opcode) {
			/* cmd same as previous, same module and opcode */
			*show_or = 1;
		} else {
			/* cmd different from prev*/
			*show_or = 0;
			*prev_module = cmd->module;
			*prev_opcode = cmd->opcode;

		}
	} else {
		*show_or = 0;
		*prev_module = 0;
		*prev_opcode = 0;
	}
}

/*
 * word can be: proto from to other
 * proto show proto
 * from show from
 * to show to
 * other show all other filters
 */
int show_filter(ipfw_insn *cmd, char *word, int type)
{
	struct ipfw3_keyword *k;
	struct ipfw3_mapping *m;
	shower_func fn;
	int i, j, show_or;
	uint8_t prev_module, prev_opcode;

	k = keywords;
	m = mappings;
	for (i = 1; i < KEYWORD_SIZE; i++, k++) {
		if (k->type == type) {
			if (k->module == cmd->module &&
					k->opcode == cmd->opcode) {
				for (j = 1; j < MAPPING_SIZE; j++, m++) {
					if (m->type == IN_USE &&
						k->module == m->module &&
						k->opcode == m->opcode) {
						prev_show_chk(cmd, &prev_module,
							&prev_opcode, &show_or);
						if (cmd->len & F_NOT)
							printf(" not");

						fn = m->shower;
						(*fn)(cmd, show_or);
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

void
help(void)
{
	fprintf(stderr, "usage: ipfw3 [options]\n"
			"	ipfw3 add [rulenum] [set id] action filters\n"
			"	ipfw3 delete [rulenum]\n"
			"	ipfw3 flush\n"
			"	ipfw3 list [rulenum]\n"
			"	ipfw3 show [rulenum]\n"
			"	ipfw3 zero [rulenum]\n"
			"	ipfw3 set [show|enable|disable]\n"
			"	ipfw3 module\n"
			"	ipfw3 [enable|disable]\n"
			"	ipfw3 log [reset|off|on]\n"
			"	ipfw3 nat [config|show|delete]\n"
			"	ipfw3 pipe [config|show|delete]\n"
			"	ipfw3 state [add|delete|list|show]\n"
			"	ipfw3 nat [config|show]\n"
			"\nsee ipfw3 manpage for details\n");
	exit(EX_USAGE);
}

void
rule_delete(int ac, char *av[])
{
	int error, rulenum;

	NEXT_ARG;

	while (ac && isdigit(**av)) {
		rulenum = atoi(*av);
		error = do_set_x(IP_FW_DEL, &rulenum, sizeof(int));
		if (error) {
			err(EX_OSERR, "do_get_x(IP_FW_DEL)");
		}
		NEXT_ARG;
	}
}

/*
 * helper function, updates the pointer to cmd with the length
 * of the current command, and also cleans up the first word of
 * the new command in case it has been clobbered before.
 */
ipfw_insn*
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
void
rule_add(int ac, char *av[], uint8_t insert)
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
	ipfw_insn *prev;
	char *prev_av;
	ipfw_insn *the_comment = NULL;
	struct ipfw_ioc_rule *rule;
	struct ipfw3_keyword *key;
	struct ipfw3_mapping *map;
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

	rule->insert = insert;

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
	 * parse before
	 */
	for (;;) {
		for (i = 0, key = keywords; i < KEYWORD_SIZE; i++, key++) {
			if (key->type == BEFORE &&
				strcmp(key->word, *av) == 0) {
				for (j = 0, map = mappings;
					j < MAPPING_SIZE; j++, map++) {
					if (map->type == IN_USE &&
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
		if (ac > 0 && key->type == ACTION &&
			strcmp(key->word, *av) == 0) {
			for (j = 0, map = mappings;
					j < MAPPING_SIZE; j++, map++) {
				if (map->type == IN_USE &&
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
		if (key->type == PROTO &&
			strcmp(key->word, "proto") == 0) {
			for (j = 0, map = mappings;
					j < MAPPING_SIZE; j++, map++) {
				if (map->type == IN_USE &&
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
		char *s, *cur;		/* current filter */
		ipfw_insn_u32 *cmd32; 	/* alias for cmd */

		s = *av;
		cmd32 = (ipfw_insn_u32 *)cmd;
		if (strcmp(*av, "or") == 0) {
			if (prev == NULL)
				errx(EX_USAGE, "'or' should"
						"between two filters\n");
			prev->len |= F_OR;
			cmd->len = F_OR;
			*av = prev_av;
		}
		if (strcmp(*av, "not") == 0) {
			if (cmd->len & F_NOT)
				errx(EX_USAGE, "double \"not\" not allowed\n");
			cmd->len = F_NOT;
			NEXT_ARG;
			continue;
		}
		cur = *av;
		for (i = 0, key = keywords; i < KEYWORD_SIZE; i++, key++) {
			if ((key->type == FILTER ||
                                key->type == AFTER ||
                                key->type == FROM ||
                                key->type == TO) &&
				strcmp(key->word, cur) == 0) {
				for (j = 0, map = mappings;
					j< MAPPING_SIZE; j++, map++) {
					if (map->type == IN_USE &&
						map->module == key->module &&
						map->opcode == key->opcode ) {
						fn = map->parser;
						(*fn)(&cmd, &ac, &av);
						break;
					}
				}
				break;
			} else if (i == KEYWORD_SIZE - 1) {
				errx(EX_USAGE, "bad command `%s'", cur);
			}
		}
		if (i >= KEYWORD_SIZE) {
			break;
		} else if (F_LEN(cmd) > 0) {
			prev = cmd;
			prev_av = cur;
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
		if (src->module == MODULE_BASIC_ID &&
				src->opcode == O_BASIC_COMMENT) {
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
		err(EX_UNAVAILABLE, "setsockopt(%s)", "IP_FW_ADD");
	}
	if (!do_quiet)
		rule_show(rule, 10, 10);
}

void
rule_zero(int ac, char *av[])
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

void
rule_flush(void)
{
	int cmd = IP_FW_FLUSH;
	if (do_pipe) {
		cmd = IP_DUMMYNET_FLUSH;
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
		if (do_pipe)
			errx(EX_USAGE, "pipe/queue in use");
		else
			errx(EX_USAGE, "do_set_x(IP_FW_FLUSH) failed");
	}
	if (!do_quiet) {
		printf("Flushed all %s.\n", do_pipe ? "pipes" : "rules");
	}
}

void
rule_list(int ac, char *av[])
{
	struct ipfw_ioc_rule *rule;

	void *data = NULL;
	int bcwidth, nbytes, pcwidth, width;
	int nalloc = 1024;
	int the_rule_num = 0;
	int total_len;

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
	rule = data;
	bcwidth = pcwidth = 0;
	if (do_acct) {
		total_len = 0;
		for (rule = data; rule != NULL; rule = (void *)rule + IOC_RULESIZE(rule)) {
			/* packet counter */
			width = snprintf(NULL, 0, "%ju", (uintmax_t)rule->pcnt);
			if (width > pcwidth)
				pcwidth = width;

			/* byte counter */
			width = snprintf(NULL, 0, "%ju", (uintmax_t)rule->bcnt);
			if (width > bcwidth)
				bcwidth = width;

			total_len += IOC_RULESIZE(rule);
			if (total_len == nbytes) {
				break;
			}
		}
	}


	if (ac == 1) {
		the_rule_num = atoi(*av);
	}

	total_len = 0;
	for (rule = data; rule != NULL; rule = (void *)rule + IOC_RULESIZE(rule)) {
		if(the_rule_num == 0 || rule->rulenum == the_rule_num) {
			rule_show(rule, pcwidth, bcwidth);
		}
		total_len += IOC_RULESIZE(rule);
		if (total_len == nbytes) {
			break;
		}
	}

}

void
rule_show(struct ipfw_ioc_rule *rule, int pcwidth, int bcwidth)
{
	static int twidth = 0;
	ipfw_insn *cmd;
	int l;

	u_int32_t set_disable = rule->sets;

	if (set_disable & (1 << rule->set)) { /* disabled */
		if (!show_sets)
			return;
		else
			printf("# DISABLED ");
	}
	if (do_compact) {
		printf("%u", rule->rulenum);
	} else {
		printf("%05u", rule->rulenum);
	}

	if (do_acct) {
		if (do_compact) {
			printf(" %ju %ju", (uintmax_t)rule->pcnt,
						(uintmax_t)rule->bcnt);
		} else {
			printf(" %*ju %*ju", pcwidth, (uintmax_t)rule->pcnt,
				bcwidth, (uintmax_t)rule->bcnt);
		}
	}

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
			printf(" %s", timestr);
		} else {
			printf(" %*s", twidth, " ");
		}
	} else if (do_time == 2) {
		printf( " %10u", rule->timestamp);
	}

	if (show_sets)
		printf(" set %d", rule->set);


	struct ipfw3_keyword *k;
	struct ipfw3_mapping *m;
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
					if (m->type == IN_USE &&
						m->module == cmd->module &&
						m->opcode == cmd->opcode) {
						if (cmd->module == MODULE_BASIC_ID &&
							cmd->opcode == O_BASIC_COMMENT) {
							comment_fn = m->shower;
							comment_cmd = cmd;
						} else {
							fn = m->shower;
							(*fn)(cmd, 0);
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
	for (l = rule->act_ofs, cmd = rule->cmd; l > 0; l -= F_LEN(cmd),
			cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		changed = show_filter(cmd, "proto", PROTO);
	}
	if (!changed && !do_quiet)
		printf(" ip");

	/*
	 * show from
	 */
	changed = 0;
	for (l = rule->act_ofs, cmd = rule->cmd; l > 0; l -= F_LEN(cmd),
			cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		changed = show_filter(cmd, "from", FROM);
	}
	if (!changed && !do_quiet)
		printf(" from any");

	/*
	 * show to
	 */
	changed = 0;
	for (l = rule->act_ofs, cmd = rule->cmd; l > 0; l -= F_LEN(cmd),
			cmd = (ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd))) {
		changed = show_filter(cmd, "to", TO);
	}
	if (!changed && !do_quiet)
		printf(" to any");

	/*
	 * show other filters
	 */
	l = rule->act_ofs;
	cmd = rule->cmd;
	m = mappings;
	for ( ; l > 0; ) {
		show_filter(cmd, "other", FILTER);
		l -= F_LEN(cmd);
		cmd=(ipfw_insn *)((uint32_t *)cmd + F_LEN(cmd));
	}

	/* show the comment in the end */
	if (comment_fn != NULL) {
		(*comment_fn)(comment_cmd, 0);
	}
done:
	printf("\n");
}

/*
 * do_set_x - extended version of do_set
 * insert a x_header in the beginning of the rule buf
 * and call setsockopt() with IP_FW_X.
 */
int
do_set_x(int optname, void *rule, int optlen)
{
	int len, *newbuf, retval;
	ip_fw_x_header *x_header;

	if (fw3_socket < 0)
		err(EX_UNAVAILABLE, "socket not avaialble");

	len = optlen + sizeof(ip_fw_x_header);
	newbuf = malloc(len);
	if (newbuf == NULL)
		err(EX_OSERR, "malloc newbuf in do_set_x");

	bzero(newbuf, len);
	x_header = (ip_fw_x_header *)newbuf;
	x_header->opcode = optname;
	/* copy the rule into the newbuf, just after the x_header*/
	bcopy(rule, ++x_header, optlen);
	retval = setsockopt(fw3_socket, IPPROTO_IP, IP_FW_X, newbuf, len);
	free(newbuf);
	return retval;
}

/*
 * same as do_set_x
 */
int
do_get_x(int optname, void *rule, int *optlen)
{
	int len, *newbuf, retval;
	ip_fw_x_header *x_header;

	if (fw3_socket < 0)
		err(EX_UNAVAILABLE, "socket not avaialble");

	len = *optlen + sizeof(ip_fw_x_header);
	newbuf = malloc(len);
	if (newbuf == NULL)
		err(EX_OSERR, "malloc newbuf in do_get_x");

	bzero(newbuf, len);
	x_header = (ip_fw_x_header *)newbuf;
	x_header->opcode = optname;
	/* copy the rule into the newbuf, just after the x_header*/
	bcopy(rule, ++x_header, *optlen);
	retval = getsockopt(fw3_socket, IPPROTO_IP, IP_FW_X, newbuf, &len);
	bcopy(newbuf, rule, len);
	free(newbuf);
	*optlen = len;
	return retval;
}

int
ipfw3_main(int ac, char **av)
{
	int ch;

	if (ac == 1)
		help();

	/* Set the force flag for non-interactive processes */
	do_force = !isatty(STDIN_FILENO);

	optind = optreset = 1;
	while ((ch = getopt(ac, av, "hs:acefStTv")) != -1)
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
		case 'e':
			do_expired = 1;
			break;
		case 'f':
			do_force = 1;
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
	NEED1("bad arguments, for usage summary ``ipfw3''");

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
		module_load();
		rule_add(ac, av, 0);
	} else if (!strncmp(*av, "insert", strlen(*av))) {
		module_load();
		rule_add(ac, av, 1);
	} else if (!strncmp(*av, "delete", strlen(*av))) {
		rule_delete(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		rule_flush();
	} else if (!strncmp(*av, "list", strlen(*av))) {
		module_load();
		rule_list(ac, av);
	} else if (!strncmp(*av, "show", strlen(*av))) {
		do_acct++;
		module_load();
		rule_list(ac, av);
	} else if (!strncmp(*av, "zero", strlen(*av))) {
		rule_zero(ac, av);
	} else if (!strncmp(*av, "set", strlen(*av))) {
		set_main(ac, av);
	} else if (!strncmp(*av, "module", strlen(*av))) {
		NEXT_ARG;
		if (!strncmp(*av, "list", strlen(*av))) {
			module_list(ac, av);
		} else {
			errx(EX_USAGE, "bad ipfw3 module command `%s'", *av);
		}
	} else if (!strncmp(*av, "log", strlen(*av))) {
		NEXT_ARG;
		log_main(ac, av);
	} else if (!strncmp(*av, "nat", strlen(*av))) {
		NEXT_ARG;
		nat_main(ac, av);
	} else if (!strncmp(*av, "pipe", strlen(*av)) ||
		!strncmp(*av, "queue", strlen(*av))) {
		NEXT_ARG;
		dummynet_main(ac, av);
	} else if (!strncmp(*av, "state", strlen(*av))) {
		NEXT_ARG;
		state_main(ac, av);
	} else if (!strncmp(*av, "table", strlen(*av))) {
		if (ac > 2 && isdigit(*(av[1]))) {
			char *p = av[1];
			av[1] = av[2];
			av[2] = p;
		}
		NEXT_ARG;
		table_main(ac, av);
	} else if (!strncmp(*av, "sync", strlen(*av))) {
		NEXT_ARG;
		sync_main(ac, av);
	} else {
		errx(EX_USAGE, "bad ipfw3 command `%s'", *av);
	}
	return 0;
}

void
ipfw3_readfile(int ac, char *av[])
{
	char	buf[BUFSIZ];
	char	*a, *p, *args[MAX_ARGS], *cmd = NULL;
	char	linename[17];
	int	i=0, lineno=0, qflag=0, pflag=0, status;
	FILE	*f = NULL;
	pid_t	preproc = 0;
	int	c;

	while ((c = getopt(ac, av, "D:U:p:q")) != -1) {
		switch (c) {
		case 'D':
			if (!pflag)
				errx(EX_USAGE, "-D requires -p");
			if (i > MAX_ARGS - 2)
				errx(EX_USAGE, "too many -D or -U options");
			args[i++] = "-D";
			args[i++] = optarg;
			break;

		case 'U':
			if (!pflag)
				errx(EX_USAGE, "-U requires -p");
			if (i > MAX_ARGS - 2)
				errx(EX_USAGE, "too many -D or -U options");
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
		ipfw3_main(i, args);
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
	fw3_socket = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (fw3_socket < 0)
		err(EX_UNAVAILABLE, "socket");

	memset(keywords, 0, LEN_FW3_KEYWORD * KEYWORD_SIZE);
	memset(mappings, 0, LEN_FW3_MAPPING * MAPPING_SIZE);

	prepare_default_funcs();

	if (ac > 1 && av[ac - 1][0] == '/' && access(av[ac - 1], R_OK) == 0)
		ipfw3_readfile(ac, av);
	else
		ipfw3_main(ac, av);
	return EX_OK;
}
