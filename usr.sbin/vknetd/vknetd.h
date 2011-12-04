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
 * $DragonFly: src/usr.sbin/vknetd/vknetd.h,v 1.1 2008/05/27 01:58:01 dillon Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <net/bridge/if_bridgevar.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <err.h>
#include <grp.h>

#include <pthread.h>

struct ioinfo;
TAILQ_HEAD(mac_list, mac);

typedef struct bridge {
        TAILQ_ENTRY(bridge) entry;
	struct mac_list	mac_list;
        struct bridge *fd_next;
	struct ioinfo *io;
        int flags;
} *bridge_t;

typedef struct mac {
        TAILQ_ENTRY(mac) lru_entry;
        TAILQ_ENTRY(mac) bridge_entry;
        struct mac *mac_next;
        bridge_t bridge;
        u_int8_t macbuf[6];
} *mac_t;

#define BRIDGE_HSIZE	256
#define BRIDGE_HMASK	(BRIDGE_HSIZE - 1)

#define MAC_HSIZE	1024
#define MAC_HMASK	(MAC_HSIZE - 1)

#define NMACS		1024

#define MAXPKT		(8192 + 128)

typedef struct ioinfo {
	int	fd;
	int	istap;
} *ioinfo_t;

extern int SecureOpt;
extern struct in_addr NetAddress;
extern struct in_addr NetMask;

bridge_t bridge_add(ioinfo_t io);
void bridge_del(bridge_t bridge);
void bridge_packet(bridge_t bridge, u_int8_t *pkt, int bytes);

void mac_init(void);
mac_t mac_find(u_int8_t *macbuf);
void mac_add(bridge_t bridge, u_int8_t *macbuf);
void mac_delete(mac_t mac);
int mac_broadcast(u_int8_t *macbuf);

int filter_ok(u_int8_t *pkt, int bytes);

