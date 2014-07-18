/*
 * Copyright (c) 1989, 1991, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_socket.c	8.5 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/nfs/nfs_socket.c,v 1.60.2.6 2003/03/26 01:44:46 alfred Exp $
 */

/*
 * Socket operations for use by nfs
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/protosw.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/syslog.h>
#include <sys/thread.h>
#include <sys/tprintf.h>
#include <sys/sysctl.h>
#include <sys/signalvar.h>

#include <sys/signal2.h>
#include <sys/mutex2.h>
#include <sys/socketvar2.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/thread2.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "nfsrtt.h"

#define	TRUE	1
#define	FALSE	0

/*
 * RTT calculations are scaled by 256 (8 bits).  A proper fractional
 * RTT will still be calculated even with a slow NFS timer.
 */
#define	NFS_SRTT(r)	(r)->r_nmp->nm_srtt[proct[(r)->r_procnum]]
#define	NFS_SDRTT(r)	(r)->r_nmp->nm_sdrtt[proct[(r)->r_procnum]]
#define NFS_RTT_SCALE_BITS	8	/* bits */
#define NFS_RTT_SCALE		256	/* value */

/*
 * Defines which timer to use for the procnum.
 * 0 - default
 * 1 - getattr
 * 2 - lookup
 * 3 - read
 * 4 - write
 */
static int proct[NFS_NPROCS] = {
	0, 1, 0, 2, 1, 3, 3, 4, 0, 0,	/* 00-09	*/
	0, 0, 0, 0, 0, 0, 3, 3, 0, 0,	/* 10-19	*/
	0, 5, 0, 0, 0, 0,		/* 20-29	*/
};

static int multt[NFS_NPROCS] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	/* 00-09	*/
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	/* 10-19	*/
	1, 2, 1, 1, 1, 1,		/* 20-29	*/
};

static int nfs_backoff[8] = { 2, 3, 5, 8, 13, 21, 34, 55 };
static int nfs_realign_test;
static int nfs_realign_count;
static int nfs_showrtt;
static int nfs_showrexmit;
int nfs_maxasyncbio = NFS_MAXASYNCBIO;

SYSCTL_DECL(_vfs_nfs);

SYSCTL_INT(_vfs_nfs, OID_AUTO, realign_test, CTLFLAG_RW, &nfs_realign_test, 0,
    "Number of times mbufs have been tested for bad alignment");
SYSCTL_INT(_vfs_nfs, OID_AUTO, realign_count, CTLFLAG_RW, &nfs_realign_count, 0,
    "Number of realignments for badly aligned mbuf data");
SYSCTL_INT(_vfs_nfs, OID_AUTO, showrtt, CTLFLAG_RW, &nfs_showrtt, 0,
    "Show round trip time output");
SYSCTL_INT(_vfs_nfs, OID_AUTO, showrexmit, CTLFLAG_RW, &nfs_showrexmit, 0,
    "Show retransmits info");
SYSCTL_INT(_vfs_nfs, OID_AUTO, maxasyncbio, CTLFLAG_RW, &nfs_maxasyncbio, 0,
    "Max number of asynchronous bio's");

static int nfs_request_setup(nfsm_info_t info);
static int nfs_request_auth(struct nfsreq *rep);
static int nfs_request_try(struct nfsreq *rep);
static int nfs_request_waitreply(struct nfsreq *rep);
static int nfs_request_processreply(nfsm_info_t info, int);

int nfsrtton = 0;
struct nfsrtt nfsrtt;
struct callout	nfs_timer_handle;

static int	nfs_msg (struct thread *,char *,char *);
static int	nfs_rcvlock (struct nfsmount *nmp, struct nfsreq *myreq);
static void	nfs_rcvunlock (struct nfsmount *nmp);
static void	nfs_realign (struct mbuf **pm, int hsiz);
static int	nfs_receive (struct nfsmount *nmp, struct nfsreq *rep,
				struct sockaddr **aname, struct mbuf **mp);
static void	nfs_softterm (struct nfsreq *rep, int islocked);
static void	nfs_hardterm (struct nfsreq *rep, int islocked);
static int	nfs_reconnect (struct nfsmount *nmp, struct nfsreq *rep);
#ifndef NFS_NOSERVER 
static int	nfsrv_getstream (struct nfssvc_sock *, int, int *);
static void	nfs_timer_req(struct nfsreq *req);
static void	nfs_checkpkt(struct mbuf *m, int len);

int (*nfsrv3_procs[NFS_NPROCS]) (struct nfsrv_descript *nd,
				    struct nfssvc_sock *slp,
				    struct thread *td,
				    struct mbuf **mreqp) = {
	nfsrv_null,
	nfsrv_getattr,
	nfsrv_setattr,
	nfsrv_lookup,
	nfsrv3_access,
	nfsrv_readlink,
	nfsrv_read,
	nfsrv_write,
	nfsrv_create,
	nfsrv_mkdir,
	nfsrv_symlink,
	nfsrv_mknod,
	nfsrv_remove,
	nfsrv_rmdir,
	nfsrv_rename,
	nfsrv_link,
	nfsrv_readdir,
	nfsrv_readdirplus,
	nfsrv_statfs,
	nfsrv_fsinfo,
	nfsrv_pathconf,
	nfsrv_commit,
	nfsrv_noop,
	nfsrv_noop,
	nfsrv_noop,
	nfsrv_noop
};
#endif /* NFS_NOSERVER */

/*
 * Initialize sockets and congestion for a new NFS connection.
 * We do not free the sockaddr if error.
 */
int
nfs_connect(struct nfsmount *nmp, struct nfsreq *rep)
{
	struct socket *so;
	int error;
	struct sockaddr *saddr;
	struct sockaddr_in *sin;
	struct thread *td = &thread0; /* only used for socreate and sobind */

	nmp->nm_so = so = NULL;
	if (nmp->nm_flag & NFSMNT_FORCE)
		return (EINVAL);
	saddr = nmp->nm_nam;
	error = socreate(saddr->sa_family, &so, nmp->nm_sotype,
		nmp->nm_soproto, td);
	if (error)
		goto bad;
	nmp->nm_soflags = so->so_proto->pr_flags;

	/*
	 * Some servers require that the client port be a reserved port number.
	 */
	if (saddr->sa_family == AF_INET && (nmp->nm_flag & NFSMNT_RESVPORT)) {
		struct sockopt sopt;
		int ip;
		struct sockaddr_in ssin;

		bzero(&sopt, sizeof sopt);
		ip = IP_PORTRANGE_LOW;
		sopt.sopt_level = IPPROTO_IP;
		sopt.sopt_name = IP_PORTRANGE;
		sopt.sopt_val = (void *)&ip;
		sopt.sopt_valsize = sizeof(ip);
		sopt.sopt_td = NULL;
		error = sosetopt(so, &sopt);
		if (error)
			goto bad;
		bzero(&ssin, sizeof ssin);
		sin = &ssin;
		sin->sin_len = sizeof (struct sockaddr_in);
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = INADDR_ANY;
		sin->sin_port = htons(0);
		error = sobind(so, (struct sockaddr *)sin, td);
		if (error)
			goto bad;
		bzero(&sopt, sizeof sopt);
		ip = IP_PORTRANGE_DEFAULT;
		sopt.sopt_level = IPPROTO_IP;
		sopt.sopt_name = IP_PORTRANGE;
		sopt.sopt_val = (void *)&ip;
		sopt.sopt_valsize = sizeof(ip);
		sopt.sopt_td = NULL;
		error = sosetopt(so, &sopt);
		if (error)
			goto bad;
	}

	/*
	 * Protocols that do not require connections may be optionally left
	 * unconnected for servers that reply from a port other than NFS_PORT.
	 */
	if (nmp->nm_flag & NFSMNT_NOCONN) {
		if (nmp->nm_soflags & PR_CONNREQUIRED) {
			error = ENOTCONN;
			goto bad;
		}
	} else {
		error = soconnect(so, nmp->nm_nam, td, TRUE);
		if (error)
			goto bad;

		/*
		 * Wait for the connection to complete. Cribbed from the
		 * connect system call but with the wait timing out so
		 * that interruptible mounts don't hang here for a long time.
		 */
		crit_enter();
		while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
			(void) tsleep((caddr_t)&so->so_timeo, 0,
				"nfscon", 2 * hz);
			if ((so->so_state & SS_ISCONNECTING) &&
			    so->so_error == 0 && rep &&
			    (error = nfs_sigintr(nmp, rep, rep->r_td)) != 0){
				soclrstate(so, SS_ISCONNECTING);
				crit_exit();
				goto bad;
			}
		}
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			crit_exit();
			goto bad;
		}
		crit_exit();
	}
	so->so_rcv.ssb_timeo = (5 * hz);
	so->so_snd.ssb_timeo = (5 * hz);

	/*
	 * Get buffer reservation size from sysctl, but impose reasonable
	 * limits.
	 */
	if (nmp->nm_sotype == SOCK_STREAM) {
		if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
			struct sockopt sopt;
			int val;

			bzero(&sopt, sizeof sopt);
			sopt.sopt_level = SOL_SOCKET;
			sopt.sopt_name = SO_KEEPALIVE;
			sopt.sopt_val = &val;
			sopt.sopt_valsize = sizeof val;
			val = 1;
			sosetopt(so, &sopt);
		}
		if (so->so_proto->pr_protocol == IPPROTO_TCP) {
			struct sockopt sopt;
			int val;

			bzero(&sopt, sizeof sopt);
			sopt.sopt_level = IPPROTO_TCP;
			sopt.sopt_name = TCP_NODELAY;
			sopt.sopt_val = &val;
			sopt.sopt_valsize = sizeof val;
			val = 1;
			sosetopt(so, &sopt);

			bzero(&sopt, sizeof sopt);
			sopt.sopt_level = IPPROTO_TCP;
			sopt.sopt_name = TCP_FASTKEEP;
			sopt.sopt_val = &val;
			sopt.sopt_valsize = sizeof val;
			val = 1;
			sosetopt(so, &sopt);
		}
	}
	error = soreserve(so, nfs_soreserve, nfs_soreserve, NULL);
	if (error)
		goto bad;
	atomic_set_int(&so->so_rcv.ssb_flags, SSB_NOINTR);
	atomic_set_int(&so->so_snd.ssb_flags, SSB_NOINTR);

	/* Initialize other non-zero congestion variables */
	nmp->nm_srtt[0] = nmp->nm_srtt[1] = nmp->nm_srtt[2] = 
		nmp->nm_srtt[3] = (NFS_TIMEO << NFS_RTT_SCALE_BITS);
	nmp->nm_sdrtt[0] = nmp->nm_sdrtt[1] = nmp->nm_sdrtt[2] =
		nmp->nm_sdrtt[3] = 0;
	nmp->nm_maxasync_scaled = NFS_MINASYNC_SCALED;
	nmp->nm_timeouts = 0;

	/*
	 * Assign nm_so last.  The moment nm_so is assigned the nfs_timer()
	 * can mess with the socket.
	 */
	nmp->nm_so = so;
	return (0);

bad:
	if (so) {
		soshutdown(so, SHUT_RDWR);
		soclose(so, FNONBLOCK);
	}
	return (error);
}

/*
 * Reconnect routine:
 * Called when a connection is broken on a reliable protocol.
 * - clean up the old socket
 * - nfs_connect() again
 * - set R_NEEDSXMIT for all outstanding requests on mount point
 * If this fails the mount point is DEAD!
 * nb: Must be called with the nfs_sndlock() set on the mount point.
 */
static int
nfs_reconnect(struct nfsmount *nmp, struct nfsreq *rep)
{
	struct nfsreq *req;
	int error;

	nfs_disconnect(nmp);
	if (nmp->nm_rxstate >= NFSSVC_STOPPING)
		return (EINTR);
	while ((error = nfs_connect(nmp, rep)) != 0) {
		if (error == EINTR || error == ERESTART)
			return (EINTR);
		if (error == EINVAL)
			return (error);
		if (nmp->nm_rxstate >= NFSSVC_STOPPING)
			return (EINTR);
		(void) tsleep((caddr_t)&lbolt, 0, "nfscon", 0);
	}

	/*
	 * Loop through outstanding request list and fix up all requests
	 * on old socket.
	 */
	crit_enter();
	TAILQ_FOREACH(req, &nmp->nm_reqq, r_chain) {
		KKASSERT(req->r_nmp == nmp);
		req->r_flags |= R_NEEDSXMIT;
	}
	crit_exit();
	return (0);
}

