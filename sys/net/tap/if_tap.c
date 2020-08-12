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
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/ttycom.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/serialize.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>
#include <sys/devfs.h>
#include <sys/queue.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/route.h>

#include <netinet/in.h>

#include "if_tapvar.h"
#include "if_tap.h"

#define TAP_IFFLAGS	(IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST)

#define TAP		"tap"
#define TAPDEBUG	if (tapdebug) if_printf

/* module */
static int		tapmodevent(module_t, int, void *);

/* device */
static struct tap_softc *tapcreate(cdev_t, int);
static void		tapdestroy(struct tap_softc *);

/* clone */
static int		tap_clone_create(struct if_clone *, int,
					 caddr_t, caddr_t);
static int		tap_clone_destroy(struct ifnet *);

/* network interface */
static void		tapifinit(void *);
static void		tapifstart(struct ifnet *, struct ifaltq_subque *);
static int		tapifioctl(struct ifnet *, u_long, caddr_t,
				   struct ucred *);
static void		tapifstop(struct tap_softc *, int);
static void		tapifflags(struct tap_softc *);

/* character device */
static d_open_t		tapopen;
static d_clone_t	tapclone;
static d_close_t	tapclose;
static d_read_t		tapread;
static d_write_t	tapwrite;
static d_ioctl_t	tapioctl;
static d_kqfilter_t	tapkqfilter;

static struct dev_ops	tap_ops = {
	{ TAP, 0, 0 },
	.d_open =	tapopen,
	.d_close =	tapclose,
	.d_read =	tapread,
	.d_write =	tapwrite,
	.d_ioctl =	tapioctl,
	.d_kqfilter =	tapkqfilter
};

/* kqueue support */
static void		tap_filter_detach(struct knote *);
static int		tap_filter_read(struct knote *, long);
static int		tap_filter_write(struct knote *, long);

static struct filterops tapread_filtops = {
	FILTEROP_ISFD,
	NULL,
	tap_filter_detach,
	tap_filter_read
};
static struct filterops tapwrite_filtops = {
	FILTEROP_ISFD,
	NULL,
	tap_filter_detach,
	tap_filter_write
};

static int		tapdebug = 0;		/* debug flag */
static int		taprefcnt = 0;		/* module ref. counter */
static int		tapuopen = 0;		/* allow user open() */
static int		tapuponopen = 0;	/* IFF_UP when opened */

static MALLOC_DEFINE(M_TAP, TAP, "Ethernet tunnel interface");

static DEVFS_DEFINE_CLONE_BITMAP(tap);

struct if_clone tap_cloner = IF_CLONE_INITIALIZER(
	TAP, tap_clone_create, tap_clone_destroy, 0, IF_MAXUNIT);

static SLIST_HEAD(,tap_softc) tap_listhead =
	SLIST_HEAD_INITIALIZER(&tap_listhead);

SYSCTL_INT(_debug, OID_AUTO, if_tap_debug, CTLFLAG_RW, &tapdebug, 0, "");
SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, OID_AUTO, tap, CTLFLAG_RW, 0,
	    "Ethernet tunnel software network interface");
SYSCTL_INT(_net_link_tap, OID_AUTO, user_open, CTLFLAG_RW, &tapuopen, 0,
	   "Allow user to open /dev/tap (based on node permissions)");
SYSCTL_INT(_net_link_tap, OID_AUTO, up_on_open, CTLFLAG_RW, &tapuponopen, 0,
	   "Bring interface up when /dev/tap is opened");
SYSCTL_INT(_net_link_tap, OID_AUTO, debug, CTLFLAG_RW, &tapdebug, 0, "");
SYSCTL_INT(_net_link_tap, OID_AUTO, refcnt, CTLFLAG_RD, &taprefcnt, 0,
	   "Number of opened devices");

DEV_MODULE(if_tap, tapmodevent, NULL);

/*
 * tapmodevent
 *
 * module event handler
 */
