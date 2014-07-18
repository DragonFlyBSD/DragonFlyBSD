/*-
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)socketvar.h	8.3 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/socketvar.h,v 1.46.2.10 2003/08/24 08:24:39 hsu Exp $
 * $DragonFly: src/sys/sys/socketvar.h,v 1.35 2008/08/28 23:15:45 dillon Exp $
 */

#ifndef _SYS_SOCKETVAR_H_
#define _SYS_SOCKETVAR_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>			/* for TAILQ macros */
#endif
#ifndef _SYS_EVENT_H_
#include <sys/event.h>			/* for struct kqinfo */
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>			/* for struct lwkt_token */
#endif
#ifndef _SYS_SOCKBUF_H_
#include <sys/sockbuf.h>
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _NET_NETMSG_H_
#include <net/netmsg.h>
#endif

#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

struct accept_filter;

/*
 * Signaling socket buffers contain additional elements for locking
 * and signaling conditions.  These are used primarily by sockets.
 *
 * WARNING: See partial clearing of fields in kern/uipc_socket.c
 *	    sorflush() and sowflush().
 */
struct signalsockbuf {
	struct sockbuf sb;
	struct kqinfo ssb_kq;	/* process selecting read/write */
	uint32_t ssb_flags;	/* flags, see below (use atomic ops) */
	u_int	ssb_timeo;	/* timeout for read/write */
	long	ssb_lowat;	/* low water mark */
	u_long	ssb_hiwat;	/* high water mark / max actual char count */
	u_long	ssb_mbmax;	/* max chars of mbufs to use */
	struct lwkt_token ssb_token; /* frontend/backend serializer */
};

#define ssb_cc		sb.sb_cc	/* commonly used fields */
#define ssb_mb		sb.sb_mb	/* commonly used fields */
#define ssb_mbcnt	sb.sb_mbcnt	/* commonly used fields */
#define ssb_cc_prealloc	sb.sb_cc_prealloc
#define ssb_mbcnt_prealloc sb.sb_mbcnt_prealloc

#define	SSB_LOCK	0x0001		/* lock on data queue */
#define	SSB_WANT	0x0002		/* someone is waiting to lock */
#define	SSB_WAIT	0x0004		/* someone is waiting for data/space */
#define	SSB_ASYNC	0x0010		/* ASYNC I/O, need signals */
#define	SSB_UPCALL	0x0020		/* someone wants an upcall */
#define	SSB_NOINTR	0x0040		/* operations not interruptible */
/*#define SSB_AIO	0x0080*/	/* AIO operations queued */
#define SSB_KNOTE	0x0100		/* kernel note attached */
#define SSB_MEVENT	0x0200		/* need message event notification */
#define SSB_STOP	0x0400		/* backpressure indicator */
#define	SSB_AUTOSIZE	0x0800		/* automatically size socket buffer */
#define SSB_AUTOLOWAT	0x1000		/* automatically scale lowat */
#define SSB_WAKEUP	0x2000		/* wakeup event race */
#define SSB_PREALLOC	0x4000		/* prealloc supported */
#define SSB_STOPSUPP	0x8000		/* SSB_STOP supported */

#define SSB_CLEAR_MASK	(SSB_ASYNC | SSB_UPCALL | SSB_STOP | \
			 SSB_AUTOSIZE | SSB_AUTOLOWAT)

#define SSB_NOTIFY_MASK	(SSB_WAIT | SSB_ASYNC | SSB_UPCALL | \
			 SSB_KNOTE | SSB_MEVENT)

/*
 * Per-socket kernel structure.  Contains universal send and receive queues,
 * protocol control handle, and error information.
 */
struct socket {
	short	so_type;		/* generic type, see socket.h */
	short	so_options;		/* from socket call, see socket.h */
	short	so_linger;		/* time to linger while closing */
	short	so_state;		/* internal state flags SS_*, below */
	void	*so_pcb;		/* protocol control block */
	struct	protosw *so_proto;	/* protocol handle */
	struct	socket *so_head;	/* back pointer to accept socket */
	lwkt_port_t so_port;		/* message port */