/*
 * NFS disconnect. Clean up and unlink.
 */
void
nfs_disconnect(struct nfsmount *nmp)
{
	struct socket *so;

	if (nmp->nm_so) {
		so = nmp->nm_so;
		nmp->nm_so = NULL;
		soshutdown(so, SHUT_RDWR);
		soclose(so, FNONBLOCK);
	}
}

void
nfs_safedisconnect(struct nfsmount *nmp)
{
	nfs_rcvlock(nmp, NULL);
	nfs_disconnect(nmp);
	nfs_rcvunlock(nmp);
}

/*
 * This is the nfs send routine. For connection based socket types, it
 * must be called with an nfs_sndlock() on the socket.
 * "rep == NULL" indicates that it has been called from a server.
 * For the client side:
 * - return EINTR if the RPC is terminated, 0 otherwise
 * - set R_NEEDSXMIT if the send fails for any reason
 * - do any cleanup required by recoverable socket errors (?)
 * For the server side:
 * - return EINTR or ERESTART if interrupted by a signal
 * - return EPIPE if a connection is lost for connection based sockets (TCP...)
 * - do any cleanup required by recoverable socket errors (?)
 */
int
nfs_send(struct socket *so, struct sockaddr *nam, struct mbuf *top,
	 struct nfsreq *rep)
{
	struct sockaddr *sendnam;
	int error, soflags, flags;

	if (rep) {
		if (rep->r_flags & R_SOFTTERM) {
			m_freem(top);
			return (EINTR);
		}
		if ((so = rep->r_nmp->nm_so) == NULL) {
			rep->r_flags |= R_NEEDSXMIT;
			m_freem(top);
			return (0);
		}
		rep->r_flags &= ~R_NEEDSXMIT;
		soflags = rep->r_nmp->nm_soflags;
	} else {
		soflags = so->so_proto->pr_flags;
	}
	if ((soflags & PR_CONNREQUIRED) || (so->so_state & SS_ISCONNECTED))
		sendnam = NULL;
	else
		sendnam = nam;
	if (so->so_type == SOCK_SEQPACKET)
		flags = MSG_EOR;
	else
		flags = 0;

	/*
	 * calls pru_sosend -> sosend -> so_pru_send -> netrpc
	 */
	error = so_pru_sosend(so, sendnam, NULL, top, NULL, flags,
			      curthread /*XXX*/);

	/*
	 * ENOBUFS for dgram sockets is transient and non fatal.
	 * No need to log, and no need to break a soft mount.
	 */
	if (error == ENOBUFS && so->so_type == SOCK_DGRAM) {
		error = 0;
		/*
		 * do backoff retransmit on client
		 */
		if (rep) {
			if ((rep->r_nmp->nm_state & NFSSTA_SENDSPACE) == 0) {
				rep->r_nmp->nm_state |= NFSSTA_SENDSPACE;
				kprintf("Warning: NFS: Insufficient sendspace "
					"(%lu),\n"
					"\t You must increase vfs.nfs.soreserve"
					"or decrease vfs.nfs.maxasyncbio\n",
					so->so_snd.ssb_hiwat);
			}
			rep->r_flags |= R_NEEDSXMIT;
		}
	}

	if (error) {
		if (rep) {
			log(LOG_INFO, "nfs send error %d for server %s\n",error,
			    rep->r_nmp->nm_mountp->mnt_stat.f_mntfromname);
			/*
			 * Deal with errors for the client side.
			 */
			if (rep->r_flags & R_SOFTTERM)
				error = EINTR;
			else
				rep->r_flags |= R_NEEDSXMIT;
		} else {
			log(LOG_INFO, "nfsd send error %d\n", error);
		}

		/*
		 * Handle any recoverable (soft) socket errors here. (?)
		 */
		if (error != EINTR && error != ERESTART &&
			error != EWOULDBLOCK && error != EPIPE)
			error = 0;
	}
	return (error);
}

/*
 * Receive a Sun RPC Request/Reply. For SOCK_DGRAM, the work is all
 * done by soreceive(), but for SOCK_STREAM we must deal with the Record
 * Mark and consolidate the data into a new mbuf list.
 * nb: Sometimes TCP passes the data up to soreceive() in long lists of
 *     small mbufs.
 * For SOCK_STREAM we must be very careful to read an entire record once
 * we have read any of it, even if the system call has been interrupted.
 */
static int
nfs_receive(struct nfsmount *nmp, struct nfsreq *rep,
	    struct sockaddr **aname, struct mbuf **mp)
{
	struct socket *so;
	struct sockbuf sio;
	struct uio auio;
	struct iovec aio;
	struct mbuf *m;
	struct mbuf *control;
	u_int32_t len;
	struct sockaddr **getnam;
	int error, sotype, rcvflg;
	struct thread *td = curthread;	/* XXX */

	/*
	 * Set up arguments for soreceive()
	 */
	*mp = NULL;
	*aname = NULL;
	sotype = nmp->nm_sotype;

	/*
	 * For reliable protocols, lock against other senders/receivers
	 * in case a reconnect is necessary.
	 * For SOCK_STREAM, first get the Record Mark to find out how much
	 * more there is to get.
	 * We must lock the socket against other receivers
	 * until we have an entire rpc request/reply.
	 */
	if (sotype != SOCK_DGRAM) {
		error = nfs_sndlock(nmp, rep);
		if (error)
			return (error);
tryagain:
		/*
		 * Check for fatal errors and resending request.
		 */
		/*
		 * Ugh: If a reconnect attempt just happened, nm_so
		 * would have changed. NULL indicates a failed
		 * attempt that has essentially shut down this
		 * mount point.
		 */
		if (rep && (rep->r_mrep || (rep->r_flags & R_SOFTTERM))) {
			nfs_sndunlock(nmp);
			return (EINTR);
		}
		so = nmp->nm_so;
		if (so == NULL) {
			error = nfs_reconnect(nmp, rep);
			if (error) {
				nfs_sndunlock(nmp);
				return (error);
			}
			goto tryagain;
		}
		while (rep && (rep->r_flags & R_NEEDSXMIT)) {
			m = m_copym(rep->r_mreq, 0, M_COPYALL, MB_WAIT);
			nfsstats.rpcretries++;
			error = nfs_send(so, rep->r_nmp->nm_nam, m, rep);
			if (error) {
				if (error == EINTR || error == ERESTART ||
				    (error = nfs_reconnect(nmp, rep)) != 0) {
					nfs_sndunlock(nmp);
					return (error);
				}
				goto tryagain;
			}
		}
		nfs_sndunlock(nmp);
		if (sotype == SOCK_STREAM) {
			/*
			 * Get the length marker from the stream
			 */
			aio.iov_base = (caddr_t)&len;
			aio.iov_len = sizeof(u_int32_t);
			auio.uio_iov = &aio;
			auio.uio_iovcnt = 1;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_READ;
			auio.uio_offset = 0;
			auio.uio_resid = sizeof(u_int32_t);
			auio.uio_td = td;
			do {
			   rcvflg = MSG_WAITALL;
			   error = so_pru_soreceive(so, NULL, &auio, NULL,
						    NULL, &rcvflg);
			   if (error == EWOULDBLOCK && rep) {
				if (rep->r_flags & R_SOFTTERM)
					return (EINTR);
			   }
			} while (error == EWOULDBLOCK);

			if (error == 0 && auio.uio_resid > 0) {
			    /*
			     * Only log short packets if not EOF
			     */
			    if (auio.uio_resid != sizeof(u_int32_t)) {
				log(LOG_INFO,
				    "short receive (%d/%d) from nfs server %s\n",
				    (int)(sizeof(u_int32_t) - auio.uio_resid),
				    (int)sizeof(u_int32_t),
				    nmp->nm_mountp->mnt_stat.f_mntfromname);
			    }
			    error = EPIPE;
			}
			if (error)
				goto errout;
			len = ntohl(len) & ~0x80000000;
			/*
			 * This is SERIOUS! We are out of sync with the sender
			 * and forcing a disconnect/reconnect is all I can do.
			 */
			if (len > NFS_MAXPACKET) {
			    log(LOG_ERR, "%s (%d) from nfs server %s\n",
				"impossible packet length",
				len,
				nmp->nm_mountp->mnt_stat.f_mntfromname);
			    error = EFBIG;
			    goto errout;
			}

			/*
			 * Get the rest of the packet as an mbuf chain
			 */
			sbinit(&sio, len);
			do {
			    rcvflg = MSG_WAITALL;
			    error = so_pru_soreceive(so, NULL, NULL, &sio,
						     NULL, &rcvflg);
			} while (error == EWOULDBLOCK || error == EINTR ||
				 error == ERESTART);
			if (error == 0 && sio.sb_cc != len) {
			    if (sio.sb_cc != 0) {
				log(LOG_INFO,
				    "short receive (%zu/%d) from nfs server %s\n",
				    (size_t)len - auio.uio_resid, len,
				    nmp->nm_mountp->mnt_stat.f_mntfromname);
			    }
			    error = EPIPE;
			}
			*mp = sio.sb_mb;
		} else {
			/*
			 * Non-stream, so get the whole packet by not
			 * specifying MSG_WAITALL and by specifying a large
			 * length.
			 *
			 * We have no use for control msg., but must grab them
			 * and then throw them away so we know what is going
			 * on.
			 */
			sbinit(&sio, 100000000);
			do {
			    rcvflg = 0;
			    error =  so_pru_soreceive(so, NULL, NULL, &sio,
						      &control, &rcvflg);
			    if (control)
				m_freem(control);
			    if (error == EWOULDBLOCK && rep) {
				if (rep->r_flags & R_SOFTTERM) {
					m_freem(sio.sb_mb);
					return (EINTR);
				}
			    }
			} while (error == EWOULDBLOCK ||
				 (error == 0 && sio.sb_mb == NULL && control));
			if ((rcvflg & MSG_EOR) == 0)
				kprintf("Egad!!\n");
			if (error == 0 && sio.sb_mb == NULL)
				error = EPIPE;
			len = sio.sb_cc;
			*mp = sio.sb_mb;
		}
errout:
		if (error && error != EINTR && error != ERESTART) {
			m_freem(*mp);
			*mp = NULL;
			if (error != EPIPE) {
				log(LOG_INFO,
				    "receive error %d from nfs server %s\n",
				    error,
				 nmp->nm_mountp->mnt_stat.f_mntfromname);
			}
			error = nfs_sndlock(nmp, rep);
			if (!error) {
				error = nfs_reconnect(nmp, rep);
				if (!error)
					goto tryagain;
				else
					nfs_sndunlock(nmp);
			}
		}
	} else {
		if ((so = nmp->nm_so) == NULL)
			return (EACCES);
		if (so->so_state & SS_ISCONNECTED)
			getnam = NULL;
		else
			getnam = aname;
		sbinit(&sio, 100000000);
		do {
			rcvflg = 0;
			error =  so_pru_soreceive(so, getnam, NULL, &sio,
						  NULL, &rcvflg);
			if (error == EWOULDBLOCK && rep &&
			    (rep->r_flags & R_SOFTTERM)) {
				m_freem(sio.sb_mb);
				return (EINTR);
			}
		} while (error == EWOULDBLOCK);

		len = sio.sb_cc;
		*mp = sio.sb_mb;

		/*
		 * A shutdown may result in no error and no mbuf.
		 * Convert to EPIPE.
		 */
		if (*mp == NULL && error == 0)
			error = EPIPE;
	}
	if (error) {
		m_freem(*mp);
		*mp = NULL;
	}

	/*
	 * Search for any mbufs that are not a multiple of 4 bytes long
	 * or with m_data not longword aligned.
	 * These could cause pointer alignment problems, so copy them to
	 * well aligned mbufs.
	 */
	nfs_realign(mp, 5 * NFSX_UNSIGNED);
	return (error);
}

/*
 * Implement receipt of reply on a socket.
 *
 * We must search through the list of received datagrams matching them
 * with outstanding requests using the xid, until ours is found.
 *
 * If myrep is NULL we process packets on the socket until
 * interrupted or until nm_reqrxq is non-empty.
 */
