/*
 * Copyright (c) 1990,1994 Regents of The University of Michigan.
 * All Rights Reserved.  See COPYRIGHT.
 *
 * $DragonFly: src/sys/netproto/atalk/ddp_usrreq.c,v 1.14 2008/09/24 14:26:39 sephe Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/mplock2.h>
#include <sys/msgport2.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>

#include "at.h"
#include "at_var.h"
#include "ddp_var.h"
#include "at_extern.h"

static void at_pcbdisconnect( struct ddpcb *ddp );
static void at_sockaddr(struct ddpcb *ddp, struct sockaddr **addr);
static int at_pcbsetaddr(struct ddpcb *ddp, struct sockaddr *addr,
			  struct thread *td);
static int at_pcbconnect(struct ddpcb *ddp, struct sockaddr *addr, 
			 struct thread *td);
static void at_pcbdetach(struct socket *so, struct ddpcb *ddp);
static int at_pcballoc(struct socket *so);

struct ddpcb	*ddp_ports[ ATPORT_LAST ];
struct ddpcb	*ddpcb = NULL;
static u_long	ddp_sendspace = DDP_MAXSZ; /* Max ddp size + 1 (ddp_type) */
static u_long	ddp_recvspace = 10 * ( 587 + sizeof( struct sockaddr_at ));

static void
ddp_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct ddpcb	*ddp;
	int error;

	ddp = sotoddpcb(so);
	if (ddp != NULL) {
		error = EINVAL;
		goto out;
	}

	error = at_pcballoc(so);
	if (error == 0) {
		error = soreserve(so, ddp_sendspace, ddp_recvspace,
				  ai->sb_rlimit);
	}
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
ddp_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct ddpcb	*ddp;
	int error;
	
	ddp = sotoddpcb(so);
	if (ddp == NULL) {
		error = EINVAL;
	} else {
		at_pcbdetach(so, ddp);
		error = 0;
	}
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

static void
ddp_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct ddpcb *ddp;
	int error;
	
	ddp = sotoddpcb(so);
	if (ddp) {
		error = at_pcbsetaddr(ddp, msg->bind.nm_nam, msg->bind.nm_td);
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}
    
static void
ddp_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct ddpcb *ddp;
	int error;
	
	ddp = sotoddpcb(so);
	if (ddp == NULL) {
		error = EINVAL;
	} else if (ddp->ddp_fsat.sat_port != ATADDR_ANYPORT ) {
		error = EISCONN;
	} else {
		error = at_pcbconnect(ddp, msg->connect.nm_nam,
				      msg->connect.nm_td);
		if (error == 0)
			soisconnected(so);
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
ddp_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct ddpcb *ddp;
	int error;
	
	ddp = sotoddpcb(so);
	if (ddp == NULL) {
		error = EINVAL;
	} else if (ddp->ddp_fsat.sat_addr.s_node == ATADDR_ANYNODE) {
		error = ENOTCONN;
	} else {
		soreference(so);
		at_pcbdisconnect(ddp);
		ddp->ddp_fsat.sat_addr.s_node = ATADDR_ANYNODE;
		soisdisconnected(so);
		sofree(so);		/* soref above */
		error = 0;
	}
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
ddp_shutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;
	struct ddpcb	*ddp;
	int error;

	ddp = sotoddpcb(so);
	if (ddp) {
		socantsendmore(so);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->shutdown.base.lmsg, error);
}

static void
ddp_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *addr = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	struct ddpcb *ddp;
	int error;
	
	ddp = sotoddpcb(so);
	if (ddp == NULL) {
		error = EINVAL;
		goto out;
	}

	if (control && control->m_len) {
		error = EINVAL;
		goto out;
    	}

	if (addr) {
		if (ddp->ddp_fsat.sat_port != ATADDR_ANYPORT) {
			error = EISCONN;
			goto out;
		}

		error = at_pcbconnect(ddp, addr, msg->send.nm_td);
		if (error)
			goto out;
	} else {
		if (ddp->ddp_fsat.sat_port == ATADDR_ANYPORT) {
			error = ENOTCONN;
			goto out;
		}
	}

	error = ddp_output(m, so);
	m = NULL;
	if (addr) {
		at_pcbdisconnect(ddp);
	}
out:
	if (m)
		m_freem(m);
	if (control)
		m_freem(control);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
ddp_abort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct ddpcb *ddp;
	int error;
	
	ddp = sotoddpcb(so);
	if (ddp) {
		soisdisconnected( so );
		at_pcbdetach( so, ddp );
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->abort.base.lmsg, error);
}