	/*
	 * These fields are used to manage sockets capable of accepting
	 * new connections.
	 */
	TAILQ_HEAD(, socket) so_incomp;	/* in-progress, incomplete */
	TAILQ_HEAD(, socket) so_comp;	/* completed but not yet accepted */
	TAILQ_ENTRY(socket) so_list;	/* list of unaccepted connections */
	short	so_qlen;		/* so_comp count */
	short	so_incqlen;		/* so_incomp count */
	short	so_qlimit;		/* max number queued connections */

	/*
	 * Misc socket support
	 */
	short	so_timeo;		/* connection timeout */
	u_short	so_error;		/* error affecting connection */
	struct  sigio *so_sigio;	/* information for async I/O or
					   out of band data (SIGURG) */
	u_long	so_oobmark;		/* chars to oob mark */
	TAILQ_HEAD(, aiocblist) so_aiojobq; /* AIO ops waiting on socket */
	struct signalsockbuf so_rcv;
	struct signalsockbuf so_snd;

	void	(*so_upcall) (struct socket *, void *, int);
	void	*so_upcallarg;
	struct	ucred *so_cred;		/* user credentials */
	/* NB: generation count must not be first; easiest to make it last. */
	void	*so_emuldata;		/* private data for emulators */
	int	so_refs;		/* shutdown refs */
	struct	so_accf { 
		struct	accept_filter *so_accept_filter;
		void	*so_accept_filter_arg;	/* saved filter args */
		char	*so_accept_filter_str;	/* saved user args */
	} *so_accf;

	struct netmsg_base so_clomsg;
	struct sockaddr *so_faddr;

	struct spinlock so_rcvd_spin;
	struct netmsg_pru_rcvd so_rcvd_msg;
};

#endif

/*
 * Socket state bits.
 *
 * NOTE: The following states are interlocked with so_refs:
 *
 *	SS_NOFDREF	so_refs while not set
 *	(so_pcb)	so_refs while set
 */
#define	SS_NOFDREF		0x0001	/* no file table ref any more */
#define	SS_ISCONNECTED		0x0002	/* socket connected to a peer */
#define	SS_ISCONNECTING		0x0004	/* in process of connecting to peer */
#define	SS_ISDISCONNECTING	0x0008	/* in process of disconnecting */
#define	SS_CANTSENDMORE		0x0010	/* can't send more data to peer */
#define	SS_CANTRCVMORE		0x0020	/* can't receive more data from peer */
#define	SS_RCVATMARK		0x0040	/* at mark on input */

#define	SS_ASSERTINPROG		0x0100	/* sonewconn race debugging */
#define	SS_ASYNC		0x0200	/* async i/o notify */
#define	SS_ISCONFIRMING		0x0400	/* deciding to accept connection req */

#define	SS_INCOMP		0x0800	/* unaccepted, incomplete connection */
#define	SS_COMP			0x1000	/* unaccepted, complete connection */
#define	SS_ISDISCONNECTED	0x2000	/* socket disconnected from peer */

/*
 * Externalized form of struct socket used by the sysctl(3) interface.
 */
struct	xsocket {
	size_t	xso_len;	/* length of this structure */
	struct	socket *xso_so;	/* makes a convenient handle sometimes */
	short	so_type;
	short	so_options;
	short	so_linger;
	short	so_state;
	void	*so_pcb;		/* another convenient handle */
	int	xso_protocol;
	int	xso_family;
	short	so_qlen;
	short	so_incqlen;
	short	so_qlimit;
	short	so_timeo;
	u_short	so_error;
	pid_t	so_pgid;
	u_long	so_oobmark;
	struct	xsockbuf {
		u_long	sb_cc;
		u_long	sb_hiwat;
		u_long	sb_mbcnt;
		u_long	sb_mbmax;
		long	sb_lowat;
		u_int	sb_timeo;
		short	sb_flags;
	} so_rcv, so_snd;
	uid_t	so_uid;		/* XXX */
};

