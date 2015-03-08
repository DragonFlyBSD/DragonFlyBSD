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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>
#include <netinet/in.h>

#include "../../../sys/net/ipfw2/ip_fw3.h"
#include "../../../sbin/ipfw2/ipfw.h"
#include "ipfw2_layer2.h"

/*
 * Returns the number of bits set (from left) in a contiguous bitmask,
 * or -1 if the mask is not contiguous.
 * XXX this needs a proper fix.
 * This effectively works on masks in big-endian (network) format.
 * when compiled on little endian architectures.
 *
 * First bit is bit 7 of the first byte -- note, for MAC addresses,
 * the first bit on the wire is bit 0 of the first byte.
 * len is the max length in bits.
 */
static int
contigmask(u_char *p, int len)
{
	int i, n;
	for (i = 0; i < len ; i++) {
		if ( (p[i/8] & (1 << (7 - (i%8)))) == 0) /* first bit unset */
			break;
	}
	for (n = i + 1; n < len; n++) {
		if ( (p[n/8] & (1 << (7 - (n%8)))) != 0)
			return -1; /* mask not contiguous */
	}
	return i;
}

/*
 * prints a MAC address/mask pair
 */
static void
print_mac(u_char *addr, u_char *mask)
{
	int l = contigmask(mask, 48);

	if (l == 0) {
		printf(" any");
	} else {
		printf(" %02x:%02x:%02x:%02x:%02x:%02x",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
		if (l == -1) {
			printf("&%02x:%02x:%02x:%02x:%02x:%02x",
				mask[0], mask[1], mask[2],
				mask[3], mask[4], mask[5]);
		} else if (l < 48) {
			printf("/%d", l);
		}
	}
}

static void
get_mac_addr_mask(char *p, u_char *addr, u_char *mask)
{
	int i, l;

	for (i = 0; i < 6; i++)
		addr[i] = mask[i] = 0;
	if (!strcmp(p, "any"))
		return;

	for (i = 0; *p && i < 6; i++, p++) {
		addr[i] = strtol(p, &p, 16);
		if (*p != ':') /* we start with the mask */
			break;
	}
	if (*p == '/') { /* mask len */
		l = strtol(p + 1, &p, 0);
		for (i = 0; l > 0; l -= 8, i++)
			mask[i] = (l >=8) ? 0xff : (~0) << (8-l);
	} else if (*p == '&') { /* mask */
		for (i = 0, p++; *p && i < 6; i++, p++) {
			mask[i] = strtol(p, &p, 16);
			if (*p != ':')
				break;
		}
	} else if (*p == '\0') {
		for (i = 0; i < 6; i++)
			mask[i] = 0xff;
	}
	for (i = 0; i < 6; i++)
		addr[i] &= mask[i];
}

void
parse_layer2(ipfw_insn **cmd, int *ac, char **av[])
{
	(*cmd)->opcode = O_LAYER2_LAYER2;
	(*cmd)->module = MODULE_LAYER2_ID;
	(*cmd)->len =  ((*cmd)->len&(F_NOT|F_OR))|LEN_OF_IPFWINSN;
	NEXT_ARG1;
}

void
parse_mac(ipfw_insn **cmd, int *ac, char **av[])
{
	NEED(*ac, 3, "mac dst src");
	NEXT_ARG1;
	(*cmd)->opcode = O_LAYER2_MAC;
	(*cmd)->module = MODULE_LAYER2_ID;
	(*cmd)->len =  ((*cmd)->len&(F_NOT|F_OR))|F_INSN_SIZE(ipfw_insn_mac);
	ipfw_insn_mac *mac = (ipfw_insn_mac *)(*cmd);
	get_mac_addr_mask(**av, mac->addr, mac->mask);	/* dst */
	NEXT_ARG1;
	get_mac_addr_mask(**av, &(mac->addr[6]), &(mac->mask[6])); /* src */
	NEXT_ARG1;
}

void
show_layer2(ipfw_insn *cmd)
{
	printf(" layer2");
}

void
show_mac(ipfw_insn *cmd)
{
	ipfw_insn_mac *m = (ipfw_insn_mac *)cmd;
	printf(" mac");
	print_mac( m->addr, m->mask);
	print_mac( m->addr + 6, m->mask + 6);
}

void
load_module(register_func function, register_keyword keyword)
{
	keyword(MODULE_LAYER2_ID, O_LAYER2_LAYER2, "layer2", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER2_ID, O_LAYER2_LAYER2,
			(parser_func)parse_layer2, (shower_func)show_layer2);

	keyword(MODULE_LAYER2_ID, O_LAYER2_MAC, "mac", IPFW_KEYWORD_TYPE_FILTER);
	function(MODULE_LAYER2_ID, O_LAYER2_MAC,
			(parser_func)parse_mac,(shower_func)show_mac);
}