static int
tapmodevent(module_t mod, int type, void *data)
{
	static cdev_t dev = NULL;
	struct tap_softc *sc, *sc_tmp;

	switch (type) {
	case MOD_LOAD:
		dev = make_autoclone_dev(&tap_ops, &DEVFS_CLONE_BITMAP(tap),
					 tapclone, UID_ROOT, GID_WHEEL,
					 0600, TAP);
		SLIST_INIT(&tap_listhead);
		if_clone_attach(&tap_cloner);
		break;

	case MOD_UNLOAD:
		if (taprefcnt > 0)
			return (EBUSY);

		if_clone_detach(&tap_cloner);

		SLIST_FOREACH_MUTABLE(sc, &tap_listhead, tap_link, sc_tmp)
			tapdestroy(sc);

		dev_ops_remove_all(&tap_ops);
		destroy_autoclone_dev(dev, &DEVFS_CLONE_BITMAP(tap));
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}


/*
 * Create or clone an interface
 */
static struct tap_softc *
tapcreate(cdev_t dev, int flags)
{
	struct tap_softc *sc;
	struct ifnet *ifp;
	uint8_t ether_addr[ETHER_ADDR_LEN];
	int unit = minor(dev);

	sc = kmalloc(sizeof(*sc), M_TAP, M_WAITOK | M_ZERO);
	dev->si_drv1 = sc;
	sc->tap_dev = dev;
	sc->tap_flags |= flags;

	reference_dev(dev); /* device association */

	/* generate fake MAC address: 00 bd xx xx xx unit_no */
	ether_addr[0] = 0x00;
	ether_addr[1] = 0xbd;
	bcopy(&ticks, &ether_addr[2], 3);
	ether_addr[5] = (u_char)unit;

	/* fill the rest and attach interface */
	ifp = &sc->tap_if;
	if_initname(ifp, TAP, unit);
	ifp->if_type = IFT_ETHER;
	ifp->if_init = tapifinit;
	ifp->if_start = tapifstart;
	ifp->if_ioctl = tapifioctl;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = TAP_IFFLAGS;
	ifp->if_softc = sc;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, ether_addr, NULL);

	sc->tap_flags |= TAP_INITED;
	sc->tap_devq.ifq_maxlen = ifqmaxlen;

	SLIST_INSERT_HEAD(&tap_listhead, sc, tap_link);

	TAPDEBUG(ifp, "created, minor = %#x, flags = 0x%x\n",
		 unit, sc->tap_flags);
	return (sc);
}

static struct tap_softc *
tapfind(int unit)
{
	struct tap_softc *sc;

	SLIST_FOREACH(sc, &tap_listhead, tap_link) {
		if (minor(sc->tap_dev) == unit)
			return (sc);
	}
	return (NULL);
}

/*
 * Create a new tap instance via ifconfig.
 */
static int
tap_clone_create(struct if_clone *ifc __unused, int unit,
		 caddr_t params __unused, caddr_t data)
{
	struct tap_softc *sc;
	cdev_t dev = (cdev_t)data;
	int flags;

	if (tapfind(unit) != NULL)
		return (EEXIST);

	if (dev == NULL) {
		if (!devfs_clone_bitmap_chk(&DEVFS_CLONE_BITMAP(tap), unit)) {
			devfs_clone_bitmap_set(&DEVFS_CLONE_BITMAP(tap), unit);
			dev = make_dev(&tap_ops, unit, UID_ROOT, GID_WHEEL,
				       0600, "%s%d", TAP, unit);
		} else {
			dev = devfs_find_device_by_name("%s%d", TAP, unit);
			if (dev == NULL)
				return (ENOENT);
		}
		flags = TAP_MANUALMAKE;
	} else {
		flags = 0;
	}

	if ((sc = tapcreate(dev, flags)) == NULL)
		return (ENOMEM);

	sc->tap_flags |= TAP_CLONE;

	TAPDEBUG(&sc->tap_if, "clone created, minor = %#x, flags = 0x%x\n",
		 minor(sc->tap_dev), sc->tap_flags);

	return (0);
}

/*
 * Open the tap device.
 */
