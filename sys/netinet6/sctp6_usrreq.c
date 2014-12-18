/*	$KAME: sctp6_usrreq.c,v 1.35 2004/08/17 06:28:03 t-momose Exp $	*/

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
#if !(defined(__OpenBSD__) || defined(__APPLE__))
#include "opt_inet.h"
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include "opt_inet6.h"
#include "opt_inet.h"
#endif
#ifdef __NetBSD__
#include "opt_inet.h"
#endif
#if !(defined(__OpenBSD__) || defined(__APPLE__))
#include "opt_ipsec.h"
#endif
#ifdef __APPLE__
#include <sctp.h>
#elif !defined(__OpenBSD__)
#include "opt_sctp.h"
#endif

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/proc.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_var.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_input.h>
#include <netinet/sctp_asconf.h>
#include <netinet6/ip6_var.h>
#include <netinet/ip6.h>
#if !defined(__OpenBSD__)
#include <netinet6/in6_pcb.h>
#endif
#include <netinet/icmp6.h>
#include <netinet6/sctp6_var.h>
#include <netinet6/ip6protosw.h>
#include <netinet6/nd6.h>

#ifdef IPSEC
#ifndef __OpenBSD__
#include <netinet6/ipsec6.h>
#include <netinet6/ipsec.h>
#else
#undef IPSEC
#endif
#endif /*IPSEC*/

#if defined(NFAITH) && NFAITH > 0
#include <net/if_faith.h>
#endif

#include <net/net_osdep.h>

extern struct protosw inetsw[];

#if defined(HAVE_NRL_INPCB) || defined(__FreeBSD__) || defined(__DragonFly__)
#ifndef in6pcb
#define in6pcb		inpcb
#endif
#ifndef sotoin6pcb
#define sotoin6pcb      sotoinpcb
#endif
#endif

#ifdef SCTP_DEBUG
extern u_int32_t sctp_debug_on;
#endif

static int sctp6_bind_oncpu(struct socket *so, struct sockaddr *addr, thread_t td);


extern int sctp_no_csum_on_loopback;

int
sctp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6;
	struct sctphdr *sh;
	struct sctp_inpcb *in6p = NULL;
	struct sctp_nets *net;
	int refcount_up = 0;
	u_int32_t check, calc_check;
	struct inpcb *in6p_ip;
	struct sctp_chunkhdr *ch;
	struct mbuf *opts = NULL;
	int length, mlen, offset, iphlen;
	u_int8_t ecn_bits;
	struct sctp_tcb *stcb = NULL;
	int off = *offp;

	ip6 = mtod(m, struct ip6_hdr *);
#ifndef PULLDOWN_TEST
	/* If PULLDOWN_TEST off, must be in a single mbuf. */
	IP6_EXTHDR_CHECK(m, off, (int)(sizeof(*sh) + sizeof(*ch)), IPPROTO_DONE);
	sh = (struct sctphdr *)((caddr_t)ip6 + off);
	ch = (struct sctp_chunkhdr *)((caddr_t)sh + sizeof(*sh));
#else
	/* Ensure that (sctphdr + sctp_chunkhdr) in a row. */
	IP6_EXTHDR_GET(sh, struct sctphdr *, m, off, sizeof(*sh) + sizeof(*ch));
	if (sh == NULL) {
		sctp_pegs[SCTP_HDR_DROPS]++;
		return IPPROTO_DONE;
	}
	ch = (struct sctp_chunkhdr *)((caddr_t)sh + sizeof(struct sctphdr));
#endif

	iphlen = off;
	offset = iphlen + sizeof(*sh) + sizeof(*ch);

#if defined(NFAITH) && NFAITH > 0
#if defined(__FreeBSD_cc_version) && __FreeBSD_cc_version <= 430000
#if defined(NFAITH) && 0 < NFAITH
	if (faithprefix(&ip6h->ip6_dst)) {
		/* XXX send icmp6 host/port unreach? */
		goto bad;
	}
#endif
#else

#ifdef __FreeBSD__
	if (faithprefix_p != NULL && (*faithprefix_p)(&ip6->ip6_dst)) {
		/* XXX send icmp6 host/port unreach? */
		goto bad;
	}
#else
	if (faithprefix(&ip6->ip6_dst))
		goto bad;
#endif
#endif /* __FreeBSD_cc_version */

#endif /* NFAITH defined and > 0 */
	sctp_pegs[SCTP_INPKTS]++;
#ifdef SCTP_DEBUG
	if (sctp_debug_on & SCTP_DEBUG_INPUT1) {
		kprintf("V6 input gets a packet iphlen:%d pktlen:%d\n", iphlen, m->m_pkthdr.len);
	}
