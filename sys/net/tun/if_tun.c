/*
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 * Copyright (c) 2026 The DragonFly Project
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
 * $NetBSD: if_tun.c,v 1.14 1994/06/29 06:36:25 cgd Exp $
 * $FreeBSD: src/sys/net/if_tun.c,v 1.74.2.8 2002/02/13 00:43:11 dillon Exp $
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/caps.h>
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
static struct tun_softc *tuncreate(cdev_t);
static void		tundestroy(struct tun_softc *sc);

/* clone */
static int		tun_clone_create(struct if_clone *, int,
					 caddr_t, caddr_t);
static int		tun_clone_destroy(struct ifnet *);

/* network interface */
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
#define TUN_BITMAP	&DEVFS_CLONE_BITMAP(tun)

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
		dev = make_autoclone_dev(&tun_ops, TUN_BITMAP, tunclone,
					 UID_ROOT, GID_WHEEL, 0600, TUN);

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
		destroy_autoclone_dev(dev, TUN_BITMAP);
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

/*
 * Create a new interface and the /dev/tunN per-interface device.
 *
 * Called by devfs when /dev/tun is opened.
 */
static int
tunclone(struct dev_clone_args *ap)
{
	char ifname[IFNAMSIZ];
	int unit, error;

	error = caps_priv_check(ap->a_cred, SYSCAP_RESTRICTEDROOT);
	if (error != 0)
		return (error);

	unit = devfs_clone_bitmap_get(TUN_BITMAP, 0);
	if (unit < 0)
		return (ENOSPC);

	ksnprintf(ifname, sizeof(ifname), "%s%d", TUN, unit);
	ap->a_dev = make_only_dev(&tun_ops, unit, UID_ROOT, GID_WHEEL,
				  0600, "%s", ifname);

	/*
	 * Use the if_clone framework to create cloned device/interface,
	 * so the two creation methods (opening /dev/tun; ifconfig create)
	 * are consistent and can be mixed.
	 *
	 * Need to pass the cdev_t because the device created by
	 * 'make_only_dev()' doesn't appear in '/dev' yet so that it can't
	 * be found by 'devfs_find_device_by_name()' in 'tun_clone_create()'.
	 */
	error = if_clone_create(ifname, sizeof(ifname), NULL,
				(caddr_t)ap->a_dev);
	if (error != 0) {
		destroy_dev(ap->a_dev);
		ap->a_dev = NULL;
		devfs_clone_bitmap_put(TUN_BITMAP, unit);
	}

	return (error);
}

static struct tun_softc *
tuncreate(cdev_t dev)
{
	struct tun_softc *sc;
	struct ifnet *ifp;
	int unit = minor(dev);

	ifp = if_alloc(IFT_PPP);
	if (ifp == NULL) {
		kprintf("%s: if_alloc() failed for %s%d\n",
			__func__, TUN, unit);
		return (NULL);
	}

	sc = kmalloc(sizeof(*sc), M_TUN, M_WAITOK | M_ZERO);
	sc->tun_dev = dev;
	sc->tun_ifp = ifp;
	sc->tun_flags = TUN_INITED;

	dev->si_drv1 = sc;
	reference_dev(dev);  /* device association */

	if_initname(ifp, TUN, unit);
	ifp->if_mtu = TUNMTU;
	ifp->if_ioctl = tunifioctl;
	ifp->if_output = tunifoutput;
	ifp->if_start = tunifstart;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_softc = sc;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	if_attach(ifp, NULL);
	bpfattach(ifp, DLT_NULL, sizeof(uint32_t));

	SLIST_INSERT_HEAD(&tun_listhead, sc, tun_link);
	TUNDEBUG(ifp, "created, minor = %#x\n", unit);
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
	devfs_clone_bitmap_put(TUN_BITMAP, unit);

	SLIST_REMOVE(&tun_listhead, sc, tun_softc, tun_link);
	kfree(sc, M_TUN);
}

/*
 * Open the per-interface device node /dev/tunN.
 *
 * Access is controlled by the file permissions of the device node.
 */
