/*
 * Copyright (C) 1999-2000 by Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * BASED ON:
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 */

/*
 * $FreeBSD: src/sys/net/if_tap.c,v 1.3.2.3 2002/04/14 21:41:48 luigi Exp $
 * $DragonFly: src/sys/net/tap/if_tap.c,v 1.28 2006/09/05 00:55:48 dillon Exp $
 * $Id: if_tap.c,v 0.21 2000/07/23 21:46:02 max Exp $
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/thread2.h>
#include <sys/ttycom.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/serialize.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/route.h>

#include <netinet/in.h>

#include "if_tapvar.h"
#include "if_tap.h"


#define CDEV_NAME	"tap"
#define CDEV_MAJOR	149
#define TAPDEBUG	if (tapdebug) if_printf

#define TAP		"tap"
#define VMNET		"vmnet"
#define VMNET_DEV_MASK	0x00010000

/* module */
static int 		tapmodevent	(module_t, int, void *);

/* device */
static void		tapcreate	(dev_t);

/* network interface */
static void		tapifstart	(struct ifnet *);
static int		tapifioctl	(struct ifnet *, u_long, caddr_t,
					 struct ucred *);
static void		tapifinit	(void *);

/* character device */
static d_open_t		tapopen;
static d_close_t	tapclose;
static d_read_t		tapread;
static d_write_t	tapwrite;
static d_ioctl_t	tapioctl;
static d_poll_t		tappoll;

static struct dev_ops	tap_ops = {
	{ CDEV_NAME, CDEV_MAJOR, 0 },
	.d_open =	tapopen,
	.d_close =	tapclose,
	.d_read =	tapread,
	.d_write =	tapwrite,
	.d_ioctl =	tapioctl,
	.d_poll =	tappoll,
};

static int		taprefcnt = 0;		/* module ref. counter   */
static int		taplastunit = -1;	/* max. open unit number */
static int		tapdebug = 0;		/* debug flag            */

MALLOC_DECLARE(M_TAP);
MALLOC_DEFINE(M_TAP, CDEV_NAME, "Ethernet tunnel interface");
SYSCTL_INT(_debug, OID_AUTO, if_tap_debug, CTLFLAG_RW, &tapdebug, 0, "");
DEV_MODULE(if_tap, tapmodevent, NULL);

/*
 * tapmodevent
 *
 * module event handler
 */
static int
tapmodevent(module_t mod, int type, void *data)
{
	static int		 attached = 0;
	struct ifnet		*ifp = NULL;
	int			 unit;

	switch (type) {
	case MOD_LOAD:
		if (attached)
			return (EEXIST);

		dev_ops_add(&tap_ops, 0, 0);
		attached = 1;
	break;

	case MOD_UNLOAD:
		if (taprefcnt > 0)
			return (EBUSY);

		dev_ops_remove(&tap_ops, 0, 0);

		/* XXX: maintain tap ifs in a local list */
		unit = 0;
		while (unit <= taplastunit) {
			TAILQ_FOREACH(ifp, &ifnet, if_link) {
				if ((strcmp(ifp->if_dname, TAP) == 0) ||
				    (strcmp(ifp->if_dname, VMNET) == 0)) {
					if (ifp->if_dunit == unit)
						break;
				}
			}

			if (ifp != NULL) {
				struct tap_softc	*tp = ifp->if_softc;

				TAPDEBUG(ifp, "detached. minor = %#x, " \
					"taplastunit = %d\n",
					minor(tp->tap_dev), taplastunit);

				ether_ifdetach(ifp);
				destroy_dev(tp->tap_dev);
				kfree(tp, M_TAP);
			}
			else
				unit ++;
		}

		attached = 0;
	break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
} /* tapmodevent */


/*
 * tapcreate
 *
 * to create interface
 */
static void
tapcreate(dev_t dev)
{
	struct ifnet		*ifp = NULL;
	struct tap_softc	*tp = NULL;
	uint8_t			ether_addr[ETHER_ADDR_LEN];
	int			 unit;
	char			*name = NULL;

	/* allocate driver storage and create device */
	MALLOC(tp, struct tap_softc *, sizeof(*tp), M_TAP, M_WAITOK);
	bzero(tp, sizeof(*tp));

	/* select device: tap or vmnet */
	if (minor(dev) & VMNET_DEV_MASK) {
		name = VMNET;
		unit = lminor(dev) & 0xff;
		tp->tap_flags |= TAP_VMNET;
	}
	else {
		name = TAP;
		unit = lminor(dev);
	}

	tp->tap_dev = make_dev(&tap_ops, minor(dev), UID_ROOT, GID_WHEEL, 
						0600, "%s%d", name, unit);
	tp->tap_dev->si_drv1 = dev->si_drv1 = tp;
	reference_dev(tp->tap_dev);	/* so we can destroy it later */

	/* generate fake MAC address: 00 bd xx xx xx unit_no */
	ether_addr[0] = 0x00;
	ether_addr[1] = 0xbd;
	bcopy(&ticks, ether_addr, 4);
	ether_addr[5] = (u_char)unit;

	/* fill the rest and attach interface */	
	ifp = &tp->tap_if;
	ifp->if_softc = tp;

	if_initname(ifp, name, unit);
	if (unit > taplastunit)
		taplastunit = unit;

	ifp->if_init = tapifinit;
	ifp->if_start = tapifstart;
	ifp->if_ioctl = tapifioctl;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = (IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST);
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, ether_addr, NULL);

	tp->tap_flags |= TAP_INITED;

	TAPDEBUG(ifp, "created. minor = %#x\n", minor(tp->tap_dev));
} /* tapcreate */


