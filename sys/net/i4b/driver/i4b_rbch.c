/*
 * Copyright (c) 1997, 2001 Hellmuth Michaelis. All rights reserved.
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
 *---------------------------------------------------------------------------
 *
 *	i4b_rbch.c - device driver for raw B channel data
 *	---------------------------------------------------
 *
 * $FreeBSD: src/sys/i4b/driver/i4b_rbch.c,v 1.10.2.3 2001/08/12 16:22:48 hm Exp $
 * $DragonFly: src/sys/net/i4b/driver/i4b_rbch.c,v 1.22 2006/12/22 23:44:55 swildner Exp $
 *
 *	last edit-date: [Sat Aug 11 18:06:57 2001]
 *
 *---------------------------------------------------------------------------*/

#include "use_i4brbch.h"

#if NI4BRBCH > 0

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/tty.h>
#include <sys/thread2.h>
#include <sys/vnode.h>

#include <net/i4b/include/machine/i4b_ioctl.h>
#include <net/i4b/include/machine/i4b_rbch_ioctl.h>
#include <net/i4b/include/machine/i4b_debug.h>

#include "../include/i4b_global.h"
#include "../include/i4b_mbuf.h"
#include "../include/i4b_l3l4.h"
#include "../layer4/i4b_l4.h"

#include <sys/event.h>
#include <sys/filio.h>

static drvr_link_t rbch_drvr_linktab[NI4BRBCH];
static isdn_link_t *isdn_linktab[NI4BRBCH];

#define I4BRBCHACCT		1 	/* enable accounting messages */
#define	I4BRBCHACCTINTVL	2	/* accounting msg interval in secs */

static struct rbch_softc {

	int sc_unit;			/* unit number 		*/

	int sc_devstate;		/* state of driver	*/
#define ST_IDLE		0x00
#define ST_CONNECTED	0x01
#define ST_ISOPEN	0x02
#define ST_RDWAITDATA	0x04
#define ST_WRWAITEMPTY	0x08
#define ST_NOBLOCK	0x10

	int sc_bprot;			/* B-ch protocol used	*/

	call_desc_t *sc_cd;		/* Call Descriptor */

	struct termios it_in;

	struct ifqueue sc_hdlcq;	/* hdlc read queue	*/
#define I4BRBCHMAXQLEN	10

	struct kqinfo kqp;		/* select / poll / kevent */

#if I4BRBCHACCT
	struct callout	sc_timeout;

	int		sc_iinb;	/* isdn driver # of inbytes	*/
	int		sc_ioutb;	/* isdn driver # of outbytes	*/
	int		sc_linb;	/* last # of bytes rx'd		*/
	int		sc_loutb;	/* last # of bytes tx'd 	*/
	int		sc_fn;		/* flag, first null acct	*/
#endif	
} rbch_softc[NI4BRBCH];

static void rbch_rx_data_rdy(int unit);
static void rbch_tx_queue_empty(int unit);
static void rbch_connect(int unit, void *cdp);
static void rbch_disconnect(int unit, void *cdp);
static void rbch_init_linktab(int unit);
static void rbch_clrq(int unit);

#define PDEVSTATIC	static
#define IOCTL_CMD_T	u_long

PDEVSTATIC d_open_t i4brbchopen;
PDEVSTATIC d_close_t i4brbchclose;
PDEVSTATIC d_read_t i4brbchread;
PDEVSTATIC d_write_t i4brbchwrite;
PDEVSTATIC d_ioctl_t i4brbchioctl;
PDEVSTATIC d_kqfilter_t i4brbchkqfilter;

PDEVSTATIC void i4brbchkfilt_detach(struct knote *);
PDEVSTATIC int i4brbchkfilt_read(struct knote *, long);
PDEVSTATIC int i4brbchkfilt_write(struct knote *, long);

#define CDEV_MAJOR 57

