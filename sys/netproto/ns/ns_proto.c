/*
 * Copyright (c) 1984, 1985, 1986, 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	From: @(#)ns_proto.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netns/ns_proto.c,v 1.10 1999/08/28 00:49:51 peter Exp $
 * $DragonFly: src/sys/netproto/ns/ns_proto.c,v 1.6 2004/06/07 07:04:33 dillon Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/mbuf.h>

#include <net/radix.h>

#include "ns.h"
#include "idp.h"
#include "idp_var.h"
#include "sp.h"
#include "spidp.h"
#include "spp_timer.h"
#include "spp_var.h"

/*
 * NS protocol family: IDP, ERR, PE, SPP, ROUTE.
 */

static  struct pr_usrreqs nousrreqs;

struct protosw nssw[] = {
{ 0,		&nsdomain,	0,		0,
  0,		idp_output,	0,		0,
  cpu0_soport,
  ns_init,	0,		0,		0,
  &nousrreqs
},
{ SOCK_DGRAM,	&nsdomain,	0,		PR_ATOMIC|PR_ADDR,
  0,		0,		idp_ctlinput,	idp_ctloutput,
  cpu0_soport,
  0,		0,		0,		0,
  &idp_usrreqs
},
{ SOCK_STREAM,	&nsdomain,	NSPROTO_SPP,	PR_CONNREQUIRED|PR_WANTRCVD,
  spp_input,	0,		spp_ctlinput,	spp_ctloutput,
  cpu0_soport,
  spp_init,	spp_fasttimo,	spp_slowtimo,	0,
  &spp_usrreqs
},
{ SOCK_SEQPACKET,&nsdomain,	NSPROTO_SPP,	PR_CONNREQUIRED|PR_WANTRCVD|PR_ATOMIC,
  spp_input,	0,		spp_ctlinput,	spp_ctloutput,
  cpu0_soport,
  0,		0,		0,		0,
  &spp_usrreqs_sp
},
{ SOCK_RAW,	&nsdomain,	NSPROTO_RAW,	PR_ATOMIC|PR_ADDR,
  idp_input,	idp_output,	0,		idp_ctloutput,
  cpu0_soport,
  0,		0,		0,		0,
  &idp_raw_usrreqs
},
{ SOCK_RAW,	&nsdomain,	NSPROTO_ERROR,	PR_ATOMIC|PR_ADDR,
  idp_ctlinput,	idp_output,	0,		idp_ctloutput,
  cpu0_soport,
  0,		0,		0,		0,
  &idp_raw_usrreqs
}
};

struct domain nsdomain =
    { AF_NS, "network systems", 0, 0, 0,
      nssw, &nssw[sizeof(nssw)/sizeof(nssw[0])], 0,
      rn_inithead, 16, sizeof(struct sockaddr_ns)};

DOMAIN_SET(ns);
