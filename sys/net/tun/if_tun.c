/*	$NetBSD: if_tun.c,v 1.14 1994/06/29 06:36:25 cgd Exp $	*/

/*
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 *
 * This source may be freely distributed, however I would be interested
 * in any changes that are made.
 *
 * This driver takes packets off the IP i/f and hands them up to a
 * user process to have its wicked way with. This driver has it's
 * roots in a similar driver written by Phil Cockcroft (formerly) at
 * UCL. This driver is based much more on read/write/poll mode of
 * operation though.
 *
 * $FreeBSD: src/sys/net/if_tun.c,v 1.74.2.8 2002/02/13 00:43:11 dillon Exp $
 */

#include "use_tun.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/thread2.h>
#include <sys/ttycom.h>
#include <sys/signalvar.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

#include <sys/mplock2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/netisr.h>
#include <net/route.h>
#include <sys/devfs.h>

#ifdef INET
#include <netinet/in.h>
#endif

#include <net/bpf.h>

#include "if_tunvar.h"
#include "if_tun.h"

static MALLOC_DEFINE(M_TUN, "tun", "Tunnel Interface");

static void tunattach (void *);
PSEUDO_SET(tunattach, if_tun);

static void tuncreate (cdev_t dev);

#define TUNDEBUG	if (tundebug) if_printf
static int tundebug = 0;
SYSCTL_INT(_debug, OID_AUTO, if_tun_debug, CTLFLAG_RW, &tundebug, 0,
    "Enable debug output");

static int tunoutput (struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *rt);
static int tunifioctl (struct ifnet *, u_long, caddr_t, struct ucred *);
static int tuninit (struct ifnet *);
static void tunstart(struct ifnet *, struct ifaltq_subque *);
static void tun_filter_detach(struct knote *);
static int tun_filter_read(struct knote *, long);
static int tun_filter_write(struct knote *, long);

static	d_open_t	tunopen;
static	d_close_t	tunclose;
static	d_read_t	tunread;
static	d_write_t	tunwrite;
static	d_ioctl_t	tunioctl;
static	d_kqfilter_t	tunkqfilter;

static d_clone_t tunclone;
DEVFS_DECLARE_CLONE_BITMAP(tun);

#if NTUN <= 1
#define TUN_PREALLOCATED_UNITS	4
#else
#define TUN_PREALLOCATED_UNITS	NTUN
#endif

static struct dev_ops tun_ops = {
	{ "tun", 0, 0 },
	.d_open =	tunopen,
	.d_close =	tunclose,
	.d_read =	tunread,
	.d_write =	tunwrite,
	.d_ioctl =	tunioctl,
	.d_kqfilter =	tunkqfilter
};

static void
tunattach(void *dummy)
{
	int i;
	make_autoclone_dev(&tun_ops, &DEVFS_CLONE_BITMAP(tun),
		tunclone, UID_UUCP, GID_DIALER, 0600, "tun");
	for (i = 0; i < TUN_PREALLOCATED_UNITS; i++) {
		make_dev(&tun_ops, i, UID_UUCP, GID_DIALER, 0600, "tun%d", i);
		devfs_clone_bitmap_set(&DEVFS_CLONE_BITMAP(tun), i);
	}
	/* Doesn't need uninit because unloading is not possible, see PSEUDO_SET */
}

static int
tunclone(struct dev_clone_args *ap)
{
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(tun), 0);
	ap->a_dev = make_only_dev(&tun_ops, unit, UID_UUCP, GID_DIALER, 0600,
								"tun%d", unit);

	return 0;
}

static void
tuncreate(cdev_t dev)
{
	struct tun_softc *sc;
	struct ifnet *ifp;

#if 0
	dev = make_dev(&tun_ops, minor(dev),
	    UID_UUCP, GID_DIALER, 0600, "tun%d", lminor(dev));
#endif

	sc = kmalloc(sizeof(*sc), M_TUN, M_WAITOK | M_ZERO);
	sc->tun_flags = TUN_INITED;

	ifp = &sc->tun_if;
	if_initname(ifp, "tun", lminor(dev));
	ifp->if_mtu = TUNMTU;
	ifp->if_ioctl = tunifioctl;
	ifp->if_output = tunoutput;
	ifp->if_start = tunstart;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_type = IFT_PPP;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);
	ifp->if_softc = sc;
	if_attach(ifp, NULL);
	bpfattach(ifp, DLT_NULL, sizeof(u_int));
	dev->si_drv1 = sc;
}