static struct dev_ops i4brbch_ops = {
	{ "i4brbch", CDEV_MAJOR, D_KQFILTER },
	.d_open =	i4brbchopen,
	.d_close =	i4brbchclose,
	.d_read =	i4brbchread,
	.d_write =	i4brbchwrite,
	.d_ioctl =	i4brbchioctl,
	.d_kqfilter =	i4brbchkqfilter
};

static void i4brbchattach(void *);
PSEUDO_SET(i4brbchattach, i4b_rbch);

/*===========================================================================*
 *			DEVICE DRIVER ROUTINES
 *===========================================================================*/

/*---------------------------------------------------------------------------*
 *	interface attach routine
 *---------------------------------------------------------------------------*/
PDEVSTATIC void
i4brbchattach(void *dummy)
{
	int i;

#ifndef HACK_NO_PSEUDO_ATTACH_MSG
	kprintf("i4brbch: %d raw B channel access device(s) attached\n", NI4BRBCH);
#endif
	
	for(i=0; i < NI4BRBCH; i++)
	{
		make_dev(&i4brbch_ops, i,
			UID_ROOT, GID_WHEEL, 0600, "i4brbch%d", i);

#if I4BRBCHACCT
		callout_init(&rbch_softc[i].sc_timeout);
		rbch_softc[i].sc_fn = 1;
#endif
		rbch_softc[i].sc_unit = i;
		rbch_softc[i].sc_devstate = ST_IDLE;
		rbch_softc[i].sc_hdlcq.ifq_maxlen = I4BRBCHMAXQLEN;
		rbch_softc[i].it_in.c_ispeed = rbch_softc[i].it_in.c_ospeed = 64000;
		termioschars(&rbch_softc[i].it_in);
		rbch_init_linktab(i);
	}
}