/* ARGSUSED */
int
nfs_reply(struct nfsmount *nmp, struct nfsreq *myrep)
{
	struct nfsreq *rep;
	struct sockaddr *nam;
	u_int32_t rxid;
	u_int32_t *tl;
	int error;
	struct nfsm_info info;

	/*
	 * Loop around until we get our own reply
	 */
	for (;;) {
		/*
		 * Lock against other receivers so that I don't get stuck in
		 * sbwait() after someone else has received my reply for me.
		 * Also necessary for connection based protocols to avoid
		 * race conditions during a reconnect.
		 *
		 * If nfs_rcvlock() returns EALREADY, that means that
		 * the reply has already been recieved by another
		 * process and we can return immediately.  In this
		 * case, the lock is not taken to avoid races with
		 * other processes.
		 */
		info.mrep = NULL;

		error = nfs_rcvlock(nmp, myrep);
		if (error == EALREADY)
			return (0);
		if (error)
			return (error);

		/*
		 * If myrep is NULL we are the receiver helper thread.
		 * Stop waiting for incoming replies if there are
		 * messages sitting on reqrxq that we need to process,
		 * or if a shutdown request is pending.
		 */
		if (myrep == NULL && (TAILQ_FIRST(&nmp->nm_reqrxq) ||
		    nmp->nm_rxstate > NFSSVC_PENDING)) {
			nfs_rcvunlock(nmp);
			return(EWOULDBLOCK);
		}

		/*
		 * Get the next Rpc reply off the socket
		 *
		 * We cannot release the receive lock until we've
		 * filled in rep->r_mrep, otherwise a waiting
		 * thread may deadlock in soreceive with no incoming
		 * packets expected.
		 */
		error = nfs_receive(nmp, myrep, &nam, &info.mrep);
		if (error) {
			/*
			 * Ignore routing errors on connectionless protocols??
			 */
			nfs_rcvunlock(nmp);
			if (NFSIGNORE_SOERROR(nmp->nm_soflags, error)) {
				if (nmp->nm_so == NULL)
					return (error);
				nmp->nm_so->so_error = 0;
				continue;
			}
			return (error);
		}
		if (nam)
			kfree(nam, M_SONAME);

		/*
		 * Get the xid and check that it is an rpc reply
		 */
		info.md = info.mrep;
		info.dpos = mtod(info.md, caddr_t);
		NULLOUT(tl = nfsm_dissect(&info, 2*NFSX_UNSIGNED));
		rxid = *tl++;
		if (*tl != rpc_reply) {
			nfsstats.rpcinvalid++;
			m_freem(info.mrep);
			info.mrep = NULL;
nfsmout:
			nfs_rcvunlock(nmp);
			continue;
		}

		/*
		 * Loop through the request list to match up the reply
		 * Iff no match, just drop the datagram.  On match, set
		 * r_mrep atomically to prevent the timer from messing
		 * around with the request after we have exited the critical
		 * section.
		 */
		crit_enter();
		TAILQ_FOREACH(rep, &nmp->nm_reqq, r_chain) {
			if (rep->r_mrep == NULL && rxid == rep->r_xid)
				break;
		}

		/*
		 * Fill in the rest of the reply if we found a match.
		 *
		 * Deal with duplicate responses if there was no match.
		 */
		if (rep) {
			rep->r_md = info.md;
			rep->r_dpos = info.dpos;
			if (nfsrtton) {
				struct rttl *rt;

				rt = &nfsrtt.rttl[nfsrtt.pos];
				rt->proc = rep->r_procnum;
				rt->rto = 0;
				rt->sent = 0;
				rt->cwnd = nmp->nm_maxasync_scaled;
				rt->srtt = nmp->nm_srtt[proct[rep->r_procnum] - 1];
				rt->sdrtt = nmp->nm_sdrtt[proct[rep->r_procnum] - 1];
				rt->fsid = nmp->nm_mountp->mnt_stat.f_fsid;
				getmicrotime(&rt->tstamp);
				if (rep->r_flags & R_TIMING)
					rt->rtt = rep->r_rtt;
				else
					rt->rtt = 1000000;
				nfsrtt.pos = (nfsrtt.pos + 1) % NFSRTTLOGSIZ;
			}

			/*
			 * New congestion control is based only on async
			 * requests.
			 */
			if (nmp->nm_maxasync_scaled < NFS_MAXASYNC_SCALED)
				++nmp->nm_maxasync_scaled;
			if (rep->r_flags & R_SENT) {
				rep->r_flags &= ~R_SENT;
			}
			/*
			 * Update rtt using a gain of 0.125 on the mean
			 * and a gain of 0.25 on the deviation.
			 *
			 * NOTE SRTT/SDRTT are only good if R_TIMING is set.
			 */
			if ((rep->r_flags & R_TIMING) && rep->r_rexmit == 0) {
				/*
				 * Since the timer resolution of
				 * NFS_HZ is so course, it can often
				 * result in r_rtt == 0. Since
				 * r_rtt == N means that the actual
				 * rtt is between N+dt and N+2-dt ticks,
				 * add 1.
				 */
				int n;
				int d;

#define NFSRSB	NFS_RTT_SCALE_BITS
				n = ((NFS_SRTT(rep) * 7) +
				     (rep->r_rtt << NFSRSB)) >> 3;
				d = n - NFS_SRTT(rep);
				NFS_SRTT(rep) = n;

				/*
				 * Don't let the jitter calculation decay
				 * too quickly, but we want a fast rampup.
				 */
				if (d < 0)
					d = -d;
				d <<= NFSRSB;
				if (d < NFS_SDRTT(rep))
					n = ((NFS_SDRTT(rep) * 15) + d) >> 4;
				else
					n = ((NFS_SDRTT(rep) * 3) + d) >> 2;
				NFS_SDRTT(rep) = n;
#undef NFSRSB
			}
			nmp->nm_timeouts = 0;
			rep->r_mrep = info.mrep;
			nfs_hardterm(rep, 0);
		} else {
			/*
			 * Extract vers, prog, nfsver, procnum.  A duplicate
			 * response means we didn't wait long enough so
			 * we increase the SRTT to avoid future spurious
			 * timeouts.
			 */
			u_int procnum = nmp->nm_lastreprocnum;
			int n;

			if (procnum < NFS_NPROCS && proct[procnum]) {
				if (nfs_showrexmit)
					kprintf("D");
				n = nmp->nm_srtt[proct[procnum]];
				n += NFS_ASYSCALE * NFS_HZ;
				if (n < NFS_ASYSCALE * NFS_HZ * 10)
					n = NFS_ASYSCALE * NFS_HZ * 10;
				nmp->nm_srtt[proct[procnum]] = n;
			}
		}
		nfs_rcvunlock(nmp);
		crit_exit();

		/*
		 * If not matched to a request, drop it.
		 * If it's mine, get out.
		 */
		if (rep == NULL) {
			nfsstats.rpcunexpected++;
			m_freem(info.mrep);
			info.mrep = NULL;
		} else if (rep == myrep) {
			if (rep->r_mrep == NULL)
				panic("nfsreply nil");
			return (0);
		}
	}
}

/*
 * Run the request state machine until the target state is reached
 * or a fatal error occurs.  The target state is not run.  Specifying
 * a target of NFSM_STATE_DONE runs the state machine until the rpc
 * is complete.
 *
 * EINPROGRESS is returned for all states other then the DONE state,
 * indicating that the rpc is still in progress.
 */
int
nfs_request(struct nfsm_info *info, nfsm_state_t bstate, nfsm_state_t estate)
{
	struct nfsreq *req;

	while (info->state >= bstate && info->state < estate) {
		switch(info->state) {
		case NFSM_STATE_SETUP:
			/*
			 * Setup the nfsreq.  Any error which occurs during
			 * this state is fatal.
			 */
			info->error = nfs_request_setup(info);
			if (info->error) {
				info->state = NFSM_STATE_DONE;
				return (info->error);
			} else {
				req = info->req;
				req->r_mrp = &info->mrep;
				req->r_mdp = &info->md;
				req->r_dposp = &info->dpos;
				info->state = NFSM_STATE_AUTH;
			}
			break;
		case NFSM_STATE_AUTH:
			/*
			 * Authenticate the nfsreq.  Any error which occurs
			 * during this state is fatal.
			 */
			info->error = nfs_request_auth(info->req);
			if (info->error) {
				info->state = NFSM_STATE_DONE;
				return (info->error);
			} else {
				info->state = NFSM_STATE_TRY;
			}
			break;
		case NFSM_STATE_TRY:
			/*
			 * Transmit or retransmit attempt.  An error in this
			 * state is ignored and we always move on to the
			 * next state.
			 *
			 * This can trivially race the receiver if the
			 * request is asynchronous.  nfs_request_try()
			 * will thus set the state for us and we
			 * must also return immediately if we are
			 * running an async state machine, because
			 * info can become invalid due to races after
			 * try() returns.
			 */
			if (info->req->r_flags & R_ASYNC) {
				nfs_request_try(info->req);
				if (estate == NFSM_STATE_WAITREPLY)
					return (EINPROGRESS);
			} else {
				nfs_request_try(info->req);
				info->state = NFSM_STATE_WAITREPLY;
			}
			break;
		case NFSM_STATE_WAITREPLY:
			/*
			 * Wait for a reply or timeout and move on to the
			 * next state.  The error returned by this state
			 * is passed to the processing code in the next
			 * state.
			 */
			info->error = nfs_request_waitreply(info->req);
			info->state = NFSM_STATE_PROCESSREPLY;
			break;
		case NFSM_STATE_PROCESSREPLY:
			/*
			 * Process the reply or timeout.  Errors which occur
			 * in this state may cause the state machine to
			 * go back to an earlier state, and are fatal
			 * otherwise.
			 */
			info->error = nfs_request_processreply(info,
							       info->error);
			switch(info->error) {
			case ENEEDAUTH:
				info->state = NFSM_STATE_AUTH;
				break;
			case EAGAIN:
				info->state = NFSM_STATE_TRY;
				break;
			default:
				/*
				 * Operation complete, with or without an
				 * error.  We are done.
				 */
				info->req = NULL;
				info->state = NFSM_STATE_DONE;
				return (info->error);
			}
			break;
		case NFSM_STATE_DONE:
			/*
			 * Shouldn't be reached
			 */
			return (info->error);
			/* NOT REACHED */
		}
	}

	/*
	 * If we are done return the error code (if any).
	 * Otherwise return EINPROGRESS.
	 */
	if (info->state == NFSM_STATE_DONE)
		return (info->error);
	return (EINPROGRESS);
}

/*
 * nfs_request - goes something like this
 *	- fill in request struct
 *	- links it into list
 *	- calls nfs_send() for first transmit
 *	- calls nfs_receive() to get reply
 *	- break down rpc header and return with nfs reply pointed to
 *	  by mrep or error
 * nb: always frees up mreq mbuf list
 */
static int
nfs_request_setup(nfsm_info_t info)
{
	struct nfsreq *req;
	struct nfsmount *nmp;
	struct mbuf *m;
	int i;

	/*
	 * Reject requests while attempting a forced unmount.
	 */
	if (info->vp->v_mount->mnt_kern_flag & MNTK_UNMOUNTF) {
		m_freem(info->mreq);
		info->mreq = NULL;
		return (EIO);
	}
	nmp = VFSTONFS(info->vp->v_mount);
	req = kmalloc(sizeof(struct nfsreq), M_NFSREQ, M_WAITOK);
	req->r_nmp = nmp;
	req->r_vp = info->vp;
	req->r_td = info->td;
	req->r_procnum = info->procnum;
	req->r_mreq = NULL;
	req->r_cred = info->cred;

	i = 0;
	m = info->mreq;
	while (m) {
		i += m->m_len;
		m = m->m_next;
	}
	req->r_mrest = info->mreq;
	req->r_mrest_len = i;

	/*
	 * The presence of a non-NULL r_info in req indicates
	 * async completion via our helper threads.  See the receiver
	 * code.
	 */
	if (info->bio) {
		req->r_info = info;
		req->r_flags = R_ASYNC;
	} else {
		req->r_info = NULL;
		req->r_flags = 0;
	}
	info->req = req;
	return(0);
}

