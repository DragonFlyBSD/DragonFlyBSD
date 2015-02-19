/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)bpf.c	8.2 (Berkeley) 3/28/94
 *
 * $FreeBSD: src/sys/net/bpf.c,v 1.59.2.12 2002/04/14 21:41:48 luigi Exp $
 */

#include "use_bpf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/ttycom.h>
#include <sys/filedesc.h>

#include <sys/event.h>

#include <sys/socket.h>
#include <sys/vnode.h>

#include <sys/thread2.h>

#include <net/if.h>
#include <net/bpf.h>
#include <net/bpfdesc.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <sys/devfs.h>

struct netmsg_bpf_output {
	struct netmsg_base base;
	struct mbuf	*nm_mbuf;
	struct ifnet	*nm_ifp;
	struct sockaddr	*nm_dst;
};

MALLOC_DEFINE(M_BPF, "BPF", "BPF data");
DEVFS_DECLARE_CLONE_BITMAP(bpf);

#if NBPF <= 1
#define BPF_PREALLOCATED_UNITS	4
#else
#define BPF_PREALLOCATED_UNITS	NBPF
#endif

#if NBPF > 0

/*
 * The default read buffer size is patchable.
 */
static int bpf_bufsize = BPF_DEFAULTBUFSIZE;
SYSCTL_INT(_debug, OID_AUTO, bpf_bufsize, CTLFLAG_RW,
   &bpf_bufsize, 0, "Current size of bpf buffer");
int bpf_maxbufsize = BPF_MAXBUFSIZE;
SYSCTL_INT(_debug, OID_AUTO, bpf_maxbufsize, CTLFLAG_RW,
   &bpf_maxbufsize, 0, "Maximum size of bpf buffer");

/*
 *  bpf_iflist is the list of interfaces; each corresponds to an ifnet
 */
static struct bpf_if	*bpf_iflist;

static struct lwkt_token bpf_token = LWKT_TOKEN_INITIALIZER(bpf_token);

static int	bpf_allocbufs(struct bpf_d *);
static void	bpf_attachd(struct bpf_d *d, struct bpf_if *bp);
static void	bpf_detachd(struct bpf_d *d);
static void	bpf_resetd(struct bpf_d *);
static void	bpf_freed(struct bpf_d *);
static void	bpf_mcopy(const void *, void *, size_t);
static int	bpf_movein(struct uio *, int, struct mbuf **,
			   struct sockaddr *, int *, struct bpf_insn *);
static int	bpf_setif(struct bpf_d *, struct ifreq *);
static void	bpf_timed_out(void *);
static void	bpf_wakeup(struct bpf_d *);
static void	catchpacket(struct bpf_d *, u_char *, u_int, u_int,
			    void (*)(const void *, void *, size_t),
			    const struct timeval *);
static int	bpf_setf(struct bpf_d *, struct bpf_program *, u_long cmd);
static int	bpf_getdltlist(struct bpf_d *, struct bpf_dltlist *);
static int	bpf_setdlt(struct bpf_d *, u_int);
static void	bpf_drvinit(void *unused);
static void	bpf_filter_detach(struct knote *kn);
static int	bpf_filter_read(struct knote *kn, long hint);

static d_open_t		bpfopen;
static d_clone_t	bpfclone;
static d_close_t	bpfclose;
static d_read_t		bpfread;
static d_write_t	bpfwrite;
static d_ioctl_t	bpfioctl;
static d_kqfilter_t	bpfkqfilter;

#define CDEV_MAJOR 23
static struct dev_ops bpf_ops = {
	{ "bpf", 0, D_MPSAFE },
	.d_open =	bpfopen,
	.d_close =	bpfclose,
	.d_read =	bpfread,
	.d_write =	bpfwrite,
	.d_ioctl =	bpfioctl,
	.d_kqfilter =	bpfkqfilter
};


static int
bpf_movein(struct uio *uio, int linktype, struct mbuf **mp,
	   struct sockaddr *sockp, int *datlen, struct bpf_insn *wfilter)
{
	struct mbuf *m;
	int error;
	int len;
	int hlen;
	int slen;

	*datlen = 0;
	*mp = NULL;

	/*
	 * Build a sockaddr based on the data link layer type.
	 * We do this at this level because the ethernet header
	 * is copied directly into the data field of the sockaddr.
	 * In the case of SLIP, there is no header and the packet
	 * is forwarded as is.
	 * Also, we are careful to leave room at the front of the mbuf
	 * for the link level header.
	 */
	switch (linktype) {
	case DLT_SLIP:
		sockp->sa_family = AF_INET;
		hlen = 0;
		break;

	case DLT_EN10MB:
		sockp->sa_family = AF_UNSPEC;
		/* XXX Would MAXLINKHDR be better? */
		hlen = sizeof(struct ether_header);
		break;

	case DLT_RAW:
	case DLT_NULL:
		sockp->sa_family = AF_UNSPEC;
		hlen = 0;
		break;

	case DLT_ATM_RFC1483:
		/*
		 * en atm driver requires 4-byte atm pseudo header.
		 * though it isn't standard, vpi:vci needs to be
		 * specified anyway.
		 */
		sockp->sa_family = AF_UNSPEC;
		hlen = 12;	/* XXX 4(ATM_PH) + 3(LLC) + 5(SNAP) */
		break;

	case DLT_PPP:
		sockp->sa_family = AF_UNSPEC;
		hlen = 4;	/* This should match PPP_HDRLEN */
		break;

	default:
		return(EIO);
	}

	len = uio->uio_resid;
	*datlen = len - hlen;
	if ((unsigned)len > MCLBYTES)
		return(EIO);

	m = m_getl(len, M_WAITOK, MT_DATA, M_PKTHDR, NULL);
	if (m == NULL)
		return(ENOBUFS);
	m->m_pkthdr.len = m->m_len = len;
	m->m_pkthdr.rcvif = NULL;
	*mp = m;

