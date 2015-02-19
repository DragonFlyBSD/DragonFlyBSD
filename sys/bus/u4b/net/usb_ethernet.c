/* $FreeBSD$ */
/*-
 * Copyright (c) 2009 Andrew Thompson (thompsa@FreeBSD.org)
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/ethernet.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/vlan/if_vlan_var.h>

#include <sys/devfs.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include <bus/u4b/usb.h>
#include <bus/u4b/usbdi.h>

#include <bus/u4b/usb_process.h>
#include <bus/u4b/net/usb_ethernet.h>

static SYSCTL_NODE(_net, OID_AUTO, ue, CTLFLAG_RD, 0,
    "USB Ethernet parameters");

#define	UE_LOCK(_ue)		lockmgr((_ue)->ue_lock, LK_EXCLUSIVE)
#define	UE_UNLOCK(_ue)		lockmgr((_ue)->ue_lock, LK_RELEASE)
#define	UE_LOCK_ASSERT(_ue)	KKASSERT(lockowned((_ue)->ue_lock))

MODULE_DEPEND(uether, usb, 1, 1, 1);
MODULE_DEPEND(uether, miibus, 1, 1, 1);

/*
static struct unrhdr *ueunit;
*/
DEVFS_DECLARE_CLONE_BITMAP(ue);

static usb_proc_callback_t ue_attach_post_task;
static usb_proc_callback_t ue_promisc_task;
static usb_proc_callback_t ue_setmulti_task;
static usb_proc_callback_t ue_ifmedia_task;
static usb_proc_callback_t ue_tick_task;
static usb_proc_callback_t ue_start_task;
static usb_proc_callback_t ue_stop_task;

static void	ue_init(void *);
static void	ue_start(struct ifnet *, struct ifaltq_subque *);
static int	ue_ifmedia_upd(struct ifnet *);
static void	ue_watchdog(void *);

/*
 * Return values:
 *    0: success
 * Else: device has been detached
 */
uint8_t
uether_pause(struct usb_ether *ue, unsigned int _ticks)
{
	if (usb_proc_is_gone(&ue->ue_tq)) {
		/* nothing to do */
		return (1);
	}
	usb_pause_mtx(ue->ue_lock, _ticks);
	return (0);
}

static void
ue_queue_command(struct usb_ether *ue,
    usb_proc_callback_t *fn,
    struct usb_proc_msg *t0, struct usb_proc_msg *t1)
{
	struct usb_ether_cfg_task *task;

	UE_LOCK_ASSERT(ue);

	if (usb_proc_is_gone(&ue->ue_tq)) {
		return;         /* nothing to do */
	}
	/* 
	 * NOTE: The task cannot get executed before we drop the
	 * "sc_mtx" mutex. It is safe to update fields in the message
	 * structure after that the message got queued.
	 */
	task = (struct usb_ether_cfg_task *)
	  usb_proc_msignal(&ue->ue_tq, t0, t1);

	/* Setup callback and self pointers */
	task->hdr.pm_callback = fn;
	task->ue = ue;

	/*
	 * Start and stop must be synchronous!
	 */
	if ((fn == ue_start_task) || (fn == ue_stop_task))
		usb_proc_mwait(&ue->ue_tq, t0, t1);
}

struct ifnet *
uether_getifp(struct usb_ether *ue)
{
	return &(ue->ue_arpcom.ac_if);
}

struct mii_data *
uether_getmii(struct usb_ether *ue)
{
	return (device_get_softc(ue->ue_miibus));
}

void *
uether_getsc(struct usb_ether *ue)
{
	return (ue->ue_sc);
}

static int
ue_sysctl_parent(SYSCTL_HANDLER_ARGS)
{
	struct usb_ether *ue = arg1;
	const char *name;

	name = device_get_nameunit(ue->ue_dev);
	return SYSCTL_OUT(req, name, strlen(name));
}

int
uether_ifattach(struct usb_ether *ue)
{
	int error;

	/* check some critical parameters */
	if ((ue->ue_dev == NULL) ||
	    (ue->ue_udev == NULL) ||
	    (ue->ue_lock == NULL) ||
	    (ue->ue_methods == NULL))
		return (EINVAL);

	error = usb_proc_create(&ue->ue_tq, ue->ue_lock, 
	    device_get_nameunit(ue->ue_dev), USB_PRI_MED);
	if (error) {
		device_printf(ue->ue_dev, "could not setup taskqueue\n");
		goto error;
	}

	/* fork rest of the attach code */
	UE_LOCK(ue);
	ue_queue_command(ue, ue_attach_post_task,
	    &ue->ue_sync_task[0].hdr,
	    &ue->ue_sync_task[1].hdr);
	UE_UNLOCK(ue);

error:
	return (error);
}

