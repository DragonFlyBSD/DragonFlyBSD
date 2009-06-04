/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $OpenBSD: ahci.c,v 1.147 2009/02/16 21:19:07 miod Exp $
 */

#include "ahci.h"

int	ahci_port_start(struct ahci_port *, int);
int	ahci_port_stop(struct ahci_port *, int);
int	ahci_port_clo(struct ahci_port *);
int	ahci_port_softreset(struct ahci_port *);
int	ahci_port_portreset(struct ahci_port *);

int	ahci_load_prdt(struct ahci_ccb *);
void	ahci_unload_prdt(struct ahci_ccb *);
static void ahci_load_prdt_callback(void *info, bus_dma_segment_t *segs,
				    int nsegs, int error);
int	ahci_poll(struct ahci_ccb *, int, void (*)(void *));
void	ahci_start(struct ahci_ccb *);

static void ahci_ata_cmd_timeout_unserialized(void *arg);
static void ahci_ata_cmd_timeout(void *arg);

void	ahci_issue_pending_ncq_commands(struct ahci_port *);
void	ahci_issue_pending_commands(struct ahci_port *, int);

u_int32_t	ahci_port_intr(struct ahci_port *, u_int32_t);

struct ahci_ccb	*ahci_get_ccb(struct ahci_port *);
void	ahci_put_ccb(struct ahci_ccb *);

struct ahci_ccb	*ahci_get_err_ccb(struct ahci_port *);
void	ahci_put_err_ccb(struct ahci_ccb *);

int	ahci_port_read_ncq_error(struct ahci_port *, int *);

struct ahci_dmamem *ahci_dmamem_alloc(struct ahci_softc *, bus_dma_tag_t tag);
void	ahci_dmamem_free(struct ahci_softc *, struct ahci_dmamem *);
static void ahci_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error);

void	ahci_empty_done(struct ahci_ccb *ccb);
void	ahci_ata_cmd_done(struct ahci_ccb *ccb);

/* Wait for all bits in _b to be cleared */
#define ahci_pwait_clr(_ap, _r, _b) ahci_pwait_eq((_ap), (_r), (_b), 0)

/* Wait for all bits in _b to be set */
#define ahci_pwait_set(_ap, _r, _b) ahci_pwait_eq((_ap), (_r), (_b), (_b))

int
ahci_init(struct ahci_softc *sc)
{
	u_int32_t	cap, pi;

	DPRINTF(AHCI_D_VERBOSE, " GHC 0x%b",
		ahci_read(sc, AHCI_REG_GHC), AHCI_FMT_GHC);

	/* save BIOS initialised parameters, enable staggered spin up */
	cap = ahci_read(sc, AHCI_REG_CAP);
	cap &= AHCI_REG_CAP_SMPS;
	cap |= AHCI_REG_CAP_SSS;
	pi = ahci_read(sc, AHCI_REG_PI);

	if (AHCI_REG_GHC_AE & ahci_read(sc, AHCI_REG_GHC)) {
		/* reset the controller */
		ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_HR);
		if (ahci_wait_ne(sc, AHCI_REG_GHC, AHCI_REG_GHC_HR,
		    AHCI_REG_GHC_HR) != 0) {
			device_printf(sc->sc_dev,
				      "unable to reset controller\n");
			return (1);
		}
	}

	/* enable ahci (global interrupts disabled) */
	ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_AE);

	/* restore parameters */
	ahci_write(sc, AHCI_REG_CAP, cap);
	ahci_write(sc, AHCI_REG_PI, pi);

	return (0);
}