/*
 * Macros for sockets and socket buffering.
 */

#define	sosendallatonce(so) \
    ((so)->so_proto->pr_flags & PR_ATOMIC)

/* can we read something from so? */
#define	soreadable(so) \
    ((so)->so_rcv.ssb_cc >= (so)->so_rcv.ssb_lowat || \
	((so)->so_state & SS_CANTRCVMORE) || \
	!TAILQ_EMPTY(&(so)->so_comp) || (so)->so_error)

/* can we write something to so? */
#define	sowriteable(so) \
    ((ssb_space(&(so)->so_snd) >= (so)->so_snd.ssb_lowat && \
	(((so)->so_state&SS_ISCONNECTED) || \
	  ((so)->so_proto->pr_flags&PR_CONNREQUIRED)==0)) || \
     ((so)->so_state & SS_CANTSENDMORE) || \
     (so)->so_error)

/* do we have to send all at once on a socket? */

#ifdef _KERNEL

/*
 * How much space is there in a socket buffer (so->so_snd or so->so_rcv)?
 * This is problematical if the fields are unsigned, as the space might
 * still be negative (cc > hiwat or mbcnt > mbmax).  Should detect
 * overflow and return 0.
 *
 * SSB_STOP ignores cc/hiwat and returns 0.  This is used by unix domain
 * stream sockets to signal backpressure.
 */
static __inline
long
ssb_space(struct signalsockbuf *ssb)
{
	long bleft;
	long mleft;

	if (ssb->ssb_flags & SSB_STOP)
		return(0);
	bleft = ssb->ssb_hiwat - ssb->ssb_cc;
	mleft = ssb->ssb_mbmax - ssb->ssb_mbcnt;
	return((bleft < mleft) ? bleft : mleft);
}

static __inline long
ssb_space_prealloc(struct signalsockbuf *ssb)
{
	long bleft, bleft_prealloc;
	long mleft, mleft_prealloc;

	if (ssb->ssb_flags & SSB_STOP)
		return(0);

	bleft = ssb->ssb_hiwat - ssb->ssb_cc;
	bleft_prealloc = ssb->ssb_hiwat - ssb->ssb_cc_prealloc;
	if (bleft_prealloc < bleft)
		bleft = bleft_prealloc;

	mleft = ssb->ssb_mbmax - ssb->ssb_mbcnt;
	mleft_prealloc = ssb->ssb_mbmax - ssb->ssb_mbcnt_prealloc;
	if (mleft_prealloc < mleft)
		mleft = mleft_prealloc;

	return((bleft < mleft) ? bleft : mleft);
}

/*
 * NOTE: Only works w/ later ssb_appendstream() on m
 */
static __inline void
ssb_preallocstream(struct signalsockbuf *ssb, struct mbuf *m)
{
	if (m->m_len == 0)
		return;
	sbprealloc(&ssb->sb, m);
}

#endif

#define ssb_append(ssb, m)						\
	sbappend(&(ssb)->sb, m)

#define ssb_appendstream(ssb, m)					\
	sbappendstream(&(ssb)->sb, m)

#define ssb_appendrecord(ssb, m)					\
	sbappendrecord(&(ssb)->sb, m)

#define ssb_appendaddr(ssb, src, m, control)				\
	((ssb_space(ssb) <= 0) ? 0 : sbappendaddr(&(ssb)->sb, src, m, control))

#define ssb_appendcontrol(ssb, m, control)				\
	((ssb_space(ssb) <= 0) ? 0 : sbappendcontrol(&(ssb)->sb, m, control))