/*
 * tunnel open - must be superuser & the device must be
 * configured in
 */
static	int
tunopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct ifnet	*ifp;
	struct tun_softc *tp;
	int	error;

	if ((error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) != 0)
		return (error);

	tp = dev->si_drv1;
	if (!tp) {
		tuncreate(dev);
		tp = dev->si_drv1;
	}
	if (tp->tun_flags & TUN_OPEN)
		return EBUSY;
	tp->tun_pid = curproc->p_pid;
	ifp = &tp->tun_if;
	tp->tun_flags |= TUN_OPEN;
	TUNDEBUG(ifp, "open\n");
	return (0);
}

/*
 * tunclose - close the device - mark i/f down & delete
 * routing info
 */
static	int
tunclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *tp;
	struct ifnet	*ifp;

	tp = dev->si_drv1;
	ifp = &tp->tun_if;

	tp->tun_flags &= ~TUN_OPEN;
	tp->tun_pid = 0;

	/* Junk all pending output. */
	ifq_purge_all(&ifp->if_snd);

	if (ifp->if_flags & IFF_UP)
		if_down(ifp);
	ifp->if_flags &= ~IFF_RUNNING;
	if_purgeaddrs_nolink(ifp);

	funsetown(&tp->tun_sigio);
	KNOTE(&tp->tun_rkq.ki_note, 0);

	TUNDEBUG(ifp, "closed\n");
#if 0
	if (dev->si_uminor >= TUN_PREALLOCATED_UNITS) {
		devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(tun), dev->si_uminor);
	}
#endif
	return (0);
}

static int
tuninit(struct ifnet *ifp)
{
#ifdef INET
	struct tun_softc *tp = ifp->if_softc;
#endif
	struct ifaddr_container *ifac;
	int error = 0;

	TUNDEBUG(ifp, "tuninit\n");

	ifp->if_flags |= IFF_UP | IFF_RUNNING;
	getmicrotime(&ifp->if_lastchange);

	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr == NULL) {
			error = EFAULT;
			/* XXX: Should maybe return straight off? */
		} else {
#ifdef INET
			if (ifa->ifa_addr->sa_family == AF_INET) {
			    struct sockaddr_in *si;

			    si = (struct sockaddr_in *)ifa->ifa_addr;
			    if (si->sin_addr.s_addr)
				    tp->tun_flags |= TUN_IASET;
			}
#endif
		}
	}
	return (error);
}

/*
 * Process an ioctl request.
 *
 * MPSAFE
 */
