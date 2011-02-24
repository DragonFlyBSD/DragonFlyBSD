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

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/route.h>
#include <sys/devfs.h>

#include <netinet/in.h>

#include "if_tapvar.h"
#include "if_tap.h"

#define TAP_IFFLAGS	(IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST)

#define TAP_PREALLOCATED_UNITS	4

#define CDEV_NAME	"tap"
#define TAPDEBUG	if (tapdebug) if_printf

#define TAP		"tap"
#define VMNET		"vmnet"
#define VMNET_DEV_MASK	0x00010000

DEVFS_DECLARE_CLONE_BITMAP(tap);

/* module */
static int 		tapmodevent	(module_t, int, void *);

/* device */
static struct tap_softc *tapcreate(int, cdev_t);
static void		tapdestroy(struct tap_softc *);

/* clone */
static int		tap_clone_create(struct if_clone *, int, caddr_t);
static int		tap_clone_destroy(struct ifnet *);


/* network interface */
static void		tapifstart	(struct ifnet *);
static int		tapifioctl	(struct ifnet *, u_long, caddr_t,
					 struct ucred *);
static void		tapifinit	(void *);
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
	{ CDEV_NAME, 0, 0 },
	.d_open =	tapopen,
	.d_close =	tapclose,
	.d_read =	tapread,
	.d_write =	tapwrite,
	.d_ioctl =	tapioctl,
	.d_kqfilter =	tapkqfilter
};

static int		taprefcnt = 0;		/* module ref. counter   */
static int		taplastunit = -1;	/* max. open unit number */
static int		tapdebug = 0;		/* debug flag            */
static int		tapuopen = 0;		/* all user open()       */
static int		tapuponopen = 0;	/* IFF_UP       */

MALLOC_DECLARE(M_TAP);
MALLOC_DEFINE(M_TAP, CDEV_NAME, "Ethernet tunnel interface");
struct if_clone tap_cloner = IF_CLONE_INITIALIZER("tap",
			     tap_clone_create, tap_clone_destroy,
			     0, IF_MAXUNIT);
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

DEV_MODULE(if_tap, tapmodevent, NULL);

/*
 * tapmodevent
 *
 * module event handler
 */
static int
tapmodevent(module_t mod, int type, void *data)
{
	static int attached = 0;
	struct tap_softc *tp, *ntp;
	int i;

	switch (type) {
	case MOD_LOAD:
		if (attached)
			return (EEXIST);

		make_autoclone_dev(&tap_ops, &DEVFS_CLONE_BITMAP(tap), tapclone,
				   UID_ROOT, GID_WHEEL, 0600, "tap");
		SLIST_INIT(&tap_listhead);
		if_clone_attach(&tap_cloner);

		for (i = 0; i < TAP_PREALLOCATED_UNITS; ++i) {
			make_dev(&tap_ops, i, UID_ROOT, GID_WHEEL,
				 0600, "tap%d", i);
			devfs_clone_bitmap_set(&DEVFS_CLONE_BITMAP(tap), i);
		}

		attached = 1;
		break;

	case MOD_UNLOAD:
		if (taprefcnt > 0)
			return (EBUSY);

		if_clone_detach(&tap_cloner);

		/* Maintain tap ifs in a local list */
		SLIST_FOREACH_MUTABLE(tp, &tap_listhead, tap_link, ntp)
			tapdestroy(tp);

		attached = 0;

		devfs_clone_handler_del("tap");
		dev_ops_remove_all(&tap_ops);
		devfs_clone_bitmap_uninit(&DEVFS_CLONE_BITMAP(tap));
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
} /* tapmodevent */


/*
 * tapcreate - create or clone an interface
 */
static struct tap_softc *
tapcreate(int unit, cdev_t dev)
{
	const char	*name = TAP;
	struct ifnet	*ifp;
	struct tap_softc *tp;
	uint8_t		ether_addr[ETHER_ADDR_LEN];

	tp = kmalloc(sizeof(*tp), M_TAP, M_WAITOK | M_ZERO);
	dev->si_drv1 = tp;
	tp->tap_dev = dev;
	tp->tap_unit = unit;

	reference_dev(dev);	/* tp association */

	/* generate fake MAC address: 00 bd xx xx xx unit_no */
	ether_addr[0] = 0x00;
	ether_addr[1] = 0xbd;
	bcopy(&ticks, &ether_addr[2], 3);
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
	ifp->if_flags = TAP_IFFLAGS;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, ether_addr, NULL);

	tp->tap_flags |= TAP_INITED;
	tp->tap_devq.ifq_maxlen = ifqmaxlen;

	SLIST_INSERT_HEAD(&tap_listhead, tp, tap_link);

	TAPDEBUG(ifp, "created. minor = %#x\n", minor(dev));
	return (tp);
}

