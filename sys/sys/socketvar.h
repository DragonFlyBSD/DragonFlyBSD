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


#include <sys/types.h>
#include <sys/queue.h>			/* for TAILQ macros */
#include <sys/selinfo.h>		/* for struct selinfo */
#include <sys/sockbuf.h>
#include <sys/lock.h>

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct accept_filter;

/*
 * Signaling socket buffers contain additional elements for locking
 * and signaling conditions.  These are used primarily by sockets.
 */
struct signalsockbuf {
	struct sockbuf sb;
	/*
	 * protects access to ssb from user process context
	 */
	struct lock lk;
	struct selinfo ssb_sel;	/* process selecting read/write */
	short	ssb_flags;	/* flags, see below */
	short	ssb_timeo;	/* timeout for read/write */
	long	ssb_lowat;	/* low water mark */
	u_long	ssb_hiwat;	/* high water mark / max actual char count */
	u_long	ssb_mbmax;	/* max chars of mbufs to use */
	u_int	ssb_waiting;	/* waiters present */
};

#define	SSB_LOCK	0x01		/* lock on data queue */
#define	SSB_WANT	0x02		/* someone is waiting to lock */
#define	SSB_WAIT	0x04		/* someone is waiting for data/space */
#define	SSB_SEL		0x08		/* someone is selecting */
#define	SSB_ASYNC	0x10		/* ASYNC I/O, need signals */
#define	SSB_UPCALL	0x20		/* someone wants an upcall */
#define	SSB_NOINTR	0x40		/* operations not interruptible */
#define SSB_AIO		0x80		/* AIO operations queued */
#define SSB_KNOTE	0x100		/* kernel note attached */
#define SSB_MEVENT	0x200		/* need message event notification */

/*
 * Per-socket kernel structure.  Contains universal send and receive queues,
 * protocol control handle, and error information.
 */
struct socket {
	/*
	 * used for mutual exclusion between process context kernel code
	 */
	struct	lock so_lock;
	short	so_type;		/* generic type, see socket.h */
	short	so_options;		/* from socket call, see socket.h */
	short	so_linger;		/* time to linger while closing */
	short	so_state;		/* internal state flags SS_*, below */
	void	*so_pcb;		/* protocol control block */
	struct	protosw *so_proto;	/* protocol handle */
	struct	socket *so_head;	/* back pointer to accept socket */

	/*
	 * These fields are used to manage sockets capable of accepting
	 * new connections.
	 */
	struct	spinlock so_qlock;	/* protects following queues and
					 * len counts */
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
	struct netmsg_pru_notify so_notify_msg;

	void	(*so_upcall) (struct socket *, void *, int);
	void	*so_upcallarg;
	struct	ucred *so_cred;		/* user credentials */
	/* NB: generation count must not be first; easiest to make it last. */
	void	*so_emuldata;		/* private data for emulators */
	struct	so_accf { 
		struct	accept_filter *so_accept_filter;
		void	*so_accept_filter_arg;	/* saved filter args */
		char	*so_accept_filter_str;	/* saved user args */
	} *so_accf;
};

#endif

/*
 * Socket state bits.
 */
#define	SS_NOFDREF		0x0001	/* no file table ref any more */
#define	SS_ISCONNECTED		0x0002	/* socket connected to a peer */
#define	SS_ISCONNECTING		0x0004	/* in process of connecting to peer */
#define	SS_ISDISCONNECTING	0x0008	/* in process of disconnecting */
#define	SS_CANTSENDMORE		0x0010	/* can't send more data to peer */
#define	SS_CANTRCVMORE		0x0020	/* can't receive more data from peer */
#define	SS_RCVATMARK		0x0040	/* at mark on input */

#define	SS_ABORTING		0x0100	/* so_abort() in progress */
#define	SS_ASYNC		0x0200	/* async i/o notify */
					/* 0x0400 was SS_ISCONFIRMING */

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
		short	sb_flags;
		short	sb_timeo;
	} so_rcv, so_snd;
	uid_t	so_uid;		/* XXX */
};

#ifdef _KERNEL

/*
 * Macros for sockets and socket buffering.
 */

#define	sosendallatonce(so) \
    ((so)->so_proto->pr_flags & PR_ATOMIC)

/*
 * Return send space available based on hiwat.  Due to packetization
 * it is possible to temporarily exceed hiwat, so the return value 
 * is allowed to go negative.
 */
static inline long
ssb_space(struct signalsockbuf *ssb)
{
	long ret;

	ret = ssb->ssb_hiwat - sb_cc_est(&ssb->sb);

	return ret;
}

