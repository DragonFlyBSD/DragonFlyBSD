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
 */

#include <sys/param.h>
#include <sys/socketvar.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/netmsg2.h>

#include <netinet/in.h>

#include <net/ipfw3/ip_fw.h>

int ip_fw3_loaded;
int fw3_enable = 1;
int fw3_one_pass = 1;

static void	ip_fw3_sockopt_dispatch(netmsg_t msg);

int
ip_fw3_sockopt(struct sockopt *sopt)
{
	struct netmsg_base smsg;

	netmsg_init(&smsg, NULL, &curthread->td_msgport,
		    0, ip_fw3_sockopt_dispatch);
	smsg.lmsg.u.ms_resultp = sopt;
	return lwkt_domsg(IPFW_CFGPORT, &smsg.lmsg, 0);
}

static void
ip_fw3_sockopt_dispatch(netmsg_t msg)
{
	struct sockopt *sopt = msg->lmsg.u.ms_resultp;
	int error;

	KKASSERT(mycpuid == 0);

	if (IPFW3_LOADED)
		error = ip_fw_ctl_x_ptr(sopt);
	else
		error = ENOPROTOOPT;
	lwkt_replymsg(&msg->lmsg, error);
}