static
struct tap_softc *
tapfind(int unit)
{
	struct tap_softc *tp;

	SLIST_FOREACH(tp, &tap_listhead, tap_link) {
		if (tp->tap_unit == unit)
			return(tp);
	}
	return (NULL);
}

/*
 * tap_clone_create:
 *
 * Create a new tap instance via ifconfig.
 */
static int
tap_clone_create(struct if_clone *ifc __unused, int unit,
    caddr_t param __unused)
{
	struct tap_softc *tp;
	cdev_t dev;

	tp = tapfind(unit);
	if (tp == NULL) {
		if (!devfs_clone_bitmap_chk(&DEVFS_CLONE_BITMAP(tap), unit)) {
			devfs_clone_bitmap_set(&DEVFS_CLONE_BITMAP(tap), unit);
			dev = make_dev(&tap_ops, unit, UID_ROOT, GID_WHEEL,
					   0600, "%s%d", TAP, unit);
		} else {
			dev = devfs_find_device_by_name("%s%d", TAP, unit);
		}

		KKASSERT(dev != NULL);
		tp = tapcreate(unit, dev);
	}
	tp->tap_flags |= TAP_CLONE;
	TAPDEBUG(&tp->tap_if, "clone created. minor = %#x tap_flags = 0x%x\n",
		 minor(tp->tap_dev), tp->tap_flags);

	return (0);
}

/*
 * tapopen 
 *
 * to open tunnel. must be superuser
 */
static int
tapopen(struct dev_open_args *ap)
{
	cdev_t dev = NULL;
	struct tap_softc *tp = NULL;
	struct ifnet *ifp = NULL;
	int error;

	if (tapuopen == 0 && 
	    (error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) != 0)
		return (error);

	get_mplock();
	dev = ap->a_head.a_dev;
	tp = dev->si_drv1;
	if (tp == NULL)
		tp = tapcreate(minor(dev), dev);
	if (tp->tap_flags & TAP_OPEN) {
		rel_mplock();
		return (EBUSY);
	}
	ifp = &tp->arpcom.ac_if;

	if ((tp->tap_flags & TAP_CLONE) == 0) {
		EVENTHANDLER_INVOKE(ifnet_attach_event, ifp);

		/* Announce the return of the interface. */
		rt_ifannouncemsg(ifp, IFAN_ARRIVAL);
	}

	bcopy(tp->arpcom.ac_enaddr, tp->ether_addr, sizeof(tp->ether_addr));

	if (curthread->td_proc)
		fsetown(curthread->td_proc->p_pid, &tp->tap_sigtd);
	tp->tap_flags |= TAP_OPEN;
	taprefcnt ++;

	if (tapuponopen && (ifp->if_flags & IFF_UP) == 0) {
		crit_enter();
		if_up(ifp);
		crit_exit();

		ifnet_serialize_all(ifp);
		tapifflags(tp);
		ifnet_deserialize_all(ifp);

		tp->tap_flags |= TAP_CLOSEDOWN;
	}

	TAPDEBUG(ifp, "opened. minor = %#x, refcnt = %d, taplastunit = %d\n",
		 minor(tp->tap_dev), taprefcnt, taplastunit);

	rel_mplock();
	return (0);
}

static int
tapclone(struct dev_clone_args *ap)
{
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(tap), 0);
	ap->a_dev = make_only_dev(&tap_ops, unit, UID_ROOT, GID_WHEEL,
				  0600, "%s%d", TAP, unit);
	tapcreate(unit, ap->a_dev);
	return (0);
}

