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
#include <sys/ttycom.h>
#include <sys/signalvar.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/mplock2.h>
#include <sys/devfs.h>
#include <sys/queue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_clone.h>
#include <net/ifq_var.h>
#include <net/netisr.h>
#include <net/route.h>

#ifdef INET
#include <netinet/in.h>
#endif

#include "if_tunvar.h"
#include "if_tun.h"

#define TUN		"tun"
#define TUNDEBUG	if (tundebug) if_printf

/* module */
static int		tunmodevent(module_t, int, void *);

/* device */
static struct tun_softc *tuncreate(cdev_t, int);
static void		tundestroy(struct tun_softc *sc);

/* clone */
static int		tun_clone_create(struct if_clone *, int,
					 caddr_t, caddr_t);
static int		tun_clone_destroy(struct ifnet *);

/* network interface */
static int		tunifinit(struct ifnet *);
static void		tunifstart(struct ifnet *, struct ifaltq_subque *);
static int		tunifoutput(struct ifnet *, struct mbuf *,
				    struct sockaddr *, struct rtentry *rt);
static int		tunifioctl(struct ifnet *, u_long,
				   caddr_t, struct ucred *);

/* character device */
static d_open_t		tunopen;
static d_close_t	tunclose;
static d_read_t		tunread;
static d_write_t	tunwrite;
static d_ioctl_t	tunioctl;
static d_kqfilter_t	tunkqfilter;
static d_clone_t	tunclone;

static struct dev_ops	tun_ops = {
	{ TUN, 0, 0 },
	.d_open =	tunopen,
	.d_close =	tunclose,
	.d_read =	tunread,
	.d_write =	tunwrite,
	.d_ioctl =	tunioctl,
	.d_kqfilter =	tunkqfilter
};

/* kqueue support */
static void		tun_filter_detach(struct knote *);
static int		tun_filter_read(struct knote *, long);
static int		tun_filter_write(struct knote *, long);

static struct filterops tun_read_filtops = {
	FILTEROP_ISFD,
	NULL,
	tun_filter_detach,
	tun_filter_read
};
static struct filterops tun_write_filtops = {
	FILTEROP_ISFD,
	NULL,
	tun_filter_detach,
	tun_filter_write
};

static int		tundebug = 0;	/* debug flag */
static int		tunrefcnt = 0;	/* module reference counter */

static MALLOC_DEFINE(M_TUN, TUN, "Tunnel Interface");

static DEVFS_DEFINE_CLONE_BITMAP(tun);

struct if_clone tun_cloner = IF_CLONE_INITIALIZER(
	TUN, tun_clone_create, tun_clone_destroy, 0, IF_MAXUNIT);

static SLIST_HEAD(,tun_softc) tun_listhead =
	SLIST_HEAD_INITIALIZER(&tun_listhead);

SYSCTL_INT(_debug, OID_AUTO, if_tun_debug, CTLFLAG_RW, &tundebug, 0,
	   "Enable debug output");
SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, OID_AUTO, tun, CTLFLAG_RW, 0,
	    "IP tunnel software network interface");
SYSCTL_INT(_net_link_tun, OID_AUTO, debug, CTLFLAG_RW, &tundebug, 0,
	   "Enable debug output");
SYSCTL_INT(_net_link_tun, OID_AUTO, refcnt, CTLFLAG_RD, &tunrefcnt, 0,
	   "Number of opened devices");

DEV_MODULE(if_tun, tunmodevent, NULL);

/*
 * tunmodevent - module event handler
 */