	if (m->m_len < hlen) {
		error = EPERM;
		goto bad;
	}

	error = uiomove(mtod(m, u_char *), len, uio);
	if (error)
		goto bad;

	slen = bpf_filter(wfilter, mtod(m, u_char *), len, len);
	if (slen == 0) {
		error = EPERM;
		goto bad;
	}

	/*
	 * Make room for link header, and copy it to sockaddr.
	 */
	if (hlen != 0) {
		bcopy(m->m_data, sockp->sa_data, hlen);
		m->m_pkthdr.len -= hlen;
		m->m_len -= hlen;
		m->m_data += hlen; /* XXX */
	}
	return (0);
bad:
	m_freem(m);
	return(error);
}

/*
 * Attach file to the bpf interface, i.e. make d listen on bp.
 * Must be called at splimp.
 */
static void
bpf_attachd(struct bpf_d *d, struct bpf_if *bp)
{
	/*
	 * Point d at bp, and add d to the interface's list of listeners.
	 * Finally, point the driver's bpf cookie at the interface so
	 * it will divert packets to bpf.
	 */
	lwkt_gettoken(&bpf_token);
	d->bd_bif = bp;
	SLIST_INSERT_HEAD(&bp->bif_dlist, d, bd_next);
	*bp->bif_driverp = bp;

	EVENTHANDLER_INVOKE(bpf_track, bp->bif_ifp, bp->bif_dlt, 1);
	lwkt_reltoken(&bpf_token);
}

/*
 * Detach a file from its interface.
 */
static void
bpf_detachd(struct bpf_d *d)
{
	int error;
	struct bpf_if *bp;
	struct ifnet *ifp;

	lwkt_gettoken(&bpf_token);
	bp = d->bd_bif;
	ifp = bp->bif_ifp;

	/* Remove d from the interface's descriptor list. */
	SLIST_REMOVE(&bp->bif_dlist, d, bpf_d, bd_next);

	if (SLIST_EMPTY(&bp->bif_dlist)) {
		/*
		 * Let the driver know that there are no more listeners.
		 */
		*bp->bif_driverp = NULL;
	}
	d->bd_bif = NULL;

	EVENTHANDLER_INVOKE(bpf_track, ifp, bp->bif_dlt, 0);

	/*
	 * Check if this descriptor had requested promiscuous mode.
	 * If so, turn it off.
	 */
	if (d->bd_promisc) {
		d->bd_promisc = 0;
		error = ifpromisc(ifp, 0);
		if (error != 0 && error != ENXIO) {
			/*
			 * ENXIO can happen if a pccard is unplugged,
			 * Something is really wrong if we were able to put
			 * the driver into promiscuous mode, but can't
			 * take it out.
			 */
			if_printf(ifp, "bpf_detach: ifpromisc failed(%d)\n",
				  error);
		}
	}
	lwkt_reltoken(&bpf_token);
}

/*
 * Open ethernet device.  Returns ENXIO for illegal minor device number,
 * EBUSY if file is open by another process.
 */
/* ARGSUSED */
static int
bpfopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bpf_d *d;

	lwkt_gettoken(&bpf_token);
	if (ap->a_cred->cr_prison) {
		lwkt_reltoken(&bpf_token);
		return(EPERM);
	}

	d = dev->si_drv1;
	/*
	 * Each minor can be opened by only one process.  If the requested
	 * minor is in use, return EBUSY.
	 */
	if (d != NULL) {
		lwkt_reltoken(&bpf_token);
		return(EBUSY);
	}

	d = kmalloc(sizeof *d, M_BPF, M_WAITOK | M_ZERO);
	dev->si_drv1 = d;
	d->bd_bufsize = bpf_bufsize;
	d->bd_sig = SIGIO;
	d->bd_seesent = 1;
	callout_init(&d->bd_callout);
	lwkt_reltoken(&bpf_token);

	return(0);
}

static int
bpfclone(struct dev_clone_args *ap)
{
	int unit;

	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(bpf), 0);
	ap->a_dev = make_only_dev(&bpf_ops, unit, 0, 0, 0600, "bpf%d", unit);

	return 0;
}

/*
 * Close the descriptor by detaching it from its interface,
 * deallocating its buffers, and marking it free.
 */
/* ARGSUSED */
static int
bpfclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bpf_d *d = dev->si_drv1;

	lwkt_gettoken(&bpf_token);
	funsetown(&d->bd_sigio);
	if (d->bd_state == BPF_WAITING)
		callout_stop(&d->bd_callout);
	d->bd_state = BPF_IDLE;
	if (d->bd_bif != NULL)
		bpf_detachd(d);
	bpf_freed(d);
	dev->si_drv1 = NULL;
	if (dev->si_uminor >= BPF_PREALLOCATED_UNITS) {
		devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(bpf), dev->si_uminor);
		destroy_dev(dev);
	}
	kfree(d, M_BPF);
	lwkt_reltoken(&bpf_token);

	return(0);
}

/*
 * Rotate the packet buffers in descriptor d.  Move the store buffer
 * into the hold slot, and the free buffer into the store slot.
 * Zero the length of the new store buffer.
 */
#define ROTATE_BUFFERS(d) \
	(d)->bd_hbuf = (d)->bd_sbuf; \
	(d)->bd_hlen = (d)->bd_slen; \
	(d)->bd_sbuf = (d)->bd_fbuf; \
	(d)->bd_slen = 0; \
	(d)->bd_fbuf = NULL;
/*
 *  bpfread - read next chunk of packets from buffers
 */
