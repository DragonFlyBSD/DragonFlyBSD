/*
 * Copyright (c) 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

/*
 * Copyright (c) 1982, 1986, 1991, 1993, 1995
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
 *	@(#)in_pcb.c	8.4 (Berkeley) 5/24/95
 * $FreeBSD: src/sys/netinet/in_pcb.c,v 1.59.2.27 2004/01/02 04:06:42 ambrisko Exp $
 */

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <sys/socketvar2.h>
#include <sys/msgport2.h>

#include <machine/limits.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/netisr2.h>
#include <net/toeplitz2.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#define INP_LOCALGROUP_SIZMIN	8
#define INP_LOCALGROUP_SIZMAX	256

static struct inpcb *in_pcblookup_local(struct inpcbporthead *porthash,
		struct in_addr laddr, u_int lport_arg, int wild_okay,
		struct ucred *cred);

struct in_addr zeroin_addr;

/*
 * These configure the range of local port addresses assigned to
 * "unspecified" outgoing connections/packets/whatever.
 */
int ipport_lowfirstauto = IPPORT_RESERVED - 1;	/* 1023 */
int ipport_lowlastauto = IPPORT_RESERVEDSTART;	/* 600 */

int ipport_firstauto = IPPORT_RESERVED;		/* 1024 */
int ipport_lastauto = IPPORT_USERRESERVED;	/* 5000 */

int ipport_hifirstauto = IPPORT_HIFIRSTAUTO;	/* 49152 */
int ipport_hilastauto = IPPORT_HILASTAUTO;	/* 65535 */

#define RANGECHK(var, min, max) \
	if ((var) < (min)) { (var) = (min); } \
	else if ((var) > (max)) { (var) = (max); }

int udpencap_enable = 1;	/* enabled by default */
int udpencap_port = 4500;	/* triggers decapsulation */

/*
 * Per-netisr inpcb markers.
 * NOTE: they should only be used in netisrs.
 */
static struct inpcb		*in_pcbmarkers;
static struct inpcontainer	*in_pcbcontainer_markers;

static int
sysctl_net_ipport_check(SYSCTL_HANDLER_ARGS)
{
	int error;

	error = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2, req);
	if (!error) {
		RANGECHK(ipport_lowfirstauto, 1, IPPORT_RESERVED - 1);
		RANGECHK(ipport_lowlastauto, 1, IPPORT_RESERVED - 1);

		RANGECHK(ipport_firstauto, IPPORT_RESERVED, USHRT_MAX);
		RANGECHK(ipport_lastauto, IPPORT_RESERVED, USHRT_MAX);

		RANGECHK(ipport_hifirstauto, IPPORT_RESERVED, USHRT_MAX);
		RANGECHK(ipport_hilastauto, IPPORT_RESERVED, USHRT_MAX);
	}
	return (error);
}

#undef RANGECHK

SYSCTL_NODE(_net_inet_ip, IPPROTO_IP, portrange, CTLFLAG_RW, 0, "IP Ports");

SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, lowfirst, CTLTYPE_INT|CTLFLAG_RW,
	   &ipport_lowfirstauto, 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, lowlast, CTLTYPE_INT|CTLFLAG_RW,
	   &ipport_lowlastauto, 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, first, CTLTYPE_INT|CTLFLAG_RW,
	   &ipport_firstauto, 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, last, CTLTYPE_INT|CTLFLAG_RW,
	   &ipport_lastauto, 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, hifirst, CTLTYPE_INT|CTLFLAG_RW,
	   &ipport_hifirstauto, 0, &sysctl_net_ipport_check, "I", "");
SYSCTL_PROC(_net_inet_ip_portrange, OID_AUTO, hilast, CTLTYPE_INT|CTLFLAG_RW,
	   &ipport_hilastauto, 0, &sysctl_net_ipport_check, "I", "");

/* Initialized by ip_init() */
int ip_porthash_trycount;
SYSCTL_INT(_net_inet_ip, OID_AUTO, porthash_trycount, CTLFLAG_RW,
    &ip_porthash_trycount, 0,
    "Number of tries to find local port matching hash of 4-tuple");

/*
 * in_pcb.c: manage the Protocol Control Blocks.
 *
 * NOTE: It is assumed that most of these functions will be called from
 * a critical section.  XXX - There are, unfortunately, a few exceptions
 * to this rule that should be fixed.
 *
 * NOTE: The caller should initialize the cpu field to the cpu running the
 * protocol stack associated with this inpcbinfo.
 */

void
in_pcbinfo_init(struct inpcbinfo *pcbinfo, int cpu, boolean_t shared)
{
	KASSERT(cpu >= 0 && cpu < netisr_ncpus, ("invalid cpu%d", cpu));
	pcbinfo->cpu = cpu;

	LIST_INIT(&pcbinfo->pcblisthead);
	pcbinfo->portsave = kmalloc(sizeof(*pcbinfo->portsave), M_PCB,
				    M_WAITOK | M_ZERO);

	if (shared) {
		pcbinfo->infotoken = kmalloc(sizeof(struct lwkt_token),
		    M_PCB, M_WAITOK);
		lwkt_token_init(pcbinfo->infotoken, "infotoken");
	} else {
		pcbinfo->infotoken = NULL;
	}
}

void
in_pcbportinfo_set(struct inpcbinfo *pcbinfo, struct inpcbportinfo *portinfo,
    int portinfo_cnt)
{

	KASSERT(portinfo_cnt > 0, ("invalid portinfo_cnt %d", portinfo_cnt));
	pcbinfo->portinfo = portinfo;
	pcbinfo->portinfo_cnt = portinfo_cnt;
}

struct baddynamicports baddynamicports;

/*
 * Check if the specified port is invalid for dynamic allocation.
 */
int
in_baddynamic(u_int16_t port, u_int16_t proto)
{
	switch (proto) {
	case IPPROTO_TCP:
		return (DP_ISSET(baddynamicports.tcp, port));
	case IPPROTO_UDP:
		return (DP_ISSET(baddynamicports.udp, port));
	default:
		return (0);
	}
}

void
in_pcbonlist(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in the correct netisr"));
	KASSERT((inp->inp_flags & INP_ONLIST) == 0, ("already on pcblist"));
	inp->inp_flags |= INP_ONLIST;

	GET_PCBINFO_TOKEN(pcbinfo);
	LIST_INSERT_HEAD(&pcbinfo->pcblisthead, inp, inp_list);
	pcbinfo->ipi_count++;
	REL_PCBINFO_TOKEN(pcbinfo);
}

void
in_pcbofflist(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in the correct netisr"));
	KASSERT(inp->inp_flags & INP_ONLIST, ("not on pcblist"));
	inp->inp_flags &= ~INP_ONLIST;

	GET_PCBINFO_TOKEN(pcbinfo);
	LIST_REMOVE(inp, inp_list);
	KASSERT(pcbinfo->ipi_count > 0,
	    ("invalid inpcb count %d", pcbinfo->ipi_count));
	pcbinfo->ipi_count--;
	REL_PCBINFO_TOKEN(pcbinfo);
}

/*
 * Allocate a PCB and associate it with the socket.
 */
int
in_pcballoc(struct socket *so, struct inpcbinfo *pcbinfo)
{
	struct inpcb *inp;

	inp = kmalloc(pcbinfo->ipi_size, M_PCB, M_WAITOK|M_ZERO|M_NULLOK);
	if (inp == NULL)
		return (ENOMEM);
	inp->inp_lgrpindex = -1;
	inp->inp_gencnt = ++pcbinfo->ipi_gencnt;
	inp->inp_pcbinfo = pcbinfo;
	inp->inp_socket = so;
#ifdef INET6
	if (INP_CHECK_SOCKAF(so, AF_INET6)) {
		if (ip6_auto_flowlabel)
			inp->inp_flags |= IN6P_AUTOFLOWLABEL;
		inp->inp_af = AF_INET6;
	} else
#endif
	inp->inp_af = AF_INET;
	soreference(so);
	so->so_pcb = inp;

	in_pcbonlist(inp);
	return (0);
}

/*
 * Unlink a pcb with the intention of moving it to another cpu with a
 * different pcbinfo.  While unlinked nothing should attempt to dereference
 * inp_pcbinfo, NULL it out so we assert if it does.
 */
void
in_pcbunlink_flags(struct inpcb *inp, struct inpcbinfo *pcbinfo, int flags)
{
	KASSERT(inp->inp_pcbinfo == pcbinfo, ("pcbinfo mismatch"));
	KASSERT((inp->inp_flags & (flags | INP_CONNECTED)) == 0,
	    ("already linked"));

	in_pcbofflist(inp);
	inp->inp_pcbinfo = NULL;
}

void
in_pcbunlink(struct inpcb *inp, struct inpcbinfo *pcbinfo)
{
	in_pcbunlink_flags(inp, pcbinfo, INP_WILDCARD);
}

/*
 * Relink a pcb into a new pcbinfo.
 */
void
in_pcblink_flags(struct inpcb *inp, struct inpcbinfo *pcbinfo, int flags)
{
	KASSERT(inp->inp_pcbinfo == NULL, ("has pcbinfo"));
	KASSERT((inp->inp_flags & (flags | INP_CONNECTED)) == 0,
	    ("already linked"));

	inp->inp_pcbinfo = pcbinfo;
	in_pcbonlist(inp);
}

void
in_pcblink(struct inpcb *inp, struct inpcbinfo *pcbinfo)
{
	return in_pcblink_flags(inp, pcbinfo, INP_WILDCARD);
}

static boolean_t
in_pcbporthash_update(struct inpcbportinfo *portinfo,
    struct inpcb *inp, u_short lport, struct ucred *cred, int wild)
{
	struct inpcbporthead *porthash;

	/*
	 * This has to be atomic.  If the porthash is shared across multiple
	 * protocol threads, e.g. tcp and udp, then the token must be held.
	 */
	porthash = in_pcbporthash_head(portinfo, lport);
	GET_PORTHASH_TOKEN(porthash);