/*
 * tapopen 
 *
 * to open tunnel. must be superuser
 */
static int
tapopen(struct dev_open_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct tap_softc	*tp = NULL;
	int			 error;

	if ((error = suser_cred(ap->a_cred, 0)) != 0)
		return (error);

	tp = dev->si_drv1;
	if (tp == NULL) {
		tapcreate(dev);
		tp = dev->si_drv1;
	}

	if (tp->tap_flags & TAP_OPEN)
		return (EBUSY);

	bcopy(tp->arpcom.ac_enaddr, tp->ether_addr, sizeof(tp->ether_addr));

	tp->tap_td = curthread;
	tp->tap_flags |= TAP_OPEN;
	taprefcnt ++;

	TAPDEBUG(&tp->arpcom.ac_if,
		 "opened. minor = %#x, refcnt = %d, taplastunit = %d\n",
		 minor(tp->tap_dev), taprefcnt, taplastunit);

	return (0);
} /* tapopen */


/*
 * tapclose
 *
 * close the device - mark i/f down & delete routing info
 */
static int
tapclose(struct dev_close_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;

	/* junk all pending output */

	lwkt_serialize_enter(ifp->if_serializer);
	ifq_purge(&ifp->if_snd);
	lwkt_serialize_exit(ifp->if_serializer);

	/*
	 * do not bring the interface down, and do not anything with
	 * interface, if we are in VMnet mode. just close the device.
	 */

	if (((tp->tap_flags & TAP_VMNET) == 0) && (ifp->if_flags & IFF_UP)) {
		if_down(ifp);
		lwkt_serialize_enter(ifp->if_serializer);
		if (ifp->if_flags & IFF_RUNNING) {
			/* find internet addresses and delete routes */
			struct ifaddr	*ifa = NULL;

			TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
				if (ifa->ifa_addr->sa_family == AF_INET) {
					rtinit(ifa, (int)RTM_DELETE, 0);

					/* remove address from interface */
					bzero(ifa->ifa_addr, 
						   sizeof(*(ifa->ifa_addr)));
					bzero(ifa->ifa_dstaddr, 
						   sizeof(*(ifa->ifa_dstaddr)));
					bzero(ifa->ifa_netmask, 
						   sizeof(*(ifa->ifa_netmask)));
				}
			}

			ifp->if_flags &= ~IFF_RUNNING;
		}
		lwkt_serialize_exit(ifp->if_serializer);
	}

	funsetown(tp->tap_sigio);
	selwakeup(&tp->tap_rsel);

	tp->tap_flags &= ~TAP_OPEN;
	tp->tap_td = NULL;

	taprefcnt --;
	if (taprefcnt < 0) {
		taprefcnt = 0;
		if_printf(ifp, "minor = %#x, refcnt = %d is out of sync. "
			"set refcnt to 0\n", minor(tp->tap_dev), taprefcnt);
	}

	TAPDEBUG(ifp, "closed. minor = %#x, refcnt = %d, taplastunit = %d\n",
		 minor(tp->tap_dev), taprefcnt, taplastunit);

	return (0);
} /* tapclose */