static void
ue_attach_post_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;
	struct ifnet *ifp = uether_getifp(ue);
	int error;
	char num[14];			/* sufficient for 32 bits */

	/* first call driver's post attach routine */
	ue->ue_methods->ue_attach_post(ue);

	UE_UNLOCK(ue);

	KKASSERT(!lockowned(ue->ue_lock));
	ue->ue_unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(ue), 0);
	usb_callout_init_mtx(&ue->ue_watchdog, ue->ue_lock, 0);
	sysctl_ctx_init(&ue->ue_sysctl_ctx);

	KKASSERT(!lockowned(ue->ue_lock));
	error = 0;

	KKASSERT(!lockowned(ue->ue_lock));
	ifp->if_softc = ue;
	if_initname(ifp, "ue", ue->ue_unit);
	if (ue->ue_methods->ue_attach_post_sub != NULL) {
		error = ue->ue_methods->ue_attach_post_sub(ue);
		KKASSERT(!lockowned(ue->ue_lock));
	} else {
		ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
		if (ue->ue_methods->ue_ioctl != NULL)
			ifp->if_ioctl = ue->ue_methods->ue_ioctl;
		else
			ifp->if_ioctl = uether_ioctl;
		ifp->if_start = ue_start;
		ifp->if_init = ue_init;
		ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
		ifq_set_ready(&ifp->if_snd);

		if (ue->ue_methods->ue_mii_upd != NULL &&
		    ue->ue_methods->ue_mii_sts != NULL) {
			error = mii_phy_probe(ue->ue_dev, &ue->ue_miibus, 
			  		      ue_ifmedia_upd, ue->ue_methods->ue_mii_sts);
		}
	}

	if (error) {
		device_printf(ue->ue_dev, "attaching PHYs failed\n");
		goto fail;
	}

	if_printf(ifp, "<USB Ethernet> on %s\n", device_get_nameunit(ue->ue_dev));
	ether_ifattach(ifp, ue->ue_eaddr, NULL);
	/* Tell upper layer we support VLAN oversized frames. */
	if (ifp->if_capabilities & IFCAP_VLAN_MTU)
		ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	ksnprintf(num, sizeof(num), "%u", ue->ue_unit);
	ue->ue_sysctl_oid = SYSCTL_ADD_NODE(&ue->ue_sysctl_ctx,
	    &SYSCTL_NODE_CHILDREN(_net, ue),
	    OID_AUTO, num, CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_PROC(&ue->ue_sysctl_ctx,
	    SYSCTL_CHILDREN(ue->ue_sysctl_oid), OID_AUTO,
	    "%parent", CTLTYPE_STRING | CTLFLAG_RD, ue, 0,
	    ue_sysctl_parent, "A", "parent device");

	KKASSERT(!lockowned(ue->ue_lock));
	UE_LOCK(ue);
	return;

fail:
	devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(ue), ue->ue_unit);
	UE_LOCK(ue);
	return;
}

void
uether_ifdetach(struct usb_ether *ue)
{
	struct ifnet *ifp;

	/* wait for any post attach or other command to complete */
	usb_proc_drain(&ue->ue_tq);

	/* read "ifnet" pointer after taskqueue drain */
	ifp = uether_getifp(ue);

	if (ifp != NULL) {

		/* we are not running any more */
		UE_LOCK(ue);
		ifp->if_flags &= ~IFF_RUNNING;
		UE_UNLOCK(ue);
		
		/* drain any callouts */
		usb_callout_drain(&ue->ue_watchdog);

		/* detach miibus */
		if (ue->ue_miibus != NULL) {
			device_delete_child(ue->ue_dev, ue->ue_miibus);
		}

		/* detach ethernet */
		ether_ifdetach(ifp);

		/* free sysctl */
		sysctl_ctx_free(&ue->ue_sysctl_ctx);

		/* free unit */
		devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(ue), ue->ue_unit);
	}

	/* free taskqueue, if any */
	usb_proc_free(&ue->ue_tq);
}

uint8_t
uether_is_gone(struct usb_ether *ue)
{
	return (usb_proc_is_gone(&ue->ue_tq));
}

void
uether_init(void *arg)
{
	ue_init(arg);
}

static void
ue_init(void *arg)
{
	struct usb_ether *ue = arg;

	UE_LOCK(ue);
	ue_queue_command(ue, ue_start_task,
	    &ue->ue_sync_task[0].hdr, 
	    &ue->ue_sync_task[1].hdr);
	UE_UNLOCK(ue);
}

static void
ue_start_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;
	struct ifnet *ifp = uether_getifp(ue);

	UE_LOCK_ASSERT(ue);

	ue->ue_methods->ue_init(ue);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;
	if (ue->ue_methods->ue_tick != NULL)
		usb_callout_reset(&ue->ue_watchdog, hz, ue_watchdog, ue);
}

static void
ue_stop_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;

	UE_LOCK_ASSERT(ue);

	usb_callout_stop(&ue->ue_watchdog);

	ue->ue_methods->ue_stop(ue);
}

void
uether_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{

	ue_start(ifp, ifsq);
}

static void
ue_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct usb_ether *ue = ifp->if_softc;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;
	UE_LOCK(ue);
	ue->ue_methods->ue_start(ue);
	UE_UNLOCK(ue);
}

static void
ue_promisc_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;

	ue->ue_methods->ue_setpromisc(ue);
}

static void
ue_setmulti_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;

	ue->ue_methods->ue_setmulti(ue);
}