/*
 * tapclose
 *
 * close the device - mark i/f down & delete routing info
 */
static int
tapclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tap_softc *tp = dev->si_drv1;
	struct ifnet *ifp = &tp->tap_if;
	int clear_flags = 0;

	get_mplock();

	/* Junk all pending output */
	ifq_purge(&ifp->if_snd);

	/*
	 * Do not bring the interface down, and do not anything with
	 * interface, if we are in VMnet mode. just close the device.
	 *
	 * If the interface is not cloned, we always bring it down.
	 *
	 * If the interface is cloned, then we bring it down during
	 * closing only if it was brought up during opening.
	 */
	if ((tp->tap_flags & TAP_VMNET) == 0 &&
	    ((tp->tap_flags & TAP_CLONE) == 0 ||
	     (tp->tap_flags & TAP_CLOSEDOWN))) {
		if (ifp->if_flags & IFF_UP)
			if_down(ifp);
		clear_flags = 1;
	}
	ifnet_serialize_all(ifp);
	tapifstop(tp, clear_flags);
	ifnet_deserialize_all(ifp);

	if ((tp->tap_flags & TAP_CLONE) == 0) {
		if_purgeaddrs_nolink(ifp);

		EVENTHANDLER_INVOKE(ifnet_detach_event, ifp);

		/* Announce the departure of the interface. */
		rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
	}

	funsetown(&tp->tap_sigio);
	tp->tap_sigio = NULL;
	KNOTE(&tp->tap_rkq.ki_note, 0);

	tp->tap_flags &= ~TAP_OPEN;
	funsetown(&tp->tap_sigtd);
	tp->tap_sigtd = NULL;

	taprefcnt --;
	if (taprefcnt < 0) {
		taprefcnt = 0;
		if_printf(ifp, "minor = %#x, refcnt = %d is out of sync. "
			"set refcnt to 0\n", minor(tp->tap_dev), taprefcnt);
	}

	TAPDEBUG(ifp, "closed. minor = %#x, refcnt = %d, taplastunit = %d\n",
		 minor(tp->tap_dev), taprefcnt, taplastunit);

	if (tp->tap_unit >= TAP_PREALLOCATED_UNITS)
		tapdestroy(tp);

	rel_mplock();
	return (0);
}

/*
 * tapdestroy:
 *
 *	Destroy a tap instance.
 */
static void
tapdestroy(struct tap_softc *tp)
{
	struct ifnet *ifp = &tp->arpcom.ac_if;
	cdev_t dev;

	TAPDEBUG(ifp, "destroyed. minor = %#x, refcnt = %d, taplastunit = %d\n",
		 minor(tp->tap_dev), taprefcnt, taplastunit);

	ifnet_serialize_all(ifp);
	tapifstop(tp, 1);
	ifnet_deserialize_all(ifp);

	ether_ifdetach(ifp);
	SLIST_REMOVE(&tap_listhead, tp, tap_softc, tap_link);

	dev = tp->tap_dev;
	tp->tap_dev = NULL;
	dev->si_drv1 = NULL;

	release_dev(dev);	/* tp association */

	/*
	 * Also destroy the cloned device
	 */
	if (tp->tap_unit >= TAP_PREALLOCATED_UNITS) {
		destroy_dev(dev);
		devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(tap), tp->tap_unit);
	}

	kfree(tp, M_TAP);

	taplastunit--;
}

/*
 * tap_clone_destroy:
 *
 *	Destroy a tap instance.
 */
