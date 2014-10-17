/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <machine/md_var.h>
#include <machine/cothread.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <netinet/in_var.h>

#include <sys/stat.h>
#include <net/tap/if_tap.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define VKE_DEVNAME		"vke"

#define VKE_CHUNK	8 /* number of mbufs to queue before interrupting */

#define NETFIFOINDEX(u, sc) ((u) & ((sc)->sc_ringsize - 1))

#define VKE_COTD_RUN	0
#define VKE_COTD_EXIT	1
#define VKE_COTD_DEAD	2

struct vke_fifo {
	struct mbuf	**array;
	int		rindex;
	int		windex;
};
typedef struct vke_fifo *fifo_t;

/* Default value for a long time */
#define VKE_DEFAULT_RINGSIZE	256
static int vke_max_ringsize = 0;
TUNABLE_INT("hw.vke.max_ringsize", &vke_max_ringsize);

#define LOW_POW_2(n)	(1 << (fls(n) - 1))

struct vke_softc {
	struct arpcom		arpcom;
	int			sc_fd;
	int			sc_unit;

	cothread_t		cotd_tx;
	cothread_t		cotd_rx;

	int			cotd_tx_exit;
	int			cotd_rx_exit;

	void			*sc_txbuf;
	int			sc_txbuf_len;

	fifo_t			sc_txfifo;
	fifo_t			sc_txfifo_done;
	fifo_t			sc_rxfifo;

	int			sc_ringsize;

	long			cotd_ipackets;
	long			cotd_oerrors;
	long			cotd_opackets;

	struct sysctl_ctx_list	sc_sysctl_ctx;
	struct sysctl_oid	*sc_sysctl_tree;

	int			sc_tap_unit;	/* unit of backend tap(4) */
	in_addr_t		sc_addr;	/* address */
	in_addr_t		sc_mask;	/* netmask */
};

static void	vke_start(struct ifnet *, struct ifaltq_subque *);
static void	vke_init(void *);
static int	vke_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

static int	vke_attach(const struct vknetif_info *, int);
static int	vke_stop(struct vke_softc *);
static int	vke_init_addr(struct ifnet *, in_addr_t, in_addr_t);
static void	vke_tx_intr(cothread_t cotd);
static void	vke_tx_thread(cothread_t cotd);
static void	vke_rx_intr(cothread_t cotd);
static void	vke_rx_thread(cothread_t cotd);

static int vke_txfifo_enqueue(struct vke_softc *sc, struct mbuf *m);
static struct mbuf *vke_txfifo_dequeue(struct vke_softc *sc);

static int vke_txfifo_done_enqueue(struct vke_softc *sc, struct mbuf *m);
static struct mbuf * vke_txfifo_done_dequeue(struct vke_softc *sc, struct mbuf *nm);

static struct mbuf *vke_rxfifo_dequeue(struct vke_softc *sc, struct mbuf *nm);
static struct mbuf *vke_rxfifo_sniff(struct vke_softc *sc);

static void
vke_sysinit(void *arg __unused)
{
	int i, unit;

	KASSERT(NetifNum <= VKNETIF_MAX, ("too many netifs: %d", NetifNum));

	unit = 0;
	for (i = 0; i < NetifNum; ++i) {
		if (vke_attach(&NetifInfo[i], unit) == 0)
			++unit;
	}
}
SYSINIT(vke, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, vke_sysinit, NULL);

/*
 * vke_txfifo_done_enqueue() - Add an mbuf to the transmit done fifo.  Since
 * the cothread cannot free transmit mbufs after processing we put them on
 * the done fifo so the kernel can free them.
 */
static int
vke_txfifo_done_enqueue(struct vke_softc *sc, struct mbuf *m)
{
	fifo_t fifo = sc->sc_txfifo_done;

	while (NETFIFOINDEX(fifo->windex + 1, sc) == NETFIFOINDEX(fifo->rindex, sc)) {
		usleep(20000);
	}

	fifo->array[NETFIFOINDEX(fifo->windex, sc)] = m;
	cpu_sfence();
	++fifo->windex;
	return (0);
}

/*
 * vke_txfifo_done_dequeue() - Remove an mbuf from the transmit done fifo.
 */