int
ahci_port_alloc(struct ahci_softc *sc, u_int port)
{
	struct ahci_port		*ap;
	struct ahci_ccb			*ccb;
	u_int64_t			dva;
	u_int32_t			cmd;
	struct ahci_cmd_hdr		*hdr;
	struct ahci_cmd_table		*table;
	int	rc = ENOMEM;
	int	error;
	int	i;

	ap = kmalloc(sizeof(*ap), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ap == NULL) {
		device_printf(sc->sc_dev,
			      "unable to allocate memory for port %d\n",
			      port);
		goto reterr;
	}

	ksnprintf(ap->ap_name, sizeof(ap->ap_name), "%s%d.%d",
		  device_get_name(sc->sc_dev),
		  device_get_unit(sc->sc_dev),
		  port);
	sc->sc_ports[port] = ap;

	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
	    AHCI_PORT_REGION(port), AHCI_PORT_SIZE, &ap->ap_ioh) != 0) {
		device_printf(sc->sc_dev,
			      "unable to create register window for port %d\n",
			      port);
		goto freeport;
	}

	ap->ap_sc = sc;
	ap->ap_num = port;
	TAILQ_INIT(&ap->ap_ccb_free);
	TAILQ_INIT(&ap->ap_ccb_pending);
	lockinit(&ap->ap_ccb_lock, "ahcipo", 0, 0);

	/* Disable port interrupts */
	ahci_pwrite(ap, AHCI_PREG_IE, 0);

	/* Sec 10.1.2 - deinitialise port if it is already running */
	cmd = ahci_pread(ap, AHCI_PREG_CMD);
	if ((cmd & (AHCI_PREG_CMD_ST | AHCI_PREG_CMD_CR |
	    AHCI_PREG_CMD_FRE | AHCI_PREG_CMD_FR)) ||
	    (ahci_pread(ap, AHCI_PREG_SCTL) & AHCI_PREG_SCTL_DET)) {
		int r;

		r = ahci_port_stop(ap, 1);
		if (r) {
			device_printf(sc->sc_dev,
				  "unable to disable %s, ignoring port %d\n",
				  ((r == 2) ? "CR" : "FR"), port);
			rc = ENXIO;
			goto freeport;
		}

		/* Write DET to zero */
		ahci_pwrite(ap, AHCI_PREG_SCTL, 0);
	}

	/* Allocate RFIS */
	ap->ap_dmamem_rfis = ahci_dmamem_alloc(sc, sc->sc_tag_rfis);
	if (ap->ap_dmamem_rfis == NULL) {
		kprintf("NORFIS\n");
		goto nomem;
	}

	/* Setup RFIS base address */
	ap->ap_rfis = (struct ahci_rfis *) AHCI_DMA_KVA(ap->ap_dmamem_rfis);
	dva = AHCI_DMA_DVA(ap->ap_dmamem_rfis);
	ahci_pwrite(ap, AHCI_PREG_FBU, (u_int32_t)(dva >> 32));
	ahci_pwrite(ap, AHCI_PREG_FB, (u_int32_t)dva);

	/* Enable FIS reception and activate port. */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	cmd |= AHCI_PREG_CMD_FRE | AHCI_PREG_CMD_POD | AHCI_PREG_CMD_SUD;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd | AHCI_PREG_CMD_ICC_ACTIVE);

	/* Check whether port activated.  Skip it if not. */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	if ((cmd & AHCI_PREG_CMD_FRE) == 0) {
		kprintf("NOT-ACTIVATED\n");
		rc = ENXIO;
		goto freeport;
	}

	/* Allocate a CCB for each command slot */
	ap->ap_ccbs = kmalloc(sizeof(struct ahci_ccb) * sc->sc_ncmds, M_DEVBUF,
			      M_WAITOK | M_ZERO);
	if (ap->ap_ccbs == NULL) {
		device_printf(sc->sc_dev,
			      "unable to allocate command list for port %d\n",
			      port);
		goto freeport;
	}

	/* Command List Structures and Command Tables */
	ap->ap_dmamem_cmd_list = ahci_dmamem_alloc(sc, sc->sc_tag_cmdh);
	ap->ap_dmamem_cmd_table = ahci_dmamem_alloc(sc, sc->sc_tag_cmdt);
	if (ap->ap_dmamem_cmd_table == NULL ||
	    ap->ap_dmamem_cmd_list == NULL) {
nomem:
		device_printf(sc->sc_dev,
			      "unable to allocate DMA memory for port %d\n",
			      port);
		goto freeport;
	}

	/* Setup command list base address */
	dva = AHCI_DMA_DVA(ap->ap_dmamem_cmd_list);
	ahci_pwrite(ap, AHCI_PREG_CLBU, (u_int32_t)(dva >> 32));
	ahci_pwrite(ap, AHCI_PREG_CLB, (u_int32_t)dva);

	/* Split CCB allocation into CCBs and assign to command header/table */
	hdr = AHCI_DMA_KVA(ap->ap_dmamem_cmd_list);
	table = AHCI_DMA_KVA(ap->ap_dmamem_cmd_table);
	for (i = 0; i < sc->sc_ncmds; i++) {
		ccb = &ap->ap_ccbs[i];

		error = bus_dmamap_create(sc->sc_tag_data, BUS_DMA_ALLOCNOW,
					  &ccb->ccb_dmamap);
		if (error) {
			device_printf(sc->sc_dev,
				      "unable to create dmamap for port %d "
				      "ccb %d\n", port, i);
			goto freeport;
		}

		callout_init(&ccb->ccb_timeout);
		ccb->ccb_slot = i;
		ccb->ccb_port = ap;
		ccb->ccb_cmd_hdr = &hdr[i];
		ccb->ccb_cmd_table = &table[i];
		dva = AHCI_DMA_DVA(ap->ap_dmamem_cmd_table) +
		    ccb->ccb_slot * sizeof(struct ahci_cmd_table);
		ccb->ccb_cmd_hdr->ctba_hi = htole32((u_int32_t)(dva >> 32));
		ccb->ccb_cmd_hdr->ctba_lo = htole32((u_int32_t)dva);

		ccb->ccb_xa.fis =
		    (struct ata_fis_h2d *)ccb->ccb_cmd_table->cfis;
		ccb->ccb_xa.packetcmd = ccb->ccb_cmd_table->acmd;
		ccb->ccb_xa.tag = i;

		ccb->ccb_xa.ata_put_xfer = ahci_ata_put_xfer;

		ccb->ccb_xa.state = ATA_S_COMPLETE;
		ahci_put_ccb(ccb);
	}

	/* Wait for ICC change to complete */
	ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_ICC);

	/* Reset port */
	rc = ahci_port_portreset(ap);
	switch (rc) {
	case ENODEV:
		switch (ahci_pread(ap, AHCI_PREG_SSTS) & AHCI_PREG_SSTS_DET) {
		case AHCI_PREG_SSTS_DET_DEV_NE:
			device_printf(sc->sc_dev,
				      "device not communicating on port %d\n",
				      port);
			break;
		case AHCI_PREG_SSTS_DET_PHYOFFLINE:
			device_printf(sc->sc_dev,
				      "PHY offline on port %d\n",
				      port);
			break;
		default:
			device_printf(sc->sc_dev,
				"no device detected on port %d\n",
				port);
			break;
		}
		break;

	case EBUSY:
		device_printf(sc->sc_dev,
			      "device on port %d didn't come ready, "
			      "TFD: 0x%b\n",
			      port,
			      ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS);

		/* Try a soft reset to clear busy */
		rc = ahci_port_softreset(ap);
		if (rc) {
			device_printf(sc->sc_dev,
				      "unable to communicate "
				      "with device on port %d\n",
				      port);
			goto freeport;
		}
		break;

	default:
		break;
	}

	/*
	 * Enable command transfers on the port if a device was detected.
	 * Otherwise leave them disabled but leave the port structure
	 * intact so we get hot-plug interrupts.
	 */
	if (rc == 0) {
		DPRINTF(AHCI_D_VERBOSE, "%s: detected device on port %d\n",
			device_get_name(sc->sc_dev), port);
		if (ahci_port_start(ap, 0)) {
			device_printf(sc->sc_dev,
				      "failed to start command DMA on port %d, "
				      "disabling\n", port);
			rc = ENXIO;	/* couldn't start port */
		}
	}

	/*
	 * A missing or busy device is not fatal for the purposes of
	 * port allocation.  We still want to detect hot-plug
	 * state changes.
	 */
	if (rc == ENODEV || rc == EBUSY) {
		rc = 0;
	}

	/* Flush interrupts for port */
	ahci_pwrite(ap, AHCI_PREG_IS, ahci_pread(ap, AHCI_PREG_IS));
	ahci_write(sc, AHCI_REG_IS, 1 << port);

	/* Enable port interrupts */
	ahci_pwrite(ap, AHCI_PREG_IE,
			AHCI_PREG_IE_TFEE | AHCI_PREG_IE_HBFE |
			AHCI_PREG_IE_IFE | AHCI_PREG_IE_OFE |
			AHCI_PREG_IE_DPE | AHCI_PREG_IE_UFE |
			AHCI_PREG_IE_PCE | AHCI_PREG_IE_PRCE |
#ifdef AHCI_COALESCE
	    ((sc->sc_ccc_ports & (1 << port)) ?
			0 : (AHCI_PREG_IE_SDBE | AHCI_PREG_IE_DHRE))
#else
			AHCI_PREG_IE_SDBE | AHCI_PREG_IE_DHRE
#endif
	    );

freeport:
	if (rc != 0)
		ahci_port_free(sc, port);
reterr:
	return (rc);
}

void
ahci_port_free(struct ahci_softc *sc, u_int port)
{
	struct ahci_port		*ap = sc->sc_ports[port];
	struct ahci_ccb			*ccb;

	/* Ensure port is disabled and its interrupts are flushed */
	if (ap->ap_sc) {
		ahci_pwrite(ap, AHCI_PREG_CMD, 0);
		ahci_pwrite(ap, AHCI_PREG_IE, 0);
		ahci_pwrite(ap, AHCI_PREG_IS, ahci_pread(ap, AHCI_PREG_IS));
		ahci_write(sc, AHCI_REG_IS, 1 << port);
	}

	if (ap->ap_ccbs) {
		while ((ccb = ahci_get_ccb(ap)) != NULL) {
			if (ccb->ccb_dmamap) {
				bus_dmamap_destroy(sc->sc_tag_data,
						   ccb->ccb_dmamap);
				ccb->ccb_dmamap = NULL;
			}
		}
		kfree(ap->ap_ccbs, M_DEVBUF);
		ap->ap_ccbs = NULL;
	}

	if (ap->ap_dmamem_cmd_list) {
		ahci_dmamem_free(sc, ap->ap_dmamem_cmd_list);
		ap->ap_dmamem_cmd_list = NULL;
	}
	if (ap->ap_dmamem_rfis) {
		ahci_dmamem_free(sc, ap->ap_dmamem_rfis);
		ap->ap_dmamem_rfis = NULL;
	}
	if (ap->ap_dmamem_cmd_table) {
		ahci_dmamem_free(sc, ap->ap_dmamem_cmd_table);
		ap->ap_dmamem_cmd_table = NULL;
	}

	/* bus_space(9) says we dont free the subregions handle */

	kfree(ap, M_DEVBUF);
	sc->sc_ports[port] = NULL;
}

int
ahci_port_start(struct ahci_port *ap, int fre_only)
{
	u_int32_t			r;

	/* Turn on FRE (and ST) */
	r = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	r |= AHCI_PREG_CMD_FRE;
	if (!fre_only)
		r |= AHCI_PREG_CMD_ST;
	ahci_pwrite(ap, AHCI_PREG_CMD, r);

#ifdef AHCI_COALESCE
	/* (Re-)enable coalescing on the port. */
	if (ap->ap_sc->sc_ccc_ports & (1 << ap->ap_num)) {
		ap->ap_sc->sc_ccc_ports_cur |= (1 << ap->ap_num);
		ahci_write(ap->ap_sc, AHCI_REG_CCC_PORTS,
		    ap->ap_sc->sc_ccc_ports_cur);
	}
#endif

	if (!(ap->ap_sc->sc_flags & AHCI_F_IGN_FR)) {
		/* Wait for FR to come on */
		if (ahci_pwait_set(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_FR))
			return (2);
	}

	/* Wait for CR to come on */
	if (!fre_only && ahci_pwait_set(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_CR))
		return (1);

	return (0);
}