static int
tap_clone_destroy(struct ifnet *ifp)
{
	struct tap_softc *tp = ifp->if_softc;
	
	if ((tp->tap_flags & TAP_CLONE) == 0)
		return ENXIO;

	TAPDEBUG(&tp->tap_if, "clone destroyed. minor = %#x tap_flags = 0x%x\n",
		 minor(tp->tap_dev), tp->tap_flags);
	tapdestroy(tp);

	return 0;
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
	struct tap_softc *tp = xtp;
	struct ifnet *ifp = &tp->tap_if;

	TAPDEBUG(ifp, "initializing, minor = %#x tap_flags = 0x%x\n",
		 minor(tp->tap_dev), tp->tap_flags);

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	tapifstop(tp, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	/* attempt to start output */
	tapifstart(ifp);
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
	struct tap_softc 	*tp = (struct tap_softc *)(ifp->if_softc);
	struct ifstat		*ifs = NULL;
	struct ifmediareq	*ifmr = NULL;
	int			error = 0;
	int			dummy;

	switch (cmd) {
		case SIOCSIFADDR:
		case SIOCGIFADDR:
		case SIOCSIFMTU:
			error = ether_ioctl(ifp, cmd, data);
			break;

		case SIOCSIFFLAGS:
			tapifflags(tp);
			break;

		case SIOCADDMULTI: /* XXX -- just like vmnet does */
		case SIOCDELMULTI:
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
			if (tp->tap_flags & TAP_OPEN)
				ifmr->ifm_status |= IFM_ACTIVE;
			ifmr->ifm_current = ifmr->ifm_active;
			if (dummy >= 1) {
				int media = IFM_ETHER;
				error = copyout(&media,
						ifmr->ifm_ulist,
						sizeof(int));
			}
			break;

		case SIOCGIFSTATUS:
			ifs = (struct ifstat *)data;
			dummy = strlen(ifs->ascii);
			if ((tp->tap_flags & TAP_OPEN) &&
			    dummy < sizeof(ifs->ascii)) {
				if (tp->tap_sigtd && tp->tap_sigtd->sio_proc) {
				    ksnprintf(ifs->ascii + dummy,
					sizeof(ifs->ascii) - dummy,
					"\tOpened by pid %d\n",
					(int)tp->tap_sigtd->sio_proc->p_pid);
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
tapifstart(struct ifnet *ifp)
{
	struct tap_softc *tp = ifp->if_softc;
	struct ifqueue *ifq;
	struct mbuf *m;
	int has_data = 0;

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

	ifq = &tp->tap_devq;
	while ((m = ifq_dequeue(&ifp->if_snd, NULL)) != NULL) {
		if (IF_QFULL(ifq)) {
			IF_DROP(ifq);
			ifp->if_oerrors++;
			m_freem(m);
		} else {
			IF_ENQUEUE(ifq, m);
			ifp->if_opackets++;
			has_data = 1;
		}
	}

	if (has_data) {
		if (tp->tap_flags & TAP_RWAIT) {
			tp->tap_flags &= ~TAP_RWAIT;
			wakeup((caddr_t)tp);
		}

		KNOTE(&tp->tap_rkq.ki_note, 0);

		if ((tp->tap_flags & TAP_ASYNC) && (tp->tap_sigio != NULL)) {
			get_mplock();
			pgsigio(tp->tap_sigio, SIGIO, 0);
			rel_mplock();
		}
	}

	ifp->if_flags &= ~IFF_OACTIVE;
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
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;
 	struct tapinfo		*tapp = NULL;
	struct mbuf *mb;
	short f;
	int error;

	ifnet_serialize_all(ifp);
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

		/* Take a look at devq first */
		IF_POLL(&tp->tap_devq, mb);
		if (mb == NULL)
			mb = ifq_poll(&ifp->if_snd);

		if (mb != NULL) {
			for(; mb != NULL; mb = mb->m_next)
				*(int *)data += mb->m_len;
		} 
		break;

	case FIOSETOWN:
		error = fsetown(*(int *)data, &tp->tap_sigio);
		break;

	case FIOGETOWN:
		*(int *)data = fgetown(&tp->tap_sigio);
		break;

	/* this is deprecated, FIOSETOWN should be used instead */
	case TIOCSPGRP:
		error = fsetown(-(*(int *)data), &tp->tap_sigio);
		break;

	/* this is deprecated, FIOGETOWN should be used instead */
	case TIOCGPGRP:
		*(int *)data = -fgetown(&tp->tap_sigio);
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
		ifnet_serialize_all(ifp);
		IF_DEQUEUE(&tp->tap_devq, m0);
		if (m0 == NULL) {
			if (ap->a_ioflag & IO_NDELAY) {
				ifnet_deserialize_all(ifp);
				return (EWOULDBLOCK);
			}
			tp->tap_flags |= TAP_RWAIT;
			tsleep_interlock(tp, PCATCH);
			ifnet_deserialize_all(ifp);
			error = tsleep(tp, PCATCH | PINTERLOCKED, "taprd", 0);
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
		TAPDEBUG(ifp, "dropping mbuf, minor = %#x\n",
			 minor(tp->tap_dev));
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
	struct tap_softc	*tp = dev->si_drv1;
	struct ifnet		*ifp = &tp->tap_if;
	struct mbuf		*top = NULL, **mp = NULL, *m = NULL;
	int			error = 0;
	size_t			tlen, mlen;

	TAPDEBUG(ifp, "writing, minor = %#x\n", minor(tp->tap_dev));

	if ((tp->tap_flags & TAP_READY) != TAP_READY) {
		TAPDEBUG(ifp, "not ready. minor = %#x, tap_flags = 0x%x\n",
			 minor(tp->tap_dev), tp->tap_flags);
		return (EHOSTDOWN);
	}

	if (uio->uio_resid == 0)
		return (0);

	if (uio->uio_resid > TAPMRU) {
		TAPDEBUG(ifp, "invalid packet len = %zu, minor = %#x\n",
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
		m->m_len = (int)szmin(mlen, uio->uio_resid);
		error = uiomove(mtod(m, caddr_t), (size_t)m->m_len, uio);
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

	top->m_pkthdr.len = (int)tlen;
	top->m_pkthdr.rcvif = ifp;
	
	/*
	 * Ethernet bridge and bpf are handled in ether_input
	 *
	 * adjust mbuf and give packet to the ether_input
	 */
	ifnet_serialize_all(ifp);
	ifp->if_input(ifp, top);
	ifp->if_ipackets ++; /* ibytes are counted in ether_input */
	ifnet_deserialize_all(ifp);

	return (0);
}

/*
 * tapkqfilter - called from the fileops interface with nothing held
 *
 * MPSAFE
 */
static int filt_tapread(struct knote *kn, long hint);
static int filt_tapwrite(struct knote *kn, long hint);
static void filt_tapdetach(struct knote *kn);
static struct filterops tapread_filtops =
	{ FILTEROP_ISFD, NULL, filt_tapdetach, filt_tapread };
static struct filterops tapwrite_filtops =
	{ FILTEROP_ISFD, NULL, filt_tapdetach, filt_tapwrite };

static int
tapkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct tap_softc *tp;
	struct klist *list;
	struct ifnet *ifp;

	tp = dev->si_drv1;
	list = &tp->tap_rkq.ki_note;
	ifp = &tp->tap_if;
	ap->a_result =0;

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &tapread_filtops;
		kn->kn_hook = (void *)tp;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &tapwrite_filtops;
		kn->kn_hook = (void *)tp;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return(0);
	}

	knote_insert(list, kn);
	return(0);
}

static int
filt_tapread(struct knote *kn, long hint)
{
	struct tap_softc *tp = (void *)kn->kn_hook;

	if (IF_QEMPTY(&tp->tap_devq) == 0)	/* XXX serializer */
		return(1);
	else
		return(0);
}

static int
filt_tapwrite(struct knote *kn, long hint)
{
	/* Always ready for a write */
	return (1);
}

static void
filt_tapdetach(struct knote *kn)
{
	struct tap_softc *tp = (void *)kn->kn_hook;

	knote_remove(&tp->tap_rkq.ki_note, kn);
}

static void
tapifstop(struct tap_softc *tp, int clear_flags)
{
	struct ifnet *ifp = &tp->tap_if;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	IF_DRAIN(&tp->tap_devq);
	tp->tap_flags &= ~TAP_CLOSEDOWN;
	if (clear_flags)
		ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
}

static void
tapifflags(struct tap_softc *tp)
{
	struct ifnet *ifp = &tp->arpcom.ac_if;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);
	if ((tp->tap_flags & TAP_VMNET) == 0) {
		/*
		 * Only for non-vmnet tap(4)
		 */
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				tapifinit(tp);
		} else {
			tapifstop(tp, 1);
		}
	} else {
		/* XXX */
	}
}