static int
tapopen(struct dev_open_args *ap)
{
	cdev_t dev = NULL;
	struct tap_softc *sc = NULL;
	struct ifnet *ifp = NULL;
	int error;

	if (tapuopen == 0 &&
	    (error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) != 0)
		return (error);

	get_mplock();

	dev = ap->a_head.a_dev;
	sc = dev->si_drv1;
	if (sc == NULL && (sc = tapcreate(dev, 0)) == NULL) {
		rel_mplock();
		return (ENOMEM);
	}
	if (sc->tap_flags & TAP_OPEN) {
		rel_mplock();
		return (EBUSY);
	}

	ifp = &sc->tap_if;
	bcopy(sc->arpcom.ac_enaddr, sc->ether_addr, sizeof(sc->ether_addr));

	if (curthread->td_proc)
		fsetown(curthread->td_proc->p_pid, &sc->tap_sigtd);
	sc->tap_flags |= TAP_OPEN;
	taprefcnt++;

	if (tapuponopen && (ifp->if_flags & IFF_UP) == 0) {
		crit_enter();
		if_up(ifp);
		crit_exit();

		ifnet_serialize_all(ifp);
		tapifflags(sc);
		ifnet_deserialize_all(ifp);

		sc->tap_flags |= TAP_CLOSEDOWN;
	}

	TAPDEBUG(ifp, "opened, minor = %#x. Module refcnt = %d\n",
		 minor(dev), taprefcnt);

	rel_mplock();
	return (0);
}

static int
tapclone(struct dev_clone_args *ap)
{
	char ifname[IFNAMSIZ];
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(tap), 0);
	ksnprintf(ifname, IFNAMSIZ, "%s%d", TAP, unit);
	ap->a_dev = make_only_dev(&tap_ops, unit, UID_ROOT, GID_WHEEL,
				  0600, "%s", ifname);

	/*
	 * Use the if_clone framework to create cloned device/interface,
	 * so the two clone methods (autoclone device /dev/tap; ifconfig
	 * clone) are consistent and can be mix used.
	 *
	 * Need to pass the cdev_t because the device created by
	 * 'make_only_dev()' doesn't appear in '/dev' yet so that it can't
	 * be found by 'devfs_find_device_by_name()' in 'tap_clone_create()'.
	 */
	return (if_clone_create(ifname, IFNAMSIZ, NULL, (caddr_t)ap->a_dev));
}

/*
 * Close the tap device: bring the interface down & delete routing info.
 */
static int
tapclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tap_softc *sc = dev->si_drv1;
	struct ifnet *ifp;
	int unit = minor(dev);
	int clear_flags = 0;
	char ifname[IFNAMSIZ];

	KASSERT(sc != NULL,
		("try closing the already destroyed %s%d", TAP, unit));
	ifp = &sc->tap_if;

	get_mplock();

	/* Junk all pending output */
	ifq_purge_all(&ifp->if_snd);

	/*
	 * If the interface is not cloned, we always bring it down.
	 *
	 * If the interface is cloned, then we bring it down during
	 * closing only if it was brought up during opening.
	 */
	if ((sc->tap_flags & TAP_CLONE) == 0 ||
	    (sc->tap_flags & TAP_CLOSEDOWN)) {
		if (ifp->if_flags & IFF_UP)
			if_down(ifp);
		clear_flags = 1;
	}
	ifnet_serialize_all(ifp);
	tapifstop(sc, clear_flags);
	ifnet_deserialize_all(ifp);

	if ((sc->tap_flags & TAP_MANUALMAKE) == 0) {
		if_purgeaddrs_nolink(ifp);

		EVENTHANDLER_INVOKE(ifnet_detach_event, ifp);

		/* Announce the departure of the interface. */
		rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
	}

	funsetown(&sc->tap_sigio);
	sc->tap_sigio = NULL;
	KNOTE(&sc->tap_rkq.ki_note, 0);

	sc->tap_flags &= ~TAP_OPEN;
	funsetown(&sc->tap_sigtd);
	sc->tap_sigtd = NULL;

	taprefcnt--;
	if (taprefcnt < 0) {
		taprefcnt = 0;
		if_printf(ifp, ". Module refcnt = %d is out of sync! "
			  "Force refcnt to be 0.\n", taprefcnt);
	}

	TAPDEBUG(ifp, "closed, minor = %#x. Module refcnt = %d\n",
		 unit, taprefcnt);

	/* Only auto-destroy if the interface was not manually created. */
	if ((sc->tap_flags & TAP_MANUALMAKE) == 0) {
		ksnprintf(ifname, IFNAMSIZ, "%s%d", TAP, unit);
		if_clone_destroy(ifname);
	}

	rel_mplock();
	return (0);
}