/*---------------------------------------------------------------------------*
 *	open rbch device
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4brbchopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int unit = minor(dev);
	
	if(unit >= NI4BRBCH)
		return(ENXIO);

	if(rbch_softc[unit].sc_devstate & ST_ISOPEN)
		return(EBUSY);

#if 0
	rbch_clrq(unit);
#endif
	
	rbch_softc[unit].sc_devstate |= ST_ISOPEN;		

	NDBGL4(L4_RBCHDBG, "unit %d, open", unit);	

	return(0);
}

/*---------------------------------------------------------------------------*
 *	close rbch device
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4brbchclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];
	
	if(sc->sc_devstate & ST_CONNECTED)
		i4b_l4_drvrdisc(BDRV_RBCH, unit);

	sc->sc_devstate &= ~ST_ISOPEN;		

	rbch_clrq(unit);
	
	NDBGL4(L4_RBCHDBG, "unit %d, closed", unit);
	
	return(0);
}

/*---------------------------------------------------------------------------*
 *	read from rbch device
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4brbchread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct mbuf *m;
	int error = 0;
	int unit = minor(dev);
	struct ifqueue *iqp;
	struct rbch_softc *sc = &rbch_softc[unit];

	CRIT_VAR;
	
	NDBGL4(L4_RBCHDBG, "unit %d, enter read", unit);
	
	CRIT_BEG;
	if(!(sc->sc_devstate & ST_ISOPEN))
	{
		CRIT_END;
		NDBGL4(L4_RBCHDBG, "unit %d, read while not open", unit);
		return(EIO);
	}

	if((sc->sc_devstate & ST_NOBLOCK) || (ap->a_ioflag & IO_NDELAY))
	{
		if(!(sc->sc_devstate & ST_CONNECTED)) {
			CRIT_END;
			return(EWOULDBLOCK);
		}

		if(sc->sc_bprot == BPROT_RHDLC)
			iqp = &sc->sc_hdlcq;
		else
			iqp = isdn_linktab[unit]->rx_queue;	

		if(IF_QEMPTY(iqp) && (sc->sc_devstate & ST_ISOPEN)) {
			CRIT_END;
			return(EWOULDBLOCK);
	}
	}
	else
	{
		while(!(sc->sc_devstate & ST_CONNECTED))
		{
			NDBGL4(L4_RBCHDBG, "unit %d, wait read init", unit);
		
			if((error = tsleep((caddr_t) &rbch_softc[unit],
					       PCATCH, "rrrbch", 0 )) != 0)
			{
				CRIT_END;
				NDBGL4(L4_RBCHDBG, "unit %d, error %d tsleep", unit, error);
				return(error);
			}
		}

		if(sc->sc_bprot == BPROT_RHDLC)
			iqp = &sc->sc_hdlcq;
		else
			iqp = isdn_linktab[unit]->rx_queue;	

		while(IF_QEMPTY(iqp) && (sc->sc_devstate & ST_ISOPEN))
		{
			sc->sc_devstate |= ST_RDWAITDATA;
		
			NDBGL4(L4_RBCHDBG, "unit %d, wait read data", unit);
		
			if((error = tsleep((caddr_t) &isdn_linktab[unit]->rx_queue,
					   PCATCH, "rrbch", 0 )) != 0)
			{
				CRIT_END;
				NDBGL4(L4_RBCHDBG, "unit %d, error %d tsleep read", unit, error);
				sc->sc_devstate &= ~ST_RDWAITDATA;
				return(error);
			}
		}
	}

	IF_DEQUEUE(iqp, m);

	NDBGL4(L4_RBCHDBG, "unit %d, read %d bytes", unit, m->m_len);
	
	if(m && m->m_len)
	{
		error = uiomove(m->m_data, m->m_len, uio);
	}
	else
	{
		NDBGL4(L4_RBCHDBG, "unit %d, error %d uiomove", unit, error);
		error = EIO;
	}
		
	if(m)
		i4b_Bfreembuf(m);

	CRIT_END;

	return(error);
}

/*---------------------------------------------------------------------------*
 *	write to rbch device
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4brbchwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	struct mbuf *m;
	int error = 0;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];

	CRIT_VAR;
	
	NDBGL4(L4_RBCHDBG, "unit %d, write", unit);	

	CRIT_BEG;
	if(!(sc->sc_devstate & ST_ISOPEN))
	{
		NDBGL4(L4_RBCHDBG, "unit %d, write while not open", unit);
		CRIT_END;
		return(EIO);
	}

	if((sc->sc_devstate & ST_NOBLOCK) || (ap->a_ioflag & IO_NDELAY))
	{
		if(!(sc->sc_devstate & ST_CONNECTED)) {
			CRIT_END;
			return(EWOULDBLOCK);
		}
		if(IF_QFULL(isdn_linktab[unit]->tx_queue) && (sc->sc_devstate & ST_ISOPEN)) {
			CRIT_END;
			return(EWOULDBLOCK);
	}
	}
	else
	{
		while(!(sc->sc_devstate & ST_CONNECTED))
		{
			NDBGL4(L4_RBCHDBG, "unit %d, write wait init", unit);
		
			error = tsleep((caddr_t) &rbch_softc[unit],
						   PCATCH, "wrrbch", 0 );
			if(error == ERESTART) {
				CRIT_END;
				return (ERESTART);
			}
			else if(error == EINTR)
			{
				CRIT_END;
				NDBGL4(L4_RBCHDBG, "unit %d, EINTR during wait init", unit);
				return(EINTR);
			}
			else if(error)
			{
				CRIT_END;
				NDBGL4(L4_RBCHDBG, "unit %d, error %d tsleep init", unit, error);
				return(error);
			}
			tsleep((caddr_t) &rbch_softc[unit], PCATCH, "xrbch", (hz*1));
		}

		while(IF_QFULL(isdn_linktab[unit]->tx_queue) && (sc->sc_devstate & ST_ISOPEN))
		{
			sc->sc_devstate |= ST_WRWAITEMPTY;

			NDBGL4(L4_RBCHDBG, "unit %d, write queue full", unit);
		
			if ((error = tsleep((caddr_t) &isdn_linktab[unit]->tx_queue,
					    PCATCH, "wrbch", 0)) != 0) {
				sc->sc_devstate &= ~ST_WRWAITEMPTY;
				if(error == ERESTART)
				{
					CRIT_END;
					return(ERESTART);
				}
				else if(error == EINTR)
				{
					CRIT_END;
					NDBGL4(L4_RBCHDBG, "unit %d, EINTR during wait write", unit);
					return(error);
				}
				else if(error)
				{
					CRIT_END;
					NDBGL4(L4_RBCHDBG, "unit %d, error %d tsleep write", unit, error);
					return(error);
				}
			}
		}
	}

	if(!(sc->sc_devstate & ST_ISOPEN))
	{
		NDBGL4(L4_RBCHDBG, "unit %d, not open anymore", unit);
		CRIT_END;
		return(EIO);
	}

	if((m = i4b_Bgetmbuf(BCH_MAX_DATALEN)) != NULL)
	{
		m->m_len = (int)szmin(BCH_MAX_DATALEN, uio->uio_resid);

		NDBGL4(L4_RBCHDBG, "unit %d, write %d bytes", unit, m->m_len);
		
		error = uiomove(m->m_data, (size_t)m->m_len, uio);

		if(IF_QFULL(isdn_linktab[unit]->tx_queue))
			m_freem(m);
		else
			IF_ENQUEUE(isdn_linktab[unit]->tx_queue, m);
		(*isdn_linktab[unit]->bch_tx_start)(isdn_linktab[unit]->unit, isdn_linktab[unit]->channel);
	}

	CRIT_END;
	
	return(error);
}

/*---------------------------------------------------------------------------*
 *	rbch device ioctl handlibg
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4brbchioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error = 0;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];
	
	switch(ap->a_cmd)
	{
		case FIOASYNC:	/* Set async mode */
			if (*(int *)ap->a_data)
			{
				NDBGL4(L4_RBCHDBG, "unit %d, setting async mode", unit);
			}
			else
			{
				NDBGL4(L4_RBCHDBG, "unit %d, clearing async mode", unit);
			}
			break;

		case TIOCCDTR:	/* Clear DTR */
			if(sc->sc_devstate & ST_CONNECTED)
			{
				NDBGL4(L4_RBCHDBG, "unit %d, disconnecting for DTR down", unit);
				i4b_l4_drvrdisc(BDRV_RBCH, unit);
			}
			break;

		case I4B_RBCH_DIALOUT:
                {
			size_t l;

			for (l = 0; l < TELNO_MAX && ((char *)ap->a_data)[l]; l++)
				;
			if (l)
			{
				NDBGL4(L4_RBCHDBG, "unit %d, attempting dialout to %s", unit, (char *)ap->a_data);
				i4b_l4_dialoutnumber(BDRV_RBCH, unit, l, (char *)ap->a_data);
				break;
			}
			/* fall through to SDTR */
		}

		case TIOCSDTR:	/* Set DTR */
			NDBGL4(L4_RBCHDBG, "unit %d, attempting dialout (DTR)", unit);
			i4b_l4_dialout(BDRV_RBCH, unit);
			break;

		case TIOCSETA:	/* Set termios struct */
			break;

		case TIOCGETA:	/* Get termios struct */
			*(struct termios *)ap->a_data = sc->it_in;
			break;

		case TIOCMGET:
			*(int *)ap->a_data = TIOCM_LE|TIOCM_DTR|TIOCM_RTS|TIOCM_CTS|TIOCM_DSR;
			if (sc->sc_devstate & ST_CONNECTED)
				*(int *)ap->a_data |= TIOCM_CD;
			break;

		case I4B_RBCH_VR_REQ:
                {
			msg_vr_req_t *mvr;

			mvr = (msg_vr_req_t *)ap->a_data;

			mvr->version = VERSION;
			mvr->release = REL;
			mvr->step = STEP;			
			break;
		}

		default:	/* Unknown stuff */
			NDBGL4(L4_RBCHDBG, "unit %d, ioctl, unknown cmd %lx", unit, ap->a_cmd);
			error = EINVAL;
			break;
	}
	return(error);
}