/*
 * tapifinit
 *
 * network interface initialization function
 */
static void
tapifinit(void *xtp)
{
	struct tap_softc	*tp = (struct tap_softc *)xtp;
	struct ifnet		*ifp = &tp->tap_if;

	TAPDEBUG(ifp, "initializing, minor = %#x\n", minor(tp->tap_dev));

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	/* attempt to start output */
	tapifstart(ifp);
} /* tapifinit */


/*
 * tapifioctl
 *
 * Process an ioctl request on network interface
 *
 * MPSAFE
 */
int
tapifioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct tap_softc 	*tp = (struct tap_softc *)(ifp->if_softc);
	struct ifstat		*ifs = NULL;
	int			 dummy;

	switch (cmd) {
		case SIOCSIFADDR:
		case SIOCGIFADDR:
		case SIOCSIFMTU:
			dummy = ether_ioctl(ifp, cmd, data);
			return (dummy);

		case SIOCSIFFLAGS: /* XXX -- just like vmnet does */
		case SIOCADDMULTI:
		case SIOCDELMULTI:
			break;

		case SIOCGIFSTATUS:
			ifs = (struct ifstat *)data;
			dummy = strlen(ifs->ascii);
			if (tp->tap_td != NULL && dummy < sizeof(ifs->ascii)) {
				if (tp->tap_td->td_proc) {
				    snprintf(ifs->ascii + dummy,
					sizeof(ifs->ascii) - dummy,
					"\tOpened by pid %d\n",
					(int)tp->tap_td->td_proc->p_pid);
				} else {
				    snprintf(ifs->ascii + dummy,
					sizeof(ifs->ascii) - dummy,
					"\tOpened by td %p\n", tp->tap_td);
				}
			}
			break;

		default:
			return (EINVAL);
	}

	return (0);
} /* tapifioctl */


/*
 * tapifstart 
 * 
 * queue packets from higher level ready to put out
 */
static void
tapifstart(struct ifnet *ifp)
{
	struct tap_softc	*tp = ifp->if_softc;

	TAPDEBUG(ifp, "starting, minor = %#x\n", minor(tp->tap_dev));

	/*
	 * do not junk pending output if we are in VMnet mode.
	 * XXX: can this do any harm because of queue overflow?
	 */

	if (((tp->tap_flags & TAP_VMNET) == 0) && 
	    ((tp->tap_flags & TAP_READY) != TAP_READY)) {
		TAPDEBUG(ifp, "not ready. minor = %#x, tap_flags = 0x%x\n",
			 minor(tp->tap_dev), tp->tap_flags);

		ifq_purge(&ifp->if_snd);
		return;
	}

	ifp->if_flags |= IFF_OACTIVE;

	if (!ifq_is_empty(&ifp->if_snd)) {
		if (tp->tap_flags & TAP_RWAIT) {
			tp->tap_flags &= ~TAP_RWAIT;
			wakeup((caddr_t)tp);
		}

		if ((tp->tap_flags & TAP_ASYNC) && (tp->tap_sigio != NULL))
			pgsigio(tp->tap_sigio, SIGIO, 0);

		/*
		 * selwakeup is not MPSAFE.  tapifstart is.
		 */
		get_mplock();
		selwakeup(&tp->tap_rsel);
		rel_mplock();
		ifp->if_opackets ++; /* obytes are counted in ether_output */
	}

	ifp->if_flags &= ~IFF_OACTIVE;
} /* tapifstart */


