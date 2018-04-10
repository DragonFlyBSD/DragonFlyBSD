/*
 * Copyright (c) 2014 - 2018 The DragonFly Project.  All rights reserved.
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
#include <net/ipfw3/ip_fw3_table.h>
#include <net/ipfw3/ip_fw3_sync.h>
#include <net/dummynet3/ip_dummynet3.h>
#include <net/ipfw3_basic/ip_fw3_basic.h>
#include <net/ipfw3_nat/ip_fw3_nat.h>

#include "ipfw3.h"
#include "ipfw3nat.h"

extern int verbose;
extern int do_time;
extern int do_quiet;
extern int do_force;

struct char_int_map nat_params[] = {
	{ "ip", 		TOK_IP },
	{ NULL, 0 },
};


void
nat_config(int ac, char **av)
{
	struct ioc_nat *ioc;
	int error, tok;
	char *id, buf[NAT_BUF_LEN];

	memset(buf, 0, NAT_BUF_LEN);
	ioc = (struct ioc_nat *)buf;

	NEXT_ARG;
	/* Nat id. */
	if (ac && isdigit(**av)) {
		id = *av;
		ioc->id = atoi(*av);
		if (ioc->id <= 0 || ioc->id > NAT_ID_MAX) {
			errx(EX_DATAERR, "invalid nat id");
		}
		NEXT_ARG;
	} else {
		errx(EX_DATAERR, "missing nat id");
	}

	if (ac == 0)
		errx(EX_DATAERR, "missing option");

	while (ac > 0) {
		tok = match_token(nat_params, *av);
		NEXT_ARG;
		switch (tok) {
		case TOK_IP:
			if (ac == 0)
				errx(EX_DATAERR, "missing option");
			if (!inet_aton(av[0], &(ioc->ip)))
				errx(EX_DATAERR, "bad ip addr `%s'", av[0]);
			NEXT_ARG;
			break;
		default:
			errx(EX_DATAERR, "unrecognised option ``%s''", av[-1]);
		}
	}
	error = do_set_x(IP_FW_NAT_ADD, buf, LEN_IOC_NAT);
	if (error) {
		if (errno == 1) {
			errx(EX_DATAERR, "nat %s is exists", id);
		} else {
			err(1, "do_set_x(%s)", "IP_FW_NAT_ADD");
		}
	}

	/* After every modification, we show the resultant rule. */
	int _ac = 2;
	char *_av[] = {"config", id};
	nat_show(_ac, _av);
}

void
nat_show_config(struct ioc_nat *ioc)
{
	printf("ipfw3 nat %u config", ioc->id);
	if (ioc->ip.s_addr != 0)
		printf(" ip %s", inet_ntoa(ioc->ip));
	printf("\n");
}

void
nat_show(int ac, char **av)
{
	struct ioc_nat *ioc;
	int nbytes, nalloc, size, len;
	int id, all;
	uint8_t *data;

	nalloc = 1024;
	size = 0;
	data = NULL;
	id = 0;
	all = 0;

	NEXT_ARG;
	if (ac == 0) {
		all = 1;
	} else {
		id = strtoul(*av, NULL, 10);
	}

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

	len = 0;
	ioc = (struct ioc_nat *)data;
	while (len < nbytes) {
		if (all == 1 || ioc->id == id) {
			nat_show_config(ioc);
		}
		len += LEN_IOC_NAT;
		ioc++;
	}
}

int
str2proto(const char* str)
{
	if (!strcmp (str, "tcp"))
		return IPPROTO_TCP;
	if (!strcmp (str, "udp"))
		return IPPROTO_UDP;
	errx (EX_DATAERR, "unknown protocol %s. Expected tcp or udp", str);
}

void
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

void
nat_delete_config(int ac, char *av[])
{
	NEXT_ARG;
	int i = 0;
	if (ac > 0) {
		i = atoi(*av);
	}
	if (do_set_x(IP_FW_NAT_DEL, &i, sizeof(i)) == -1)
		errx(EX_USAGE, "NAT %d in use or not exists", i);
}

void
nat_show_state(int ac, char **av)
{
	int nbytes, nalloc;
	int nat_id;
	uint8_t *data;

	nalloc = 1024;
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
		memcpy(data, &nat_id, sizeof(int));
		if (do_get_x(IP_FW_NAT_GET_RECORD, data, &nbytes) < 0) {
			err(EX_OSERR, "do_get_x(IP_FW_NAT_GET_RECORD)");
		}
	}
	if (nbytes == 0)
		exit(EX_OK);
	struct ipfw_ioc_nat_state *nat_state;
	nat_state =(struct ipfw_ioc_nat_state *)data;
	int count = nbytes / sizeof( struct ipfw_ioc_nat_state);
	int i, uptime_sec;
	uptime_sec = get_kern_boottime();
	for (i = 0; i < count; i ++) {
		printf("#%d ", nat_state->cpuid);
		printf("%s:%hu => ",inet_ntoa(nat_state->src_addr),
				htons(nat_state->src_port));
		printf("%s:%hu",inet_ntoa(nat_state->alias_addr),
				htons(nat_state->alias_port));
		printf(" -> %s:%hu ",inet_ntoa(nat_state->dst_addr),
				htons(nat_state->dst_port));
		if (do_time == 1) {
			char timestr[30];
			time_t t = _long_to_time(uptime_sec +
					nat_state->timestamp);
			strcpy(timestr, ctime(&t));
			*strchr(timestr, '\n') = '\0';
			printf("%s ", timestr);
		} else if (do_time == 2) {
			printf( "%10u ", uptime_sec + nat_state->timestamp);
		}
		struct protoent *pe = getprotobynumber(nat_state->link_type);
		printf("%s ", pe->p_name);
		printf(" %s", nat_state->is_outgoing? "out": "in");
		printf("\n");
		nat_state++;
	}
}

int
get_kern_boottime(void)
{
	struct timeval boottime;
	size_t size;
	int mib[2];
	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;
	size = sizeof(boottime);
	if (sysctl(mib, 2, &boottime, &size, NULL, 0) != -1 &&
			boottime.tv_sec != 0) {
		return boottime.tv_sec;
	}
	return -1;
}

void
nat_flush(void)
{
	int cmd = IP_FW_NAT_FLUSH;
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
		errx(EX_USAGE, "NAT configuration in use");
	}
	if (!do_quiet) {
		printf("Flushed all nat configurations");
	}
}

void
nat_main(int ac, char **av)
{
	if (!strncmp(*av, "config", strlen(*av))) {
		nat_config(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		nat_flush();
	} else if (!strncmp(*av, "show", strlen(*av)) ||
			!strncmp(*av, "list", strlen(*av))) {
		if (ac > 2 && isdigit(*(av[1]))) {
			char *p = av[1];
			av[1] = av[2];
			av[2] = p;
		}
		NEXT_ARG;
		if (!strncmp(*av, "config", strlen(*av))) {
			nat_show(ac, av);
		} else if (!strncmp(*av, "state", strlen(*av))) {
			nat_show_state(ac,av);
		} else {
			errx(EX_USAGE,
					"bad nat show command `%s'", *av);
		}
	} else if (!strncmp(*av, "delete", strlen(*av))) {
		nat_delete_config(ac, av);
	} else {
		errx(EX_USAGE, "bad ipfw nat command `%s'", *av);
	}
}