static int
tunmodevent(module_t mod, int type, void *data)
{
	static cdev_t dev = NULL;
	struct tun_softc *sc, *sc_tmp;

	switch (type) {
	case MOD_LOAD:
		dev = make_autoclone_dev(&tun_ops, &DEVFS_CLONE_BITMAP(tun),
					 tunclone, UID_UUCP, GID_DIALER,
					 0600, TUN);

		SLIST_INIT(&tun_listhead);
		if_clone_attach(&tun_cloner);
		break;

	case MOD_UNLOAD:
		if (tunrefcnt > 0)
			return (EBUSY);

		if_clone_detach(&tun_cloner);

		SLIST_FOREACH_MUTABLE(sc, &tun_listhead, tun_link, sc_tmp)
			tundestroy(sc);

		dev_ops_remove_all(&tun_ops);
		destroy_autoclone_dev(dev, &DEVFS_CLONE_BITMAP(tun));
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static int
tunclone(struct dev_clone_args *ap)
{
	char ifname[IFNAMSIZ];
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(tun), 0);
	ksnprintf(ifname, IFNAMSIZ, "%s%d", TUN, unit);
	ap->a_dev = make_only_dev(&tun_ops, unit, UID_UUCP, GID_DIALER,
				  0600, "%s", ifname);

	/*
	 * Use the if_clone framework to create cloned device/interface,
	 * so the two clone methods (autoclone device /dev/tun; ifconfig
	 * clone) are consistent and can be mix used.
	 *
	 * Need to pass the cdev_t because the device created by
	 * 'make_only_dev()' doesn't appear in '/dev' yet so that it can't
	 * be found by 'devfs_find_device_by_name()' in 'tun_clone_create()'.
	 */
	return (if_clone_create(ifname, IFNAMSIZ, NULL, (caddr_t)ap->a_dev));
}

static struct tun_softc *
tuncreate(cdev_t dev, int flags)
{
	struct tun_softc *sc;
	struct ifnet *ifp;
	int unit = minor(dev);

	sc = kmalloc(sizeof(*sc), M_TUN, M_WAITOK | M_ZERO);
	dev->si_drv1 = sc;
	sc->tun_dev = dev;
	sc->tun_flags = TUN_INITED;
	sc->tun_flags |= flags;

	reference_dev(dev);  /* device association */

	ifp = sc->tun_ifp = if_alloc(IFT_PPP);
	if (ifp == NULL) {
		kprintf("%s: failed to if_alloc() interface for %s%d",
			__func__, TUN, unit);
		return (NULL);
	}

	if_initname(ifp, TUN, unit);
	ifp->if_mtu = TUNMTU;
	ifp->if_ioctl = tunifioctl;
	ifp->if_output = tunifoutput;
	ifp->if_start = tunifstart;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_type = IFT_PPP;
	ifp->if_softc = sc;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	if_attach(ifp, NULL);
	bpfattach(ifp, DLT_NULL, sizeof(u_int));

	SLIST_INSERT_HEAD(&tun_listhead, sc, tun_link);
	TUNDEBUG(ifp, "created, minor = %#x, flags = 0x%x\n",
		 unit, sc->tun_flags);
	return (sc);
}

static void
tundestroy(struct tun_softc *sc)
{
	cdev_t dev = sc->tun_dev;
	struct ifnet *ifp = sc->tun_ifp;
	int unit = minor(dev);

	TUNDEBUG(ifp, "destroyed, minor = %#x. Module refcnt = %d\n",
		 unit, tunrefcnt);

	bpfdetach(ifp);
	if_detach(ifp);
	if_free(ifp);

	sc->tun_dev = NULL;
	dev->si_drv1 = NULL;
	release_dev(dev);  /* device disassociation */

	/* Also destroy the cloned device */
	destroy_dev(dev);
	devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(tun), unit);

	SLIST_REMOVE(&tun_listhead, sc, tun_softc, tun_link);
	kfree(sc, M_TUN);
}

/*
 * tunnel open - must be superuser & the device must be configured in
 */
static int
tunopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *sc;
	int error;

	if ((error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) != 0)
		return (error);

	sc = dev->si_drv1;
	if (sc == NULL && (sc = tuncreate(dev, 0)) == NULL)
		return (ENOMEM);
	if (sc->tun_flags & TUN_OPEN)
		return (EBUSY);

	sc->tun_pid = curproc->p_pid;
	sc->tun_flags |= TUN_OPEN;
	tunrefcnt++;

	TUNDEBUG(sc->tun_ifp, "opened, minor = %#x. Module refcnt = %d\n",
		 minor(dev), tunrefcnt);
	return (0);
}