static int
bpfread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bpf_d *d = dev->si_drv1;
	int timed_out;
	int error;

	lwkt_gettoken(&bpf_token);
	/*
	 * Restrict application to use a buffer the same size as
	 * as kernel buffers.
	 */
	if (ap->a_uio->uio_resid != d->bd_bufsize) {
		lwkt_reltoken(&bpf_token);
		return(EINVAL);
	}

	if (d->bd_state == BPF_WAITING)
		callout_stop(&d->bd_callout);
	timed_out = (d->bd_state == BPF_TIMED_OUT);
	d->bd_state = BPF_IDLE;
	/*
	 * If the hold buffer is empty, then do a timed sleep, which
	 * ends when the timeout expires or when enough packets
	 * have arrived to fill the store buffer.
	 */
	while (d->bd_hbuf == NULL) {
		if ((d->bd_immediate || (ap->a_ioflag & IO_NDELAY) || timed_out)
		    && d->bd_slen != 0) {
			/*
			 * A packet(s) either arrived since the previous,
			 * We're in immediate mode, or are reading
			 * in non-blocking mode, and a packet(s)
			 * either arrived since the previous
			 * read or arrived while we were asleep.
			 * Rotate the buffers and return what's here.
			 */
			ROTATE_BUFFERS(d);
			break;
		}

		/*
		 * No data is available, check to see if the bpf device
		 * is still pointed at a real interface.  If not, return
		 * ENXIO so that the userland process knows to rebind
		 * it before using it again.
		 */
		if (d->bd_bif == NULL) {
			lwkt_reltoken(&bpf_token);
			return(ENXIO);
		}

		if (ap->a_ioflag & IO_NDELAY) {
			lwkt_reltoken(&bpf_token);
			return(EWOULDBLOCK);
		}
		error = tsleep(d, PCATCH, "bpf", d->bd_rtout);
		if (error == EINTR || error == ERESTART) {
			lwkt_reltoken(&bpf_token);
			return(error);
		}
		if (error == EWOULDBLOCK) {
			/*
			 * On a timeout, return what's in the buffer,
			 * which may be nothing.  If there is something
			 * in the store buffer, we can rotate the buffers.
			 */
			if (d->bd_hbuf)
				/*
				 * We filled up the buffer in between
				 * getting the timeout and arriving
				 * here, so we don't need to rotate.
				 */
				break;

			if (d->bd_slen == 0) {
				lwkt_reltoken(&bpf_token);
				return(0);
			}
			ROTATE_BUFFERS(d);
			break;
		}
	}
	/*
	 * At this point, we know we have something in the hold slot.
	 */

	/*
	 * Move data from hold buffer into user space.
	 * We know the entire buffer is transferred since
	 * we checked above that the read buffer is bpf_bufsize bytes.
	 */
	error = uiomove(d->bd_hbuf, d->bd_hlen, ap->a_uio);

	d->bd_fbuf = d->bd_hbuf;
	d->bd_hbuf = NULL;
	d->bd_hlen = 0;
	lwkt_reltoken(&bpf_token);

	return(error);
}


/*
 * If there are processes sleeping on this descriptor, wake them up.
 */
static void
bpf_wakeup(struct bpf_d *d)
{
	if (d->bd_state == BPF_WAITING) {
		callout_stop(&d->bd_callout);
		d->bd_state = BPF_IDLE;
	}
	wakeup(d);
	if (d->bd_async && d->bd_sig && d->bd_sigio)
		pgsigio(d->bd_sigio, d->bd_sig, 0);

	KNOTE(&d->bd_kq.ki_note, 0);
}

static void
bpf_timed_out(void *arg)
{
	struct bpf_d *d = (struct bpf_d *)arg;

	if (d->bd_state == BPF_WAITING) {
		d->bd_state = BPF_TIMED_OUT;
		if (d->bd_slen != 0)
			bpf_wakeup(d);
	}
}

static void
bpf_output_dispatch(netmsg_t msg)
{
	struct netmsg_bpf_output *bmsg = (struct netmsg_bpf_output *)msg;
	struct ifnet *ifp = bmsg->nm_ifp;
	int error;

	/*
	 * The driver frees the mbuf.
	 */
	error = ifp->if_output(ifp, bmsg->nm_mbuf, bmsg->nm_dst, NULL);
	lwkt_replymsg(&msg->lmsg, error);
}

static int
bpfwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bpf_d *d = dev->si_drv1;
	struct ifnet *ifp;
	struct mbuf *m;
	int error, ret;
	struct sockaddr dst;
	int datlen;
	struct netmsg_bpf_output bmsg;

	lwkt_gettoken(&bpf_token);
	if (d->bd_bif == NULL) {
		lwkt_reltoken(&bpf_token);
		return(ENXIO);
	}

	ifp = d->bd_bif->bif_ifp;

	if (ap->a_uio->uio_resid == 0) {
		lwkt_reltoken(&bpf_token);
		return(0);
	}

	error = bpf_movein(ap->a_uio, (int)d->bd_bif->bif_dlt, &m,
			   &dst, &datlen, d->bd_wfilter);
	if (error) {
		lwkt_reltoken(&bpf_token);
		return(error);
	}

	if (datlen > ifp->if_mtu) {
		m_freem(m);
		lwkt_reltoken(&bpf_token);
		return(EMSGSIZE);
	}

	if (d->bd_hdrcmplt)
		dst.sa_family = pseudo_AF_HDRCMPLT;

	netmsg_init(&bmsg.base, NULL, &curthread->td_msgport,
		    0, bpf_output_dispatch);
	bmsg.nm_mbuf = m;
	bmsg.nm_ifp = ifp;
	bmsg.nm_dst = &dst;

	ret = lwkt_domsg(netisr_cpuport(0), &bmsg.base.lmsg, 0);
	lwkt_reltoken(&bpf_token);

	return ret;
}

/*
 * Reset a descriptor by flushing its packet buffer and clearing the
 * receive and drop counts.  Should be called at splimp.
 */
static void
bpf_resetd(struct bpf_d *d)
{
	if (d->bd_hbuf) {
		/* Free the hold buffer. */
		d->bd_fbuf = d->bd_hbuf;
		d->bd_hbuf = NULL;
	}
	d->bd_slen = 0;
	d->bd_hlen = 0;
	d->bd_rcount = 0;
	d->bd_dcount = 0;
}