int
uether_ifmedia_upd(struct ifnet *ifp)
{

	return (ue_ifmedia_upd(ifp));
}

static int
ue_ifmedia_upd(struct ifnet *ifp)
{
	struct usb_ether *ue = ifp->if_softc;

	/* Defer to process context */
	UE_LOCK(ue);
	ue_queue_command(ue, ue_ifmedia_task,
	    &ue->ue_media_task[0].hdr,
	    &ue->ue_media_task[1].hdr);
	UE_UNLOCK(ue);

	return (0);
}

static void
ue_ifmedia_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;
	struct ifnet *ifp = uether_getifp(ue);

	ue->ue_methods->ue_mii_upd(ifp);
}

static void
ue_watchdog(void *arg)
{
	struct usb_ether *ue = arg;
	struct ifnet *ifp = uether_getifp(ue);
	
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	ue_queue_command(ue, ue_tick_task,
	    &ue->ue_tick_task[0].hdr, 
	    &ue->ue_tick_task[1].hdr);

	usb_callout_reset(&ue->ue_watchdog, hz, ue_watchdog, ue);
}

static void
ue_tick_task(struct usb_proc_msg *_task)
{
	struct usb_ether_cfg_task *task =
	    (struct usb_ether_cfg_task *)_task;
	struct usb_ether *ue = task->ue;
	struct ifnet *ifp = uether_getifp(ue);
	
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	ue->ue_methods->ue_tick(ue);
}

int
uether_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred* uc)
{
	struct usb_ether *ue = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		UE_LOCK(ue);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) 
				ue_queue_command(ue, ue_promisc_task,
				    &ue->ue_promisc_task[0].hdr, 
				    &ue->ue_promisc_task[1].hdr);
			else
				ue_queue_command(ue, ue_start_task,
				    &ue->ue_sync_task[0].hdr, 
				    &ue->ue_sync_task[1].hdr);
		} else {
			ue_queue_command(ue, ue_stop_task,
			    &ue->ue_sync_task[0].hdr, 
			    &ue->ue_sync_task[1].hdr);
		}
		UE_UNLOCK(ue);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		UE_LOCK(ue);
		ue_queue_command(ue, ue_setmulti_task,
		    &ue->ue_multi_task[0].hdr, 
		    &ue->ue_multi_task[1].hdr);
		UE_UNLOCK(ue);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (ue->ue_miibus != NULL) {
			mii = device_get_softc(ue->ue_miibus);
			error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		} else
			error = ether_ioctl(ifp, command, data);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static int
uether_modevent(module_t mod, int type, void *data)
{
	static int attached = 0;

	switch (type) {
	case MOD_LOAD:
		if (attached)
			return (EEXIST);

		devfs_clone_bitmap_init(&DEVFS_CLONE_BITMAP(ue));

		attached = 1;
        break;
	case MOD_UNLOAD:
		devfs_clone_bitmap_uninit(&DEVFS_CLONE_BITMAP(ue));
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}
static moduledata_t uether_mod = {
	"uether",
	uether_modevent,
	0
};

struct mbuf *
uether_newbuf(void)
{
	struct mbuf *m_new;

	m_new = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL)
		return (NULL);
	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	m_adj(m_new, ETHER_ALIGN);
	return (m_new);
}

int
uether_rxmbuf(struct usb_ether *ue, struct mbuf *m, 
    unsigned int len)
{
	struct ifnet *ifp = uether_getifp(ue);

	UE_LOCK_ASSERT(ue);

	/* finalize mbuf */
	IFNET_STAT_INC(ifp, ipackets, 1);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = len;

	/* enqueue for later when the lock can be released */
	IF_ENQUEUE(&ue->ue_rxq, m);
	return (0);
}

int
uether_rxbuf(struct usb_ether *ue, struct usb_page_cache *pc, 
    unsigned int offset, unsigned int len)
{
	struct ifnet *ifp = uether_getifp(ue);
	struct mbuf *m;

	UE_LOCK_ASSERT(ue);

	if (len < ETHER_HDR_LEN || len > MCLBYTES - ETHER_ALIGN)
		return (1);

	m = uether_newbuf();
	if (m == NULL) {
		IFNET_STAT_INC(ifp, iqdrops, 1);
		return (ENOMEM);
	}

	usbd_copy_out(pc, offset, mtod(m, uint8_t *), len);

	/* finalize mbuf */
	IFNET_STAT_INC(ifp, ipackets, 1);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = len;

	/* enqueue for later when the lock can be released */
	IF_ENQUEUE(&ue->ue_rxq, m);
	return (0);
}

void
uether_rxflush(struct usb_ether *ue)
{
	struct ifnet *ifp = uether_getifp(ue);
	struct mbuf *m;

	UE_LOCK_ASSERT(ue);

	for (;;) {
		IF_DEQUEUE(&ue->ue_rxq, m);
		if (m == NULL)
			break;

		/*
		 * The USB xfer has been resubmitted so its safe to unlock now.
		 */
		UE_UNLOCK(ue);
		ifp->if_input(ifp, m, NULL, -1);
		UE_LOCK(ue);
	}
}

DECLARE_MODULE(uether, uether_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(uether, 1);
