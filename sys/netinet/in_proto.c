/*
 * Copyright (c) 1982, 1986, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)in_proto.c	8.2 (Berkeley) 2/9/95
 * $FreeBSD: src/sys/netinet/in_proto.c,v 1.53.2.7 2003/08/24 08:24:38 hsu Exp $
 */

#include "opt_ipdivert.h"
#include "opt_mrouting.h"
#include "opt_ipsec.h"
#include "opt_inet6.h"
#include "opt_sctp.h"
#include "opt_carp.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/igmp_var.h>
#ifdef PIM
#include <netinet/pim_var.h>
#endif
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip_encap.h>
#ifdef IPDIVERT
#include <netinet/ip_divert.h>
#endif

/*
 * TCP/IP protocol family: IP, ICMP, UDP, TCP.
 */

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netinet6/ah.h>
#ifdef IPSEC_ESP
#include <netinet6/esp.h>
#endif
#include <netinet6/ipcomp.h>
#endif /* IPSEC */

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#endif /* FAST_IPSEC */

#ifdef SCTP
#include <netinet/sctp_pcb.h>
#include <netinet/sctp.h>
#include <netinet/sctp_var.h>
#endif /* SCTP */

#include <net/netisr.h>		/* for cpu0_soport */

#ifdef CARP
#include <netinet/ip_carp.h>
#endif

extern	struct domain inetdomain;
static	struct pr_usrreqs nousrreqs;

struct protosw inetsw[] = {
    {
	.pr_type = 0,
	.pr_domain = &inetdomain,
	.pr_protocol = 0,
	.pr_flags = 0,

	.pr_ctlport = NULL,

	.pr_init = ip_init,
	.pr_fasttimo = NULL,
	.pr_slowtimo = ip_slowtimo,
	.pr_drain = ip_drain,
	.pr_usrreqs = &nousrreqs
    },
    {
	.pr_type = SOCK_DGRAM,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_UDP,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_MPSAFE|
	    PR_ASYNC_SEND|PR_ASEND_HOLDTD,

	.pr_initport = udp_initport,
	.pr_input = udp_input,
	.pr_output = NULL,
	.pr_ctlinput = udp_ctlinput,
	.pr_ctloutput = udp_ctloutput,

	.pr_ctlport = udp_ctlport,
	.pr_init = udp_init,
	.pr_usrreqs = &udp_usrreqs
    },
    {
	.pr_type = SOCK_STREAM,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_TCP,
	.pr_flags = PR_CONNREQUIRED|PR_WANTRCVD|PR_MPSAFE|
	    PR_ASYNC_SEND|PR_ASYNC_RCVD|PR_ACONN_HOLDTD,

	.pr_initport = tcp_initport,
	.pr_input = tcp_input,
	.pr_output = NULL,
	.pr_ctlinput = tcp_ctlinput,
	.pr_ctloutput = tcp_ctloutput,

	.pr_ctlport = tcp_ctlport,
	.pr_init = tcp_init,
	.pr_fasttimo = NULL,
	.pr_slowtimo = NULL,
	.pr_drain = tcp_drain,
	.pr_usrreqs = &tcp_usrreqs
    },
#ifdef SCTP
    /*
     * Order is very important here, we add the good one in
     * in this postion so it maps to the right ip_protox[]
     * postion for SCTP. Don't move the one above below
     * this one or IPv6/4 compatability will break
     */
    {
	.pr_type = SOCK_DGRAM,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_SCTP,
	.pr_flags = PR_ADDR_OPT|PR_WANTRCVD,

	.pr_input = sctp_input,
	.pr_output = NULL,
	.pr_ctlinput = sctp_ctlinput,
	.pr_ctloutput = sctp_ctloutput,

	.pr_ctlport = cpu0_ctlport,
	.pr_init = sctp_init,
	.pr_drain = sctp_drain,
	.pr_usrreqs = &sctp_usrreqs
    },
    {
	.pr_type = SOCK_SEQPACKET,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_SCTP,
	.pr_flags = PR_ADDR_OPT|PR_WANTRCVD,

	.pr_input = sctp_input,
	.pr_output = NULL,
	.pr_ctlinput = sctp_ctlinput,
	.pr_ctloutput = sctp_ctloutput,

	.pr_ctlport = cpu0_ctlport,
	.pr_drain = sctp_drain,
	.pr_usrreqs = &sctp_usrreqs
    },