	if (in_pcblookup_local(porthash, inp->inp_laddr, lport, wild, cred)) {
		REL_PORTHASH_TOKEN(porthash);
		return FALSE;
	}
	inp->inp_lport = lport;
	in_pcbinsporthash(porthash, inp);

	REL_PORTHASH_TOKEN(porthash);
	return TRUE;
}

static int
in_pcbsetlport(struct inpcb *inp, int wild, struct ucred *cred)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbportinfo *portinfo;
	u_short first, last, lport, step, first0, last0;
	int count, error;
	int portinfo_first, portinfo_idx;
	uint32_t cut;

	/*
	 * We force matches against wildcard ports in order to
	 * avoid auto-assigning lport to such ports, which would
	 * cause problems for same-machine connect()s.
	 */
	wild = INPLOOKUP_WILDCARD;

	inp->inp_flags |= INP_ANONPORT;

	step = pcbinfo->portinfo_cnt;
	portinfo_first = mycpuid % pcbinfo->portinfo_cnt;
	portinfo_idx = portinfo_first;

	if (inp->inp_flags & INP_HIGHPORT) {
		first0 = ipport_hifirstauto;	/* sysctl */
		last0  = ipport_hilastauto;
	} else if (inp->inp_flags & INP_LOWPORT) {
		if (cred &&
		    (error =
		     priv_check_cred(cred, PRIV_NETINET_RESERVEDPORT, 0))) {
			inp->inp_laddr.s_addr = INADDR_ANY;
			return error;
		}
		first0 = ipport_lowfirstauto;	/* 1023 */
		last0  = ipport_lowlastauto;	/* 600 */
	} else {
		first0 = ipport_firstauto;	/* sysctl */
		last0  = ipport_lastauto;
	}
	if (first0 > last0) {
		lport = last0;
		last0 = first0;
		first0 = lport;
	}
	KKASSERT(last0 >= first0);

	cut = karc4random();
loop:
	portinfo = &pcbinfo->portinfo[portinfo_idx];
	first = first0;
	last = last0;

	/*
	 * Simple check to ensure all ports are not used up causing
	 * a deadlock here.
	 */
	in_pcbportrange(&last, &first, portinfo->offset, step);
	lport = last - first;
	count = lport / step;

	lport = rounddown(cut % lport, step) + first;
	KKASSERT(lport % step == portinfo->offset);

	for (;;) {
		if (count-- < 0) {	/* completely used? */
			error = EADDRNOTAVAIL;
			break;
		}

		if (__predict_false(lport < first || lport > last)) {
			lport = first;
			KKASSERT(lport % step == portinfo->offset);
		}

		if (in_pcbporthash_update(portinfo, inp, htons(lport),
					  cred, wild)) {
			error = 0;
			break;
		}

		lport += step;
		KKASSERT(lport % step == portinfo->offset);
	}

	if (error) {
		/* Try next portinfo */
		portinfo_idx++;
		portinfo_idx %= pcbinfo->portinfo_cnt;
		if (portinfo_idx != portinfo_first)
			goto loop;
		inp->inp_laddr.s_addr = INADDR_ANY;
	}
	return error;
}

static __inline struct inpcbporthead *
OBTAIN_LPORTHASH_TOKEN(struct inpcbinfo *pcbinfo, u_short lport)
{
	struct inpcbportinfo *portinfo;
	struct inpcbporthead *porthash;
	u_short lport_ho = ntohs(lport);

	/*
	 * Locate the proper portinfo based on lport
	 */
	portinfo = &pcbinfo->portinfo[lport_ho % pcbinfo->portinfo_cnt];
	KKASSERT((lport_ho % pcbinfo->portinfo_cnt) == portinfo->offset);

	porthash = in_pcbporthash_head(portinfo, lport);
	GET_PORTHASH_TOKEN(porthash);

	return porthash;
}

static int
in_pcbbind_laddr(struct sockaddr_in *sin, struct in_addr *laddr,
    struct thread *td)
{
	struct sockaddr_in jsin;

	if (!prison_replace_wildcards(td, (struct sockaddr *)sin))
		return (EINVAL);

	if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)) &&
	    sin->sin_addr.s_addr != INADDR_ANY) {
		sin->sin_port = 0;		/* yech... */
		bzero(&sin->sin_zero, sizeof sin->sin_zero);
		if (ifa_ifwithaddr((struct sockaddr *)sin) == NULL)
			return (EADDRNOTAVAIL);
	}

	*laddr = sin->sin_addr;

	jsin.sin_family = AF_INET;
	jsin.sin_addr.s_addr = laddr->s_addr;
	if (!prison_replace_wildcards(td, (struct sockaddr *)&jsin)) {
		laddr->s_addr = INADDR_ANY;
		return (EINVAL);
	}
	laddr->s_addr = jsin.sin_addr.s_addr;

	return (0);
}

static int
in_pcbbind_laddrport_check(const struct socket *so, struct sockaddr_in *sin,
    struct inpcbporthead *porthash, int wild, struct ucred *cred,
    struct thread *td)
{
	int reuseport = (so->so_options & SO_REUSEPORT);
	struct inpcb *t;

	ASSERT_PORTHASH_TOKEN_HELD(porthash);

	if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
		/*
		 * Treat SO_REUSEADDR as SO_REUSEPORT for multicast;
		 * allow complete duplication of binding if
		 * SO_REUSEPORT is set, or if SO_REUSEADDR is set
		 * and a multicast address is bound on both
		 * new and duplicated sockets.
		 */
		if (so->so_options & SO_REUSEADDR)
			reuseport = SO_REUSEADDR | SO_REUSEPORT;
	}

	if (so->so_cred->cr_uid != 0 &&
	    !IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
		t = in_pcblookup_local(porthash, sin->sin_addr, sin->sin_port,
				       INPLOOKUP_WILDCARD, cred);
		if (t && t->inp_socket != so &&
		    (so->so_cred->cr_uid != t->inp_socket->so_cred->cr_uid))
			return (EADDRINUSE);
	}
	if (cred && !prison_replace_wildcards(td, (struct sockaddr *)sin))
		return (EADDRNOTAVAIL);

	/*
	 * When binding to a local port if the best match is against
	 * an accepted socket we generally want to allow the binding.
	 * This means that there is no longer any specific socket
	 * bound or bound for listening.
	 */
	t = in_pcblookup_local(porthash, sin->sin_addr, sin->sin_port,
			       wild, cred);
	if (t && t->inp_socket != so &&
	    (reuseport & t->inp_socket->so_options) == 0 &&
	    (t->inp_socket->so_state & SS_ACCEPTMECH) == 0)
		return (EADDRINUSE);

	return (0);
}

int
in_pcbsrcaddr_check(const struct inpcb *inp, struct sockaddr_in *sin,
    struct in_addr *laddr, struct thread *td)
{
	const struct socket *so = inp->inp_socket;
	struct inpcbporthead *porthash;
	struct ucred *cred = NULL;
	int wild = 0;
	int error;

	/* inp must be bound beforehand. */
	KKASSERT(inp->inp_lport != 0);
	KKASSERT(sin->sin_len == sizeof(*sin));

	if (!(so->so_options & (SO_REUSEADDR|SO_REUSEPORT)))
		wild = 1;    /* neither SO_REUSEADDR nor SO_REUSEPORT is set */
	if (td->td_proc)
		cred = td->td_proc->p_ucred;

	/* Always use inp_lport */
	sin->sin_port = inp->inp_lport;

	error = in_pcbbind_laddr(sin, laddr, td);
	if (error)
		return (error);

	if (IN_MULTICAST(ntohl(laddr->s_addr))) {
		/* Unlike bind, multicast src address is not allowed. */
		return (EINVAL);
	}

	if (inp->inp_laddr.s_addr == laddr->s_addr) {
		/*
		 * src address is same as what we bound to.
		 *
		 * inp_laddr == INADDR_ANY && srcaddr == INADDR_ANY
		 * is allowed, which does not really matter.
		 */
		return (0);
	} else if (inp->inp_laddr.s_addr != INADDR_ANY &&
	    !IN_MULTICAST(ntohl(inp->inp_laddr.s_addr))) {
		/* Already bound to a specific address */
		return (EINVAL);
	}

	/*
	 * This has to be atomic.  If the porthash is shared across
	 * multiple protocol threads, e.g. tcp and udp then the token
	 * must be held.
	 */
	porthash = OBTAIN_LPORTHASH_TOKEN(inp->inp_pcbinfo, inp->inp_lport);

	/*
	 * Restore the sin_port whacked by in_pcbbind_ladddr();
	 * sin->sin_port is checked by in_pcbbind_laddrport_check().
	 */
	sin->sin_port = inp->inp_lport;

	error = in_pcbbind_laddrport_check(so, sin, porthash, wild, cred, td);
	if (error)
		laddr->s_addr = INADDR_ANY;

	REL_PORTHASH_TOKEN(porthash);
	return (error);
}