/*
 *  FIONREAD		Check for read packet available.
 *  SIOCGIFADDR		Get interface address - convenient hook to driver.
 *  BIOCGBLEN		Get buffer len [for read()].
 *  BIOCSETF		Set ethernet read filter.
 *  BIOCSETWF		Set ethernet write filter.
 *  BIOCFLUSH		Flush read packet buffer.
 *  BIOCPROMISC		Put interface into promiscuous mode.
 *  BIOCGDLT		Get link layer type.
 *  BIOCGETIF		Get interface name.
 *  BIOCSETIF		Set interface.
 *  BIOCSRTIMEOUT	Set read timeout.
 *  BIOCGRTIMEOUT	Get read timeout.
 *  BIOCGSTATS		Get packet stats.
 *  BIOCIMMEDIATE	Set immediate mode.
 *  BIOCVERSION		Get filter language version.
 *  BIOCGHDRCMPLT	Get "header already complete" flag
 *  BIOCSHDRCMPLT	Set "header already complete" flag
 *  BIOCGSEESENT	Get "see packets sent" flag
 *  BIOCSSEESENT	Set "see packets sent" flag
 *  BIOCLOCK		Set "locked" flag
 */
/* ARGSUSED */
static int
bpfioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bpf_d *d = dev->si_drv1;
	int error = 0;

	lwkt_gettoken(&bpf_token);
	if (d->bd_state == BPF_WAITING)
		callout_stop(&d->bd_callout);
	d->bd_state = BPF_IDLE;

	if (d->bd_locked == 1) {
		switch (ap->a_cmd) {
		case BIOCGBLEN:
		case BIOCFLUSH:
		case BIOCGDLT:
		case BIOCGDLTLIST: 
		case BIOCGETIF:
		case BIOCGRTIMEOUT:
		case BIOCGSTATS:
		case BIOCVERSION:
		case BIOCGRSIG:
		case BIOCGHDRCMPLT:
		case FIONREAD:
		case BIOCLOCK:
		case BIOCSRTIMEOUT:
		case BIOCIMMEDIATE:
		case TIOCGPGRP:
			break;
		default:
			lwkt_reltoken(&bpf_token);
			return (EPERM);
		}
	}
	switch (ap->a_cmd) {
	default:
		error = EINVAL;
		break;

	/*
	 * Check for read packet available.
	 */
	case FIONREAD:
		{
			int n;

			n = d->bd_slen;
			if (d->bd_hbuf)
				n += d->bd_hlen;

			*(int *)ap->a_data = n;
			break;
		}

	case SIOCGIFADDR:
		{
			struct ifnet *ifp;

			if (d->bd_bif == NULL) {
				error = EINVAL;
			} else {
				ifp = d->bd_bif->bif_ifp;
				ifnet_serialize_all(ifp);
				error = ifp->if_ioctl(ifp, ap->a_cmd,
						      ap->a_data, ap->a_cred);
				ifnet_deserialize_all(ifp);
			}
			break;
		}

	/*
	 * Get buffer len [for read()].
	 */
	case BIOCGBLEN:
		*(u_int *)ap->a_data = d->bd_bufsize;
		break;

	/*
	 * Set buffer length.
	 */
	case BIOCSBLEN:
		if (d->bd_bif != NULL) {
			error = EINVAL;
		} else {
			u_int size = *(u_int *)ap->a_data;

			if (size > bpf_maxbufsize)
				*(u_int *)ap->a_data = size = bpf_maxbufsize;
			else if (size < BPF_MINBUFSIZE)
				*(u_int *)ap->a_data = size = BPF_MINBUFSIZE;
			d->bd_bufsize = size;
		}
		break;

	/*
	 * Set link layer read filter.
	 */
	case BIOCSETF:
	case BIOCSETWF:
		error = bpf_setf(d, (struct bpf_program *)ap->a_data, 
			ap->a_cmd);
		break;

	/*
	 * Flush read packet buffer.
	 */
	case BIOCFLUSH:
		bpf_resetd(d);
		break;

	/*
	 * Put interface into promiscuous mode.
	 */
	case BIOCPROMISC:
		if (d->bd_bif == NULL) {
			/*
			 * No interface attached yet.
			 */
			error = EINVAL;
			break;
		}
		if (d->bd_promisc == 0) {
			error = ifpromisc(d->bd_bif->bif_ifp, 1);
			if (error == 0)
				d->bd_promisc = 1;
		}
		break;

	/*
	 * Get device parameters.
	 */
	case BIOCGDLT:
		if (d->bd_bif == NULL)
			error = EINVAL;
		else
			*(u_int *)ap->a_data = d->bd_bif->bif_dlt;
		break;

	/*
	 * Get a list of supported data link types.
	 */
	case BIOCGDLTLIST:
		if (d->bd_bif == NULL) {
			error = EINVAL;
		} else {
			error = bpf_getdltlist(d,
				(struct bpf_dltlist *)ap->a_data);
		}
		break;

	/*
	 * Set data link type.
	 */
	case BIOCSDLT:
		if (d->bd_bif == NULL)
			error = EINVAL;
		else
			error = bpf_setdlt(d, *(u_int *)ap->a_data);
		break;

	/*
	 * Get interface name.
	 */
	case BIOCGETIF:
		if (d->bd_bif == NULL) {
			error = EINVAL;
		} else {
			struct ifnet *const ifp = d->bd_bif->bif_ifp;
			struct ifreq *const ifr = (struct ifreq *)ap->a_data;

			strlcpy(ifr->ifr_name, ifp->if_xname,
				sizeof ifr->ifr_name);
		}
		break;

	/*
	 * Set interface.
	 */
	case BIOCSETIF:
		error = bpf_setif(d, (struct ifreq *)ap->a_data);
		break;

	/*
	 * Set read timeout.
	 */
	case BIOCSRTIMEOUT:
		{
			struct timeval *tv = (struct timeval *)ap->a_data;

			/*
			 * Subtract 1 tick from tvtohz() since this isn't
			 * a one-shot timer.
			 */
			if ((error = itimerfix(tv)) == 0)
				d->bd_rtout = tvtohz_low(tv);
			break;
		}

	/*
	 * Get read timeout.
	 */
	case BIOCGRTIMEOUT:
		{
			struct timeval *tv = (struct timeval *)ap->a_data;

			tv->tv_sec = d->bd_rtout / hz;
			tv->tv_usec = (d->bd_rtout % hz) * ustick;
			break;
		}

	/*
	 * Get packet stats.
	 */
	case BIOCGSTATS:
		{
			struct bpf_stat *bs = (struct bpf_stat *)ap->a_data;

			bs->bs_recv = d->bd_rcount;
			bs->bs_drop = d->bd_dcount;
			break;
		}

	/*
	 * Set immediate mode.
	 */
	case BIOCIMMEDIATE:
		d->bd_immediate = *(u_int *)ap->a_data;
		break;

	case BIOCVERSION:
		{
			struct bpf_version *bv = (struct bpf_version *)ap->a_data;

			bv->bv_major = BPF_MAJOR_VERSION;
			bv->bv_minor = BPF_MINOR_VERSION;
			break;
		}

	/*
	 * Get "header already complete" flag
	 */
	case BIOCGHDRCMPLT:
		*(u_int *)ap->a_data = d->bd_hdrcmplt;
		break;

	/*
	 * Set "header already complete" flag
	 */
	case BIOCSHDRCMPLT:
		d->bd_hdrcmplt = *(u_int *)ap->a_data ? 1 : 0;
		break;

	/*
	 * Get "see sent packets" flag
	 */
	case BIOCGSEESENT:
		*(u_int *)ap->a_data = d->bd_seesent;
		break;

	/*
	 * Set "see sent packets" flag
	 */
	case BIOCSSEESENT:
		d->bd_seesent = *(u_int *)ap->a_data;
		break;

	case FIOASYNC:		/* Send signal on receive packets */
		d->bd_async = *(int *)ap->a_data;
		break;

	case FIOSETOWN:
		error = fsetown(*(int *)ap->a_data, &d->bd_sigio);
		break;

	case FIOGETOWN:
		*(int *)ap->a_data = fgetown(&d->bd_sigio);
		break;

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		error = fsetown(-(*(int *)ap->a_data), &d->bd_sigio);
		break;

	/* This is deprecated, FIOGETOWN should be used instead. */
	case TIOCGPGRP:
		*(int *)ap->a_data = -fgetown(&d->bd_sigio);
		break;

	case BIOCSRSIG:		/* Set receive signal */
		{
			u_int sig;

			sig = *(u_int *)ap->a_data;

			if (sig >= NSIG)
				error = EINVAL;
			else
				d->bd_sig = sig;
			break;
		}
	case BIOCGRSIG:
		*(u_int *)ap->a_data = d->bd_sig;
		break;
	case BIOCLOCK:
		d->bd_locked = 1;
		break;
	}
	lwkt_reltoken(&bpf_token);

	return(error);
}