#endif
 	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		/* No multi-cast support in SCTP */
		sctp_pegs[SCTP_IN_MCAST]++;
		goto bad;
	}
	/* destination port of 0 is illegal, based on RFC2960. */
	if (sh->dest_port == 0)
		goto bad;
	if ((sctp_no_csum_on_loopback == 0) ||
	   (m->m_pkthdr.rcvif == NULL) ||
	   (m->m_pkthdr.rcvif->if_type != IFT_LOOP)) {
		/* we do NOT validate things from the loopback if the
		 * sysctl is set to 1.
		 */
		check = sh->checksum;		/* save incoming checksum */
		if ((check == 0) && (sctp_no_csum_on_loopback)) {
			/* special hook for where we got a local address
			 * somehow routed across a non IFT_LOOP type interface
			 */
			if (IN6_ARE_ADDR_EQUAL(&ip6->ip6_src, &ip6->ip6_dst))
				goto sctp_skip_csum;
		}
		sh->checksum = 0;		/* prepare for calc */
		calc_check = sctp_calculate_sum(m, &mlen, iphlen);
		if (calc_check != check) {
#ifdef SCTP_DEBUG
			if (sctp_debug_on & SCTP_DEBUG_INPUT1) {
				kprintf("Bad CSUM on SCTP packet calc_check:%x check:%x  m:%p mlen:%d iphlen:%d\n",
				       calc_check, check, m,
				       mlen, iphlen);
			}
#endif
			stcb = sctp_findassociation_addr(m, iphlen, offset - sizeof(*ch),
							 sh, ch, &in6p, &net);
			/* in6p's ref-count increased && stcb locked */
			if ((in6p) && (stcb)) {
				sctp_send_packet_dropped(stcb, net, m, iphlen, 1);
				sctp_chunk_output(in6p, stcb, 2);
			}  else if ((in6p != NULL) && (stcb == NULL)) {
				refcount_up = 1;
			}
			sctp_pegs[SCTP_BAD_CSUM]++;
			goto bad;
		}
		sh->checksum = calc_check;
	} else {
sctp_skip_csum:
		mlen = m->m_pkthdr.len;
	}
	net = NULL;
	/*
	 * Locate pcb and tcb for datagram
	 * sctp_findassociation_addr() wants IP/SCTP/first chunk header...
	 */
#ifdef SCTP_DEBUG
	if (sctp_debug_on & SCTP_DEBUG_INPUT1) {
		kprintf("V6 Find the association\n");
	}
#endif
	stcb = sctp_findassociation_addr(m, iphlen, offset - sizeof(*ch),
	    sh, ch, &in6p, &net);
	/* in6p's ref-count increased */
	if (in6p == NULL) {
		struct sctp_init_chunk *init_chk, chunk_buf;

		sctp_pegs[SCTP_NOPORTS]++;
		if (ch->chunk_type == SCTP_INITIATION) {
			/* we do a trick here to get the INIT tag,
			 * dig in and get the tag from the INIT and
			 * put it in the common header.
			 */
			init_chk = (struct sctp_init_chunk *)sctp_m_getptr(m,
			    iphlen + sizeof(*sh), sizeof(*init_chk),
			    (u_int8_t *)&chunk_buf);
			sh->v_tag = init_chk->init.initiate_tag;
		}
		sctp_send_abort(m, iphlen, sh, 0, NULL);
		goto bad;
	} else if (stcb == NULL) {
		refcount_up = 1;
	}
	in6p_ip = (struct inpcb *)in6p;
#ifdef IPSEC
	/*
	 * Check AH/ESP integrity.
	 */
#ifdef __OpenBSD__
	{
		struct inpcb *i_inp;
		struct m_tag *mtag;
		struct tdb_ident *tdbi;
		struct tdb *tdb;
		int error;

		/* Find most recent IPsec tag */
		i_inp = (struct inpcb *)in6p;
		mtag = m_tag_find(m, PACKET_TAG_IPSEC_IN_DONE, NULL);
		crit_enter();
		if (mtag != NULL) {
			tdbi = (struct tdb_ident *)(mtag + 1);
			tdb = gettdb(tdbi->spi, &tdbi->dst, tdbi->proto);
		} else
			tdb = NULL;

		ipsp_spd_lookup(m, af, iphlen, &error, IPSP_DIRECTION_IN,
		    tdb, i_inp);
		if (error) {
			crit_exit();
			goto bad;
		}

		/* Latch SA */
		if (i_inp->inp_tdb_in != tdb) {
			if (tdb) {
				tdb_add_inp(tdb, i_inp, 1);
				if (i_inp->inp_ipo == NULL) {
					i_inp->inp_ipo = ipsec_add_policy(i_inp,
					    af, IPSP_DIRECTION_OUT);
					if (i_inp->inp_ipo == NULL) {
						crit_exit();
						goto bad;
					}
				}
				if (i_inp->inp_ipo->ipo_dstid == NULL &&
				    tdb->tdb_srcid != NULL) {
					i_inp->inp_ipo->ipo_dstid =
					    tdb->tdb_srcid;
					tdb->tdb_srcid->ref_count++;
				}
				if (i_inp->inp_ipsec_remotecred == NULL &&
				    tdb->tdb_remote_cred != NULL) {
					i_inp->inp_ipsec_remotecred =
					    tdb->tdb_remote_cred;
					tdb->tdb_remote_cred->ref_count++;
				}
				if (i_inp->inp_ipsec_remoteauth == NULL &&
				    tdb->tdb_remote_auth != NULL) {
					i_inp->inp_ipsec_remoteauth =
					    tdb->tdb_remote_auth;
					tdb->tdb_remote_auth->ref_count++;
				}
			} else { /* Just reset */
				TAILQ_REMOVE(&i_inp->inp_tdb_in->tdb_inp_in,
				    i_inp, binp_tdb_in_next);
				i_inp->inp_tdb_in = NULL;
			}
		}
		crit_exit();
	}
#else
	if (ipsec6_in_reject_so(m, in6p->sctp_socket)) {
/* XXX */
#ifndef __APPLE__
		/* FIX ME: need to find right stat for __APPLE__ */
		ipsec6stat.in_polvio++;
#endif
		goto bad;
	}
