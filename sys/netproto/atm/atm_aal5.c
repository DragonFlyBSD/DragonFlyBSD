/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/sys/netatm/atm_aal5.c,v 1.6 1999/10/09 23:24:59 green Exp $
 *	@(#) $DragonFly: src/sys/netproto/atm/atm_aal5.c,v 1.13 2007/04/22 01:13:15 dillon Exp $
 */

/*
 * Core ATM Services
 * -----------------
 *
 * ATM AAL5 socket protocol processing
 *
 */

#include "kern_include.h"
#include <sys/stat.h>

/*
 * Global variables
 */
u_long		atm_aal5_sendspace = 64 * 1024;	/* XXX */
u_long		atm_aal5_recvspace = 64 * 1024;	/* XXX */


/*
 * Local functions
 */
static void	atm_aal5_abort(netmsg_t);
static void	atm_aal5_accept(netmsg_t);
static void	atm_aal5_attach(netmsg_t);
static void	atm_aal5_bind(netmsg_t);
static void	atm_aal5_connect(netmsg_t);
static void	atm_aal5_control(netmsg_t);
static void	atm_aal5_detach(netmsg_t);
static void	atm_aal5_listen(netmsg_t);
static void	atm_aal5_peeraddr(netmsg_t);
static void	atm_aal5_send(netmsg_t);
static void	atm_aal5_sense(netmsg_t);
static void	atm_aal5_disconnect(netmsg_t);
static void	atm_aal5_shutdown(netmsg_t);
static void	atm_aal5_sockaddr(netmsg_t);

static int	atm_aal5_incoming (void *, Atm_connection *,
			Atm_attributes *, void **);
static void	atm_aal5_cpcs_data (void *, KBuffer *);
static caddr_t	atm_aal5_getname (void *);


/*
 * New-style socket request routines
 */
struct pr_usrreqs	atm_aal5_usrreqs = {
	.pru_abort = atm_aal5_abort,
	.pru_accept = atm_aal5_accept,
	.pru_attach = atm_aal5_attach,
	.pru_bind = atm_aal5_bind,
	.pru_connect = atm_aal5_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = atm_aal5_control,
	.pru_detach = atm_aal5_detach,
	.pru_disconnect = atm_aal5_disconnect,
	.pru_listen = atm_aal5_listen,
	.pru_peeraddr = atm_aal5_peeraddr,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = atm_aal5_send,
	.pru_sense = atm_aal5_sense,
	.pru_shutdown = atm_aal5_shutdown,
	.pru_sockaddr = atm_aal5_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

/*
 * Local variables
 */
static Atm_endpoint	atm_aal5_endpt = {
	NULL,
	ENDPT_SOCK_AAL5,
	NULL,
	atm_aal5_getname,
	atm_sock_connected,
	atm_sock_cleared,
	atm_aal5_incoming,
	NULL,
	NULL,
	NULL,
	atm_aal5_cpcs_data,
	NULL,
	NULL,
	NULL,
	NULL
};

static Atm_attributes	atm_aal5_defattr = {
	NULL,			/* nif */
	CMAPI_CPCS,		/* api */
	0,			/* api_init */
	0,			/* headin */
	0,			/* headout */
	{			/* aal */
		T_ATM_PRESENT,
		ATM_AAL5
	},
	{			/* traffic */
		T_ATM_ABSENT,
	},
	{			/* bearer */
		T_ATM_ABSENT,
	},
	{			/* bhli */
		T_ATM_ABSENT
	},
	{			/* blli */
		T_ATM_ABSENT,
		T_ATM_ABSENT,
	},
	{			/* llc */
		T_ATM_ABSENT,
	},
	{			/* called */
		T_ATM_ABSENT,
		{
			T_ATM_ABSENT,
			0
		},
		{
			T_ATM_ABSENT,
			0
		}
	},
	{			/* calling */
		T_ATM_ABSENT
	},
	{			/* qos */
		T_ATM_ABSENT,
	},
	{			/* transit */
		T_ATM_ABSENT
	},
	{			/* cause */
		T_ATM_ABSENT
	}
};


/*
 * Handy common code macros
 */
#ifdef DIAGNOSTIC

#define ATM_INTRO(f)						\
    do {							\
	crit_enter();						\
	ATM_DEBUG2("aal5 socket %s (%p)\n", f, so);		\
	/*							\
	 * Stack queue should have been drained			\
	 */							\
	if (atm_stackq_head != NULL)				\
		panic("atm_aal5: stack queue not empty");	\
    } while(0)

#else /* !DIAGNOSTIC */

#define ATM_INTRO(f)						\
    do {							\
	crit_enter();						\
    } while(0)

#endif /* DIAGNOSTIC */

#define	ATM_OUTRO()						\
    out: do {							\
	/*							\
	 * Drain any deferred calls				\
	 */							\
	STACK_DRAIN();						\
	crit_exit();						\
	lwkt_replymsg(&msg->lmsg, error);			\
	return;							\
	goto out;						\
    } while(0)							\

/*
 * Abnormally terminate service
 *
 * Arguments:
 *	so	pointer to socket
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
atm_aal5_abort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	int error;

	ATM_INTRO("abort");
	so->so_error = ECONNABORTED;
	error = atm_sock_detach(so);
	ATM_OUTRO();
}

/*
 * Accept pending connection
 *
 * Arguments:
 *	so	pointer to socket
 *	addr	pointer to pointer to contain protocol address
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_accept(netmsg_t msg)
{
	struct socket *so = msg->accept.base.nm_so;
	int error;

	ATM_INTRO("accept");

	/*
	 * Everything is pretty much done already, we just need to
	 * return the caller's address to the user.
	 */
	error = atm_sock_peeraddr(so, msg->accept.nm_nam);
	ATM_OUTRO();
}

/*
 * Attach protocol to socket
 *
 * Arguments:
 *	so	pointer to socket
 *	proto	protocol identifier
 *	p	pointer to process
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	Atm_pcb *atp;
	int error;

	ATM_INTRO("attach");

	/*
	 * Do general attach stuff
	 */
	error = atm_sock_attach(so, atm_aal5_sendspace, atm_aal5_recvspace,
				ai->sb_rlimit);
	if (error)
		goto out;

	/*
	 * Finish up any protocol specific stuff
	 */
	atp = sotoatmpcb(so);
	atp->atp_type = ATPT_AAL5;

	/*
	 * Set default connection attributes
	 */
	atp->atp_attr = atm_aal5_defattr;
	strncpy(atp->atp_name, "(AAL5)", T_ATM_APP_NAME_LEN);

	ATM_OUTRO();
}