static int
nfs_request_auth(struct nfsreq *rep)
{
	struct nfsmount *nmp = rep->r_nmp;
	struct mbuf *m;
	char nickv[RPCX_NICKVERF];
	int error = 0, auth_len, auth_type;
	int verf_len;
	u_int32_t xid;
	char *auth_str, *verf_str;
	struct ucred *cred;

	cred = rep->r_cred;
	rep->r_failed_auth = 0;

	/*
	 * Get the RPC header with authorization.
	 */
	verf_str = auth_str = NULL;
	if (nmp->nm_flag & NFSMNT_KERB) {
		verf_str = nickv;
		verf_len = sizeof (nickv);
		auth_type = RPCAUTH_KERB4;
		bzero((caddr_t)rep->r_key, sizeof(rep->r_key));
		if (rep->r_failed_auth ||
		    nfs_getnickauth(nmp, cred, &auth_str, &auth_len,
				    verf_str, verf_len)) {
			error = nfs_getauth(nmp, rep, cred, &auth_str,
				&auth_len, verf_str, &verf_len, rep->r_key);
			if (error) {
				m_freem(rep->r_mrest);
				rep->r_mrest = NULL;
				kfree((caddr_t)rep, M_NFSREQ);
				return (error);
			}
		}
	} else {
		auth_type = RPCAUTH_UNIX;
		if (cred->cr_ngroups < 1)
			panic("nfsreq nogrps");
		auth_len = ((((cred->cr_ngroups - 1) > nmp->nm_numgrps) ?
			nmp->nm_numgrps : (cred->cr_ngroups - 1)) << 2) +
			5 * NFSX_UNSIGNED;
	}
	if (rep->r_mrest)
		nfs_checkpkt(rep->r_mrest, rep->r_mrest_len);
	m = nfsm_rpchead(cred, nmp->nm_flag, rep->r_procnum, auth_type,
			auth_len, auth_str, verf_len, verf_str,
			rep->r_mrest, rep->r_mrest_len, &rep->r_mheadend, &xid);
	rep->r_mrest = NULL;
	if (auth_str)
		kfree(auth_str, M_TEMP);

	/*
	 * For stream protocols, insert a Sun RPC Record Mark.
	 */
	if (nmp->nm_sotype == SOCK_STREAM) {
		M_PREPEND(m, NFSX_UNSIGNED, MB_WAIT);
		if (m == NULL) {
			kfree(rep, M_NFSREQ);
			return (ENOBUFS);
		}
		*mtod(m, u_int32_t *) = htonl(0x80000000 |
			 (m->m_pkthdr.len - NFSX_UNSIGNED));
	}

	nfs_checkpkt(m, m->m_pkthdr.len);

	rep->r_mreq = m;
	rep->r_xid = xid;
	return (0);
}

static int
nfs_request_try(struct nfsreq *rep)
{
	struct nfsmount *nmp = rep->r_nmp;
	struct mbuf *m2;
	int error;

	/*
	 * Request is not on any queue, only the owner has access to it
	 * so it should not be locked by anyone atm.
	 *
	 * Interlock to prevent races.  While locked the only remote
	 * action possible is for r_mrep to be set (once we enqueue it).
	 */
	if (rep->r_flags == 0xdeadc0de) {
		print_backtrace(-1);
		panic("flags nbad");
	}
	KKASSERT((rep->r_flags & (R_LOCKED | R_ONREQQ)) == 0);
	if (nmp->nm_flag & NFSMNT_SOFT)
		rep->r_retry = nmp->nm_retry;
	else
		rep->r_retry = NFS_MAXREXMIT + 1;	/* past clip limit */
	rep->r_rtt = rep->r_rexmit = 0;
	if (proct[rep->r_procnum] > 0)
		rep->r_flags |= R_TIMING | R_LOCKED;
	else
		rep->r_flags |= R_LOCKED;
	rep->r_mrep = NULL;

	nfsstats.rpcrequests++;

	if (nmp->nm_flag & NFSMNT_FORCE) {
		rep->r_flags |= R_SOFTTERM;
		rep->r_flags &= ~R_LOCKED;
		if (rep->r_info)
			rep->r_info->error = EINTR;
		return (0);
	}
	rep->r_flags |= R_NEEDSXMIT;	/* in case send lock races us */

	/*
	 * Do the client side RPC.
	 *
	 * Chain request into list of outstanding requests. Be sure
	 * to put it LAST so timer finds oldest requests first.  Note
	 * that our control of R_LOCKED prevents the request from
	 * getting ripped out from under us or transmitted by the
	 * timer code.
	 *
	 * For requests with info structures we must atomically set the
	 * info's state because the structure could become invalid upon
	 * return due to races (i.e., if async)
	 */
	crit_enter();
	mtx_link_init(&rep->r_link);
	KKASSERT((rep->r_flags & R_ONREQQ) == 0);
	TAILQ_INSERT_TAIL(&nmp->nm_reqq, rep, r_chain);
	rep->r_flags |= R_ONREQQ;
	++nmp->nm_reqqlen;
	if (rep->r_flags & R_ASYNC)
		rep->r_info->state = NFSM_STATE_WAITREPLY;
	crit_exit();

	error = 0;

	/*
	 * Send if we can.  Congestion control is not handled here any more
	 * becausing trying to defer the initial send based on the nfs_timer
	 * requires having a very fast nfs_timer, which is silly.
	 */
	if (nmp->nm_so) {
		if (nmp->nm_soflags & PR_CONNREQUIRED)
			error = nfs_sndlock(nmp, rep);
		if (error == 0 && (rep->r_flags & R_NEEDSXMIT)) {
			m2 = m_copym(rep->r_mreq, 0, M_COPYALL, MB_WAIT);
			error = nfs_send(nmp->nm_so, nmp->nm_nam, m2, rep);
			rep->r_flags &= ~R_NEEDSXMIT;
			if ((rep->r_flags & R_SENT) == 0) {
				rep->r_flags |= R_SENT;
			}
			if (nmp->nm_soflags & PR_CONNREQUIRED)
				nfs_sndunlock(nmp);
		}
	} else {
		rep->r_rtt = -1;
	}
	if (error == EPIPE)
		error = 0;

	/*
	 * Release the lock.  The only remote action that may have occurred
	 * would have been the setting of rep->r_mrep.  If this occured
	 * and the request was async we have to move it to the reader
	 * thread's queue for action.
	 *
	 * For async requests also make sure the reader is woken up so
	 * it gets on the socket to read responses.
	 */
	crit_enter();
	if (rep->r_flags & R_ASYNC) {
		if (rep->r_mrep)
			nfs_hardterm(rep, 1);
		rep->r_flags &= ~R_LOCKED;
		nfssvc_iod_reader_wakeup(nmp);
	} else {
		rep->r_flags &= ~R_LOCKED;
	}
	if (rep->r_flags & R_WANTED) {
		rep->r_flags &= ~R_WANTED;
		wakeup(rep);
	}
	crit_exit();
	return (error);
}

/*
 * This code is only called for synchronous requests.  Completed synchronous
 * requests are left on reqq and we remove them before moving on to the
 * processing state.
 */
static int
nfs_request_waitreply(struct nfsreq *rep)
{
	struct nfsmount *nmp = rep->r_nmp;
	int error;

	KKASSERT((rep->r_flags & R_ASYNC) == 0);

	/*
	 * Wait until the request is finished.
	 */
	error = nfs_reply(nmp, rep);

	/*
	 * RPC done, unlink the request, but don't rip it out from under
	 * the callout timer.
	 *
	 * Once unlinked no other receiver or the timer will have
	 * visibility, so we do not have to set R_LOCKED.
	 */
	crit_enter();
	while (rep->r_flags & R_LOCKED) {
		rep->r_flags |= R_WANTED;
		tsleep(rep, 0, "nfstrac", 0);
	}
	KKASSERT(rep->r_flags & R_ONREQQ);
	TAILQ_REMOVE(&nmp->nm_reqq, rep, r_chain);
	rep->r_flags &= ~R_ONREQQ;
	--nmp->nm_reqqlen;
	if (TAILQ_FIRST(&nmp->nm_bioq) &&
	    nmp->nm_reqqlen <= nfs_maxasyncbio * 2 / 3) {
		nfssvc_iod_writer_wakeup(nmp);
	}
	crit_exit();

	/*
	 * Decrement the outstanding request count.
	 */
	if (rep->r_flags & R_SENT) {
		rep->r_flags &= ~R_SENT;
	}
	return (error);
}

/*
 * Process reply with error returned from nfs_requet_waitreply().
 *
 * Returns EAGAIN if it wants us to loop up to nfs_request_try() again.
 * Returns ENEEDAUTH if it wants us to loop up to nfs_request_auth() again.
 */
static int
nfs_request_processreply(nfsm_info_t info, int error)
{
	struct nfsreq *req = info->req;
	struct nfsmount *nmp = req->r_nmp;
	u_int32_t *tl;
	int verf_type;
	int i;

	/*
	 * If there was a successful reply and a tprintf msg.
	 * tprintf a response.
	 */
	if (error == 0 && (req->r_flags & R_TPRINTFMSG)) {
		nfs_msg(req->r_td,
			nmp->nm_mountp->mnt_stat.f_mntfromname,
			"is alive again");
	}

	/*
	 * Assign response and handle any pre-process error.  Response
	 * fields can be NULL if an error is already pending.
	 */
	info->mrep = req->r_mrep;
	info->md = req->r_md;
	info->dpos = req->r_dpos;

	if (error) {
		m_freem(req->r_mreq);
		req->r_mreq = NULL;
		kfree(req, M_NFSREQ);
		info->req = NULL;
		return (error);
	}

	/*
	 * break down the rpc header and check if ok
	 */
	NULLOUT(tl = nfsm_dissect(info, 3 * NFSX_UNSIGNED));
	if (*tl++ == rpc_msgdenied) {
		if (*tl == rpc_mismatch) {
			error = EOPNOTSUPP;
		} else if ((nmp->nm_flag & NFSMNT_KERB) &&
			   *tl++ == rpc_autherr) {
			if (req->r_failed_auth == 0) {
				req->r_failed_auth++;
				req->r_mheadend->m_next = NULL;
				m_freem(info->mrep);
				info->mrep = NULL;
				m_freem(req->r_mreq);
				req->r_mreq = NULL;
				return (ENEEDAUTH);
			} else {
				error = EAUTH;
			}
		} else {
			error = EACCES;
		}
		m_freem(info->mrep);
		info->mrep = NULL;
		m_freem(req->r_mreq);
		req->r_mreq = NULL;
		kfree(req, M_NFSREQ);
		info->req = NULL;
		return (error);
	}

	/*
	 * Grab any Kerberos verifier, otherwise just throw it away.
	 */
	verf_type = fxdr_unsigned(int, *tl++);
	i = fxdr_unsigned(int32_t, *tl);
	if ((nmp->nm_flag & NFSMNT_KERB) && verf_type == RPCAUTH_KERB4) {
		error = nfs_savenickauth(nmp, req->r_cred, i, req->r_key,
					 &info->md, &info->dpos, info->mrep);
		if (error)
			goto nfsmout;
	} else if (i > 0) {
		ERROROUT(nfsm_adv(info, nfsm_rndup(i)));
	}
	NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
	/* 0 == ok */
	if (*tl == 0) {
		NULLOUT(tl = nfsm_dissect(info, NFSX_UNSIGNED));
		if (*tl != 0) {
			error = fxdr_unsigned(int, *tl);

			/*
			 * Does anyone even implement this?  Just impose
			 * a 1-second delay.
			 */
			if ((nmp->nm_flag & NFSMNT_NFSV3) &&
				error == NFSERR_TRYLATER) {
				m_freem(info->mrep);
				info->mrep = NULL;
				error = 0;

				tsleep((caddr_t)&lbolt, 0, "nqnfstry", 0);
				return (EAGAIN);	/* goto tryagain */
			}

#if 0
			/*
			 * XXX We can't do this here any more because the
			 *     caller may be holding a shared lock on the
			 *     namecache entry.
			 *
			 * If the File Handle was stale, invalidate the
			 * lookup cache, just in case.
			 *
			 * To avoid namecache<->vnode deadlocks we must
			 * release the vnode lock if we hold it.
			 */
			if (error == ESTALE) {
				struct vnode *vp = req->r_vp;
				int ltype;

				ltype = lockstatus(&vp->v_lock, curthread);
				if (ltype == LK_EXCLUSIVE || ltype == LK_SHARED)
					lockmgr(&vp->v_lock, LK_RELEASE);
				cache_inval_vp(vp, CINV_CHILDREN);
				if (ltype == LK_EXCLUSIVE || ltype == LK_SHARED)
					lockmgr(&vp->v_lock, ltype);
			}
#endif
			if (nmp->nm_flag & NFSMNT_NFSV3) {
				KKASSERT(*req->r_mrp == info->mrep);
				KKASSERT(*req->r_mdp == info->md);
				KKASSERT(*req->r_dposp == info->dpos);
				error |= NFSERR_RETERR;
			} else {
				m_freem(info->mrep);
				info->mrep = NULL;
			}
			m_freem(req->r_mreq);
			req->r_mreq = NULL;
			kfree(req, M_NFSREQ);
			info->req = NULL;
			return (error);
		}

		KKASSERT(*req->r_mrp == info->mrep);
		KKASSERT(*req->r_mdp == info->md);
		KKASSERT(*req->r_dposp == info->dpos);
		m_freem(req->r_mreq);
		req->r_mreq = NULL;
		kfree(req, M_NFSREQ);
		return (0);
	}
	m_freem(info->mrep);
	info->mrep = NULL;
	error = EPROTONOSUPPORT;
nfsmout:
	m_freem(req->r_mreq);
	req->r_mreq = NULL;
	kfree(req, M_NFSREQ);
	info->req = NULL;
	return (error);
}