#endif
#endif /*IPSEC*/

	/*
	 * Construct sockaddr format source address.
	 * Stuff source address and datagram in user buffer.
	 */
	if ((in6p->ip_inp.inp.inp_flags & INP_CONTROLOPTS)
#ifndef __OpenBSD__
	    || (in6p->sctp_socket->so_options & SO_TIMESTAMP)
#endif
	    ) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
#if (defined(SCTP_BASE_FREEBSD) && __FreeBSD_version < 501113) || \
    defined(__APPLE__) || defined(__DragonFly__)
		ip6_savecontrol(in6p_ip, &opts, ip6, m);
#elif defined(__FreeBSD__) && (__FreeBSD_version >= 440000 || (defined(SCTP_BASE_FREEBSD) && __FreeBSD_version >= 501113))
		ip6_savecontrol(in6p_ip, m, &opts);
#else
		ip6_savecontrol(in6p_ip, m, &opts, NULL);
#endif
#else
		ip6_savecontrol((struct in6pcb *)in6p_ip, m, &opts);
#endif
	}

	/*
	 * CONTROL chunk processing
	 */
	length = ntohs(ip6->ip6_plen) + iphlen;
	offset -= sizeof(*ch);
	ecn_bits = ((ntohl(ip6->ip6_flow) >> 20) & 0x000000ff);
	crit_enter();
	sctp_common_input_processing(&m, iphlen, offset, length, sh, ch,
	    in6p, stcb, net, ecn_bits);
	/* inp's ref-count reduced && stcb unlocked */
	crit_exit();
	/* XXX this stuff below gets moved to appropriate parts later... */
	if (m)
		m_freem(m);
	if (opts)
		m_freem(opts);

	if ((in6p) && refcount_up){
		/* reduce ref-count */
		SCTP_INP_WLOCK(in6p);
		SCTP_INP_DECR_REF(in6p);
		SCTP_INP_WUNLOCK(in6p);
	}

	return IPPROTO_DONE;

bad:
	if (stcb)
		SCTP_TCB_UNLOCK(stcb);

	if ((in6p) && refcount_up){
		/* reduce ref-count */
		SCTP_INP_WLOCK(in6p);
		SCTP_INP_DECR_REF(in6p);
		SCTP_INP_WUNLOCK(in6p);
	}
	if (m)
		m_freem(m);
	if (opts)
		m_freem(opts);
	return IPPROTO_DONE;
}


static void
sctp6_notify_mbuf(struct sctp_inpcb *inp,
		  struct icmp6_hdr *icmp6,
		  struct sctphdr *sh,
		  struct sctp_tcb *stcb,
		  struct sctp_nets *net)
{
	unsigned int nxtsz;

	if ((inp == NULL) || (stcb == NULL) || (net == NULL) ||
	    (icmp6 == NULL) || (sh == NULL)) {
		goto out;
	}

	/* First do we even look at it? */
	if (ntohl(sh->v_tag) != (stcb->asoc.peer_vtag))
		goto out;

	if (icmp6->icmp6_type != ICMP6_PACKET_TOO_BIG) {
		/* not PACKET TO BIG */
		goto out;
	}
	/*
	 * ok we need to look closely. We could even get smarter and
	 * look at anyone that we sent to in case we get a different
	 * ICMP that tells us there is no way to reach a host, but for
	 * this impl, all we care about is MTU discovery.
	 */
	nxtsz = ntohl(icmp6->icmp6_mtu);
	/* Stop any PMTU timer */
	sctp_timer_stop(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, NULL);

	/* Adjust destination size limit */
	if (net->mtu > nxtsz) {
		net->mtu = nxtsz;
	}
	/* now what about the ep? */
	if (stcb->asoc.smallest_mtu > nxtsz) {
		struct sctp_tmit_chunk *chk;
		struct sctp_stream_out *strm;
		/* Adjust that too */
		stcb->asoc.smallest_mtu = nxtsz;
		/* now off to subtract IP_DF flag if needed */

		TAILQ_FOREACH(chk, &stcb->asoc.send_queue, sctp_next) {
			if ((chk->send_size+IP_HDR_SIZE) > nxtsz) {
				chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
			}
		}
		TAILQ_FOREACH(chk, &stcb->asoc.sent_queue, sctp_next) {
			if ((chk->send_size+IP_HDR_SIZE) > nxtsz) {
				/*
				 * For this guy we also mark for immediate
				 * resend since we sent to big of chunk
				 */
				chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
				if (chk->sent != SCTP_DATAGRAM_RESEND)
					stcb->asoc.sent_queue_retran_cnt++;
				chk->sent = SCTP_DATAGRAM_RESEND;
				chk->rec.data.doing_fast_retransmit = 0;

				chk->sent = SCTP_DATAGRAM_RESEND;
				/* Clear any time so NO RTT is being done */
				chk->sent_rcv_time.tv_sec = 0;
				chk->sent_rcv_time.tv_usec = 0;
				stcb->asoc.total_flight -= chk->send_size;
				net->flight_size -= chk->send_size;
			}
		}
		TAILQ_FOREACH(strm, &stcb->asoc.out_wheel, next_spoke) {
			TAILQ_FOREACH(chk, &strm->outqueue, sctp_next) {
				if ((chk->send_size+IP_HDR_SIZE) > nxtsz) {
					chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
				}
			}
		}
	}
	sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, inp, stcb, NULL);