static inline long
ssb_reader_cc_est(struct signalsockbuf *ssb)
{
	return sb_cc_est(&ssb->sb);
}

static inline long
ssb_writer_cc_est(struct signalsockbuf *ssb)
{
	return sb_cc_est(&ssb->sb);
}

#if 0

static inline long
ssb_reader_space_est(struct signalsockbuf *ssb)
{
	long ret;

	/* actual space is <= than our estimate */
	ret = ssb->ssb_hiwat - sb_cc_est(&ssb->sb);

	/*
	 * no idea if ret can be <0, let's find out -- agg
	 */
	KKASSERT(ret >= 0);
	return ret;
}	

static inline long
ssb_writer_space_est(struct signalsockbuf *ssb)
{
	long ret;

	/* actual space is >= than our estimate */
	ret = ssb->ssb_hiwat - sb_cc_est(&ssb->sb);

	/*
	 * no idea if ret can be <0, let's find out -- agg
	 */
	KKASSERT(ret >= 0);
	return ret;
}	

#endif

/*
 * If true, the available space is >= bytes. If false,
 * we don't know
 */
static inline long
ssb_writer_space_ge(struct signalsockbuf *ssb, int bytes)
{
	long len;

	len = ssb->sb.wbytes - ssb->sb.rbytes;
	/*
	 * here ->wbytes is stable and ->rbytes can only be
	 * increased, so
	 * 	cc <= len
	 * and
	 * 	space = hiwat - cc <=> cc = hiwat - space
	 * so
	 * 	hiwat - space <= len <=>
	 * 	hiwat - len <= space
	 */
	return bytes <= (ssb->ssb_hiwat - len);
}

#define ssb_append(ssb, m)						\
	sb_append(&(ssb)->sb, m)

#define ssb_append_stream(ssb, m)					\
	sb_append_stream(&(ssb)->sb, m)

#define ssb_append_record(ssb, m)					\
	sb_append_record(&(ssb)->sb, m)

#define ssb_deq_record(ssb)						\
	sb_deq_record(&(ssb)->sb)

/* note: this function returns 1 on success, 0 on failure */
#define ssb_append_addr(ssb, src, m, control)				\
	sb_append_addr(&(ssb)->sb, src, m, control)

#define ssb_append_control(ssb, m, control)				\
	sb_append_control(&(ssb)->sb, m, control)

#define ssb_head(ssb)							\
	sb_head(&(ssb)->sb)

#define ssb_next_note(ssb)						\
	sb_next_note(&(ssb)->sb)

#define ssb_insert_knote(ssb, kn) {					\
        SLIST_INSERT_HEAD(&(ssb)->ssb_sel.si_note, kn, kn_selnext);	\
	(ssb)->ssb_flags |= SSB_KNOTE;					\
}

#define ssb_remove_knote(ssb, kn) {					\
        SLIST_REMOVE(&(ssb)->ssb_sel.si_note, kn, knote, kn_selnext);	\
	if (SLIST_EMPTY(&(ssb)->ssb_sel.si_note))			\
		(ssb)->ssb_flags &= ~SSB_KNOTE;				\
}

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
	    struct ucred *cred);
int	soo_poll (struct file *fp, int events, struct ucred *cred);
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

void	soabort (struct socket *so);
void	soaborta (struct socket *so);
void	soabort_oncpu (struct socket *so);
int	soaccept (struct socket *so, struct sockaddr **nam);
struct	socket *soalloc (int waitok);
int	sobind (struct socket *so, struct sockaddr *nam, struct thread *td);
void	socantrcvmore (struct socket *so);
void	socantsendmore (struct socket *so);
int	soclose (struct socket *so, int fflags);
int	soconnect (struct socket *so, struct sockaddr *nam, struct thread *td);
int	soconnect2 (struct socket *so1, struct socket *so2);
int	socreate (int dom, struct socket **aso, int type, int proto,
	    struct thread *td);
void	sodealloc (struct socket *so);
int	sodisconnect (struct socket *so);
void	sofree (struct socket *so);
int	sogetopt (struct socket *so, struct sockopt *sopt);
void	sohasoutofband (struct socket *so);
void	soisconnected (struct socket *so);
void	soisconnecting (struct socket *so);
void	soisdisconnected (struct socket *so);
void	soisdisconnecting (struct socket *so);
int	solisten (struct socket *so, int backlog, struct thread *td);
struct socket *sonewconn (struct socket *head, int connstatus);
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

int	sopoll (struct socket *so, int events, struct ucred *cred,
		    struct thread *td);
int	soreceive (struct socket *so, struct sockaddr **paddr,
		       struct uio *uio, struct sockbuf *sio,
		   int sio_climit,
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