int
in_pcbbind(struct inpcb *inp, struct sockaddr *nam, struct thread *td)
{
	const struct socket *so = inp->inp_socket;
	struct ucred *cred = NULL;
	int wild = 0;

	if (inp->inp_lport != 0 || inp->inp_laddr.s_addr != INADDR_ANY)
		return (EINVAL);	/* already bound */

	if (!(so->so_options & (SO_REUSEADDR|SO_REUSEPORT)))
		wild = 1;    /* neither SO_REUSEADDR nor SO_REUSEPORT is set */
	if (td->td_proc)
		cred = td->td_proc->p_ucred;

	if (nam != NULL) {
		struct sockaddr_in *sin = (struct sockaddr_in *)nam;
		struct inpcbporthead *porthash;
		u_short lport;
		int error;

		if (nam->sa_len != sizeof *sin)
			return (EINVAL);
#ifdef notdef
		/*
		 * We should check the family, but old programs
		 * incorrectly fail to initialize it.
		 */
		if (sin->sin_family != AF_INET)
			return (EAFNOSUPPORT);
#endif

		/*
		 * Save sin_port for later use, since it will
		 * be whacked by in_pcbbind_laddr().
		 */
		lport = sin->sin_port;

		error = in_pcbbind_laddr(sin, &inp->inp_laddr, td);
		if (error)
			return (error);

		if (lport == 0) {
			/* Auto-select local port */
			return in_pcbsetlport(inp, wild, cred);
		}

		/* GROSS */
		if (ntohs(lport) < IPPORT_RESERVED && cred &&
		    (error =
		     priv_check_cred(cred, PRIV_NETINET_RESERVEDPORT, 0))) {
			inp->inp_laddr.s_addr = INADDR_ANY;
			return (error);
		}

		/*
		 * This has to be atomic.  If the porthash is shared across
		 * multiple protocol threads, e.g. tcp and udp then the token
		 * must be held.
		 */
		porthash = OBTAIN_LPORTHASH_TOKEN(inp->inp_pcbinfo, lport);

		/*
		 * Restore the sin_port whacked by in_pcbbind_ladddr();
		 * sin->sin_port is checked by in_pcbbind_laddrport_check().
		 */
		sin->sin_port = lport;

		error = in_pcbbind_laddrport_check(so, sin, porthash,
						   wild, cred, td);
		if (error) {
			inp->inp_laddr.s_addr = INADDR_ANY;
			goto done;
		}

		inp->inp_lport = lport;
		in_pcbinsporthash(porthash, inp);
		error = 0;
done:
		REL_PORTHASH_TOKEN(porthash);
		return (error);
	} else {
		struct sockaddr_in jsin;

		jsin.sin_family = AF_INET;
		jsin.sin_addr.s_addr = inp->inp_laddr.s_addr;
		if (!prison_replace_wildcards(td, (struct sockaddr *)&jsin)) {
			inp->inp_laddr.s_addr = INADDR_ANY;
			return (EINVAL);
		}
		inp->inp_laddr.s_addr = jsin.sin_addr.s_addr;

		return in_pcbsetlport(inp, wild, cred);
	}
}

/*
 * Lookup a PCB based on the local and remote address and port.
 *
 * This function is only used when scanning for a free port.
 */
static struct inpcb *
in_pcblookup_localremote(struct inpcbporthead *porthash, struct in_addr laddr,
			 u_short lport, struct in_addr faddr, u_short fport,
			 struct ucred *cred)
{
	struct inpcb *inp;
	struct inpcbport *phd;
	struct inpcb *match = NULL;
	struct prison *pscan;
	struct prison *pr;

	/*
	 * If the porthashbase is shared across several cpus, it must
	 * have been locked.
	 */
	ASSERT_PORTHASH_TOKEN_HELD(porthash);

	/*
	 * Best fit PCB lookup.
	 *
	 * First see if this local port is in use by looking on the
	 * port hash list.
	 */
	LIST_FOREACH(phd, porthash, phd_hash) {
		if (phd->phd_port == lport)
			break;
	}
	if (phd != NULL) {
		pr = cred ? cred->cr_prison : NULL;

		LIST_FOREACH(inp, &phd->phd_pcblist, inp_portlist) {
#ifdef INET6
			if (!INP_ISIPV4(inp))
				continue;
#endif
			if (inp->inp_laddr.s_addr == INADDR_ANY) {
				if (inp->inp_socket && inp->inp_socket->so_cred)
					pscan = inp->inp_socket->so_cred->cr_prison;
				else
					pscan = NULL;
				if (pr != pscan)
					continue;
			} else {
				if (inp->inp_laddr.s_addr != laddr.s_addr)
					continue;
			}

			if (inp->inp_faddr.s_addr != INADDR_ANY &&
			    inp->inp_faddr.s_addr != faddr.s_addr)
				continue;

			if (inp->inp_fport != 0 && inp->inp_fport != fport)
				continue;

			match = inp;
			break;
		}
	}
	return (match);
}

static boolean_t
in_pcbporthash_update4(struct inpcbportinfo *portinfo, struct inpcb *inp,
		       u_short lport, const struct sockaddr_in *sin,
		       struct ucred *cred)
{
	struct inpcbporthead *porthash;

	/*
	 * This has to be atomic.  If the porthash is shared across multiple
	 * protocol threads, e.g. tcp and udp, then the token must be held.
	 */
	porthash = in_pcbporthash_head(portinfo, lport);
	GET_PORTHASH_TOKEN(porthash);

	if (in_pcblookup_localremote(porthash, inp->inp_laddr, lport,
				     sin->sin_addr, sin->sin_port, cred)) {
		REL_PORTHASH_TOKEN(porthash);
		return FALSE;
	}
	inp->inp_lport = lport;
	in_pcbinsporthash(porthash, inp);

	REL_PORTHASH_TOKEN(porthash);
	return TRUE;
}

int
in_pcbbind_remote(struct inpcb *inp, const struct sockaddr *remote,
    struct thread *td)
{
	struct proc *p = td->td_proc;
	const struct sockaddr_in *sin = (const struct sockaddr_in *)remote;
	struct sockaddr_in jsin;
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct ucred *cred = NULL;
	u_short first, last, lport;
	int count, hash_count;
	int error, selfconn = 0;
	int cpuid = mycpuid;
	uint32_t hash_base = 0, hash;

	ASSERT_NETISR_NCPUS(cpuid);

	if (TAILQ_EMPTY(&in_ifaddrheads[cpuid])) /* XXX broken! */
		return (EADDRNOTAVAIL);

	KKASSERT(inp->inp_laddr.s_addr != INADDR_ANY);
	if (inp->inp_lport != 0)
		return (EINVAL);	/* already bound */

	KKASSERT(p);
	cred = p->p_ucred;

	jsin.sin_family = AF_INET;
	jsin.sin_addr.s_addr = inp->inp_laddr.s_addr;
	if (!prison_replace_wildcards(td, (struct sockaddr *)&jsin)) {
		inp->inp_laddr.s_addr = INADDR_ANY;
		return (EINVAL);
	}
	inp->inp_laddr.s_addr = jsin.sin_addr.s_addr;

	hash_count = ip_porthash_trycount;
	if (hash_count > 0) {
		hash_base = toeplitz_piecemeal_addr(sin->sin_addr.s_addr) ^
		    toeplitz_piecemeal_addr(inp->inp_laddr.s_addr) ^
		    toeplitz_piecemeal_port(sin->sin_port);
	} else {
		hash_count = 0;
	}

	inp->inp_flags |= INP_ANONPORT;

	if (inp->inp_flags & INP_HIGHPORT) {
		first = ipport_hifirstauto;	/* sysctl */
		last  = ipport_hilastauto;
	} else if (inp->inp_flags & INP_LOWPORT) {
		if (cred &&
		    (error =
		     priv_check_cred(cred, PRIV_NETINET_RESERVEDPORT, 0))) {
			inp->inp_laddr.s_addr = INADDR_ANY;
			return (error);
		}
		first = ipport_lowfirstauto;	/* 1023 */
		last = ipport_lowlastauto;	/* 600 */
	} else {
		first = ipport_firstauto;	/* sysctl */
		last  = ipport_lastauto;
	}
	if (first > last) {
		lport = last;
		last = first;
		first = lport;
	}
	KKASSERT(last >= first);

	count = last - first;
	lport = (karc4random() % count) + first;
	count += hash_count;

	/*
	 * Simple check to ensure all ports are not used up causing
	 * a deadlock here.
	 */
	for (;;) {
		u_short lport_no;

		if (count-- < 0) {	/* completely used? */
			error = EADDRNOTAVAIL;
			break;
		}

		if (__predict_false(lport < first || lport > last))
			lport = first;
		lport_no = htons(lport);

		/* This could happen on loopback interface */
		if (__predict_false(sin->sin_port == lport_no &&
		    sin->sin_addr.s_addr == inp->inp_laddr.s_addr)) {
			if (!selfconn) {
				++count; /* don't count this try */
				selfconn = 1;
			}
			goto next;
		}

		if (hash_count) {
			--hash_count;
			hash = hash_base ^
			    toeplitz_piecemeal_port(lport_no);
			if (netisr_hashcpu(hash) != cpuid && hash_count)
				goto next;
		}

		if (in_pcbporthash_update4(
			    &pcbinfo->portinfo[lport % pcbinfo->portinfo_cnt],
			    inp, lport_no, sin, cred)) {
			error = 0;
			break;
		}
next:
		++lport;
	}

	if (error)
		inp->inp_laddr.s_addr = INADDR_ANY;
	return (error);
}

/*
 * Figure out the local interface address to pair against the requested
 * target address, as well as validate the target address.
 */
int
in_pcbladdr_find(struct inpcb *inp, struct sockaddr *nam,
		 struct sockaddr_in **plocal_sin, struct thread *td, int find)
{
	struct in_ifaddr_container *iac;
	struct in_ifaddr *ia;
	struct ucred *cred = NULL;
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	struct sockaddr *jsin;
	struct prison *pr;
	struct route *ro;
	int alloc_route = 0;

	if (nam->sa_len != sizeof *sin)
		return (EINVAL);
	if (sin->sin_family != AF_INET)
		return (EAFNOSUPPORT);
	if (sin->sin_port == 0)
		return (EADDRNOTAVAIL);

	/*
	 * Are we in a jail?
	 */
	pr = NULL;
	if (td && td->td_proc && td->td_proc->p_ucred)
		cred = td->td_proc->p_ucred;
	if (cred)
		pr = cred->cr_prison;