static void
ddp_setpeeraddr(netmsg_t msg)
{
	lwkt_replymsg(&msg->peeraddr.base.lmsg, EOPNOTSUPP);
}

static void
ddp_setsockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **nam = msg->sockaddr.nm_nam;
	struct ddpcb	*ddp;
	int error;

	ddp = sotoddpcb(so);
	if (ddp) {
		at_sockaddr(ddp, nam);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
}

static void
ddp_control(netmsg_t msg)
{
	struct socket *so = msg->control.base.nm_so;
	int error;

	error = at_control(so, msg->control.nm_cmd,
			   msg->control.nm_data,
			   msg->control.nm_ifp,
			   msg->control.nm_td);
	lwkt_replymsg(&msg->control.base.lmsg, error);
}


static void
at_sockaddr(struct ddpcb *ddp, struct sockaddr **addr)
{
    *addr = dup_sockaddr((struct sockaddr *)&ddp->ddp_lsat);
}

static int 
at_pcbsetaddr(struct ddpcb *ddp, struct sockaddr *addr, struct thread *td)
{
    struct sockaddr_at	lsat, *sat;
    struct at_ifaddr	*aa;
    struct ddpcb	*ddpp;

    if ( ddp->ddp_lsat.sat_port != ATADDR_ANYPORT ) { /* shouldn't be bound */
	return( EINVAL );
    }

    if (addr != 0) {			/* validate passed address */
	sat = (struct sockaddr_at *)addr;
	if (sat->sat_family != AF_APPLETALK) {
	    return(EAFNOSUPPORT);
	}

	if ( sat->sat_addr.s_node != ATADDR_ANYNODE ||
		sat->sat_addr.s_net != ATADDR_ANYNET ) {
	    for ( aa = at_ifaddr; aa; aa = aa->aa_next ) {
		if (( sat->sat_addr.s_net == AA_SAT( aa )->sat_addr.s_net ) &&
		 ( sat->sat_addr.s_node == AA_SAT( aa )->sat_addr.s_node )) {
		    break;
		}
	    }
	    if ( !aa ) {
		return( EADDRNOTAVAIL );
	    }
	}

	if ( sat->sat_port != ATADDR_ANYPORT ) {
	    if ( sat->sat_port < ATPORT_FIRST ||
		    sat->sat_port >= ATPORT_LAST ) {
		return( EINVAL );
	    }
	    if ( sat->sat_port < ATPORT_RESERVED &&
		 priv_check(td, PRIV_ROOT) ) {
		return( EACCES );
	    }
	}
    } else {
	bzero( (caddr_t)&lsat, sizeof( struct sockaddr_at ));
	lsat.sat_len = sizeof(struct sockaddr_at);
	lsat.sat_addr.s_node = ATADDR_ANYNODE;
	lsat.sat_addr.s_net = ATADDR_ANYNET;
	lsat.sat_family = AF_APPLETALK;
	sat = &lsat;
    }

    if ( sat->sat_addr.s_node == ATADDR_ANYNODE &&
	    sat->sat_addr.s_net == ATADDR_ANYNET ) {
	if ( at_ifaddr == NULL ) {
	    return( EADDRNOTAVAIL );
	}
	sat->sat_addr = AA_SAT( at_ifaddr )->sat_addr;
    }
    ddp->ddp_lsat = *sat;

    /*
     * Choose port.
     */
    if ( sat->sat_port == ATADDR_ANYPORT ) {
	for ( sat->sat_port = ATPORT_RESERVED;
		sat->sat_port < ATPORT_LAST; sat->sat_port++ ) {
	    if ( ddp_ports[ sat->sat_port - 1 ] == 0 ) {
		break;
	    }
	}
	if ( sat->sat_port == ATPORT_LAST ) {
	    return( EADDRNOTAVAIL );
	}
	ddp->ddp_lsat.sat_port = sat->sat_port;
	ddp_ports[ sat->sat_port - 1 ] = ddp;
    } else {
	for ( ddpp = ddp_ports[ sat->sat_port - 1 ]; ddpp;
		ddpp = ddpp->ddp_pnext ) {
	    if ( ddpp->ddp_lsat.sat_addr.s_net == sat->sat_addr.s_net &&
		    ddpp->ddp_lsat.sat_addr.s_node == sat->sat_addr.s_node ) {
		break;
	    }
	}
	if ( ddpp != NULL ) {
	    return( EADDRINUSE );
	}
	ddp->ddp_pnext = ddp_ports[ sat->sat_port - 1 ];
	ddp_ports[ sat->sat_port - 1 ] = ddp;
	if ( ddp->ddp_pnext ) {
	    ddp->ddp_pnext->ddp_pprev = ddp;
	}
    }

    return( 0 );
}

