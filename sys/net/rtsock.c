/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1988, 1991, 1993
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
 *	@(#)rtsock.c	8.7 (Berkeley) 10/12/95
 * $FreeBSD: src/sys/net/rtsock.c,v 1.44.2.11 2002/12/04 14:05:41 ru Exp $
 */

#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/raw_cb.h>
#include <net/netmsg2.h>

#ifdef SCTP
extern void sctp_add_ip_address(struct ifaddr *ifa);
extern void sctp_delete_ip_address(struct ifaddr *ifa);
#endif /* SCTP */

MALLOC_DEFINE(M_RTABLE, "routetbl", "routing tables");

static struct route_cb {
	int	ip_count;
	int	ip6_count;
	int	ipx_count;
	int	ns_count;
	int	any_count;
} route_cb;

static const struct sockaddr route_src = { 2, PF_ROUTE, };

struct walkarg {
	int	w_tmemsize;
	int	w_op, w_arg;
	void	*w_tmem;
	struct sysctl_req *w_req;
};

static struct mbuf *
		rt_msg_mbuf (int, struct rt_addrinfo *);
static void	rt_msg_buffer (int, struct rt_addrinfo *, void *buf, int len);
static int	rt_msgsize (int type, struct rt_addrinfo *rtinfo);
static int	rt_xaddrs (char *, char *, struct rt_addrinfo *);
static int	sysctl_dumpentry (struct radix_node *rn, void *vw);
static int	sysctl_iflist (int af, struct walkarg *w);
static int	route_output(struct mbuf *, struct socket *, ...);
static void	rt_setmetrics (u_long, struct rt_metrics *,
			       struct rt_metrics *);

/*
 * It really doesn't make any sense at all for this code to share much
 * with raw_usrreq.c, since its functionality is so restricted.  XXX
 */
static void
rts_abort(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_abort(msg);
	/* msg invalid now */
	crit_exit();
}

/* pru_accept is EOPNOTSUPP */

static void
rts_attach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct rawcb *rp;
	int proto = msg->attach.nm_proto;
	int error;

	crit_enter();
	if (sotorawcb(so) != NULL) {
		error = EISCONN;
		goto done;
	}

	rp = kmalloc(sizeof *rp, M_PCB, M_WAITOK | M_ZERO);

	/*
	 * The critical section is necessary to block protocols from sending
	 * error notifications (like RTM_REDIRECT or RTM_LOSING) while
	 * this PCB is extant but incompletely initialized.
	 * Probably we should try to do more of this work beforehand and
	 * eliminate the critical section.
	 */
	so->so_pcb = rp;
	soreference(so);	/* so_pcb assignment */
	error = raw_attach(so, proto, ai->sb_rlimit);
	rp = sotorawcb(so);
	if (error) {
		kfree(rp, M_PCB);
		goto done;
	}
	switch(rp->rcb_proto.sp_protocol) {
	case AF_INET:
		route_cb.ip_count++;
		break;
	case AF_INET6:
		route_cb.ip6_count++;
		break;
	case AF_IPX:
		route_cb.ipx_count++;
		break;
	case AF_NS:
		route_cb.ns_count++;
		break;
	}
	rp->rcb_faddr = &route_src;
	route_cb.any_count++;
	soisconnected(so);
	so->so_options |= SO_USELOOPBACK;
	error = 0;
done:
	crit_exit();
	lwkt_replymsg(&msg->lmsg, error);
}

static void
rts_bind(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_bind(msg); /* xxx just EINVAL */
	/* msg invalid now */
	crit_exit();
}

static void
rts_connect(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_connect(msg); /* XXX just EINVAL */
	/* msg invalid now */
	crit_exit();
}

/* pru_connect2 is EOPNOTSUPP */
/* pru_control is EOPNOTSUPP */

static void
rts_detach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct rawcb *rp = sotorawcb(so);

	crit_enter();
	if (rp != NULL) {
		switch(rp->rcb_proto.sp_protocol) {
		case AF_INET:
			route_cb.ip_count--;
			break;
		case AF_INET6:
			route_cb.ip6_count--;
			break;
		case AF_IPX:
			route_cb.ipx_count--;
			break;
		case AF_NS:
			route_cb.ns_count--;
			break;
		}
		route_cb.any_count--;
	}
	raw_usrreqs.pru_detach(msg);
	/* msg invalid now */
	crit_exit();
}

static void
rts_disconnect(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_disconnect(msg);
	/* msg invalid now */
	crit_exit();
}

/* pru_listen is EOPNOTSUPP */

static void
rts_peeraddr(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_peeraddr(msg);
	/* msg invalid now */
	crit_exit();
}

/* pru_rcvd is EOPNOTSUPP */
/* pru_rcvoob is EOPNOTSUPP */

static void
rts_send(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_send(msg);
	/* msg invalid now */
	crit_exit();
}

/* pru_sense is null */

static void
rts_shutdown(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_shutdown(msg);
	/* msg invalid now */
	crit_exit();
}

static void
rts_sockaddr(netmsg_t msg)
{
	crit_enter();
	raw_usrreqs.pru_sockaddr(msg);
	/* msg invalid now */
	crit_exit();
}