	/*
	 * If the destination address is INADDR_ANY then use the primary
	 * local address.
	 *
	 * If the supplied address is INADDR_BROADCAST, and the primary
	 * interface supports broadcast, choose the broadcast address for
	 * that interface.
	 *
	 * If jailed, locate an interface address acceptable to the jail.
	 */
	if (sin->sin_addr.s_addr == INADDR_ANY) {
		TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
			ia = iac->ia;
			if (pr == NULL ||
			    jailed_ip(pr, sintosa(&ia->ia_addr))) {
				sin->sin_addr = IA_SIN(ia)->sin_addr;
				break;
			}
		}
	} else if (sin->sin_addr.s_addr == (u_long)INADDR_BROADCAST) {
		TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
			ia = iac->ia;
			if ((pr == NULL ||
			     jailed_ip(pr, sintosa(&ia->ia_addr))) &&
			    (iac->ia->ia_ifp->if_flags & IFF_BROADCAST)) {
				sin->sin_addr =
				    satosin(&ia->ia_broadaddr)->sin_addr;
				break;
			}
		}
	}

	/*
	 * If asked to do a search, use the cached route or do a route table
	 * lookup to try to find an acceptable local interface IP.
	 */
	if (find == 0)
		return 0;

	ia = NULL;

	/*
	 * If we have a cached route, check to see if it is acceptable.
	 * If not, free it.
	 */
	ro = &inp->inp_route;
	if (ro->ro_rt &&
	    (!(ro->ro_rt->rt_flags & RTF_UP) ||
	     ro->ro_dst.sa_family != AF_INET ||
	     satosin(&ro->ro_dst)->sin_addr.s_addr !=
			      sin->sin_addr.s_addr ||
	     inp->inp_socket->so_options & SO_DONTROUTE)) {
		RTFREE(ro->ro_rt);
		ro->ro_rt = NULL;
	}

	/*
	 * If we do not have a route, construct one and do a lookup,
	 * unless we are forbidden to do so.
	 *
	 * Note that we should check the address family of the cached
	 * destination, in case of sharing the cache with IPv6.
	 */
	if (!(inp->inp_socket->so_options & SO_DONTROUTE) && /*XXX*/
	    (ro->ro_rt == NULL || ro->ro_rt->rt_ifp == NULL)) {
		bzero(&ro->ro_dst, sizeof(struct sockaddr_in));
		ro->ro_dst.sa_family = AF_INET;
		ro->ro_dst.sa_len = sizeof(struct sockaddr_in);
		((struct sockaddr_in *)&ro->ro_dst)->sin_addr = sin->sin_addr;
		rtalloc(ro);
		alloc_route = 1;
	}

	/*
	 * If we found a route, use the address corresponding to the
	 * outgoing interface.
	 *
	 * If jailed, try to find a compatible address on the outgoing
	 * interface.
	 */
	if (ro->ro_rt) {
		ia = ifatoia(ro->ro_rt->rt_ifa);
		if (pr == NULL)
			goto skip;
		if (jailed_ip(pr, sintosa(&ia->ia_addr)))
			goto skip;
		TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
			if (iac->ia->ia_ifp != ia->ia_ifp)
				continue;
			ia = iac->ia;
			if (jailed_ip(pr, sintosa(&ia->ia_addr)))
				goto skip;
		}
		ia = NULL;
	}
skip:

	/*
	 * If the route didn't work or there was no route,
	 * fall-back to the first address in in_ifaddrheads[].
	 *
	 * If jailed and this address is not available for
	 * the jail, leave ia set to NULL.
	 */
	if (ia == NULL) {
		u_short fport = sin->sin_port;

		sin->sin_port = 0;
		ia = ifatoia(ifa_ifwithdstaddr(sintosa(sin)));
		if (ia && pr && !jailed_ip(pr, sintosa(&ia->ia_addr)))
			ia = NULL;

		if (ia == NULL)
			ia = ifatoia(ifa_ifwithnet(sintosa(sin)));
		if (ia && pr && !jailed_ip(pr, sintosa(&ia->ia_addr)))
			ia = NULL;

		sin->sin_port = fport;
		if (ia == NULL && !TAILQ_EMPTY(&in_ifaddrheads[mycpuid]))
			ia = TAILQ_FIRST(&in_ifaddrheads[mycpuid])->ia;

		if (ia && pr && !jailed_ip(pr, sintosa(&ia->ia_addr)))
			ia = NULL;

		if (pr == NULL && ia == NULL)
			goto fail;
	}

	/*
	 * If the destination address is multicast and an outgoing
	 * interface has been set as a multicast option, use the
	 * address of that interface as our source address.
	 */
	if (pr == NULL && IN_MULTICAST(ntohl(sin->sin_addr.s_addr)) &&
	    inp->inp_moptions != NULL) {
		struct ip_moptions *imo;
		struct ifnet *ifp;

		imo = inp->inp_moptions;
		if ((ifp = imo->imo_multicast_ifp) != NULL) {
			struct in_ifaddr_container *iac;

			ia = NULL;
			TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
				if (iac->ia->ia_ifp == ifp) {
					ia = iac->ia;
					break;
				}
			}
			if (ia == NULL)
				goto fail;
		}
	}

	/*
	 * If we still don't have a local address, and are jailed,
	 * use the jail's first non-localhost IP.  If there isn't
	 * one, use the jail's first localhost IP.
	 *
	 * Don't do pcblookup call here; return interface in plocal_sin
	 * and exit to caller, that will do the lookup.
	 */
	if (ia == NULL && pr) {
		jsin = prison_get_nonlocal(cred->cr_prison, AF_INET, NULL);
		if (jsin == NULL)
			jsin = prison_get_local(cred->cr_prison, AF_INET, NULL);
		if (jsin)
			*plocal_sin = satosin(jsin);
		else
			goto fail;
	} else if (ia) {
		*plocal_sin = &ia->ia_addr;
	} else {
		goto fail;
	}
	return (0);
fail:
	if (alloc_route)
		in_pcbresetroute(inp);
	return (EADDRNOTAVAIL);
}

int
in_pcbladdr(struct inpcb *inp, struct sockaddr *nam,
	    struct sockaddr_in **plocal_sin, struct thread *td)
{
	return in_pcbladdr_find(inp, nam, plocal_sin, td,
				(inp->inp_laddr.s_addr == INADDR_ANY));
}

/*
 * Outer subroutine:
 * Connect from a socket to a specified address.
 * Both address and port must be specified in argument sin.
 * If don't have a local address for this socket yet,
 * then pick one.
 */
int
in_pcbconnect(struct inpcb *inp, struct sockaddr *nam, struct thread *td)
{
	struct sockaddr_in *if_sin;
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	int error;

	if_sin = NULL;	/* avoid gcc warnings */

	/* Call inner routine to assign local interface address. */
	if ((error = in_pcbladdr(inp, nam, &if_sin, td)) != 0)
		return (error);

	if (in_pcblookup_hash(inp->inp_pcbinfo, sin->sin_addr, sin->sin_port,
			      inp->inp_laddr.s_addr ?
				inp->inp_laddr : if_sin->sin_addr,
			      inp->inp_lport, FALSE, NULL) != NULL) {
		return (EADDRINUSE);
	}
	if (inp->inp_laddr.s_addr == INADDR_ANY) {
		if (inp->inp_lport == 0) {
			error = in_pcbbind(inp, NULL, td);
			if (error)
				return (error);
		}
		inp->inp_laddr = if_sin->sin_addr;
	}
	inp->inp_faddr = sin->sin_addr;
	inp->inp_fport = sin->sin_port;
	in_pcbinsconnhash(inp);
	return (0);
}

void
in_pcbdisconnect(struct inpcb *inp)
{

	in_pcbremconnhash(inp);
	inp->inp_faddr.s_addr = INADDR_ANY;
	inp->inp_fport = 0;
}

void
in_pcbdetach(struct inpcb *inp)
{
	struct socket *so = inp->inp_socket;
	struct inpcbinfo *ipi = inp->inp_pcbinfo;

	inp->inp_gencnt = ++ipi->ipi_gencnt;
	KKASSERT((so->so_state & SS_ASSERTINPROG) == 0);
	in_pcbremlists(inp);
	so->so_pcb = NULL;
	sofree(so);			/* remove pcb ref */
	if (inp->inp_options)
		m_free(inp->inp_options);
	if (inp->inp_route.ro_rt)
		rtfree(inp->inp_route.ro_rt);
	ip_freemoptions(inp->inp_moptions);
	kfree(inp, M_PCB);
}

/*
 * The socket may have an invalid PCB, i.e. NULL.  For example, a TCP
 * socket received RST.
 */
static int
in_setsockaddr(struct socket *so, struct sockaddr **nam)
{
	struct inpcb *inp;
	struct sockaddr_in *sin;

	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));
	inp = so->so_pcb;
	if (!inp)
		return (ECONNRESET);

	sin = kmalloc(sizeof *sin, M_SONAME, M_WAITOK | M_ZERO);
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof *sin;
	sin->sin_port = inp->inp_lport;
	sin->sin_addr = inp->inp_laddr;

	*nam = (struct sockaddr *)sin;
	return (0);
}

void
in_setsockaddr_dispatch(netmsg_t msg)
{
	int error;

	error = in_setsockaddr(msg->base.nm_so, msg->peeraddr.nm_nam);
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * The socket may have an invalid PCB, i.e. NULL.  For example, a TCP
 * socket received RST.
 */
int
in_setpeeraddr(struct socket *so, struct sockaddr **nam)
{
	struct inpcb *inp;
	struct sockaddr_in *sin;

	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));
	inp = so->so_pcb;
	if (!inp)
		return (ECONNRESET);

	sin = kmalloc(sizeof *sin, M_SONAME, M_WAITOK | M_ZERO);
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof *sin;
	sin->sin_port = inp->inp_fport;
	sin->sin_addr = inp->inp_faddr;

	*nam = (struct sockaddr *)sin;
	return (0);
}

void
in_setpeeraddr_dispatch(netmsg_t msg)
{
	int error;

	error = in_setpeeraddr(msg->base.nm_so, msg->peeraddr.nm_nam);
	lwkt_replymsg(&msg->lmsg, error);
}

