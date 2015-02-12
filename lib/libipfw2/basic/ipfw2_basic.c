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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sysexits.h>
#include <timeconv.h>
#include <unistd.h>

#include <netinet/in.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>

#include "../../../sys/net/ipfw2/ip_fw3.h"
#include "../../../sbin/ipfw2/ipfw.h"
#include "ipfw2_basic.h"


#define	IP_MASK_ALL	0xffffffff
/*
 * we use IPPROTO_ETHERTYPE as a fake protocol id to call the print routines
 * This is only used in this code.
 */
#define IPPROTO_ETHERTYPE	0x1000


struct char_int_map limit_types[] = {
	{ "src-addr", 	1 },
	{ "src-port", 	2 },
	{ "dst-addr", 	3 },
	{ "dst-port", 	4 },
	{ NULL, 	0 }
};

static struct char_int_map ether_types[] = {
	{ "ip", 	0x0800 },
	{ "ipv4", 	0x0800 },
	{ "ipv6", 	0x86dd },
	{ "arp", 	0x0806 },
	{ "rarp", 	0x8035 },
	{ "vlan", 	0x8100 },
	{ "loop", 	0x9000 },
	{ "trail", 	0x1000 },
	{ "pppoe_disc", 0x8863 },
	{ "pppoe_sess", 0x8864 },
	{ "ipx_8022", 	0x00E0 },
	{ "ipx_8023", 	0x0000 },
	{ "ipx_ii", 	0x8137 },
	{ "ipx_snap", 	0x8137 },
	{ "ipx", 	0x8137 },
	{ "ns", 	0x0600 },
	{ NULL, 	0 }
};

/**
 * match_token takes a table and a string, returns the value associated
 * with the string (0 meaning an error in most cases)
 */
static int
match_token(struct char_int_map *table, char *string)
{
	while (table->key) {
		if (strcmp(table->key, string) == 0)
			return table->val;

		table++;
	}
	return 0;
};

static char *
match_token2(struct char_int_map *table, int val)
{
	while (table->val) {
		if (table->val == val)
			return table->key;

		table++;
	}
	return NULL;
};

static void
fill_iface(ipfw_insn_if *cmd, char *arg)
{
	cmd->name[0] = '\0';
	cmd->o.len |= F_INSN_SIZE(ipfw_insn_if);

	/* Parse the interface or address */
	if (!strcmp(arg, "any")){
		cmd->o.len = 0;
	} else if (!isdigit(*arg)) {
		strlcpy(cmd->name, arg, sizeof(cmd->name));
		cmd->p.glob = strpbrk(arg, "*?[") != NULL ? 1 : 0;
	} else if (!inet_aton(arg, &cmd->p.ip))
		errx(EX_DATAERR, "bad ip address ``%s''", arg);
}

static int
lookup_host (char *host, struct in_addr *ipaddr)
{
	struct hostent *he;

	if (!inet_aton(host, ipaddr)) {
		if ((he = gethostbyname(host)) == NULL)
			return(-1);
		*ipaddr = *(struct in_addr *)he->h_addr_list[0];
	}
	return(0);
}

/*
 * Like strtol, but also translates service names into port numbers
 * for some protocols.
 * In particular:
 *	proto == -1 disables the protocol check;
 *	proto == IPPROTO_ETHERTYPE looks up an internal table
 *	proto == <some value in /etc/protocols> matches the values there.
 * Returns *end == s in case the parameter is not found.
 */
static int
strtoport(char *s, char **end, int base, int proto)
{
	char *p, *buf;
	char *s1;
	int i;

	*end = s; 		/* default - not found */
	if ( *s == '\0')
		return 0; 	/* not found */

	if (isdigit(*s))
		return strtol(s, end, base);

	/*
	 * find separator. '\\' escapes the next char.
	 */
	for (s1 = s; *s1 && (isalnum(*s1) || *s1 == '\\') ; s1++) {
		if (*s1 == '\\' && s1[1] != '\0')
			s1++;
	}

	buf = malloc(s1 - s + 1);
	if (buf == NULL)
		return 0;

	/*
	 * copy into a buffer skipping backslashes
	 */
	for (p = s, i = 0; p != s1 ; p++)
		if ( *p != '\\')
			buf[i++] = *p;
	buf[i++] = '\0';

	if (proto == IPPROTO_ETHERTYPE) {
		i = match_token(ether_types, buf);
		free(buf);
		if (i != -1) {	/* found */
			*end = s1;
			return i;
		}
	} else {
		struct protoent *pe = NULL;
		struct servent *se;

		if (proto != 0)
			pe = getprotobynumber(proto);
		setservent(1);
		se = getservbyname(buf, pe ? pe->p_name : NULL);
		free(buf);
		if (se != NULL) {
			*end = s1;
			return ntohs(se->s_port);
		}
	}
	return 0; 	/* not found */
}