/*
 * close the device - mark interface down & delete routing info
 */
static int
tunclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp;
	int unit = minor(dev);
	char ifname[IFNAMSIZ];

	KASSERT(sc != NULL,
		("try closing the already destroyed %s%d", TUN, unit));
	ifp = sc->tun_ifp;

	sc->tun_flags &= ~TUN_OPEN;
	sc->tun_pid = 0;

	/* Junk all pending output. */
	ifq_purge_all(&ifp->if_snd);

	if (ifp->if_flags & IFF_UP)
		if_down(ifp);
	ifp->if_flags &= ~IFF_RUNNING;

	if ((sc->tun_flags & TUN_MANUALMAKE) == 0) {
		if_purgeaddrs_nolink(ifp);

		EVENTHANDLER_INVOKE(ifnet_detach_event, ifp);

		/* Announce the departure of the interface. */
		rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
	}

	funsetown(&sc->tun_sigio);
	KNOTE(&sc->tun_rkq.ki_note, 0);

	tunrefcnt--;
	if (tunrefcnt < 0) {
		tunrefcnt = 0;
		if_printf(ifp, ". Module refcnt = %d is out of sync! "
			  "Force refcnt to be 0.\n", tunrefcnt);
	}

	TUNDEBUG(ifp, "closed, minor = %#x. Module refcnt = %d\n",
		 unit, tunrefcnt);

	/* Only auto-destroy if the interface was not manually created. */
	if ((sc->tun_flags & TUN_MANUALMAKE) == 0) {
		ksnprintf(ifname, IFNAMSIZ, "%s%d", TUN, unit);
		if_clone_destroy(ifname);
	}

	return (0);
}


/*
 * Interface clone support
 *
 * Create and destroy tun device/interface via ifconfig(8).
 */

static struct tun_softc *
tunfind(int unit)
{
	struct tun_softc *sc;

	SLIST_FOREACH(sc, &tun_listhead, tun_link) {
		if (minor(sc->tun_dev) == unit)
			return (sc);
	}
	return (NULL);
}

static int
tun_clone_create(struct if_clone *ifc __unused, int unit,
		 caddr_t params __unused, caddr_t data)
{
	struct tun_softc *sc;
	cdev_t dev = (cdev_t)data;
	int flags;

	if (tunfind(unit) != NULL)
		return (EEXIST);

	if (dev == NULL) {
		if (!devfs_clone_bitmap_chk(&DEVFS_CLONE_BITMAP(tun), unit)) {
			devfs_clone_bitmap_set(&DEVFS_CLONE_BITMAP(tun), unit);
			dev = make_dev(&tun_ops, unit, UID_UUCP, GID_DIALER,
				       0600, "%s%d", TUN, unit);
		} else {
			dev = devfs_find_device_by_name("%s%d", TUN, unit);
			if (dev == NULL)
				return (ENOENT);
		}
		flags = TUN_MANUALMAKE;
	} else {
		flags = 0;
	}

	if ((sc = tuncreate(dev, flags)) == NULL)
		return (ENOMEM);

	sc->tun_flags |= TUN_CLONE;

	TUNDEBUG(sc->tun_ifp, "clone created, minor = %#x, flags = 0x%x\n",
		 minor(sc->tun_dev), sc->tun_flags);

	return (0);
}

static int
tun_clone_destroy(struct ifnet *ifp)
{
	struct tun_softc *sc = ifp->if_softc;

	if (sc->tun_flags & TUN_OPEN)
		return (EBUSY);
	if ((sc->tun_flags & TUN_CLONE) == 0)
		return (EINVAL);

	TUNDEBUG(ifp, "clone destroyed, minor = %#x, flags = 0x%x\n",
		 minor(sc->tun_dev), sc->tun_flags);
	tundestroy(sc);

	return (0);
}