void
in_pcbnotifyall(struct inpcbinfo *pcbinfo, struct in_addr faddr, int err,
    inp_notify_t notify)
{
	struct inpcb *inp, *marker;

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in the correct netisr"));
	marker = in_pcbmarker();

	/*
	 * NOTE:
	 * - If INP_PLACEMARKER is set we must ignore the rest of the
	 *   structure and skip it.
	 * - It is safe to nuke inpcbs here, since we are in their own
	 *   netisr.
	 */
	GET_PCBINFO_TOKEN(pcbinfo);

	LIST_INSERT_HEAD(&pcbinfo->pcblisthead, marker, inp_list);
	while ((inp = LIST_NEXT(marker, inp_list)) != NULL) {
		LIST_REMOVE(marker, inp_list);
		LIST_INSERT_AFTER(inp, marker, inp_list);

		if (inp->inp_flags & INP_PLACEMARKER)
			continue;
#ifdef INET6
		if (!INP_ISIPV4(inp))
			continue;
#endif
		if (inp->inp_faddr.s_addr != faddr.s_addr ||
		    inp->inp_socket == NULL)
			continue;
		(*notify)(inp, err);		/* can remove inp from list! */
	}
	LIST_REMOVE(marker, inp_list);

	REL_PCBINFO_TOKEN(pcbinfo);
}

void
in_pcbpurgeif0(struct inpcbinfo *pcbinfo, struct ifnet *ifp)
{
	struct inpcb *inp, *marker;

	/*
	 * We only need to make sure that we are in netisr0, where all
	 * multicast operation happen.  We could check inpcbinfo which
	 * does not belong to netisr0 by holding the inpcbinfo's token.
	 * In this case, the pcbinfo must be able to be shared, i.e.
	 * pcbinfo->infotoken is not NULL.
	 */
	ASSERT_NETISR0;
	KASSERT(pcbinfo->cpu == 0 || pcbinfo->infotoken != NULL,
	    ("pcbinfo could not be shared"));

	/*
	 * Get a marker for the current netisr (netisr0).
	 *
	 * It is possible that the multicast address deletion blocks,
	 * which could cause temporary token releasing.  So we use
	 * inpcb marker here to get a coherent view of the inpcb list.
	 *
	 * While, on the other hand, moptions are only added and deleted
	 * in netisr0, so we would not see staled moption or miss moption
	 * even if the token was released due to the blocking multicast
	 * address deletion.
	 */
	marker = in_pcbmarker();

	GET_PCBINFO_TOKEN(pcbinfo);

	LIST_INSERT_HEAD(&pcbinfo->pcblisthead, marker, inp_list);
	while ((inp = LIST_NEXT(marker, inp_list)) != NULL) {
		struct ip_moptions *imo;

		LIST_REMOVE(marker, inp_list);
		LIST_INSERT_AFTER(inp, marker, inp_list);

		if (inp->inp_flags & INP_PLACEMARKER)
			continue;
		imo = inp->inp_moptions;
		if (INP_ISIPV4(inp) && imo != NULL) {
			int i, gap;

			/*
			 * Unselect the outgoing interface if it is being
			 * detached.
			 */
			if (imo->imo_multicast_ifp == ifp)
				imo->imo_multicast_ifp = NULL;

			/*
			 * Drop multicast group membership if we joined
			 * through the interface being detached.
			 */
			for (i = 0, gap = 0; i < imo->imo_num_memberships;
			    i++) {
				if (imo->imo_membership[i]->inm_ifp == ifp) {
					/*
					 * NOTE:
					 * This could block and the pcbinfo
					 * token could be passively released.
					 */
					in_delmulti(imo->imo_membership[i]);
					gap++;
				} else if (gap != 0)
					imo->imo_membership[i - gap] =
					    imo->imo_membership[i];
			}
			imo->imo_num_memberships -= gap;
		}
	}
	LIST_REMOVE(marker, inp_list);

	REL_PCBINFO_TOKEN(pcbinfo);
}

/*
 * Check for alternatives when higher level complains
 * about service problems.  For now, invalidate cached
 * routing information.  If the route was created dynamically
 * (by a redirect), time to try a default gateway again.
 */
void
in_losing(struct inpcb *inp)
{
	struct rtentry *rt;
	struct rt_addrinfo rtinfo;

	if ((rt = inp->inp_route.ro_rt)) {
		bzero(&rtinfo, sizeof(struct rt_addrinfo));
		rtinfo.rti_info[RTAX_DST] = rt_key(rt);
		rtinfo.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
		rtinfo.rti_info[RTAX_NETMASK] = rt_mask(rt);
		rtinfo.rti_flags = rt->rt_flags;
		rt_missmsg(RTM_LOSING, &rtinfo, rt->rt_flags, 0);
		if (rt->rt_flags & RTF_DYNAMIC) {
			rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
			    rt_mask(rt), rt->rt_flags, NULL);
		}
		inp->inp_route.ro_rt = NULL;
		rtfree(rt);
		/*
		 * A new route can be allocated
		 * the next time output is attempted.
		 */
	}
}

/*
 * After a routing change, flush old routing
 * and allocate a (hopefully) better one.
 */
void
in_rtchange(struct inpcb *inp, int err)
{
	if (inp->inp_route.ro_rt) {
		rtfree(inp->inp_route.ro_rt);
		inp->inp_route.ro_rt = NULL;
		/*
		 * A new route can be allocated the next time
		 * output is attempted.
		 */
	}
}

/*
 * Lookup a PCB based on the local address and port.
 *
 * This function is only used when scanning for a free port.
 */
static struct inpcb *
in_pcblookup_local(struct inpcbporthead *porthash, struct in_addr laddr,
		   u_int lport_arg, int wild_okay, struct ucred *cred)
{
	struct prison *pscan;
	struct prison *pr;
	struct inpcb *inp;
	int matchwild = 3, wildcard;
	u_short lport = lport_arg;
	struct inpcbport *phd;
	struct inpcb *match = NULL;

	/*
	 * If the porthashbase is shared across several cpus, it must
	 * have been locked.
	 */
	ASSERT_PORTHASH_TOKEN_HELD(porthash);

	/*
	 * Best fit PCB lookup.
	 *
	 * First see if this local port is in use by looking on the
	 * port hash list.
	 */
	LIST_FOREACH(phd, porthash, phd_hash) {
		if (phd->phd_port == lport)
			break;
	}
	if (phd != NULL) {
		pr = cred ? cred->cr_prison : NULL;

		/*
		 * Port is in use by one or more PCBs. Look for best
		 * fit.
		 *
		 * If in a prison we may wish to allow the jail to override
		 * a wildcard listen on the host.  Since the jail forces its
		 * own wildcard listens to a specific set of jail IPs, this
		 * override allows most services on the host to remain as
		 * they were and still be 'jail friendly'.
		 */
		LIST_FOREACH(inp, &phd->phd_pcblist, inp_portlist) {
			wildcard = 0;
#ifdef INET6
			if (!INP_ISIPV4(inp))
				continue;
#endif
			if (inp->inp_faddr.s_addr != INADDR_ANY)
				wildcard++;

			/*
			 * Prison are independent of each other in terms
			 * of allowing bindings.  This can result in multiple
			 * overloaded bindings which in_pcblookup_pkthash()
			 * will have to sort out.
			 *
			 * Allow wildcarded entries to co-exist with specific
			 * entries.  Specific entries override wildcarded
			 * entries.
			 */
			if (inp->inp_socket && inp->inp_socket->so_cred)
				pscan = inp->inp_socket->so_cred->cr_prison;
			else
				pscan = NULL;
			if (pr != pscan)
				continue;
			if (inp->inp_laddr.s_addr == INADDR_ANY) {
				if (laddr.s_addr != INADDR_ANY)
					wildcard++;
			} else {
				if (laddr.s_addr == INADDR_ANY)
					wildcard++;
				else if (inp->inp_laddr.s_addr != laddr.s_addr)
					continue;
			}
			if (wildcard && !wild_okay)
				continue;
			if (wildcard < matchwild) {
				match = inp;
				matchwild = wildcard;
				if (matchwild == 0)
					break;
			}
		}
	}
	return (match);
}

struct inpcb *
in_pcblocalgroup_last(const struct inpcbinfo *pcbinfo,
    const struct inpcb *inp)
{
	const struct inp_localgrphead *hdr;
	const struct inp_localgroup *grp;
	int i;

	if (pcbinfo->localgrphashbase == NULL)
		return NULL;

	GET_PCBINFO_TOKEN(pcbinfo);

	hdr = &pcbinfo->localgrphashbase[
	    INP_PCBLOCALGRPHASH(inp->inp_lport, pcbinfo->localgrphashmask)];

	LIST_FOREACH(grp, hdr, il_list) {
		if (grp->il_af == inp->inp_af &&
		    grp->il_lport == inp->inp_lport &&
		    memcmp(&grp->il_dependladdr,
			&inp->inp_inc.inc_ie.ie_dependladdr,
			sizeof(grp->il_dependladdr)) == 0) {
			break;
		}
	}
	if (grp == NULL || grp->il_inpcnt == 1) {
		REL_PCBINFO_TOKEN(pcbinfo);
		return NULL;
	}

	KASSERT(grp->il_inpcnt >= 2,
	    ("invalid localgroup inp count %d", grp->il_inpcnt));
	for (i = 0; i < grp->il_inpcnt; ++i) {
		if (grp->il_inp[i] == inp) {
			int last = grp->il_inpcnt - 1;

			if (i == last)
				last = grp->il_inpcnt - 2;
			REL_PCBINFO_TOKEN(pcbinfo);
			return grp->il_inp[last];
		}
	}
	REL_PCBINFO_TOKEN(pcbinfo);
	return NULL;
}

static struct inpcb *
inp_localgroup_lookup(const struct inpcbinfo *pcbinfo,
    struct in_addr laddr, uint16_t lport, uint32_t pkt_hash)
{
	struct inpcb *local_wild;
	struct inpcb *jinp;
	struct inpcb *jinp_wild;
	struct inpcb *inp;
	const struct inp_localgrphead *hdr;
	const struct inp_localgroup *grp;
	struct sockaddr_in jsin;
	struct prison *pr;
	struct ucred *cred;
	int idx;
	int net_listen_ov_local;
	int net_listen_ov_wild;

	ASSERT_PCBINFO_TOKEN_HELD(pcbinfo);

	hdr = &pcbinfo->localgrphashbase[
	    INP_PCBLOCALGRPHASH(lport, pcbinfo->localgrphashmask)];

