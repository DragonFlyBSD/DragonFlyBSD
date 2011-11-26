/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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
 * $DragonFly: src/tools/tools/netrate/pktgenctl/pktgenctl.c,v 1.3 2008/03/29 11:45:46 sephe Exp $
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/ethernet.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pktgen/pktgen.h"

#define PKTGEN_DEVPATH	"/dev/pktg0"

#define DEFAULT_SPORT	3001
#define DEFAULT_DPORT	3000

#define INDST_MASK	0x1
#define INSRC_MASK	0x2
#define EADDR_MASK	0x4
#define IFACE_MASK	0x8
#define DATALEN_MASK	0x10
#define SPORT_MASK	0x20
#define DPORT_MASK	0x40
#define CPUID_MASK	0x80
#define DURATION_MASK	0x100
#define YIELD_MASK	0x200

#define MASK_NEEDED	(INDST_MASK | INSRC_MASK | EADDR_MASK | IFACE_MASK)

static void
usage(void)
{
	fprintf(stderr, "pktgenctl -d dst_inaddr[,ndst] -s src_inaddr[,nsrc] "
			"-e (gw_eaddr|dst_eaddr) -i iface "
			"[-p src_port[,nsport]] [-P dst_port[,ndport]] "
			"[-m data_len] [-c cpuid] [-l duration] [-y yield]\n");
	exit(1);
}

static int
get_range(char *str)
{
	char *ptr;

	ptr = strstr(str, ",");
	if (ptr == NULL) {
		return 0;
	} else {
		*ptr = '\0';
		return atoi(ptr + 1);
	}
}

int
main(int argc, char *argv[])
{
	struct pktgen_conf conf;
	struct sockaddr *sa;
	struct sockaddr_in *dst_sin, *src_sin;
	struct sockaddr_dl sdl;
	char eaddr_str[32];
	uint32_t arg_mask = 0;
	int fd, c, n;

	memset(&conf, 0, sizeof(conf));

	conf.pc_cpuid = 0;
	conf.pc_duration = 10;
	conf.pc_datalen = 10;

	sa = &conf.pc_dst_lladdr;
	sa->sa_family = AF_LINK;
	sa->sa_len = ETHER_ADDR_LEN;

	dst_sin = &conf.pc_dst;
	dst_sin->sin_family = AF_INET;
	dst_sin->sin_port = htons(DEFAULT_DPORT);

	src_sin = &conf.pc_src;
	src_sin->sin_family = AF_INET;
	src_sin->sin_port = htons(DEFAULT_SPORT);

	while ((c = getopt(argc, argv, "d:s:e:i:p:P:m:c:l:y:")) != -1) {
		switch (c) {
		case 'd':
			conf.pc_ndaddr = get_range(optarg);
			n = inet_pton(AF_INET, optarg,
				      &dst_sin->sin_addr.s_addr);
			if (n == 0)
				errx(1, "-d: invalid inet address");
			else if (n < 0)
				err(1, "-d");
			arg_mask |= INDST_MASK;
			break;

		case 's':
			conf.pc_nsaddr = get_range(optarg);
			n = inet_pton(AF_INET, optarg,
				      &src_sin->sin_addr.s_addr);
			if (n == 0)
				errx(1, "-s: invalid inet address");
			else if (n < 0)
				err(1, "-s");
			arg_mask |= INSRC_MASK;
			break;

		case 'e':
			strcpy(eaddr_str, "if0.");
			strlcpy(&eaddr_str[strlen("if0.")], optarg,
				sizeof(eaddr_str) - strlen("if0."));

			memset(&sdl, 0, sizeof(sdl));
			sdl.sdl_len = sizeof(sdl);
			link_addr(eaddr_str, &sdl);
			bcopy(LLADDR(&sdl), sa->sa_data, ETHER_ADDR_LEN);
			arg_mask |= EADDR_MASK;
			break;

		case 'i':
			strlcpy(conf.pc_ifname, optarg, sizeof(conf.pc_ifname));
			arg_mask |= IFACE_MASK;
			break;

		case 'p':
			conf.pc_nsport = get_range(optarg);
			src_sin->sin_port = htons(atoi(optarg));
			arg_mask |= SPORT_MASK;
			break;

		case 'P':
			conf.pc_ndport = get_range(optarg);
			dst_sin->sin_port = htons(atoi(optarg));
			arg_mask |= DPORT_MASK;
			break;

		case 'm':
			conf.pc_datalen = atoi(optarg);
			arg_mask |= DATALEN_MASK;
			break;

		case 'c':
			conf.pc_cpuid = atoi(optarg);
			arg_mask |= CPUID_MASK;
			break;

		case 'l':
			conf.pc_duration = atoi(optarg);
			arg_mask |= DURATION_MASK;
			break;

		case 'y':
			conf.pc_yield = atoi(optarg);
			arg_mask |= YIELD_MASK;
			break;
		}
	}

	if ((arg_mask & MASK_NEEDED) != MASK_NEEDED)
		usage();

	fd = open(PKTGEN_DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "open(" PKTGEN_DEVPATH ")");

	if (ioctl(fd, PKTGENSCONF, &conf) < 0)
		err(1, "ioctl(PKTGENSCONF)");

	if (ioctl(fd, PKTGENSTART) < 0)
		err(1, "ioctl(PKTGENSTART)");

	close(fd);
	exit(0);
}
