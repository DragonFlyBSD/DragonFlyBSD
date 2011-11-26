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
 * $DragonFly: src/usr.sbin/vknetd/bridge.c,v 1.1 2008/05/27 01:58:01 dillon Exp $
 */
/*
 * Bridging code (serialized)
 */
#include "vknetd.h"

static TAILQ_HEAD(, bridge) BridgeList = TAILQ_HEAD_INITIALIZER(BridgeList);

/*
 * Add the unix domain descriptor to our bridge
 */
bridge_t
bridge_add(ioinfo_t io)
{
	bridge_t bridge;

	bridge = malloc(sizeof(struct bridge));
	bzero(bridge, sizeof(*bridge));
	bridge->io = io;
	TAILQ_INIT(&bridge->mac_list);
	TAILQ_INSERT_TAIL(&BridgeList, bridge, entry);

	return(bridge);
}

/*
 * Remove the unix domain descriptor from our bridge
 */
void
bridge_del(bridge_t bridge)
{
	mac_t mac;

	TAILQ_REMOVE(&BridgeList, bridge, entry);

	while ((mac = TAILQ_FIRST(&bridge->mac_list)) != NULL)
		mac_delete(mac);

	free(bridge);
}

/*
 * Bridge a packet.  The packet is in the following form:
 *
 *	[src_mac:6][dst_mac:6][packet]
 */
void
bridge_packet(bridge_t bridge, u_int8_t *pkt, int bytes)
{
	bridge_t scan;
	mac_t mac;

	if (mac_broadcast(pkt + 6) == 0) {
		mac = mac_find(pkt + 6);
		if (mac == NULL) {
			mac_add(bridge, pkt + 6);
		} else if (mac->bridge != bridge) {
			mac_delete(mac);
			mac_add(bridge, pkt + 6);
		}
	}
	if (mac_broadcast(pkt + 0) == 0 && (mac = mac_find(pkt + 0)) != NULL) {
		if (mac->bridge != bridge &&
		    (mac->bridge->io->istap == 0 || filter_ok(pkt, bytes))) {
			write(mac->bridge->io->fd, pkt, bytes);
		}
	} else {
		TAILQ_FOREACH(scan, &BridgeList, entry) {
			if (scan != bridge &&
			    (scan->io->istap == 0 || filter_ok(pkt, bytes))) {
				write(scan->io->fd, pkt, bytes);
			}
		}
	}
}

