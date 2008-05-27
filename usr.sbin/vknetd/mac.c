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
 * $DragonFly: src/usr.sbin/vknetd/mac.c,v 1.1 2008/05/27 01:58:01 dillon Exp $
 */
/*
 * Bridging code (serialized)
 */
#include "vknetd.h"

static struct mac_list LRUList = TAILQ_HEAD_INITIALIZER(LRUList);
static mac_t MacHash[MAC_HSIZE];

void
mac_init(void)
{
	static struct mac MacAry[NMACS];
	int i;

	for (i = 0; i < NMACS; ++i)
		TAILQ_INSERT_TAIL(&LRUList, &MacAry[i], lru_entry);
}

static __inline int
machash(u_int8_t *macbuf)
{
	int hv;

	hv = macbuf[0] ^ (macbuf[1] << 1) ^ (macbuf[2] << 2) ^
	     (macbuf[3] << 3) ^ (macbuf[4] << 4) ^ (macbuf[5] << 5);
	return(hv & MAC_HMASK);
}

/*
 * Locate a cached MAC address, return NULL if we do not have it cached.
 * The MAC is moved to the head of the LRU list.
 */
mac_t
mac_find(u_int8_t *macbuf)
{
	mac_t scan;

	for (scan = MacHash[machash(macbuf)]; scan; scan = scan->mac_next) {
		if (bcmp(macbuf, scan->macbuf, 6) == 0) {
			if (TAILQ_FIRST(&LRUList) != scan) {
				TAILQ_REMOVE(&LRUList, scan, lru_entry);
				TAILQ_INSERT_HEAD(&LRUList, scan, lru_entry);
			}
			return(scan);
		}
	}
	return(NULL);
}

/*
 * Create a bridge association for a MAC address.  The caller must
 * have already deleted any pre-existing association.
 */
void
mac_add(bridge_t bridge, u_int8_t *macbuf)
{
	mac_t *hashp;
	mac_t mac;

	/*
	 * Recycle least recently used MAC
	 */
	mac = TAILQ_LAST(&LRUList, mac_list);
	if (mac->bridge)
		mac_delete(mac);

	/*
	 * Create the bridge assocation and enter the MAC into the hash
	 * table.
	 */
	bcopy(macbuf, mac->macbuf, 6);
	TAILQ_INSERT_TAIL(&bridge->mac_list, mac, bridge_entry);

	hashp = &MacHash[machash(mac->macbuf)];
	mac->mac_next = *hashp;
	*hashp = mac;

	mac->bridge = bridge;

	/*
	 * We don't want to recycle the MAC any time soon.
	 */
	TAILQ_REMOVE(&LRUList, mac, lru_entry);
	TAILQ_INSERT_HEAD(&LRUList, mac, lru_entry);
}

/*
 * Delete a MAC's bridge assocation
 */
void
mac_delete(mac_t mac)
{
	mac_t *hashp;

	/*
	 * Remove the bridge linkage
	 */
	TAILQ_REMOVE(&mac->bridge->mac_list, mac, bridge_entry);
	mac->bridge = NULL;

	/*
	 * Remove the MAC hash linkage
	 */
	hashp = &MacHash[machash(mac->macbuf)];
	while (*hashp != mac) {
		assert(*hashp);
		hashp = &(*hashp)->mac_next;
	}
	*hashp = mac->mac_next;

	/*
	 * This MAC is now unused, place on end of LRU list.
	 */
	TAILQ_REMOVE(&LRUList, mac, lru_entry);
	TAILQ_INSERT_TAIL(&LRUList, mac, lru_entry);
}

/*
 * Return non-zero if this is a broadcast MAC
 */
int
mac_broadcast(u_int8_t *macbuf)
{
	if (macbuf[0] == 0 && macbuf[1] == 0 && macbuf[2] == 0 &&
	    macbuf[3] == 0 && macbuf[4] == 0 && macbuf[5] == 0) {
		return(1);
	}
	if (macbuf[0] == 0xFF && macbuf[1] == 0xFF && macbuf[2] == 0xFF &&
	    macbuf[3] == 0xFF && macbuf[4] == 0xFF && macbuf[5] == 0xFF) {
		return(1);
	}
	return(0);
}

