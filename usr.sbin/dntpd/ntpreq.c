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
 * $DragonFly: src/usr.sbin/dntpd/ntpreq.c,v 1.3 2005/04/24 09:39:27 dillon Exp $
 */

#include "defs.h"

static __inline void
s_fixedpt_ntoh(struct s_fixedpt *fixed)
{
    fixed->int_parts = ntohs(fixed->int_parts);
    fixed->fractions = ntohs(fixed->fractions);
}

static __inline void
s_fixedpt_hton(struct s_fixedpt *fixed)
{
    fixed->int_parts = htons(fixed->int_parts);
    fixed->fractions = htons(fixed->fractions);
}


static __inline void
l_fixedpt_ntoh(struct l_fixedpt *fixed)
{
    fixed->int_partl = ntohl(fixed->int_partl);
    fixed->fractionl = ntohl(fixed->fractionl);
}

static __inline void
l_fixedpt_hton(struct l_fixedpt *fixed)
{
    fixed->int_partl = htonl(fixed->int_partl);
    fixed->fractionl = htonl(fixed->fractionl);
}

static void
ntp_ntoh(struct ntp_msg *msg)
{
    msg->refid = ntohl(msg->refid);
    s_fixedpt_ntoh(&msg->rootdelay);
    s_fixedpt_ntoh(&msg->dispersion);
    l_fixedpt_ntoh(&msg->reftime);
    l_fixedpt_ntoh(&msg->orgtime);
    l_fixedpt_ntoh(&msg->rectime);
    l_fixedpt_ntoh(&msg->xmttime);
    msg->keyid = ntohl(msg->keyid);
}

static void
ntp_hton(struct ntp_msg *msg)
{
    msg->refid = htonl(msg->refid);
    s_fixedpt_hton(&msg->rootdelay);
    s_fixedpt_hton(&msg->dispersion);
    l_fixedpt_hton(&msg->reftime);
    l_fixedpt_hton(&msg->orgtime);
    l_fixedpt_hton(&msg->rectime);
    l_fixedpt_hton(&msg->xmttime);
    msg->keyid = htonl(msg->keyid);
}

int
udp_ntptimereq(int fd, struct timeval *rtvp, struct timeval *ltvp, 
	       struct timeval *lbtvp)
{
    struct ntp_msg wmsg;
    struct ntp_msg rmsg;
    struct timeval tv1;
    struct timeval seltv;
    fd_set rfds;
    int error;
    int n;

    /*
     * Setup the message
     */
    bzero(&wmsg, sizeof(wmsg));
    wmsg.status = LI_ALARM | MODE_CLIENT | (NTP_VERSION << 3);
    wmsg.ppoll = 4;
    wmsg.precision = -6;
    wmsg.rootdelay.int_parts = 1;
    wmsg.dispersion.int_parts = 1;
    wmsg.refid = 0;
    wmsg.xmttime.int_partl = time(NULL) + JAN_1970;
    wmsg.xmttime.fractionl = random();

    /*
     * Timed transmit
     */
    gettimeofday(&tv1, NULL);
    ntp_hton(&wmsg);
    n = write(fd, &wmsg, NTP_MSGSIZE_NOAUTH);
    if (n != NTP_MSGSIZE_NOAUTH)
	return(-1);
    ntp_ntoh(&wmsg);

    /*
     * Wait for reply
     */
    seltv.tv_sec = 2;
    seltv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    select(fd + 1, &rfds, NULL, NULL, &seltv);

    /*
     * Drain socket, process the first matching reply or error out.
     * Errors are not necessarily fatal.  e.g. a signal could cause select()
     * to return early.
     */
    error = -1;
    while ((n = read(fd, &rmsg, sizeof(rmsg))) >= 0) {
	if (n < NTP_MSGSIZE_NOAUTH)
	    continue;
	ntp_ntoh(&rmsg);
	if (bcmp(&rmsg.orgtime, &wmsg.xmttime, sizeof(rmsg.orgtime)) != 0)
	    continue;

	/*
	 * Ok, we have a good reply, how long did it take?
	 *
	 * reftime
	 * orgtime
	 * rectime
	 * xmttime
	 */
	gettimeofday(ltvp, NULL);
	sysntp_getbasetime(lbtvp);

	l_fixedpt_to_tv(&rmsg.xmttime, rtvp);
	tv_add_micro(rtvp, (long)(tv_delta_double(&tv1, ltvp) * 1000000.0) / 2);

	error = 0;
	break;
    }
    return(error);
}