static int
contigmask(u_char *p, int len)
{
	int i, n;
	for (i=0; i<len ; i++) {
		if ( (p[i/8] & (1 << (7 - (i%8)))) == 0) /* first bit unset */
			break;
	}
	for (n=i+1; n < len; n++) {
		if ( (p[n/8] & (1 << (7 - (n%8)))) != 0)
			return -1; /* mask not contiguous */
	}
	return i;
}

static void
fill_ip(ipfw_insn_ip *cmd, char *av)
{
	char *p = NULL, md = 0;
	u_int32_t i;

	cmd->o.len &= ~F_LEN_MASK; 	/* zero len */

	if (!strncmp(av, "any", strlen(av)))
		return;

	if (!strncmp(av, "me", strlen(av))) {
		cmd->o.len |= F_INSN_SIZE(ipfw_insn);
		return;
	}

	p = strchr(av, '/');
	if (!p)
		p = strchr(av, ':');

	if (p) {
		md = *p;
		*p++ = '\0';
	}

	if (lookup_host(av, &cmd->addr) != 0)
		errx(EX_NOHOST, "hostname ``%s'' unknown", av);
	switch (md) {
	case ':':
		if (!inet_aton(p, &cmd->mask))
			errx(EX_DATAERR, "bad netmask ``%s''", p);
		break;
	case '/':
		i = atoi(p);
		if (i == 0)
			cmd->mask.s_addr = htonl(0);
		else if (i > 32)
			errx(EX_DATAERR, "bad width ``%s''", p);
		else
			cmd->mask.s_addr = htonl(~0 << (32 - i));
		break;
	default:
		cmd->mask.s_addr = htonl(~0);
		break;
	}
	cmd->addr.s_addr &= cmd->mask.s_addr;

	if (p)
		p = strchr(p, '{');
	if (p) {
		u_int32_t *d;
		int low, high;
		int i = contigmask((u_char *)&(cmd->mask), 32);

		if (i < 24 || i > 31) {
			fprintf(stderr, "invalid set with mask %d\n", i);
			exit(0);
		}
		cmd->o.arg1 = 1<<(32-i);
		cmd->addr.s_addr = ntohl(cmd->addr.s_addr);
		d = (u_int32_t *)&cmd->mask;
		cmd->o.opcode = O_BASIC_IP_DST_SET; 	/* default */
		cmd->o.len |= F_INSN_SIZE(ipfw_insn_u32) + (cmd->o.arg1+31)/32;
		for (i = 0; i < (cmd->o.arg1+31)/32 ; i++)
			d[i] = 0; 	/* clear masks */

		av = p+1;
		low = cmd->addr.s_addr & 0xff;
		high = low + cmd->o.arg1 - 1;
		while (isdigit(*av)) {
			char *s;
			u_int16_t a = strtol(av, &s, 0);

			if (s == av) /* no parameter */
				break;
			if (a < low || a > high) {
				fprintf(stderr,
					"addr %d out of range [%d-%d]\n",
						a, low, high);
				exit(0);
			}
			a -= low;
			d[ a/32] |= 1<<(a & 31);
			if (*s != ',')
				break;
			av = s+1;
		}
		return;
	}

	if (cmd->mask.s_addr == 0) {
		if (cmd->o.len & F_NOT)
			errx(EX_DATAERR, "not any never matches");
		else	/* useless, nuke it */
			return;
	} else if (cmd->mask.s_addr == IP_MASK_ALL) {
		cmd->o.len |= F_INSN_SIZE(ipfw_insn_u32);
	} else {						/* addr/mask */
		cmd->o.len |= F_INSN_SIZE(ipfw_insn_ip);
		printf("cmd->o.len=%d\n", cmd->o.len);
	}
}


static ipfw_insn *add_srcip(ipfw_insn *cmd, char *av)
{
	fill_ip((ipfw_insn_ip *)cmd, av);
	if ((int)cmd->opcode == O_BASIC_IP_DST_SET)		/* set */
		cmd->opcode = O_BASIC_IP_SRC_SET;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn))		/* me */
		cmd->opcode = O_BASIC_IP_SRC_ME;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_u32))	/* one IP */
		cmd->opcode = O_BASIC_IP_SRC;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_ip))	/* addr/mask */
		cmd->opcode = O_BASIC_IP_SRC_MASK;
	return cmd;
}


