/*
 * LG-P500 Smartphone
 * Written by Yellow Rabbit <yrabbit@sdf.lonestar.org>
 */

/*
 * XXX
 * USB:
 * Takes two interfaces.
 * IN and OUT endpoints on the data interface (altsetting).
 * Interrupt endpoint on the control interface.
 *
 * NET:
 * Transfer frames without modification (AS IS).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/bpf.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbcdc.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usbdivar.h>
#include <bus/usb/usb_ethersubr.h>

#include "if_lgue.h"

/*
 * Supported device vendors/products
 */
static struct usb_devno lgue_devs[] = {
	{ USB_DEVICE(0x1004, 0x61a2) }	/* LG P500 */
};

static int lgue_match(device_t);
static int lgue_attach(device_t);
static int lgue_detach(device_t);

static void lgue_start(struct ifnet *);
static void lgue_stop(struct lgue_softc *);
static void lgue_init(void *);
static void lgue_watchdog(struct ifnet *);
static int lgue_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

static int lgue_encap(struct lgue_softc *, struct mbuf *);
static int lgue_start_transfer(struct lgue_softc *);

static void lgue_txeof(usbd_xfer_handle, usbd_private_handle, usbd_status);

static void lgue_start_ipifunc(void *);
static void lgue_start_schedule(struct ifnet *);

static int lgue_newbuf(struct lgue_softc *, int, struct mbuf **);
static void lgue_rxstart(struct ifnet *);
static void lgue_rxeof(usbd_xfer_handle, usbd_private_handle, usbd_status);

static void lgue_intrstart(struct ifnet *);
static void lgue_intreof(usbd_xfer_handle, usbd_private_handle, usbd_status);

static int lgue_get_data_iface_no(usbd_device_handle,
	       usb_interface_descriptor_t *);

static int lgue_getmac(struct lgue_softc *, void *);
static int lgue_getmtu(struct lgue_softc *);

static int hex(char);

static device_method_t lgue_methods[] = {
	DEVMETHOD(device_probe,	 lgue_match),
	DEVMETHOD(device_attach, lgue_attach),
	DEVMETHOD(device_detach, lgue_detach),

	{ 0, 0 }
};

static driver_t lgue_driver = {
	"lgue",
	lgue_methods,
	sizeof(struct lgue_softc)
};

static devclass_t lgue_devclass;

DECLARE_DUMMY_MODULE(if_lgue);
DRIVER_MODULE(lgue, uhub, lgue_driver, lgue_devclass, usbd_driver_load, NULL);
MODULE_DEPEND(lgue, usb, 1, 1, 1);

/*
 * Probe chip
 */
static int
lgue_match(device_t dev)
{
	struct usb_attach_arg *uaa;
	usb_interface_descriptor_t *id;

	uaa = device_get_ivars(dev);
	if (uaa->iface == NULL)
		return(UMATCH_NONE);

	if (usb_lookup(lgue_devs, uaa->vendor, uaa->product) != NULL) {
		id = usbd_get_interface_descriptor(uaa->iface);
		if (id != NULL &&
		    id->bInterfaceClass == UICLASS_CDC &&
		    id->bInterfaceSubClass == UISUBCLASS_ETHERNET_NETWORKING_CONTROL_MODEL)
			return(UMATCH_VENDOR_PRODUCT);
	}
	return(UMATCH_NONE);
}

/*
 * Attach the interface.
 */