int
ahci_port_stop(struct ahci_port *ap, int stop_fis_rx)
{
	u_int32_t			r;

#ifdef AHCI_COALESCE
	/* Disable coalescing on the port while it is stopped. */
	if (ap->ap_sc->sc_ccc_ports & (1 << ap->ap_num)) {
		ap->ap_sc->sc_ccc_ports_cur &= ~(1 << ap->ap_num);
		ahci_write(ap->ap_sc, AHCI_REG_CCC_PORTS,
		    ap->ap_sc->sc_ccc_ports_cur);
	}
#endif

	/* Turn off ST (and FRE) */
	r = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	r &= ~AHCI_PREG_CMD_ST;
	if (stop_fis_rx)
		r &= ~AHCI_PREG_CMD_FRE;
	ahci_pwrite(ap, AHCI_PREG_CMD, r);

	/* Wait for CR to go off */
	if (ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_CR))
		return (1);

	/* Wait for FR to go off */
	if (stop_fis_rx && ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_FR))
		return (2);

	return (0);
}

/* AHCI command list override -> forcibly clear TFD.STS.{BSY,DRQ} */
int
ahci_port_clo(struct ahci_port *ap)
{
	struct ahci_softc		*sc = ap->ap_sc;
	u_int32_t			cmd;

	/* Only attempt CLO if supported by controller */
	if ((ahci_read(sc, AHCI_REG_CAP) & AHCI_REG_CAP_SCLO) == 0)
		return (1);

	/* Issue CLO */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
#ifdef DIAGNOSTIC
	if (cmd & AHCI_PREG_CMD_ST) {
		kprintf("%s: CLO requested while port running\n",
			PORTNAME(ap));
	}
#endif
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd | AHCI_PREG_CMD_CLO);

	/* Wait for completion */
	if (ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_CLO)) {
		kprintf("%s: CLO did not complete\n", PORTNAME(ap));
		return (1);
	}

	return (0);
}

/* AHCI soft reset, Section 10.4.1 */
int
ahci_port_softreset(struct ahci_port *ap)
{
	struct ahci_ccb			*ccb = NULL;
	struct ahci_cmd_hdr		*cmd_slot;
	u_int8_t			*fis;
	int				rc = EIO;
	u_int32_t			cmd;

	DPRINTF(AHCI_D_VERBOSE, "%s: soft reset\n", PORTNAME(ap));

	crit_enter();

	/* Save previous command register state */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;

	/* Idle port */
	if (ahci_port_stop(ap, 0)) {
		kprintf("%s: failed to stop port, cannot softreset\n",
			PORTNAME(ap));
		goto err;
	}

	/* Request CLO if device appears hung */
	if (ahci_pread(ap, AHCI_PREG_TFD) &
	     (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		ahci_port_clo(ap);
	}

	/* Clear port errors to permit TFD transfer */
	ahci_pwrite(ap, AHCI_PREG_SERR, ahci_pread(ap, AHCI_PREG_SERR));

	/* Restart port */
	if (ahci_port_start(ap, 0)) {
		kprintf("%s: failed to start port, cannot softreset\n",
		        PORTNAME(ap));
		goto err;
	}

	/* Check whether CLO worked */
	if (ahci_pwait_clr(ap, AHCI_PREG_TFD,
	    AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: CLO %s, need port reset\n",
			PORTNAME(ap),
			(ahci_read(ap->ap_sc, AHCI_REG_CAP) & AHCI_REG_CAP_SCLO)
			? "failed" : "unsupported");
		rc = EBUSY;
		goto err;
	}

	/* Prep first D2H command with SRST feature & clear busy/reset flags */
	ccb = ahci_get_err_ccb(ap);
	cmd_slot = ccb->ccb_cmd_hdr;
	bzero(ccb->ccb_cmd_table, sizeof(struct ahci_cmd_table));

	fis = ccb->ccb_cmd_table->cfis;
	fis[0] = 0x27;	/* Host to device */
	fis[15] = 0x04;	/* SRST DEVCTL */

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_C); /* Clear busy on OK */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_R); /* Reset */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_W); /* Write */

	ccb->ccb_xa.state = ATA_S_PENDING;
	if (ahci_poll(ccb, hz, NULL) != 0)
		goto err;

	/* Prep second D2H command to read status and complete reset sequence */
	fis[0] = 0x27;	/* Host to device */
	fis[15] = 0;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_W);

	ccb->ccb_xa.state = ATA_S_PENDING;
	if (ahci_poll(ccb, hz, NULL) != 0)
		goto err;

	if (ahci_pwait_clr(ap, AHCI_PREG_TFD, AHCI_PREG_TFD_STS_BSY |
	    AHCI_PREG_TFD_STS_DRQ | AHCI_PREG_TFD_STS_ERR)) {
		kprintf("%s: device didn't come ready after reset, TFD: 0x%b\n",
			PORTNAME(ap),
			ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS);
		rc = EBUSY;
		goto err;
	}

	rc = 0;
err:
	if (ccb != NULL) {
		/* Abort our command, if it failed, by stopping command DMA. */
		if (rc != 0 && (ap->ap_active & (1 << ccb->ccb_slot))) {
			kprintf("%s: stopping the port, softreset slot "
				"%d was still active.\n",
				PORTNAME(ap),
				ccb->ccb_slot);
			ahci_port_stop(ap, 0);
		}
		ccb->ccb_xa.state = ATA_S_ERROR;
		ahci_put_err_ccb(ccb);
	}

	/* Restore saved CMD register state */
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	crit_exit();

	return (rc);
}

/* AHCI port reset, Section 10.4.2 */
int
ahci_port_portreset(struct ahci_port *ap)
{
	u_int32_t			cmd, r;
	int				rc;

	DPRINTF(AHCI_D_VERBOSE, "%s: port reset\n", PORTNAME(ap));

	/* Save previous command register state */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;

	/* Clear ST, ignoring failure */
	ahci_port_stop(ap, 0);

	/* Perform device detection */
	ap->ap_ata.ap_type = ATA_PORT_T_NONE;
	ahci_pwrite(ap, AHCI_PREG_SCTL, 0);
	DELAY(10000);
	r = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_INIT;

	if (AhciForceGen1 & (1 << ap->ap_num)) {
		kprintf("%s: Force 1.5Gbits\n", PORTNAME(ap));
		r |= AHCI_PREG_SCTL_SPD_GEN1;
	} else {
		r |= AHCI_PREG_SCTL_SPD_ANY;
	}
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	DELAY(10000);	/* wait at least 1ms for COMRESET to be sent */
	r &= ~AHCI_PREG_SCTL_DET_INIT;
	r |= AHCI_PREG_SCTL_DET_NONE;
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	DELAY(10000);

	/* Wait for device to be detected and communications established */
	if (ahci_pwait_eq(ap, AHCI_PREG_SSTS, AHCI_PREG_SSTS_DET,
			  AHCI_PREG_SSTS_DET_DEV)) {
		rc = ENODEV;
		goto err;
	}

	/* Clear SERR (incl X bit), so TFD can update */
	ahci_pwrite(ap, AHCI_PREG_SERR, ahci_pread(ap, AHCI_PREG_SERR));

	/* Wait for device to become ready */
	/* XXX maybe more than the default wait is appropriate here? */
	if (ahci_pwait_clr(ap, AHCI_PREG_TFD, AHCI_PREG_TFD_STS_BSY |
			       AHCI_PREG_TFD_STS_DRQ | AHCI_PREG_TFD_STS_ERR)) {
		rc = EBUSY;
		kprintf("%s: Device will not come ready 0x%b\n",
			PORTNAME(ap),
			ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS);
		goto err;
	}

	/*
	 * Figure out if we are a ATAPI or DISK device
	 */
	u_int32_t sig;
	sig = ahci_pread(ap, AHCI_PREG_SIG);
	if ((sig & 0xffff0000) == (SATA_SIGNATURE_ATAPI & 0xffff0000)) {
		ap->ap_ata.ap_type = ATA_PORT_T_ATAPI;
	} else {
		ap->ap_ata.ap_type = ATA_PORT_T_DISK;
	}
	rc = 0;
err:
	/* Restore preserved port state */
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	return (rc);
}

