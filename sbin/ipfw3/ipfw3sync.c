/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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

#include "../../sys/net/ipfw3/ip_fw3.h"
#include "../../sys/net/ipfw3/ip_fw3_table.h"
#include "../../sys/net/ipfw3/ip_fw3_sync.h"
#include "../../sys/net/dummynet3/ip_dummynet3.h"
#include "../../sys/net/libalias/alias.h"
#include "../../sys/net/ipfw3_basic/ip_fw3_basic.h"
#include "../../sys/net/ipfw3_nat/ip_fw3_nat.h"

#include "ipfw3.h"
#include "ipfw3sync.h"

void
sync_config_edge(int ac, char *av[])
{
	struct ipfw_ioc_sync_edge ioc_edge;
	NEXT_ARG;
	if (isdigit(**av)) {
		ioc_edge.port = atoi(*av);
		if (ioc_edge.port == 0) {
			errx(EX_USAGE, "invalid edge port `%s'", *av);
		}
		NEXT_ARG;
		if (strcmp(*av, "all") == 0) {
			ioc_edge.hw_same = 1;
		} else {
			ioc_edge.hw_same = 0;
		}
		if(do_set_x(IP_FW_SYNC_EDGE_CONF,
				&ioc_edge, sizeof(ioc_edge)) < 0) {
			err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_EDGE_CONF)");
		}
	} else {
		errx(EX_USAGE, "invalid edge port `%s'", *av);
	}
}

void
sync_config_centre(int ac, char *av[])
{
	struct ipfw_ioc_sync_centre *centre;
	struct ipfw_sync_edge *edge;
	struct in_addr addr;
	char *tok;
	char *str;
	int count = 0, step = 10, len, data_len;

	void *data = NULL;

	NEXT_ARG;
	tok = strtok(*av, ",");
	len = sizeof(int);

	data_len = len + step * sizeof(struct ipfw_sync_edge);
	data = malloc(data_len);
	centre = (struct ipfw_ioc_sync_centre *)data;
	edge = centre->edges;
	while (tok != NULL) {
		str = strchr(tok,':');
		if (str != NULL) {
			*(str++) = '\0';
			edge->port = (u_short)strtoul(str, NULL, 0);
			if (edge->port == 0) {
				errx(EX_USAGE, "edge `%s:%s' invalid",
						tok, str);
			}
		} else {
			err(EX_UNAVAILABLE, "dst invalid");
		}
		inet_aton(tok, &addr);
		edge->addr = addr.s_addr;
		if (count >= step) {
			step += 10;
			data_len = len + step * sizeof(struct ipfw_sync_edge);
			if ((data = realloc(data, data_len)) == NULL) {
				err(EX_OSERR, "realloc in config sync centre");
			}
		}

		tok = strtok (NULL, ",");
		count++;
		edge++;
	}
	if (count > MAX_EDGES) {
		err(EX_OSERR,"too much edges");
	}
	centre->count = count;
	len += count * sizeof(struct ipfw_sync_edge);
	if(do_set_x(IP_FW_SYNC_CENTRE_CONF, data, len) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_CENTRE_CONF)");
	}

}

void
sync_show_config(int ac, char *av[])
{
	void *data = NULL;
	int nalloc = 1000, nbytes;
	nbytes = nalloc;

	while (nbytes >= nalloc) {
		nalloc = nalloc * 2 + 321;
		nbytes = nalloc;
		if (data == NULL) {
			if ((data = malloc(nbytes)) == NULL) {
				err(EX_OSERR, "malloc");
			}
		} else if ((data = realloc(data, nbytes)) == NULL) {
			err(EX_OSERR, "realloc");
		}
		if (do_get_x(IP_FW_SYNC_SHOW_CONF, data, &nbytes) < 0) {
			err(EX_OSERR, "getsockopt(IP_FW_SYNC_SHOW_CONF)");
		}
	}
	struct ipfw_ioc_sync_context *sync_ctx;
	sync_ctx = (struct ipfw_ioc_sync_context *)data;
	if (sync_ctx->edge_port != 0) {
		printf("ipfw3sync edge on %d %s\n", sync_ctx->edge_port,
				sync_ctx->hw_same == 1 ? "all" : "");
	}
	if (sync_ctx->count > 0) {
		struct ipfw_sync_edge *edge;
		int i;

		edge = sync_ctx->edges;
		printf("ipfw3sync centre to %d edge(s)\n", sync_ctx->count);
		for (i = 0; i < sync_ctx->count; i++) {
			struct in_addr in;
			in.s_addr = edge->addr;
			printf("edge on %s:%d\n", inet_ntoa(in), edge->port);
			edge++;
		}
	}

}

void
sync_show_status(int ac, char *av[])
{
	int running, len;
	len = sizeof(running);
	if (do_get_x(IP_FW_SYNC_SHOW_STATUS, &running, &len) < 0) {
		err(EX_OSERR, "getsockopt(IP_FW_SYNC_SHOW_STATUS)");
	}
	if (running & 1) {
		printf("edge is running\n");
	}
	if (running & 2) {
		printf("centre is running\n");
	}
}

void
sync_edge_start(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_EDGE_START, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_EDGE_START)");
	}
}

void
sync_centre_start(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_CENTRE_START, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_CENTRE_START)");
	}
}

void
sync_edge_stop(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_EDGE_STOP, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_EDGE_STOP)");
	}
}

void
sync_centre_stop(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_CENTRE_STOP, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_CENTRE_STOP");
	}
}

void
sync_edge_clear(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_EDGE_CLEAR, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_EDGE_CLEAR)");
	}
}

void
sync_centre_clear(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_CENTRE_CLEAR, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_CENTRE_CLEAR)");
	}
}

void
sync_edge_test(int ac, char *av[])
{
	int i = 0;
	if(do_set_x(IP_FW_SYNC_EDGE_TEST, &i, sizeof(i)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_EDGE_CLEAR)");
	}
}

void
sync_centre_test(int ac, char *av[])
{
	int n;
	NEXT_ARG;
	if (!isdigit(**av)) {
		errx(EX_DATAERR, "invalid test number %s\n", *av);
	}
	n = atoi(*av);
	if(do_set_x(IP_FW_SYNC_CENTRE_TEST, &n, sizeof(n)) < 0) {
		err(EX_UNAVAILABLE, "do_set_x(IP_FW_SYNC_CENTRE_TEST)");
	}
	printf("centre test %d sent\n", n);
}
