/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/usr.sbin/vknetd/filter.c,v 1.2 2008/05/27 22:47:16 dillon Exp $
 */
/*
 * Bridging code (serialized)
 */
#include "vknetd.h"
#include <netinet/ip.h>
#include <netinet/udp.h>

/*
 * Return on-zero if the packet should be passed through, zero if it should
 * be discarded.
 */
int
filter_ok(u_int8_t *pkt, int bytes)
{
	struct ip *ip;
	u_int16_t ether_type;

	if (bytes < 12 + 2 + 20)
		return(0);

	/*
	 * Allow only ARP and IP packetes
	 */
	ether_type = ntohs(*(u_int16_t *)(pkt + 12));
	if (ether_type == ETHERTYPE_ARP)
		return(1);
	if (ether_type != ETHERTYPE_IP)
		return(0);

	/*
	 * Allow only ICMP, TCP, and UDP protocols.
	 */
	ip = (void *)(pkt + 14);

	switch(ip->ip_p) {
	case 1:		/* ICMP */
		/* XXX fix me */
		break;
	case 17:	/* UDP */
	case 6:		/* TCP */
		/*
		 * ip_src must represent our network or be 0.
		 * 0 is a special case, mainly so bootp (dhclient) gets
		 * through.
		 */
		if (ip->ip_src.s_addr == 0)
			break;
		if (SecureOpt &&
		    (ip->ip_src.s_addr & NetMask.s_addr) !=
		    (NetAddress.s_addr & NetMask.s_addr)) {
			fprintf(stderr, "Filtered Address: %08x\n",
				ntohl(ip->ip_src.s_addr));
			return(0);
		}
		break;
	default:
		if (SecureOpt)
			return(0);
		break;
	}
	return(1);
}