/*
 * Destroy a tap instance: the interface and the associated device.
 */
static void
tapdestroy(struct tap_softc *sc)
{
	struct ifnet *ifp = &sc->tap_if;
	cdev_t dev = sc->tap_dev;
	int unit = minor(dev);

	TAPDEBUG(ifp, "destroyed, minor = %#x. Module refcnt = %d\n",
		 unit, taprefcnt);

	ifnet_serialize_all(ifp);
	tapifstop(sc, 1);
	ifnet_deserialize_all(ifp);
	ether_ifdetach(ifp);

	sc->tap_dev = NULL;
	dev->si_drv1 = NULL;
	release_dev(dev); /* device disassociation */

	/* Also destroy the cloned device */
	destroy_dev(dev);
	devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(tap), unit);

	SLIST_REMOVE(&tap_listhead, sc, tap_softc, tap_link);
	kfree(sc, M_TAP);
}


/*
 * Destroy a tap instance that is created via ifconfig.
 */
static int
tap_clone_destroy(struct ifnet *ifp)
{
	struct tap_softc *sc = ifp->if_softc;

	if (sc->tap_flags & TAP_OPEN)
		return (EBUSY);
	if ((sc->tap_flags & TAP_CLONE) == 0)
		return (EINVAL);

	TAPDEBUG(ifp, "clone destroyed, minor = %#x, flags = 0x%x\n",
		 minor(sc->tap_dev), sc->tap_flags);
	tapdestroy(sc);

	return (0);
}


/*
 * tapifinit
 *
 * Network interface initialization function (called with if serializer held)
 *
 * MPSAFE
 */