static int
tunopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tun_softc *sc;

	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENXIO);
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

	flags = TUN_CLONE;
	if (dev == NULL) {
		/* The interface clone case. */
		flags |= TUN_MANUALMAKE;
		if (devfs_clone_bitmap_chk(TUN_BITMAP, unit)) {
			kprintf("%s: bitmap out-of-sync (unit=%d allocated)\n",
				__func__, unit);
			return (EEXIST);
		}
		devfs_clone_bitmap_set(TUN_BITMAP, unit);
		dev = make_dev(&tun_ops, unit, UID_ROOT, GID_WHEEL,
			       0600, "%s%d", TUN, unit);
	}

	sc = tuncreate(dev);
	if (sc == NULL) {
		if (flags & TUN_MANUALMAKE) {
			destroy_dev(dev);
			devfs_clone_bitmap_put(TUN_BITMAP, unit);
		}
		return (ENOMEM);
	}

	sc->tun_flags |= flags;

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
	size_t len;
	int error = 0;

	switch (cmd) {
	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		if (sc->tun_pid) {
			len = strlen(ifs->ascii);
			ksnprintf(ifs->ascii + len, IFSTATMAX - len,
				  "\tOpened by PID %d\n", sc->tun_pid);
		}
		break;
	case SIOCSIFADDR:
	case SIOCSIFDSTADDR:
		ifp->if_flags |= IFF_UP | IFF_RUNNING;
		/* Everything else is done at a higher level. */
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
			wakeup(sc);
		}

		if ((sc->tun_flags & TUN_ASYNC) && sc->tun_sigio != NULL) {
			get_mplock();
			pgsigio(sc->tun_sigio, SIGIO, 0);
			rel_mplock();
		}

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
	struct altq_pktattr pktattr;
	uint32_t family;
	size_t len;
	int error;

	TUNDEBUG(ifp, "output\n");

	if ((ifp->if_flags & IFF_UP) == 0 ||
	    (sc->tun_flags & TUN_READY) != TUN_READY)
	{
		TUNDEBUG(ifp, "not up or ready, flags = 0x%x\n", sc->tun_flags);
		m_freem(m0);
		return (EHOSTDOWN);
	}

	/*
	 * BPF write needs to be handled specially: the address family is in
	 * the first 4 bytes of the packet.
	 *
	 * The packet comes from userland, so it may be shorter than that or
	 * fragmented. Handle both cases.
	 */
	if (dst->sa_family == AF_UNSPEC) {
		if (m0->m_pkthdr.len < (int)sizeof(uint32_t)) {
			m_freem(m0);
			IFNET_STAT_INC(ifp, oerrors, 1);
			return (EINVAL);
		}
		if (m0->m_len < (int)sizeof(uint32_t) &&
		    (m0 = m_pullup(m0, sizeof(uint32_t))) == NULL)
		{
			IFNET_STAT_INC(ifp, oerrors, 1);
			return (ENOBUFS);
		}

		dst->sa_family = (sa_family_t)*mtod(m0, uint32_t *);
		m_adj(m0, sizeof(uint32_t));
	}

	/* Prepend the address family as a 4-byte field. */
	family = (uint32_t)dst->sa_family;
	bpf_ptap(ifp->if_bpf, m0, &family, sizeof(family));

	/*
	 * Classify the packet after striping the BPF header (so it sees
	 * the IP header) and before prepending LMODE/IFHEAD header.
	 */
	ifq_classify(&ifp->if_snd, m0, dst->sa_family, &pktattr);

	if (sc->tun_flags & TUN_LMODE) {
		/* Allocate space for sockaddr */
		M_PREPEND(m0, dst->sa_len, M_NOWAIT);
		if (m0 == NULL){
			IFNET_STAT_INC(ifp, oerrors, 1);
			return (ENOBUFS);
		} else {
			bcopy(dst, m0->m_data, dst->sa_len);
		}
	}

	if (sc->tun_flags & TUN_IFHEAD) {
		/* Prepend the address family */
		M_PREPEND(m0, sizeof(uint32_t), M_NOWAIT);
		if (m0 == NULL){
			IFNET_STAT_INC(ifp, oerrors, 1);
			return (ENOBUFS);
		} else {
			*(uint32_t *)m0->m_data = htonl(dst->sa_family);
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

	len = m0->m_pkthdr.len;
	error = ifq_handoff(ifp, m0, &pktattr);
	if (error != 0) {
		IFNET_STAT_INC(ifp, collisions, 1);
		return (error);
	}

	IFNET_STAT_INC(ifp, opackets, 1);
	IFNET_STAT_INC(ifp, obytes, len);

	if (sc->tun_flags & TUN_RWAIT) {
		sc->tun_flags &= ~TUN_RWAIT;
		wakeup(sc);
	}

	if ((sc->tun_flags & TUN_ASYNC) && sc->tun_sigio != NULL) {
		get_mplock();
		pgsigio(sc->tun_sigio, SIGIO, 0);
		rel_mplock();
	}

	return (0);
}

static int
tunifoutput(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst,
	    struct rtentry *rt)
{
	struct tun_softc *sc = ifp->if_softc;
	int error;

	ifnet_serialize_all(ifp);
	error = tunifoutput_serialized(ifp, m0, dst, rt);
	ifnet_deserialize_all(ifp);

	if (error == 0) {
		KNOTE(&sc->tun_rkq.ki_note, 0);
	}

	return (error);
}


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
		if (ifp->if_type != tunp->type) {
			error = EPROTOTYPE;
			break;
		}
		if (tunp->mtu < IF_MINMTU || tunp->mtu > IF_MAXMTU) {
			error = EINVAL;
			break;
		}
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
		error = caps_priv_check(ap->a_cred, SYSCAP_RESTRICTEDROOT);
		if (error == 0)
			tundebug = *(int *)data;
		break;

	case TUNGDEBUG:
		error = caps_priv_check(ap->a_cred, SYSCAP_RESTRICTEDROOT);
		if (error == 0)
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
		if (ifp->if_flags & IFF_UP) {
			error = EBUSY;
			break;
		}
		switch (*(int *)data & ~IFF_MULTICAST) {
		case IFF_POINTOPOINT:
		case IFF_BROADCAST:
			ifp->if_flags &= ~(IFF_BROADCAST | IFF_POINTOPOINT |
					   IFF_MULTICAST);
			ifp->if_flags |= *(int *)data;
			break;
		default:
			error = EINVAL;
			break;
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
static int
tunread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp = sc->tun_ifp;
	struct ifaltq_subque *ifsq = ifq_get_subq_default(&ifp->if_snd);
	struct mbuf *m0;
	size_t len;
	int error;

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

	error = 0;
	while (m0 != NULL && uio->uio_resid > 0 && error == 0) {
		len = szmin(uio->uio_resid, (size_t)m0->m_len);
		if (len != 0)
			error = uiomove(mtod(m0, caddr_t), len, uio);
		m0 = m_free(m0);
	}

	if (m0 != NULL) {
		TUNDEBUG(ifp, "dropping mbuf\n");
		m_freem(m0);
	}
	return (error);
}

/*
 * the ops write interface - an atomic write is a packet - or else!
 */
static int
tunwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tun_softc *sc = dev->si_drv1;
	struct ifnet *ifp = sc->tun_ifp;
	struct mbuf *top;
	size_t mru;
	uint32_t family;
	bool ifhead;
	int isr;

	TUNDEBUG(ifp, "tunwrite\n");

	if (uio->uio_resid == 0)
		return (0);

	ifhead = ((sc->tun_flags & TUN_IFHEAD) != 0);

	mru = ifhead ? (TUNMRU + sizeof(family)) : TUNMRU;
	if (uio->uio_resid > mru) {
		TUNDEBUG(ifp, "packet too big: len=%zu, mru=%zu\n",
			 uio->uio_resid, mru);
		return (EIO);
	}
	if (ifhead && uio->uio_resid <= sizeof(family)) {
		TUNDEBUG(ifp, "short packet: len=%zu\n", uio->uio_resid);
		return (EINVAL);
	}

	top = m_uiomove(uio);
	if (top == NULL) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		return (EFAULT);
	}

	top->m_pkthdr.rcvif = ifp;

	if (ifhead) {
		if (top->m_len < sizeof(family) &&
		    (top = m_pullup(top, sizeof(family))) == NULL)
			return (ENOBUFS);
		family = ntohl(*mtod(top, uint32_t *));
		m_adj(top, sizeof(family));
	} else {
		family = AF_INET;
	}

	bpf_ptap(ifp->if_bpf, top, &family, sizeof(family));

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
		m_freem(top);
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
		ap->a_result = EOPNOTSUPP;
		break;
	}

	if (ap->a_result == 0) {
		knote_insert(&sc->tun_rkq.ki_note, kn);
	}

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
	if (!ifsq_is_empty(ifq_get_subq_default(&ifp->if_snd))) {
		if ((kn->kn_sfflags & NOTE_HUPONLY) == 0)
			ready = 1;
	}
	ifnet_deserialize_all(ifp);

	return (ready);
}