/*---------------------------------------------------------------------------*
 *	device driver poll
 *---------------------------------------------------------------------------*/
static struct filterops i4brbchkfiltops_read =
	{ FILTEROP_ISFD, NULL, i4brbchkfilt_detach, i4brbchkfilt_read };
static struct filterops i4brbchkfiltops_write =
	{ FILTEROP_ISFD, NULL, i4brbchkfilt_detach, i4brbchkfilt_write };

PDEVSTATIC int
i4brbchkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &i4brbchkfiltops_read;
		kn->kn_hook = (caddr_t)dev;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &i4brbchkfiltops_write;
		kn->kn_hook = (caddr_t)dev;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &sc->kqp.ki_note;
	knote_insert(klist, kn);

	return (0);
}

PDEVSTATIC void
i4brbchkfilt_detach(struct knote *kn)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];
	struct klist *klist;

	klist = &sc->kqp.ki_note;
	knote_remove(klist, kn);
}

PDEVSTATIC int
i4brbchkfilt_read(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];
	int ready = 0;

	crit_enter();

	if (!(sc->sc_devstate & ST_ISOPEN)) {
		crit_exit();
		kn->kn_flags |= EV_ERROR;
		kn->kn_data = EBADF;
		return (0);
	}

	if (sc->sc_devstate & ST_CONNECTED) {
		struct ifqueue *iqp;

		if(sc->sc_bprot == BPROT_RHDLC)
			iqp = &sc->sc_hdlcq;
		else
			iqp = isdn_linktab[unit]->rx_queue;

		if(!IF_QEMPTY(iqp))
			ready = 1;
	}

	crit_exit();

	return (ready);
}