	/*
	 * Order of socket selection:
	 * 1. non-wild.
	 * 2. wild.
	 *
	 * NOTE: Local group does not contain jailed sockets
	 */
	jsin.sin_family = AF_INET;
	jsin.sin_addr.s_addr = laddr.s_addr;

	jinp = NULL;
	jinp_wild = NULL;
	local_wild = NULL;
	net_listen_ov_local = 0;
	net_listen_ov_wild = 0;

	LIST_FOREACH(grp, hdr, il_list) {
#ifdef INET6
		if (grp->il_af != AF_INET)
			continue;
#endif
		if (grp->il_lport != lport)
			continue;

		/*
		 * look for a match
		 */
		idx = netisr_hashlsb(pkt_hash) % grp->il_inpcnt;
		inp = grp->il_inp[idx];

		/*
		 * Modulo-N is used here, which greatly reduces
		 * completion queue token contention, thus more
		 * cpu time is saved.
		 */
		if (grp->il_jailed) {
			if (inp->inp_socket == NULL)
				continue;
			cred = inp->inp_socket->so_cred;
			if (cred == NULL)
				continue;
			pr = cred->cr_prison;
			if (pr == NULL)
				continue;
			if (!jailed_ip(pr, (struct sockaddr *)&jsin))
				continue;
			if (grp->il_laddr.s_addr == laddr.s_addr) {
				jinp = inp;
				if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_NET_LISTEN_OVERRIDE))
					net_listen_ov_local = 1;

			} else if (grp->il_laddr.s_addr == INADDR_ANY &&
				   jinp_wild == NULL) {
				jinp_wild = inp;
				if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_NET_LISTEN_OVERRIDE))
					net_listen_ov_wild = 1;
			}
		} else {
			if (grp->il_laddr.s_addr == laddr.s_addr) {
				return inp;
			} else if (grp->il_laddr.s_addr == INADDR_ANY) {
				local_wild = inp;
			}
		}
	}

	if (net_listen_ov_local)
		return jinp;
	if (net_listen_ov_wild)
		return jinp_wild;
	if (local_wild)
		return (local_wild);
	if (jinp)
		return (jinp);
	return (jinp_wild);
}

/*
 * Lookup PCB in hash list.
 *
 * This is used to match incoming packets to a pcb
 */
struct inpcb *
in_pcblookup_pkthash(struct inpcbinfo *pcbinfo, struct in_addr faddr,
    u_int fport_arg, struct in_addr laddr, u_int lport_arg,
    boolean_t wildcard, struct ifnet *ifp, const struct mbuf *m)
{
	struct inpcbhead *head;
	struct inpcb *inp, *jinp=NULL;
	u_short fport = fport_arg, lport = lport_arg;

	/*
	 * First look for an exact match.
	 */
	head = &pcbinfo->hashbase[INP_PCBCONNHASH(faddr.s_addr, fport,
						  laddr.s_addr, lport,
						  pcbinfo->hashmask)];
	LIST_FOREACH(inp, head, inp_hash) {
#ifdef INET6
		if (!INP_ISIPV4(inp))
			continue;
#endif
		if (in_hosteq(inp->inp_faddr, faddr) &&
		    in_hosteq(inp->inp_laddr, laddr) &&
		    inp->inp_fport == fport && inp->inp_lport == lport) {
			/*
			 * Found specific address, host overrides jailed
			 * inpcb.
			 */
			if (inp->inp_socket == NULL ||
			    inp->inp_socket->so_cred->cr_prison == NULL) {
				return (inp);
			}
			if (jinp == NULL)
				jinp = inp;
		}
	}
	if (jinp != NULL)
		return (jinp);

	/*
	 * We generally get here for connections to wildcarded listeners.
	 * Any wildcarded listeners in jails must be restricted to the
	 * jailed IPs only.
	 */
	if (wildcard) {
		struct inpcb *local_wild = NULL;
		struct inpcb *jinp_wild = NULL;
		struct inpcontainer *ic;
		struct inpcontainerhead *chead;
		struct sockaddr_in jsin;
		struct ucred *cred;
		struct prison *pr;
		int net_listen_ov_local = 0;
		int net_listen_ov_wild = 0;

		GET_PCBINFO_TOKEN(pcbinfo);

		/*
		 * Check local group first.  When present, the localgroup
		 * hash utilizes the same non-jailed-vs/jailed priortization
		 * that the normal wildcardhash does.
		 */
		if (pcbinfo->localgrphashbase != NULL &&
		    m != NULL && (m->m_flags & M_HASH)) {
			inp = inp_localgroup_lookup(pcbinfo, laddr, lport,
						    m->m_pkthdr.hash);
			if (inp != NULL) {
				REL_PCBINFO_TOKEN(pcbinfo);
				return inp;
			}
		}

		/*
		 * Order of socket selection:
		 *
		 * 1. non-jailed, non-wild.
		 * 2. non-jailed, wild.		(allow_listen_override on)
		 * 3. jailed, non-wild.
		 * 4. jailed, wild.
		 * 5. non-jailed, wild.		(allow_listen_override off)
		 *
		 * NOTE: jailed wildcards are still restricted to the jail
		 *	 IPs.
		 *
		 * NOTE: (1) and (3) already handled above.
		 */
		jsin.sin_family = AF_INET;
		chead = &pcbinfo->wildcardhashbase[
		    INP_PCBWILDCARDHASH(lport, pcbinfo->wildcardhashmask)];

		LIST_FOREACH(ic, chead, ic_list) {
			inp = ic->ic_inp;
			if (inp->inp_flags & INP_PLACEMARKER)
				continue;

			/*
			 * Basic validation
			 */
#ifdef INET6
			if (!INP_ISIPV4(inp))
				continue;
#endif
			if (inp->inp_lport != lport)
				continue;

			/*
			 * Calculate prison, setup jsin for jailed_ip()
			 * check.
			 */
			jsin.sin_addr.s_addr = laddr.s_addr;
			pr = NULL;
			cred = NULL;
			if (inp->inp_socket) {
				cred = inp->inp_socket->so_cred;
				if (cred)
					pr = cred->cr_prison;
			}

			/*
			 * Assign jinp, jinp_wild, and local_wild as
			 * appropriate, track whether the jail supports
			 * listen overrides.
			 */
			if (pr) {
				if (!jailed_ip(pr, (struct sockaddr *)&jsin))
					continue;
				if (inp->inp_laddr.s_addr == laddr.s_addr &&
				    jinp == NULL) {
					jinp = inp;
					if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_NET_LISTEN_OVERRIDE))
						net_listen_ov_local = 1;
				}
				if (inp->inp_laddr.s_addr == INADDR_ANY &&
				    jinp_wild == NULL) {
					jinp_wild = inp;
					if (PRISON_CAP_ISSET(pr->pr_caps, PRISON_CAP_NET_LISTEN_OVERRIDE))
						net_listen_ov_wild = 1;
				}
			} else {
				if (inp->inp_laddr.s_addr == laddr.s_addr) {
					REL_PCBINFO_TOKEN(pcbinfo);
					return (inp);
				}
				if (inp->inp_laddr.s_addr == INADDR_ANY)
					local_wild = inp;
			}
		}

		REL_PCBINFO_TOKEN(pcbinfo);

		if (net_listen_ov_local)
			return jinp;
		if (net_listen_ov_wild)
			return jinp_wild;
		if (local_wild)
			return (local_wild);
		if (jinp)
			return (jinp);
		return (jinp_wild);
	}

	/*
	 * Not found.
	 */
	return (NULL);
}

struct inpcb *
in_pcblookup_hash(struct inpcbinfo *pcbinfo, struct in_addr faddr,
    u_int fport_arg, struct in_addr laddr, u_int lport_arg,
    boolean_t wildcard, struct ifnet *ifp)
{
	return in_pcblookup_pkthash(pcbinfo, faddr, fport_arg,
	    laddr, lport_arg, wildcard, ifp, NULL);
}

/*
 * Insert PCB into connection hash table.
 */
void
in_pcbinsconnhash(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbhead *bucket;
	u_int32_t hashkey_faddr, hashkey_laddr;

#ifdef INET6
	if (INP_ISIPV6(inp)) {
		hashkey_faddr = inp->in6p_faddr.s6_addr32[3] /* XXX JH */;
		hashkey_laddr = inp->in6p_laddr.s6_addr32[3] /* XXX JH */;
	} else {
#endif
		hashkey_faddr = inp->inp_faddr.s_addr;
		hashkey_laddr = inp->inp_laddr.s_addr;
#ifdef INET6
	}
#endif

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in the correct netisr"));
	ASSERT_INP_NOTINHASH(inp);
	inp->inp_flags |= INP_CONNECTED;

	/*
	 * Insert into the connection hash table.
	 */
	bucket = &pcbinfo->hashbase[INP_PCBCONNHASH(hashkey_faddr,
	    inp->inp_fport, hashkey_laddr, inp->inp_lport, pcbinfo->hashmask)];
	LIST_INSERT_HEAD(bucket, inp, inp_hash);
}

/*
 * Remove PCB from connection hash table.
 */
void
in_pcbremconnhash(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo __debugvar = inp->inp_pcbinfo;

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in the correct netisr"));
	KASSERT(inp->inp_flags & INP_CONNECTED, ("inp not connected"));

	LIST_REMOVE(inp, inp_hash);
	inp->inp_flags &= ~INP_CONNECTED;
}

/*
 * Insert PCB into port hash table.
 */
