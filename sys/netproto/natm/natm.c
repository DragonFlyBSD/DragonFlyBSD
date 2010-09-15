/*	$NetBSD: natm.c,v 1.5 1996/11/09 03:26:26 chuck Exp $	*/
/* $FreeBSD: src/sys/netnatm/natm.c,v 1.12 2000/02/13 03:32:03 peter Exp $ */
/* $DragonFly: src/sys/netproto/natm/natm.c,v 1.31 2008/09/24 14:26:39 sephe Exp $ */

/*
 *
 * Copyright (c) 1996 Charles D. Cranor and Washington University.
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
 *      This product includes software developed by Charles D. Cranor and
 *      Washington University.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * natm.c: native mode ATM access (both aal0 and aal5).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sockio.h>
#include <sys/protosw.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/mplock2.h>

#include <net/if.h>
#include <net/if_atm.h>
#include <net/netisr.h>

#include <netinet/in.h>

#include "natm.h"

static u_long natm5_sendspace = 16*1024;
static u_long natm5_recvspace = 16*1024;

static u_long natm0_sendspace = 16*1024;
static u_long natm0_recvspace = 16*1024;

/*
 * user requests
 */
#ifdef FREEBSD_USRREQS

/*
 * FreeBSD new usrreqs supersedes pr_usrreq.
 */

static void
natm_usr_attach(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    struct pru_attach_info *ai = msg->attach.nm_ai;
    int proto = msg->attach.nm_proto;
    struct natmpcb *npcb;
    int error = 0;

    crit_enter();
    npcb = (struct natmpcb *) so->so_pcb;

    if (npcb) {
	error = EISCONN;
	goto out;
    }

    if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
	if (proto == PROTO_NATMAAL5) 
	    error = soreserve(so, natm5_sendspace, natm5_recvspace,
			      ai->sb_rlimit);
	else
	    error = soreserve(so, natm0_sendspace, natm0_recvspace,
			      ai->sb_rlimit);
        if (error)
          goto out;
    }

    so->so_pcb = (caddr_t) (npcb = npcb_alloc(M_WAITOK));
    npcb->npcb_socket = so;
 out:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);
}

static void
natm_usr_detach(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    struct natmpcb *npcb;
    int error = 0;

    crit_enter();
    npcb = (struct natmpcb *) so->so_pcb;
    if (npcb == NULL) {
	error = EINVAL;
	goto out;
    }

    /*
     * we turn on 'drain' *before* we sofree.
     */
    so->so_pcb = NULL;
    npcb_free(npcb, NPCB_DESTROY);	/* drain */
    sofree(so);				/* remove pcb ref */
 out:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);
}

static void
natm_usr_connect(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    struct sockaddr *nam = msg->connect.nm_nam;
    struct thread *td = msg->connect.nm_td;
    struct natmpcb *npcb;
    struct sockaddr_natm *snatm;
    struct atm_pseudoioctl api;
    struct ifnet *ifp;
    int error = 0;
    int proto;

    crit_enter();
    proto = so->so_proto->pr_protocol;
    npcb = (struct natmpcb *) so->so_pcb;
    if (npcb == NULL) {
	error = EINVAL;
	goto out;
    }

    /*
     * validate nam and npcb
     */

    snatm = (struct sockaddr_natm *)nam;
    if (snatm->snatm_len != sizeof(*snatm) ||
	(npcb->npcb_flags & NPCB_FREE) == 0) {
	error = EINVAL;
	goto out;
    }
    if (snatm->snatm_family != AF_NATM) {
	error = EAFNOSUPPORT;
	goto out;
    }

    snatm->snatm_if[IFNAMSIZ-1] = '\0';  /* XXX ensure null termination
					    since ifunit() uses strcmp */

    /*
     * convert interface string to ifp, validate.
     */

    ifp = ifunit(snatm->snatm_if);
    if (ifp == NULL || (ifp->if_flags & IFF_RUNNING) == 0) {
	error = ENXIO;
	goto out;
    }
    if (ifp->if_output != atm_output) {
	error = EAFNOSUPPORT;
	goto out;
    }

    /*
     * register us with the NATM PCB layer
     */

    if (npcb_add(npcb, ifp, snatm->snatm_vci, snatm->snatm_vpi) != npcb) {
        error = EADDRINUSE;
        goto out;
    }

    /*
     * enable rx
     */

    ATM_PH_FLAGS(&api.aph) = (proto == PROTO_NATMAAL5) ? ATM_PH_AAL5 : 0;
    ATM_PH_VPI(&api.aph) = npcb->npcb_vpi;
    ATM_PH_SETVCI(&api.aph, npcb->npcb_vci);
    api.rxhand = npcb;
    ifnet_serialize_all(ifp);
    if (ifp->if_ioctl == NULL || 
	ifp->if_ioctl(ifp, SIOCATMENA, (caddr_t) &api,
		      td->td_proc->p_ucred) != 0) {
	ifnet_deserialize_all(ifp);
	npcb_free(npcb, NPCB_REMOVE);
        error = EIO;
	goto out;
    }
    ifnet_deserialize_all(ifp);

    soisconnected(so);

 out:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);
}