/*
 * Network interface functions
 */

static int
tunifinit(struct ifnet *ifp)
{
#ifdef INET
	struct tun_softc *sc = ifp->if_softc;
#endif
	struct ifaddr_container *ifac;
	int error = 0;

	TUNDEBUG(ifp, "initialize\n");

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
					sc->tun_flags |= TUN_IASET;
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
static int
tunifioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct tun_softc *sc = ifp->if_softc;
	struct ifstat *ifs;
	int error = 0;

	switch (cmd) {
	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		if (sc->tun_pid)
			ksprintf(ifs->ascii + strlen(ifs->ascii),
			    "\tOpened by PID %d\n", sc->tun_pid);
		break;
	case SIOCSIFADDR:
		error = tunifinit(ifp);
		TUNDEBUG(ifp, "address set, error=%d\n", error);
		break;
	case SIOCSIFDSTADDR:
		error = tunifinit(ifp);
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
 * Start packet transmission on the interface.
 * when the interface queue is rate-limited by ALTQ,
 * if_start is needed to drain packets from the queue in order
 * to notify readers when outgoing packets become ready.
 */
static void
tunifstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct tun_softc *sc = ifp->if_softc;
	struct mbuf *m;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if (!ifq_is_enabled(&ifp->if_snd))
		return;

	m = ifsq_poll(ifsq);
	if (m != NULL) {
		if (sc->tun_flags & TUN_RWAIT) {
			sc->tun_flags &= ~TUN_RWAIT;
			wakeup((caddr_t)sc);
		}
		if (sc->tun_flags & TUN_ASYNC && sc->tun_sigio)
			pgsigio(sc->tun_sigio, SIGIO, 0);
		ifsq_deserialize_hw(ifsq);
		KNOTE(&sc->tun_rkq.ki_note, 0);
		ifsq_serialize_hw(ifsq);
	}
}

/*
 * tunifoutput - queue packets from higher level ready to put out.
 *
 * MPSAFE
 */
static int
tunifoutput_serialized(struct ifnet *ifp, struct mbuf *m0,
		       struct sockaddr *dst, struct rtentry *rt)
{
	struct tun_softc *sc = ifp->if_softc;
	int error;
	struct altq_pktattr pktattr;

	TUNDEBUG(ifp, "output\n");

	if ((sc->tun_flags & TUN_READY) != TUN_READY) {
		TUNDEBUG(ifp, "not ready, flags = 0x%x\n", sc->tun_flags);
		m_freem (m0);
		return (EHOSTDOWN);
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
	if (sc->tun_flags & TUN_LMODE) {
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

	if (sc->tun_flags & TUN_IFHEAD) {
		/* Prepend the address family */
		M_PREPEND(m0, 4, M_NOWAIT);

		/* if allocation failed drop packet */
		if (m0 == NULL){
			IFNET_STAT_INC(ifp, oerrors, 1);
			return (ENOBUFS);
		} else {
			*(u_int32_t *)m0->m_data = htonl(dst->sa_family);
		}
	} else {
#ifdef INET
		if (dst->sa_family != AF_INET)
#endif
		{
			m_freem(m0);
			return (EAFNOSUPPORT);
		}
	}

	error = ifq_handoff(ifp, m0, &pktattr);
	if (error) {
		IFNET_STAT_INC(ifp, collisions, 1);
	} else {
		IFNET_STAT_INC(ifp, opackets, 1);
		if (sc->tun_flags & TUN_RWAIT) {
			sc->tun_flags &= ~TUN_RWAIT;
			wakeup((caddr_t)sc);
		}
		get_mplock();
		if (sc->tun_flags & TUN_ASYNC && sc->tun_sigio)
			pgsigio(sc->tun_sigio, SIGIO, 0);
		rel_mplock();
		ifnet_deserialize_all(ifp);
		KNOTE(&sc->tun_rkq.ki_note, 0);
		ifnet_serialize_all(ifp);
	}
	return (error);
}

static int
tunifoutput(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst,
	    struct rtentry *rt)
{
	int error;

	ifnet_serialize_all(ifp);
	error = tunifoutput_serialized(ifp, m0, dst, rt);
	ifnet_deserialize_all(ifp);

	return (error);
}


/*
 * the ops interface is now pretty minimal.
 */
static int
tunioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp = sc->tun_ifp;
	struct ifreq *ifr;
	struct tuninfo *tunp;
	int error = 0;

	switch (ap->a_cmd) {
	case TUNSIFINFO:
		tunp = (struct tuninfo *)data;
		if (ifp->if_type != tunp->type)
			return (EPROTOTYPE);
		if (tunp->mtu < IF_MINMTU)
			return (EINVAL);
		ifp->if_mtu = tunp->mtu;
		ifp->if_baudrate = tunp->baudrate;
		break;

	case TUNGIFINFO:
		tunp = (struct tuninfo *)data;
		tunp->mtu = ifp->if_mtu;
		tunp->type = ifp->if_type;
		tunp->baudrate = ifp->if_baudrate;
		break;

	case TUNGIFNAME:
		ifr = (struct ifreq *)data;
		strlcpy(ifr->ifr_name, ifp->if_xname, IFNAMSIZ);
		break;

	case TUNSDEBUG:
		tundebug = *(int *)data;
		break;

	case TUNGDEBUG:
		*(int *)data = tundebug;
		break;

	case TUNSLMODE:
		if (*(int *)data) {
			sc->tun_flags |= TUN_LMODE;
			sc->tun_flags &= ~TUN_IFHEAD;
		} else {
			sc->tun_flags &= ~TUN_LMODE;
		}
		break;

	case TUNSIFHEAD:
		if (*(int *)data) {
			sc->tun_flags |= TUN_IFHEAD;
			sc->tun_flags &= ~TUN_LMODE;
		} else {
			sc->tun_flags &= ~TUN_IFHEAD;
		}
		break;

	case TUNGIFHEAD:
		*(int *)data = (sc->tun_flags & TUN_IFHEAD) ? 1 : 0;
		break;

	case TUNSIFMODE:
		/* deny this if UP */
		if (ifp->if_flags & IFF_UP)
			return (EBUSY);

		switch (*(int *)data & ~IFF_MULTICAST) {
		case IFF_POINTOPOINT:
		case IFF_BROADCAST:
			ifp->if_flags &= ~(IFF_BROADCAST | IFF_POINTOPOINT);
			ifp->if_flags |= *(int *)data;
			break;
		default:
			return (EINVAL);
		}
		break;

	case TUNSIFPID:
		sc->tun_pid = curproc->p_pid;
		break;

	case FIOASYNC:
		if (*(int *)data)
			sc->tun_flags |= TUN_ASYNC;
		else
			sc->tun_flags &= ~TUN_ASYNC;
		break;

	case FIONREAD:
		*(int *)data = ifsq_poll_pktlen(
		    ifq_get_subq_default(&ifp->if_snd));
		break;

	case FIOSETOWN:
		error = fsetown(*(int *)data, &sc->tun_sigio);
		break;

	case FIOGETOWN:
		*(int *)data = fgetown(&sc->tun_sigio);
		break;

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		error = fsetown(-(*(int *)data), &sc->tun_sigio);
		break;

	/* This is deprecated, FIOGETOWN should be used instead. */
	case TIOCGPGRP:
		*(int *)data = -fgetown(&sc->tun_sigio);
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
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
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp = sc->tun_ifp;
	struct ifaltq_subque *ifsq = ifq_get_subq_default(&ifp->if_snd);
	struct mbuf *m0;
	int error=0, len;

	TUNDEBUG(ifp, "read\n");
	if ((sc->tun_flags & TUN_READY) != TUN_READY) {
		TUNDEBUG(ifp, "not ready, flags = 0x%x\n", sc->tun_flags);
		return (EHOSTDOWN);
	}

	sc->tun_flags &= ~TUN_RWAIT;

	ifnet_serialize_all(ifp);

	while ((m0 = ifsq_dequeue(ifsq)) == NULL) {
		if (ap->a_ioflag & IO_NDELAY) {
			ifnet_deserialize_all(ifp);
			return (EWOULDBLOCK);
		}
		sc->tun_flags |= TUN_RWAIT;
		ifnet_deserialize_all(ifp);
		if ((error = tsleep(sc, PCATCH, "tunread", 0)) != 0)
			return (error);
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
		TUNDEBUG(ifp, "dropping mbuf\n");
		m_freem(m0);
	}
	return (error);
}

/*
 * the ops write interface - an atomic write is a packet - or else!
 */
static	int
tunwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp = sc->tun_ifp;
	struct mbuf *top, **mp, *m;
	size_t tlen;
	uint32_t family, mru;
	int error = 0;
	int isr;

	TUNDEBUG(ifp, "tunwrite\n");

	if (uio->uio_resid == 0)
		return (0);

	mru = TUNMRU;
	if (sc->tun_flags & TUN_IFHEAD)
		mru += sizeof(family);
	if (uio->uio_resid > mru) {
		TUNDEBUG(ifp, "len = %zd!\n", uio->uio_resid);
		return (EIO);
	}

	/* get a header mbuf */
	MGETHDR(m, M_WAITOK, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	tlen = uio->uio_resid;
	top = NULL;
	mp = &top;
	while (error == 0 && uio->uio_resid > 0) {
		m->m_len = (int)szmin(MHLEN, uio->uio_resid);
		error = uiomove(mtod(m, caddr_t), (size_t)m->m_len, uio);
		*mp = m;
		mp = &m->m_next;
		if (uio->uio_resid > 0) {
			MGET(m, M_WAITOK, MT_DATA);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
		}
	}
	if (error) {
		if (top)
			m_freem(top);
		IFNET_STAT_INC(ifp, ierrors, 1);
		return (error);
	}

	top->m_pkthdr.len = (int)tlen;
	top->m_pkthdr.rcvif = ifp;

	if (ifp->if_bpf) {
		bpf_gettoken();

		if (ifp->if_bpf) {
			if (sc->tun_flags & TUN_IFHEAD) {
				/*
				 * Conveniently, we already have a 4-byte
				 * address family prepended to our packet !
				 * Inconveniently, it's in the wrong byte
				 * order !
				 */
				if ((top = m_pullup(top, sizeof(family)))
				    == NULL) {
					bpf_reltoken();
					return (ENOBUFS);
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

	if (sc->tun_flags & TUN_IFHEAD) {
		if (top->m_len < sizeof(family) &&
		    (top = m_pullup(top, sizeof(family))) == NULL)
				return (ENOBUFS);
		family = ntohl(*mtod(top, u_int32_t *));
		m_adj(top, sizeof(family));
	} else {
		family = AF_INET;
	}

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


/*
 * tunkqfilter - support for the kevent() system call.
 */
static int
tunkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp = sc->tun_ifp;
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;
	ifnet_serialize_all(ifp);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &tun_read_filtops;
		kn->kn_hook = (caddr_t)sc;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &tun_write_filtops;
		kn->kn_hook = (caddr_t)sc;
		break;
	default:
		ifnet_deserialize_all(ifp);
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &sc->tun_rkq.ki_note;
	knote_insert(klist, kn);
	ifnet_deserialize_all(ifp);

	return (0);
}

static void
tun_filter_detach(struct knote *kn)
{
	struct tun_softc *sc = (struct tun_softc *)kn->kn_hook;
	struct klist *klist = &sc->tun_rkq.ki_note;

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
	struct tun_softc *sc = (struct tun_softc *)kn->kn_hook;
	struct ifnet *ifp = sc->tun_ifp;
	int ready = 0;

	ifnet_serialize_all(ifp);
	if (!ifsq_is_empty(ifq_get_subq_default(&ifp->if_snd)))
		ready = 1;
	ifnet_deserialize_all(ifp);

	return (ready);
}