/*
 * Set d's packet filter program to fp.  If this file already has a filter,
 * free it and replace it.  Returns EINVAL for bogus requests.
 */
static int
bpf_setf(struct bpf_d *d, struct bpf_program *fp, u_long cmd)
{
	struct bpf_insn *fcode, *old;
	u_int wfilter, flen, size;

	if (cmd == BIOCSETWF) {
		old = d->bd_wfilter;
		wfilter = 1;
	} else {
		wfilter = 0;
		old = d->bd_rfilter;
	}
	if (fp->bf_insns == NULL) {
		if (fp->bf_len != 0)
			return(EINVAL);
		if (wfilter)
			d->bd_wfilter = NULL;
		else
			d->bd_rfilter = NULL;
		bpf_resetd(d);
		if (old != NULL)
			kfree(old, M_BPF);
		return(0);
	}
	flen = fp->bf_len;
	if (flen > BPF_MAXINSNS)
		return(EINVAL);

	size = flen * sizeof *fp->bf_insns;
	fcode = (struct bpf_insn *)kmalloc(size, M_BPF, M_WAITOK);
	if (copyin(fp->bf_insns, fcode, size) == 0 &&
	    bpf_validate(fcode, (int)flen)) {
		if (wfilter)
			d->bd_wfilter = fcode;
		else
			d->bd_rfilter = fcode;
		bpf_resetd(d);
		if (old != NULL)
			kfree(old, M_BPF);

		return(0);
	}
	kfree(fcode, M_BPF);
	return(EINVAL);
}

/*
 * Detach a file from its current interface (if attached at all) and attach
 * to the interface indicated by the name stored in ifr.
 * Return an errno or 0.
 */
static int
bpf_setif(struct bpf_d *d, struct ifreq *ifr)
{
	struct bpf_if *bp;
	int error;
	struct ifnet *theywant;

	ifnet_lock();

	theywant = ifunit(ifr->ifr_name);
	if (theywant == NULL) {
		ifnet_unlock();
		return(ENXIO);
	}

	/*
	 * Look through attached interfaces for the named one.
	 */
	for (bp = bpf_iflist; bp != NULL; bp = bp->bif_next) {
		struct ifnet *ifp = bp->bif_ifp;

		if (ifp == NULL || ifp != theywant)
			continue;
		/* skip additional entry */
		if (bp->bif_driverp != &ifp->if_bpf)
			continue;
		/*
		 * We found the requested interface.
		 * Allocate the packet buffers if we need to.
		 * If we're already attached to requested interface,
		 * just flush the buffer.
		 */
		if (d->bd_sbuf == NULL) {
			error = bpf_allocbufs(d);
			if (error != 0) {
				ifnet_unlock();
				return(error);
			}
		}
		if (bp != d->bd_bif) {
			if (d->bd_bif != NULL) {
				/*
				 * Detach if attached to something else.
				 */
				bpf_detachd(d);
			}

			bpf_attachd(d, bp);
		}
		bpf_resetd(d);

		ifnet_unlock();
		return(0);
	}

	ifnet_unlock();

	/* Not found. */
	return(ENXIO);
}