static void
tapifinit(void *xtp)
{
	struct tap_softc *sc = xtp;
	struct ifnet *ifp = &sc->tap_if;
	struct ifaltq_subque *ifsq = ifq_get_subq_default(&ifp->if_snd);

	TAPDEBUG(ifp, "initializing, minor = %#x, flags = 0x%x\n",
		 minor(sc->tap_dev), sc->tap_flags);

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	tapifstop(sc, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifsq_clr_oactive(ifsq);

	/* attempt to start output */
	tapifstart(ifp, ifsq);
}

static void
tapifflags(struct tap_softc *sc)
{
	struct ifnet *ifp = &sc->tap_if;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (ifp->if_flags & IFF_UP) {
		if ((ifp->if_flags & IFF_RUNNING) == 0)
			tapifinit(sc);
	} else {
		tapifstop(sc, 1);
	}
}

/*
 * tapifioctl
 *
 * Process an ioctl request on network interface (called with if serializer
 * held).
 *
 * MPSAFE
 */
static int
tapifioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct tap_softc *sc = ifp->if_softc;
	struct ifreq *ifr = NULL;
	struct ifstat *ifs = NULL;
	struct ifmediareq *ifmr = NULL;
	int error = 0;
	int dummy;

	switch (cmd) {
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSIFMTU:
		ifr = (struct ifreq *)data;
		ifp->if_mtu = ifr->ifr_mtu;
		TAPDEBUG(ifp, "mtu set\n");
		break;

	case SIOCSIFADDR:
	case SIOCGIFADDR:
		error = ether_ioctl(ifp, cmd, data);
		break;

	case SIOCSIFFLAGS:
		tapifflags(sc);
		break;

	case SIOCGIFMEDIA:
		/*
		 * The bridge code needs this when running the
		 * spanning tree protocol.
		 */
		ifmr = (struct ifmediareq *)data;
		dummy = ifmr->ifm_count;
		ifmr->ifm_count = 1;
		ifmr->ifm_status = IFM_AVALID;
		ifmr->ifm_active = IFM_ETHER;
		if (sc->tap_flags & TAP_OPEN)
			ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_current = ifmr->ifm_active;
		if (dummy >= 1) {
			int media = IFM_ETHER;
			error = copyout(&media, ifmr->ifm_ulist, sizeof(int));
		}
		break;

	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		dummy = strlen(ifs->ascii);
		if ((sc->tap_flags & TAP_OPEN) && dummy < sizeof(ifs->ascii)) {
			if (sc->tap_sigtd && sc->tap_sigtd->sio_proc) {
				ksnprintf(ifs->ascii + dummy,
				    sizeof(ifs->ascii) - dummy,
				    "\tOpened by pid %d\n",
				    (int)sc->tap_sigtd->sio_proc->p_pid);
			} else {
				ksnprintf(ifs->ascii + dummy,
				    sizeof(ifs->ascii) - dummy,
				    "\tOpened by <unknown>\n");
			}
		}
		break;

	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * tapifstart
 *
 * Queue packets from higher level ready to put out (called with if serializer
 * held)
 *
 * MPSAFE
 */
static void
tapifstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct tap_softc *sc = ifp->if_softc;
	struct ifqueue *ifq;
	struct mbuf *m;
	int has_data = 0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	TAPDEBUG(ifp, "starting, minor = %#x\n", minor(sc->tap_dev));

	if ((sc->tap_flags & TAP_READY) != TAP_READY) {
		TAPDEBUG(ifp, "not ready, minor = %#x, flags = 0x%x\n",
			 minor(sc->tap_dev), sc->tap_flags);
		ifsq_purge(ifsq);
		return;
	}

	ifsq_set_oactive(ifsq);

	ifq = &sc->tap_devq;
	while ((m = ifsq_dequeue(ifsq)) != NULL) {
		if (IF_QFULL(ifq)) {
			IF_DROP(ifq);
			IFNET_STAT_INC(ifp, oerrors, 1);
			m_freem(m);
		} else {
			IF_ENQUEUE(ifq, m);
			IFNET_STAT_INC(ifp, opackets, 1);
			has_data = 1;
		}
	}

	if (has_data) {
		if (sc->tap_flags & TAP_RWAIT) {
			sc->tap_flags &= ~TAP_RWAIT;
			wakeup((caddr_t)sc);
		}

		KNOTE(&sc->tap_rkq.ki_note, 0);

		if ((sc->tap_flags & TAP_ASYNC) && (sc->tap_sigio != NULL)) {
			get_mplock();
			pgsigio(sc->tap_sigio, SIGIO, 0);
			rel_mplock();
		}
	}

	ifsq_clr_oactive(ifsq);
}

static void
tapifstop(struct tap_softc *sc, int clear_flags)
{
	struct ifnet *ifp = &sc->tap_if;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	IF_DRAIN(&sc->tap_devq);
	sc->tap_flags &= ~TAP_CLOSEDOWN;
	if (clear_flags) {
		ifp->if_flags &= ~IFF_RUNNING;
		ifsq_clr_oactive(ifq_get_subq_default(&ifp->if_snd));
	}
}

/*
 * tapioctl
 *
 * The ops interface is now pretty minimal.  Called via fileops with nothing
 * held.
 *
 * MPSAFE
 */
static int
tapioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct tap_softc *sc = dev->si_drv1;
	struct ifnet *ifp = &sc->tap_if;
	struct ifreq *ifr;
	struct tapinfo *tapp = NULL;
	struct mbuf *mb;
	int error;

	ifnet_serialize_all(ifp);
	error = 0;

	switch (ap->a_cmd) {
	case TAPSIFINFO:
		tapp = (struct tapinfo *)data;
		if (ifp->if_type != tapp->type)
			return (EPROTOTYPE);
		ifp->if_mtu = tapp->mtu;
		ifp->if_baudrate = tapp->baudrate;
		break;

	case TAPGIFINFO:
		tapp = (struct tapinfo *)data;
		tapp->mtu = ifp->if_mtu;
		tapp->type = ifp->if_type;
		tapp->baudrate = ifp->if_baudrate;
		break;

	case TAPGIFNAME:
		ifr = (struct ifreq *)data;
		strlcpy(ifr->ifr_name, ifp->if_xname, IFNAMSIZ);
		break;

	case TAPSDEBUG:
		tapdebug = *(int *)data;
		break;

	case TAPGDEBUG:
		*(int *)data = tapdebug;
		break;

	case FIOASYNC:
		if (*(int *)data)
			sc->tap_flags |= TAP_ASYNC;
		else
			sc->tap_flags &= ~TAP_ASYNC;
		break;

	case FIONREAD:
		/* Take a look at devq first */
		IF_POLL(&sc->tap_devq, mb);
		if (mb != NULL) {
			*(int *)data = 0;
			for(; mb != NULL; mb = mb->m_next)
				*(int *)data += mb->m_len;
		} else {
			*(int *)data = ifsq_poll_pktlen(
			    ifq_get_subq_default(&ifp->if_snd));
		}
		break;

	case FIOSETOWN:
		error = fsetown(*(int *)data, &sc->tap_sigio);
		if (error == 0)
			error = fsetown(*(int *)data, &sc->tap_sigtd);
		break;

	case FIOGETOWN:
		*(int *)data = fgetown(&sc->tap_sigio);
		break;

	/* this is deprecated, FIOSETOWN should be used instead */
	case TIOCSPGRP:
		error = fsetown(-(*(int *)data), &sc->tap_sigio);
		break;

	/* this is deprecated, FIOGETOWN should be used instead */
	case TIOCGPGRP:
		*(int *)data = -fgetown(&sc->tap_sigio);
		break;

	/*
	 * Support basic control of the network interface via the device file.
	 * e.g., vke(4) currently uses the 'SIOCGIFADDR' ioctl.
	 */

	case SIOCGIFFLAGS:
		bcopy(&ifp->if_flags, data, sizeof(ifp->if_flags));
		break;

	case SIOCGIFADDR:
		bcopy(sc->ether_addr, data, sizeof(sc->ether_addr));
		break;

	case SIOCSIFADDR:
		bcopy(data, sc->ether_addr, sizeof(sc->ether_addr));
		break;

	default:
		error = ENOTTY;
		break;
	}

	ifnet_deserialize_all(ifp);
	return (error);
}