out:
	if (inp) {
		/* reduce inp's ref-count */
		SCTP_INP_WLOCK(inp);
		SCTP_INP_DECR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
	}
	if (stcb)
		SCTP_TCB_UNLOCK(stcb);
}


void
sctp6_ctlinput(int cmd, struct sockaddr *pktdst, void *d)
{
	struct sctphdr sh;
	struct ip6ctlparam *ip6cp = NULL;
	int cm;

	if (pktdst->sa_family != AF_INET6 ||
	    pktdst->sa_len != sizeof(struct sockaddr_in6))
		return;

	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	if (PRC_IS_REDIRECT(cmd)) {
		d = NULL;
	} else if (inet6ctlerrmap[cmd] == 0) {
		return;
	}

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
	} else {
		ip6cp = NULL;
	}

	if (ip6cp) {
		/*
		 * XXX: We assume that when IPV6 is non NULL,
		 * M and OFF are valid.
		 */
		/* check if we can safely examine src and dst ports */
		struct sctp_inpcb *inp;
		struct sctp_tcb *stcb;
		struct sctp_nets *net;
		struct sockaddr_in6 final;

		if (ip6cp->ip6c_m == NULL ||
		    (size_t)ip6cp->ip6c_m->m_pkthdr.len < (ip6cp->ip6c_off + sizeof(sh)))
			return;

		bzero(&sh, sizeof(sh));
		bzero(&final, sizeof(final));
		inp = NULL;
		net = NULL;
		m_copydata(ip6cp->ip6c_m, ip6cp->ip6c_off, sizeof(sh),
		    (caddr_t)&sh);
		ip6cp->ip6c_src->sin6_port = sh.src_port;
		final.sin6_len = sizeof(final);
		final.sin6_family = AF_INET6;
#if defined(__FreeBSD__) && __FreeBSD_cc_version < 440000
		final.sin6_addr = *ip6cp->ip6c_finaldst;
#else
		final.sin6_addr = ((struct sockaddr_in6 *)pktdst)->sin6_addr;
#endif /* __FreeBSD_cc_version */
		final.sin6_port = sh.dest_port;
		crit_enter();
		stcb = sctp_findassociation_addr_sa((struct sockaddr *)ip6cp->ip6c_src,
						    (struct sockaddr *)&final,
						    &inp, &net, 1);
		/* inp's ref-count increased && stcb locked */
		if (stcb != NULL && inp && (inp->sctp_socket != NULL)) {
			if (cmd == PRC_MSGSIZE) {
				sctp6_notify_mbuf(inp,
						  ip6cp->ip6c_icmp6,
						  &sh,
						  stcb,
						  net);
				/* inp's ref-count reduced && stcb unlocked */
			} else {
				if (cmd == PRC_HOSTDEAD) {
					cm = EHOSTUNREACH;
				} else {
					cm = inet6ctlerrmap[cmd];
				}
				sctp_notify(inp, cm, &sh,
					    (struct sockaddr *)&final,
					    stcb, net);
				/* inp's ref-count reduced && stcb unlocked */
			}
		} else {
			if (PRC_IS_REDIRECT(cmd) && inp) {
#ifdef __OpenBSD__
				in_rtchange((struct inpcb *)inp,
					    inetctlerrmap[cmd]);
#else
				in6_rtchange((struct in6pcb *)inp,
					     inet6ctlerrmap[cmd]);
#endif
			}
			if (inp) {
				/* reduce inp's ref-count */
				SCTP_INP_WLOCK(inp);
				SCTP_INP_DECR_REF(inp);
				SCTP_INP_WUNLOCK(inp);
			}
			if (stcb)
				SCTP_TCB_UNLOCK(stcb);
		}
		crit_exit();
	}
}

/*
 * this routine can probably be collasped into the one in sctp_userreq.c
 * since they do the same thing and now we lookup with a sockaddr
 */
#ifdef __FreeBSD__
static int
sctp6_getcred(SYSCTL_HANDLER_ARGS)
{
	struct sockaddr_in6 addrs[2];
	struct sctp_inpcb *inp;
	struct sctp_nets *net;
	struct sctp_tcb *stcb;
	int error;

#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
	error = suser(req->td);
#else
	error = suser(req->p);
#endif
	if (error)
		return (error);

	if (req->newlen != sizeof(addrs))
		return (EINVAL);
	if (req->oldlen != sizeof(struct ucred))
		return (EINVAL);
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);
	crit_enter();

        stcb = sctp_findassociation_addr_sa(sin6tosa(&addrs[0]),
                                           sin6tosa(&addrs[1]),
                                           &inp, &net, 1);
	if (stcb == NULL || inp == NULL || inp->sctp_socket == NULL) {
		error = ENOENT;
		if (inp) {
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		}
		goto out;
	}
	error = SYSCTL_OUT(req, inp->sctp_socket->so_cred,
			   sizeof(struct ucred));

	SCTP_TCB_UNLOCK (stcb);
out:
	crit_exit();
	return (error);
}

SYSCTL_PROC(_net_inet6_sctp6, OID_AUTO, getcred, CTLTYPE_OPAQUE|CTLFLAG_RW,
	    0, 0,
	    sctp6_getcred, "S,ucred", "Get the ucred of a SCTP6 connection");