static int
at_pcbconnect(struct ddpcb *ddp, struct sockaddr *addr, struct thread *td)
{
    struct sockaddr_at	*sat = (struct sockaddr_at *)addr;
    struct route	*ro;
    struct at_ifaddr	*aa = 0;
    struct ifnet	*ifp;
    u_short		hintnet = 0, net;

    if (sat->sat_family != AF_APPLETALK) {
	return(EAFNOSUPPORT);
    }

    /*
     * Under phase 2, network 0 means "the network".  We take "the
     * network" to mean the network the control block is bound to.
     * If the control block is not bound, there is an error.
     */
    if ( sat->sat_addr.s_net == ATADDR_ANYNET
		&& sat->sat_addr.s_node != ATADDR_ANYNODE ) {
	if ( ddp->ddp_lsat.sat_port == ATADDR_ANYPORT ) {
	    return( EADDRNOTAVAIL );
	}
	hintnet = ddp->ddp_lsat.sat_addr.s_net;
    }

    ro = &ddp->ddp_route;
    /*
     * If we've got an old route for this pcb, check that it is valid.
     * If we've changed our address, we may have an old "good looking"
     * route here.  Attempt to detect it.
     */
    if ( ro->ro_rt ) {
	if ( hintnet ) {
	    net = hintnet;
	} else {
	    net = sat->sat_addr.s_net;
	}
	aa = 0;
	if ((ifp = ro->ro_rt->rt_ifp) != NULL) {
	    for ( aa = at_ifaddr; aa; aa = aa->aa_next ) {
		if ( aa->aa_ifp == ifp &&
			ntohs( net ) >= ntohs( aa->aa_firstnet ) &&
			ntohs( net ) <= ntohs( aa->aa_lastnet )) {
		    break;
		}
	    }
	}
	if ( aa == NULL || ( satosat( &ro->ro_dst )->sat_addr.s_net !=
		( hintnet ? hintnet : sat->sat_addr.s_net ) ||
		satosat( &ro->ro_dst )->sat_addr.s_node !=
		sat->sat_addr.s_node )) {
	    RTFREE( ro->ro_rt );
	    ro->ro_rt = NULL;
	}
    }

    /*
     * If we've got no route for this interface, try to find one.
     */
    if ( ro->ro_rt == NULL ||
	 ro->ro_rt->rt_ifp == NULL ) {
	ro->ro_dst.sa_len = sizeof( struct sockaddr_at );
	ro->ro_dst.sa_family = AF_APPLETALK;
	if ( hintnet ) {
	    satosat( &ro->ro_dst )->sat_addr.s_net = hintnet;
	} else {
	    satosat( &ro->ro_dst )->sat_addr.s_net = sat->sat_addr.s_net;
	}
	satosat( &ro->ro_dst )->sat_addr.s_node = sat->sat_addr.s_node;
	rtalloc( ro );
    }

    /*
     * Make sure any route that we have has a valid interface.
     */
    aa = 0;
    if ( ro->ro_rt && ( ifp = ro->ro_rt->rt_ifp )) {
	for ( aa = at_ifaddr; aa; aa = aa->aa_next ) {
	    if ( aa->aa_ifp == ifp ) {
		break;
	    }
	}
    }
    if ( aa == 0 ) {
	return( ENETUNREACH );
    }

    ddp->ddp_fsat = *sat;
    if ( ddp->ddp_lsat.sat_port == ATADDR_ANYPORT ) {
	return(at_pcbsetaddr(ddp, NULL, td));
    }
    return( 0 );
}

static void 
at_pcbdisconnect( struct ddpcb	*ddp )
{
    ddp->ddp_fsat.sat_addr.s_net = ATADDR_ANYNET;
    ddp->ddp_fsat.sat_addr.s_node = ATADDR_ANYNODE;
    ddp->ddp_fsat.sat_port = ATADDR_ANYPORT;
}

static int
at_pcballoc( struct socket *so )
{
	struct ddpcb	*ddp;

	MALLOC(ddp, struct ddpcb *, sizeof *ddp, M_PCB, M_WAITOK | M_ZERO);
	ddp->ddp_lsat.sat_port = ATADDR_ANYPORT;

	ddp->ddp_next = ddpcb;
	ddp->ddp_prev = NULL;
	ddp->ddp_pprev = NULL;
	ddp->ddp_pnext = NULL;
	if (ddpcb) {
		ddpcb->ddp_prev = ddp;
	}
	ddpcb = ddp;

	ddp->ddp_socket = so;
	so->so_pcb = (caddr_t)ddp;
	return(0);
}