int
ahci_load_prdt(struct ahci_ccb *ccb)
{
	struct ahci_port		*ap = ccb->ccb_port;
	struct ahci_softc		*sc = ap->ap_sc;
	struct ata_xfer			*xa = &ccb->ccb_xa;
	struct ahci_prdt		*prdt = ccb->ccb_cmd_table->prdt;
	bus_dmamap_t			dmap = ccb->ccb_dmamap;
	struct ahci_cmd_hdr		*cmd_slot = ccb->ccb_cmd_hdr;
	int				error;

	if (xa->datalen == 0) {
		ccb->ccb_cmd_hdr->prdtl = 0;
		return (0);
	}

	error = bus_dmamap_load(sc->sc_tag_data, dmap,
				xa->data, xa->datalen,
				ahci_load_prdt_callback,
				&prdt,
				((xa->flags & ATA_F_NOWAIT) ?
				    BUS_DMA_NOWAIT : BUS_DMA_WAITOK));
	if (error != 0) {
		kprintf("%s: error %d loading dmamap\n", PORTNAME(ap), error);
		return (1);
	}
	if (xa->flags & ATA_F_PIO)
		prdt->flags |= htole32(AHCI_PRDT_FLAG_INTR);

	cmd_slot->prdtl = htole16(prdt - ccb->ccb_cmd_table->prdt + 1);

	bus_dmamap_sync(sc->sc_tag_data, dmap,
			(xa->flags & ATA_F_READ) ?
			    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	return (0);

#ifdef DIAGNOSTIC
diagerr:
	bus_dmamap_unload(sc->sc_tag_data, dmap);
	return (1);
#endif
}

/*
 * Callback from BUSDMA system to load the segment list.  The passed segment
 * list is a temporary structure.
 */
static
void
ahci_load_prdt_callback(void *info, bus_dma_segment_t *segs, int nsegs,
			int error)
{
	struct ahci_prdt *prd = *(void **)info;
	u_int64_t addr;

	KKASSERT(nsegs <= AHCI_MAX_PRDT);

	while (nsegs) {
		addr = segs->ds_addr;
		prd->dba_hi = htole32((u_int32_t)(addr >> 32));
		prd->dba_lo = htole32((u_int32_t)addr);
#ifdef DIAGNOSTIC
		KKASSERT((addr & 1) == 0);
		KKASSERT((segs->ds_len & 1) == 0);
#endif
		prd->flags = htole32(segs->ds_len - 1);
		--nsegs;
		if (nsegs)
			++prd;
		++segs;
	}
	*(void **)info = prd;	/* return last valid segment */
}

void
ahci_unload_prdt(struct ahci_ccb *ccb)
{
	struct ahci_port		*ap = ccb->ccb_port;
	struct ahci_softc		*sc = ap->ap_sc;
	struct ata_xfer			*xa = &ccb->ccb_xa;
	bus_dmamap_t			dmap = ccb->ccb_dmamap;

	if (xa->datalen != 0) {
		bus_dmamap_sync(sc->sc_tag_data, dmap,
				(xa->flags & ATA_F_READ) ?
				BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->sc_tag_data, dmap);

		if (ccb->ccb_xa.flags & ATA_F_NCQ)
			xa->resid = 0;
		else
			xa->resid = xa->datalen -
			    le32toh(ccb->ccb_cmd_hdr->prdbc);
	}
}

int
ahci_poll(struct ahci_ccb *ccb, int timeout, void (*timeout_fn)(void *))
{
	struct ahci_port		*ap = ccb->ccb_port;

	crit_enter();
	ahci_start(ccb);
	do {
		if (ahci_port_intr(ap, AHCI_PREG_CI_ALL_SLOTS) &
		    (1 << ccb->ccb_slot)) {
			crit_exit();
			return (0);
		}
		DELAY(1000000 / hz);
	} while (--timeout > 0);
	kprintf("timeout ccb state %d\n", ccb->ccb_xa.state);

	if (timeout_fn != NULL)
		timeout_fn(ccb);
	crit_exit();

	return (1);
}

void
ahci_start(struct ahci_ccb *ccb)
{
	struct ahci_port		*ap = ccb->ccb_port;
	struct ahci_softc		*sc = ap->ap_sc;

	KKASSERT(ccb->ccb_xa.state == ATA_S_PENDING);

	/* Zero transferred byte count before transfer */
	ccb->ccb_cmd_hdr->prdbc = 0;

	/* Sync command list entry and corresponding command table entry */
	bus_dmamap_sync(sc->sc_tag_cmdh,
			AHCI_DMA_MAP(ap->ap_dmamem_cmd_list),
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_tag_cmdt,
			AHCI_DMA_MAP(ap->ap_dmamem_cmd_table),
			BUS_DMASYNC_PREWRITE);

	/* Prepare RFIS area for write by controller */
	bus_dmamap_sync(sc->sc_tag_rfis,
			AHCI_DMA_MAP(ap->ap_dmamem_rfis),
			BUS_DMASYNC_PREREAD);

	if (ccb->ccb_xa.flags & ATA_F_NCQ) {
		/* Issue NCQ commands only when there are no outstanding
		 * standard commands. */
		if (ap->ap_active != 0 || !TAILQ_EMPTY(&ap->ap_ccb_pending))
			TAILQ_INSERT_TAIL(&ap->ap_ccb_pending, ccb, ccb_entry);
		else {
			KKASSERT(ap->ap_active_cnt == 0);
			ap->ap_sactive |= (1 << ccb->ccb_slot);
			ccb->ccb_xa.state = ATA_S_ONCHIP;
			ahci_pwrite(ap, AHCI_PREG_SACT, 1 << ccb->ccb_slot);
			ahci_pwrite(ap, AHCI_PREG_CI, 1 << ccb->ccb_slot);
		}
	} else {
		/* Wait for all NCQ commands to finish before issuing standard
		 * command. */
		if (ap->ap_sactive != 0 || ap->ap_active_cnt == 2)
			TAILQ_INSERT_TAIL(&ap->ap_ccb_pending, ccb, ccb_entry);
		else if (ap->ap_active_cnt < 2) {
			ap->ap_active |= 1 << ccb->ccb_slot;
			ccb->ccb_xa.state = ATA_S_ONCHIP;
			ahci_pwrite(ap, AHCI_PREG_CI, 1 << ccb->ccb_slot);
			ap->ap_active_cnt++;
		}
	}
}

void
ahci_issue_pending_ncq_commands(struct ahci_port *ap)
{
	struct ahci_ccb			*nextccb;
	u_int32_t			sact_change = 0;

	KKASSERT(ap->ap_active_cnt == 0);

	nextccb = TAILQ_FIRST(&ap->ap_ccb_pending);
	if (nextccb == NULL || !(nextccb->ccb_xa.flags & ATA_F_NCQ))
		return;

	/* Start all the NCQ commands at the head of the pending list. */
	do {
		TAILQ_REMOVE(&ap->ap_ccb_pending, nextccb, ccb_entry);
		sact_change |= 1 << nextccb->ccb_slot;
		nextccb->ccb_xa.state = ATA_S_ONCHIP;
		nextccb = TAILQ_FIRST(&ap->ap_ccb_pending);
	} while (nextccb && (nextccb->ccb_xa.flags & ATA_F_NCQ));

	ap->ap_sactive |= sact_change;
	ahci_pwrite(ap, AHCI_PREG_SACT, sact_change);
	ahci_pwrite(ap, AHCI_PREG_CI, sact_change);

	return;
}

void
ahci_issue_pending_commands(struct ahci_port *ap, int last_was_ncq)
{
	struct ahci_ccb			*nextccb;

	nextccb = TAILQ_FIRST(&ap->ap_ccb_pending);
	if (nextccb && (nextccb->ccb_xa.flags & ATA_F_NCQ)) {
		KKASSERT(last_was_ncq == 0);	/* otherwise it should have
						 * been started already. */

		/* Issue NCQ commands only when there are no outstanding
		 * standard commands. */
		ap->ap_active_cnt--;
		if (ap->ap_active == 0)
			ahci_issue_pending_ncq_commands(ap);
		else
			KKASSERT(ap->ap_active_cnt == 1);
	} else if (nextccb) {
		if (ap->ap_sactive != 0 || last_was_ncq)
			KKASSERT(ap->ap_active_cnt == 0);

		/* Wait for all NCQ commands to finish before issuing standard
		 * command. */
		if (ap->ap_sactive != 0)
			return;

		/* Keep up to 2 standard commands on-chip at a time. */
		do {
			TAILQ_REMOVE(&ap->ap_ccb_pending, nextccb, ccb_entry);
			ap->ap_active |= 1 << nextccb->ccb_slot;
			nextccb->ccb_xa.state = ATA_S_ONCHIP;
			ahci_pwrite(ap, AHCI_PREG_CI, 1 << nextccb->ccb_slot);
			if (last_was_ncq)
				ap->ap_active_cnt++;
			if (ap->ap_active_cnt == 2)
				break;
			KKASSERT(ap->ap_active_cnt == 1);
			nextccb = TAILQ_FIRST(&ap->ap_ccb_pending);
		} while (nextccb && !(nextccb->ccb_xa.flags & ATA_F_NCQ));
	} else if (!last_was_ncq) {
		KKASSERT(ap->ap_active_cnt == 1 || ap->ap_active_cnt == 2);

		/* Standard command finished, none waiting to start. */
		ap->ap_active_cnt--;
	} else {
		KKASSERT(ap->ap_active_cnt == 0);

		/* NCQ command finished. */
	}
}

void
ahci_intr(void *arg)
{
	struct ahci_softc		*sc = arg;
	u_int32_t			is, ack = 0;
	int				port;

	/* Read global interrupt status */
	is = ahci_read(sc, AHCI_REG_IS);
	if (is == 0 || is == 0xffffffff)
		return;
	ack = is;

#ifdef AHCI_COALESCE
	/* Check coalescing interrupt first */
	if (is & sc->sc_ccc_mask) {
		DPRINTF(AHCI_D_INTR, "%s: command coalescing interrupt\n",
		    DEVNAME(sc));
		is &= ~sc->sc_ccc_mask;
		is |= sc->sc_ccc_ports_cur;
	}
#endif

	/* Process interrupts for each port */
	while (is) {
		port = ffs(is) - 1;
		if (sc->sc_ports[port]) {
			ahci_port_intr(sc->sc_ports[port],
				       AHCI_PREG_CI_ALL_SLOTS);
		}
		is &= ~(1 << port);
	}

	/* Finally, acknowledge global interrupt */
	ahci_write(sc, AHCI_REG_IS, ack);
}

u_int32_t
ahci_port_intr(struct ahci_port *ap, u_int32_t ci_mask)
{
	struct ahci_softc		*sc = ap->ap_sc;
	u_int32_t			is, ci_saved, ci_masked, processed = 0;
	int				slot, need_restart = 0;
	struct ahci_ccb			*ccb = NULL;
	volatile u_int32_t		*active;
#ifdef DIAGNOSTIC
	u_int32_t			tmp;
#endif

	is = ahci_pread(ap, AHCI_PREG_IS);

	/* Ack port interrupt only if checking all command slots. */
	if (ci_mask == AHCI_PREG_CI_ALL_SLOTS)
		ahci_pwrite(ap, AHCI_PREG_IS, is);

	if (is)
		DPRINTF(AHCI_D_INTR, "%s: interrupt: %b\n", PORTNAME(ap),
			is, AHCI_PFMT_IS);

	if (ap->ap_sactive) {
		/* Active NCQ commands - use SActive instead of CI */
		KKASSERT(ap->ap_active == 0);
		KKASSERT(ap->ap_active_cnt == 0);
		ci_saved = ahci_pread(ap, AHCI_PREG_SACT);
		active = &ap->ap_sactive;
	} else {
		/* Save CI */
		ci_saved = ahci_pread(ap, AHCI_PREG_CI);
		active = &ap->ap_active;
	}

	/* Command failed.  See AHCI 1.1 spec 6.2.2.1 and 6.2.2.2. */
	if (is & AHCI_PREG_IS_TFES) {
		u_int32_t		tfd, serr;
		int			err_slot;

		tfd = ahci_pread(ap, AHCI_PREG_TFD);
		serr = ahci_pread(ap, AHCI_PREG_SERR);

		if (ap->ap_sactive == 0) {
			/* Errored slot is easy to determine from CMD. */
			err_slot = AHCI_PREG_CMD_CCS(ahci_pread(ap,
			    AHCI_PREG_CMD));
			ccb = &ap->ap_ccbs[err_slot];

			/* Preserve received taskfile data from the RFIS. */
			memcpy(&ccb->ccb_xa.rfis, ap->ap_rfis->rfis,
			    sizeof(struct ata_fis_d2h));
		} else
			err_slot = -1;	/* Must extract error from log page */

		DPRINTF(AHCI_D_VERBOSE, "%s: errored slot %d, TFD: %b, SERR:"
		    " %b, DIAG: %b\n", PORTNAME(ap), err_slot, tfd,
		    AHCI_PFMT_TFD_STS, AHCI_PREG_SERR_ERR(serr),
		    AHCI_PFMT_SERR_ERR, AHCI_PREG_SERR_DIAG(serr),
		    AHCI_PFMT_SERR_DIAG);

		/* Turn off ST to clear CI and SACT. */
		ahci_port_stop(ap, 0);
		need_restart = 1;

		/* Clear SERR to enable capturing new errors. */
		ahci_pwrite(ap, AHCI_PREG_SERR, serr);

		/* Acknowledge the interrupts we can recover from. */
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_TFES |
		    AHCI_PREG_IS_IFS);
		is = ahci_pread(ap, AHCI_PREG_IS);

		/* If device hasn't cleared its busy status, try to idle it. */
		if (tfd & (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
			kprintf("%s: attempting to idle device\n",
				PORTNAME(ap));
			if (ahci_port_softreset(ap)) {
				kprintf("%s: failed to soft reset device\n",
					PORTNAME(ap));
				if (ahci_port_portreset(ap)) {
					kprintf("%s: failed to port reset "
						"device, give up on it\n",
						PORTNAME(ap));
					goto fatal;
				}
			}

			/* Had to reset device, can't gather extended info. */
		} else if (ap->ap_sactive) {
			/* Recover the NCQ error from log page 10h. */
			ahci_port_read_ncq_error(ap, &err_slot);
			if (err_slot < 0)
				goto failall;

			DPRINTF(AHCI_D_VERBOSE, "%s: NCQ errored slot %d\n",
				PORTNAME(ap), err_slot);

			ccb = &ap->ap_ccbs[err_slot];
		} else {
			/* Didn't reset, could gather extended info from log. */
		}

		/*
		 * If we couldn't determine the errored slot, reset the port
		 * and fail all the active slots.
		 */
		if (err_slot == -1) {
			if (ahci_port_softreset(ap) != 0 &&
			    ahci_port_portreset(ap) != 0) {
				kprintf("%s: couldn't reset after NCQ error, "
					"disabling device.\n",
					PORTNAME(ap));
				goto fatal;
			}
			kprintf("%s: couldn't recover NCQ error, failing "
				"all outstanding commands.\n",
				PORTNAME(ap));
			goto failall;
		}

		/* Clear the failed command in saved CI so completion runs. */
		ci_saved &= ~(1 << err_slot);

		/* Note the error in the ata_xfer. */
		KKASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
		ccb->ccb_xa.state = ATA_S_ERROR;

#ifdef DIAGNOSTIC
		/* There may only be one outstanding standard command now. */
		if (ap->ap_sactive == 0) {
			tmp = ci_saved;
			if (tmp) {
				slot = ffs(tmp) - 1;
				tmp &= ~(1 << slot);
				KKASSERT(tmp == 0);
			}
		}
#endif
	}

	/*
	 * Port change (hot-plug).
	 *
	 * A PCS interrupt will occur on hot-plug once communication is
	 * established.
	 *
	 * A PRCS interrupt will occur on hot-unplug (and possibly also
	 * on hot-plug).
	 *
	 * We can then check the CPS (Cold Presence State) bit to determine
	 * if a device is plugged in or not and do the right thing.
	 */
	if (is & (AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS)) {
		ahci_pwrite(ap, AHCI_PREG_SERR,
			(AHCI_PREG_SERR_DIAG_N | AHCI_PREG_SERR_DIAG_X) << 16);
		switch (ahci_pread(ap, AHCI_PREG_SSTS) & AHCI_PREG_SSTS_DET) {
		case AHCI_PREG_SSTS_DET_DEV:
			if (ap->ap_ata.ap_type == ATA_PORT_T_NONE) {
				kprintf("%s: HOTPLUG - Device added\n",
					PORTNAME(ap));
				if (ahci_port_portreset(ap) == 0)
					ahci_cam_changed(ap);
			}
			break;
		default:
			if (ap->ap_ata.ap_type != ATA_PORT_T_NONE) {
				kprintf("%s: HOTPLUG - Device removed\n",
					PORTNAME(ap));
				ahci_port_portreset(ap);
				ahci_cam_changed(ap);
			}
			break;
		}
	}

	/* Check for remaining errors - they are fatal. */
	if (is & (AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS | AHCI_PREG_IS_IFS |
		  AHCI_PREG_IS_OFS | AHCI_PREG_IS_UFS)) {
		kprintf("%s: unrecoverable errors (IS: %b), disabling port.\n",
			PORTNAME(ap), is, AHCI_PFMT_IS);

		/* XXX try recovery first */
		goto fatal;
	}

	/* Fail all outstanding commands if we know the port won't recover. */
	if (ap->ap_state == AP_S_FATAL_ERROR) {
fatal:
		ap->ap_state = AP_S_FATAL_ERROR;
failall:

		/* Ensure port is shut down. */
		ahci_port_stop(ap, 1);

		/* Error all the active slots. */
		ci_masked = ci_saved & *active;
		while (ci_masked) {
			slot = ffs(ci_masked) - 1;
			ccb = &ap->ap_ccbs[slot];
			ci_masked &= ~(1 << slot);
			ccb->ccb_xa.state = ATA_S_ERROR;
		}

		/* Run completion for all active slots. */
		ci_saved &= ~*active;

		/*
		 * Don't restart the port if our problems were deemed fatal.
		 *
		 * Also acknowlege all fatal interrupt sources to prevent
		 * a livelock.
		 */
		if (ap->ap_state == AP_S_FATAL_ERROR) {
			need_restart = 0;
			ahci_pwrite(ap, AHCI_PREG_IS,
				    AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS |
				    AHCI_PREG_IS_IFS | AHCI_PREG_IS_OFS |
				    AHCI_PREG_IS_UFS);
		}
	}

	/*
	 * CCB completion is detected by noticing its slot's bit in CI has
	 * changed to zero some time after we activated it.
	 * If we are polling, we may only be interested in particular slot(s).
	 */
	ci_masked = ~ci_saved & *active & ci_mask;
	while (ci_masked) {
		slot = ffs(ci_masked) - 1;
		ccb = &ap->ap_ccbs[slot];
		ci_masked &= ~(1 << slot);

		DPRINTF(AHCI_D_INTR, "%s: slot %d is complete%s\n",
		    PORTNAME(ap), slot, ccb->ccb_xa.state == ATA_S_ERROR ?
		    " (error)" : "");

		bus_dmamap_sync(sc->sc_tag_cmdh,
				AHCI_DMA_MAP(ap->ap_dmamem_cmd_list),
				BUS_DMASYNC_POSTWRITE);

		bus_dmamap_sync(sc->sc_tag_cmdt,
				AHCI_DMA_MAP(ap->ap_dmamem_cmd_table),
				BUS_DMASYNC_POSTWRITE);

		bus_dmamap_sync(sc->sc_tag_rfis,
				AHCI_DMA_MAP(ap->ap_dmamem_rfis),
				BUS_DMASYNC_POSTREAD);

		*active &= ~(1 << ccb->ccb_slot);
		ccb->ccb_done(ccb);

		processed |= 1 << ccb->ccb_slot;
	}

	if (need_restart) {
		/* Restart command DMA on the port */
		ahci_port_start(ap, 0);

		/* Re-enable outstanding commands on port. */
		if (ci_saved) {
#ifdef DIAGNOSTIC
			tmp = ci_saved;
			while (tmp) {
				slot = ffs(tmp) - 1;
				tmp &= ~(1 << slot);
				ccb = &ap->ap_ccbs[slot];
				KKASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
				KKASSERT((!!(ccb->ccb_xa.flags & ATA_F_NCQ)) ==
				    (!!ap->ap_sactive));
			}
#endif
			DPRINTF(AHCI_D_VERBOSE, "%s: ahci_port_intr "
			    "re-enabling%s slots %08x\n", PORTNAME(ap),
			    ap->ap_sactive ? " NCQ" : "", ci_saved);

			if (ap->ap_sactive)
				ahci_pwrite(ap, AHCI_PREG_SACT, ci_saved);
			ahci_pwrite(ap, AHCI_PREG_CI, ci_saved);
		}
	}

	return (processed);
}

struct ahci_ccb *
ahci_get_ccb(struct ahci_port *ap)
{
	struct ahci_ccb			*ccb;

	lockmgr(&ap->ap_ccb_lock, LK_EXCLUSIVE);
	ccb = TAILQ_FIRST(&ap->ap_ccb_free);
	if (ccb != NULL) {
		KKASSERT(ccb->ccb_xa.state == ATA_S_PUT);
		TAILQ_REMOVE(&ap->ap_ccb_free, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_SETUP;
	}
	lockmgr(&ap->ap_ccb_lock, LK_RELEASE);

	return (ccb);
}

void
ahci_put_ccb(struct ahci_ccb *ccb)
{
	struct ahci_port		*ap = ccb->ccb_port;

#ifdef DIAGNOSTIC
	if (ccb->ccb_xa.state != ATA_S_COMPLETE &&
	    ccb->ccb_xa.state != ATA_S_TIMEOUT &&
	    ccb->ccb_xa.state != ATA_S_ERROR) {
		kprintf("%s: invalid ata_xfer state %02x in ahci_put_ccb, "
			"slot %d\n",
			PORTNAME(ccb->ccb_port), ccb->ccb_xa.state,
			ccb->ccb_slot);
	}
#endif

	ccb->ccb_xa.state = ATA_S_PUT;
	lockmgr(&ap->ap_ccb_lock, LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&ap->ap_ccb_free, ccb, ccb_entry);
	lockmgr(&ap->ap_ccb_lock, LK_RELEASE);
}

struct ahci_ccb *
ahci_get_err_ccb(struct ahci_port *ap)
{
	struct ahci_ccb *err_ccb;
	u_int32_t sact;

	/* No commands may be active on the chip. */
	sact = ahci_pread(ap, AHCI_PREG_SACT);
	if (sact != 0)
		kprintf("ahci_get_err_ccb but SACT %08x != 0?\n", sact);
	KKASSERT(ahci_pread(ap, AHCI_PREG_CI) == 0);

#ifdef DIAGNOSTIC
	KKASSERT(ap->ap_err_busy == 0);
	ap->ap_err_busy = 1;
#endif
	/* Save outstanding command state. */
	ap->ap_err_saved_active = ap->ap_active;
	ap->ap_err_saved_active_cnt = ap->ap_active_cnt;
	ap->ap_err_saved_sactive = ap->ap_sactive;

	/*
	 * Pretend we have no commands outstanding, so that completions won't
	 * run prematurely.
	 */
	ap->ap_active = ap->ap_active_cnt = ap->ap_sactive = 0;

	/*
	 * Grab a CCB to use for error recovery.  This should never fail, as
	 * we ask atascsi to reserve one for us at init time.
	 */
	err_ccb = ahci_get_ccb(ap);
	KKASSERT(err_ccb != NULL);
	err_ccb->ccb_xa.flags = 0;
	err_ccb->ccb_done = ahci_empty_done;

	return err_ccb;
}

void
ahci_put_err_ccb(struct ahci_ccb *ccb)
{
	struct ahci_port *ap = ccb->ccb_port;
	u_int32_t sact;

#ifdef DIAGNOSTIC
	KKASSERT(ap->ap_err_busy);
#endif
	/* No commands may be active on the chip */
	sact = ahci_pread(ap, AHCI_PREG_SACT);
	if (sact != 0) {
		kprintf("ahci_port_err_ccb_restore but SACT %08x != 0?\n",
			sact);
	}
	KKASSERT(ahci_pread(ap, AHCI_PREG_CI) == 0);

	/* Done with the CCB */
	ahci_put_ccb(ccb);

	/* Restore outstanding command state */
	ap->ap_sactive = ap->ap_err_saved_sactive;
	ap->ap_active_cnt = ap->ap_err_saved_active_cnt;
	ap->ap_active = ap->ap_err_saved_active;

#ifdef DIAGNOSTIC
	ap->ap_err_busy = 0;
#endif
}

int
ahci_port_read_ncq_error(struct ahci_port *ap, int *err_slotp)
{
	struct ahci_ccb			*ccb;
	struct ahci_cmd_hdr		*cmd_slot;
	u_int32_t			cmd;
	struct ata_fis_h2d		*fis;
	int				rc = EIO;

	DPRINTF(AHCI_D_VERBOSE, "%s: read log page\n", PORTNAME(ap));

	/* Save command register state. */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;

	/* Port should have been idled already.  Start it. */
	KKASSERT((cmd & AHCI_PREG_CMD_CR) == 0);
	ahci_port_start(ap, 0);

	/* Prep error CCB for READ LOG EXT, page 10h, 1 sector. */
	ccb = ahci_get_err_ccb(ap);
	ccb->ccb_xa.flags = ATA_F_NOWAIT | ATA_F_READ | ATA_F_POLL;
	ccb->ccb_xa.data = ap->ap_err_scratch;
	ccb->ccb_xa.datalen = 512;
	cmd_slot = ccb->ccb_cmd_hdr;
	bzero(ccb->ccb_cmd_table, sizeof(struct ahci_cmd_table));

	fis = (struct ata_fis_h2d *)ccb->ccb_cmd_table->cfis;
	fis->type = ATA_FIS_TYPE_H2D;
	fis->flags = ATA_H2D_FLAGS_CMD;
	fis->command = ATA_C_READ_LOG_EXT;
	fis->lba_low = 0x10;		/* queued error log page (10h) */
	fis->sector_count = 1;		/* number of sectors (1) */
	fis->sector_count_exp = 0;
	fis->lba_mid = 0;		/* starting offset */
	fis->lba_mid_exp = 0;
	fis->device = 0;

	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */

	if (ahci_load_prdt(ccb) != 0) {
		rc = ENOMEM;	/* XXX caller must abort all commands */
		goto err;
	}

	ccb->ccb_xa.state = ATA_S_PENDING;
	if (ahci_poll(ccb, hz, NULL) != 0)
		goto err;

	rc = 0;
err:
	/* Abort our command, if it failed, by stopping command DMA. */
	if (rc != 0 && (ap->ap_active & (1 << ccb->ccb_slot))) {
		kprintf("%s: log page read failed, slot %d was still active.\n",
			PORTNAME(ap), ccb->ccb_slot);
		ahci_port_stop(ap, 0);
	}

	/* Done with the error CCB now. */
	ahci_unload_prdt(ccb);
	ahci_put_err_ccb(ccb);

	/* Extract failed register set and tags from the scratch space. */
	if (rc == 0) {
		struct ata_log_page_10h		*log;
		int				err_slot;

		log = (struct ata_log_page_10h *)ap->ap_err_scratch;
		if (log->err_regs.type & ATA_LOG_10H_TYPE_NOTQUEUED) {
			/* Not queued bit was set - wasn't an NCQ error? */
			kprintf("%s: read NCQ error page, but not an NCQ "
				"error?\n",
				PORTNAME(ap));
			rc = ESRCH;
		} else {
			/* Copy back the log record as a D2H register FIS. */
			*err_slotp = err_slot = log->err_regs.type &
			    ATA_LOG_10H_TYPE_TAG_MASK;

			ccb = &ap->ap_ccbs[err_slot];
			memcpy(&ccb->ccb_xa.rfis, &log->err_regs,
			    sizeof(struct ata_fis_d2h));
			ccb->ccb_xa.rfis.type = ATA_FIS_TYPE_D2H;
			ccb->ccb_xa.rfis.flags = 0;
		}
	}

	/* Restore saved CMD register state */
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	return (rc);
}

/*
 * Allocate memory for various structures DMAd by hardware.  The maximum
 * number of segments for these tags is 1 so the DMA memory will have a
 * single physical base address.
 */
struct ahci_dmamem *
ahci_dmamem_alloc(struct ahci_softc *sc, bus_dma_tag_t tag)
{
	struct ahci_dmamem *adm;
	int	error;

	adm = kmalloc(sizeof(*adm), M_DEVBUF, M_INTWAIT | M_ZERO);

	error = bus_dmamem_alloc(tag, (void **)&adm->adm_kva,
				 BUS_DMA_ZERO, &adm->adm_map);
	if (error == 0) {
		adm->adm_tag = tag;
		error = bus_dmamap_load(tag, adm->adm_map,
					adm->adm_kva,
					bus_dma_tag_getmaxsize(tag),
					ahci_dmamem_saveseg, &adm->adm_busaddr,
					0);
	}
	if (error) {
		if (adm->adm_map) {
			bus_dmamap_destroy(tag, adm->adm_map);
			adm->adm_map = NULL;
			adm->adm_tag = NULL;
			adm->adm_kva = NULL;
		}
		kfree(adm, M_DEVBUF);
		adm = NULL;
	}
	return (adm);
}

static
void
ahci_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error)
{
	KKASSERT(error == 0);
	KKASSERT(nsegs == 1);
	*(bus_addr_t *)info = segs->ds_addr;
}


void
ahci_dmamem_free(struct ahci_softc *sc, struct ahci_dmamem *adm)
{
	if (adm->adm_map) {
		bus_dmamap_unload(adm->adm_tag, adm->adm_map);
		bus_dmamap_destroy(adm->adm_tag, adm->adm_map);
		adm->adm_map = NULL;
		adm->adm_tag = NULL;
		adm->adm_kva = NULL;
	}
	kfree(adm, M_DEVBUF);
}

u_int32_t
ahci_read(struct ahci_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
			  BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, r));
}

