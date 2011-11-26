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
 * $DragonFly: src/usr.sbin/dntpd/socket.c,v 1.4 2007/06/25 21:33:36 dillon Exp $
 */

#include "defs.h"

int
udp_socket(const char *target, int port, struct sockaddr *sam,
	   dns_error_policy_t dns_error_policy)
{
    struct addrinfo hints, *res, *res0;
    char servname[128];
    const char *cause = NULL;
    int error;
    int fd;
    int tos;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(servname, sizeof(servname), "%d", port);
    error = getaddrinfo(target, servname, &hints, &res0);
    if (error) {
	if (dns_error_policy == LOG_DNS_ERROR)
	    logerr("getaddrinfo (%s) init error: %s", target,
		gai_strerror(error));
        return(-1);
    }

    fd = -1;
    for (res = res0; res; res = res->ai_next) {
        fd = socket(res->ai_family, res->ai_socktype,
        res->ai_protocol);
        if (fd < 0) {
           cause = "socket";
           continue;
        }

        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
            logerr("%s: unable to set non-blocking mode", target);
            close(fd);
            fd = -1;
            continue;
        }

        if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
           cause = "connect";
           close(fd);
           fd = -1;
           continue;
        }

        break;  /* okay we got one */
    }

    if (fd < 0) {
        logerr("Unable to establish a connection with %s: %s", target, cause);
        return(-1);
    }
    memcpy(sam, res->ai_addr, res->ai_addr->sa_len);
    freeaddrinfo(res0);

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
