/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sbin/syslink/syslink.c,v 1.1 2007/04/16 17:36:04 dillon Exp $
 */

#include "syslink.h"

enum cmd { CMD_NONE, CMD_LIST, CMD_ADD, CMD_DEL, CMD_MOD };

static int parse_add(const char *base);
static int run_cmd(enum cmd commandopt);
static void usage(const char *av0);

int ForceOpt;
int NumericOpt;
int VerboseOpt;
const char *SysId;
const char *LinkId;
const char *LabelStr;
enum proto Protocol;	/* filled in by parse_add() */
int NumBits;
int TargetFd = -1;
const char *TargetPath;
struct sockaddr_in TargetSin;

int
main(int ac, char **av)
{
	const char *pidfile = NULL;
	const char *av0 = av[0];
	char *ptr;
	enum cmd commandopt;
	int ch;
	int i;

	commandopt = CMD_NONE;

	while ((ch = getopt(ac, av, "fnlvp:")) != -1) {
		switch(ch) {
		case 'f':
			++ForceOpt;
			break;
		case 'n':
			++NumericOpt;
			break;
		case 'l':
			commandopt = CMD_LIST;
			break;
		case 'v':
			++VerboseOpt;
			break;
		case 'p':
			pidfile = optarg;
			break;
		default:
			fprintf(stderr, "unknown option: -%c\n", optopt);
			usage(av0);
		}
	}
	ac -= optind;
	av += optind;

	/*
	 * -l with no arguments dumps all syslink routers.  This is the
	 * only command that does not require further arguments.
	 */
	if (commandopt == CMD_LIST && ac == 0)
		exit(run_cmd(commandopt));
	if (ac == 0)
		usage(av0);

	/*
	 * Parse sysid[:linkid]
	 */
	ptr = strdup(av[0]);
	SysId = ptr;
	if ((ptr = strchr(ptr, ':')) != NULL) {
		*ptr++ = 0;
		LinkId = ptr;
	}
	--ac;
	++av;

	/*
	 * Handle options that are actually commands (-l only at the moment).
	 * There should be no more arguments if we have a command-as-option.
	 */
	if (commandopt != CMD_NONE) {
		if (ac)
			usage(av0);
		exit(run_cmd(commandopt));
	}

	/*
	 * Parse keyword commands, set commandopt as an earmark.
	 */
	if (ac == 0) {
		fprintf(stderr, "Missing command directive\n");
		usage(av0);
	}
	--ac;
	++av;

	if (strcmp(av[-1], "add") == 0) {
		/*
		 * add [protocol:]target[/bits]
		 */
		commandopt = CMD_ADD;
		if (ac == 0)
			usage(av0);
		if (parse_add(av[0]))
			usage(av0);
		--ac;
		++av;
	} else if (strcmp(av[-1], "del") == 0) {
		commandopt = CMD_DEL;
	} else if (strcmp(av[-1], "delete") == 0) {
		commandopt = CMD_DEL;
	} else if (strcmp(av[-1], "mod") == 0) {
		commandopt = CMD_MOD;
	} else if (strcmp(av[-1], "modify") == 0) {
		commandopt = CMD_MOD;
	} else {
		fprintf(stderr, "Unknown command directive: %s\n", av[-1]);
		usage(av0);
	}

	/*
	 * Parse supplementary info
	 */
	for (i = 0; i < ac; ++i) {
		if (strcmp(av[i], "label") == 0) {
			LabelStr = av[i+1];
			++i;
		} else if (strcmp(av[i], "port") == 0) {
			ptr = av[i+1];
			TargetSin.sin_port = htons(strtol(ptr, &ptr, 0));
			if (*ptr) {
				fprintf(stderr, "Non-numeric port specified\n");
				usage(av0);
			}
			++i;
		} else {
			fprintf(stderr, "Unknown directive: %s\n", av[i]);
			usage(av0);
		}
	}
	if (i > ac) {
		fprintf(stderr, "Expected argument for last directive\n");
		usage(av0);
	}

	exit(run_cmd(commandopt));
}