static struct filterops bpf_read_filtops =
	{ FILTEROP_ISFD, NULL, bpf_filter_detach, bpf_filter_read };

static int
bpfkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct klist *klist;
	struct bpf_d *d;

	lwkt_gettoken(&bpf_token);
	d = dev->si_drv1;
	if (d->bd_bif == NULL) {
		ap->a_result = 1;
		lwkt_reltoken(&bpf_token);
		return (0);
	}

	ap->a_result = 0;
	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &bpf_read_filtops;
		kn->kn_hook = (caddr_t)d;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		lwkt_reltoken(&bpf_token);
		return (0);
	}

	klist = &d->bd_kq.ki_note;
	knote_insert(klist, kn);
	lwkt_reltoken(&bpf_token);

	return (0);
}

static void
bpf_filter_detach(struct knote *kn)
{
	struct klist *klist;
	struct bpf_d *d;

	d = (struct bpf_d *)kn->kn_hook;
	klist = &d->bd_kq.ki_note;
	knote_remove(klist, kn);
}

static int
bpf_filter_read(struct knote *kn, long hint)
{
	struct bpf_d *d;
	int ready = 0;

	d = (struct bpf_d *)kn->kn_hook;
	if (d->bd_hlen != 0 ||
	    ((d->bd_immediate || d->bd_state == BPF_TIMED_OUT) &&
	    d->bd_slen != 0)) {
		ready = 1;
	} else {
		/* Start the read timeout if necessary. */
		if (d->bd_rtout > 0 && d->bd_state == BPF_IDLE) {
			callout_reset(&d->bd_callout, d->bd_rtout,
			    bpf_timed_out, d);
			d->bd_state = BPF_WAITING;
		}
	}

	return (ready);
}


/*
 * Process the packet pkt of length pktlen.  The packet is parsed
 * by each listener's filter, and if accepted, stashed into the
 * corresponding buffer.
 */
void
bpf_tap(struct bpf_if *bp, u_char *pkt, u_int pktlen)
{
	struct bpf_d *d;
	struct timeval tv;
	int gottime = 0;
	u_int slen;

	lwkt_gettoken(&bpf_token);
	/* Re-check */
	if (bp == NULL) {
		lwkt_reltoken(&bpf_token);
		return;
	}

	/*
	 * Note that the ipl does not have to be raised at this point.
	 * The only problem that could arise here is that if two different
	 * interfaces shared any data.  This is not the case.
	 */
	SLIST_FOREACH(d, &bp->bif_dlist, bd_next) {
		++d->bd_rcount;
		slen = bpf_filter(d->bd_rfilter, pkt, pktlen, pktlen);
		if (slen != 0) {
			if (!gottime) {
				microtime(&tv);
				gottime = 1;
			}
			catchpacket(d, pkt, pktlen, slen, ovbcopy, &tv);
		}
	}
	lwkt_reltoken(&bpf_token);
}

/*
 * Copy data from an mbuf chain into a buffer.  This code is derived
 * from m_copydata in sys/uipc_mbuf.c.
 */
static void
bpf_mcopy(const void *src_arg, void *dst_arg, size_t len)
{
	const struct mbuf *m;
	u_int count;
	u_char *dst;

	m = src_arg;
	dst = dst_arg;
	while (len > 0) {
		if (m == NULL)
			panic("bpf_mcopy");
		count = min(m->m_len, len);
		bcopy(mtod(m, void *), dst, count);
		m = m->m_next;
		dst += count;
		len -= count;
	}
}

/*
 * Process the packet in the mbuf chain m.  The packet is parsed by each
 * listener's filter, and if accepted, stashed into the corresponding
 * buffer.
 */
void
bpf_mtap(struct bpf_if *bp, struct mbuf *m)
{
	struct bpf_d *d;
	u_int pktlen, slen;
	struct timeval tv;
	int gottime = 0;

	lwkt_gettoken(&bpf_token);
	/* Re-check */
	if (bp == NULL) {
		lwkt_reltoken(&bpf_token);
		return;
	}

	/* Don't compute pktlen, if no descriptor is attached. */
	if (SLIST_EMPTY(&bp->bif_dlist)) {
		lwkt_reltoken(&bpf_token);
		return;
	}

	pktlen = m_lengthm(m, NULL);

	SLIST_FOREACH(d, &bp->bif_dlist, bd_next) {
		if (!d->bd_seesent && (m->m_pkthdr.rcvif == NULL))
			continue;
		++d->bd_rcount;
		slen = bpf_filter(d->bd_rfilter, (u_char *)m, pktlen, 0);
		if (slen != 0) {
			if (!gottime) {
				microtime(&tv);
				gottime = 1;
			}
			catchpacket(d, (u_char *)m, pktlen, slen, bpf_mcopy,
				    &tv);
		}
	}
	lwkt_reltoken(&bpf_token);
}

/*
 * Incoming linkage from device drivers, where we have a mbuf chain
 * but need to prepend some arbitrary header from a linear buffer.
 *
 * Con up a minimal dummy header to pacify bpf.  Allocate (only) a
 * struct m_hdr on the stack.  This is safe as bpf only reads from the
 * fields in this header that we initialize, and will not try to free
 * it or keep a pointer to it.
 */
void
bpf_mtap_hdr(struct bpf_if *arg, caddr_t data, u_int dlen, struct mbuf *m,
    u_int direction)
{
	struct m_hdr mh;

	mh.mh_flags = 0;
	mh.mh_next = m;
	mh.mh_len = dlen;
	mh.mh_data = data;

	bpf_mtap(arg, (struct mbuf *) &mh);
}