#ifndef NFS_NOSERVER
/*
 * Generate the rpc reply header
 * siz arg. is used to decide if adding a cluster is worthwhile
 */
int
nfs_rephead(int siz, struct nfsrv_descript *nd, struct nfssvc_sock *slp,
	    int err, struct mbuf **mrq, struct mbuf **mbp, caddr_t *bposp)
{
	u_int32_t *tl;
	struct nfsm_info info;

	siz += RPC_REPLYSIZ;
	info.mb = m_getl(max_hdr + siz, MB_WAIT, MT_DATA, M_PKTHDR, NULL);
	info.mreq = info.mb;
	info.mreq->m_pkthdr.len = 0;
	/*
	 * If this is not a cluster, try and leave leading space
	 * for the lower level headers.
	 */
	if ((max_hdr + siz) < MINCLSIZE)
		info.mreq->m_data += max_hdr;
	tl = mtod(info.mreq, u_int32_t *);
	info.mreq->m_len = 6 * NFSX_UNSIGNED;
	info.bpos = ((caddr_t)tl) + info.mreq->m_len;
	*tl++ = txdr_unsigned(nd->nd_retxid);
	*tl++ = rpc_reply;
	if (err == ERPCMISMATCH || (err & NFSERR_AUTHERR)) {
		*tl++ = rpc_msgdenied;
		if (err & NFSERR_AUTHERR) {
			*tl++ = rpc_autherr;
			*tl = txdr_unsigned(err & ~NFSERR_AUTHERR);
			info.mreq->m_len -= NFSX_UNSIGNED;
			info.bpos -= NFSX_UNSIGNED;
		} else {
			*tl++ = rpc_mismatch;
			*tl++ = txdr_unsigned(RPC_VER2);
			*tl = txdr_unsigned(RPC_VER2);
		}
	} else {
		*tl++ = rpc_msgaccepted;

		/*
		 * For Kerberos authentication, we must send the nickname
		 * verifier back, otherwise just RPCAUTH_NULL.
		 */
		if (nd->nd_flag & ND_KERBFULL) {
		    struct nfsuid *nuidp;
		    struct timeval ktvout;

		    for (nuidp = NUIDHASH(slp, nd->nd_cr.cr_uid)->lh_first;
			nuidp != NULL; nuidp = nuidp->nu_hash.le_next) {
			if (nuidp->nu_cr.cr_uid == nd->nd_cr.cr_uid &&
			    (!nd->nd_nam2 || netaddr_match(NU_NETFAM(nuidp),
			     &nuidp->nu_haddr, nd->nd_nam2)))
			    break;
		    }
		    if (nuidp) {
			/*
			 * Encrypt the timestamp in ecb mode using the
			 * session key.
			 */
#ifdef NFSKERB
			XXX
#else
			ktvout.tv_sec = 0;
			ktvout.tv_usec = 0;
#endif

			*tl++ = rpc_auth_kerb;
			*tl++ = txdr_unsigned(3 * NFSX_UNSIGNED);
			*tl = ktvout.tv_sec;
			tl = nfsm_build(&info, 3 * NFSX_UNSIGNED);
			*tl++ = ktvout.tv_usec;
			*tl++ = txdr_unsigned(nuidp->nu_cr.cr_uid);
		    } else {
			*tl++ = 0;
			*tl++ = 0;
		    }
		} else {
			*tl++ = 0;
			*tl++ = 0;
		}
		switch (err) {
		case EPROGUNAVAIL:
			*tl = txdr_unsigned(RPC_PROGUNAVAIL);
			break;
		case EPROGMISMATCH:
			*tl = txdr_unsigned(RPC_PROGMISMATCH);
			tl = nfsm_build(&info, 2 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(2);
			*tl = txdr_unsigned(3);
			break;
		case EPROCUNAVAIL:
			*tl = txdr_unsigned(RPC_PROCUNAVAIL);
			break;
		case EBADRPC:
			*tl = txdr_unsigned(RPC_GARBAGE);
			break;
		default:
			*tl = 0;
			if (err != NFSERR_RETVOID) {
				tl = nfsm_build(&info, NFSX_UNSIGNED);
				if (err)
				    *tl = txdr_unsigned(nfsrv_errmap(nd, err));
				else
				    *tl = 0;
			}
			break;
		}
	}

	if (mrq != NULL)
	    *mrq = info.mreq;
	*mbp = info.mb;
	*bposp = info.bpos;
	if (err != 0 && err != NFSERR_RETVOID)
		nfsstats.srvrpc_errs++;
	return (0);
}


#endif /* NFS_NOSERVER */

/*
 * Nfs timer routine.
 *
 * Scan the nfsreq list and retranmit any requests that have timed out
 * To avoid retransmission attempts on STREAM sockets (in the future) make
 * sure to set the r_retry field to 0 (implies nm_retry == 0).
 *
 * Requests with attached responses, terminated requests, and
 * locked requests are ignored.  Locked requests will be picked up
 * in a later timer call.
 */
void
nfs_timer_callout(void *arg /* never used */)
{
	struct nfsmount *nmp;
	struct nfsreq *req;
#ifndef NFS_NOSERVER
	struct nfssvc_sock *slp;
	u_quad_t cur_usec;
#endif /* NFS_NOSERVER */

	lwkt_gettoken(&nfs_token);
	TAILQ_FOREACH(nmp, &nfs_mountq, nm_entry) {
		lwkt_gettoken(&nmp->nm_token);
		TAILQ_FOREACH(req, &nmp->nm_reqq, r_chain) {
			KKASSERT(nmp == req->r_nmp);
			if (req->r_mrep)
				continue;
			if (req->r_flags & (R_SOFTTERM | R_LOCKED))
				continue;

			/*
			 * Handle timeout/retry.  Be sure to process r_mrep
			 * for async requests that completed while we had
			 * the request locked or they will hang in the reqq
			 * forever.
			 */
			req->r_flags |= R_LOCKED;
			if (nfs_sigintr(nmp, req, req->r_td)) {
				nfs_softterm(req, 1);
				req->r_flags &= ~R_LOCKED;
			} else {
				nfs_timer_req(req);
				if (req->r_flags & R_ASYNC) {
					if (req->r_mrep)
						nfs_hardterm(req, 1);
					req->r_flags &= ~R_LOCKED;
					nfssvc_iod_reader_wakeup(nmp);
				} else {
					req->r_flags &= ~R_LOCKED;
				}
			}
			if (req->r_flags & R_WANTED) {
				req->r_flags &= ~R_WANTED;
				wakeup(req);
			}
		}
		lwkt_reltoken(&nmp->nm_token);
	}
#ifndef NFS_NOSERVER

	/*
	 * Scan the write gathering queues for writes that need to be
	 * completed now.
	 */
	cur_usec = nfs_curusec();

	TAILQ_FOREACH(slp, &nfssvc_sockhead, ns_chain) {
		/* XXX race against removal */
		if (lwkt_trytoken(&slp->ns_token)) {
			if (slp->ns_tq.lh_first &&
			    (slp->ns_tq.lh_first->nd_time <= cur_usec)) {
				nfsrv_wakenfsd(slp, 1);
			}
			lwkt_reltoken(&slp->ns_token);
		}
	}
#endif /* NFS_NOSERVER */

	callout_reset(&nfs_timer_handle, nfs_ticks, nfs_timer_callout, NULL);
	lwkt_reltoken(&nfs_token);
}

static
void
nfs_timer_req(struct nfsreq *req)
{
	struct thread *td = &thread0; /* XXX for creds, will break if sleep */
	struct nfsmount *nmp = req->r_nmp;
	struct mbuf *m;
	struct socket *so;
	int timeo;
	int error;

	/*
	 * rtt ticks and timeout calculation.  Return if the timeout
	 * has not been reached yet, unless the packet is flagged
	 * for an immediate send.
	 *
	 * The mean rtt doesn't help when we get random I/Os, we have
	 * to multiply by fairly large numbers.
	 */
	if (req->r_rtt >= 0) {
		/*
		 * Calculate the timeout to test against.
		 */
		req->r_rtt++;
		if (nmp->nm_flag & NFSMNT_DUMBTIMR) {
			timeo = nmp->nm_timeo << NFS_RTT_SCALE_BITS;
		} else if (req->r_flags & R_TIMING) {
			timeo = NFS_SRTT(req) + NFS_SDRTT(req);
		} else {
			timeo = nmp->nm_timeo << NFS_RTT_SCALE_BITS;
		}
		timeo *= multt[req->r_procnum];
		/* timeo is still scaled by SCALE_BITS */

#define NFSFS	(NFS_RTT_SCALE * NFS_HZ)
		if (req->r_flags & R_TIMING) {
			static long last_time;
			if (nfs_showrtt && last_time != time_uptime) {
				kprintf("rpccmd %d NFS SRTT %d SDRTT %d "
					"timeo %d.%03d\n",
					proct[req->r_procnum],
					NFS_SRTT(req), NFS_SDRTT(req),
					timeo / NFSFS,
					timeo % NFSFS * 1000 /  NFSFS);
				last_time = time_uptime;
			}
		}
#undef NFSFS

		/*
		 * deal with nfs_timer jitter.
		 */
		timeo = (timeo >> NFS_RTT_SCALE_BITS) + 1;
		if (timeo < 2)
			timeo = 2;

		if (nmp->nm_timeouts > 0)
			timeo *= nfs_backoff[nmp->nm_timeouts - 1];
		if (timeo > NFS_MAXTIMEO)
			timeo = NFS_MAXTIMEO;
		if (req->r_rtt <= timeo) {
			if ((req->r_flags & R_NEEDSXMIT) == 0)
				return;
		} else if (nmp->nm_timeouts < 8) {
			nmp->nm_timeouts++;
		}
	}

	/*
	 * Check for server not responding
	 */
	if ((req->r_flags & R_TPRINTFMSG) == 0 &&
	     req->r_rexmit > nmp->nm_deadthresh) {
		nfs_msg(req->r_td, nmp->nm_mountp->mnt_stat.f_mntfromname,
			"not responding");
		req->r_flags |= R_TPRINTFMSG;
	}
	if (req->r_rexmit >= req->r_retry) {	/* too many */
		nfsstats.rpctimeouts++;
		nfs_softterm(req, 1);
		return;
	}

	/*
	 * Generally disable retransmission on reliable sockets,
	 * unless the request is flagged for immediate send.
	 */
	if (nmp->nm_sotype != SOCK_DGRAM) {
		if (++req->r_rexmit > NFS_MAXREXMIT)
			req->r_rexmit = NFS_MAXREXMIT;
		if ((req->r_flags & R_NEEDSXMIT) == 0)
			return;
	}

	/*
	 * Stop here if we do not have a socket!
	 */
	if ((so = nmp->nm_so) == NULL)
		return;

	/*
	 * If there is enough space and the window allows.. resend it.
	 *
	 * r_rtt is left intact in case we get an answer after the
	 * retry that was a reply to the original packet.
	 *
	 * NOTE: so_pru_send()
	 */
	if (ssb_space(&so->so_snd) >= req->r_mreq->m_pkthdr.len &&
	    (req->r_flags & (R_SENT | R_NEEDSXMIT)) &&
	   (m = m_copym(req->r_mreq, 0, M_COPYALL, MB_DONTWAIT))){
		if ((nmp->nm_flag & NFSMNT_NOCONN) == 0)
		    error = so_pru_send(so, 0, m, NULL, NULL, td);
		else
		    error = so_pru_send(so, 0, m, nmp->nm_nam, NULL, td);
		if (error) {
			if (NFSIGNORE_SOERROR(nmp->nm_soflags, error))
				so->so_error = 0;
			req->r_flags |= R_NEEDSXMIT;
		} else if (req->r_mrep == NULL) {
			/*
			 * Iff first send, start timing
			 * else turn timing off, backoff timer
			 * and divide congestion window by 2.
			 *
			 * It is possible for the so_pru_send() to
			 * block and for us to race a reply so we
			 * only do this if the reply field has not
			 * been filled in.  R_LOCKED will prevent
			 * the request from being ripped out from under
			 * us entirely.
			 *
			 * Record the last resent procnum to aid us
			 * in duplicate detection on receive.
			 */
			if ((req->r_flags & R_NEEDSXMIT) == 0) {
				if (nfs_showrexmit)
					kprintf("X");
				if (++req->r_rexmit > NFS_MAXREXMIT)
					req->r_rexmit = NFS_MAXREXMIT;
				nmp->nm_maxasync_scaled >>= 1;
				if (nmp->nm_maxasync_scaled < NFS_MINASYNC_SCALED)
					nmp->nm_maxasync_scaled = NFS_MINASYNC_SCALED;
				nfsstats.rpcretries++;
				nmp->nm_lastreprocnum = req->r_procnum;
			} else {
				req->r_flags |= R_SENT;
				req->r_flags &= ~R_NEEDSXMIT;
			}
		}
	}
}

/*
 * Mark all of an nfs mount's outstanding requests with R_SOFTTERM and
 * wait for all requests to complete. This is used by forced unmounts
 * to terminate any outstanding RPCs.
 *
 * Locked requests cannot be canceled but will be marked for
 * soft-termination.
 */
int
nfs_nmcancelreqs(struct nfsmount *nmp)
{
	struct nfsreq *req;
	int i;

	crit_enter();
	TAILQ_FOREACH(req, &nmp->nm_reqq, r_chain) {
		if (req->r_mrep != NULL || (req->r_flags & R_SOFTTERM))
			continue;
		nfs_softterm(req, 0);
	}
	/* XXX  the other two queues as well */
	crit_exit();

	for (i = 0; i < 30; i++) {
		crit_enter();
		TAILQ_FOREACH(req, &nmp->nm_reqq, r_chain) {
			if (nmp == req->r_nmp)
				break;
		}
		crit_exit();
		if (req == NULL)
			return (0);
		tsleep(&lbolt, 0, "nfscancel", 0);
	}
	return (EBUSY);
}

/*
 * Soft-terminate a request, effectively marking it as failed.
 *
 * Must be called from within a critical section.
 */
static void
nfs_softterm(struct nfsreq *rep, int islocked)
{
	rep->r_flags |= R_SOFTTERM;
	if (rep->r_info)
		rep->r_info->error = EINTR;
	nfs_hardterm(rep, islocked);
}

/*
 * Hard-terminate a request, typically after getting a response.
 *
 * The state machine can still decide to re-issue it later if necessary.
 *
 * Must be called from within a critical section.
 */
static void
nfs_hardterm(struct nfsreq *rep, int islocked)
{
	struct nfsmount *nmp = rep->r_nmp;

	/*
	 * The nm_send count is decremented now to avoid deadlocks
	 * when the process in soreceive() hasn't yet managed to send
	 * its own request.
	 */
	if (rep->r_flags & R_SENT) {
		rep->r_flags &= ~R_SENT;
	}

	/*
	 * If we locked the request or nobody else has locked the request,
	 * and the request is async, we can move it to the reader thread's
	 * queue now and fix up the state.
	 *
	 * If we locked the request or nobody else has locked the request,
	 * we can wake up anyone blocked waiting for a response on the
	 * request.
	 */
	if (islocked || (rep->r_flags & R_LOCKED) == 0) {
		if ((rep->r_flags & (R_ONREQQ | R_ASYNC)) ==
		    (R_ONREQQ | R_ASYNC)) {
			rep->r_flags &= ~R_ONREQQ;
			TAILQ_REMOVE(&nmp->nm_reqq, rep, r_chain);
			--nmp->nm_reqqlen;
			TAILQ_INSERT_TAIL(&nmp->nm_reqrxq, rep, r_chain);
			KKASSERT(rep->r_info->state == NFSM_STATE_TRY ||
				 rep->r_info->state == NFSM_STATE_WAITREPLY);

			/*
			 * When setting the state to PROCESSREPLY we must
			 * roll-up any error not related to the contents of
			 * the reply (i.e. if there is no contents).
			 */
			rep->r_info->state = NFSM_STATE_PROCESSREPLY;
			nfssvc_iod_reader_wakeup(nmp);
			if (TAILQ_FIRST(&nmp->nm_bioq) &&
			    nmp->nm_reqqlen <= nfs_maxasyncbio * 2 / 3) {
				nfssvc_iod_writer_wakeup(nmp);
			}
		}
		mtx_abort_ex_link(&nmp->nm_rxlock, &rep->r_link);
	}
}

/*
 * Test for a termination condition pending on the process.
 * This is used for NFSMNT_INT mounts.
 */
int
nfs_sigintr(struct nfsmount *nmp, struct nfsreq *rep, struct thread *td)
{
	sigset_t tmpset;
	struct proc *p;
	struct lwp *lp;

	if (rep && (rep->r_flags & R_SOFTTERM))
		return (EINTR);
	/* Terminate all requests while attempting a forced unmount. */
	if (nmp->nm_mountp->mnt_kern_flag & MNTK_UNMOUNTF)
		return (EINTR);
	if (!(nmp->nm_flag & NFSMNT_INT))
		return (0);
	/* td might be NULL YYY */
	if (td == NULL || (p = td->td_proc) == NULL)
		return (0);

	lp = td->td_lwp;
	tmpset = lwp_sigpend(lp);
	SIGSETNAND(tmpset, lp->lwp_sigmask);
	SIGSETNAND(tmpset, p->p_sigignore);
	if (SIGNOTEMPTY(tmpset) && NFSINT_SIGMASK(tmpset))
		return (EINTR);

	return (0);
}

/*
 * Lock a socket against others.
 * Necessary for STREAM sockets to ensure you get an entire rpc request/reply
 * and also to avoid race conditions between the processes with nfs requests
 * in progress when a reconnect is necessary.
 */
int
nfs_sndlock(struct nfsmount *nmp, struct nfsreq *rep)
{
	mtx_t mtx = &nmp->nm_txlock;
	struct thread *td;
	int slptimeo;
	int slpflag;
	int error;

	slpflag = 0;
	slptimeo = 0;
	td = rep ? rep->r_td : NULL;
	if (nmp->nm_flag & NFSMNT_INT)
		slpflag = PCATCH;

	while ((error = mtx_lock_ex_try(mtx)) != 0) {
		if (nfs_sigintr(nmp, rep, td)) {
			error = EINTR;
			break;
		}
		error = mtx_lock_ex(mtx, "nfsndlck", slpflag, slptimeo);
		if (error == 0)
			break;
		if (slpflag == PCATCH) {
			slpflag = 0;
			slptimeo = 2 * hz;
		}
	}
	/* Always fail if our request has been cancelled. */
	if (rep && (rep->r_flags & R_SOFTTERM)) {
		if (error == 0)
			mtx_unlock(mtx);
		error = EINTR;
	}
	return (error);
}

/*
 * Unlock the stream socket for others.
 */
void
nfs_sndunlock(struct nfsmount *nmp)
{
	mtx_unlock(&nmp->nm_txlock);
}

/*
 * Lock the receiver side of the socket.
 *
 * rep may be NULL.
 */
static int
nfs_rcvlock(struct nfsmount *nmp, struct nfsreq *rep)
{
	mtx_t mtx = &nmp->nm_rxlock;
	int slpflag;
	int slptimeo;
	int error;

	/*
	 * Unconditionally check for completion in case another nfsiod
	 * get the packet while the caller was blocked, before the caller
	 * called us.  Packet reception is handled by mainline code which
	 * is protected by the BGL at the moment.
	 *
	 * We do not strictly need the second check just before the
	 * tsleep(), but it's good defensive programming.
	 */
	if (rep && rep->r_mrep != NULL)
		return (EALREADY);

	if (nmp->nm_flag & NFSMNT_INT)
		slpflag = PCATCH;
	else
		slpflag = 0;
	slptimeo = 0;

	while ((error = mtx_lock_ex_try(mtx)) != 0) {
		if (nfs_sigintr(nmp, rep, (rep ? rep->r_td : NULL))) {
			error = EINTR;
			break;
		}
		if (rep && rep->r_mrep != NULL) {
			error = EALREADY;
			break;
		}

		/*
		 * NOTE: can return ENOLCK, but in that case rep->r_mrep
		 *       will already be set.
		 */
		if (rep) {
			error = mtx_lock_ex_link(mtx, &rep->r_link,
						 "nfsrcvlk",
						 slpflag, slptimeo);
		} else {
			error = mtx_lock_ex(mtx, "nfsrcvlk", slpflag, slptimeo);
		}
		if (error == 0)
			break;

		/*
		 * If our reply was recieved while we were sleeping,
		 * then just return without taking the lock to avoid a
		 * situation where a single iod could 'capture' the
		 * recieve lock.
		 */
		if (rep && rep->r_mrep != NULL) {
			error = EALREADY;
			break;
		}
		if (slpflag == PCATCH) {
			slpflag = 0;
			slptimeo = 2 * hz;
		}
	}
	if (error == 0) {
		if (rep && rep->r_mrep != NULL) {
			error = EALREADY;
			mtx_unlock(mtx);
		}
	}
	return (error);
}

/*
 * Unlock the stream socket for others.
 */
static void
nfs_rcvunlock(struct nfsmount *nmp)
{
	mtx_unlock(&nmp->nm_rxlock);
}

/*
 * nfs_realign:
 *
 * Check for badly aligned mbuf data and realign by copying the unaligned
 * portion of the data into a new mbuf chain and freeing the portions
 * of the old chain that were replaced.
 *
 * We cannot simply realign the data within the existing mbuf chain
 * because the underlying buffers may contain other rpc commands and
 * we cannot afford to overwrite them.
 *
 * We would prefer to avoid this situation entirely.  The situation does
 * not occur with NFS/UDP and is supposed to only occassionally occur
 * with TCP.  Use vfs.nfs.realign_count and realign_test to check this.
 *
 * NOTE!  MB_DONTWAIT cannot be used here.  The mbufs must be acquired
 *	  because the rpc request OR reply cannot be thrown away.  TCP NFS
 *	  mounts do not retry their RPCs unless the TCP connection itself
 *	  is dropped so throwing away a RPC will basically cause the NFS
 *	  operation to lockup indefinitely.
 */
static void
nfs_realign(struct mbuf **pm, int hsiz)
{
	struct mbuf *m;
	struct mbuf *n = NULL;

	/*
	 * Check for misalignemnt
	 */
	++nfs_realign_test;
	while ((m = *pm) != NULL) {
		if ((m->m_len & 0x3) || (mtod(m, intptr_t) & 0x3))
			break;
		pm = &m->m_next;
	}

	/*
	 * If misalignment found make a completely new copy.
	 */
	if (m) {
		++nfs_realign_count;
		n = m_dup_data(m, MB_WAIT);
		m_freem(*pm);
		*pm = n;
	}
}

#ifndef NFS_NOSERVER

/*
 * Parse an RPC request
 * - verify it
 * - fill in the cred struct.
 */
int
nfs_getreq(struct nfsrv_descript *nd, struct nfsd *nfsd, int has_header)
{
	int len, i;
	u_int32_t *tl;
	struct uio uio;
	struct iovec iov;
	caddr_t cp;
	u_int32_t nfsvers, auth_type;
	uid_t nickuid;
	int error = 0, ticklen;
	struct nfsuid *nuidp;
	struct timeval tvin, tvout;
	struct nfsm_info info;
#if 0				/* until encrypted keys are implemented */
	NFSKERBKEYSCHED_T keys;	/* stores key schedule */
#endif

	info.mrep = nd->nd_mrep;
	info.md = nd->nd_md;
	info.dpos = nd->nd_dpos;

	if (has_header) {
		NULLOUT(tl = nfsm_dissect(&info, 10 * NFSX_UNSIGNED));
		nd->nd_retxid = fxdr_unsigned(u_int32_t, *tl++);
		if (*tl++ != rpc_call) {
			m_freem(info.mrep);
			return (EBADRPC);
		}
	} else {
		NULLOUT(tl = nfsm_dissect(&info, 8 * NFSX_UNSIGNED));
	}
	nd->nd_repstat = 0;
	nd->nd_flag = 0;
	if (*tl++ != rpc_vers) {
		nd->nd_repstat = ERPCMISMATCH;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	if (*tl != nfs_prog) {
		nd->nd_repstat = EPROGUNAVAIL;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	tl++;
	nfsvers = fxdr_unsigned(u_int32_t, *tl++);
	if (nfsvers < NFS_VER2 || nfsvers > NFS_VER3) {
		nd->nd_repstat = EPROGMISMATCH;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	if (nfsvers == NFS_VER3)
		nd->nd_flag = ND_NFSV3;
	nd->nd_procnum = fxdr_unsigned(u_int32_t, *tl++);
	if (nd->nd_procnum == NFSPROC_NULL)
		return (0);
	if (nd->nd_procnum >= NFS_NPROCS ||
		(nd->nd_procnum >= NQNFSPROC_GETLEASE) ||
		(!nd->nd_flag && nd->nd_procnum > NFSV2PROC_STATFS)) {
		nd->nd_repstat = EPROCUNAVAIL;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	if ((nd->nd_flag & ND_NFSV3) == 0)
		nd->nd_procnum = nfsv3_procid[nd->nd_procnum];
	auth_type = *tl++;
	len = fxdr_unsigned(int, *tl++);
	if (len < 0 || len > RPCAUTH_MAXSIZ) {
		m_freem(info.mrep);
		return (EBADRPC);
	}

	nd->nd_flag &= ~ND_KERBAUTH;
	/*
	 * Handle auth_unix or auth_kerb.
	 */
	if (auth_type == rpc_auth_unix) {
		len = fxdr_unsigned(int, *++tl);
		if (len < 0 || len > NFS_MAXNAMLEN) {
			m_freem(info.mrep);
			return (EBADRPC);
		}
		ERROROUT(nfsm_adv(&info, nfsm_rndup(len)));
		NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
		bzero((caddr_t)&nd->nd_cr, sizeof (struct ucred));
		nd->nd_cr.cr_ref = 1;
		nd->nd_cr.cr_uid = fxdr_unsigned(uid_t, *tl++);
		nd->nd_cr.cr_ruid = nd->nd_cr.cr_svuid = nd->nd_cr.cr_uid;
		nd->nd_cr.cr_gid = fxdr_unsigned(gid_t, *tl++);
		nd->nd_cr.cr_rgid = nd->nd_cr.cr_svgid = nd->nd_cr.cr_gid;
		len = fxdr_unsigned(int, *tl);
		if (len < 0 || len > RPCAUTH_UNIXGIDS) {
			m_freem(info.mrep);
			return (EBADRPC);
		}
		NULLOUT(tl = nfsm_dissect(&info, (len + 2) * NFSX_UNSIGNED));
		for (i = 1; i <= len; i++)
		    if (i < NGROUPS)
			nd->nd_cr.cr_groups[i] = fxdr_unsigned(gid_t, *tl++);
		    else
			tl++;
		nd->nd_cr.cr_ngroups = (len >= NGROUPS) ? NGROUPS : (len + 1);
		if (nd->nd_cr.cr_ngroups > 1)
		    nfsrvw_sort(nd->nd_cr.cr_groups, nd->nd_cr.cr_ngroups);
		len = fxdr_unsigned(int, *++tl);
		if (len < 0 || len > RPCAUTH_MAXSIZ) {
			m_freem(info.mrep);
			return (EBADRPC);
		}
		if (len > 0) {
			ERROROUT(nfsm_adv(&info, nfsm_rndup(len)));
		}
	} else if (auth_type == rpc_auth_kerb) {
		switch (fxdr_unsigned(int, *tl++)) {
		case RPCAKN_FULLNAME:
			ticklen = fxdr_unsigned(int, *tl);
			*((u_int32_t *)nfsd->nfsd_authstr) = *tl;
			uio.uio_resid = nfsm_rndup(ticklen) + NFSX_UNSIGNED;
			nfsd->nfsd_authlen = uio.uio_resid + NFSX_UNSIGNED;
			if (uio.uio_resid > (len - 2 * NFSX_UNSIGNED)) {
				m_freem(info.mrep);
				return (EBADRPC);
			}
			uio.uio_offset = 0;
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			uio.uio_segflg = UIO_SYSSPACE;
			iov.iov_base = (caddr_t)&nfsd->nfsd_authstr[4];
			iov.iov_len = RPCAUTH_MAXSIZ - 4;
			ERROROUT(nfsm_mtouio(&info, &uio, uio.uio_resid));
			NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
			if (*tl++ != rpc_auth_kerb ||
				fxdr_unsigned(int, *tl) != 4 * NFSX_UNSIGNED) {
				kprintf("Bad kerb verifier\n");
				nd->nd_repstat = (NFSERR_AUTHERR|AUTH_BADVERF);
				nd->nd_procnum = NFSPROC_NOOP;
				return (0);
			}
			NULLOUT(cp = nfsm_dissect(&info, 4 * NFSX_UNSIGNED));
			tl = (u_int32_t *)cp;
			if (fxdr_unsigned(int, *tl) != RPCAKN_FULLNAME) {
				kprintf("Not fullname kerb verifier\n");
				nd->nd_repstat = (NFSERR_AUTHERR|AUTH_BADVERF);
				nd->nd_procnum = NFSPROC_NOOP;
				return (0);
			}
			cp += NFSX_UNSIGNED;
			bcopy(cp, nfsd->nfsd_verfstr, 3 * NFSX_UNSIGNED);
			nfsd->nfsd_verflen = 3 * NFSX_UNSIGNED;
			nd->nd_flag |= ND_KERBFULL;
			nfsd->nfsd_flag |= NFSD_NEEDAUTH;
			break;
		case RPCAKN_NICKNAME:
			if (len != 2 * NFSX_UNSIGNED) {
				kprintf("Kerb nickname short\n");
				nd->nd_repstat = (NFSERR_AUTHERR|AUTH_BADCRED);
				nd->nd_procnum = NFSPROC_NOOP;
				return (0);
			}
			nickuid = fxdr_unsigned(uid_t, *tl);
			NULLOUT(tl = nfsm_dissect(&info, 2 * NFSX_UNSIGNED));
			if (*tl++ != rpc_auth_kerb ||
				fxdr_unsigned(int, *tl) != 3 * NFSX_UNSIGNED) {
				kprintf("Kerb nick verifier bad\n");
				nd->nd_repstat = (NFSERR_AUTHERR|AUTH_BADVERF);
				nd->nd_procnum = NFSPROC_NOOP;
				return (0);
			}
			NULLOUT(tl = nfsm_dissect(&info, 3 * NFSX_UNSIGNED));
			tvin.tv_sec = *tl++;
			tvin.tv_usec = *tl;

			for (nuidp = NUIDHASH(nfsd->nfsd_slp,nickuid)->lh_first;
			    nuidp != NULL; nuidp = nuidp->nu_hash.le_next) {
				if (nuidp->nu_cr.cr_uid == nickuid &&
				    (!nd->nd_nam2 ||
				     netaddr_match(NU_NETFAM(nuidp),
				      &nuidp->nu_haddr, nd->nd_nam2)))
					break;
			}
			if (!nuidp) {
				nd->nd_repstat =
					(NFSERR_AUTHERR|AUTH_REJECTCRED);
				nd->nd_procnum = NFSPROC_NOOP;
				return (0);
			}

			/*
			 * Now, decrypt the timestamp using the session key
			 * and validate it.
			 */
#ifdef NFSKERB
			XXX
#else
			tvout.tv_sec = 0;
			tvout.tv_usec = 0;
#endif

			tvout.tv_sec = fxdr_unsigned(long, tvout.tv_sec);
			tvout.tv_usec = fxdr_unsigned(long, tvout.tv_usec);
			if (nuidp->nu_expire != time_uptime ||
			    nuidp->nu_timestamp.tv_sec > tvout.tv_sec ||
			    (nuidp->nu_timestamp.tv_sec == tvout.tv_sec &&
			     nuidp->nu_timestamp.tv_usec > tvout.tv_usec)) {
				nuidp->nu_expire = 0;
				nd->nd_repstat =
				    (NFSERR_AUTHERR|AUTH_REJECTVERF);
				nd->nd_procnum = NFSPROC_NOOP;
				return (0);
			}
			nfsrv_setcred(&nuidp->nu_cr, &nd->nd_cr);
			nd->nd_flag |= ND_KERBNICK;
			break;
		}
	} else {
		nd->nd_repstat = (NFSERR_AUTHERR | AUTH_REJECTCRED);
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}

	nd->nd_md = info.md;
	nd->nd_dpos = info.dpos;
	return (0);
nfsmout:
	return (error);
}

#endif

/*
 * Send a message to the originating process's terminal.  The thread and/or
 * process may be NULL.  YYY the thread should not be NULL but there may
 * still be some uio_td's that are still being passed as NULL through to
 * nfsm_request().
 */
static int
nfs_msg(struct thread *td, char *server, char *msg)
{
	tpr_t tpr;

	if (td && td->td_proc)
		tpr = tprintf_open(td->td_proc);
	else
		tpr = NULL;
	tprintf(tpr, "nfs server %s: %s\n", server, msg);
	tprintf_close(tpr);
	return (0);
}

#ifndef NFS_NOSERVER

/*
 * Socket upcall routine for nfsd sockets.  This runs in the protocol
 * thread and passes waitflag == MB_DONTWAIT.
 */
void
nfsrv_rcv_upcall(struct socket *so, void *arg, int waitflag)
{
	struct nfssvc_sock *slp = (struct nfssvc_sock *)arg;

	if (slp->ns_needq_upcall == 0) {
		slp->ns_needq_upcall = 1;	/* ok to race */
		lwkt_gettoken(&nfs_token);
		nfsrv_wakenfsd(slp, 1);
		lwkt_reltoken(&nfs_token);
	}
#if 0
	lwkt_gettoken(&slp->ns_token);
	slp->ns_flag |= SLP_NEEDQ;
	nfsrv_rcv(so, arg, waitflag);
	lwkt_reltoken(&slp->ns_token);
#endif
}

/*
 * Process new data on a receive socket.  Essentially do as much as we can
 * non-blocking, else punt and it will be called with MB_WAIT from an nfsd.
 *
 * slp->ns_token is held on call
 */
void
nfsrv_rcv(struct socket *so, void *arg, int waitflag)
{
	struct nfssvc_sock *slp = (struct nfssvc_sock *)arg;
	struct mbuf *m;
	struct sockaddr *nam;
	struct sockbuf sio;
	int flags, error;
	int nparallel_wakeup = 0;

	ASSERT_LWKT_TOKEN_HELD(&slp->ns_token);

	if ((slp->ns_flag & SLP_VALID) == 0)
		return;

	/*
	 * Do not allow an infinite number of completed RPC records to build 
	 * up before we stop reading data from the socket.  Otherwise we could
	 * end up holding onto an unreasonable number of mbufs for requests
	 * waiting for service.
	 *
	 * This should give pretty good feedback to the TCP layer and
	 * prevents a memory crunch for other protocols.
	 *
	 * Note that the same service socket can be dispatched to several
	 * nfs servers simultaniously.  The tcp protocol callback calls us
	 * with MB_DONTWAIT.  nfsd calls us with MB_WAIT (typically).
	 */
	if (NFSRV_RECLIMIT(slp))
		return;

	/*
	 * Handle protocol specifics to parse an RPC request.  We always
	 * pull from the socket using non-blocking I/O.
	 */
	if (so->so_type == SOCK_STREAM) {
		/*
		 * The data has to be read in an orderly fashion from a TCP
		 * stream, unlike a UDP socket.  It is possible for soreceive
		 * and/or nfsrv_getstream() to block, so make sure only one
		 * entity is messing around with the TCP stream at any given
		 * moment.  The receive sockbuf's lock in soreceive is not
		 * sufficient.
		 */
		if (slp->ns_flag & SLP_GETSTREAM)
			return;
		slp->ns_flag |= SLP_GETSTREAM;

		/*
		 * Do soreceive().  Pull out as much data as possible without
		 * blocking.
		 */
		sbinit(&sio, 1000000000);
		flags = MSG_DONTWAIT;
		error = so_pru_soreceive(so, &nam, NULL, &sio, NULL, &flags);
		if (error || sio.sb_mb == NULL) {
			if (error != EWOULDBLOCK)
				slp->ns_flag |= SLP_DISCONN;
			slp->ns_flag &= ~(SLP_GETSTREAM | SLP_NEEDQ);
			goto done;
		}
		m = sio.sb_mb;
		if (slp->ns_rawend) {
			slp->ns_rawend->m_next = m;
			slp->ns_cc += sio.sb_cc;
		} else {
			slp->ns_raw = m;
			slp->ns_cc = sio.sb_cc;
		}
		while (m->m_next)
			m = m->m_next;
		slp->ns_rawend = m;

		/*
		 * Now try and parse as many record(s) as we can out of the
		 * raw stream data.  This will set SLP_DOREC.
		 */
		error = nfsrv_getstream(slp, waitflag, &nparallel_wakeup);
		if (error && error != EWOULDBLOCK)
			slp->ns_flag |= SLP_DISCONN;
		slp->ns_flag &= ~SLP_GETSTREAM;
	} else {
		/*
		 * For UDP soreceive typically pulls just one packet, loop
		 * to get the whole batch.
		 */
		do {
			sbinit(&sio, 1000000000);
			flags = MSG_DONTWAIT;
			error = so_pru_soreceive(so, &nam, NULL, &sio,
						 NULL, &flags);
			if (sio.sb_mb) {
				struct nfsrv_rec *rec;
				int mf = (waitflag & MB_DONTWAIT) ?
					    M_NOWAIT : M_WAITOK;
				rec = kmalloc(sizeof(struct nfsrv_rec),
					     M_NFSRVDESC, mf);
				if (!rec) {
					if (nam)
						kfree(nam, M_SONAME);
					m_freem(sio.sb_mb);
					continue;
				}
				nfs_realign(&sio.sb_mb, 10 * NFSX_UNSIGNED);
				rec->nr_address = nam;
				rec->nr_packet = sio.sb_mb;
				STAILQ_INSERT_TAIL(&slp->ns_rec, rec, nr_link);
				++slp->ns_numrec;
				slp->ns_flag |= SLP_DOREC;
				++nparallel_wakeup;
			} else {
				slp->ns_flag &= ~SLP_NEEDQ;
			}
			if (error) {
				if ((so->so_proto->pr_flags & PR_CONNREQUIRED)
				    && error != EWOULDBLOCK) {
					slp->ns_flag |= SLP_DISCONN;
					break;
				}
			}
			if (NFSRV_RECLIMIT(slp))
				break;
		} while (sio.sb_mb);
	}

	/*
	 * If we were upcalled from the tcp protocol layer and we have
	 * fully parsed records ready to go, or there is new data pending,
	 * or something went wrong, try to wake up a nfsd thread to deal
	 * with it.
	 */
done:
	/* XXX this code is currently not executed (nfsrv_rcv_upcall) */
	if (waitflag == MB_DONTWAIT && (slp->ns_flag & SLP_ACTION_MASK)) {
		lwkt_gettoken(&nfs_token);
		nfsrv_wakenfsd(slp, nparallel_wakeup);
		lwkt_reltoken(&nfs_token);
	}
}

/*
 * Try and extract an RPC request from the mbuf data list received on a
 * stream socket. The "waitflag" argument indicates whether or not it
 * can sleep.
 */
static int
nfsrv_getstream(struct nfssvc_sock *slp, int waitflag, int *countp)
{
	struct mbuf *m, **mpp;
	char *cp1, *cp2;
	int len;
	struct mbuf *om, *m2, *recm;
	u_int32_t recmark;

	for (;;) {
	    if (slp->ns_reclen == 0) {
		if (slp->ns_cc < NFSX_UNSIGNED)
			return (0);
		m = slp->ns_raw;
		if (m->m_len >= NFSX_UNSIGNED) {
			bcopy(mtod(m, caddr_t), (caddr_t)&recmark, NFSX_UNSIGNED);
			m->m_data += NFSX_UNSIGNED;
			m->m_len -= NFSX_UNSIGNED;
		} else {
			cp1 = (caddr_t)&recmark;
			cp2 = mtod(m, caddr_t);
			while (cp1 < ((caddr_t)&recmark) + NFSX_UNSIGNED) {
				while (m->m_len == 0) {
					m = m->m_next;
					cp2 = mtod(m, caddr_t);
				}
				*cp1++ = *cp2++;
				m->m_data++;
				m->m_len--;
			}
		}
		slp->ns_cc -= NFSX_UNSIGNED;
		recmark = ntohl(recmark);
		slp->ns_reclen = recmark & ~0x80000000;
		if (recmark & 0x80000000)
			slp->ns_flag |= SLP_LASTFRAG;
		else
			slp->ns_flag &= ~SLP_LASTFRAG;
		if (slp->ns_reclen > NFS_MAXPACKET || slp->ns_reclen <= 0) {
			log(LOG_ERR, "%s (%d) from nfs client\n",
			    "impossible packet length",
			    slp->ns_reclen);
			return (EPERM);
		}
	    }

	    /*
	     * Now get the record part.
	     *
	     * Note that slp->ns_reclen may be 0.  Linux sometimes
	     * generates 0-length RPCs
	     */
	    recm = NULL;
	    if (slp->ns_cc == slp->ns_reclen) {
		recm = slp->ns_raw;
		slp->ns_raw = slp->ns_rawend = NULL;
		slp->ns_cc = slp->ns_reclen = 0;
	    } else if (slp->ns_cc > slp->ns_reclen) {
		len = 0;
		m = slp->ns_raw;
		om = NULL;

		while (len < slp->ns_reclen) {
			if ((len + m->m_len) > slp->ns_reclen) {
				m2 = m_copym(m, 0, slp->ns_reclen - len,
					waitflag);
				if (m2) {
					if (om) {
						om->m_next = m2;
						recm = slp->ns_raw;
					} else
						recm = m2;
					m->m_data += slp->ns_reclen - len;
					m->m_len -= slp->ns_reclen - len;
					len = slp->ns_reclen;
				} else {
					return (EWOULDBLOCK);
				}
			} else if ((len + m->m_len) == slp->ns_reclen) {
				om = m;
				len += m->m_len;
				m = m->m_next;
				recm = slp->ns_raw;
				om->m_next = NULL;
			} else {
				om = m;
				len += m->m_len;
				m = m->m_next;
			}
		}
		slp->ns_raw = m;
		slp->ns_cc -= len;
		slp->ns_reclen = 0;
	    } else {
		return (0);
	    }

	    /*
	     * Accumulate the fragments into a record.
	     */
	    mpp = &slp->ns_frag;
	    while (*mpp)
		mpp = &((*mpp)->m_next);
	    *mpp = recm;
	    if (slp->ns_flag & SLP_LASTFRAG) {
		struct nfsrv_rec *rec;
		int mf = (waitflag & MB_DONTWAIT) ? M_NOWAIT : M_WAITOK;
		rec = kmalloc(sizeof(struct nfsrv_rec), M_NFSRVDESC, mf);
		if (!rec) {
		    m_freem(slp->ns_frag);
		} else {
		    nfs_realign(&slp->ns_frag, 10 * NFSX_UNSIGNED);
		    rec->nr_address = NULL;
		    rec->nr_packet = slp->ns_frag;
		    STAILQ_INSERT_TAIL(&slp->ns_rec, rec, nr_link);
		    ++slp->ns_numrec;
		    slp->ns_flag |= SLP_DOREC;
		    ++*countp;
		}
		slp->ns_frag = NULL;
	    }
	}
}

#ifdef INVARIANTS

/*
 * Sanity check our mbuf chain.
 */
static void
nfs_checkpkt(struct mbuf *m, int len)
{
	int xlen = 0;
	while (m) {
		xlen += m->m_len;
		m = m->m_next;
	}
	if (xlen != len) {
		panic("nfs_checkpkt: len mismatch %d/%d mbuf %p",
			xlen, len, m);
	}
}

#else

static void
nfs_checkpkt(struct mbuf *m __unused, int len __unused)
{
}

#endif

/*
 * Parse an RPC header.
 *
 * If the socket is invalid or no records are pending we return ENOBUFS.
 * The caller must deal with NEEDQ races.
 */
int
nfsrv_dorec(struct nfssvc_sock *slp, struct nfsd *nfsd,
	    struct nfsrv_descript **ndp)
{
	struct nfsrv_rec *rec;
	struct mbuf *m;
	struct sockaddr *nam;
	struct nfsrv_descript *nd;
	int error;

	*ndp = NULL;
	if ((slp->ns_flag & SLP_VALID) == 0 || !STAILQ_FIRST(&slp->ns_rec))
		return (ENOBUFS);
	rec = STAILQ_FIRST(&slp->ns_rec);
	STAILQ_REMOVE_HEAD(&slp->ns_rec, nr_link);
	KKASSERT(slp->ns_numrec > 0);
	if (--slp->ns_numrec == 0)
		slp->ns_flag &= ~SLP_DOREC;
	nam = rec->nr_address;
	m = rec->nr_packet;
	kfree(rec, M_NFSRVDESC);
	nd = kmalloc(sizeof(struct nfsrv_descript), M_NFSRVDESC, M_WAITOK);
	nd->nd_md = nd->nd_mrep = m;
	nd->nd_nam2 = nam;
	nd->nd_dpos = mtod(m, caddr_t);
	error = nfs_getreq(nd, nfsd, TRUE);
	if (error) {
		if (nam) {
			kfree(nam, M_SONAME);
		}
		kfree((caddr_t)nd, M_NFSRVDESC);
		return (error);
	}
	*ndp = nd;
	nfsd->nfsd_nd = nd;
	return (0);
}

/*
 * Try to assign service sockets to nfsd threads based on the number
 * of new rpc requests that have been queued on the service socket.
 *
 * If no nfsd's are available or additonal requests are pending, set the
 * NFSD_CHECKSLP flag so that one of the running nfsds will go look for
 * the work in the nfssvc_sock list when it is finished processing its
 * current work.  This flag is only cleared when an nfsd can not find
 * any new work to perform.
 */
void
nfsrv_wakenfsd(struct nfssvc_sock *slp, int nparallel)
{
	struct nfsd *nd;

	if ((slp->ns_flag & SLP_VALID) == 0)
		return;
	if (nparallel <= 1)
		nparallel = 1;
	TAILQ_FOREACH(nd, &nfsd_head, nfsd_chain) {
		if (nd->nfsd_flag & NFSD_WAITING) {
			nd->nfsd_flag &= ~NFSD_WAITING;
			if (nd->nfsd_slp)
				panic("nfsd wakeup");
			nfsrv_slpref(slp);
			nd->nfsd_slp = slp;
			wakeup((caddr_t)nd);
			if (--nparallel == 0)
				break;
		}
	}

	/*
	 * If we couldn't assign slp then the NFSDs are all busy and
	 * we set a flag indicating that there is pending work.
	 */
	if (nparallel)
		nfsd_head_flag |= NFSD_CHECKSLP;
}
#endif /* NFS_NOSERVER */