/*
 * Detach protocol from socket
 *
 * Arguments:
 *	so	pointer to socket
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	int error;

	ATM_INTRO("detach");

	error = atm_sock_detach(so);

	ATM_OUTRO();
}

/*
 * Bind address to socket
 *
 * Arguments:
 *	so	pointer to socket
 *	addr	pointer to protocol address
 *	p	pointer to process
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	int error;

	ATM_INTRO("bind");

	error = atm_sock_bind(so, msg->bind.nm_nam);

	ATM_OUTRO();
}

/*
 * Listen for incoming connections
 *
 * Arguments:
 *	so	pointer to socket
 *	p	pointer to process
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_listen(netmsg_t msg)
{
	struct socket *so = msg->listen.base.nm_so;
	int error;

	ATM_INTRO("listen");

	error = atm_sock_listen(so, &atm_aal5_endpt);

	ATM_OUTRO();
}


/*
 * Connect socket to peer
 *
 * Arguments:
 *	so	pointer to socket
 *	addr	pointer to protocol address
 *	p	pointer to process
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct thread *td = msg->connect.nm_td;
	Atm_pcb *atp;
	int error;

	ATM_INTRO("connect");

	atp = sotoatmpcb(so);

	/*
	 * Resize send socket buffer to maximum sdu size
	 */
	if (atp->atp_attr.aal.tag == T_ATM_PRESENT) {
		long size;

		size = atp->atp_attr.aal.v.aal5.forward_max_SDU_size;
		if (size != T_ATM_ABSENT) {
			if (!ssb_reserve(&so->so_snd, size, so,
				       &td->td_proc->p_rlimit[RLIMIT_SBSIZE])) {
				error = ENOBUFS;
				goto out;
			}
		}
	}

	/*
	 * Now get the socket connected
	 */
	error = atm_sock_connect(so, msg->connect.nm_nam, &atm_aal5_endpt);

	ATM_OUTRO();
}

