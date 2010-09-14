/*	$KAME: sctp_var.h,v 1.23 2004/10/27 07:57:49 itojun Exp $	*/
/*	$DragonFly: src/sys/netinet/sctp_var.h,v 1.4 2007/04/22 01:13:14 dillon Exp $	*/

/*
 * Copyright (c) 2001, 2002, 2003, 2004 Cisco Systems, Inc.
 * All rights reserved.
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
 *      This product includes software developed by Cisco Systems, Inc.
 * 4. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CISCO SYSTEMS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CISCO SYSTEMS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NETINET_SCTP_VAR_H_
#define _NETINET_SCTP_VAR_H_

#ifndef __OpenBSD__
#include <sys/socketvar.h>
#endif
#include <netinet/sctp_uio.h>

/* SCTP Kernel structures */

/*
 * Names for SCTP sysctl objects
 */
#ifndef __APPLE__
#define	SCTPCTL_MAXDGRAM	    1	/* max datagram size */
#define	SCTPCTL_RECVSPACE	    2	/* default receive buffer space */
#define SCTPCTL_AUTOASCONF          3   /* auto asconf enable/disable flag */
#define SCTPCTL_ECN_ENABLE          4	/* Is ecn allowed */
#define SCTPCTL_ECN_NONCE           5   /* Is ecn nonce allowed */
#define SCTPCTL_STRICT_SACK         6	/* strictly require sack'd TSN's to be
					 * smaller than sndnxt.
					 */
#define SCTPCTL_NOCSUM_LO           7   /* Require that the Loopback NOT have
				         * the crc32 checksum on packets routed over
					 * it.
				         */
#define SCTPCTL_STRICT_INIT         8
#define SCTPCTL_PEER_CHK_OH         9
#define SCTPCTL_MAXBURST            10
#define SCTPCTL_MAXCHUNKONQ         11
#define SCTPCTL_DELAYED_SACK        12
#define SCTPCTL_HB_INTERVAL         13
#define SCTPCTL_PMTU_RAISE          14
#define SCTPCTL_SHUTDOWN_GUARD      15
#define SCTPCTL_SECRET_LIFETIME     16
#define SCTPCTL_RTO_MAX             17
#define SCTPCTL_RTO_MIN             18
#define SCTPCTL_RTO_INITIAL         19
#define SCTPCTL_INIT_RTO_MAX        20
#define SCTPCTL_COOKIE_LIFE         21
#define SCTPCTL_INIT_RTX_MAX        22
#define SCTPCTL_ASSOC_RTX_MAX       23
#define SCTPCTL_PATH_RTX_MAX        24
#define SCTPCTL_NR_OUTGOING_STREAMS 25
#ifdef SCTP_DEBUG
#define SCTPCTL_DEBUG               26
#define SCTPCTL_MAXID		    27
#else
#define SCTPCTL_MAXID		    26
#endif

#endif

#ifdef SCTP_DEBUG
#define SCTPCTL_NAMES { \
	{ 0, 0 }, \
	{ "maxdgram", CTLTYPE_INT }, \
	{ "recvspace", CTLTYPE_INT }, \
	{ "autoasconf", CTLTYPE_INT }, \
	{ "ecn_enable", CTLTYPE_INT }, \
	{ "ecn_nonce", CTLTYPE_INT }, \
	{ "strict_sack", CTLTYPE_INT }, \
	{ "looback_nocsum", CTLTYPE_INT }, \
	{ "strict_init", CTLTYPE_INT }, \
	{ "peer_chkoh", CTLTYPE_INT }, \
	{ "maxburst", CTLTYPE_INT }, \
	{ "maxchunks", CTLTYPE_INT }, \
	{ "delayed_sack_time", CTLTYPE_INT }, \
	{ "heartbeat_interval", CTLTYPE_INT }, \
	{ "pmtu_raise_time", CTLTYPE_INT }, \
	{ "shutdown_guard_time", CTLTYPE_INT }, \
	{ "secret_lifetime", CTLTYPE_INT }, \
	{ "rto_max", CTLTYPE_INT }, \
	{ "rto_min", CTLTYPE_INT }, \
	{ "rto_initial", CTLTYPE_INT }, \
	{ "init_rto_max", CTLTYPE_INT }, \
	{ "valid_cookie_life", CTLTYPE_INT }, \
	{ "init_rtx_max", CTLTYPE_INT }, \
	{ "assoc_rtx_max", CTLTYPE_INT }, \
	{ "path_rtx_max", CTLTYPE_INT }, \
	{ "nr_outgoing_streams", CTLTYPE_INT }, \
	{ "debug", CTLTYPE_INT }, \
}
#else
#define SCTPCTL_NAMES { \
	{ 0, 0 }, \
	{ "maxdgram", CTLTYPE_INT }, \
	{ "recvspace", CTLTYPE_INT }, \
	{ "autoasconf", CTLTYPE_INT }, \
	{ "ecn_enable", CTLTYPE_INT }, \
	{ "ecn_nonce", CTLTYPE_INT }, \
	{ "strict_sack", CTLTYPE_INT }, \
	{ "looback_nocsum", CTLTYPE_INT }, \
	{ "strict_init", CTLTYPE_INT }, \
	{ "peer_chkoh", CTLTYPE_INT }, \
	{ "maxburst", CTLTYPE_INT }, \
	{ "maxchunks", CTLTYPE_INT }, \
	{ "delayed_sack_time", CTLTYPE_INT }, \
	{ "heartbeat_interval", CTLTYPE_INT }, \
	{ "pmtu_raise_time", CTLTYPE_INT }, \
	{ "shutdown_guard_time", CTLTYPE_INT }, \
	{ "secret_lifetime", CTLTYPE_INT }, \
	{ "rto_max", CTLTYPE_INT }, \
	{ "rto_min", CTLTYPE_INT }, \
	{ "rto_initial", CTLTYPE_INT }, \
	{ "init_rto_max", CTLTYPE_INT }, \
	{ "valid_cookie_life", CTLTYPE_INT }, \
	{ "init_rtx_max", CTLTYPE_INT }, \
	{ "assoc_rtx_max", CTLTYPE_INT }, \
	{ "path_rtx_max", CTLTYPE_INT }, \
	{ "nr_outgoing_streams", CTLTYPE_INT }, \
}
#endif