void
bpf_mtap_family(struct bpf_if *bp, struct mbuf *m, sa_family_t family)
{
	u_int family4;

	KKASSERT(family != AF_UNSPEC);

	family4 = (u_int)family;
	bpf_ptap(bp, m, &family4, sizeof(family4));
}

/*
 * Process the packet in the mbuf chain m with the header in m prepended.
 * The packet is parsed by each listener's filter, and if accepted,
 * stashed into the corresponding buffer.
 */
void
bpf_ptap(struct bpf_if *bp, struct mbuf *m, const void *data, u_int dlen)
{
	struct mbuf mb;

	/*
	 * Craft on-stack mbuf suitable for passing to bpf_mtap.
	 * Note that we cut corners here; we only setup what's
	 * absolutely needed--this mbuf should never go anywhere else.
	 */
	mb.m_next = m;
	mb.m_data = __DECONST(void *, data); /* LINTED */
	mb.m_len = dlen;
	mb.m_pkthdr.rcvif = m->m_pkthdr.rcvif;

	bpf_mtap(bp, &mb);
}

/*
 * Move the packet data from interface memory (pkt) into the
 * store buffer.  Return 1 if it's time to wakeup a listener (buffer full),
 * otherwise 0.  "copy" is the routine called to do the actual data
 * transfer.  bcopy is passed in to copy contiguous chunks, while
 * bpf_mcopy is passed in to copy mbuf chains.  In the latter case,
 * pkt is really an mbuf.
 */
static void
catchpacket(struct bpf_d *d, u_char *pkt, u_int pktlen, u_int snaplen,
	    void (*cpfn)(const void *, void *, size_t),
	    const struct timeval *tv)
{
	struct bpf_hdr *hp;
	int totlen, curlen;
	int hdrlen = d->bd_bif->bif_hdrlen;
	int wakeup = 0;
	/*
	 * Figure out how many bytes to move.  If the packet is
	 * greater or equal to the snapshot length, transfer that
	 * much.  Otherwise, transfer the whole packet (unless
	 * we hit the buffer size limit).
	 */
	totlen = hdrlen + min(snaplen, pktlen);
	if (totlen > d->bd_bufsize)
		totlen = d->bd_bufsize;

	/*
	 * Round up the end of the previous packet to the next longword.
	 */
	curlen = BPF_WORDALIGN(d->bd_slen);
	if (curlen + totlen > d->bd_bufsize) {
		/*
		 * This packet will overflow the storage buffer.
		 * Rotate the buffers if we can, then wakeup any
		 * pending reads.
		 */
		if (d->bd_fbuf == NULL) {
			/*
			 * We haven't completed the previous read yet,
			 * so drop the packet.
			 */
			++d->bd_dcount;
			return;
		}
		ROTATE_BUFFERS(d);
		wakeup = 1;
		curlen = 0;
	} else if (d->bd_immediate || d->bd_state == BPF_TIMED_OUT) {
		/*
		 * Immediate mode is set, or the read timeout has
		 * already expired during a select call.  A packet
		 * arrived, so the reader should be woken up.
		 */
		wakeup = 1;
	}

	/*
	 * Append the bpf header.
	 */
	hp = (struct bpf_hdr *)(d->bd_sbuf + curlen);
	hp->bh_tstamp = *tv;
	hp->bh_datalen = pktlen;
	hp->bh_hdrlen = hdrlen;
	/*
	 * Copy the packet data into the store buffer and update its length.
	 */
	(*cpfn)(pkt, (u_char *)hp + hdrlen, (hp->bh_caplen = totlen - hdrlen));
	d->bd_slen = curlen + totlen;

	if (wakeup)
		bpf_wakeup(d);
}

/*
 * Initialize all nonzero fields of a descriptor.
 */
static int
bpf_allocbufs(struct bpf_d *d)
{
	d->bd_fbuf = kmalloc(d->bd_bufsize, M_BPF, M_WAITOK);
	d->bd_sbuf = kmalloc(d->bd_bufsize, M_BPF, M_WAITOK);
	d->bd_slen = 0;
	d->bd_hlen = 0;
	return(0);
}

/*
 * Free buffers and packet filter program currently in use by a descriptor.
 * Called on close.
 */
static void
bpf_freed(struct bpf_d *d)
{
	/*
	 * We don't need to lock out interrupts since this descriptor has
	 * been detached from its interface and it yet hasn't been marked
	 * free.
	 */
	if (d->bd_sbuf != NULL) {
		kfree(d->bd_sbuf, M_BPF);
		if (d->bd_hbuf != NULL)
			kfree(d->bd_hbuf, M_BPF);
		if (d->bd_fbuf != NULL)
			kfree(d->bd_fbuf, M_BPF);
	}
	if (d->bd_rfilter)
		kfree(d->bd_rfilter, M_BPF);
	if (d->bd_wfilter)
		kfree(d->bd_wfilter, M_BPF);
}

/*
 * Attach an interface to bpf.  ifp is a pointer to the structure
 * defining the interface to be attached, dlt is the link layer type,
 * and hdrlen is the fixed size of the link header (variable length
 * headers are not yet supported).
 */
void
bpfattach(struct ifnet *ifp, u_int dlt, u_int hdrlen)
{
	bpfattach_dlt(ifp, dlt, hdrlen, &ifp->if_bpf);
}