static void
natm_usr_disconnect(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    struct natmpcb *npcb;
    struct atm_pseudoioctl api;
    struct ifnet *ifp;
    int error = 0;

    crit_enter();
    npcb = (struct natmpcb *) so->so_pcb;
    if (npcb == NULL) {
	error = EINVAL;
	goto out;
    }

    if ((npcb->npcb_flags & NPCB_CONNECTED) == 0) {
        kprintf("natm: disconnected check\n");
        error = EIO;
	goto out;
    }
    ifp = npcb->npcb_ifp;

    /*
     * disable rx
     */

    ATM_PH_FLAGS(&api.aph) = ATM_PH_AAL5;
    ATM_PH_VPI(&api.aph) = npcb->npcb_vpi;
    ATM_PH_SETVCI(&api.aph, npcb->npcb_vci);
    api.rxhand = npcb;
    if (ifp->if_ioctl != NULL) {
	ifnet_serialize_all(ifp);
	ifp->if_ioctl(ifp, SIOCATMDIS, (caddr_t) &api, NULL);
	ifnet_deserialize_all(ifp);
    }

    npcb_free(npcb, NPCB_REMOVE);
    soisdisconnected(so);

 out:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);
}

static void
natm_usr_shutdown(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;

    socantsendmore(so);

    lwkt_replymsg(&msg->lmsg, 0);
}

static void
natm_usr_send(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    struct mbuf *m = msg->send.nm_m;
    struct mbuf *control = msg->send.nm_control;
    struct natmpcb *npcb;
    struct atm_pseudohdr *aph;
    int error = 0;
    int proto = so->so_proto->pr_protocol;

    crit_enter();

    npcb = (struct natmpcb *) so->so_pcb;
    if (npcb == NULL) {
	error = EINVAL;
	goto out;
    }

    if (control && control->m_len) {
	m_freem(control);
	m_freem(m);
	error = EINVAL;
	goto out;
    }

    /*
     * send the data.   we must put an atm_pseudohdr on first
     */

    M_PREPEND(m, sizeof(*aph), M_WAITOK);
    if (m == NULL) {
        error = ENOBUFS;
	goto out;
    }
    aph = mtod(m, struct atm_pseudohdr *);
    ATM_PH_VPI(aph) = npcb->npcb_vpi;
    ATM_PH_SETVCI(aph, npcb->npcb_vci);
    ATM_PH_FLAGS(aph) = (proto == PROTO_NATMAAL5) ? ATM_PH_AAL5 : 0;

    error = atm_output(npcb->npcb_ifp, m, NULL, NULL);

 out:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);

}

static void
natm_usr_peeraddr(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    struct sockaddr **nam = msg->peeraddr.nm_nam;
    struct natmpcb *npcb;
    struct sockaddr_natm *snatm, ssnatm;
    int error = 0;

    crit_enter();
    npcb = (struct natmpcb *) so->so_pcb;
    if (npcb == NULL) {
	error = EINVAL;
	goto out;
    }

    snatm = &ssnatm;
    bzero(snatm, sizeof(*snatm));
    snatm->snatm_len = sizeof(*snatm);
    snatm->snatm_family = AF_NATM;
    strlcpy(snatm->snatm_if, npcb->npcb_ifp->if_xname,
        sizeof(snatm->snatm_if));
    snatm->snatm_vci = npcb->npcb_vci;
    snatm->snatm_vpi = npcb->npcb_vpi;
    *nam = dup_sockaddr((struct sockaddr *)snatm);

 out:
    crit_exit();
    lwkt_replymsg(&msg->lmsg, error);
}