/*
 * Disconnect connected socket
 *
 * Arguments:
 *	so	pointer to socket
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	int error;

	ATM_INTRO("disconnect");

	error = atm_sock_disconnect(so);

	ATM_OUTRO();
}


/*
 * Shut down socket data transmission
 *
 * Arguments:
 *	so	pointer to socket
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_shutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;
	int error;

	ATM_INTRO("shutdown");

	socantsendmore(so);
	error = 0;

	ATM_OUTRO();
}


/*
 * Send user data
 *
 * Arguments:
 *	so	pointer to socket
 *	flags	send data flags
 *	m	pointer to buffer containing user data
 *	addr	pointer to protocol address
 *	control	pointer to buffer containing protocol control data
 *	p	pointer to process
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	KBuffer	*m = msg->send.nm_m;
	KBuffer	*control = msg->send.nm_control;
	Atm_pcb *atp;
	int error;

	ATM_INTRO("send");

	/*
	 * We don't support any control functions
	 */
	if (control) {
		int clen;

		clen = KB_LEN(control);
		KB_FREEALL(control);
		if (clen) {
			KB_FREEALL(m);
			error = EINVAL;
			goto out;
		}
	}

	/*
	 * We also don't support any flags or send-level addressing
	 */
	if (msg->send.nm_flags || msg->send.nm_addr) {
		KB_FREEALL(m);
		error = EINVAL;
		goto out;
	}

	/*
	 * All we've got left is the data, so push it out
	 */
	atp = sotoatmpcb(so);
	error = atm_cm_cpcs_data(atp->atp_conn, m);
	if (error) {
		/*
		 * Output problem, drop packet
		 */
		atm_sock_stat.as_outdrop[atp->atp_type]++;
		KB_FREEALL(m);
	}

	ATM_OUTRO();
}


/*
 * Do control operation - ioctl system call
 *
 * Arguments:
 *	so	pointer to socket
 *	cmd	ioctl code
 *	data	pointer to code specific parameter data area
 *	ifp	pointer to ifnet structure if it's an interface ioctl
 *	p	pointer to process
 *
 * Returns:
 *	0 	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_control(netmsg_t msg)
{
	struct socket *so = msg->control.base.nm_so;
	int error;

	ATM_INTRO("control");

	switch (msg->control.nm_cmd) {
	default:
		error = EOPNOTSUPP;
		break;
	}

	ATM_OUTRO();
}

/*
 * Sense socket status - fstat system call
 *
 * Arguments:
 *	so	pointer to socket
 *	st	pointer to file status structure
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_sense(netmsg_t msg)
{
	struct socket *so = msg->sense.base.nm_so;
	struct stat *st = msg->sense.nm_stat;
	int error;

	ATM_INTRO("sense");

	/*
	 * Just return the max sdu size for the connection
	 */
	st->st_blksize = so->so_snd.ssb_hiwat;
	error = 0;

	ATM_OUTRO();
}


/*
 * Retrieve local socket address
 *
 * Arguments:
 *	so	pointer to socket
 *	addr	pointer to pointer to contain protocol address
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_sockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	int error;

	ATM_INTRO("sockaddr");

	error = atm_sock_sockaddr(so, msg->sockaddr.nm_nam);

	ATM_OUTRO();
}


/*
 * Retrieve peer socket address
 *
 * Arguments:
 *	so	pointer to socket
 *	addr	pointer to pointer to contain protocol address
 *
 * Returns:
 *	0	request processed
 *	error	error processing request - reason indicated
 *
 */
static void
atm_aal5_peeraddr(netmsg_t msg)
{
	struct socket *so = msg->peeraddr.base.nm_so;
	int error;

	ATM_INTRO("peeraddr");

	error = atm_sock_peeraddr(so, msg->peeraddr.nm_nam);

	ATM_OUTRO();
}

/*
 * Process getsockopt/setsockopt system calls
 *
 * Arguments:
 *	so	pointer to socket
 *	sopt	pointer to socket option info
 *
 * Returns:
 *	0 	request processed
 *	error	error processing request - reason indicated
 *
 */
void
atm_aal5_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->ctloutput.base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	Atm_pcb *atp;
	int error;

	ATM_INTRO("ctloutput");

	/*
	 * Make sure this is for us
	 */
	if (sopt->sopt_level != T_ATM_SIGNALING) {
		error = EINVAL;
		goto out;
	}
	atp = sotoatmpcb(so);
	if (atp == NULL) {
		error = ENOTCONN;
		goto out;
	}

	switch (sopt->sopt_dir) {
	case SOPT_SET:
		/*
		 * setsockopt()
		 */

		/*
		 * Validate socket state
		 */
		switch (sopt->sopt_name) {
		case T_ATM_ADD_LEAF:
		case T_ATM_DROP_LEAF:
			if ((so->so_state & SS_ISCONNECTED) == 0) {
				error = ENOTCONN;
				goto out;
			}
			break;
		case T_ATM_CAUSE:
		case T_ATM_APP_NAME:
			break;
		default:
			if (so->so_state & SS_ISCONNECTED) {
				error = EISCONN;
				goto out;
			}
			break;
		}

		/*
		 * Validate and save user-supplied option data
		 */
		error = atm_sock_setopt(so, sopt, atp);
		break;
	case SOPT_GET:
		/*
		 * getsockopt()
		 */

		/*
		 * Return option data
		 */
		error = atm_sock_getopt(so, sopt, atp);
		break;
	default:
		error = EINVAL;
		break;
	}

	ATM_OUTRO();
}