static struct pr_usrreqs route_usrreqs = {
	.pru_abort = rts_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = rts_attach,
	.pru_bind = rts_bind,
	.pru_connect = rts_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = pr_generic_notsupp,
	.pru_detach = rts_detach,
	.pru_disconnect = rts_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = rts_peeraddr,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = rts_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = rts_shutdown,
	.pru_sockaddr = rts_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

static __inline sa_family_t
familyof(struct sockaddr *sa)
{
	return (sa != NULL ? sa->sa_family : 0);
}

/*
 * Routing socket input function.  The packet must be serialized onto cpu 0.
 * We use the cpu0_soport() netisr processing loop to handle it.
 *
 * This looks messy but it means that anyone, including interrupt code,
 * can send a message to the routing socket.
 */
static void
rts_input_handler(netmsg_t msg)
{
	static const struct sockaddr route_dst = { 2, PF_ROUTE, };
	struct sockproto route_proto;
	struct netmsg_packet *pmsg = &msg->packet;
	struct mbuf *m;
	sa_family_t family;
	struct rawcb *skip;

	family = pmsg->base.lmsg.u.ms_result;
	route_proto.sp_family = PF_ROUTE;
	route_proto.sp_protocol = family;

	m = pmsg->nm_packet;
	M_ASSERTPKTHDR(m);

	skip = m->m_pkthdr.header;
	m->m_pkthdr.header = NULL;

	raw_input(m, &route_proto, &route_src, &route_dst, skip);
}

static void
rts_input_skip(struct mbuf *m, sa_family_t family, struct rawcb *skip)
{
	struct netmsg_packet *pmsg;
	lwkt_port_t port;

	M_ASSERTPKTHDR(m);

	port = netisr_portfn(0);	/* XXX same as for routing socket */
	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
		    0, rts_input_handler);
	pmsg->nm_packet = m;
	pmsg->base.lmsg.u.ms_result = family;
	m->m_pkthdr.header = skip; /* XXX steal field in pkthdr */
	lwkt_sendmsg(port, &pmsg->base.lmsg);
}

static __inline void
rts_input(struct mbuf *m, sa_family_t family)
{
	rts_input_skip(m, family, NULL);
}

static void *
reallocbuf_nofree(void *ptr, size_t len, size_t olen)
{
	void *newptr;

	newptr = kmalloc(len, M_RTABLE, M_INTWAIT | M_NULLOK);
	if (newptr == NULL)
		return NULL;
	bcopy(ptr, newptr, olen);
	return (newptr);
}

/*
 * Internal helper routine for route_output().
 */
static int
_fillrtmsg(struct rt_msghdr **prtm, struct rtentry *rt,
	   struct rt_addrinfo *rtinfo)
{
	int msglen;
	struct rt_msghdr *rtm = *prtm;

	/* Fill in rt_addrinfo for call to rt_msg_buffer(). */
	rtinfo->rti_dst = rt_key(rt);
	rtinfo->rti_gateway = rt->rt_gateway;
	rtinfo->rti_netmask = rt_mask(rt);		/* might be NULL */
	rtinfo->rti_genmask = rt->rt_genmask;		/* might be NULL */
	if (rtm->rtm_addrs & (RTA_IFP | RTA_IFA)) {
		if (rt->rt_ifp != NULL) {
			rtinfo->rti_ifpaddr =
			    TAILQ_FIRST(&rt->rt_ifp->if_addrheads[mycpuid])
			    ->ifa->ifa_addr;
			rtinfo->rti_ifaaddr = rt->rt_ifa->ifa_addr;
			if (rt->rt_ifp->if_flags & IFF_POINTOPOINT)
				rtinfo->rti_bcastaddr = rt->rt_ifa->ifa_dstaddr;
			rtm->rtm_index = rt->rt_ifp->if_index;
		} else {
			rtinfo->rti_ifpaddr = NULL;
			rtinfo->rti_ifaaddr = NULL;
		}
	} else if (rt->rt_ifp != NULL) {
		rtm->rtm_index = rt->rt_ifp->if_index;
	}

	msglen = rt_msgsize(rtm->rtm_type, rtinfo);
	if (rtm->rtm_msglen < msglen) {
		/* NOTE: Caller will free the old rtm accordingly */
		rtm = reallocbuf_nofree(rtm, msglen, rtm->rtm_msglen);
		if (rtm == NULL)
			return (ENOBUFS);
		*prtm = rtm;
	}
	rt_msg_buffer(rtm->rtm_type, rtinfo, rtm, msglen);

	rtm->rtm_flags = rt->rt_flags;
	rtm->rtm_rmx = rt->rt_rmx;
	rtm->rtm_addrs = rtinfo->rti_addrs;

	return (0);
}

struct rtm_arg {
	struct rt_msghdr	*bak_rtm;
	struct rt_msghdr	*new_rtm;
};

static int
fillrtmsg(struct rtm_arg *arg, struct rtentry *rt,
	  struct rt_addrinfo *rtinfo)
{
	struct rt_msghdr *rtm = arg->new_rtm;
	int error;

	error = _fillrtmsg(&rtm, rt, rtinfo);
	if (!error) {
		if (arg->new_rtm != rtm) {
			/*
			 * _fillrtmsg() just allocated a new rtm;
			 * if the previously allocated rtm is not
			 * the backing rtm, it should be freed.
			 */
			if (arg->new_rtm != arg->bak_rtm)
				kfree(arg->new_rtm, M_RTABLE);
			arg->new_rtm = rtm;
		}
	}
	return error;
}

static void route_output_add_callback(int, int, struct rt_addrinfo *,
					struct rtentry *, void *);
static void route_output_delete_callback(int, int, struct rt_addrinfo *,
					struct rtentry *, void *);
static int route_output_get_callback(int, struct rt_addrinfo *,
				     struct rtentry *, void *, int);
static int route_output_change_callback(int, struct rt_addrinfo *,
					struct rtentry *, void *, int);
static int route_output_lock_callback(int, struct rt_addrinfo *,
				      struct rtentry *, void *, int);