/*
 * tapioctl
 *
 * the ops interface is now pretty minimal
 */
static int
tapioctl(struct dev_ioctl_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;
 	struct tapinfo		*tapp = NULL;
	struct mbuf *mb;
	short f;
	int error;

	lwkt_serialize_enter(ifp->if_serializer);
	error = 0;

	switch (ap->a_cmd) {
	case TAPSIFINFO:
		tapp = (struct tapinfo *)data;
		ifp->if_mtu = tapp->mtu;
		ifp->if_type = tapp->type;
		ifp->if_baudrate = tapp->baudrate;
		break;

	case TAPGIFINFO:
		tapp = (struct tapinfo *)data;
		tapp->mtu = ifp->if_mtu;
		tapp->type = ifp->if_type;
		tapp->baudrate = ifp->if_baudrate;
		break;

	case TAPSDEBUG:
		tapdebug = *(int *)data;
		break;

	case TAPGDEBUG:
		*(int *)data = tapdebug;
		break;

	case FIOASYNC:
		if (*(int *)data)
			tp->tap_flags |= TAP_ASYNC;
		else
			tp->tap_flags &= ~TAP_ASYNC;
		break;

	case FIONREAD:
		*(int *)data = 0;
		if ((mb = ifq_poll(&ifp->if_snd)) != NULL) {
			for(; mb != NULL; mb = mb->m_next)
				*(int *)data += mb->m_len;
		} 
		break;

	case FIOSETOWN:
		error = fsetown(*(int *)data, &tp->tap_sigio);
		break;

	case FIOGETOWN:
		*(int *)data = fgetown(tp->tap_sigio);
		break;

	/* this is deprecated, FIOSETOWN should be used instead */
	case TIOCSPGRP:
		error = fsetown(-(*(int *)data), &tp->tap_sigio);
		break;

	/* this is deprecated, FIOGETOWN should be used instead */
	case TIOCGPGRP:
		*(int *)data = -fgetown(tp->tap_sigio);
		break;

	/* VMware/VMnet port ioctl's */

	case SIOCGIFFLAGS:	/* get ifnet flags */
		bcopy(&ifp->if_flags, data, sizeof(ifp->if_flags));
		break;

	case VMIO_SIOCSIFFLAGS: /* VMware/VMnet SIOCSIFFLAGS */
		f = *(short *)data;
		f &= 0x0fff;
		f &= ~IFF_CANTCHANGE;
		f |= IFF_UP;
		ifp->if_flags = f | (ifp->if_flags & IFF_CANTCHANGE);
		break;

	case OSIOCGIFADDR:	/* get MAC address of the remote side */
	case SIOCGIFADDR:
		bcopy(tp->ether_addr, data, sizeof(tp->ether_addr));
		break;

	case SIOCSIFADDR:	/* set MAC address of the remote side */
		bcopy(data, tp->ether_addr, sizeof(tp->ether_addr));
		break;

	default:
		error = ENOTTY;
		break;
	}
	lwkt_serialize_exit(ifp->if_serializer);
	return (error);
} /* tapioctl */


/*
 * tapread
 *
 * the ops read interface - reads a packet at a time, or at
 * least as much of a packet as can be read
 */