    {
	.pr_type = SOCK_STREAM,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_SCTP,
	.pr_flags = PR_CONNREQUIRED|PR_ADDR_OPT|PR_WANTRCVD,

	.pr_input = sctp_input,
	.pr_output = NULL,
	.pr_ctlinput = sctp_ctlinput,
	.pr_ctloutput = sctp_ctloutput,

	.pr_ctlport = cpu0_ctlport,
	.pr_drain = sctp_drain,
	.pr_usrreqs = &sctp_usrreqs
    },
#endif /* SCTP */
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_RAW,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = rip_input,
	.pr_output = NULL,
	.pr_ctlinput = rip_ctlinput,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = cpu0_ctlport,
	.pr_usrreqs = &rip_usrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_ICMP,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR|PR_MPSAFE,

	.pr_input = icmp_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_usrreqs = &rip_usrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_IGMP,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR|PR_MPSAFE,

	.pr_input = igmp_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_init = igmp_init,
	.pr_fasttimo = igmp_fasttimo,
	.pr_slowtimo = igmp_slowtimo,
	.pr_drain = NULL,
	.pr_usrreqs = &rip_usrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_RSVP,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR,

	.pr_input = rsvp_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_usrreqs = &rip_usrreqs
    },
#ifdef IPSEC
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_AH,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = ah4_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = NULL,

	.pr_ctlport = NULL,
	.pr_usrreqs = &nousrreqs
    },
#ifdef IPSEC_ESP
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_ESP,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = esp4_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = NULL,

	.pr_ctlport = NULL,
	.pr_usrreqs = &nousrreqs
    },
#endif
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_IPCOMP,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = ipcomp4_input,
	.pr_output = 0,
	.pr_ctlinput = NULL,
	.pr_ctloutput = NULL,

	.pr_ctlport = NULL,
	.pr_usrreqs = &nousrreqs
    },
#endif /* IPSEC */
#ifdef FAST_IPSEC
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_AH,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = ipsec4_common_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = NULL,

	.pr_ctlport = NULL,
	.pr_usrreqs = &nousrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_ESP,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = ipsec4_common_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = NULL,

	.pr_ctlport = NULL,
	.pr_usrreqs = &nousrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_IPCOMP,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = ipsec4_common_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = NULL,

	.pr_ctlport = NULL,
	.pr_usrreqs = &nousrreqs
    },
#endif /* FAST_IPSEC */
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_IPV4,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR,
	.pr_input = encap4_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = NULL,
	.pr_init = encap_init,
	.pr_usrreqs = &rip_usrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_MOBILE,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR,

	.pr_input = encap4_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = NULL,
	.pr_init = encap_init,
	.pr_usrreqs = &rip_usrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_GRE,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR,

	.pr_input = encap4_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = NULL,
	.pr_init = encap_init,
	.pr_usrreqs = &rip_usrreqs
    },
#ifdef INET6
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_IPV6,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR,

	.pr_input = encap4_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = NULL,
	.pr_init = encap_init,
	.pr_usrreqs = &rip_usrreqs
    },
#endif
#ifdef IPDIVERT
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_DIVERT,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = div_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = ip_ctloutput,

	.pr_ctlport = NULL,
	.pr_init = div_init,
	.pr_usrreqs = &div_usrreqs
    },
#endif
#ifdef PIM
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_PIM,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_LASTHDR,

	.pr_input = pim_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = NULL,
	.pr_usrreqs = &rip_usrreqs
    },