/*ARGSUSED*/
static int
route_output(struct mbuf *m, struct socket *so, ...)
{
	struct rtm_arg arg;
	struct rt_msghdr *rtm = NULL;
	struct rawcb *rp = NULL;
	struct pr_output_info *oi;
	struct rt_addrinfo rtinfo;
	sa_family_t family;
	int len, error = 0;
	__va_list ap;

	M_ASSERTPKTHDR(m);

	__va_start(ap, so);
	oi = __va_arg(ap, struct pr_output_info *);
	__va_end(ap);

	family = familyof(NULL);

#define gotoerr(e) { error = e; goto flush;}

	if (m == NULL ||
	    (m->m_len < sizeof(long) &&
	     (m = m_pullup(m, sizeof(long))) == NULL))
		return (ENOBUFS);
	len = m->m_pkthdr.len;
	if (len < sizeof(struct rt_msghdr) ||
	    len != mtod(m, struct rt_msghdr *)->rtm_msglen)
		gotoerr(EINVAL);

	rtm = kmalloc(len, M_RTABLE, M_INTWAIT | M_NULLOK);
	if (rtm == NULL)
		gotoerr(ENOBUFS);

	m_copydata(m, 0, len, (caddr_t)rtm);
	if (rtm->rtm_version != RTM_VERSION)
		gotoerr(EPROTONOSUPPORT);

	rtm->rtm_pid = oi->p_pid;
	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_addrs = rtm->rtm_addrs;
	if (rt_xaddrs((char *)(rtm + 1), (char *)rtm + len, &rtinfo) != 0)
		gotoerr(EINVAL);

	rtinfo.rti_flags = rtm->rtm_flags;
	if (rtinfo.rti_dst == NULL || rtinfo.rti_dst->sa_family >= AF_MAX ||
	    (rtinfo.rti_gateway && rtinfo.rti_gateway->sa_family >= AF_MAX))
		gotoerr(EINVAL);

	family = familyof(rtinfo.rti_dst);

	if (rtinfo.rti_genmask != NULL) {
		error = rtmask_add_global(rtinfo.rti_genmask);
		if (error)
			goto flush;
	}

	/*
	 * Verify that the caller has the appropriate privilege; RTM_GET
	 * is the only operation the non-superuser is allowed.
	 */
	if (rtm->rtm_type != RTM_GET &&
	    priv_check_cred(so->so_cred, PRIV_ROOT, 0) != 0)
		gotoerr(EPERM);

	switch (rtm->rtm_type) {
	case RTM_ADD:
		if (rtinfo.rti_gateway == NULL) {
			error = EINVAL;
		} else {
			error = rtrequest1_global(RTM_ADD, &rtinfo, 
					  route_output_add_callback, rtm);
		}
		break;
	case RTM_DELETE:
		/*
		 * Backing rtm (bak_rtm) could _not_ be freed during
		 * rtrequest1_global or rtsearch_global, even if the
		 * callback reallocates the rtm due to its size changes,
		 * since rtinfo points to the backing rtm's memory area.
		 * After rtrequest1_global or rtsearch_global returns,
		 * it is safe to free the backing rtm, since rtinfo will
		 * not be used anymore.
		 *
		 * new_rtm will be used to save the new rtm allocated
		 * by rtrequest1_global or rtsearch_global.
		 */
		arg.bak_rtm = rtm;
		arg.new_rtm = rtm;
		error = rtrequest1_global(RTM_DELETE, &rtinfo,
					  route_output_delete_callback, &arg);
		rtm = arg.new_rtm;
		if (rtm != arg.bak_rtm)
			kfree(arg.bak_rtm, M_RTABLE);
		break;
	case RTM_GET:
		/* See the comment in RTM_DELETE */
		arg.bak_rtm = rtm;
		arg.new_rtm = rtm;
		error = rtsearch_global(RTM_GET, &rtinfo,
					route_output_get_callback, &arg,
					RTS_NOEXACTMATCH);
		rtm = arg.new_rtm;
		if (rtm != arg.bak_rtm)
			kfree(arg.bak_rtm, M_RTABLE);
		break;
	case RTM_CHANGE:
		error = rtsearch_global(RTM_CHANGE, &rtinfo,
					route_output_change_callback, rtm,
					RTS_EXACTMATCH);
		break;
	case RTM_LOCK:
		error = rtsearch_global(RTM_LOCK, &rtinfo,
					route_output_lock_callback, rtm,
					RTS_EXACTMATCH);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
flush:
	if (rtm != NULL) {
		if (error != 0)
			rtm->rtm_errno = error;
		else
			rtm->rtm_flags |= RTF_DONE;
	}

	/*
	 * Check to see if we don't want our own messages.
	 */
	if (!(so->so_options & SO_USELOOPBACK)) {
		if (route_cb.any_count <= 1) {
			if (rtm != NULL)
				kfree(rtm, M_RTABLE);
			m_freem(m);
			return (error);
		}
		/* There is another listener, so construct message */
		rp = sotorawcb(so);
	}
	if (rtm != NULL) {
		m_copyback(m, 0, rtm->rtm_msglen, (caddr_t)rtm);
		if (m->m_pkthdr.len < rtm->rtm_msglen) {
			m_freem(m);
			m = NULL;
		} else if (m->m_pkthdr.len > rtm->rtm_msglen)
			m_adj(m, rtm->rtm_msglen - m->m_pkthdr.len);
		kfree(rtm, M_RTABLE);
	}
	if (m != NULL)
		rts_input_skip(m, family, rp);
	return (error);
}

static void
route_output_add_callback(int cmd, int error, struct rt_addrinfo *rtinfo,
			  struct rtentry *rt, void *arg)
{
	struct rt_msghdr *rtm = arg;

	if (error == 0 && rt != NULL) {
		rt_setmetrics(rtm->rtm_inits, &rtm->rtm_rmx,
		    &rt->rt_rmx);
		rt->rt_rmx.rmx_locks &= ~(rtm->rtm_inits);
		rt->rt_rmx.rmx_locks |=
		    (rtm->rtm_inits & rtm->rtm_rmx.rmx_locks);
		if (rtinfo->rti_genmask != NULL) {
			rt->rt_genmask = rtmask_purelookup(rtinfo->rti_genmask);
			if (rt->rt_genmask == NULL) {
				/*
				 * This should not happen, since we
				 * have already installed genmask
				 * on each CPU before we reach here.
				 */
				panic("genmask is gone!?");
			}
		} else {
			rt->rt_genmask = NULL;
		}
		rtm->rtm_index = rt->rt_ifp->if_index;
	}
}

static void
route_output_delete_callback(int cmd, int error, struct rt_addrinfo *rtinfo,
			  struct rtentry *rt, void *arg)
{
	if (error == 0 && rt) {
		++rt->rt_refcnt;
		if (fillrtmsg(arg, rt, rtinfo) != 0) {
			error = ENOBUFS;
			/* XXX no way to return the error */
		}
		--rt->rt_refcnt;
	}
	if (rt && rt->rt_refcnt == 0) {
		++rt->rt_refcnt;
		rtfree(rt);
	}
}

static int
route_output_get_callback(int cmd, struct rt_addrinfo *rtinfo,
			  struct rtentry *rt, void *arg, int found_cnt)
{
	int error, found = 0;

	if (((rtinfo->rti_flags ^ rt->rt_flags) & RTF_HOST) == 0)
		found = 1;

	error = fillrtmsg(arg, rt, rtinfo);
	if (!error && found) {
		/* Got the exact match, we could return now! */
		error = EJUSTRETURN;
	}
	return error;
}

static int
route_output_change_callback(int cmd, struct rt_addrinfo *rtinfo,
			     struct rtentry *rt, void *arg, int found_cnt)
{
	struct rt_msghdr *rtm = arg;
	struct ifaddr *ifa;
	int error = 0;

	/*
	 * new gateway could require new ifaddr, ifp;
	 * flags may also be different; ifp may be specified
	 * by ll sockaddr when protocol address is ambiguous
	 */
	if (((rt->rt_flags & RTF_GATEWAY) && rtinfo->rti_gateway != NULL) ||
	    rtinfo->rti_ifpaddr != NULL ||
	    (rtinfo->rti_ifaaddr != NULL &&
	     !sa_equal(rtinfo->rti_ifaaddr, rt->rt_ifa->ifa_addr))) {
		error = rt_getifa(rtinfo);
		if (error != 0)
			goto done;
	}
	if (rtinfo->rti_gateway != NULL) {
		/*
		 * We only need to generate rtmsg upon the
		 * first route to be changed.
		 */
		error = rt_setgate(rt, rt_key(rt), rtinfo->rti_gateway,
			found_cnt == 1 ? RTL_REPORTMSG : RTL_DONTREPORT);
		if (error != 0)
			goto done;
	}
	if ((ifa = rtinfo->rti_ifa) != NULL) {
		struct ifaddr *oifa = rt->rt_ifa;

		if (oifa != ifa) {
			if (oifa && oifa->ifa_rtrequest)
				oifa->ifa_rtrequest(RTM_DELETE, rt, rtinfo);
			IFAFREE(rt->rt_ifa);
			IFAREF(ifa);
			rt->rt_ifa = ifa;
			rt->rt_ifp = rtinfo->rti_ifp;
		}
	}
	rt_setmetrics(rtm->rtm_inits, &rtm->rtm_rmx, &rt->rt_rmx);
	if (rt->rt_ifa && rt->rt_ifa->ifa_rtrequest)
		rt->rt_ifa->ifa_rtrequest(RTM_ADD, rt, rtinfo);
	if (rtinfo->rti_genmask != NULL) {
		rt->rt_genmask = rtmask_purelookup(rtinfo->rti_genmask);
		if (rt->rt_genmask == NULL) {
			/*
			 * This should not happen, since we
			 * have already installed genmask
			 * on each CPU before we reach here.
			 */
			panic("genmask is gone!?");
		}
	}
	rtm->rtm_index = rt->rt_ifp->if_index;
done:
	return error;
}

static int
route_output_lock_callback(int cmd, struct rt_addrinfo *rtinfo,
			   struct rtentry *rt, void *arg,
			   int found_cnt __unused)
{
	struct rt_msghdr *rtm = arg;

	rt->rt_rmx.rmx_locks &= ~(rtm->rtm_inits);
	rt->rt_rmx.rmx_locks |=
		(rtm->rtm_inits & rtm->rtm_rmx.rmx_locks);
	return 0;
}

static void
rt_setmetrics(u_long which, struct rt_metrics *in, struct rt_metrics *out)
{
#define setmetric(flag, elt) if (which & (flag)) out->elt = in->elt;
	setmetric(RTV_RPIPE, rmx_recvpipe);
	setmetric(RTV_SPIPE, rmx_sendpipe);
	setmetric(RTV_SSTHRESH, rmx_ssthresh);
	setmetric(RTV_RTT, rmx_rtt);
	setmetric(RTV_RTTVAR, rmx_rttvar);
	setmetric(RTV_HOPCOUNT, rmx_hopcount);
	setmetric(RTV_MTU, rmx_mtu);
	setmetric(RTV_EXPIRE, rmx_expire);
	setmetric(RTV_MSL, rmx_msl);
	setmetric(RTV_IWMAXSEGS, rmx_iwmaxsegs);
	setmetric(RTV_IWCAPSEGS, rmx_iwcapsegs);
#undef setmetric
}

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

/*
 * Extract the addresses of the passed sockaddrs.
 * Do a little sanity checking so as to avoid bad memory references.
 * This data is derived straight from userland.
 */
static int
rt_xaddrs(char *cp, char *cplim, struct rt_addrinfo *rtinfo)
{
	struct sockaddr *sa;
	int i;

	for (i = 0; (i < RTAX_MAX) && (cp < cplim); i++) {
		if ((rtinfo->rti_addrs & (1 << i)) == 0)
			continue;
		sa = (struct sockaddr *)cp;
		/*
		 * It won't fit.
		 */
		if ((cp + sa->sa_len) > cplim) {
			return (EINVAL);
		}

		/*
		 * There are no more...  Quit now.
		 * If there are more bits, they are in error.
		 * I've seen this.  route(1) can evidently generate these. 
		 * This causes kernel to core dump.
		 * For compatibility, if we see this, point to a safe address.
		 */
		if (sa->sa_len == 0) {
			static struct sockaddr sa_zero = {
				sizeof sa_zero, AF_INET,
			};

			rtinfo->rti_info[i] = &sa_zero;
			kprintf("rtsock: received more addr bits than sockaddrs.\n");
			return (0); /* should be EINVAL but for compat */
		}

		/* Accept the sockaddr. */
		rtinfo->rti_info[i] = sa;
		cp += ROUNDUP(sa->sa_len);
	}
	return (0);
}

static int
rt_msghdrsize(int type)
{
	switch (type) {
	case RTM_DELADDR:
	case RTM_NEWADDR:
		return sizeof(struct ifa_msghdr);
	case RTM_DELMADDR:
	case RTM_NEWMADDR:
		return sizeof(struct ifma_msghdr);
	case RTM_IFINFO:
		return sizeof(struct if_msghdr);
	case RTM_IFANNOUNCE:
	case RTM_IEEE80211:
		return sizeof(struct if_announcemsghdr);
	default:
		return sizeof(struct rt_msghdr);
	}
}

static int
rt_msgsize(int type, struct rt_addrinfo *rtinfo)
{
	int len, i;

	len = rt_msghdrsize(type);
	for (i = 0; i < RTAX_MAX; i++) {
		if (rtinfo->rti_info[i] != NULL)
			len += ROUNDUP(rtinfo->rti_info[i]->sa_len);
	}
	len = ALIGN(len);
	return len;
}

/*
 * Build a routing message in a buffer.
 * Copy the addresses in the rtinfo->rti_info[] sockaddr array
 * to the end of the buffer after the message header.
 *
 * Set the rtinfo->rti_addrs bitmask of addresses present in rtinfo->rti_info[].
 * This side-effect can be avoided if we reorder the addrs bitmask field in all
 * the route messages to line up so we can set it here instead of back in the
 * calling routine.
 */
static void
rt_msg_buffer(int type, struct rt_addrinfo *rtinfo, void *buf, int msglen)
{
	struct rt_msghdr *rtm;
	char *cp;
	int dlen, i;

	rtm = (struct rt_msghdr *) buf;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_type = type;
	rtm->rtm_msglen = msglen;

	cp = (char *)buf + rt_msghdrsize(type);
	rtinfo->rti_addrs = 0;
	for (i = 0; i < RTAX_MAX; i++) {
		struct sockaddr *sa;

		if ((sa = rtinfo->rti_info[i]) == NULL)
			continue;
		rtinfo->rti_addrs |= (1 << i);
		dlen = ROUNDUP(sa->sa_len);
		bcopy(sa, cp, dlen);
		cp += dlen;
	}
}

/*
 * Build a routing message in a mbuf chain.
 * Copy the addresses in the rtinfo->rti_info[] sockaddr array
 * to the end of the mbuf after the message header.
 *
 * Set the rtinfo->rti_addrs bitmask of addresses present in rtinfo->rti_info[].
 * This side-effect can be avoided if we reorder the addrs bitmask field in all
 * the route messages to line up so we can set it here instead of back in the
 * calling routine.
 */
static struct mbuf *
rt_msg_mbuf(int type, struct rt_addrinfo *rtinfo)
{
	struct mbuf *m;
	struct rt_msghdr *rtm;
	int hlen, len;
	int i;

	hlen = rt_msghdrsize(type);
	KASSERT(hlen <= MCLBYTES, ("rt_msg_mbuf: hlen %d doesn't fit", hlen));

	m = m_getl(hlen, MB_DONTWAIT, MT_DATA, M_PKTHDR, NULL);
	if (m == NULL)
		return (NULL);
	mbuftrackid(m, 32);
	m->m_pkthdr.len = m->m_len = hlen;
	m->m_pkthdr.rcvif = NULL;
	rtinfo->rti_addrs = 0;
	len = hlen;
	for (i = 0; i < RTAX_MAX; i++) {
		struct sockaddr *sa;
		int dlen;

		if ((sa = rtinfo->rti_info[i]) == NULL)
			continue;
		rtinfo->rti_addrs |= (1 << i);
		dlen = ROUNDUP(sa->sa_len);
		m_copyback(m, len, dlen, (caddr_t)sa); /* can grow mbuf chain */
		len += dlen;
	}
	if (m->m_pkthdr.len != len) { /* one of the m_copyback() calls failed */
		m_freem(m);
		return (NULL);
	}
	rtm = mtod(m, struct rt_msghdr *);
	bzero(rtm, hlen);
	rtm->rtm_msglen = len;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_type = type;
	return (m);
}

/*
 * This routine is called to generate a message from the routing
 * socket indicating that a redirect has occurred, a routing lookup
 * has failed, or that a protocol has detected timeouts to a particular
 * destination.
 */
void
rt_missmsg(int type, struct rt_addrinfo *rtinfo, int flags, int error)
{
	struct sockaddr *dst = rtinfo->rti_info[RTAX_DST];
	struct rt_msghdr *rtm;
	struct mbuf *m;

	if (route_cb.any_count == 0)
		return;
	m = rt_msg_mbuf(type, rtinfo);
	if (m == NULL)
		return;
	rtm = mtod(m, struct rt_msghdr *);
	rtm->rtm_flags = RTF_DONE | flags;
	rtm->rtm_errno = error;
	rtm->rtm_addrs = rtinfo->rti_addrs;
	rts_input(m, familyof(dst));
}

void
rt_dstmsg(int type, struct sockaddr *dst, int error)
{
	struct rt_msghdr *rtm;
	struct rt_addrinfo addrs;
	struct mbuf *m;

	if (route_cb.any_count == 0)
		return;
	bzero(&addrs, sizeof(struct rt_addrinfo));
	addrs.rti_info[RTAX_DST] = dst;
	m = rt_msg_mbuf(type, &addrs);
	if (m == NULL)
		return;
	rtm = mtod(m, struct rt_msghdr *);
	rtm->rtm_flags = RTF_DONE;
	rtm->rtm_errno = error;
	rtm->rtm_addrs = addrs.rti_addrs;
	rts_input(m, familyof(dst));
}

/*
 * This routine is called to generate a message from the routing
 * socket indicating that the status of a network interface has changed.
 */
void
rt_ifmsg(struct ifnet *ifp)
{
	struct if_msghdr *ifm;
	struct mbuf *m;
	struct rt_addrinfo rtinfo;

	if (route_cb.any_count == 0)
		return;
	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	m = rt_msg_mbuf(RTM_IFINFO, &rtinfo);
	if (m == NULL)
		return;
	ifm = mtod(m, struct if_msghdr *);
	ifm->ifm_index = ifp->if_index;
	ifm->ifm_flags = ifp->if_flags;
	ifm->ifm_data = ifp->if_data;
	ifm->ifm_addrs = 0;
	rts_input(m, 0);
}

static void
rt_ifamsg(int cmd, struct ifaddr *ifa)
{
	struct ifa_msghdr *ifam;
	struct rt_addrinfo rtinfo;
	struct mbuf *m;
	struct ifnet *ifp = ifa->ifa_ifp;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_ifaaddr = ifa->ifa_addr;
	rtinfo.rti_ifpaddr =
		TAILQ_FIRST(&ifp->if_addrheads[mycpuid])->ifa->ifa_addr;
	rtinfo.rti_netmask = ifa->ifa_netmask;
	rtinfo.rti_bcastaddr = ifa->ifa_dstaddr;

	m = rt_msg_mbuf(cmd, &rtinfo);
	if (m == NULL)
		return;

	ifam = mtod(m, struct ifa_msghdr *);
	ifam->ifam_index = ifp->if_index;
	ifam->ifam_metric = ifa->ifa_metric;
	ifam->ifam_flags = ifa->ifa_flags;
	ifam->ifam_addrs = rtinfo.rti_addrs;

	rts_input(m, familyof(ifa->ifa_addr));
}

void
rt_rtmsg(int cmd, struct rtentry *rt, struct ifnet *ifp, int error)
{
	struct rt_msghdr *rtm;
	struct rt_addrinfo rtinfo;
	struct mbuf *m;
	struct sockaddr *dst;

	if (rt == NULL)
		return;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_dst = dst = rt_key(rt);
	rtinfo.rti_gateway = rt->rt_gateway;
	rtinfo.rti_netmask = rt_mask(rt);
	if (ifp != NULL) {
		rtinfo.rti_ifpaddr =
		TAILQ_FIRST(&ifp->if_addrheads[mycpuid])->ifa->ifa_addr;
	}
	rtinfo.rti_ifaaddr = rt->rt_ifa->ifa_addr;

	m = rt_msg_mbuf(cmd, &rtinfo);
	if (m == NULL)
		return;

	rtm = mtod(m, struct rt_msghdr *);
	if (ifp != NULL)
		rtm->rtm_index = ifp->if_index;
	rtm->rtm_flags |= rt->rt_flags;
	rtm->rtm_errno = error;
	rtm->rtm_addrs = rtinfo.rti_addrs;

	rts_input(m, familyof(dst));
}

/*
 * This is called to generate messages from the routing socket
 * indicating a network interface has had addresses associated with it.
 * if we ever reverse the logic and replace messages TO the routing
 * socket indicate a request to configure interfaces, then it will
 * be unnecessary as the routing socket will automatically generate
 * copies of it.
 */
void
rt_newaddrmsg(int cmd, struct ifaddr *ifa, int error, struct rtentry *rt)
{
#ifdef SCTP
	/*
	 * notify the SCTP stack
	 * this will only get called when an address is added/deleted
	 * XXX pass the ifaddr struct instead if ifa->ifa_addr...
	 */
	if (cmd == RTM_ADD)
		sctp_add_ip_address(ifa);
	else if (cmd == RTM_DELETE)
		sctp_delete_ip_address(ifa);
#endif /* SCTP */

	if (route_cb.any_count == 0)
		return;

	if (cmd == RTM_ADD) {
		rt_ifamsg(RTM_NEWADDR, ifa);
		rt_rtmsg(RTM_ADD, rt, ifa->ifa_ifp, error);
	} else {
		KASSERT((cmd == RTM_DELETE), ("unknown cmd %d", cmd));
		rt_rtmsg(RTM_DELETE, rt, ifa->ifa_ifp, error);
		rt_ifamsg(RTM_DELADDR, ifa);
	}
}

/*
 * This is the analogue to the rt_newaddrmsg which performs the same
 * function but for multicast group memberhips.  This is easier since
 * there is no route state to worry about.
 */
void
rt_newmaddrmsg(int cmd, struct ifmultiaddr *ifma)
{
	struct rt_addrinfo rtinfo;
	struct mbuf *m = NULL;
	struct ifnet *ifp = ifma->ifma_ifp;
	struct ifma_msghdr *ifmam;

	if (route_cb.any_count == 0)
		return;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_ifaaddr = ifma->ifma_addr;
	if (ifp != NULL && !TAILQ_EMPTY(&ifp->if_addrheads[mycpuid])) {
		rtinfo.rti_ifpaddr =
		TAILQ_FIRST(&ifp->if_addrheads[mycpuid])->ifa->ifa_addr;
	}
	/*
	 * If a link-layer address is present, present it as a ``gateway''
	 * (similarly to how ARP entries, e.g., are presented).
	 */
	rtinfo.rti_gateway = ifma->ifma_lladdr;

	m = rt_msg_mbuf(cmd, &rtinfo);
	if (m == NULL)
		return;

	ifmam = mtod(m, struct ifma_msghdr *);
	ifmam->ifmam_index = ifp->if_index;
	ifmam->ifmam_addrs = rtinfo.rti_addrs;

	rts_input(m, familyof(ifma->ifma_addr));
}

static struct mbuf *
rt_makeifannouncemsg(struct ifnet *ifp, int type, int what,
		     struct rt_addrinfo *info)
{
	struct if_announcemsghdr *ifan;
	struct mbuf *m;

	if (route_cb.any_count == 0)
		return NULL;

	bzero(info, sizeof(*info));
	m = rt_msg_mbuf(type, info);
	if (m == NULL)
		return NULL;

	ifan = mtod(m, struct if_announcemsghdr *);
	ifan->ifan_index = ifp->if_index;
	strlcpy(ifan->ifan_name, ifp->if_xname, sizeof ifan->ifan_name);
	ifan->ifan_what = what;
	return m;
}

/*
 * This is called to generate routing socket messages indicating
 * IEEE80211 wireless events.
 * XXX we piggyback on the RTM_IFANNOUNCE msg format in a clumsy way.
 */
void
rt_ieee80211msg(struct ifnet *ifp, int what, void *data, size_t data_len)
{
	struct rt_addrinfo info;
	struct mbuf *m;

	m = rt_makeifannouncemsg(ifp, RTM_IEEE80211, what, &info);
	if (m == NULL)
		return;

	/*
	 * Append the ieee80211 data.  Try to stick it in the
	 * mbuf containing the ifannounce msg; otherwise allocate
	 * a new mbuf and append.
	 *
	 * NB: we assume m is a single mbuf.
	 */
	if (data_len > M_TRAILINGSPACE(m)) {
		/* XXX use m_getb(data_len, MB_DONTWAIT, MT_DATA, 0); */
		struct mbuf *n = m_get(MB_DONTWAIT, MT_DATA);
		if (n == NULL) {
			m_freem(m);
			return;
		}
		KKASSERT(data_len <= M_TRAILINGSPACE(n));
		bcopy(data, mtod(n, void *), data_len);
		n->m_len = data_len;
		m->m_next = n;
	} else if (data_len > 0) {
		bcopy(data, mtod(m, u_int8_t *) + m->m_len, data_len);
		m->m_len += data_len;
	}
	mbuftrackid(m, 33);
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len += data_len;
	mtod(m, struct if_announcemsghdr *)->ifan_msglen += data_len;
	rts_input(m, 0);
}

/*
 * This is called to generate routing socket messages indicating
 * network interface arrival and departure.
 */
void
rt_ifannouncemsg(struct ifnet *ifp, int what)
{
	struct rt_addrinfo addrinfo;
	struct mbuf *m;

	m = rt_makeifannouncemsg(ifp, RTM_IFANNOUNCE, what, &addrinfo);
	if (m != NULL)
		rts_input(m, 0);
}

static int
resizewalkarg(struct walkarg *w, int len)
{
	void *newptr;

	newptr = kmalloc(len, M_RTABLE, M_INTWAIT | M_NULLOK);
	if (newptr == NULL)
		return (ENOMEM);
	if (w->w_tmem != NULL)
		kfree(w->w_tmem, M_RTABLE);
	w->w_tmem = newptr;
	w->w_tmemsize = len;
	return (0);
}

/*
 * This is used in dumping the kernel table via sysctl().
 */
int
sysctl_dumpentry(struct radix_node *rn, void *vw)
{
	struct walkarg *w = vw;
	struct rtentry *rt = (struct rtentry *)rn;
	struct rt_addrinfo rtinfo;
	int error, msglen;

	if (w->w_op == NET_RT_FLAGS && !(rt->rt_flags & w->w_arg))
		return 0;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_dst = rt_key(rt);
	rtinfo.rti_gateway = rt->rt_gateway;
	rtinfo.rti_netmask = rt_mask(rt);
	rtinfo.rti_genmask = rt->rt_genmask;
	if (rt->rt_ifp != NULL) {
		rtinfo.rti_ifpaddr =
		TAILQ_FIRST(&rt->rt_ifp->if_addrheads[mycpuid])->ifa->ifa_addr;
		rtinfo.rti_ifaaddr = rt->rt_ifa->ifa_addr;
		if (rt->rt_ifp->if_flags & IFF_POINTOPOINT)
			rtinfo.rti_bcastaddr = rt->rt_ifa->ifa_dstaddr;
	}
	msglen = rt_msgsize(RTM_GET, &rtinfo);
	if (w->w_tmemsize < msglen && resizewalkarg(w, msglen) != 0)
		return (ENOMEM);
	rt_msg_buffer(RTM_GET, &rtinfo, w->w_tmem, msglen);
	if (w->w_req != NULL) {
		struct rt_msghdr *rtm = w->w_tmem;

		rtm->rtm_flags = rt->rt_flags;
		rtm->rtm_use = rt->rt_use;
		rtm->rtm_rmx = rt->rt_rmx;
		rtm->rtm_index = rt->rt_ifp->if_index;
		rtm->rtm_errno = rtm->rtm_pid = rtm->rtm_seq = 0;
		rtm->rtm_addrs = rtinfo.rti_addrs;
		error = SYSCTL_OUT(w->w_req, rtm, msglen);
		return (error);
	}
	return (0);
}

static void
ifnet_compute_stats(struct ifnet *ifp)
{
	IFNET_STAT_GET(ifp, ipackets, ifp->if_ipackets);
	IFNET_STAT_GET(ifp, ierrors, ifp->if_ierrors);
	IFNET_STAT_GET(ifp, opackets, ifp->if_opackets);
	IFNET_STAT_GET(ifp, collisions, ifp->if_collisions);
	IFNET_STAT_GET(ifp, ibytes, ifp->if_ibytes);
	IFNET_STAT_GET(ifp, obytes, ifp->if_obytes);
	IFNET_STAT_GET(ifp, imcasts, ifp->if_imcasts);
	IFNET_STAT_GET(ifp, omcasts, ifp->if_omcasts);
	IFNET_STAT_GET(ifp, iqdrops, ifp->if_iqdrops);
	IFNET_STAT_GET(ifp, noproto, ifp->if_noproto);
}

static int
sysctl_iflist(int af, struct walkarg *w)
{
	struct ifnet *ifp;
	struct rt_addrinfo rtinfo;
	int msglen, error;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	TAILQ_FOREACH(ifp, &ifnet, if_link) {
		struct ifaddr_container *ifac;
		struct ifaddr *ifa;

		if (w->w_arg && w->w_arg != ifp->if_index)
			continue;
		ifac = TAILQ_FIRST(&ifp->if_addrheads[mycpuid]);
		ifa = ifac->ifa;
		rtinfo.rti_ifpaddr = ifa->ifa_addr;
		msglen = rt_msgsize(RTM_IFINFO, &rtinfo);
		if (w->w_tmemsize < msglen && resizewalkarg(w, msglen) != 0)
			return (ENOMEM);
		rt_msg_buffer(RTM_IFINFO, &rtinfo, w->w_tmem, msglen);
		rtinfo.rti_ifpaddr = NULL;
		if (w->w_req != NULL && w->w_tmem != NULL) {
			struct if_msghdr *ifm = w->w_tmem;

			ifm->ifm_index = ifp->if_index;
			ifm->ifm_flags = ifp->if_flags;
			ifnet_compute_stats(ifp);
			ifm->ifm_data = ifp->if_data;
			ifm->ifm_addrs = rtinfo.rti_addrs;
			error = SYSCTL_OUT(w->w_req, ifm, msglen);
			if (error)
				return (error);
		}
		while ((ifac = TAILQ_NEXT(ifac, ifa_link)) != NULL) {
			ifa = ifac->ifa;

			if (af && af != ifa->ifa_addr->sa_family)
				continue;
			if (curproc->p_ucred->cr_prison &&
			    prison_if(curproc->p_ucred, ifa->ifa_addr))
				continue;
			rtinfo.rti_ifaaddr = ifa->ifa_addr;
			rtinfo.rti_netmask = ifa->ifa_netmask;
			rtinfo.rti_bcastaddr = ifa->ifa_dstaddr;
			msglen = rt_msgsize(RTM_NEWADDR, &rtinfo);
			if (w->w_tmemsize < msglen &&
			    resizewalkarg(w, msglen) != 0)
				return (ENOMEM);
			rt_msg_buffer(RTM_NEWADDR, &rtinfo, w->w_tmem, msglen);
			if (w->w_req != NULL) {
				struct ifa_msghdr *ifam = w->w_tmem;

				ifam->ifam_index = ifa->ifa_ifp->if_index;
				ifam->ifam_flags = ifa->ifa_flags;
				ifam->ifam_metric = ifa->ifa_metric;
				ifam->ifam_addrs = rtinfo.rti_addrs;
				error = SYSCTL_OUT(w->w_req, w->w_tmem, msglen);
				if (error)
					return (error);
			}
		}
		rtinfo.rti_netmask = NULL;
		rtinfo.rti_ifaaddr = NULL;
		rtinfo.rti_bcastaddr = NULL;
	}
	return (0);
}

static int
sysctl_rtsock(SYSCTL_HANDLER_ARGS)
{
	int	*name = (int *)arg1;
	u_int	namelen = arg2;
	struct radix_node_head *rnh;
	int	i, error = EINVAL;
	int	origcpu;
	u_char  af;
	struct	walkarg w;

	name ++;
	namelen--;
	if (req->newptr)
		return (EPERM);
	if (namelen != 3 && namelen != 4)
		return (EINVAL);
	af = name[0];
	bzero(&w, sizeof w);
	w.w_op = name[1];
	w.w_arg = name[2];
	w.w_req = req;

	/*
	 * Optional third argument specifies cpu, used primarily for
	 * debugging the route table.
	 */
	if (namelen == 4) {
		if (name[3] < 0 || name[3] >= ncpus)
			return (EINVAL);
		origcpu = mycpuid;
		lwkt_migratecpu(name[3]);
	} else {
		origcpu = -1;
	}
	crit_enter();
	switch (w.w_op) {
	case NET_RT_DUMP:
	case NET_RT_FLAGS:
		for (i = 1; i <= AF_MAX; i++)
			if ((rnh = rt_tables[mycpuid][i]) &&
			    (af == 0 || af == i) &&
			    (error = rnh->rnh_walktree(rnh,
						       sysctl_dumpentry, &w)))
				break;
		break;

	case NET_RT_IFLIST:
		error = sysctl_iflist(af, &w);
	}
	crit_exit();
	if (w.w_tmem != NULL)
		kfree(w.w_tmem, M_RTABLE);
	if (origcpu >= 0)
		lwkt_migratecpu(origcpu);
	return (error);
}

SYSCTL_NODE(_net, PF_ROUTE, routetable, CTLFLAG_RD, sysctl_rtsock, "");

/*
 * Definitions of protocols supported in the ROUTE domain.
 */

static struct domain routedomain;		/* or at least forward */

static struct protosw routesw[] = {
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &routedomain,
	.pr_protocol = 0,
	.pr_flags = PR_ATOMIC|PR_ADDR,
	.pr_input = NULL,
	.pr_output = route_output,
	.pr_ctlinput = raw_ctlinput,
	.pr_ctloutput = NULL,
	.pr_ctlport = cpu0_ctlport,

	.pr_init = raw_init,
	.pr_usrreqs = &route_usrreqs
    }
};

static struct domain routedomain = {
	PF_ROUTE, "route", NULL, NULL, NULL,
	routesw, &routesw[(sizeof routesw)/(sizeof routesw[0])],
};

DOMAIN_SET(route);