int
tunifioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct tun_softc *tp = ifp->if_softc;
	struct ifstat *ifs;
	int error = 0;

	switch(cmd) {
	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		if (tp->tun_pid)
			ksprintf(ifs->ascii + strlen(ifs->ascii),
			    "\tOpened by PID %d\n", tp->tun_pid);
		break;
	case SIOCSIFADDR:
		error = tuninit(ifp);
		TUNDEBUG(ifp, "address set, error=%d\n", error);
		break;
	case SIOCSIFDSTADDR:
		error = tuninit(ifp);
		TUNDEBUG(ifp, "destination address set, error=%d\n", error);
		break;
	case SIOCSIFMTU:
		ifp->if_mtu = ifr->ifr_mtu;
		TUNDEBUG(ifp, "mtu set\n");
		break;
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

/*
 * tunoutput - queue packets from higher level ready to put out.
 *
 * MPSAFE
 */
static int
tunoutput_serialized(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst,
		     struct rtentry *rt)
{
	struct tun_softc *tp = ifp->if_softc;
	int error;
	struct altq_pktattr pktattr;

	TUNDEBUG(ifp, "tunoutput\n");

	if ((tp->tun_flags & TUN_READY) != TUN_READY) {
		TUNDEBUG(ifp, "not ready 0%o\n", tp->tun_flags);
		m_freem (m0);
		return EHOSTDOWN;
	}

	/*
	 * if the queueing discipline needs packet classification,
	 * do it before prepending link headers.
	 */
	ifq_classify(&ifp->if_snd, m0, dst->sa_family, &pktattr);

	/* BPF write needs to be handled specially */
	if (dst->sa_family == AF_UNSPEC) {
		dst->sa_family = *(mtod(m0, int *));
		m0->m_len -= sizeof(int);
		m0->m_pkthdr.len -= sizeof(int);
		m0->m_data += sizeof(int);
	}

	if (ifp->if_bpf) {
		bpf_gettoken();
		if (ifp->if_bpf) {
			/*
			 * We need to prepend the address family as
			 * a four byte field.
			 */
			uint32_t af = dst->sa_family;

			bpf_ptap(ifp->if_bpf, m0, &af, sizeof(af));
		}
		bpf_reltoken();
	}

	/* prepend sockaddr? this may abort if the mbuf allocation fails */
	if (tp->tun_flags & TUN_LMODE) {
		/* allocate space for sockaddr */
		M_PREPEND(m0, dst->sa_len, M_NOWAIT);

		/* if allocation failed drop packet */
		if (m0 == NULL){
			IFNET_STAT_INC(ifp, oerrors, 1);
			return (ENOBUFS);
		} else {
			bcopy(dst, m0->m_data, dst->sa_len);
		}
	}

	if (tp->tun_flags & TUN_IFHEAD) {
		/* Prepend the address family */
		M_PREPEND(m0, 4, M_NOWAIT);

		/* if allocation failed drop packet */
		if (m0 == NULL){
			IFNET_STAT_INC(ifp, oerrors, 1);
			return ENOBUFS;
		} else
			*(u_int32_t *)m0->m_data = htonl(dst->sa_family);
	} else {
#ifdef INET
		if (dst->sa_family != AF_INET)
#endif
		{
			m_freem(m0);
			return EAFNOSUPPORT;
		}
	}

	error = ifq_handoff(ifp, m0, &pktattr);
	if (error) {
		IFNET_STAT_INC(ifp, collisions, 1);
	} else {
		IFNET_STAT_INC(ifp, opackets, 1);
		if (tp->tun_flags & TUN_RWAIT) {
			tp->tun_flags &= ~TUN_RWAIT;
			wakeup((caddr_t)tp);
		}
		get_mplock();
		if (tp->tun_flags & TUN_ASYNC && tp->tun_sigio)
			pgsigio(tp->tun_sigio, SIGIO, 0);
		rel_mplock();
		ifnet_deserialize_all(ifp);
		KNOTE(&tp->tun_rkq.ki_note, 0);
		ifnet_serialize_all(ifp);
	}
	return (error);
}

static int
tunoutput(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst,
	  struct rtentry *rt)
{
	int error;

	ifnet_serialize_all(ifp);
	error = tunoutput_serialized(ifp, m0, dst, rt);
	ifnet_deserialize_all(ifp);

	return error;
}

/*
 * the ops interface is now pretty minimal.
 */
static	int
tunioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *tp = dev->si_drv1;
 	struct tuninfo *tunp;

	switch (ap->a_cmd) {
	case TUNSIFINFO:
		tunp = (struct tuninfo *)ap->a_data;
		if (tunp->mtu < IF_MINMTU)
			return (EINVAL);
		tp->tun_if.if_mtu = tunp->mtu;
		tp->tun_if.if_type = tunp->type;
		tp->tun_if.if_baudrate = tunp->baudrate;
		break;
	case TUNGIFINFO:
		tunp = (struct tuninfo *)ap->a_data;
		tunp->mtu = tp->tun_if.if_mtu;
		tunp->type = tp->tun_if.if_type;
		tunp->baudrate = tp->tun_if.if_baudrate;
		break;
	case TUNSDEBUG:
		tundebug = *(int *)ap->a_data;
		break;
	case TUNGDEBUG:
		*(int *)ap->a_data = tundebug;
		break;
	case TUNSLMODE:
		if (*(int *)ap->a_data) {
			tp->tun_flags |= TUN_LMODE;
			tp->tun_flags &= ~TUN_IFHEAD;
		} else
			tp->tun_flags &= ~TUN_LMODE;
		break;
	case TUNSIFHEAD:
		if (*(int *)ap->a_data) {
			tp->tun_flags |= TUN_IFHEAD;
			tp->tun_flags &= ~TUN_LMODE;
		} else 
			tp->tun_flags &= ~TUN_IFHEAD;
		break;
	case TUNGIFHEAD:
		*(int *)ap->a_data = (tp->tun_flags & TUN_IFHEAD) ? 1 : 0;
		break;
	case TUNSIFMODE:
		/* deny this if UP */
		if (tp->tun_if.if_flags & IFF_UP)
			return(EBUSY);

		switch (*(int *)ap->a_data & ~IFF_MULTICAST) {
		case IFF_POINTOPOINT:
		case IFF_BROADCAST:
			tp->tun_if.if_flags &= ~(IFF_BROADCAST|IFF_POINTOPOINT);
			tp->tun_if.if_flags |= *(int *)ap->a_data;
			break;
		default:
			return(EINVAL);
		}
		break;
	case TUNSIFPID:
		tp->tun_pid = curproc->p_pid;
		break;
	case FIOASYNC:
		if (*(int *)ap->a_data)
			tp->tun_flags |= TUN_ASYNC;
		else
			tp->tun_flags &= ~TUN_ASYNC;
		break;
	case FIONREAD:
		*(int *)ap->a_data = ifsq_poll_pktlen(
		    ifq_get_subq_default(&tp->tun_if.if_snd));
		break;
	case FIOSETOWN:
		return (fsetown(*(int *)ap->a_data, &tp->tun_sigio));

	case FIOGETOWN:
		*(int *)ap->a_data = fgetown(&tp->tun_sigio);
		return (0);

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		return (fsetown(-(*(int *)ap->a_data), &tp->tun_sigio));

	/* This is deprecated, FIOGETOWN should be used instead. */
	case TIOCGPGRP:
		*(int *)ap->a_data = -fgetown(&tp->tun_sigio);
		return (0);

	default:
		return (ENOTTY);
	}
	return (0);
}