static int
tapread(struct dev_read_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;
	struct mbuf		*m0 = NULL;
	int			 error = 0, len;

	TAPDEBUG(ifp, "reading, minor = %#x\n", minor(tp->tap_dev));

	if ((tp->tap_flags & TAP_READY) != TAP_READY) {
		TAPDEBUG(ifp, "not ready. minor = %#x, tap_flags = 0x%x\n",
			 minor(tp->tap_dev), tp->tap_flags);

		return (EHOSTDOWN);
	}

	tp->tap_flags &= ~TAP_RWAIT;

	/* sleep until we get a packet */
	do {
		lwkt_serialize_enter(ifp->if_serializer);
		m0 = ifq_dequeue(&ifp->if_snd, NULL);
		if (m0 == NULL) {
			if (ap->a_ioflag & IO_NDELAY) {
				lwkt_serialize_exit(ifp->if_serializer);
				return (EWOULDBLOCK);
			}
			tp->tap_flags |= TAP_RWAIT;
			crit_enter();
			tsleep_interlock(tp);
			lwkt_serialize_exit(ifp->if_serializer);
			error = tsleep(tp, PCATCH, "taprd", 0);
			crit_exit();
			if (error)
				return (error);
		} else {
			lwkt_serialize_exit(ifp->if_serializer);
		}
	} while (m0 == NULL);

	BPF_MTAP(ifp, m0);

	/* xfer packet to user space */
	while ((m0 != NULL) && (uio->uio_resid > 0) && (error == 0)) {
		len = min(uio->uio_resid, m0->m_len);
		if (len == 0)
			break;

		error = uiomove(mtod(m0, caddr_t), len, uio);
		m0 = m_free(m0);
	}

	if (m0 != NULL) {
		TAPDEBUG(ifp, "dropping mbuf, minor = %#x\n",
			 minor(tp->tap_dev));
		m_freem(m0);
	}

	return (error);
} /* tapread */


/*
 * tapwrite
 *
 * the ops write interface - an atomic write is a packet - or else!
 */
static int
tapwrite(struct dev_write_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;
	struct mbuf		*top = NULL, **mp = NULL, *m = NULL;
	int		 	 error = 0, tlen, mlen;

	TAPDEBUG(ifp, "writting, minor = %#x\n", minor(tp->tap_dev));

	if (uio->uio_resid == 0)
		return (0);

	if ((uio->uio_resid < 0) || (uio->uio_resid > TAPMRU)) {
		TAPDEBUG(ifp, "invalid packet len = %d, minor = %#x\n",
			 uio->uio_resid, minor(tp->tap_dev));

		return (EIO);
	}
	tlen = uio->uio_resid;

	/* get a header mbuf */
	MGETHDR(m, MB_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	mlen = MHLEN;

	top = 0;
	mp = &top;
	while ((error == 0) && (uio->uio_resid > 0)) {
		m->m_len = min(mlen, uio->uio_resid);
		error = uiomove(mtod(m, caddr_t), m->m_len, uio);
		*mp = m;
		mp = &m->m_next;
		if (uio->uio_resid > 0) {
			MGET(m, MB_DONTWAIT, MT_DATA);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
			mlen = MLEN;
		}
	}
	if (error) {
		ifp->if_ierrors ++;
		if (top)
			m_freem(top);
		return (error);
	}

	top->m_pkthdr.len = tlen;
	top->m_pkthdr.rcvif = ifp;
	
	/*
	 * Ethernet bridge and bpf are handled in ether_input
	 *
	 * adjust mbuf and give packet to the ether_input
	 */
	lwkt_serialize_enter(ifp->if_serializer);
	ifp->if_input(ifp, top);
	ifp->if_ipackets ++; /* ibytes are counted in ether_input */
	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
} /* tapwrite */


/*
 * tappoll
 *
 * the poll interface, this is only useful on reads
 * really. the write detect always returns true, write never blocks
 * anyway, it either accepts the packet or drops it
 */
static int
tappoll(struct dev_poll_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;
	int		 	 revents = 0;

	TAPDEBUG(ifp, "polling, minor = %#x\n", minor(tp->tap_dev));

	lwkt_serialize_enter(ifp->if_serializer);
	if (ap->a_events & (POLLIN | POLLRDNORM)) {
		if (!ifq_is_empty(&ifp->if_snd)) {
			TAPDEBUG(ifp,
				 "has data in queue. minor = %#x\n",
				 minor(tp->tap_dev));

			revents |= (ap->a_events & (POLLIN | POLLRDNORM));
		} 
		else {
			TAPDEBUG(ifp, "waiting for data, minor = %#x\n",
				 minor(tp->tap_dev));

			selrecord(curthread, &tp->tap_rsel);
		}
	}
	lwkt_serialize_exit(ifp->if_serializer);

	if (ap->a_events & (POLLOUT | POLLWRNORM))
		revents |= (ap->a_events & (POLLOUT | POLLWRNORM));
	ap->a_events = revents;
	return(0);
} /* tappoll */