/*
 * tapread
 *
 * The ops read interface - reads a packet at a time, or at
 * least as much of a packet as can be read.
 *
 * Called from the fileops interface with nothing held.
 *
 * MPSAFE
 */
static int
tapread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tap_softc *sc = dev->si_drv1;
	struct ifnet *ifp = &sc->tap_if;
	struct mbuf *m0 = NULL;
	int error = 0, len;

	TAPDEBUG(ifp, "reading, minor = %#x\n", minor(dev));

	if ((sc->tap_flags & TAP_READY) != TAP_READY) {
		TAPDEBUG(ifp, "not ready, minor = %#x, flags = 0x%x\n",
			 minor(dev), sc->tap_flags);

		return (EHOSTDOWN);
	}

	sc->tap_flags &= ~TAP_RWAIT;

	/* sleep until we get a packet */
	do {
		ifnet_serialize_all(ifp);
		IF_DEQUEUE(&sc->tap_devq, m0);
		if (m0 == NULL) {
			if (ap->a_ioflag & IO_NDELAY) {
				ifnet_deserialize_all(ifp);
				return (EWOULDBLOCK);
			}
			sc->tap_flags |= TAP_RWAIT;
			tsleep_interlock(sc, PCATCH);
			ifnet_deserialize_all(ifp);
			error = tsleep(sc, PCATCH | PINTERLOCKED, "taprd", 0);
			if (error)
				return (error);
		} else {
			ifnet_deserialize_all(ifp);
		}
	} while (m0 == NULL);

	BPF_MTAP(ifp, m0);

	/* xfer packet to user space */
	while ((m0 != NULL) && (uio->uio_resid > 0) && (error == 0)) {
		len = (int)szmin(uio->uio_resid, m0->m_len);
		if (len == 0)
			break;

		error = uiomove(mtod(m0, caddr_t), (size_t)len, uio);
		m0 = m_free(m0);
	}

	if (m0 != NULL) {
		TAPDEBUG(ifp, "dropping mbuf, minor = %#x\n", minor(dev));
		m_freem(m0);
	}

	return (error);
}

