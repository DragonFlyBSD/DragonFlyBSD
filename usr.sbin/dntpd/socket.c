/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/usr.sbin/dntpd/socket.c,v 1.3 2005/04/25 17:42:49 dillon Exp $
 */

#include "defs.h"

int
udp_socket(const char *target, int port)
{
    struct sockaddr_in sam;
    struct hostent *hp;
    int rc;
    int fd;
    int tos;

    if ((rc = inet_aton(target, &sam.sin_addr)) == 0) {
	if ((hp = gethostbyname2(target, AF_INET)) == NULL) {
	    logerr("Unable to resolve %s", target);
	    return(-1);
	}
	bcopy(hp->h_addr_list[0], &sam.sin_addr, hp->h_length);
    } else if (rc != 1) {
	logerrstr("unable to resolve ip/host: %s", target);
	return(-1);
    }
    if ((fd = socket(PF_INET, SOCK_DGRAM, PF_UNSPEC)) < 0) {
	logerr("socket(%s)", target);
	return(-1);
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
	logerr("socket(%s) unable to set non-blocking mode", target);
	close(fd);
	return(-1);
    }
    sam.sin_port = htons(port);
    sam.sin_len = sizeof(sam);
    sam.sin_family = AF_INET;
    if (connect(fd, (void *)&sam, sizeof(sam)) < 0) {
	logerr("connect(%s)", target);
	close(fd);
	return(-1);
    }
#ifdef IPTOS_LOWDELAY
    tos = IPTOS_LOWDELAY;
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)); 
#endif
#if 0
#ifdef IP_PORTRANGE
    tos = IP_PORTRANGE_HIGH;
    setsockopt(fd, IPPROTO_IP, IP_PORTRANGE, &tos, sizeof(tos)); 
#endif
#endif
    return(fd);
}

