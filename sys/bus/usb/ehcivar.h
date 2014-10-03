/*	$NetBSD: ehcivar.h,v 1.19 2005/04/29 15:04:29 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/ehcivar.h,v 1.9.2.1 2006/01/26 01:43:13 iedowse Exp $	*/
/*	$DragonFly: src/sys/bus/usb/ehcivar.h,v 1.12 2008/06/10 10:04:06 hasso Exp $	*/

/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

typedef struct ehci_soft_qtd {
	ehci_qtd_t qtd;
	struct ehci_soft_qtd *nextqtd; /* mirrors nextqtd in TD */
	ehci_physaddr_t physaddr;
	usbd_xfer_handle xfer;
	LIST_ENTRY(ehci_soft_qtd) hnext;
	u_int16_t len;
} ehci_soft_qtd_t;
#define EHCI_SQTD_SIZE ((sizeof (struct ehci_soft_qtd) + EHCI_QTD_ALIGN - 1) / EHCI_QTD_ALIGN * EHCI_QTD_ALIGN)
#define EHCI_SQTD_CHUNK (EHCI_PAGE_SIZE / EHCI_SQTD_SIZE)

typedef struct ehci_soft_qh {
	ehci_qh_t qh;
	struct ehci_soft_qh *next;
	struct ehci_soft_qh *prev;
	struct ehci_soft_qtd *sqtd;
	ehci_physaddr_t physaddr;
	int islot;		/* Interrupt list slot. */
} ehci_soft_qh_t;
#define EHCI_SQH_SIZE ((sizeof (struct ehci_soft_qh) + EHCI_QH_ALIGN - 1) / EHCI_QH_ALIGN * EHCI_QH_ALIGN)
#define EHCI_SQH_CHUNK (EHCI_PAGE_SIZE / EHCI_SQH_SIZE)

struct ehci_xfer {
	struct usbd_xfer xfer;
	struct usb_task	abort_task;
	LIST_ENTRY(ehci_xfer) inext; /* list of active xfers */
	ehci_soft_qtd_t *sqtdstart;
	ehci_soft_qtd_t *sqtdend;
	u_int32_t ehci_xfer_flags;
#ifdef DIAGNOSTIC
	int isdone;
#endif
};
#define EHCI_XFER_ABORTING	0x0001	/* xfer is aborting. */
#define EHCI_XFER_ABORTWAIT	0x0002	/* abort completion is being awaited. */

#define EXFER(xfer) ((struct ehci_xfer *)(xfer))

/* Information about an entry in the interrupt list. */
struct ehci_soft_islot {
	ehci_soft_qh_t *sqh;	/* Queue Head. */
};

#define EHCI_FRAMELIST_MAXCOUNT	1024
#define EHCI_IPOLLRATES		8 /* Poll rates (1ms, 2, 4, 8 .. 128) */
#define EHCI_INTRQHS		((1 << EHCI_IPOLLRATES) - 1)
#define EHCI_MAX_POLLRATE	(1 << (EHCI_IPOLLRATES - 1))
#define EHCI_IQHIDX(lev, pos) \
	((((pos) & ((1 << (lev)) - 1)) | (1 << (lev))) - 1)
#define EHCI_ILEV_IVAL(lev)	(1 << (lev))


#define EHCI_HASH_SIZE 128
#define EHCI_COMPANION_MAX 8

#define EHCI_SCFLG_DONEINIT	0x0001	/* ehci_init() has been called. */
#define EHCI_SCFLG_LOSTINTRBUG	0x0002	/* workaround for VIA / ATI chipsets */

typedef struct ehci_softc {
	struct usbd_bus sc_bus;		/* base device */
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_size_t sc_size;
	void *ih;

	struct resource *io_res;
	struct resource *irq_res;
	u_int sc_offs;			/* offset to operational regs */
	int sc_flags;			/* misc flags */

	char sc_vendor[32];		/* vendor string for root hub */
	int sc_id_vendor;		/* vendor ID for root hub */

	u_int32_t sc_cmd;		/* shadow of cmd reg during suspend */

	u_int sc_ncomp;
	u_int sc_npcomp;
	struct usbd_bus *sc_comps[EHCI_COMPANION_MAX];

	usb_dma_t sc_fldma;
	ehci_link_t *sc_flist;
	u_int sc_flsize;

	struct ehci_soft_islot sc_islots[EHCI_INTRQHS];

	LIST_HEAD(, ehci_xfer) sc_intrhead;

	ehci_soft_qh_t *sc_freeqhs;
	ehci_soft_qtd_t *sc_freeqtds;

	int sc_noport;
	u_int8_t sc_addr;		/* device address */
	u_int8_t sc_conf;		/* device configuration */
	usbd_xfer_handle sc_intrxfer;
	char sc_isreset;
#ifdef USB_USE_SOFTINTR
	char sc_softwake;
#endif /* USB_USE_SOFTINTR */

	u_int32_t sc_eintrs;		/* enabled interrupts */
	u_int32_t sc_dintrs;		/* deferred interrupts */
	ehci_soft_qh_t *sc_async_head;

	STAILQ_HEAD(, usbd_xfer) sc_free_xfers; /* free xfers */

	struct lock sc_doorbell_lock;

	struct callout sc_tmo_intrlist;

	char sc_dying;
} ehci_softc_t;

#define EREAD1(sc, a) bus_space_read_1((sc)->iot, (sc)->ioh, (a))
#define EREAD2(sc, a) bus_space_read_2((sc)->iot, (sc)->ioh, (a))
#define EREAD4(sc, a) bus_space_read_4((sc)->iot, (sc)->ioh, (a))
#define EWRITE1(sc, a, x) bus_space_write_1((sc)->iot, (sc)->ioh, (a), (x))
#define EWRITE2(sc, a, x) bus_space_write_2((sc)->iot, (sc)->ioh, (a), (x))
#define EWRITE4(sc, a, x) bus_space_write_4((sc)->iot, (sc)->ioh, (a), (x))
#define EOREAD1(sc, a) bus_space_read_1((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a))
#define EOREAD2(sc, a) bus_space_read_2((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a))
#define EOREAD4(sc, a) bus_space_read_4((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a))
#define EOWRITE1(sc, a, x) bus_space_write_1((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a), (x))
#define EOWRITE2(sc, a, x) bus_space_write_2((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a), (x))
#define EOWRITE4(sc, a, x) bus_space_write_4((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a), (x))

usbd_status	ehci_init(ehci_softc_t *);
int		ehci_intr(void *);
int		ehci_detach(ehci_softc_t *, int);
void		ehci_power(int state, void *priv);
void		ehci_shutdown(void *v);

#define MS_TO_TICKS(ms) ((ms) * hz / 1000)
