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


void
nat_config_add(int ac, char **av)
{
	struct ioc_nat *ioc;
	struct in_addr *ip;
	int error, len = 0;
	char *id, buf[LEN_NAT_CMD_BUF];

	memset(buf, 0, LEN_NAT_CMD_BUF);
	ioc = (struct ioc_nat *)buf;

	NEXT_ARG;
	if (ac && isdigit(**av)) {
		id = *av;
		ioc->id = atoi(*av);
		if (ioc->id <= 0 || ioc->id > NAT_ID_MAX) {
			errx(EX_DATAERR, "invalid nat id");
		}
	} else {
		errx(EX_DATAERR, "missing nat id");
	}
	len += LEN_IOC_NAT;

	NEXT_ARG;
	if (strncmp(*av, "ip", strlen(*av))) {
		errx(EX_DATAERR, "missing `ip'");
	}
	NEXT_ARG;
	ip = &ioc->ip;
	while (ac > 0){
		if (!inet_aton(*av, ip)) {
			errx(EX_DATAERR, "bad ip addr `%s'", *av);
		}
		ioc->count++;
		len += LEN_IN_ADDR;
		ip++;
		NEXT_ARG;
	}

	error = do_set_x(IP_FW_NAT_ADD, ioc, len);
	if (error) {
		err(1, "do_set_x(%s)", "IP_FW_NAT_ADD");
	}

	/* show the rule after configured */
	int _ac = 2;
	char *_av[] = {"config", id};
	nat_config_get(_ac, _av);
}

void
nat_config_show(char *buf, int nbytes, int nat_id)
{
	struct ioc_nat *ioc;
	struct in_addr *ip;
	int n, len = 0;

	while (len < nbytes) {
		ioc = (struct ioc_nat *)(buf + len);
		if (nat_id == 0 || ioc->id == nat_id) {
			printf("ipfw3 nat %u config ip", ioc->id);
		}
		ip = &ioc->ip;
		len += LEN_IOC_NAT;
		for (n = 0; n < ioc->count; n++) {
			if (nat_id == 0 || ioc->id == nat_id) {
				printf(" %s", inet_ntoa(*ip));
			}
			ip++;
			len += LEN_IN_ADDR;
		}
		if (nat_id == 0 || ioc->id == nat_id) {
			printf("\n");
		}
	}
}

void
nat_config_get(int ac, char **av)
{
	int nbytes, nalloc;
	int nat_id;
	uint8_t *data;

	nalloc = 1024;
	data = NULL;
	nat_id = 0;

	NEXT_ARG;
	if (ac == 1) {
		nat_id = strtoul(*av, NULL, 10);
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
	nat_config_show(data, nbytes, nat_id);
}

void
nat_config_delete(int ac, char *av[])
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
nat_state_show(int ac, char **av)
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

	struct ioc_nat_state *ioc;
	ioc =(struct ioc_nat_state *)data;
	int count = nbytes / LEN_IOC_NAT_STATE;
	int i;
	for (i = 0; i < count; i ++) {
		printf("%d %d", ioc->nat_id, ioc->cpu_id);
		if (ioc->proto == IPPROTO_ICMP) {
			printf(" icmp");
		} else if (ioc->proto == IPPROTO_TCP) {
			printf(" tcp");
		} else if (ioc->proto == IPPROTO_UDP) {
			printf(" udp");
		}
		printf(" %s:%hu",inet_ntoa(ioc->src_addr),
			htons(ioc->src_port));
		printf(" %s:%hu",inet_ntoa(ioc->alias_addr),
			htons(ioc->alias_port));
		printf(" %s:%hu",inet_ntoa(ioc->dst_addr),
			htons(ioc->dst_port));
		printf(" %c", ioc->direction? 'o' : 'i');
		printf(" %lld", (long long)ioc->life);
		printf("\n");
		ioc++;
	}
}

void
nat_config_flush(void)
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
		nat_config_add(ac, av);
	} else if (!strncmp(*av, "flush", strlen(*av))) {
		nat_config_flush();
	} else if (!strncmp(*av, "show", strlen(*av))) {
		if (ac > 2 && isdigit(*(av[1]))) {
			char *p = av[1];
			av[1] = av[2];
			av[2] = p;
		}
		NEXT_ARG;
		if (!strncmp(*av, "config", strlen(*av))) {
			nat_config_get(ac, av);
		} else if (!strncmp(*av, "state", strlen(*av))) {
			nat_state_show(ac,av);
		} else {
			errx(EX_USAGE, "bad nat show command `%s'", *av);
		}
	} else if (!strncmp(*av, "delete", strlen(*av))) {
		nat_config_delete(ac, av);
	} else {
		errx(EX_USAGE, "bad ipfw nat command `%s'", *av);
	}
}