#define ssb_insert_knote(ssb, kn) {					\
	knote_insert(&(ssb)->ssb_kq.ki_note, kn);			\
	atomic_set_int(&(ssb)->ssb_flags, SSB_KNOTE);			\
}

#define ssb_remove_knote(ssb, kn) {					\
	knote_remove(&(ssb)->ssb_kq.ki_note, kn);			\
	if (SLIST_EMPTY(&(ssb)->ssb_kq.ki_note))			\
		atomic_clear_int(&(ssb)->ssb_flags, SSB_KNOTE);		\
}

#define	sorwakeup(so)	sowakeup((so), &(so)->so_rcv)
#define	sowwakeup(so)	sowakeup((so), &(so)->so_snd)

#ifdef _KERNEL

/*
 * Argument structure for sosetopt et seq.  This is in the KERNEL
 * section because it will never be visible to user code.
 */
enum sopt_dir { SOPT_GET, SOPT_SET };
struct sockopt {
	enum	sopt_dir sopt_dir; /* is this a get or a set? */
	int	sopt_level;	/* second arg of [gs]etsockopt */
	int	sopt_name;	/* third arg of [gs]etsockopt */
	void   *sopt_val;	/* fourth arg of [gs]etsockopt */
	size_t	sopt_valsize;	/* (almost) fifth arg of [gs]etsockopt */
	struct	thread *sopt_td; /* calling thread or null if kernel */
};

struct accept_filter {
	char	accf_name[16];
	void	(*accf_callback)
		(struct socket *so, void *arg, int waitflag);
	void *	(*accf_create)
		(struct socket *so, char *arg);
	void	(*accf_destroy)
		(struct socket *so);
	SLIST_ENTRY(accept_filter) accf_next;	/* next on the list */
};

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_PCB);
MALLOC_DECLARE(M_SONAME);
MALLOC_DECLARE(M_ACCF);
#endif

extern int	maxsockets;
extern u_long	sb_max;		/* nominal limit */
extern u_long	sb_max_adj;	/* actual limit used by sbreserve() */

struct file;
struct filedesc;
struct mbuf;
struct rlimit;
struct sockaddr;
struct stat;
struct ucred;
struct uio;
struct knote;
struct sysmsg;

/*
 * File operations on sockets.
 */
int	soo_read (struct file *fp, struct uio *uio, struct ucred *cred,
			int flags);
int	soo_write (struct file *fp, struct uio *uio, struct ucred *cred,
			int flags);
int	soo_close (struct file *fp);
int	soo_shutdown (struct file *fp, int how);
int	soo_ioctl (struct file *fp, u_long cmd, caddr_t data,
			struct ucred *cred, struct sysmsg *msg);
int	soo_stat (struct file *fp, struct stat *ub, struct ucred *cred);
int	sokqfilter (struct file *fp, struct knote *kn);

/*
 * From uipc_socket and friends
 */
struct	sockaddr *dup_sockaddr (const struct sockaddr *sa);
int	getsockaddr (struct sockaddr **namp, caddr_t uaddr, size_t len);

void	ssb_release (struct signalsockbuf *ssb, struct socket *so);
int	ssb_reserve (struct signalsockbuf *ssb, u_long cc, struct socket *so,
		   struct rlimit *rl);
void	ssbtoxsockbuf (struct signalsockbuf *sb, struct xsockbuf *xsb);
int	ssb_wait (struct signalsockbuf *sb);
int	_ssb_lock (struct signalsockbuf *sb);

void	soabort (struct socket *so);
void	soabort_async (struct socket *so);
void	soabort_oncpu (struct socket *so);
int	soaccept (struct socket *so, struct sockaddr **nam);
void	soaccept_generic (struct socket *so);
struct	socket *soalloc (int waitok, struct protosw *);
int	sobind (struct socket *so, struct sockaddr *nam, struct thread *td);
void	socantrcvmore (struct socket *so);
void	socantsendmore (struct socket *so);
int	socket_wait (struct socket *so, struct timespec *ts, int *res);
int	soclose (struct socket *so, int fflags);
int	soconnect (struct socket *so, struct sockaddr *nam, struct thread *td,
	    boolean_t sync);