#endif

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
sctp6_abort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct sctp_inpcb *inp;
	int error;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp) {
		soisdisconnected(so);
		sctp_inpcb_free(inp, 1);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
sctp6_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct in6pcb *inp6;
	int error;
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp != NULL) {
		error = EINVAL;
		goto out;
	}

	if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
		error = soreserve(so, sctp_sendspace, sctp_recvspace, NULL);
		if (error)
			goto out;
	}
	crit_enter();
	error = sctp_inpcb_alloc(so);
	crit_exit();
	if (error)
		goto out;
	inp = (struct sctp_inpcb *)so->so_pcb;
	inp->sctp_flags |= SCTP_PCB_FLAGS_BOUND_V6;	/* I'm v6! */
	inp6 = (struct in6pcb *)inp;

	inp6->in6p_hops = -1;	        /* use kernel default */
	inp6->in6p_cksum = -1;	/* just to be sure */
#ifdef INET
	/*
	 * XXX: ugly!!
	 * IPv4 TTL initialization is necessary for an IPv6 socket as well,
	 * because the socket may be bound to an IPv6 wildcard address,
	 * which may match an IPv4-mapped IPv6 address.
	 */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
	inp6->inp_ip_ttl = ip_defttl;
#else
	inp->inp_ip_ttl = ip_defttl;
#endif
#endif
	/*
	 * Hmm what about the IPSEC stuff that is missing here but
	 * in sctp_attach()?
	 */
	error = 0;
out:
	lwkt_replymsg(&msg->lmsg, error);
}

static void
sctp6_bind(netmsg_t msg)
{
	int error;

	error = sctp6_bind_oncpu(msg->bind.base.nm_so,
				 msg->bind.nm_nam,
				 msg->bind.nm_td);
	lwkt_replymsg(&msg->lmsg, error);
}

static int
sctp6_bind_oncpu(struct socket *so, struct sockaddr *addr, thread_t td)
{
	struct sctp_inpcb *inp;
	int error;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}

	if (addr != NULL) {
		if (addr->sa_family == AF_INET) {
			/* can't bind v4 addr to v6 only socket! */
			error = EINVAL;
			goto out;
		} else {
			struct sockaddr_in6 *sin6_p;
			sin6_p = (struct sockaddr_in6 *)addr;

			if (IN6_IS_ADDR_V4MAPPED(&sin6_p->sin6_addr)) {
				/* can't bind v4-mapped addrs either! */
				/* NOTE: we don't support SIIT */
				error = EINVAL;
				goto out;
			}
		}
	}
	crit_enter();
	error = sctp_inpcb_bind(so, addr, td);
	crit_exit();
out:
	return error;
}

/*This could be made common with sctp_detach() since they are identical */
static void
sctp6_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct sctp_inpcb *inp;
	int error;

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		error = EINVAL;
		goto out;
	}
	crit_enter();
	if (((so->so_options & SO_LINGER) && (so->so_linger == 0)) ||
	    (so->so_rcv.ssb_cc > 0))
		sctp_inpcb_free(inp, 1);
	else
		sctp_inpcb_free(inp, 0);
	crit_exit();
	error = 0;
out:
	lwkt_replymsg(&msg->lmsg, error);
}

static void
sctp6_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct sctp_inpcb *inp;
	int error;

	crit_enter();
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		crit_exit();
		error = ENOTCONN;
		goto out;
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		if (LIST_EMPTY(&inp->sctp_asoc_list)) {
			/* No connection */
			crit_exit();
			error = ENOTCONN;
		} else {
			int some_on_streamwheel = 0;
			struct sctp_association *asoc;
			struct sctp_tcb *stcb;

			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb == NULL) {
				crit_exit();
				error = EINVAL;
				goto out;
			}
			asoc = &stcb->asoc;
			if (!TAILQ_EMPTY(&asoc->out_wheel)) {
				/* Check to see if some data queued */
				struct sctp_stream_out *outs;
				TAILQ_FOREACH(outs, &asoc->out_wheel,
					      next_spoke) {
					if (!TAILQ_EMPTY(&outs->outqueue)) {
						some_on_streamwheel = 1;
						break;
					}
				}
			}

			if (TAILQ_EMPTY(&asoc->send_queue) &&
			    TAILQ_EMPTY(&asoc->sent_queue) &&
			    (some_on_streamwheel == 0)) {
				/* nothing queued to send, so I'm done... */
				if ((SCTP_GET_STATE(asoc) !=
				     SCTP_STATE_SHUTDOWN_SENT) &&
				    (SCTP_GET_STATE(asoc) !=
				     SCTP_STATE_SHUTDOWN_ACK_SENT)) {
					/* only send SHUTDOWN the first time */
#ifdef SCTP_DEBUG
					if (sctp_debug_on & SCTP_DEBUG_OUTPUT4) {
						kprintf("%s:%d sends a shutdown\n",
						       __FILE__,
						       __LINE__
							);
					}
#endif
					sctp_send_shutdown(stcb, stcb->asoc.primary_destination);
					sctp_chunk_output(stcb->sctp_ep, stcb, 1);
					asoc->state = SCTP_STATE_SHUTDOWN_SENT;
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN,
							 stcb->sctp_ep, stcb,
							 asoc->primary_destination);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
							 stcb->sctp_ep, stcb,
							 asoc->primary_destination);
				}
			} else {
				/*
				 * we still got (or just got) data to send,
				 * so set SHUTDOWN_PENDING
				 */
				/*
				 * XXX sockets draft says that MSG_EOF should
				 * be sent with no data.  currently, we will
				 * allow user data to be sent first and move
				 * to SHUTDOWN-PENDING
				 */
				asoc->state |= SCTP_STATE_SHUTDOWN_PENDING;
			}
			crit_exit();
			error = 0;
		}
	} else {
		/* UDP model does not support this */
		crit_exit();
		error = EOPNOTSUPP;
	}