/*
 * The ops read interface - reads a packet at a time, or at
 * least as much of a packet as can be read.
 */
static	int
tunread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tun_softc *tp = dev->si_drv1;
	struct ifnet	*ifp = &tp->tun_if;
	struct ifaltq_subque *ifsq = ifq_get_subq_default(&ifp->if_snd);
	struct mbuf	*m0;
	int		error=0, len;

	TUNDEBUG(ifp, "read\n");
	if ((tp->tun_flags & TUN_READY) != TUN_READY) {
		TUNDEBUG(ifp, "not ready 0%o\n", tp->tun_flags);
		return EHOSTDOWN;
	}

	tp->tun_flags &= ~TUN_RWAIT;

	ifnet_serialize_all(ifp);

	while ((m0 = ifsq_dequeue(ifsq)) == NULL) {
		if (ap->a_ioflag & IO_NDELAY) {
			ifnet_deserialize_all(ifp);
			return EWOULDBLOCK;
		}
		tp->tun_flags |= TUN_RWAIT;
		ifnet_deserialize_all(ifp);
		if ((error = tsleep(tp, PCATCH, "tunread", 0)) != 0)
			return error;
		ifnet_serialize_all(ifp);
	}

	ifnet_deserialize_all(ifp);

	while (m0 && uio->uio_resid > 0 && error == 0) {
		len = (int)szmin(uio->uio_resid, m0->m_len);
		if (len != 0)
			error = uiomove(mtod(m0, caddr_t), (size_t)len, uio);
		m0 = m_free(m0);
	}

	if (m0) {
		TUNDEBUG(ifp, "Dropping mbuf\n");
		m_freem(m0);
	}
	return error;
}

/*
 * the ops write interface - an atomic write is a packet - or else!
 */