PDEVSTATIC int
i4brbchkfilt_write(struct knote *kn, long hint)
{
	cdev_t dev = (cdev_t)kn->kn_hook;
	int unit = minor(dev);
	struct rbch_softc *sc = &rbch_softc[unit];
	int ready = 0;

	crit_enter();

	if (!(sc->sc_devstate & ST_ISOPEN)) {
		crit_exit();
		kn->kn_flags |= EV_ERROR;
		kn->kn_data = EBADF;
		return (0);
	}

	/*
	 * Writes are OK if we are connected and the
	 * transmit queue can take them
	 */
	if ((sc->sc_devstate & ST_CONNECTED) &&
	   !IF_QFULL(isdn_linktab[unit]->tx_queue))
	{
		ready = 1;
	}

	crit_exit();

	return (ready);
}

#if I4BRBCHACCT
/*---------------------------------------------------------------------------*
 *	watchdog routine
 *---------------------------------------------------------------------------*/
static void
rbch_timeout(struct rbch_softc *sc)
{
	bchan_statistics_t bs;
	int unit = sc->sc_unit;

	/* get # of bytes in and out from the HSCX driver */ 
	
	(*isdn_linktab[unit]->bch_stat)
		(isdn_linktab[unit]->unit, isdn_linktab[unit]->channel, &bs);

	sc->sc_ioutb += bs.outbytes;
	sc->sc_iinb += bs.inbytes;
	
	if((sc->sc_iinb != sc->sc_linb) || (sc->sc_ioutb != sc->sc_loutb) || sc->sc_fn) 
	{
		int ri = (sc->sc_iinb - sc->sc_linb)/I4BRBCHACCTINTVL;
		int ro = (sc->sc_ioutb - sc->sc_loutb)/I4BRBCHACCTINTVL;

		if((sc->sc_iinb == sc->sc_linb) && (sc->sc_ioutb == sc->sc_loutb))
			sc->sc_fn = 0;
		else
			sc->sc_fn = 1;
			
		sc->sc_linb = sc->sc_iinb;
		sc->sc_loutb = sc->sc_ioutb;

		i4b_l4_accounting(BDRV_RBCH, unit, ACCT_DURING,
			 sc->sc_ioutb, sc->sc_iinb, ro, ri, sc->sc_ioutb, sc->sc_iinb);
 	}
	callout_reset(&sc->sc_timeout, I4BRBCHACCTINTVL * hz,
			(void *)rbch_timeout, sc);
}
#endif /* I4BRBCHACCT */