out:
	lwkt_replymsg(&msg->lmsg, error);
}

static
void
sctp6_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct mbuf *control = msg->send.nm_control;
	struct sockaddr *addr = msg->send.nm_addr;
	struct sctp_inpcb *inp;
	struct in6pcb *inp6;
	int flags = msg->send.nm_flags;
#ifdef INET
	struct sockaddr_in6 *sin6;
#endif /* INET */
	int error;
	/* No SPL needed since sctp_output does this */

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
	        if (control) {
			m_freem(control);
			control = NULL;
		}
		m_freem(m);
		error = EINVAL;
		goto out;
	}
	inp6 = (struct in6pcb *)inp;
	/* For the TCP model we may get a NULL addr, if we
	 * are a connected socket thats ok.
	 */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) &&
	    (addr == NULL)) {
	        goto connected_type;
	}
	if (addr == NULL) {
		m_freem(m);
		if (control) {
			m_freem(control);
			control = NULL;
		}
		error = EDESTADDRREQ;
		goto out;
	}

#ifdef INET
	sin6 = (struct sockaddr_in6 *)addr;
	/*
	 * We discard datagrams destined to a v4 addr or v4-mapped addr
	 */
	if (addr->sa_family == AF_INET) {
		error = EINVAL;
		goto out;
	}
	if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		/* mapped addresses aren't enabled */
		error = EINVAL;
		goto out;
	}
#endif /* INET */
connected_type:
	/* now what about control */
	if (control) {
		if (inp->control) {
			kprintf("huh? control set?\n");
			m_freem(inp->control);
			inp->control = NULL;
		}
		inp->control = control;
	}
	/* add it in possibly */
	if ((inp->pkt) &&
	    (inp->pkt->m_flags & M_PKTHDR)) {
		struct mbuf *x;
		int c_len;

		c_len = 0;
		/* How big is it */
		for (x=m;x;x = x->m_next) {
			c_len += x->m_len;
		}
		inp->pkt->m_pkthdr.len += c_len;
	}
	/* Place the data */
	if (inp->pkt) {
		inp->pkt_last->m_next = m;
		inp->pkt_last = m;
	} else {
		inp->pkt_last = inp->pkt = m;
	}
	if (
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
	    /* FreeBSD and MacOSX uses a flag passed */
	    (!(flags & PRUS_MORETOCOME))
#elif defined(__NetBSD__)
	    /* NetBSD uses the so_state field */
	    (!(so->so_state & SS_MORETOCOME))
#else
	    1   /* Open BSD does not have any "more to come" indication */
#endif
	    ) {
		/*
		 * note with the current version this code will only be
		 * used by OpenBSD, NetBSD and FreeBSD have methods for
		 * re-defining sosend() to use sctp_sosend().  One can
		 * optionaly switch back to this code (by changing back
		 * the defininitions but this is not advisable.
		 */
		error = sctp_output(inp, inp->pkt, addr,
				    inp->control, msg->send.nm_td, flags);
		inp->pkt = NULL;
		inp->control = NULL;
	} else {
		error = 0;
	}
out:
	lwkt_replymsg(&msg->lmsg, error);
}

static void
sctp6_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *addr = msg->connect.nm_nam;
	struct sctp_inpcb *inp;
	struct in6pcb *inp6;
	struct sctp_tcb *stcb;
#ifdef INET
	struct sockaddr_in6 *sin6;
#endif /* INET */
	int error = 0;

	crit_enter();
	inp6 = (struct in6pcb *)so->so_pcb;
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		crit_exit();
		error = ECONNRESET;	/* I made the same as TCP since
					 * we are not setup? */
		goto out;
	}
	SCTP_ASOC_CREATE_LOCK(inp);
	SCTP_INP_RLOCK(inp);
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) ==
	    SCTP_PCB_FLAGS_UNBOUND) {
		/* Bind a ephemeral port */
		SCTP_INP_RUNLOCK(inp);
		error = sctp6_bind_oncpu(so, NULL, msg->connect.nm_td);
		if (error) {
			crit_exit();
			SCTP_ASOC_CREATE_UNLOCK(inp);
			goto out;
		}
		SCTP_INP_RLOCK(inp);
	}

	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* We are already connected AND the TCP model */
		crit_exit();
		SCTP_INP_RUNLOCK(inp);
		SCTP_ASOC_CREATE_UNLOCK(inp);
		error = EADDRINUSE;
		goto out;
	}

#ifdef INET
	sin6 = (struct sockaddr_in6 *)addr;
	/*
	 * Ignore connections destined to a v4 addr or v4-mapped addr
	 */
	if (addr->sa_family == AF_INET) {
		crit_exit();
		SCTP_INP_RUNLOCK(inp);
		SCTP_ASOC_CREATE_UNLOCK(inp);
		error = EINVAL;
		goto out;
	}
	if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		/* mapped addresses aren't enabled */
		crit_exit();
		SCTP_INP_RUNLOCK(inp);
		SCTP_ASOC_CREATE_UNLOCK(inp);
		error = EINVAL;
		goto out;
	} else