int	soconnect2 (struct socket *so1, struct socket *so2);
int	socreate (int dom, struct socket **aso, int type, int proto,
	    struct thread *td);
int	sodisconnect (struct socket *so);
void	sofree (struct socket *so);
int	sogetopt (struct socket *so, struct sockopt *sopt);
void	sohasoutofband (struct socket *so);
void	soisconnected (struct socket *so);
void	soisconnecting (struct socket *so);
void	soisdisconnected (struct socket *so);
void	soisdisconnecting (struct socket *so);
void	soisreconnected (struct socket *so);
void	soisreconnecting (struct socket *so);
void	sosetport (struct socket *so, struct lwkt_port *port);
int	solisten (struct socket *so, int backlog, struct thread *td);
struct socket *sonewconn (struct socket *head, int connstatus);
struct socket *sonewconn_faddr (struct socket *head, int connstatus,
	    const struct sockaddr *faddr);
void	soinherit(struct socket *so, struct socket *so_inh);
int	sooptcopyin (struct sockopt *sopt, void *buf, size_t len,
			 size_t minlen);
int	soopt_to_kbuf (struct sockopt *sopt, void *buf, size_t len,
			 size_t minlen);
int	sooptcopyout (struct sockopt *sopt, const void *buf, size_t len);
void	soopt_from_kbuf (struct sockopt *sopt, const void *buf, size_t len);

/* XXX; prepare mbuf for (__FreeBSD__ < 3) routines. */
int	soopt_getm (struct sockopt *sopt, struct mbuf **mp);
int	soopt_mcopyin (struct sockopt *sopt, struct mbuf *m);
void	soopt_to_mbuf (struct sockopt *sopt, struct mbuf *m);
int	soopt_mcopyout (struct sockopt *sopt, struct mbuf *m);
int	soopt_from_mbuf (struct sockopt *sopt, struct mbuf *m);

int	soreceive (struct socket *so, struct sockaddr **paddr,
		       struct uio *uio, struct sockbuf *sio,
		       struct mbuf **controlp, int *flagsp);
int	sorecvtcp (struct socket *so, struct sockaddr **paddr,
		       struct uio *uio, struct sockbuf *sio,
		       struct mbuf **controlp, int *flagsp);
int	soreserve (struct socket *so, u_long sndcc, u_long rcvcc,
		   struct rlimit *rl);
void	sorflush (struct socket *so);
int	sosend (struct socket *so, struct sockaddr *addr, struct uio *uio,
		    struct mbuf *top, struct mbuf *control, int flags,
		    struct thread *td);
int	sosendudp (struct socket *so, struct sockaddr *addr, struct uio *uio,
		    struct mbuf *top, struct mbuf *control, int flags,
		    struct thread *td);
int	sosendtcp (struct socket *so, struct sockaddr *addr, struct uio *uio,
		    struct mbuf *top, struct mbuf *control, int flags,
		    struct thread *td);
int	sosetopt (struct socket *so, struct sockopt *sopt);
int	soshutdown (struct socket *so, int how);
void	sotoxsocket (struct socket *so, struct xsocket *xso);
void	sowakeup (struct socket *so, struct signalsockbuf *sb);

/* accept filter functions */
int	accept_filt_add (struct accept_filter *filt);
int	accept_filt_del (char *name);
struct accept_filter *	accept_filt_get (char *name);
#ifdef ACCEPT_FILTER_MOD
int accept_filt_generic_mod_event (module_t mod, int event, void *data);
SYSCTL_DECL(_net_inet_accf);
#endif /* ACCEPT_FILTER_MOD */

#endif /* _KERNEL */

#endif /* !_SYS_SOCKETVAR_H_ */