/*
 * Process Incoming Calls
 *
 * This function will receive control when an incoming call has been matched
 * to one of our registered listen parameter blocks.  Assuming the call passes
 * acceptance criteria and all required resources are available, we will
 * create a new protocol control block and socket association.  We must
 * then await notification of the final SVC setup results.  If any
 * problems are encountered, we will just tell the connection manager to
 * reject the call.
 *
 * Called from a critical section.
 *
 * Arguments:
 *	tok	owner's matched listening token
 *	cop	pointer to incoming call's connection block
 *	ap	pointer to incoming call's attributes
 *	tokp	pointer to location to store our connection token
 *
 * Returns:
 *	0	call is accepted
 *	error	call rejected - reason indicated
 *
 */
static int
atm_aal5_incoming(void *tok, Atm_connection *cop, Atm_attributes *ap,
		  void **tokp)
{
	Atm_pcb		*atp0 = tok, *atp;
	struct socket	*so;
	int		err = 0;

	/*
	 * Allocate a new socket and pcb for this connection.
	 *
	 * Note that our attach function will be called via sonewconn
	 * and it will allocate and setup most of the pcb.
	 */
	atm_sock_stat.as_inconn[atp0->atp_type]++;
	so = sonewconn(atp0->atp_socket, 0);

	if (so) {
		/*
		 * Finish pcb setup and pass pcb back to CM
		 */
		atp = sotoatmpcb(so);
		atp->atp_conn = cop;
		atp->atp_attr = *atp0->atp_conn->co_lattr;
		strncpy(atp->atp_name, atp0->atp_name, T_ATM_APP_NAME_LEN);
		*tokp = atp;
	} else {
		err = ECONNABORTED;
		atm_sock_stat.as_connfail[atp0->atp_type]++;
	}

	return (err);
}


/*
 * Process Socket VCC Input Data
 *
 * Arguments:
 *	tok	owner's connection token (atm_pcb)
 *	m	pointer to input packet buffer chain
 *
 * Returns:
 *	none
 *
 */
static void
atm_aal5_cpcs_data(void *tok, KBuffer *m)
{
	Atm_pcb		*atp = tok;
	struct socket	*so;
	int		len;

	so = atp->atp_socket;

	KB_PLENGET(m, len);

	/*
	 * Ensure that the socket is able to receive data and
	 * that there's room in the socket buffer
	 */
	if (((so->so_state & SS_ISCONNECTED) == 0) ||
	    (so->so_state & SS_CANTRCVMORE) ||
	    (len > ssb_space(&so->so_rcv))) {
		atm_sock_stat.as_indrop[atp->atp_type]++;
		KB_FREEALL(m);
		return;
	}

	/*
	 * Queue the data and notify the user
	 */
	ssb_appendrecord(&so->so_rcv, m);
	sorwakeup(so);

	return;
}

/*
 * Initialize AAL5 Sockets
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	none
 *
 */
void
atm_aal5_init(void)
{
	/*
	 * Register our endpoint
	 */
	if (atm_endpoint_register(&atm_aal5_endpt))
		panic("atm_aal5_init: register");

	/*
	 * Set default connection attributes
	 */
	atm_aal5_defattr.aal.v.aal5.forward_max_SDU_size = T_ATM_ABSENT;
	atm_aal5_defattr.aal.v.aal5.backward_max_SDU_size = T_ATM_ABSENT;
	atm_aal5_defattr.aal.v.aal5.SSCS_type = T_ATM_NULL;
}


/*
 * Get Connection's Application/Owner Name
 *
 * Arguments:
 *	tok	owner's connection token (atm_pcb)
 *
 * Returns:
 *	addr	pointer to string containing our name
 *
 */
static caddr_t
atm_aal5_getname(void *tok)
{
	Atm_pcb		*atp = tok;

	return (atp->atp_name);
}