#endif /* INET */
		addr = addr;	/* for true v6 address case */

	/* Now do we connect? */
	if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		if (stcb)
			SCTP_TCB_UNLOCK (stcb);
		SCTP_INP_RUNLOCK(inp);
	}else {
		SCTP_INP_RUNLOCK(inp);
		SCTP_INP_WLOCK(inp);
		SCTP_INP_INCR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
		stcb = sctp_findassociation_ep_addr(&inp, addr, NULL, NULL, NULL);
		if (stcb == NULL) {
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		}
	}

	if (stcb != NULL) {
		/* Already have or am bring up an association */
		SCTP_ASOC_CREATE_UNLOCK(inp);
		SCTP_TCB_UNLOCK (stcb);
		crit_exit();
		error = EALREADY;
		goto out;
	}
	/* We are GOOD to go */
	stcb = sctp_aloc_assoc(inp, addr, 1, &error, 0);
	SCTP_ASOC_CREATE_UNLOCK(inp);
	if (stcb == NULL) {
		/* Gak! no memory */
		crit_exit();
		goto out;
	}
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
		/* Set the connected flag so we can queue data */
		soisconnecting(so);
	}
	stcb->asoc.state = SCTP_STATE_COOKIE_WAIT;
	SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);
	sctp_send_initiate(inp, stcb);
	SCTP_TCB_UNLOCK (stcb);
	crit_exit();
out:
	lwkt_replymsg(&msg->lmsg, error);
}

static int
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
sctp6_getaddr(struct socket *so, struct sockaddr **addr)
{
	struct sockaddr_in6 *sin6;
#else
sctp6_getaddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_in6 *sin6 = mtod(nam, struct sockaddr_in6 *);
#endif
	struct sctp_inpcb *inp;
	/*
	 * Do the malloc first in case it blocks.
	 */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
	sin6 = kmalloc(sizeof *sin6, M_SONAME, M_WAITOK | M_ZERO);
#else
	nam->m_len = sizeof(*sin6);
#endif
	bzero(sin6, sizeof(*sin6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);

	inp = (struct sctp_inpcb *)so->so_pcb;
	if (!inp) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
		kfree(sin6, M_SONAME);
#endif
		return ECONNRESET;
	}

	sin6->sin6_port = inp->sctp_lport;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/* For the bound all case you get back 0 */
		if (inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) {
			struct sctp_tcb *stcb;
			struct sockaddr_in6 *sin_a6;
			struct sctp_nets *net;
			int fnd;

			stcb = LIST_FIRST(&inp->sctp_asoc_list);
			if (stcb == NULL) {
				goto notConn6;
			}
			fnd = 0;
			sin_a6 = NULL;
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				sin_a6 = (struct sockaddr_in6 *)&net->ro._l_addr;
				if (sin_a6->sin6_family == AF_INET6) {
					fnd = 1;
					break;
				}
			}
			if ((!fnd) || (sin_a6 == NULL)) {
				/* punt */
				goto notConn6;
			}
			sin6->sin6_addr = sctp_ipv6_source_address_selection(
			    inp, stcb, (struct route *)&net->ro, net, 0);

		} else {
			/* For the bound all case you get back 0 */
		notConn6:
			memset(&sin6->sin6_addr, 0, sizeof(sin6->sin6_addr));
		}
	} else {
		/* Take the first IPv6 address in the list */
		struct sctp_laddr *laddr;
		int fnd = 0;
		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa->ifa_addr->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin_a;
				sin_a = (struct sockaddr_in6 *)laddr->ifa->ifa_addr;
				sin6->sin6_addr = sin_a->sin6_addr;
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
			kfree(sin6, M_SONAME);
#endif
			return ENOENT;
		}
	}
	/* Scoping things for v6 */
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		/* skip ifp check below */
		in6_recoverscope(sin6, &sin6->sin6_addr, NULL);
	else
		sin6->sin6_scope_id = 0;	/*XXX*/
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
	(*addr) = (struct sockaddr *)sin6;
#endif
	return (0);
}

static int
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
sctp6_peeraddr(struct socket *so, struct sockaddr **addr)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)*addr;
#else
sctp6_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct sockaddr_in6 *sin6 = mtod(nam, struct sockaddr_in6 *);
#endif
	int fnd;
	struct sockaddr_in6 *sin_a6;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;
	/*
	 * Do the malloc first in case it blocks.
	 */
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (!(inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED)) {
		/* UDP type and listeners will drop out here */
		return (ENOTCONN);
	}
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
	sin6 = kmalloc(sizeof *sin6, M_SONAME, M_WAITOK | M_ZERO);
#else
	nam->m_len = sizeof(*sin6);
#endif
	bzero(sin6, sizeof(*sin6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);

	/* We must recapture incase we blocked */
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (!inp) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
		kfree(sin6, M_SONAME);
#endif
		return ECONNRESET;
	}
	stcb = LIST_FIRST(&inp->sctp_asoc_list);
	if (stcb == NULL) {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
		kfree(sin6, M_SONAME);
#endif
		return ECONNRESET;
	}
	fnd = 0;
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		sin_a6 = (struct sockaddr_in6 *)&net->ro._l_addr;
		if (sin_a6->sin6_family == AF_INET6) {
			fnd = 1;
			sin6->sin6_port = stcb->rport;
			sin6->sin6_addr = sin_a6->sin6_addr;
			break;
		}
	}
	if (!fnd) {
		/* No IPv4 address */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
		kfree(sin6, M_SONAME);
#endif
		return ENOENT;
	}
	in6_recoverscope(sin6, &sin6->sin6_addr, NULL);
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
	*addr = (struct sockaddr *)sin6;