/*===========================================================================*
 *			ISDN INTERFACE ROUTINES
 *===========================================================================*/

/*---------------------------------------------------------------------------*
 *	this routine is called from L4 handler at connect time
 *---------------------------------------------------------------------------*/
static void
rbch_connect(int unit, void *cdp)
{
	call_desc_t *cd = (call_desc_t *)cdp;
	struct rbch_softc *sc = &rbch_softc[unit];

	sc->sc_bprot = cd->bprot;

#if I4BRBCHACCT
	if(sc->sc_bprot == BPROT_RHDLC)
	{	
		sc->sc_iinb = 0;
		sc->sc_ioutb = 0;
		sc->sc_linb = 0;
		sc->sc_loutb = 0;

		callout_reset(&sc->sc_timeout, I4BRBCHACCTINTVL * hz,
				(void *)rbch_timeout, sc);
	}
#endif		
	if(!(sc->sc_devstate & ST_CONNECTED))
	{
		NDBGL4(L4_RBCHDBG, "unit %d, wakeup", unit);
		sc->sc_devstate |= ST_CONNECTED;
		sc->sc_cd = cdp;
		wakeup((caddr_t)sc);
	}
}

/*---------------------------------------------------------------------------*
 *	this routine is called from L4 handler at disconnect time
 *---------------------------------------------------------------------------*/
static void
rbch_disconnect(int unit, void *cdp)
{
	call_desc_t *cd = (call_desc_t *)cdp;
	struct rbch_softc *sc = &rbch_softc[unit];

	CRIT_VAR;
	
        if(cd != sc->sc_cd)
	{
		NDBGL4(L4_RBCHDBG, "rbch%d: channel %d not active",
			cd->driver_unit, cd->channelid);
		return;
	}

	CRIT_BEG;
	
	NDBGL4(L4_RBCHDBG, "unit %d, disconnect", unit);

	sc->sc_devstate &= ~ST_CONNECTED;

	sc->sc_cd = NULL;
	
#if I4BRBCHACCT
	i4b_l4_accounting(BDRV_RBCH, unit, ACCT_FINAL,
		 sc->sc_ioutb, sc->sc_iinb, 0, 0, sc->sc_ioutb, sc->sc_iinb);
	callout_stop(&sc->sc_timeout);
#endif		
	CRIT_END;
}
	
/*---------------------------------------------------------------------------*
 *	feedback from daemon in case of dial problems
 *---------------------------------------------------------------------------*/
static void
rbch_dialresponse(int unit, int status, cause_t cause)
{
}
	
/*---------------------------------------------------------------------------*
 *	interface up/down
 *---------------------------------------------------------------------------*/
static void
rbch_updown(int unit, int updown)
{
}
	
/*---------------------------------------------------------------------------*
 *	this routine is called from the HSCX interrupt handler
 *	when a new frame (mbuf) has been received and is to be put on
 *	the rx queue.
 *---------------------------------------------------------------------------*/