void
ahci_write(struct ahci_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
			  BUS_SPACE_BARRIER_WRITE);
}

int
ahci_wait_ne(struct ahci_softc *sc, bus_size_t r, u_int32_t mask,
	     u_int32_t target)
{
	int				i;

	for (i = 0; i < 1000; i++) {
		if ((ahci_read(sc, r) & mask) != target)
			return (0);
		DELAY(1000);
	}

	return (1);
}

u_int32_t
ahci_pread(struct ahci_port *ap, bus_size_t r)
{
	bus_space_barrier(ap->ap_sc->sc_iot, ap->ap_ioh, r, 4,
			  BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(ap->ap_sc->sc_iot, ap->ap_ioh, r));
}

void
ahci_pwrite(struct ahci_port *ap, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(ap->ap_sc->sc_iot, ap->ap_ioh, r, v);
	bus_space_barrier(ap->ap_sc->sc_iot, ap->ap_ioh, r, 4,
			  BUS_SPACE_BARRIER_WRITE);
}

int
ahci_pwait_eq(struct ahci_port *ap, bus_size_t r, u_int32_t mask,
    u_int32_t target)
{
	int				i;

	for (i = 0; i < 1000; i++) {
		if ((ahci_pread(ap, r) & mask) == target)
			return (0);
		DELAY(1000);
	}

	return (1);
}