#endif
#ifdef NPFSYNC
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_PFSYNC,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = pfsync_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = NULL,
	.pr_usrreqs = &rip_usrreqs
    },
#endif	/* NPFSYNC */
    {
	/* raw wildcard */
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = 0,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = rip_input,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = rip_ctloutput,

	.pr_init = rip_init,
	.pr_ctlport = NULL,
	.pr_usrreqs = &rip_usrreqs
    },
#ifdef CARP
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &inetdomain,
	.pr_protocol = IPPROTO_CARP,
	.pr_flags = PR_ATOMIC|PR_ADDR|PR_MPSAFE,

	.pr_input = carp_proto_input,
	.pr_output = rip_output,
	.pr_ctlinput = carp_proto_ctlinput,
	.pr_ctloutput = rip_ctloutput,

	.pr_ctlport = cpu0_ctlport,
	.pr_usrreqs = &rip_usrreqs
    },
#endif
};

static void
inetdomain_init(void)
{
	in_pcbglobalinit();
}

struct domain inetdomain = {
	AF_INET, "internet", inetdomain_init, NULL, NULL,
	inetsw, &inetsw[NELEM(inetsw)],
	SLIST_ENTRY_INITIALIZER,
	in_inithead, 32, sizeof(struct sockaddr_in),
};

DOMAIN_SET(inet);

SYSCTL_NODE(_net,      PF_INET,		inet,	CTLFLAG_RW, 0,
	"Internet Family");

SYSCTL_NODE(_net_inet, IPPROTO_IP,	ip,	CTLFLAG_RW, 0,	"IP");
SYSCTL_NODE(_net_inet, IPPROTO_ICMP,	icmp,	CTLFLAG_RW, 0,	"ICMP");
SYSCTL_NODE(_net_inet, IPPROTO_UDP,	udp,	CTLFLAG_RW, 0,	"UDP");
SYSCTL_NODE(_net_inet, IPPROTO_TCP,	tcp,	CTLFLAG_RW, 0,	"TCP");
SYSCTL_NODE(_net_inet, IPPROTO_IGMP,	igmp,	CTLFLAG_RW, 0,	"IGMP");
#ifdef FAST_IPSEC
/* XXX no protocol # to use, pick something "reserved" */
SYSCTL_NODE(_net_inet, 253,		ipsec,	CTLFLAG_RW, 0,	"IPSEC");
SYSCTL_NODE(_net_inet, IPPROTO_AH,	ah,	CTLFLAG_RW, 0,	"AH");
SYSCTL_NODE(_net_inet, IPPROTO_ESP,	esp,	CTLFLAG_RW, 0,	"ESP");
SYSCTL_NODE(_net_inet, IPPROTO_IPCOMP,	ipcomp,	CTLFLAG_RW, 0,	"IPCOMP");
SYSCTL_NODE(_net_inet, IPPROTO_IPIP,	ipip,	CTLFLAG_RW, 0,	"IPIP");
#else
#ifdef IPSEC
SYSCTL_NODE(_net_inet, IPPROTO_AH,	ipsec,	CTLFLAG_RW, 0,	"IPSEC");
#endif /* IPSEC */
#endif /* !FAST_IPSEC */
SYSCTL_NODE(_net_inet, IPPROTO_RAW,	raw,	CTLFLAG_RW, 0,	"RAW");
#ifdef IPDIVERT
SYSCTL_NODE(_net_inet, IPPROTO_DIVERT,	divert,	CTLFLAG_RW, 0,	"DIVERT");
#endif
#ifdef PIM
SYSCTL_NODE(_net_inet, IPPROTO_PIM,    pim,    CTLFLAG_RW, 0,  "PIM");
#endif
#ifdef CARP
SYSCTL_NODE(_net_inet, IPPROTO_CARP,    carp,    CTLFLAG_RW, 0,  "CARP");
#endif