static	int
tunwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tun_softc *tp = dev->si_drv1;
	struct ifnet	*ifp = &tp->tun_if;
	struct mbuf	*top, **mp, *m;
	int		error=0;
	size_t		tlen, mlen;
	uint32_t	family;
	int		isr;

	TUNDEBUG(ifp, "tunwrite\n");

	if (uio->uio_resid == 0)
		return 0;

	if (uio->uio_resid > TUNMRU) {
		TUNDEBUG(ifp, "len=%zd!\n", uio->uio_resid);
		return EIO;
	}
	tlen = uio->uio_resid;

	/* get a header mbuf */
	MGETHDR(m, M_WAITOK, MT_DATA);
	if (m == NULL)
		return ENOBUFS;
	mlen = MHLEN;

	top = NULL;
	mp = &top;
	while (error == 0 && uio->uio_resid > 0) {
		m->m_len = (int)szmin(mlen, uio->uio_resid);
		error = uiomove(mtod (m, caddr_t), (size_t)m->m_len, uio);
		*mp = m;
		mp = &m->m_next;
		if (uio->uio_resid > 0) {
			MGET (m, M_WAITOK, MT_DATA);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
			mlen = MLEN;
		}
	}
	if (error) {
		if (top)
			m_freem (top);
		IFNET_STAT_INC(ifp, ierrors, 1);
		return error;
	}

	top->m_pkthdr.len = (int)tlen;
	top->m_pkthdr.rcvif = ifp;

	if (ifp->if_bpf) {
		bpf_gettoken();

		if (ifp->if_bpf) {
			if (tp->tun_flags & TUN_IFHEAD) {
				/*
				 * Conveniently, we already have a 4-byte
				 * address family prepended to our packet !
				 * Inconveniently, it's in the wrong byte
				 * order !
				 */
				if ((top = m_pullup(top, sizeof(family)))
				    == NULL) {
					bpf_reltoken();
					return ENOBUFS;
				}
				*mtod(top, u_int32_t *) =
				    ntohl(*mtod(top, u_int32_t *));
				bpf_mtap(ifp->if_bpf, top);
				*mtod(top, u_int32_t *) =
				    htonl(*mtod(top, u_int32_t *));
			} else {
				/*
				 * We need to prepend the address family as
				 * a four byte field.
				 */
				static const uint32_t af = AF_INET;

				bpf_ptap(ifp->if_bpf, top, &af, sizeof(af));
			}
		}

		bpf_reltoken();
	}

	if (tp->tun_flags & TUN_IFHEAD) {
		if (top->m_len < sizeof(family) &&
		    (top = m_pullup(top, sizeof(family))) == NULL)
				return ENOBUFS;
		family = ntohl(*mtod(top, u_int32_t *));
		m_adj(top, sizeof(family));
	} else
		family = AF_INET;

	IFNET_STAT_INC(ifp, ibytes, top->m_pkthdr.len);
	IFNET_STAT_INC(ifp, ipackets, 1);

	switch (family) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		m_freem(m);
		return (EAFNOSUPPORT);
	}

	netisr_queue(isr, top);
	return (0);
}

static struct filterops tun_read_filtops =
	{ FILTEROP_ISFD, NULL, tun_filter_detach, tun_filter_read };
static struct filterops tun_write_filtops =
	{ FILTEROP_ISFD, NULL, tun_filter_detach, tun_filter_write };

static int
tunkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *tp = dev->si_drv1;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;
	ifnet_serialize_all(&tp->tun_if);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &tun_read_filtops;
		kn->kn_hook = (caddr_t)tp;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &tun_write_filtops;
		kn->kn_hook = (caddr_t)tp;
		break;
	default:
		ifnet_deserialize_all(&tp->tun_if);
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &tp->tun_rkq.ki_note;
	knote_insert(klist, kn);
	ifnet_deserialize_all(&tp->tun_if);

	return (0);
}

static void
tun_filter_detach(struct knote *kn)
{
	struct tun_softc *tp = (struct tun_softc *)kn->kn_hook;
	struct klist *klist = &tp->tun_rkq.ki_note;

	knote_remove(klist, kn);
}

static int
tun_filter_write(struct knote *kn, long hint)
{
	/* Always ready for a write */
	return (1);
}

static int
tun_filter_read(struct knote *kn, long hint)
{
	struct tun_softc *tp = (struct tun_softc *)kn->kn_hook;
	int ready = 0;

	ifnet_serialize_all(&tp->tun_if);
	if (!ifsq_is_empty(ifq_get_subq_default(&tp->tun_if.if_snd)))
		ready = 1;
	ifnet_deserialize_all(&tp->tun_if);

	return (ready);
}

/*
 * Start packet transmission on the interface.
 * when the interface queue is rate-limited by ALTQ,
 * if_start is needed to drain packets from the queue in order
 * to notify readers when outgoing packets become ready.
 */
static void
tunstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct tun_softc *tp = ifp->if_softc;
	struct mbuf *m;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if (!ifq_is_enabled(&ifp->if_snd))
		return;

	m = ifsq_poll(ifsq);
	if (m != NULL) {
		if (tp->tun_flags & TUN_RWAIT) {
			tp->tun_flags &= ~TUN_RWAIT;
			wakeup((caddr_t)tp);
		}
		if (tp->tun_flags & TUN_ASYNC && tp->tun_sigio)
			pgsigio(tp->tun_sigio, SIGIO, 0);
		ifsq_deserialize_hw(ifsq);
		KNOTE(&tp->tun_rkq.ki_note, 0);
		ifsq_serialize_hw(ifsq);
	}
}