static ipfw_insn *add_dstip(ipfw_insn *cmd, char *av)
{
	fill_ip((ipfw_insn_ip *)cmd, av);
	if ((int)cmd->opcode == O_BASIC_IP_DST_SET)
		cmd->opcode = O_BASIC_IP_DST_SET;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn))
		cmd->opcode = O_BASIC_IP_DST_ME;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_u32))
		cmd->opcode = O_BASIC_IP_DST;
	else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn_ip))
		cmd->opcode = O_BASIC_IP_DST_MASK;
	return cmd;
}


static ipfw_insn *add_proto(ipfw_insn *cmd, char *av)
{
	struct protoent *pe;
	u_char proto = 0;
	if (!strncmp(av, "all", strlen(av))) {
		;
	} else if ((proto = atoi(av)) > 0) {
		;
	} else if ((pe = getprotobyname(av)) != NULL) {
		proto = pe->p_proto;
	} else {
		errx(EX_USAGE, "protocol `%s' not recognizable\n", av);
	}
	if (proto != IPPROTO_IP) {
		cmd->opcode = O_BASIC_PROTO;
		cmd->module = MODULE_BASIC_ID;
		cmd->len = cmd->len|LEN_OF_IPFWINSN;
		cmd->arg1 = proto;
	}
	return cmd;
}

void
parse_count(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_BASIC_COUNT;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	NEXT_ARG1;
}