struct ata_xfer *
ahci_ata_get_xfer(struct ahci_port *ap)
{
	/*struct ahci_softc	*sc = ap->ap_sc;*/
	struct ahci_ccb		*ccb;

	ccb = ahci_get_ccb(ap);
	if (ccb == NULL) {
		DPRINTF(AHCI_D_XFER, "%s: ahci_ata_get_xfer: NULL ccb\n",
		    PORTNAME(ap));
		return (NULL);
	}

	DPRINTF(AHCI_D_XFER, "%s: ahci_ata_get_xfer got slot %d\n",
	    PORTNAME(ap), ccb->ccb_slot);

	ccb->ccb_xa.fis->type = ATA_FIS_TYPE_H2D;

	return (&ccb->ccb_xa);
}

void
ahci_ata_put_xfer(struct ata_xfer *xa)
{
	struct ahci_ccb			*ccb = (struct ahci_ccb *)xa;

	DPRINTF(AHCI_D_XFER, "ahci_ata_put_xfer slot %d\n", ccb->ccb_slot);

	ahci_put_ccb(ccb);
}

int
ahci_ata_cmd(struct ata_xfer *xa)
{
	struct ahci_ccb			*ccb = (struct ahci_ccb *)xa;
	struct ahci_cmd_hdr		*cmd_slot;

	KKASSERT(xa->state == ATA_S_SETUP);

	if (ccb->ccb_port->ap_state == AP_S_FATAL_ERROR)
		goto failcmd;

	ccb->ccb_done = ahci_ata_cmd_done;

	cmd_slot = ccb->ccb_cmd_hdr;
	cmd_slot->flags = htole16(5); /* FIS length (in DWORDs) */

	if (xa->flags & ATA_F_WRITE)
		cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_W);

	if (xa->flags & ATA_F_PACKET)
		cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_A);

	if (ahci_load_prdt(ccb) != 0)
		goto failcmd;

	xa->state = ATA_S_PENDING;

	if (xa->flags & ATA_F_POLL) {
		ahci_poll(ccb, xa->timeout, ahci_ata_cmd_timeout);
		return (ATA_COMPLETE);
	}

	crit_enter();
	xa->flags |= ATA_F_TIMEOUT_RUNNING;
	callout_reset(&ccb->ccb_timeout, xa->timeout,
		      ahci_ata_cmd_timeout_unserialized, ccb);
	ahci_start(ccb);
	crit_exit();
	return (ATA_QUEUED);