/*
 * Parse [protocol:]target[/bits]
 */
static
int
parse_add(const char *base)
{
	char *prot_str;
	char *targ_str;
	char *bits_str;
	char *ptr;
	struct hostent *he;

	/*
	 * Split out the protocol from the protocol:target/subnet string,
	 * leave target/subnet in targ_str.
	 */
	if (strchr(base, ':')) {
		prot_str = strdup(base);
		targ_str = strchr(prot_str, ':');
		*targ_str++ = 0;
	} else {
		prot_str = NULL;
		targ_str = strdup(base);
	}

	/*
	 * Parse the /subnet out of the target string, if present.
	 */
	if ((bits_str = strchr(targ_str, '/')) != NULL) {
		*bits_str++ = 0;
		NumBits = strtol(bits_str, &ptr, NULL);
		if (*ptr) {
			fprintf(stderr, "Malformed /subnet\n");
			return(-1);
		}
		if (NumBits < 2 || NumBits > 24) {
			fprintf(stderr, "Subnet must be 2-24 bits\n");
			return(-1);
		}
	}

	/*
	 * Figure out the protocol
	 */
	if (prot_str == NULL) {
		if (bits_str)
			Protocol = PROTO_TCP;
		else
			Protocol = PROTO_UDP;
	} else if (strcmp(prot_str, "tcp") == 0) {
		Protocol = PROTO_TCP;
	} else if (strcmp(prot_str, "udp") == 0) {
		Protocol = PROTO_UDP;
	} else if (strcmp(prot_str, "udp_ptp") == 0) {
		Protocol = PROTO_UDP_PTP;
	} else if (strcmp(prot_str, "pipe") == 0) {
		Protocol = PROTO_PIPE;
	} else if (strcmp(prot_str, "fifo") == 0) {
		Protocol = PROTO_FIFO;
	} else if (strcmp(prot_str, "listen") == 0) {
		Protocol = PROTO_LISTEN;
	} else {
		fprintf(stderr, "Unknown protocol: %s\n", prot_str);
		return(-1);
	}

	/*
	 * Process the host, file, or descriptor specification
	 */
	switch(Protocol) {
	case PROTO_TCP:
	case PROTO_UDP:
	case PROTO_UDP_PTP:
	case PROTO_LISTEN:
		TargetSin.sin_len = sizeof(TargetSin);
		TargetSin.sin_family = AF_INET;
		if (inet_aton(targ_str, &TargetSin.sin_addr) != 0) {
		} else if ((he = gethostbyname2(targ_str, AF_INET)) != NULL) {
			bcopy(he->h_addr, &TargetSin.sin_addr, he->h_length);
		} else {
			fprintf(stderr, "Cannot resolve target %s\n", targ_str);
			return(-1);
		}
		break;
	case PROTO_PIPE:
		TargetFd = strtol(targ_str, &ptr, 0);
		if (*ptr) {
			fprintf(stderr, "non-numeric file descriptor "
					"number in target\n");
			return(-1);
		}
		break;
	case PROTO_FIFO:
		TargetPath = targ_str;
		break;
	}
	return(0);
}

static
int
run_cmd(enum cmd commandopt)
{
	int exitcode = 0;

	/*
	 * Run the command
	 */
	switch(commandopt) {
	case CMD_NONE:
		break;
	case CMD_LIST:
		printf("list\n");
		break;
	case CMD_ADD:
		printf("add\n");
		break;
	case CMD_DEL:
		printf("del\n");
		break;
	case CMD_MOD:
		printf("mod\n");
		break;
	}
	return(exitcode);
}

static
void
usage(const char *av0)
{
	fprintf(stderr, 
	    "syslink -l [-nv] [sysid[:linkid]]\n"
	    "syslink [-fnv] [-p pidfile] sysid add [protocol:]target[/bits]\n"
	    "        [label name] [port num]\n"
	    "syslink [-fnv] sysid[:linkid] delete\n"
	    "syslink [-fnv] sysid[:linkid] modify\n"
	    "        [label name] [port num]\n"
	);
	exit(1);
}