void
in_pcbinsporthash(struct inpcbporthead *pcbporthash, struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbport *phd;

	/*
	 * If the porthashbase is shared across several cpus, it must
	 * have been locked.
	 */
	ASSERT_PORTHASH_TOKEN_HELD(pcbporthash);

	/*
	 * Insert into the port hash table.
	 */

	/* Go through port list and look for a head for this lport. */
	LIST_FOREACH(phd, pcbporthash, phd_hash) {
		if (phd->phd_port == inp->inp_lport)
			break;
	}

	/* If none exists, use saved one and tack it on. */
	if (phd == NULL) {
		KKASSERT(pcbinfo->portsave != NULL);
		phd = pcbinfo->portsave;
		pcbinfo->portsave = NULL;
		phd->phd_port = inp->inp_lport;
		LIST_INIT(&phd->phd_pcblist);
		LIST_INSERT_HEAD(pcbporthash, phd, phd_hash);
	}

	inp->inp_porthash = pcbporthash;
	inp->inp_phd = phd;
	LIST_INSERT_HEAD(&phd->phd_pcblist, inp, inp_portlist);

	/*
	 * Malloc one inpcbport for later use.  It is safe to use
	 * "wait" malloc here (port token would be released, if
	 * malloc ever blocked), since all changes to the porthash
	 * are done.
	 */
	if (pcbinfo->portsave == NULL) {
		pcbinfo->portsave = kmalloc(sizeof(*pcbinfo->portsave),
					    M_PCB, M_INTWAIT | M_ZERO);
	}
}

void
in_pcbinsporthash_lport(struct inpcb *inp)
{
	struct inpcbporthead *porthash;

	porthash = OBTAIN_LPORTHASH_TOKEN(inp->inp_pcbinfo, inp->inp_lport);
	in_pcbinsporthash(porthash, inp);
	REL_PORTHASH_TOKEN(porthash);
}

void
in_pcbremporthash(struct inpcb *inp)
{
	struct inpcbporthead *porthash;
	struct inpcbport *phd;

	if (inp->inp_phd == NULL)
		return;
	KASSERT(inp->inp_lport != 0, ("inpcb has no lport"));

	porthash = inp->inp_porthash;
	KASSERT(porthash != NULL, ("no porthash"));

	GET_PORTHASH_TOKEN(porthash);

	phd = inp->inp_phd;
	LIST_REMOVE(inp, inp_portlist);
	if (LIST_FIRST(&phd->phd_pcblist) == NULL) {
		LIST_REMOVE(phd, phd_hash);
		kfree(phd, M_PCB);
	}

	REL_PORTHASH_TOKEN(porthash);

	inp->inp_phd = NULL;
	/* NOTE: Don't whack inp_lport, which may be used later */
}

static struct inp_localgroup *
inp_localgroup_alloc(u_char af, uint16_t port,
    const union in_dependaddr *addr, int size)
{
	struct inp_localgroup *grp;

	grp = kmalloc(__offsetof(struct inp_localgroup, il_inp[size]),
		      M_TEMP, M_INTWAIT | M_ZERO);
	grp->il_af = af;
	grp->il_lport = port;
	grp->il_dependladdr = *addr;
	grp->il_inpsiz = size;

	return grp;
}

static void
inp_localgroup_free(struct inp_localgroup *grp)
{
	kfree(grp, M_TEMP);
}

static void
inp_localgroup_destroy(struct inp_localgroup *grp)
{
	LIST_REMOVE(grp, il_list);
	inp_localgroup_free(grp);
}

static void
inp_localgroup_copy(struct inp_localgroup *grp,
		    const struct inp_localgroup *old_grp)
{
	int i;

	KASSERT(old_grp->il_inpcnt < grp->il_inpsiz,
	    ("invalid new local group size %d and old local group count %d",
	     grp->il_inpsiz, old_grp->il_inpcnt));
	for (i = 0; i < old_grp->il_inpcnt; ++i)
		grp->il_inp[i] = old_grp->il_inp[i];
	grp->il_inpcnt = old_grp->il_inpcnt;
}

static void
in_pcbinslocalgrphash_oncpu(struct inpcb *inp, struct inpcbinfo *pcbinfo)
{
	struct inp_localgrphead *hdr;
	struct inp_localgroup *grp, *grp_alloc = NULL;
	u_char isjailed;
	int i, idx;

	ASSERT_PCBINFO_TOKEN_HELD(pcbinfo);

	if (pcbinfo->localgrphashbase == NULL)
		return;

	/*
	 * Further separate groups by whether the inp is jailed or not.
	 * This allows the inp_localgroup_lookup() code to manage port
	 * overloading between jails and non-jails.
	 *
	 * XXX all jails are collected into one group, which works fine
	 *     as we expect the jails to be listening on different addresses.
	 *     If this changes in the future we may have to break the groups
	 *     up by prison pointer as well.
	 */
	if (inp->inp_socket && inp->inp_socket->so_cred)
		isjailed = jailed(inp->inp_socket->so_cred);
	else
		isjailed = 0;

	hdr = &pcbinfo->localgrphashbase[
	    INP_PCBLOCALGRPHASH(inp->inp_lport, pcbinfo->localgrphashmask)];

again:
	LIST_FOREACH(grp, hdr, il_list) {
		if (grp->il_af == inp->inp_af &&
		    grp->il_lport == inp->inp_lport &&
		    grp->il_jailed == isjailed &&
		    memcmp(&grp->il_dependladdr,
			   &inp->inp_inc.inc_ie.ie_dependladdr,
			   sizeof(grp->il_dependladdr)) == 0) {
			break;
		}
	}
	if (grp == NULL) {
		/*
		 * Create a new local group
		 */
		if (grp_alloc == NULL) {
			grp_alloc = inp_localgroup_alloc(inp->inp_af,
			    inp->inp_lport, &inp->inp_inc.inc_ie.ie_dependladdr,
			    INP_LOCALGROUP_SIZMIN);
			/*
			 * Local group allocation could block and the
			 * local group w/ the same property might have
			 * been added by others when we were blocked;
			 * check again.
			 */
			goto again;
		} else {
			/* Local group has been allocated; link it */
			grp = grp_alloc;
			grp->il_jailed = isjailed;
			grp_alloc = NULL;
			LIST_INSERT_HEAD(hdr, grp, il_list);
		}
	} else if (grp->il_inpcnt == grp->il_inpsiz) {
#if 0
		/*
		 * REMOVED - Ensure that all entries are placed in the
		 *	     localgroup so jail operations can be
		 *	     deterministic on a il_lport basis.
		 */
		if (grp->il_inpsiz >= INP_LOCALGROUP_SIZMAX) {
			static int limit_logged = 0;

			if (!limit_logged) {
				limit_logged = 1;
				kprintf("local group port %d, "
				    "limit reached\n", ntohs(grp->il_lport));
			}
			if (grp_alloc != NULL) {
				/*
				 * This would happen if the local group
				 * w/ the same property was expanded when
				 * our local group allocation blocked.
				 */
				inp_localgroup_free(grp_alloc);
			}
			return;
		}
#endif

		/*
		 * Expand this local group
		 */
		if (grp_alloc == NULL ||
		    grp->il_inpcnt >= grp_alloc->il_inpsiz) {
			if (grp_alloc != NULL)
				inp_localgroup_free(grp_alloc);
			grp_alloc = inp_localgroup_alloc(grp->il_af,
			    grp->il_lport, &grp->il_dependladdr,
			    grp->il_inpsiz * 2);
			/*
			 * Local group allocation could block and the
			 * local group w/ the same property might have
			 * been expanded by others when we were blocked;
			 * check again.
			 */
			goto again;
		}

		/*
		 * Save the old local group, link the new one, and then
		 * destroy the old local group
		 */
		inp_localgroup_copy(grp_alloc, grp);
		LIST_INSERT_HEAD(hdr, grp_alloc, il_list);
		inp_localgroup_destroy(grp);

		grp = grp_alloc;
		grp->il_jailed = isjailed;
		grp_alloc = NULL;
	} else {
		/*
		 * Found the local group
		 */
		if (grp_alloc != NULL) {
			/*
			 * This would happen if the local group w/ the
			 * same property was added or expanded when our
			 * local group allocation blocked.
			 */
			inp_localgroup_free(grp_alloc);
			grp_alloc = NULL;
		}
	}

	KASSERT(grp->il_inpcnt < grp->il_inpsiz,
	    ("invalid local group size %d and count %d",
	     grp->il_inpsiz, grp->il_inpcnt));

	/*
	 * Keep the local group sorted by the inpcb local group index
	 * in ascending order.
	 *
	 * This eases the multi-process userland application which uses
	 * SO_REUSEPORT sockets and binds process to the owner cpu of
	 * the SO_REUSEPORT socket:
	 * If we didn't sort the local group by the inpcb local group
	 * index and one of the process owning an inpcb in this local
	 * group restarted, e.g. crashed and restarted by watchdog,
	 * other processes owning a inpcb in this local group would have
	 * to detect that event, refetch its socket's owner cpu, and
	 * re-bind.
	 */
	idx = grp->il_inpcnt;
	for (i = 0; i < idx; ++i) {
		struct inpcb *oinp = grp->il_inp[i];

		if (oinp->inp_lgrpindex > i) {
			if (inp->inp_lgrpindex < 0) {
				inp->inp_lgrpindex = i;
			} else if (inp->inp_lgrpindex != i) {
				if (bootverbose) {
					kprintf("inp %p: grpidx %d, "
					    "assigned to %d, cpu%d\n",
					    inp, inp->inp_lgrpindex, i,
					    mycpuid);
				}
			}
			grp->il_inp[i] = inp;

			/* Pull down inpcbs */
			for (; i < grp->il_inpcnt; ++i) {
				struct inpcb *oinp1 = grp->il_inp[i + 1];

				grp->il_inp[i + 1] = oinp;
				oinp = oinp1;
			}
			grp->il_inpcnt++;
			return;
		}
	}

	if (inp->inp_lgrpindex < 0) {
		inp->inp_lgrpindex = idx;
	} else if (inp->inp_lgrpindex != idx) {
		if (bootverbose) {
			kprintf("inp %p: grpidx %d, assigned to %d, cpu%d\n",
			    inp, inp->inp_lgrpindex, idx, mycpuid);
		}
	}
	grp->il_inp[idx] = inp;
	grp->il_inpcnt++;
}