void
parse_skipto(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_SKIPTO;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

/*
 * cmd->arg3 is count of the destination
 * cmd->arg1 is the type, random 0, round-robin 1, sticky 2
 */
void
parse_forward(ipfw_insn **cmd, int *ac, char **av[])
{
	ipfw_insn_sa *p = (ipfw_insn_sa *)(*cmd);
	struct sockaddr_in *sa;
	char *tok, *end = '\0';
	char *str;
	int count, port;

	(*cmd)->opcode = O_BASIC_FORWARD;
	NEXT_ARG1;
	/*
	 * multiple forward destinations are seperated by colon
	 * ip address and port are seperated by comma
	 * e.g. 192.168.1.1:80,192.168.1.2:8080
	 *      192.168.1.1,192.168.1.2 or keep the port the same
	 */
	tok = strtok(**av, ",");
	sa = &p->sa;
	count = 0;
	while (tok != NULL) {
		sa->sin_len = sizeof(struct sockaddr_in);
		sa->sin_family = AF_INET;
		sa->sin_port = 0;
		str = strchr(tok,':');
		if (str != NULL) {
			*(str++) = '\0';
			port = strtoport(str, &end, 0, 0);
			sa->sin_port = (u_short)port;
		}
		lookup_host(tok, &(sa->sin_addr));
		tok = strtok (NULL, ",");
		sa++;
		count++;
	}
	(*cmd)->arg3 = count;
	if (count == 0) {
		errx(EX_DATAERR, "forward `%s' not recognizable", **av);
	}
	NEXT_ARG1;
	if (count > 1) {
		if (strcmp(**av, "round-robin") == 0) {
			NEXT_ARG1;
			(*cmd)->arg1 = 1;
		} else if (strcmp(**av, "sticky") == 0) {
			NEXT_ARG1;
			(*cmd)->arg1 = 2;
		} else {
			/* random */
			(*cmd)->arg1 = 0;
		}
	}
	(*cmd)->len = LEN_OF_IPFWINSN + count * sizeof(struct sockaddr_in);
}

void
parse_in(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_BASIC_IN;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = 0;
	NEXT_ARG1;
}

void
parse_out(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_BASIC_OUT;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = 0;
	NEXT_ARG1;
}


void
parse_via(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	if (strcmp(*av[0], "via")==0) {
		(*cmd)->opcode = O_BASIC_VIA;
	} else if (strcmp(*av[0], "xmit")==0) {
		(*cmd)->opcode = O_BASIC_XMIT;
	} else if (strcmp(*av[0], "recv")==0) {
		(*cmd)->opcode = O_BASIC_RECV;
	}
	NEXT_ARG1;
	fill_iface((ipfw_insn_if *)(*cmd), *av[0]);
	NEXT_ARG1;
}

void
parse_from(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->module = MODULE_BASIC_ID;
	NEXT_ARG1;
	add_srcip(*cmd, **av);
	NEXT_ARG1;
}

void
parse_to(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->module = MODULE_BASIC_ID;
	NEXT_ARG1;
	add_dstip(*cmd, **av);
	NEXT_ARG1;
}

void
parse_proto(ipfw_insn **cmd, int *ac, char **av[])
{
	add_proto(*cmd, **av);
	NEXT_ARG1;
}

void
parse_prob(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_PROB;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

void
parse_keep_state(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_KEEP_STATE;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	if (strcmp(**av, "limit") == 0) {
		NEXT_ARG1;
		(*cmd)->arg3 = match_token(limit_types, **av);
		if ((*cmd)->arg3 == 0)
			errx(EX_DATAERR, "limit `%s' not recognizable", **av);

		NEXT_ARG1;
		(*cmd)->arg1 = strtoul(**av, NULL, 10);
		if ((*cmd)->arg1 == 0)
			errx(EX_DATAERR, "bad limit `%s'", **av);

		NEXT_ARG1;
	}
	if (strcmp(**av, "live") == 0) {
		NEXT_ARG1;
		(*cmd)->arg2 = strtoul(**av, NULL, 10);
		NEXT_ARG1;
	}
}

void
parse_check_state(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_CHECK_STATE;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
}

void
parse_tagged(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_TAGGED;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

void
parse_comment(ipfw_insn **cmd, int *ac, char **av[])
{
	int l = 0;
	char *p = (char *)((*cmd) + 1);

	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_COMMENT;
	(*cmd)->module = MODULE_BASIC_ID;

	while (*ac > 0) {
		l += strlen(**av) + 1;
		if (l > 84) {
			errx(EX_DATAERR, "comment too long (max 80 chars)");
		}
		strcpy(p, **av);
		p += strlen(**av);
		*p++ = ' ';
		NEXT_ARG1;
	}
	l = 1 + (l + 3) / 4;
	(*cmd)->len = l;
	*(--p) = '\0';
}

void
parse_tag(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_TAG;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

void
parse_untag(ipfw_insn **cmd, int *ac, char **av[])
{
	NEXT_ARG1;
	(*cmd)->opcode = O_BASIC_UNTAG;
	(*cmd)->module = MODULE_BASIC_ID;
	(*cmd)->len = LEN_OF_IPFWINSN;
	(*cmd)->arg1 = strtoul(**av, NULL, 10);
	NEXT_ARG1;
}

void
show_count(ipfw_insn *cmd)
{
	printf(" count");
}

void
show_skipto(ipfw_insn *cmd)
{
	printf(" skipto %u", cmd->arg1);
}

void
show_forward(ipfw_insn *cmd)
{
	struct sockaddr_in *sa;
	int i;

	ipfw_insn_sa *s = (ipfw_insn_sa *)cmd;
	sa = &s->sa;
	printf(" forward");
	for (i = 0; i < cmd->arg3; i++){
		if (i > 0)
			printf(",");
		else
			printf(" ");

		printf("%s", inet_ntoa(sa->sin_addr));
		if (sa->sin_port != 0)
			printf(":%d", sa->sin_port);

		sa++;
	}
	if (cmd->arg1 == 1)
		printf(" round-robin");
	else if (cmd->arg1 == 2)
		printf(" sticky");

}

void
show_in(ipfw_insn *cmd)
{
	printf(" in");
}

void
show_out(ipfw_insn *cmd)
{
	printf(" out");
}

void
show_via(ipfw_insn *cmd)
{
	char *s;
	ipfw_insn_if *cmdif = (ipfw_insn_if *)cmd;

	if ((int)cmd->opcode == O_BASIC_XMIT)
		s = "xmit";
	else if ((int)cmd->opcode == O_BASIC_RECV)
		s = "recv";
	else if ((int)cmd->opcode == O_BASIC_VIA)
		s = "via";
	else
		s = "?huh?";

	if (cmdif->name[0] == '\0')
		printf(" %s %s", s, inet_ntoa(cmdif->p.ip));

	printf(" %s %s", s, cmdif->name);
}

void
show_from(ipfw_insn *cmd)
{
	printf(" from %s", inet_ntoa(((ipfw_insn_ip *)cmd)->addr));
}

void
show_to(ipfw_insn *cmd)
{
	printf(" to %s", inet_ntoa(((ipfw_insn_ip *)cmd)->addr));
}

void
show_proto(ipfw_insn *cmd)
{
	struct protoent *pe;
	u_char proto = 0;
	proto = cmd->arg1;
	pe = getprotobynumber(cmd->arg1);
	printf(" %s", pe->p_name);
}

void
show_prob(ipfw_insn *cmd)
{
	printf(" prob %d%%", cmd->arg1);
}

void
show_keep_state(ipfw_insn *cmd)
{
	printf(" keep-state");
	if (cmd->arg1 != 0) {
		char *type=match_token2(limit_types, cmd->arg3);
		printf(" limit %s %d", type, cmd->arg1);
	}
	if (cmd->arg2 != 0) {
		printf(" live %d", cmd->arg2);
	}
}

void
show_check_state(ipfw_insn *cmd)
{
	printf(" check-state");
}

void
show_tagged(ipfw_insn *cmd)
{
	printf(" tagged %d", cmd->arg1);
}

void
show_comment(ipfw_insn *cmd)
{
	printf(" // %s", (char *)(cmd + 1));
}

void
show_tag(ipfw_insn *cmd)
{
	printf(" tag %d", cmd->arg1);
}

void
show_untag(ipfw_insn *cmd)
{
	printf(" untag %d", cmd->arg1);
}

void
load_module(register_func function, register_keyword keyword)
{
	keyword(MODULE_BASIC_ID, O_BASIC_COUNT, "count",
			IPFW_KEYWORD_TYPE_ACTION);
	function(MODULE_BASIC_ID, O_BASIC_COUNT,
			(parser_func)parse_count, (shower_func)show_count);

	keyword(MODULE_BASIC_ID, O_BASIC_SKIPTO, "skipto",
			IPFW_KEYWORD_TYPE_ACTION);
	function(MODULE_BASIC_ID, O_BASIC_SKIPTO,
			(parser_func)parse_skipto, (shower_func)show_skipto);

	keyword(MODULE_BASIC_ID, O_BASIC_FORWARD, "forward",
			IPFW_KEYWORD_TYPE_ACTION);
	function(MODULE_BASIC_ID, O_BASIC_FORWARD,
			(parser_func)parse_forward, (shower_func)show_forward);

	keyword(MODULE_BASIC_ID, O_BASIC_IN, "in", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_IN,
			(parser_func)parse_in, (shower_func)show_in);

	keyword(MODULE_BASIC_ID, O_BASIC_OUT, "out", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_OUT,
			(parser_func)parse_out, (shower_func)show_out);

	keyword(MODULE_BASIC_ID, O_BASIC_VIA, "via", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_VIA,
			(parser_func)parse_via, (shower_func)show_via);

	keyword(MODULE_BASIC_ID, O_BASIC_XMIT, "xmit",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_XMIT,
			(parser_func)parse_via, (shower_func)show_via);

	keyword(MODULE_BASIC_ID, O_BASIC_RECV, "recv",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_RECV,
			(parser_func)parse_via, (shower_func)show_via);

	keyword(MODULE_BASIC_ID, O_BASIC_IP_SRC, "from",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_IP_SRC,
			(parser_func)parse_from, (shower_func)show_from);

	keyword(MODULE_BASIC_ID, O_BASIC_IP_DST, "to",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_IP_DST,
			(parser_func)parse_to, (shower_func)show_to);

	keyword(MODULE_BASIC_ID, O_BASIC_PROTO, "proto",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_PROTO,
			(parser_func)parse_proto, (shower_func)show_proto);

	keyword(MODULE_BASIC_ID, O_BASIC_PROB, "prob",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_PROB,
			(parser_func)parse_prob, (shower_func)show_prob);

	keyword(MODULE_BASIC_ID, O_BASIC_KEEP_STATE, "keep-state",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_KEEP_STATE,
			(parser_func)parse_keep_state,
			(shower_func)show_keep_state);

	keyword(MODULE_BASIC_ID, O_BASIC_CHECK_STATE, "check-state",
			IPFW_KEYWORD_TYPE_OTHERS);
	function(MODULE_BASIC_ID, O_BASIC_CHECK_STATE,
			(parser_func)parse_check_state,
			(shower_func)show_check_state);

	keyword(MODULE_BASIC_ID, O_BASIC_TAG, "tag", IPFW_KEYWORD_TYPE_OTHERS);
	function(MODULE_BASIC_ID, O_BASIC_TAG,
			(parser_func)parse_tag, (shower_func)show_tag);

	keyword(MODULE_BASIC_ID, O_BASIC_UNTAG, "untag",
			IPFW_KEYWORD_TYPE_OTHERS);
	function(MODULE_BASIC_ID, O_BASIC_UNTAG,
			(parser_func)parse_untag, (shower_func)show_untag);

	keyword(MODULE_BASIC_ID, O_BASIC_TAGGED, "tagged",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_TAGGED,
			(parser_func)parse_tagged, (shower_func)show_tagged);

	keyword(MODULE_BASIC_ID, O_BASIC_COMMENT, "//",
			IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_BASIC_ID, O_BASIC_COMMENT,
			(parser_func)parse_comment, (shower_func)show_comment);
}