static void
rbch_rx_data_rdy(int unit)
{
	if(rbch_softc[unit].sc_bprot == BPROT_RHDLC)
	{
		struct mbuf *m;
		
		if((m = *isdn_linktab[unit]->rx_mbuf) == NULL)
			return;

		m->m_pkthdr.len = m->m_len;

                if(IF_QFULL(&(rbch_softc[unit].sc_hdlcq)))
		{
			NDBGL4(L4_RBCHDBG, "unit %d: hdlc rx queue full!", unit);
			m_freem(m);
		}			
		else
		{
			IF_ENQUEUE(&(rbch_softc[unit].sc_hdlcq), m);
		}
	}

	if(rbch_softc[unit].sc_devstate & ST_RDWAITDATA)
	{
		NDBGL4(L4_RBCHDBG, "unit %d, wakeup", unit);
		rbch_softc[unit].sc_devstate &= ~ST_RDWAITDATA;
		wakeup((caddr_t) &isdn_linktab[unit]->rx_queue);
	}
	else
	{
		NDBGL4(L4_RBCHDBG, "unit %d, NO wakeup", unit);
	}
	KNOTE(&rbch_softc[unit].kqp.ki_note, 0);
}

/*---------------------------------------------------------------------------*
 *	this routine is called from the HSCX interrupt handler
 *	when the last frame has been sent out and there is no
 *	further frame (mbuf) in the tx queue.
 *---------------------------------------------------------------------------*/
static void
rbch_tx_queue_empty(int unit)
{
	if(rbch_softc[unit].sc_devstate & ST_WRWAITEMPTY)
	{
		NDBGL4(L4_RBCHDBG, "unit %d, wakeup", unit);
		rbch_softc[unit].sc_devstate &= ~ST_WRWAITEMPTY;
		wakeup((caddr_t) &isdn_linktab[unit]->tx_queue);
	}
	else
	{
		NDBGL4(L4_RBCHDBG, "unit %d, NO wakeup", unit);
	}
	KNOTE(&rbch_softc[unit].kqp.ki_note, 0);
}

/*---------------------------------------------------------------------------*
 *	this routine is called from the HSCX interrupt handler
 *	each time a packet is received or transmitted
 *---------------------------------------------------------------------------*/
static void
rbch_activity(int unit, int rxtx)
{
	if (rbch_softc[unit].sc_cd)
		rbch_softc[unit].sc_cd->last_active_time = SECOND;
	KNOTE(&rbch_softc[unit].kqp.ki_note, 0);
}

/*---------------------------------------------------------------------------*
 *	clear an hdlc rx queue for a rbch unit
 *---------------------------------------------------------------------------*/
static void
rbch_clrq(int unit)
{
	CRIT_VAR;

	CRIT_BEG;
	IF_DRAIN(&rbch_softc[unit].sc_hdlcq);
	CRIT_END;
}
				
/*---------------------------------------------------------------------------*
 *	return this drivers linktab address
 *---------------------------------------------------------------------------*/
drvr_link_t *
rbch_ret_linktab(int unit)
{
	rbch_init_linktab(unit);
	return(&rbch_drvr_linktab[unit]);
}

/*---------------------------------------------------------------------------*
 *	setup the isdn_linktab for this driver
 *---------------------------------------------------------------------------*/
void
rbch_set_linktab(int unit, isdn_link_t *ilt)
{
	isdn_linktab[unit] = ilt;
}

/*---------------------------------------------------------------------------*
 *	initialize this drivers linktab
 *---------------------------------------------------------------------------*/
static void
rbch_init_linktab(int unit)
{
	rbch_drvr_linktab[unit].unit = unit;
	rbch_drvr_linktab[unit].bch_rx_data_ready = rbch_rx_data_rdy;
	rbch_drvr_linktab[unit].bch_tx_queue_empty = rbch_tx_queue_empty;
	rbch_drvr_linktab[unit].bch_activity = rbch_activity;	
	rbch_drvr_linktab[unit].line_connected = rbch_connect;
	rbch_drvr_linktab[unit].line_disconnected = rbch_disconnect;
	rbch_drvr_linktab[unit].dial_response = rbch_dialresponse;
	rbch_drvr_linktab[unit].updown_ind = rbch_updown;	
}

/*===========================================================================*/

#endif /* NI4BRBCH > 0 */