failcmd:
	crit_enter();
	xa->state = ATA_S_ERROR;
	xa->complete(xa);
	crit_exit();
	return (ATA_ERROR);
}

void
ahci_ata_cmd_done(struct ahci_ccb *ccb)
{
	struct ata_xfer			*xa = &ccb->ccb_xa;

	if (xa->flags & ATA_F_TIMEOUT_RUNNING) {
		xa->flags &= ~ATA_F_TIMEOUT_RUNNING;
		callout_stop(&ccb->ccb_timeout);
	}

	if (xa->state == ATA_S_ONCHIP || xa->state == ATA_S_ERROR)
		ahci_issue_pending_commands(ccb->ccb_port,
		    xa->flags & ATA_F_NCQ);

	ahci_unload_prdt(ccb);

	if (xa->state == ATA_S_ONCHIP)
		xa->state = ATA_S_COMPLETE;
#ifdef DIAGNOSTIC
	else if (xa->state != ATA_S_ERROR && xa->state != ATA_S_TIMEOUT)
		kprintf("%s: invalid ata_xfer state %02x in ahci_ata_cmd_done, "
			"slot %d\n",
			PORTNAME(ccb->ccb_port), xa->state, ccb->ccb_slot);
#endif
	if (xa->state != ATA_S_TIMEOUT)
		xa->complete(xa);
}