static void
at_pcbdetach( struct socket *so, struct ddpcb *ddp)
{
    soisdisconnected(so);
    so->so_pcb = NULL;
    sofree(so);

    /* remove ddp from ddp_ports list */
    if ( ddp->ddp_lsat.sat_port != ATADDR_ANYPORT &&
	    ddp_ports[ ddp->ddp_lsat.sat_port - 1 ] != NULL ) {
	if ( ddp->ddp_pprev != NULL ) {
	    ddp->ddp_pprev->ddp_pnext = ddp->ddp_pnext;
	} else {
	    ddp_ports[ ddp->ddp_lsat.sat_port - 1 ] = ddp->ddp_pnext;
	}
	if ( ddp->ddp_pnext != NULL ) {
	    ddp->ddp_pnext->ddp_pprev = ddp->ddp_pprev;
	}
    }

    if ( ddp->ddp_route.ro_rt ) {
	rtfree( ddp->ddp_route.ro_rt );
    }

    if ( ddp->ddp_prev ) {
	ddp->ddp_prev->ddp_next = ddp->ddp_next;
    } else {
	ddpcb = ddp->ddp_next;
    }
    if ( ddp->ddp_next ) {
	ddp->ddp_next->ddp_prev = ddp->ddp_prev;
    }
    FREE(ddp, M_PCB);
}

/*
 * For the moment, this just find the pcb with the correct local address.
 * In the future, this will actually do some real searching, so we can use
 * the sender's address to do de-multiplexing on a single port to many
 * sockets (pcbs).
 */
struct ddpcb *
ddp_search( struct sockaddr_at *from, struct sockaddr_at *to,
			struct at_ifaddr *aa)
{
    struct ddpcb	*ddp;

    /*
     * Check for bad ports.
     */
    if ( to->sat_port < ATPORT_FIRST || to->sat_port >= ATPORT_LAST ) {
	return( NULL );
    }

    /*
     * Make sure the local address matches the sent address.  What about
     * the interface?
     */
    for ( ddp = ddp_ports[ to->sat_port - 1 ]; ddp; ddp = ddp->ddp_pnext ) {
	/* XXX should we handle 0.YY? */

	/* XXXX.YY to socket on destination interface */
	if ( to->sat_addr.s_net == ddp->ddp_lsat.sat_addr.s_net &&
		to->sat_addr.s_node == ddp->ddp_lsat.sat_addr.s_node ) {
	    break;
	}

	/* 0.255 to socket on receiving interface */
	if ( to->sat_addr.s_node == ATADDR_BCAST && ( to->sat_addr.s_net == 0 ||
		to->sat_addr.s_net == ddp->ddp_lsat.sat_addr.s_net ) &&
		ddp->ddp_lsat.sat_addr.s_net == AA_SAT( aa )->sat_addr.s_net ) {
	    break;
	}

	/* XXXX.0 to socket on destination interface */
	if ( to->sat_addr.s_net == aa->aa_firstnet &&
		to->sat_addr.s_node == 0 &&
		ntohs( ddp->ddp_lsat.sat_addr.s_net ) >=
		ntohs( aa->aa_firstnet ) &&
		ntohs( ddp->ddp_lsat.sat_addr.s_net ) <=
		ntohs( aa->aa_lastnet )) {
	    break;
	}
    }
    return( ddp );
}

void 
ddp_init(void)
{
	netisr_register(NETISR_ATALK1, at1intr, NULL);
	netisr_register(NETISR_ATALK2, at2intr, NULL);
	netisr_register(NETISR_AARP, aarpintr, NULL);
}

#if 0
static void 
ddp_clean(void)
{
    struct ddpcb	*ddp;

    for ( ddp = ddpcb; ddp; ddp = ddp->ddp_next ) {
	at_pcbdetach( ddp->ddp_socket, ddp );
    }
}
#endif

struct pr_usrreqs ddp_usrreqs = {
	.pru_abort = ddp_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = ddp_attach,
	.pru_bind = ddp_bind,
	.pru_connect = ddp_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = ddp_control,
	.pru_detach = ddp_detach,
	.pru_disconnect = ddp_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = ddp_setpeeraddr,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = ddp_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = ddp_shutdown,
	.pru_sockaddr = ddp_setsockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};