void
bpfattach_dlt(struct ifnet *ifp, u_int dlt, u_int hdrlen, struct bpf_if **driverp)
{
	struct bpf_if *bp;

	bp = kmalloc(sizeof *bp, M_BPF, M_WAITOK | M_ZERO);

	lwkt_gettoken(&bpf_token);

	SLIST_INIT(&bp->bif_dlist);
	bp->bif_ifp = ifp;
	bp->bif_dlt = dlt;
	bp->bif_driverp = driverp;
	*bp->bif_driverp = NULL;

	bp->bif_next = bpf_iflist;
	bpf_iflist = bp;

	/*
	 * Compute the length of the bpf header.  This is not necessarily
	 * equal to SIZEOF_BPF_HDR because we want to insert spacing such
	 * that the network layer header begins on a longword boundary (for
	 * performance reasons and to alleviate alignment restrictions).
	 */
	bp->bif_hdrlen = BPF_WORDALIGN(hdrlen + SIZEOF_BPF_HDR) - hdrlen;

	lwkt_reltoken(&bpf_token);

	if (bootverbose)
		if_printf(ifp, "bpf attached\n");
}

/*
 * Detach bpf from an interface.  This involves detaching each descriptor
 * associated with the interface, and leaving bd_bif NULL.  Notify each
 * descriptor as it's detached so that any sleepers wake up and get
 * ENXIO.
 */
void
bpfdetach(struct ifnet *ifp)
{
	struct bpf_if *bp, *bp_prev;
	struct bpf_d *d;

	lwkt_gettoken(&bpf_token);

	/* Locate BPF interface information */
	bp_prev = NULL;
	for (bp = bpf_iflist; bp != NULL; bp = bp->bif_next) {
		if (ifp == bp->bif_ifp)
			break;
		bp_prev = bp;
	}

	/* Interface wasn't attached */
	if (bp->bif_ifp == NULL) {
		lwkt_reltoken(&bpf_token);
		kprintf("bpfdetach: %s was not attached\n", ifp->if_xname);
		return;
	}

	while ((d = SLIST_FIRST(&bp->bif_dlist)) != NULL) {
		bpf_detachd(d);
		bpf_wakeup(d);
	}

	if (bp_prev != NULL)
		bp_prev->bif_next = bp->bif_next;
	else
		bpf_iflist = bp->bif_next;

	kfree(bp, M_BPF);

	lwkt_reltoken(&bpf_token);
}

/*
 * Get a list of available data link type of the interface.
 */
static int
bpf_getdltlist(struct bpf_d *d, struct bpf_dltlist *bfl)
{
	int n, error;
	struct ifnet *ifp;
	struct bpf_if *bp;

	ifp = d->bd_bif->bif_ifp;
	n = 0;
	error = 0;
	for (bp = bpf_iflist; bp != NULL; bp = bp->bif_next) {
		if (bp->bif_ifp != ifp)
			continue;
		if (bfl->bfl_list != NULL) {
			if (n >= bfl->bfl_len) {
				return (ENOMEM);
			}
			error = copyout(&bp->bif_dlt,
			    bfl->bfl_list + n, sizeof(u_int));
		}
		n++;
	}
	bfl->bfl_len = n;
	return(error);
}

/*
 * Set the data link type of a BPF instance.
 */
static int
bpf_setdlt(struct bpf_d *d, u_int dlt)
{
	int error, opromisc;
	struct ifnet *ifp;
	struct bpf_if *bp;

	if (d->bd_bif->bif_dlt == dlt)
		return (0);
	ifp = d->bd_bif->bif_ifp;
	for (bp = bpf_iflist; bp != NULL; bp = bp->bif_next) {
		if (bp->bif_ifp == ifp && bp->bif_dlt == dlt)
			break;
	}
	if (bp != NULL) {
		opromisc = d->bd_promisc;
		bpf_detachd(d);
		bpf_attachd(d, bp);
		bpf_resetd(d);
		if (opromisc) {
			error = ifpromisc(bp->bif_ifp, 1);
			if (error) {
				if_printf(bp->bif_ifp,
					"bpf_setdlt: ifpromisc failed (%d)\n",
					error);
			} else {
				d->bd_promisc = 1;
			}
		}
	}
	return(bp == NULL ? EINVAL : 0);
}

void
bpf_gettoken(void)
{
	lwkt_gettoken(&bpf_token);
}

void
bpf_reltoken(void)
{
	lwkt_reltoken(&bpf_token);
}

static void
bpf_drvinit(void *unused)
{
	int i;

	make_autoclone_dev(&bpf_ops, &DEVFS_CLONE_BITMAP(bpf),
		bpfclone, 0, 0, 0600, "bpf");
	for (i = 0; i < BPF_PREALLOCATED_UNITS; i++) {
		make_dev(&bpf_ops, i, 0, 0, 0600, "bpf%d", i);
		devfs_clone_bitmap_set(&DEVFS_CLONE_BITMAP(bpf), i);
	}
}

static void
bpf_drvuninit(void *unused)
{
	devfs_clone_handler_del("bpf");
	dev_ops_remove_all(&bpf_ops);
	devfs_clone_bitmap_uninit(&DEVFS_CLONE_BITMAP(bpf));
}

SYSINIT(bpfdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,bpf_drvinit,NULL)
SYSUNINIT(bpfdev, SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,bpf_drvuninit, NULL);

#else /* !BPF */
/*
 * NOP stubs to allow bpf-using drivers to load and function.
 *
 * A 'better' implementation would allow the core bpf functionality
 * to be loaded at runtime.
 */

void
bpf_tap(struct bpf_if *bp, u_char *pkt, u_int pktlen)
{
}

void
bpf_mtap(struct bpf_if *bp, struct mbuf *m)
{
}

void
bpf_ptap(struct bpf_if *bp, struct mbuf *m, const void *data, u_int dlen)
{
}

void
bpfattach(struct ifnet *ifp, u_int dlt, u_int hdrlen)
{
}

void
bpfattach_dlt(struct ifnet *ifp, u_int dlt, u_int hdrlen, struct bpf_if **driverp)
{
}

void
bpfdetach(struct ifnet *ifp)
{
}

u_int
bpf_filter(const struct bpf_insn *pc, u_char *p, u_int wirelen, u_int buflen)
{
	return -1;	/* "no filter" behaviour */
}

void
bpf_gettoken(void)
{
}

void
bpf_reltoken(void)
{
}

#endif /* !BPF */