static void
natm_usr_control(netmsg_t msg)
{
    struct socket *so = msg->base.nm_so;
    u_long cmd = msg->control.nm_cmd;
    caddr_t arg = msg->control.nm_data;
    struct thread *td = msg->control.nm_td;
    struct natmpcb *npcb;
    struct atm_rawioctl ario;
    int error = 0;

    npcb = (struct natmpcb *) so->so_pcb;
    if (npcb == NULL) {
	error = EINVAL;
	goto out;
    }

    /*
     * raw atm ioctl.   comes in as a SIOCRAWATM.   we convert it to
     * SIOCXRAWATM and pass it to the driver.
     */
    if (cmd == SIOCRAWATM) {
        if (npcb->npcb_ifp == NULL) {
	    error = ENOTCONN;
	    goto out;
        }
        ario.npcb = npcb;
        ario.rawvalue = *((int *)arg);
	ifnet_serialize_all(npcb->npcb_ifp);
        error = npcb->npcb_ifp->if_ioctl(npcb->npcb_ifp, 
					 SIOCXRAWATM, (caddr_t) &ario,
					 td->td_proc->p_ucred);
	ifnet_deserialize_all(npcb->npcb_ifp);
	if (!error) {
	    if (ario.rawvalue) 
		npcb->npcb_flags |= NPCB_RAW;
	    else
		npcb->npcb_flags &= ~(NPCB_RAW);
	}
    } else {
	error = EOPNOTSUPP;
    }
out:
    lwkt_replymsg(&msg->lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
natm_usr_abort(netmsg_t msg)
{
    natm_usr_shutdown(msg);
    /* msg now invalid */
}

/* xxx - should be const */
struct pr_usrreqs natm_usrreqs = {
	.pru_abort = natm_usr_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = natm_usr_attach,
	.pru_bind = pr_generic_notsupp,
	.pru_connect = natm_usr_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = natm_usr_control,
	.pru_detach = natm_usr_detach,
	.pru_disconnect = natm_usr_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = natm_usr_peeraddr,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = natm_usr_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = natm_usr_shutdown,
	.pru_sockaddr = pr_generic_notsupp,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

#else  /* !FREEBSD_USRREQS */

#if defined(__NetBSD__) || defined(__OpenBSD__)
int
natm_usrreq(struct socket *so, int req, struct mbuf *m, struct mbuf *nam,
	    struct mbuf *control, struct proc *p)
#elif defined(__DragonFly__)
int
natm_usrreq(struct socket *so, int req, struct mbuf *m, struct mbuf *nam,
	    struct mbuf *control)
#endif
{
  int error = 0;
  struct natmpcb *npcb;
  struct sockaddr_natm *snatm;
  struct atm_pseudoioctl api;
  struct atm_pseudohdr *aph;
  struct atm_rawioctl ario;
  struct ifnet *ifp;
  int proto = so->so_proto->pr_protocol;

  crit_enter();

  npcb = (struct natmpcb *) so->so_pcb;

  if (npcb == NULL && req != PRU_ATTACH) {
    error = EINVAL;
    goto done;
  }
    

  switch (req) {
    case PRU_ATTACH:			/* attach protocol to up */

      if (npcb) {
	error = EISCONN;
	break;
      }

      if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
	if (proto == PROTO_NATMAAL5) 
          error = soreserve(so, natm5_sendspace, natm5_recvspace);
	else
          error = soreserve(so, natm0_sendspace, natm0_recvspace);
        if (error)
          break;
      }

      so->so_pcb = (caddr_t) (npcb = npcb_alloc(M_WAITOK));
      npcb->npcb_socket = so;

      break;

    case PRU_DETACH:			/* detach protocol from up */

      /*
       * we turn on 'drain' *before* we sofree.
       */

      so->so_pcb = NULL;
      npcb_free(npcb, NPCB_DESTROY);	/* drain */
      sofree(so);			/* remove pcb ref */

      break;

    case PRU_CONNECT:			/* establish connection to peer */

      /*
       * validate nam and npcb
       */

      if (nam->m_len != sizeof(*snatm)) {
        error = EINVAL;
	break;
      }
      snatm = mtod(nam, struct sockaddr_natm *);
      if (snatm->snatm_len != sizeof(*snatm) ||
		(npcb->npcb_flags & NPCB_FREE) == 0) {
	error = EINVAL;
	break;
      }
      if (snatm->snatm_family != AF_NATM) {
	error = EAFNOSUPPORT;
	break;
      }

      snatm->snatm_if[IFNAMSIZ-1] = '\0';  /* XXX ensure null termination
						since ifunit() uses strcmp */

      /*
       * convert interface string to ifp, validate.
       */

      ifp = ifunit(snatm->snatm_if);
      if (ifp == NULL || (ifp->if_flags & IFF_RUNNING) == 0) {
	error = ENXIO;
	break;
      }
      if (ifp->if_output != atm_output) {
	error = EAFNOSUPPORT;
	break;
      }


      /*
       * register us with the NATM PCB layer
       */

      if (npcb_add(npcb, ifp, snatm->snatm_vci, snatm->snatm_vpi) != npcb) {
        error = EADDRINUSE;
        break;
      }

      /*
       * enable rx
       */

      ATM_PH_FLAGS(&api.aph) = (proto == PROTO_NATMAAL5) ? ATM_PH_AAL5 : 0;
      ATM_PH_VPI(&api.aph) = npcb->npcb_vpi;
      ATM_PH_SETVCI(&api.aph, npcb->npcb_vci);
      api.rxhand = npcb;
      ifnet_serialize_all(ifp);
      if (ifp->if_ioctl == NULL || 
	  ifp->if_ioctl(ifp, SIOCATMENA, (caddr_t) &api, NULL) != 0) {
	ifnet_deserialize_all(ifp);
	npcb_free(npcb, NPCB_REMOVE);
        error = EIO;
	break;
      }
      ifnet_deserialize_all(ifp);

      soisconnected(so);

      break;

    case PRU_DISCONNECT:		/* disconnect from peer */

      if ((npcb->npcb_flags & NPCB_CONNECTED) == 0) {
        kprintf("natm: disconnected check\n");
        error = EIO;
	break;
      }
      ifp = npcb->npcb_ifp;

      /*
       * disable rx
       */

      ATM_PH_FLAGS(&api.aph) = ATM_PH_AAL5;
      ATM_PH_VPI(&api.aph) = npcb->npcb_vpi;
      ATM_PH_SETVCI(&api.aph, npcb->npcb_vci);
      api.rxhand = npcb;
      ifnet_serialize_all(ifp);
      if (ifp->if_ioctl != NULL)
	  ifp->if_ioctl(ifp, SIOCATMDIS, (caddr_t) &api, NULL);
      ifnet_deserialize_all(ifp);

      npcb_free(npcb, NPCB_REMOVE);
      soisdisconnected(so);

      break;

    case PRU_SHUTDOWN:			/* won't send any more data */
      socantsendmore(so);
      break;

    case PRU_SEND:			/* send this data */
      if (control && control->m_len) {
	m_freem(control);
	m_freem(m);
	error = EINVAL;
	break;
      }

      /*
       * send the data.   we must put an atm_pseudohdr on first
       */

      M_PREPEND(m, sizeof(*aph), M_WAITOK);
      if (m == NULL) {
        error = ENOBUFS;
	break;
      }
      aph = mtod(m, struct atm_pseudohdr *);
      ATM_PH_VPI(aph) = npcb->npcb_vpi;
      ATM_PH_SETVCI(aph, npcb->npcb_vci);
      ATM_PH_FLAGS(aph) = (proto == PROTO_NATMAAL5) ? ATM_PH_AAL5 : 0;

      error = atm_output(npcb->npcb_ifp, m, NULL, NULL);

      break;

    case PRU_SENSE:			/* return status into m */
      /* return zero? */
      break;

    case PRU_PEERADDR:			/* fetch peer's address */
      snatm = mtod(nam, struct sockaddr_natm *);
      bzero(snatm, sizeof(*snatm));
      nam->m_len = snatm->snatm_len = sizeof(*snatm);
      snatm->snatm_family = AF_NATM;
#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
      bcopy(npcb->npcb_ifp->if_xname, snatm->snatm_if, sizeof(snatm->snatm_if));
#elif defined(__DragonFly__)
      ksnprintf(snatm->snatm_if, sizeof(snatm->snatm_if),
	"%s%d", npcb->npcb_ifp->if_name, npcb->npcb_ifp->if_unit);
#endif
      snatm->snatm_vci = npcb->npcb_vci;
      snatm->snatm_vpi = npcb->npcb_vpi;
      break;

    case PRU_CONTROL:			/* control operations on protocol */
      /*
       * raw atm ioctl.   comes in as a SIOCRAWATM.   we convert it to
       * SIOCXRAWATM and pass it to the driver.
       */
      if ((u_long)m == SIOCRAWATM) {
        if (npcb->npcb_ifp == NULL) {
          error = ENOTCONN;
          break;
        }
        ario.npcb = npcb;
        ario.rawvalue = *((int *)nam);
	ifnet_serialize_all(npcb->npcb_ifp);
        error = npcb->npcb_ifp->if_ioctl(npcb->npcb_ifp, SIOCXRAWATM,
					 (caddr_t) &ario, NULL);
	ifnet_deserialize_all(npcb->npcb_ifp);
	if (!error) {
          if (ario.rawvalue) 
	    npcb->npcb_flags |= NPCB_RAW;
	  else
	    npcb->npcb_flags &= ~(NPCB_RAW);
	}

        break;
      }

      error = EOPNOTSUPP;
      break;

    case PRU_BIND:			/* bind socket to address */
    case PRU_LISTEN:			/* listen for connection */
    case PRU_ACCEPT:			/* accept connection from peer */
    case PRU_CONNECT2:			/* connect two sockets */
    case PRU_ABORT:			/* abort (fast DISCONNECT, DETATCH) */
					/* (only happens if LISTEN socket) */
    case PRU_RCVD:			/* have taken data; more room now */
    case PRU_FASTTIMO:			/* 200ms timeout */
    case PRU_SLOWTIMO:			/* 500ms timeout */
    case PRU_RCVOOB:			/* retrieve out of band data */
    case PRU_SENDOOB:			/* send out of band data */
    case PRU_PROTORCV:			/* receive from below */
    case PRU_PROTOSEND:			/* send to below */
    case PRU_SOCKADDR:			/* fetch socket's address */
#ifdef DIAGNOSTIC
      kprintf("natm: PRU #%d unsupported\n", req);
#endif
      error = EOPNOTSUPP;
      break;
   
    default: panic("natm usrreq");
  }

done:
  crit_exit();
  return(error);
}

#endif  /* !FREEBSD_USRREQS */

/* 
 * natm0_sysctl: not used, but here in case we want to add something
 * later...
 */

int
natm0_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
	     void *newp, size_t newlen)
{
  /* All sysctl names at this level are terminal. */
  if (namelen != 1)
    return (ENOTDIR);
  return (ENOPROTOOPT);
}

/* 
 * natm5_sysctl: not used, but here in case we want to add something
 * later...
 */

int
natm5_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
	     void *newp, size_t newlen)
{
  /* All sysctl names at this level are terminal. */
  if (namelen != 1)
    return (ENOTDIR);
  return (ENOPROTOOPT);
}