static int
lgue_attach(device_t dev)
{
	struct lgue_softc *sc;
	struct usb_attach_arg *uaa;
	struct ifnet *ifp;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;
	u_char eaddr[ETHER_ADDR_LEN];
	usbd_status err;

	sc = device_get_softc(dev);
	uaa = device_get_ivars(dev);

	sc->lgue_ctl_iface = uaa->iface;
	sc->lgue_udev = uaa->device;

	/* It has only config but in case... */
	if (usbd_set_config_no(sc->lgue_udev, LGUE_CONFIG_NO, 0)) {
		device_printf(dev, "setting config no %d failed\n",
		    LGUE_CONFIG_NO);
		return(ENXIO);
	}

	/* Get control and data intefaces */
	id = usbd_get_interface_descriptor(uaa->iface);
	sc->lgue_ctl_iface_no = id->bInterfaceNumber;
	sc->lgue_data_iface_no = lgue_get_data_iface_no(sc->lgue_udev, id);

	if (sc->lgue_data_iface_no == -1) {
		device_printf(dev, "no data interface number\n");
		goto bad;
	}

	/* Claim data interface */
	for (i = 0; i < uaa->nifaces; ++i) {
		if (uaa->ifaces[i] != NULL) {
			id = usbd_get_interface_descriptor(uaa->ifaces[i]);
			if (id != NULL &&
			    id->bInterfaceNumber == sc->lgue_data_iface_no) {
				err = usbd_set_interface(uaa->ifaces[i],
				    LGUE_ALTERNATE_SETTING);
				if ( err != USBD_NORMAL_COMPLETION) {
					device_printf(dev,
					    "no alternate data interface. err:%s\n",
					    usbd_errstr(err));
					goto bad;
				}
				sc->lgue_data_iface = uaa->ifaces[i];
				uaa->ifaces[i] = NULL;
			}
		}
	}
	if (sc->lgue_data_iface == NULL) {
		device_printf(dev, "no data interface\n");
		goto bad;
	}

	/* Find data interface endpoints */
	id = usbd_get_interface_descriptor(sc->lgue_data_iface);
	sc->lgue_ed[LGUE_ENDPT_RX] = sc->lgue_ed[LGUE_ENDPT_TX] = -1;
	for (i = 0; i < id->bNumEndpoints; ++i) {
		ed = usbd_interface2endpoint_descriptor(sc->lgue_data_iface, i);
		if (!ed) {
			device_printf(dev,
			    "couldn't get endpoint descriptor %d\n", i);
			goto bad;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
				UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->lgue_ed[LGUE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->lgue_ed[LGUE_ENDPT_TX] = ed->bEndpointAddress;
		}
	}

	if (sc->lgue_ed[LGUE_ENDPT_RX] == -1) {
		device_printf(dev, "couldn't find data bilk in\n");
		goto bad;
	}
	if (sc->lgue_ed[LGUE_ENDPT_TX] == -1) {
		device_printf(dev, "couldn't find data bilk out\n");
		goto bad;
	}

	/* Find control interface endpoint */
	id = usbd_get_interface_descriptor(sc->lgue_ctl_iface);
	sc->lgue_ed[LGUE_ENDPT_INTR] = -1;
	for (i = 0; i < id->bNumEndpoints; ++i) {
		ed = usbd_interface2endpoint_descriptor(sc->lgue_ctl_iface, i);
		if (!ed) {
			device_printf(dev,
			    "couldn't get endpoint descriptor %d\n", i);
			goto bad;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->lgue_ed[LGUE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	if (sc->lgue_ed[LGUE_ENDPT_INTR] == -1) {
		device_printf(dev, "couldn't find interrupt bilk in\n");
		goto bad;
	}

	/* Create interface */
	ifp = &sc->lgue_arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	lgue_getmac(sc, eaddr);

	ifp->if_mtu = lgue_getmtu(sc);
	ifp->if_data.ifi_mtu = ifp->if_mtu;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_baudrate = 10000000;
	ifp->if_ioctl = lgue_ioctl;
	ifp->if_start = lgue_start;
	ifp->if_watchdog = lgue_watchdog;
	ifp->if_init = lgue_init;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* Call attach routine */
	ether_ifattach(ifp, eaddr, NULL);
	usb_register_netisr();
	sc->lgue_dying = 0;
	return(0);

bad:
	return(ENXIO);
}

/*
 * Device detached.
 */
static int
lgue_detach(device_t dev)
{
	struct lgue_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = &sc->lgue_arpcom.ac_if;
	ether_ifdetach(ifp);

	if (sc->lgue_ep[LGUE_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->lgue_ep[LGUE_ENDPT_TX]);
	if (sc->lgue_ep[LGUE_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->lgue_ep[LGUE_ENDPT_RX]);
	if (sc->lgue_ep[LGUE_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->lgue_ep[LGUE_ENDPT_INTR]);
	return(0);
}

/*
 * Find data interface.
 */
int
lgue_get_data_iface_no(usbd_device_handle dev, usb_interface_descriptor_t *id)
{
	const usb_cdc_union_descriptor_t *cud;

	cud = (const usb_cdc_union_descriptor_t *)usb_find_desc_if(dev,
	    UDESC_CS_INTERFACE, UDESCSUB_CDC_UNION, id);
	return(cud ? cud->bSlaveInterface[0] : -1);
}

/*
 * Get hard max mtu
 */
static int
lgue_getmtu(struct lgue_softc *sc)
{
	const usb_cdc_ethernet_descriptor_t *ced;
	usb_interface_descriptor_t *id;

	id = usbd_get_interface_descriptor(sc->lgue_ctl_iface);
	if (id == NULL) {
		kprintf("usbd_get_interface_descriptor() returned NULL\n");
		return(ETHERMTU);
	}

	ced = (const usb_cdc_ethernet_descriptor_t *)usb_find_desc_if(sc->lgue_udev,
	    UDESC_CS_INTERFACE, UDESCSUB_CDC_ETHERNET, id);
	if (ced == NULL) {
		kprintf("usb_find_desc_if() returned NULL\n");
		return(ETHERMTU);
	}
	return(UGETW(ced->wMaxSegmentSize));
}

/*
 * Get mac address
 */
static int
lgue_getmac(struct lgue_softc *sc, void *buf)
{
	const usb_cdc_ethernet_descriptor_t *ced;
	usb_interface_descriptor_t *id;
	char sbuf[ETHER_ADDR_LEN * 2 + 1];
	usbd_status err;
	int i;

	id = usbd_get_interface_descriptor(sc->lgue_ctl_iface);
	if (id == NULL) goto bad;
	ced = (const usb_cdc_ethernet_descriptor_t *)usb_find_desc_if(sc->lgue_udev,
	    UDESC_CS_INTERFACE, UDESCSUB_CDC_ETHERNET, id);
	if (ced == NULL) goto bad;

	err = usbd_get_string(sc->lgue_udev, ced->iMACAddress, sbuf);
	if(err) {
		kprintf("Read MAC address failed\n");
		goto bad;
	}

	for (i = 0; i < ETHER_ADDR_LEN; ++i) {
		((uByte *)buf)[i] = (hex(sbuf[i * 2]) << 4) + hex(sbuf[(i * 2) + 1]);
	}
	return(0);
bad:
	return(-1);
}

/*
 * Listen INTR pipe
 */
static void
lgue_intrstart(struct ifnet *ifp)
{
	struct lgue_softc *sc;

	sc = ifp->if_softc;
	usbd_setup_xfer(sc->lgue_intr_xfer, sc->lgue_ep[LGUE_ENDPT_INTR], sc,
	    sc->lgue_intr_buf, LGUE_BUFSZ,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, lgue_intreof);
	usbd_transfer(sc->lgue_intr_xfer);
}

/*
 * INTR arrived
 */
static void
lgue_intreof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct ifnet *ifp;
	struct lgue_softc *sc;

	sc = priv;
	if (sc->lgue_dying)
		return;

	ifp = &sc->lgue_arpcom.ac_if;
	lwkt_serialize_enter(ifp->if_serializer);
	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			lwkt_serialize_exit(ifp->if_serializer);
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->lgue_ep[LGUE_ENDPT_INTR]);
			lwkt_serialize_exit(ifp->if_serializer);
		return;
	}
	lgue_intrstart(ifp);
	lwkt_serialize_exit(ifp->if_serializer);
}

/*
 * Encap packet & send
 */
static int
lgue_encap(struct lgue_softc *sc, struct mbuf *m)
{
	struct ifnet *ifp;
	struct lgue_queue_entry *entry;

	ifp = &sc->lgue_arpcom.ac_if;
	entry = kmalloc(sizeof(struct lgue_queue_entry), M_USBDEV , M_NOWAIT);
	if (entry == NULL) {
		if_printf(ifp, "no memory for internal queue entry\n");
		return(ENOBUFS);
	}
	entry->entry_mbuf = m;

	/* Put packet into internal queue tail */
	STAILQ_INSERT_TAIL(&sc->lgue_tx_queue, entry, entry_next);
	return(0);
}

/*
 * Start transfer from internal queue
 */
static int
lgue_start_transfer(struct lgue_softc *sc) {
	usbd_status err;
	struct lgue_queue_entry *entry;
	struct ifnet *ifp;

	if (STAILQ_EMPTY(&sc->lgue_tx_queue))
		return(0);

	ifp = &sc->lgue_arpcom.ac_if;
	entry = STAILQ_FIRST(&sc->lgue_tx_queue);
	STAILQ_REMOVE_HEAD(&sc->lgue_tx_queue, entry_next);

	m_copydata(entry->entry_mbuf, 0, entry->entry_mbuf->m_pkthdr.len,
	    sc->lgue_tx_buf);

	/* Transmit */
	usbd_setup_xfer(sc->lgue_tx_xfer, sc->lgue_ep[LGUE_ENDPT_TX], sc,
	    sc->lgue_tx_buf, entry->entry_mbuf->m_pkthdr.len,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, lgue_txeof);
	err = usbd_transfer(sc->lgue_tx_xfer);
	if (err != USBD_IN_PROGRESS) {
		m_freem(entry->entry_mbuf);
		kfree(entry, M_USBDEV);
		lgue_stop(sc);
		ifp->if_flags &= ~IFF_OACTIVE;
		return(EIO);
	}

	m_freem(entry->entry_mbuf);
	kfree(entry, M_USBDEV);

	sc->lgue_tx_cnt++;
	ifp->if_flags |= IFF_OACTIVE;
	ifp->if_timer = 5;
	return(0);
}

/*
 * Start call
 */
static void
lgue_start_ipifunc(void *arg)
{
	struct ifnet *ifp;
	struct lwkt_msg *lmsg;

	ifp = arg;
	lmsg = &ifp->if_start_nmsg[mycpuid].lmsg;
	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg(ifnet_portfn(mycpuid), lmsg);
	crit_exit();
}

/*
 * Schedule start call
 */
static void
lgue_start_schedule(struct ifnet *ifp)
{
#ifdef SMP
	int cpu;

	cpu = ifp->if_start_cpuid(ifp);
	if (cpu != mycpuid)
		lwkt_send_ipiq(globaldata_find(cpu), lgue_start_ipifunc, ifp);
	else
#endif
		lgue_start_ipifunc(ifp);
}

/*
 * End of sending
 */
static void
lgue_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct ifnet *ifp;
	struct lgue_softc *sc;
	usbd_status err;

	sc = priv;
	if (sc->lgue_dying)
		return;

	ifp = &sc->lgue_arpcom.ac_if;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->lgue_ep[LGUE_ENDPT_TX]);
		return;
	}
	usbd_get_xfer_status(sc->lgue_tx_xfer, NULL, NULL, NULL,&err);
	if (err)
		ifp->if_oerrors++;
	else
		ifp->if_opackets++;

	if (!STAILQ_EMPTY(&sc->lgue_tx_queue)) {
		lgue_start_schedule(ifp);
	}

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;
}

/*
 * Start transfer
 */
static void
lgue_start(struct ifnet *ifp)
{
	struct lgue_softc *sc;
	struct mbuf *m_head;

	sc = ifp->if_softc;
	if (sc->lgue_dying)
		return;

	if (ifp->if_flags & IFF_OACTIVE) {
		return;
	}

	/* To internal queue */
	while ((m_head = ifq_dequeue(&ifp->if_snd, NULL)) != NULL) {
		if (lgue_encap(sc, m_head)) {
			m_freem(m_head);
			break;
		}
		/* Filter */
		BPF_MTAP(ifp, m_head);
	}

	lgue_start_transfer(sc);
}

/*
 * Stop
 */
static void
lgue_stop(struct lgue_softc *sc)
{
	struct ifnet *ifp;
	usbd_status err;
	struct lgue_queue_entry *entry;

	if (sc->lgue_dying)
		return;
	sc->lgue_dying = 1;

	ifp = &sc->lgue_arpcom.ac_if;

	/* Stop transfers */
	if (sc->lgue_ep[LGUE_ENDPT_TX] != NULL) {
		err = usbd_abort_pipe(sc->lgue_ep[LGUE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "abort tx pipe failed:%s\n",
			    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->lgue_ep[LGUE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "close tx pipe failed:%s\n",
			    usbd_errstr(err));
		}
	}
	if (sc->lgue_ep[LGUE_ENDPT_RX] != NULL) {
		err = usbd_abort_pipe(sc->lgue_ep[LGUE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "abort rx pipe failed:%s\n",
			    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->lgue_ep[LGUE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "close rx pipe failed:%s\n",
			    usbd_errstr(err));
		}
	}
	if (sc->lgue_ep[LGUE_ENDPT_INTR] != NULL) {
		err = usbd_abort_pipe(sc->lgue_ep[LGUE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "abort intr pipe failed:%s\n",
			    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->lgue_ep[LGUE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "close intr pipe failed:%s\n",
			    usbd_errstr(err));
		}
	}

	/* Free tx buffers */
	if (sc->lgue_tx_buf != NULL) {
		kfree(sc->lgue_tx_buf, M_USBDEV);
		sc->lgue_tx_buf = NULL;
	}
	if (sc->lgue_tx_xfer != NULL) {
		usbd_free_xfer(sc->lgue_tx_xfer);
		sc->lgue_tx_xfer = NULL;
	}

	/* Free rx buffers */
	if (sc->lgue_rx_buf != NULL) {
		kfree(sc->lgue_rx_buf, M_USBDEV);
		sc->lgue_rx_buf = NULL;
	}
	if (sc->lgue_rx_xfer != NULL) {
		usbd_free_xfer(sc->lgue_rx_xfer);
		sc->lgue_rx_xfer = NULL;
	}

	/* Free intr buffer */
	if (sc->lgue_intr_buf != NULL) {
		kfree(sc->lgue_intr_buf, M_USBDEV);
		sc->lgue_intr_buf = NULL;
	}
	if (sc->lgue_intr_xfer != NULL) {
		usbd_free_xfer(sc->lgue_intr_xfer);
		sc->lgue_intr_xfer = NULL;
	}

	/* Clear internal queue */
	while (!STAILQ_EMPTY(&sc->lgue_tx_queue)) {
		entry = STAILQ_FIRST(&sc->lgue_tx_queue);
		STAILQ_REMOVE_HEAD(&sc->lgue_tx_queue, entry_next);
		m_freem(entry->entry_mbuf);
		kfree(entry, M_USBDEV);
	}

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
}

/*
 * Init
 */
static void
lgue_init(void *xsc)
{
	struct lgue_softc *sc;
	struct ifnet *ifp;
	usbd_status err;

	sc = xsc;
	ifp = &sc->lgue_arpcom.ac_if;

	if (ifp->if_flags & IFF_RUNNING)
		return;

	/* Create RX and TX bufs */
	if (sc->lgue_tx_xfer == NULL) {
		sc->lgue_tx_xfer = usbd_alloc_xfer(sc->lgue_udev);
		if (sc->lgue_tx_xfer == NULL) {
			if_printf(ifp, "tx buffer allocate failed\n");
			return;
		}
	}
	sc->lgue_tx_buf = kmalloc(LGUE_BUFSZ, M_USBDEV, M_WAITOK);
	if (sc->lgue_tx_buf == NULL) {
		if_printf(ifp, "tx internal buffer allocate failed\n");
		return;
	}

	if (sc->lgue_rx_xfer == NULL) {
		sc->lgue_rx_xfer = usbd_alloc_xfer(sc->lgue_udev);
		if (sc->lgue_rx_xfer == NULL) {
			if_printf(ifp, "rx buffer allocate failed\n");
			return;
		}
	}
	sc->lgue_rx_buf = kmalloc(LGUE_BUFSZ, M_USBDEV, M_WAITOK);
	if (sc->lgue_rx_buf == NULL) {
		if_printf(ifp, "rx internal buffer allocate failed\n");
		return;
	}

	/* Create INTR buf */
	if (sc->lgue_intr_xfer == NULL) {
		sc->lgue_intr_xfer = usbd_alloc_xfer(sc->lgue_udev);
		if (sc->lgue_intr_xfer == NULL) {
			if_printf(ifp, "intr buffer allocate failed\n");
			return;
		}
	}
	sc->lgue_intr_buf = kmalloc(LGUE_BUFSZ, M_USBDEV, M_WAITOK);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->lgue_data_iface, sc->lgue_ed[LGUE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->lgue_ep[LGUE_ENDPT_RX]);
	if (err) {
		if_printf(ifp, "open RX pipe failed: %s\n", usbd_errstr(err));
		return;
	}
	err = usbd_open_pipe(sc->lgue_data_iface, sc->lgue_ed[LGUE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->lgue_ep[LGUE_ENDPT_TX]);
	if (err) {
		if_printf(ifp, "open TX pipe failed: %s\n", usbd_errstr(err));
		return;
	}
	/* Open INTR pipe. */
	err = usbd_open_pipe(sc->lgue_ctl_iface, sc->lgue_ed[LGUE_ENDPT_INTR],
	    USBD_EXCLUSIVE_USE, &sc->lgue_ep[LGUE_ENDPT_INTR]);
	if (err) {
		if_printf(ifp, "open INTR pipe failed: %s\n", usbd_errstr(err));
		return;
	}

	/* Create internal queue */
	STAILQ_INIT(&sc->lgue_tx_queue);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	sc->lgue_dying = 0;

	lgue_rxstart(ifp);
	lgue_intrstart(ifp);
}

/*
 * New mbuf
 */
static int
lgue_newbuf(struct lgue_softc *sc, int len, struct mbuf **m_buf)
{
	struct ifnet *ifp;

	ifp = &sc->lgue_arpcom.ac_if;
	*m_buf = NULL;

	/* Allocate mbuf */
	*m_buf = m_getcl(MB_DONTWAIT, MT_DATA, MT_HEADER);
	if (*m_buf == NULL) {
		if_printf(ifp, " no memory for rx buffer --- packet dropped!\n");
		return(ENOBUFS);
	}
	(*m_buf)->m_len = (*m_buf)->m_pkthdr.len = MCLBYTES;
	m_adj(*m_buf, ETHER_ALIGN);
	return(0);
}

/*
 * Start read
 */
static void
lgue_rxstart(struct ifnet *ifp)
{
	struct lgue_softc *sc;

	sc = ifp->if_softc;
	if (sc->lgue_dying)
		return;

	usbd_setup_xfer(sc->lgue_rx_xfer, sc->lgue_ep[LGUE_ENDPT_RX], sc,
	    sc->lgue_rx_buf, LGUE_BUFSZ,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, lgue_rxeof);
	usbd_transfer(sc->lgue_rx_xfer);
}

/*
 * A frame has been uploaded: pass the resulting mbuf up to
 * the higher level protocols.
 */
static void
lgue_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct lgue_softc	*sc;
	struct mbuf		*m;
	struct ifnet	*ifp;
	int			total_len;

	sc = priv;
	if (sc->lgue_dying)
		return;

	ifp = &sc->lgue_arpcom.ac_if;

	total_len = 0;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->lgue_rx_notice)) {
			if_printf(ifp, "usb error on rx:%s\n",
			    usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->lgue_ep[LGUE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	if (total_len < sizeof(struct ether_header)) {
		ifp->if_ierrors++;
		goto done;
	}

	if (lgue_newbuf(sc, total_len, &m) == ENOBUFS) {
		ifp->if_ierrors++;
		return;
	}

	ifp->if_ipackets++;
	m_copyback(m, 0, total_len, sc->lgue_rx_buf);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = total_len;

	usb_ether_input(m);
	lgue_rxstart(ifp);
	return;
done:
	usbd_setup_xfer(sc->lgue_rx_xfer, sc->lgue_ep[LGUE_ENDPT_RX], sc,
	    sc->lgue_rx_buf, LGUE_BUFSZ,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, lgue_rxeof);
	usbd_transfer(sc->lgue_rx_xfer);
}

/*
 * Control
 */
static int
lgue_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct lgue_softc *sc;
	struct ifreq *ifr;
	int err;

	err = 0;
	ifr = (struct ifreq *)data;
	sc = ifp->if_softc;

	switch(command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				lgue_init(sc);
		} else if (ifp->if_flags & IFF_RUNNING) {
			lgue_stop(sc);
		}
		sc->lgue_if_flags = ifp->if_flags;
		err = 0;
		break;
#if 0
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		err = 0;
		break;
#endif
	case SIOCSIFMTU:
		ifp->if_mtu = ifr->ifr_mtu;
		break;
	default:
		err = ether_ioctl(ifp, command, data);
		break;
	}
	return(err);
}

/*
 * Watchdog
 */
static void
lgue_watchdog(struct ifnet *ifp)
{
	ifp->if_oerrors++;

	if (!ifq_is_empty(&ifp->if_snd))
		lgue_start_schedule(ifp);
}

/*
 * Hex -> bin
 */
static int
hex(char ch)
{
	if ((ch >= 'a') && (ch <= 'f')) return (ch-'a'+10);
	if ((ch >= '0') && (ch <= '9')) return (ch-'0');
	if ((ch >= 'A') && (ch <= 'F')) return (ch-'A'+10);
	return (-1);
}