#if defined(_KERNEL) || (defined(__APPLE__) && defined(KERNEL))

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_inet_sctp);
#endif
extern struct	pr_usrreqs sctp_usrreqs;
#elif defined(__NetBSD__)
int sctp_usrreq(struct socket *, int, struct mbuf *, struct mbuf *,
		      struct mbuf *, struct proc *);
#else
int sctp_usrreq(struct socket *, int, struct mbuf *, struct mbuf *,
		      struct mbuf *);
#endif

#define	sctp_sbspace(ssb) ((long) (((ssb)->ssb_hiwat > (ssb)->ssb_cc) ? ((ssb)->ssb_hiwat - (ssb)->ssb_cc) : 0))

#define sctp_sbspace_sub(a,b) ((a > b) ? (a - b) : 0)

extern int	sctp_sendspace;
extern int	sctp_recvspace;
extern int      sctp_ecn;
extern int      sctp_ecn_nonce;

struct sctp_nets;
struct sctp_inpcb;
struct sctp_tcb;
struct sctphdr;

#if defined(__OpenBSD__)
void sctp_fasttim(void);
#endif

union netmsg;

#if defined(__FreeBSD__) || defined(__APPLE__)
void	sctp_ctlinput(int, struct sockaddr *, void *);
int	sctp_ctloutput(struct socket *, struct sockopt *);
void	sctp_input(struct mbuf *, int);
#elif defined(__DragonFly__)
void	sctp_ctlinput(union netmsg *);
void	sctp_ctloutput(union netmsg *);
int	sctp_input(struct mbuf **, int *, int);
#else
void*	sctp_ctlinput(int, struct sockaddr *, void *);
int	sctp_ctloutput(int, struct socket *, int, int, struct mbuf **);
void	sctp_input(struct mbuf *, ... );
#endif
void	sctp_drain(void);
void	sctp_init(void);
void	sctp_shutdown(union netmsg *);
void	sctp_notify(struct sctp_inpcb *, int, struct sctphdr *,
		    struct sockaddr *, struct sctp_tcb *,
		    struct sctp_nets *);
void	sctp_usr_recvd(union netmsg *);

#if defined(INET6)
void ip_2_ip6_hdr(struct ip6_hdr *, struct ip *);
#endif

int sctp_bindx(struct socket *, int, struct sockaddr_storage *,
	int, int, struct proc *);

/* can't use sctp_assoc_t here */
int sctp_peeloff(struct socket *, struct socket *, int, caddr_t, int *);


sctp_assoc_t sctp_getassocid(struct sockaddr *);

int sctp_ingetaddr_oncpu(struct socket *, struct sockaddr **);
int sctp_peeraddr_oncpu(struct socket *, struct sockaddr **);
void sctp_peeraddr(union netmsg *);
void sctp_listen(union netmsg *);
void sctp_accept(union netmsg *);
void sctp_send(union netmsg *);

#if defined(__NetBSD__) || defined(__OpenBSD__)
int sctp_sysctl(int *, u_int, void *, size_t *, void *, size_t);
#endif

/*
 * compatibility defines for OpenBSD, Apple
 */

/* map callout into timeout for OpenBSD */
#ifdef __OpenBSD__
#ifndef callout_init
#define callout_init(args)
#define callout_reset(c, ticks, func, arg) \
do { \
	timeout_set((c), (func), (arg)); \
	timeout_add((c), (ticks)); \
} while (0)
#define callout_stop(c) timeout_del(c)
#define callout_pending(c) timeout_pending(c)
#define callout_active(c) timeout_initialized(c)
#endif
#endif

/* XXX: Hopefully temporary until APPLE changes to newer defs like other BSDs */
#if defined(__APPLE__)
#define if_addrlist	if_addrhead
#define if_list		if_link
#define ifa_list	ifa_link
#endif /* __APPLE__ **/

#endif /* _KERNEL */

#endif /* !_NETINET_SCTP_VAR_H_ */