static void natmintr(netmsg_t);

#if defined(__DragonFly__)
static void
netisr_natm_setup(void *dummy __unused)
{
	netisr_register(NETISR_NATM, natmintr, NULL);
}
SYSINIT(natm_setup, SI_BOOT2_KLD, SI_ORDER_ANY, netisr_natm_setup, NULL);
#endif

void
natm_init(void)
{
  LIST_INIT(&natm_pcbs);

  netisr_register(NETISR_NATM, natmintr, NULL);
}

/*
 * natmintr: software interrupt
 *
 * note: we expect a socket pointer in rcvif rather than an interface
 * pointer.    we can get the interface pointer from the so's PCB if
 * we really need it.
 */
static void
natmintr(netmsg_t msg)
{
  struct mbuf *m = msg->packet.nm_packet;
  struct socket *so;
  struct natmpcb *npcb;

#ifdef DIAGNOSTIC
  if ((m->m_flags & M_PKTHDR) == 0)
    panic("natmintr no HDR");
#endif

  get_mplock();

  npcb = (struct natmpcb *) m->m_pkthdr.rcvif; /* XXX: overloaded */
  so = npcb->npcb_socket;

  crit_enter();
  npcb->npcb_inq--;
  crit_exit();

  if (npcb->npcb_flags & NPCB_DRAIN) {
    m_freem(m);
    if (npcb->npcb_inq == 0)
      FREE(npcb, M_PCB);			/* done! */
    goto out;
  }

  if (npcb->npcb_flags & NPCB_FREE) {
    m_freem(m);					/* drop */
    goto out;
  }

#ifdef NEED_TO_RESTORE_IFP
  m->m_pkthdr.rcvif = npcb->npcb_ifp;
#else
#ifdef DIAGNOSTIC
m->m_pkthdr.rcvif = NULL;	/* null it out to be safe */
#endif
#endif

  if (ssb_space(&so->so_rcv) > m->m_pkthdr.len ||
     ((npcb->npcb_flags & NPCB_RAW) != 0 && so->so_rcv.ssb_cc < NPCB_RAWCC) ) {
#ifdef NATM_STAT
    natm_sookcnt++;
    natm_sookbytes += m->m_pkthdr.len;
#endif
    sbappendrecord(&so->so_rcv.sb, m);
    sorwakeup(so);
  } else {
#ifdef NATM_STAT
    natm_sodropcnt++;
    natm_sodropbytes += m->m_pkthdr.len;
#endif
    m_freem(m);
  }
out:
  rel_mplock();
  /* msg was embedded in the mbuf, do not reply! */
}