#endif
	return (0);
}

static void
sctp6_in6getaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **nam = msg->sockaddr.nm_nam;
	struct in6pcb *inp6 = sotoin6pcb(so);
	int error;

	if (inp6 == NULL) {
		error = EINVAL;
		goto out;
	}

	crit_enter();
	/* allow v6 addresses precedence */
	error = sctp6_getaddr(so, nam);
	crit_exit();
out:
	lwkt_replymsg(&msg->lmsg, error);
}

static void
sctp6_getpeeraddr(netmsg_t msg)
{
	struct socket *so = msg->peeraddr.base.nm_so;
	struct sockaddr **nam = msg->peeraddr.nm_nam;
	struct in6pcb *inp6 = sotoin6pcb(so);
	int error;

	if (inp6 == NULL) {
		error = EINVAL;
		goto out;
	}

	crit_enter();
	/* allow v6 addresses precedence */
	error = sctp6_peeraddr(so, nam);
	crit_exit();
out:
	lwkt_replymsg(&msg->lmsg, error);
}

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
struct pr_usrreqs sctp6_usrreqs = {
	.pru_abort = sctp6_abort,
	.pru_accept = sctp_accept,
	.pru_attach = sctp6_attach,
	.pru_bind = sctp6_bind,
	.pru_connect = sctp6_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in6_control_dispatch,
	.pru_detach = sctp6_detach,
	.pru_disconnect = sctp6_disconnect,
	.pru_listen = sctp_listen,
	.pru_peeraddr = sctp6_getpeeraddr,
	.pru_rcvd = sctp_usr_recvd,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = sctp6_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = sctp_shutdown,
	.pru_sockaddr = sctp6_in6getaddr,
	.pru_sosend = sctp_sosend,
	.pru_soreceive = soreceive
};

#else

#error x

int
sctp6_usrreq(struct socket *so, int req, struct mbuf *m, struct mbuf *nam,
	     struct mbuf *control, struct proc *p)
{
	int s;
	int error = 0;
	int family;

#if defined(__OpenBSD__)
	p = curproc;
#endif
	s = splsoftnet();
	family = so->so_proto->pr_domain->dom_family;

	if (req == PRU_CONTROL) {
		switch (family) {
		case PF_INET:
			error = in_control(so, (long)m, (caddr_t)nam,
			    (struct ifnet *)control
#if defined(__NetBSD__)
			     , p
#endif
			     );
#ifdef INET6
		case PF_INET6:
			error = in6_control(so, (long)m, (caddr_t)nam,
			    (struct ifnet *)control, p);
#endif
		default:
			error = EAFNOSUPPORT;
		}
		splx(s);
		return (error);
	}
#ifdef __NetBSD__
	if (req == PRU_PURGEIF) {
		struct ifnet *ifn;
		struct ifaddr *ifa;
		ifn = (struct ifnet *)control;
		TAILQ_FOREACH(ifa, &ifn->if_addrlist, ifa_list) {
			if (ifa->ifa_addr->sa_family == family) {
				sctp_delete_ip_address(ifa);
			}
		}
		switch (family) {
		case PF_INET:
			in_purgeif (ifn);
			break;
#ifdef INET6
		case PF_INET6:
			in6_purgeif (ifn);
			break;
#endif
		default:
			splx(s);
			return (EAFNOSUPPORT);
		}
		splx(s);
		return (0);
	}
#endif
	switch (req) {
	case PRU_ATTACH:
		error = sctp6_attach(so, family, p);
		break;
	case PRU_DETACH:
		error = sctp6_detach(so);
		break;
	case PRU_BIND:
		if (nam == NULL)
			return (EINVAL);
		error = sctp6_bind(so, nam, p);
		break;
	case PRU_LISTEN:
		error = sctp_listen(so, p);
		break;
	case PRU_CONNECT:
		if (nam == NULL)
			return (EINVAL);
		error = sctp6_connect(so, nam, p);
		break;
	case PRU_DISCONNECT:
		error = sctp6_disconnect(so);
		break;
	case PRU_ACCEPT:
		if (nam == NULL)
			return (EINVAL);
		error = sctp_accept(so, nam);
		break;
	case PRU_SHUTDOWN:
		error = sctp_shutdown(so);
		break;

	case PRU_RCVD:
		/*
		 * For OpenBSD and NetBSD, this is real ugly. The (mbuf *)
		 * nam that is passed (by soreceive()) is the int flags
		 * cast as a (mbuf *) yuck!
		 */
 		error = sctp_usr_recvd(so, (int)((long)nam));
		break;

	case PRU_SEND:
		/* Flags are ignored */
		error = sctp6_send(so, 0, m, nam, control, p);
		break;
	case PRU_ABORT:
		error = sctp6_abort(so);
		break;

	case PRU_SENSE:
		error = 0;
		break;
	case PRU_RCVOOB:
		error = EAFNOSUPPORT;
		break;
	case PRU_SENDOOB:
		error = EAFNOSUPPORT;
		break;
	case PRU_PEERADDR:
		error = sctp6_getpeeraddr(so, nam);
		break;
	case PRU_SOCKADDR:
		error = sctp6_in6getaddr(so, nam);
		break;
	case PRU_SLOWTIMO:
		error = 0;
		break;
	default:
		break;
	}
	splx(s);
	return (error);
}
#endif