static void
ahci_ata_cmd_timeout_unserialized(void *arg)
{
	struct ahci_ccb		*ccb = arg;
	struct ahci_port	*ap = ccb->ccb_port;

	lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
	ahci_ata_cmd_timeout(arg);
	lwkt_serialize_exit(&ap->ap_sc->sc_serializer);
}

static void
ahci_ata_cmd_timeout(void *arg)
{
	struct ahci_ccb		*ccb = arg;
	struct ata_xfer		*xa = &ccb->ccb_xa;
	struct ahci_port	*ap = ccb->ccb_port;
	volatile u_int32_t	*active;
	int			ccb_was_started, ncq_cmd;

	crit_enter();
	kprintf("CMD TIMEOUT port-cmd-reg 0x%b\n"
		"\tactive=%08x sactive=%08x\n"
		"\t  sact=%08x      ci=%08x\n",
		ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD,
		ap->ap_active, ap->ap_sactive,
		ahci_pread(ap, AHCI_PREG_SACT),
		ahci_pread(ap, AHCI_PREG_CI));

	KKASSERT(xa->flags & ATA_F_TIMEOUT_RUNNING);
	xa->flags &= ~ATA_F_TIMEOUT_RUNNING;
	ncq_cmd = (xa->flags & ATA_F_NCQ);
	active = ncq_cmd ? &ap->ap_sactive : &ap->ap_active;

	if (ccb->ccb_xa.state == ATA_S_PENDING) {
		DPRINTF(AHCI_D_TIMEOUT, "%s: command for slot %d timed out "
		    "before it got on chip\n", PORTNAME(ap), ccb->ccb_slot);
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb_was_started = 0;
	} else if (ccb->ccb_xa.state == ATA_S_ONCHIP && ahci_port_intr(ap,
	    1 << ccb->ccb_slot)) {
		DPRINTF(AHCI_D_TIMEOUT, "%s: final poll of port completed "
		    "command in slot %d\n", PORTNAME(ap), ccb->ccb_slot);
		goto ret;
	} else if (ccb->ccb_xa.state != ATA_S_ONCHIP) {
		DPRINTF(AHCI_D_TIMEOUT, "%s: command slot %d already "
		    "handled%s\n", PORTNAME(ap), ccb->ccb_slot,
		    (*active & (1 << ccb->ccb_slot)) ?
		    " but slot is still active?" : ".");
		goto ret;
	} else if ((ahci_pread(ap, ncq_cmd ? AHCI_PREG_SACT : AHCI_PREG_CI) &
		    (1 << ccb->ccb_slot)) == 0 &&
		   (*active & (1 << ccb->ccb_slot))) {
		DPRINTF(AHCI_D_TIMEOUT, "%s: command slot %d completed but "
		    "IRQ handler didn't detect it.  Why?\n", PORTNAME(ap),
		    ccb->ccb_slot);
		*active &= ~(1 << ccb->ccb_slot);
		ccb->ccb_done(ccb);
		goto ret;
	} else {
		kprintf("X5\n");
		ccb_was_started = 1;
	}

	/* Complete the slot with a timeout error. */
	ccb->ccb_xa.state = ATA_S_TIMEOUT;
	*active &= ~(1 << ccb->ccb_slot);
	DPRINTF(AHCI_D_TIMEOUT, "%s: run completion (1)\n", PORTNAME(ap));
	ccb->ccb_done(ccb);	/* This won't issue pending commands or run the
				   atascsi completion. */

	/* Reset port to abort running command. */
	if (ccb_was_started) {
		DPRINTF(AHCI_D_TIMEOUT, "%s: resetting port to abort%s command "
		    "in slot %d, active %08x\n", PORTNAME(ap), ncq_cmd ? " NCQ"
		    : "", ccb->ccb_slot, *active);
		if (ahci_port_softreset(ap) != 0 && ahci_port_portreset(ap)
		    != 0) {
			kprintf("%s: failed to reset port during timeout "
				"handling, disabling it\n",
				PORTNAME(ap));
			ap->ap_state = AP_S_FATAL_ERROR;
		}

		/* Restart any other commands that were aborted by the reset. */
		if (*active) {
			DPRINTF(AHCI_D_TIMEOUT, "%s: re-enabling%s slots "
			    "%08x\n", PORTNAME(ap), ncq_cmd ? " NCQ" : "",
			    *active);
			if (ncq_cmd)
				ahci_pwrite(ap, AHCI_PREG_SACT, *active);
			ahci_pwrite(ap, AHCI_PREG_CI, *active);
		}
	}

	/* Issue any pending commands now. */
	DPRINTF(AHCI_D_TIMEOUT, "%s: issue pending\n", PORTNAME(ap));
	if (ccb_was_started)
		ahci_issue_pending_commands(ap, ncq_cmd);
	else if (ap->ap_active == 0)
		ahci_issue_pending_ncq_commands(ap);

	/* Complete the timed out ata_xfer I/O (may generate new I/O). */
	DPRINTF(AHCI_D_TIMEOUT, "%s: run completion (2)\n", PORTNAME(ap));
	xa->complete(xa);

	DPRINTF(AHCI_D_TIMEOUT, "%s: splx\n", PORTNAME(ap));
ret:
	crit_exit();
}

void
ahci_empty_done(struct ahci_ccb *ccb)
{
	ccb->ccb_xa.state = ATA_S_COMPLETE;
}