/*
 * tapwrite
 *
 * The ops write interface - an atomic write is a packet - or else!
 *
 * Called from the fileops interface with nothing held.
 *
 * MPSAFE
 */
static int
tapwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct tap_softc *sc = dev->si_drv1;
	struct ifnet *ifp = &sc->tap_if;
	struct mbuf *top = NULL, **mp = NULL, *m = NULL;
	int error;
	size_t tlen, mlen;

	TAPDEBUG(ifp, "writing, minor = %#x\n", minor(dev));

	if ((sc->tap_flags & TAP_READY) != TAP_READY) {
		TAPDEBUG(ifp, "not ready, minor = %#x, flags = 0x%x\n",
			 minor(dev), sc->tap_flags);
		return (EHOSTDOWN);
	}

	if (uio->uio_resid == 0)
		return (0);

	if (uio->uio_resid > TAPMRU) {
		TAPDEBUG(ifp, "invalid packet len = %zu, minor = %#x\n",
			 uio->uio_resid, minor(dev));

		return (EIO);
	}
	tlen = uio->uio_resid;

	/* get a header mbuf */
	MGETHDR(m, M_WAITOK, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	mlen = MHLEN;

	top = NULL;
	mp = &top;
	error = 0;

	while (error == 0 && uio->uio_resid > 0) {
		m->m_len = (int)szmin(mlen, uio->uio_resid);
		error = uiomove(mtod(m, caddr_t), (size_t)m->m_len, uio);
		*mp = m;
		mp = &m->m_next;
		if (uio->uio_resid > 0) {
			MGET(m, M_WAITOK, MT_DATA);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
			mlen = MLEN;
		}
	}
	if (error) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		if (top)
			m_freem(top);
		return (error);
	}

	top->m_pkthdr.len = (int)tlen;
	top->m_pkthdr.rcvif = ifp;

	/*
	 * Ethernet bridge and bpf are handled in ether_input
	 *
	 * adjust mbuf and give packet to the ether_input
	 */
	ifnet_serialize_all(ifp);
	ifp->if_input(ifp, top, NULL, -1);
	IFNET_STAT_INC(ifp, ipackets, 1); /* ibytes are counted in ether_input */
	ifnet_deserialize_all(ifp);

	return (0);
}


/*
 * tapkqfilter - called from the fileops interface with nothing held
 *
 * MPSAFE
 */
static int
tapkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct tap_softc *sc = dev->si_drv1;
	struct klist *list;

	list = &sc->tap_rkq.ki_note;
	ap->a_result =0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &tapread_filtops;
		kn->kn_hook = (void *)sc;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &tapwrite_filtops;
		kn->kn_hook = (void *)sc;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	knote_insert(list, kn);
	return (0);
}

static int
tap_filter_read(struct knote *kn, long hint)
{
	struct tap_softc *sc = (struct tap_softc *)kn->kn_hook;

	if (IF_QEMPTY(&sc->tap_devq) == 0)	/* XXX serializer */
		return (1);
	else
		return (0);
}

static int
tap_filter_write(struct knote *kn, long hint)
{
	/* Always ready for a write */
	return (1);
}

static void
tap_filter_detach(struct knote *kn)
{
	struct tap_softc *sc = (struct tap_softc *)kn->kn_hook;

	knote_remove(&sc->tap_rkq.ki_note, kn);
}