void
in_pcbinswildcardhash_oncpu(struct inpcb *inp, struct inpcbinfo *pcbinfo)
{
	struct inpcontainer *ic;
	struct inpcontainerhead *bucket;

	GET_PCBINFO_TOKEN(pcbinfo);

	in_pcbinslocalgrphash_oncpu(inp, pcbinfo);

	bucket = &pcbinfo->wildcardhashbase[
	    INP_PCBWILDCARDHASH(inp->inp_lport, pcbinfo->wildcardhashmask)];

	ic = kmalloc(sizeof(struct inpcontainer), M_TEMP, M_INTWAIT);
	ic->ic_inp = inp;
	LIST_INSERT_HEAD(bucket, ic, ic_list);

	REL_PCBINFO_TOKEN(pcbinfo);
}

/*
 * Insert PCB into wildcard hash table.
 */
void
in_pcbinswildcardhash(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in correct netisr"));
	ASSERT_INP_NOTINHASH(inp);
	inp->inp_flags |= INP_WILDCARD;

	in_pcbinswildcardhash_oncpu(inp, pcbinfo);
}

static void
in_pcbremlocalgrphash_oncpu(struct inpcb *inp, struct inpcbinfo *pcbinfo)
{
	struct inp_localgrphead *hdr;
	struct inp_localgroup *grp;

	ASSERT_PCBINFO_TOKEN_HELD(pcbinfo);

	if (pcbinfo->localgrphashbase == NULL)
		return;

	hdr = &pcbinfo->localgrphashbase[
	    INP_PCBLOCALGRPHASH(inp->inp_lport, pcbinfo->localgrphashmask)];

	LIST_FOREACH(grp, hdr, il_list) {
		int i;

		for (i = 0; i < grp->il_inpcnt; ++i) {
			if (grp->il_inp[i] != inp)
				continue;

			if (grp->il_inpcnt == 1) {
				/* Destroy this local group */
				inp_localgroup_destroy(grp);
			} else {
				/* Pull up inpcbs */
				for (; i + 1 < grp->il_inpcnt; ++i)
					grp->il_inp[i] = grp->il_inp[i + 1];
				grp->il_inpcnt--;
			}
			return;
		}
	}
}

void
in_pcbremwildcardhash_oncpu(struct inpcb *inp, struct inpcbinfo *pcbinfo)
{
	struct inpcontainer *ic;
	struct inpcontainerhead *head;

	GET_PCBINFO_TOKEN(pcbinfo);

	in_pcbremlocalgrphash_oncpu(inp, pcbinfo);

	/* find bucket */
	head = &pcbinfo->wildcardhashbase[
	    INP_PCBWILDCARDHASH(inp->inp_lport, pcbinfo->wildcardhashmask)];

	LIST_FOREACH(ic, head, ic_list) {
		if (ic->ic_inp == inp)
			goto found;
	}
	REL_PCBINFO_TOKEN(pcbinfo);
	return;			/* not found! */

found:
	LIST_REMOVE(ic, ic_list);	/* remove container from bucket chain */
	REL_PCBINFO_TOKEN(pcbinfo);
	kfree(ic, M_TEMP);		/* deallocate container */
}

/*
 * Remove PCB from wildcard hash table.
 */
void
in_pcbremwildcardhash(struct inpcb *inp)
{
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;

	KASSERT(&curthread->td_msgport == netisr_cpuport(pcbinfo->cpu),
	    ("not in correct netisr"));
	KASSERT(inp->inp_flags & INP_WILDCARD, ("inp not wildcard"));

	in_pcbremwildcardhash_oncpu(inp, pcbinfo);
	inp->inp_lgrpindex = -1;
	inp->inp_flags &= ~INP_WILDCARD;
}

/*
 * Remove PCB from various lists.
 */
void
in_pcbremlists(struct inpcb *inp)
{
	in_pcbremporthash(inp);
	if (inp->inp_flags & INP_WILDCARD) {
		in_pcbremwildcardhash(inp);
	} else if (inp->inp_flags & INP_CONNECTED) {
		in_pcbremconnhash(inp);
	}

	if (inp->inp_flags & INP_ONLIST)
		in_pcbofflist(inp);
}

int
prison_xinpcb(struct thread *td, struct inpcb *inp)
{
	struct ucred *cr;

	if (td->td_proc == NULL)
		return (0);
	cr = td->td_proc->p_ucred;
	if (cr->cr_prison == NULL)
		return (0);
	if (inp->inp_socket && inp->inp_socket->so_cred &&
	    inp->inp_socket->so_cred->cr_prison &&
	    cr->cr_prison == inp->inp_socket->so_cred->cr_prison)
		return (0);
	return (1);
}

int
in_pcblist_range(SYSCTL_HANDLER_ARGS)
{
	struct inpcbinfo *pcbinfo_arr = arg1;
	int pcbinfo_arrlen = arg2;
	struct inpcb *marker;
	int cpu, origcpu;
	int error, n;

	KASSERT(pcbinfo_arrlen <= netisr_ncpus && pcbinfo_arrlen >= 1,
	    ("invalid pcbinfo count %d", pcbinfo_arrlen));

	/*
	 * The process of preparing the TCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	n = 0;
	if (req->oldptr == NULL) {
		for (cpu = 0; cpu < pcbinfo_arrlen; ++cpu)
			n += pcbinfo_arr[cpu].ipi_count;
		req->oldidx = (n + n/8 + 10) * sizeof(struct xinpcb);
		return 0;
	}

	if (req->newptr != NULL)
		return EPERM;

	marker = kmalloc(sizeof(struct inpcb), M_TEMP, M_WAITOK|M_ZERO);
	marker->inp_flags |= INP_PLACEMARKER;

	/*
	 * OK, now we're committed to doing something.  Re-fetch ipi_count
	 * after obtaining the generation count.
	 */
	error = 0;
	origcpu = mycpuid;
	for (cpu = 0; cpu < pcbinfo_arrlen && error == 0; ++cpu) {
		struct inpcbinfo *pcbinfo = &pcbinfo_arr[cpu];
		struct inpcb *inp;
		struct xinpcb xi;
		int i;

		lwkt_migratecpu(cpu);

		GET_PCBINFO_TOKEN(pcbinfo);

		n = pcbinfo->ipi_count;

		LIST_INSERT_HEAD(&pcbinfo->pcblisthead, marker, inp_list);
		i = 0;
		while ((inp = LIST_NEXT(marker, inp_list)) != NULL && i < n) {
			LIST_REMOVE(marker, inp_list);
			LIST_INSERT_AFTER(inp, marker, inp_list);

			if (inp->inp_flags & INP_PLACEMARKER)
				continue;
			if (prison_xinpcb(req->td, inp))
				continue;

			bzero(&xi, sizeof xi);
			xi.xi_len = sizeof xi;
			bcopy(inp, &xi.xi_inp, sizeof *inp);
			if (inp->inp_socket)
				sotoxsocket(inp->inp_socket, &xi.xi_socket);
			if ((error = SYSCTL_OUT(req, &xi, sizeof xi)) != 0)
				break;
			++i;
		}
		LIST_REMOVE(marker, inp_list);

		REL_PCBINFO_TOKEN(pcbinfo);

		if (error == 0 && i < n) {
			bzero(&xi, sizeof xi);
			xi.xi_len = sizeof xi;
			while (i < n) {
				error = SYSCTL_OUT(req, &xi, sizeof xi);
				if (error)
					break;
				++i;
			}
		}
	}

	lwkt_migratecpu(origcpu);
	kfree(marker, M_TEMP);
	return error;
}

int
in_pcblist_ncpus(SYSCTL_HANDLER_ARGS)
{

	return (in_pcblist_range(oidp, arg1, netisr_ncpus, req));
}

void
in_savefaddr(struct socket *so, const struct sockaddr *faddr)
{
	struct sockaddr_in *sin;

	KASSERT(faddr->sa_family == AF_INET,
	    ("not AF_INET faddr %d", faddr->sa_family));

	sin = kmalloc(sizeof(*sin), M_SONAME, M_WAITOK | M_ZERO);
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_port = ((const struct sockaddr_in *)faddr)->sin_port;
	sin->sin_addr = ((const struct sockaddr_in *)faddr)->sin_addr;

	so->so_faddr = (struct sockaddr *)sin;
}

void
in_pcbportinfo_init(struct inpcbportinfo *portinfo, int hashsize,
    u_short offset)
{
	memset(portinfo, 0, sizeof(*portinfo));

	portinfo->offset = offset;
	portinfo->porthashbase = phashinit(hashsize, M_PCB,
	    &portinfo->porthashcnt);
}

void
in_pcbportrange(u_short *hi0, u_short *lo0, u_short ofs, u_short step)
{
	int hi, lo;

	if (step == 1)
		return;

	hi = *hi0;
	lo = *lo0;

	hi = rounddown(hi, step);
	hi += ofs;
	if (hi > (int)*hi0)
		hi -= step;

	lo = roundup(lo, step);
	lo -= (step - ofs);
	if (lo < (int)*lo0)
		lo += step;

	*hi0 = hi;
	*lo0 = lo;
}

void
in_pcbglobalinit(void)
{
	int cpu;

	in_pcbmarkers = kmalloc(netisr_ncpus * sizeof(struct inpcb), M_PCB,
	    M_WAITOK | M_ZERO);
	in_pcbcontainer_markers =
	    kmalloc(netisr_ncpus * sizeof(struct inpcontainer), M_PCB,
	    M_WAITOK | M_ZERO);

	for (cpu = 0; cpu < netisr_ncpus; ++cpu) {
		struct inpcontainer *ic = &in_pcbcontainer_markers[cpu];
		struct inpcb *marker = &in_pcbmarkers[cpu];

		marker->inp_flags |= INP_PLACEMARKER;
		ic->ic_inp = marker;
	}
}

struct inpcb *
in_pcbmarker(void)
{

	ASSERT_NETISR_NCPUS(mycpuid);
	return &in_pcbmarkers[mycpuid];
}

struct inpcontainer *
in_pcbcontainer_marker(void)
{

	ASSERT_NETISR_NCPUS(mycpuid);
	return &in_pcbcontainer_markers[mycpuid];
}

void
in_pcbresetroute(struct inpcb *inp)
{
	struct route *ro = &inp->inp_route;

	if (ro->ro_rt != NULL)
		RTFREE(ro->ro_rt);
	bzero(ro, sizeof(*ro));
}