static struct mbuf *
vke_txfifo_done_dequeue(struct vke_softc *sc, struct mbuf *nm)
{
	fifo_t fifo = sc->sc_txfifo_done;
	struct mbuf *m;

	if (NETFIFOINDEX(fifo->rindex, sc) == NETFIFOINDEX(fifo->windex, sc))
		return (NULL);

	m = fifo->array[NETFIFOINDEX(fifo->rindex, sc)];
	fifo->array[NETFIFOINDEX(fifo->rindex, sc)] = nm;
	cpu_lfence();
	++fifo->rindex;
	return (m);
}

/*
 * vke_txfifo_enqueue() - Add an mbuf to the transmit fifo.
 */
static int
vke_txfifo_enqueue(struct vke_softc *sc, struct mbuf *m)
{
	fifo_t fifo = sc->sc_txfifo;

	if (NETFIFOINDEX(fifo->windex + 1, sc) == NETFIFOINDEX(fifo->rindex, sc))
		return (-1);

	fifo->array[NETFIFOINDEX(fifo->windex, sc)] = m;
	cpu_sfence();
	++fifo->windex;

	return (0);
}

/*
 * vke_txfifo_dequeue() - Return next mbuf on the transmit fifo if one
 * exists.
 */
static struct mbuf *
vke_txfifo_dequeue(struct vke_softc *sc)
{
	fifo_t fifo = sc->sc_txfifo;
	struct mbuf *m;

	if (NETFIFOINDEX(fifo->rindex, sc) == NETFIFOINDEX(fifo->windex, sc))
		return (NULL);

	m = fifo->array[NETFIFOINDEX(fifo->rindex, sc)];
	fifo->array[NETFIFOINDEX(fifo->rindex, sc)] = NULL;

	cpu_lfence();
	++fifo->rindex;
	return (m);
}

static int
vke_txfifo_empty(struct vke_softc *sc)
{
	fifo_t fifo = sc->sc_txfifo;

	if (NETFIFOINDEX(fifo->rindex, sc) == NETFIFOINDEX(fifo->windex, sc))
		return (1);
	return(0);
}

/*
 * vke_rxfifo_dequeue() - Return next mbuf on the receice fifo if one
 * exists replacing it with newm which should point to a newly allocated
 * mbuf.
 */
static struct mbuf *
vke_rxfifo_dequeue(struct vke_softc *sc, struct mbuf *newm)
{
	fifo_t fifo = sc->sc_rxfifo;
	struct mbuf *m;

	if (NETFIFOINDEX(fifo->rindex, sc) == NETFIFOINDEX(fifo->windex, sc))
		return (NULL);

	m = fifo->array[NETFIFOINDEX(fifo->rindex, sc)];
	fifo->array[NETFIFOINDEX(fifo->rindex, sc)] = newm;
	cpu_lfence();
	++fifo->rindex;
	return (m);
}

/*
 * Return the next mbuf if available but do NOT remove it from the FIFO.
 */
static struct mbuf *
vke_rxfifo_sniff(struct vke_softc *sc)
{
	fifo_t fifo = sc->sc_rxfifo;
	struct mbuf *m;

	if (NETFIFOINDEX(fifo->rindex, sc) == NETFIFOINDEX(fifo->windex, sc))
		return (NULL);

	m = fifo->array[NETFIFOINDEX(fifo->rindex, sc)];
	cpu_lfence();
	return (m);
}

static void
vke_init(void *xsc)
{
	struct vke_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	size_t ringsize = sc->sc_ringsize * sizeof(struct mbuf *);
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	vke_stop(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifsq_clr_oactive(ifq_get_subq_default(&ifp->if_snd));

	/*
	 * Allocate memory for FIFO structures and mbufs.
	 */
	sc->sc_txfifo = kmalloc(sizeof(*sc->sc_txfifo),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_txfifo_done = kmalloc(sizeof(*sc->sc_txfifo_done),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_rxfifo = kmalloc(sizeof(*sc->sc_rxfifo),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_txfifo->array = kmalloc(ringsize, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_txfifo_done->array = kmalloc(ringsize, M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_rxfifo->array = kmalloc(ringsize, M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_ringsize; i++) {
		sc->sc_rxfifo->array[i] = m_getcl(MB_WAIT, MT_DATA, M_PKTHDR);
		sc->sc_txfifo->array[i] = NULL;
		sc->sc_txfifo_done->array[i] = NULL;
	}

	sc->cotd_tx_exit = sc->cotd_rx_exit = VKE_COTD_RUN;
	sc->cotd_tx = cothread_create(vke_tx_thread, vke_tx_intr, sc, "vke_tx");
	sc->cotd_rx = cothread_create(vke_rx_thread, vke_rx_intr, sc, "vke_rx");

	if (sc->sc_addr != 0) {
		in_addr_t addr, mask;

		addr = sc->sc_addr;
		mask = sc->sc_mask;

		/*
		 * Make sure vkernel assigned
		 * address will not be added
		 * again.
		 */
		sc->sc_addr = 0;
		sc->sc_mask = 0;

		vke_init_addr(ifp, addr, mask);
	}

}

/*
 * Called from kernel.
 *
 * NOTE: We can't make any kernel callbacks while holding cothread lock
 *	 because the cothread lock is not governed by the kernel scheduler
 *	 (so mplock, tokens, etc will not be released).
 */
static void
vke_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct vke_softc *sc = ifp->if_softc;
	struct mbuf *m;
	cothread_t cotd = sc->cotd_tx;
	int count;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	count = 0;
	while ((m = ifsq_dequeue(ifsq)) != NULL) {
		if (vke_txfifo_enqueue(sc, m) != -1) {
			ETHER_BPF_MTAP(ifp, m);
			if (count++ == VKE_CHUNK) {
				cothread_lock(cotd, 0);
				cothread_signal(cotd);
				cothread_unlock(cotd, 0);
				count = 0;
			}
		} else {
			m_freem(m);
		}
	}
	if (count) {
		cothread_lock(cotd, 0);
		cothread_signal(cotd);
		cothread_unlock(cotd, 0);
	}
}

static int
vke_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct vke_softc *sc = ifp->if_softc;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				vke_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vke_stop(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = EOPNOTSUPP;
		/* TODO */
		break;
	case SIOCGIFSTATUS: {
		struct ifstat *ifs = (struct ifstat *)data;
		int len;

		len = strlen(ifs->ascii);
		if (len < sizeof(ifs->ascii)) {
			if (sc->sc_tap_unit >= 0)
				ksnprintf(ifs->ascii + len, sizeof(ifs->ascii) - len,
				    "\tBacked by tap%d\n", sc->sc_tap_unit);
		}
		break;
	}
	case SIOCSIFADDR:
		if (((struct ifaddr *)data)->ifa_addr->sa_family == AF_INET) {
			/*
			 * If we are explicitly requested to change address,
			 * we should invalidate address/netmask passed in
			 * from vkernel command line.
			 */
			sc->sc_addr = 0;
			sc->sc_mask = 0;
		}
		/* FALL THROUGH */
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static int
vke_stop(struct vke_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_flags &= ~IFF_RUNNING;
	ifsq_clr_oactive(ifq_get_subq_default(&ifp->if_snd));

	if (sc) {
		if (sc->cotd_tx) {
			cothread_lock(sc->cotd_tx, 0);
			if (sc->cotd_tx_exit == VKE_COTD_RUN)
				sc->cotd_tx_exit = VKE_COTD_EXIT;
			cothread_signal(sc->cotd_tx);
			cothread_unlock(sc->cotd_tx, 0);
			cothread_delete(&sc->cotd_tx);
		}
		if (sc->cotd_rx) {
			cothread_lock(sc->cotd_rx, 0);
			if (sc->cotd_rx_exit == VKE_COTD_RUN)
				sc->cotd_rx_exit = VKE_COTD_EXIT;
			cothread_signal(sc->cotd_rx);
			cothread_unlock(sc->cotd_rx, 0);
			cothread_delete(&sc->cotd_rx);
		}

		for (i = 0; i < sc->sc_ringsize; i++) {
			if (sc->sc_rxfifo && sc->sc_rxfifo->array[i]) {
				m_freem(sc->sc_rxfifo->array[i]);
				sc->sc_rxfifo->array[i] = NULL;
			}
			if (sc->sc_txfifo && sc->sc_txfifo->array[i]) {
				m_freem(sc->sc_txfifo->array[i]);
				sc->sc_txfifo->array[i] = NULL;
			}
			if (sc->sc_txfifo_done && sc->sc_txfifo_done->array[i]) {
				m_freem(sc->sc_txfifo_done->array[i]);
				sc->sc_txfifo_done->array[i] = NULL;
			}
		}

		if (sc->sc_txfifo) {
			if (sc->sc_txfifo->array)
				kfree(sc->sc_txfifo->array, M_DEVBUF);
			kfree(sc->sc_txfifo, M_DEVBUF);
			sc->sc_txfifo = NULL;
		}

		if (sc->sc_txfifo_done) {
			if (sc->sc_txfifo_done->array)
				kfree(sc->sc_txfifo_done->array, M_DEVBUF);
			kfree(sc->sc_txfifo_done, M_DEVBUF);
			sc->sc_txfifo_done = NULL;
		}

		if (sc->sc_rxfifo) {
			if (sc->sc_rxfifo->array)
				kfree(sc->sc_rxfifo->array, M_DEVBUF);
			kfree(sc->sc_rxfifo, M_DEVBUF);
			sc->sc_rxfifo = NULL;
		}
	}


	return 0;
}

/*
 * vke_rx_intr() is the interrupt function for the receive cothread.
 */
static void
vke_rx_intr(cothread_t cotd)
{
	struct mbuf *m;
	struct mbuf *nm;
	struct vke_softc *sc = cotd->arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	static int count = 0;

	ifnet_serialize_all(ifp);
	cothread_lock(cotd, 0);

	if (sc->cotd_rx_exit != VKE_COTD_RUN) {
		cothread_unlock(cotd, 0);
		ifnet_deserialize_all(ifp);
		return;
	}
	if (sc->cotd_ipackets) {
		IFNET_STAT_INC(ifp, ipackets, 1);
		sc->cotd_ipackets = 0;
	}
	cothread_unlock(cotd, 0);

	while ((m = vke_rxfifo_sniff(sc)) != NULL) {
		nm = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (nm) {
			vke_rxfifo_dequeue(sc, nm);
			ifp->if_input(ifp, m, NULL, -1);
			if (count++ == VKE_CHUNK) {
				cothread_lock(cotd, 0);
				cothread_signal(cotd);
				cothread_unlock(cotd, 0);
				count = 0;
			}
		} else {
			vke_rxfifo_dequeue(sc, m);
		}
	}

	if (count) {
		cothread_lock(cotd, 0);
		cothread_signal(cotd);
		cothread_unlock(cotd, 0);
	}
	ifnet_deserialize_all(ifp);
}

/*
 * vke_tx_intr() is the interrupt function for the transmit cothread.
 * Calls vke_start() to handle processing transmit mbufs.
 */
static void
vke_tx_intr(cothread_t cotd)
{
	struct vke_softc *sc = cotd->arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;

	ifnet_serialize_all(ifp);
	cothread_lock(cotd, 0);
	if (sc->cotd_tx_exit != VKE_COTD_RUN) {
		cothread_unlock(cotd, 0);
		ifnet_deserialize_all(ifp);
		return;
	}
	if (sc->cotd_opackets) {
		IFNET_STAT_INC(ifp, opackets, 1);
		sc->cotd_opackets = 0;
	}
	if (sc->cotd_oerrors) {
		IFNET_STAT_INC(ifp, oerrors, 1);
		sc->cotd_oerrors = 0;
	}
	cothread_unlock(cotd, 0);

	/*
	 * Free TX mbufs that have been processed before starting new
	 * ones going to be pipeline friendly.
	 */
	while ((m = vke_txfifo_done_dequeue(sc, NULL)) != NULL) {
		m_freem(m);
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		if_devstart(ifp);

	ifnet_deserialize_all(ifp);
}

/*
 * vke_rx_thread() is the body of the receive cothread.
 *
 * WARNING!  THIS IS A COTHREAD WHICH HAS NO PER-CPU GLOBALDATA!!!!!
 */
static void
vke_rx_thread(cothread_t cotd)
{
	struct mbuf *m;
	struct vke_softc *sc = cotd->arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	fifo_t fifo = sc->sc_rxfifo;
	fd_set fdset;
	struct timeval tv;
	int count;
	int n;

	/* Select timeout cannot be infinite since we need to check for
	 * the exit flag sc->cotd_rx_exit.
	 */
	tv.tv_sec = 0;
	tv.tv_usec = 500000;

	FD_ZERO(&fdset);
	count = 0;

	while (sc->cotd_rx_exit == VKE_COTD_RUN) {
		/*
		 * Wait for the RX FIFO to be loaded with
		 * empty mbufs.
		 */
		if (NETFIFOINDEX(fifo->windex + 1, sc) ==
		    NETFIFOINDEX(fifo->rindex, sc)) {
			usleep(20000);
			continue;
		}

		/*
		 * Load data into the rx fifo
		 */
		m = fifo->array[NETFIFOINDEX(fifo->windex, sc)];
		if (m == NULL)
			continue;
		n = read(sc->sc_fd, mtod(m, void *), MCLBYTES);
		if (n > 0) {
			/* no mycpu in cothread */
			/*IFNET_STAT_INC(ifp, ipackets, 1);*/
			++sc->cotd_ipackets;
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = m->m_len = n;
			cpu_sfence();
			++fifo->windex;
			if (count++ == VKE_CHUNK) {
				cothread_intr(cotd);
				count = 0;
			}
		} else {
			if (count) {
				cothread_intr(cotd);
				count = 0;
			}
			FD_SET(sc->sc_fd, &fdset);

			if (select(sc->sc_fd + 1, &fdset, NULL, NULL, &tv) == -1) {
				fprintf(stderr,
					VKE_DEVNAME "%d: select failed for "
					"TAP device\n", sc->sc_unit);
				usleep(1000000);
			}
		}
	}
	cpu_sfence();
	sc->cotd_rx_exit = VKE_COTD_DEAD;
}

/*
 * vke_tx_thread() is the body of the transmit cothread.
 *
 * WARNING!  THIS IS A COTHREAD WHICH HAS NO PER-CPU GLOBALDATA!!!!!
 */
static void
vke_tx_thread(cothread_t cotd)
{
	struct mbuf *m;
	struct vke_softc *sc = cotd->arg;
	/*struct ifnet *ifp = &sc->arpcom.ac_if;*/
	int count = 0;

	while (sc->cotd_tx_exit == VKE_COTD_RUN) {
		/*
		 * Write outgoing packets to the TAP interface
		 */
		m = vke_txfifo_dequeue(sc);
		if (m) {
			if (m->m_pkthdr.len <= MCLBYTES) {
				m_copydata(m, 0, m->m_pkthdr.len, sc->sc_txbuf);
				sc->sc_txbuf_len = m->m_pkthdr.len;

				if (write(sc->sc_fd, sc->sc_txbuf,
					  sc->sc_txbuf_len) < 0) {
					/* no mycpu in cothread */
					/*IFNET_STAT_INC(ifp, oerrors, 1);*/
					++sc->cotd_oerrors;
				} else {
					/* no mycpu in cothread */
					/*IFNET_STAT_INC(ifp, opackets, 1);*/
					++sc->cotd_opackets;
				}
			}
			if (count++ == VKE_CHUNK) {
				cothread_intr(cotd);
				count = 0;
			}
			vke_txfifo_done_enqueue(sc, m);
		} else {
			if (count) {
				cothread_intr(cotd);
				count = 0;
			}
			cothread_lock(cotd, 1);
			if (vke_txfifo_empty(sc))
				cothread_wait(cotd);
			cothread_unlock(cotd, 1);
		}
	}
	cpu_sfence();
	sc->cotd_tx_exit = VKE_COTD_DEAD;
}

static int
vke_attach(const struct vknetif_info *info, int unit)
{
	struct vke_softc *sc;
	struct ifnet *ifp;
	struct tapinfo tapinfo;
	uint8_t enaddr[ETHER_ADDR_LEN];
	int nmbufs;
	int fd;

	KKASSERT(info->tap_fd >= 0);
	fd = info->tap_fd;

	if (info->enaddr) {
		/*
		 * enaddr is supplied
		 */
		bcopy(info->enaddr, enaddr, ETHER_ADDR_LEN);
	} else {
		/*
		 * This is only a TAP device if tap_unit is non-zero.  If
		 * connecting to a virtual socket we generate a unique MAC.
		 *
		 * WARNING: enaddr[0] bit 0 is the multicast bit, when
		 *          randomizing enaddr[] just leave the first
		 *	    two bytes 00 00 for now.
		 */
		bzero(enaddr, sizeof(enaddr));
		if (info->tap_unit >= 0) {
			if (ioctl(fd, TAPGIFINFO, &tapinfo) < 0) {
				kprintf(VKE_DEVNAME "%d: ioctl(TAPGIFINFO) "
					"failed: %s\n", unit, strerror(errno));
				return ENXIO;
			}

			if (ioctl(fd, SIOCGIFADDR, enaddr) < 0) {
				kprintf(VKE_DEVNAME "%d: ioctl(SIOCGIFADDR) "
					"failed: %s\n", unit, strerror(errno));
				return ENXIO;
			}
		} else {
			int fd = open("/dev/urandom", O_RDONLY);
			if (fd >= 0) {
				read(fd, enaddr + 2, 4);
				close(fd);
			}
			enaddr[4] = (int)getpid() >> 8;
			enaddr[5] = (int)getpid() & 255;

		}
		enaddr[1] += 1;
	}
	if (ETHER_IS_MULTICAST(enaddr)) {
		kprintf(VKE_DEVNAME "%d: illegal MULTICAST ether mac!\n", unit);
		return ENXIO;
	}

	sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_txbuf = kmalloc(MCLBYTES, M_DEVBUF, M_WAITOK);
	sc->sc_fd = fd;
	sc->sc_unit = unit;
	sc->sc_tap_unit = info->tap_unit;
	sc->sc_addr = info->netif_addr;
	sc->sc_mask = info->netif_mask;

	if (vke_max_ringsize == 0) {
		nmbufs = nmbclusters / (NetifNum * 2);
		sc->sc_ringsize = LOW_POW_2(nmbufs);
		if (sc->sc_ringsize > VKE_DEFAULT_RINGSIZE)
			sc->sc_ringsize = VKE_DEFAULT_RINGSIZE;
	} else if (vke_max_ringsize >= VKE_CHUNK) {	/* Tunable specified */
		sc->sc_ringsize = LOW_POW_2(vke_max_ringsize);
	} else {
		sc->sc_ringsize = LOW_POW_2(VKE_CHUNK);
	}

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, VKE_DEVNAME, sc->sc_unit);

	/* NB: after if_initname() */
	sysctl_ctx_init(&sc->sc_sysctl_ctx);
	sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
					     SYSCTL_STATIC_CHILDREN(_hw),
					     OID_AUTO, ifp->if_xname,
					     CTLFLAG_RD, 0, "");
	if (sc->sc_sysctl_tree == NULL) {
		kprintf(VKE_DEVNAME "%d: can't add sysctl node\n", unit);
	} else {
		SYSCTL_ADD_INT(&sc->sc_sysctl_ctx,
			       SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			       OID_AUTO, "tap_unit",
			       CTLFLAG_RD, &sc->sc_tap_unit, 0,
			       "Backend tap(4) unit");
	}

	ifp->if_softc = sc;
	ifp->if_ioctl = vke_ioctl;
	ifp->if_start = vke_start;
	ifp->if_init = vke_init;
	ifp->if_mtu = tapinfo.mtu;
	ifp->if_baudrate = tapinfo.baudrate;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* TODO: if_media */

	ether_ifattach(ifp, enaddr, NULL);

	if (bootverbose && sc->sc_addr != 0) {
		if_printf(ifp, "pre-configured "
		    "address 0x%08x, netmask 0x%08x, %d mbuf clusters\n",
		    ntohl(sc->sc_addr), ntohl(sc->sc_mask), sc->sc_ringsize);
	}

	return 0;
}

static int
vke_init_addr(struct ifnet *ifp, in_addr_t addr, in_addr_t mask)
{
	struct ifaliasreq ifra;
	struct sockaddr_in *sin;
	int ret;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (bootverbose) {
		if_printf(ifp, "add pre-configured "
			  "address 0x%08x, netmask 0x%08x\n",
			  ntohl(addr), ntohl(mask));
	}

	bzero(&ifra, sizeof(ifra));

	/* NB: no need to set ifaliasreq.ifra_name */

	sin = (struct sockaddr_in *)&ifra.ifra_addr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_addr.s_addr = addr;

	if (mask != 0) {
		sin = (struct sockaddr_in *)&ifra.ifra_mask;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr.s_addr = mask;
	}

	/*
	 * Temporarily release serializer, in_control() will hold
	 * it again before calling ifnet.if_ioctl().
	 */
	ifnet_deserialize_all(ifp);
	ret = in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);
	ifnet_serialize_all(ifp);

	return ret;
}
