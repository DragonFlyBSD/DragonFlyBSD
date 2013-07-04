/*
 * (MPSAFE)
 *
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

void	ahci_port_interrupt_enable(struct ahci_port *ap);

int	ahci_load_prdt(struct ahci_ccb *);
void	ahci_unload_prdt(struct ahci_ccb *);
static void ahci_load_prdt_callback(void *info, bus_dma_segment_t *segs,
				    int nsegs, int error);
void	ahci_start(struct ahci_ccb *);
int	ahci_port_softreset(struct ahci_port *ap);
int	ahci_port_hardreset(struct ahci_port *ap, int hard);
void	ahci_port_hardstop(struct ahci_port *ap);

static void ahci_ata_cmd_timeout_unserialized(void *);
void	ahci_check_active_timeouts(struct ahci_port *ap);

void	ahci_beg_exclusive_access(struct ahci_port *ap, struct ata_port *at);
void	ahci_end_exclusive_access(struct ahci_port *ap, struct ata_port *at);
void	ahci_issue_pending_commands(struct ahci_port *ap, struct ahci_ccb *ccb);
void	ahci_issue_saved_commands(struct ahci_port *ap, u_int32_t mask);

int	ahci_port_read_ncq_error(struct ahci_port *, int);

struct ahci_dmamem *ahci_dmamem_alloc(struct ahci_softc *, bus_dma_tag_t tag);
void	ahci_dmamem_free(struct ahci_softc *, struct ahci_dmamem *);
static void ahci_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error);

static void ahci_dummy_done(struct ata_xfer *xa);
static void ahci_empty_done(struct ahci_ccb *ccb);
static void ahci_ata_cmd_done(struct ahci_ccb *ccb);
static u_int32_t ahci_pactive(struct ahci_port *ap);

/*
 * Initialize the global AHCI hardware.  This code does not set up any of
 * its ports.
 */
int
ahci_init(struct ahci_softc *sc)
{
	u_int32_t	pi, pleft;
	u_int32_t	bios_cap, vers;
	int		i;
	struct ahci_port *ap;

	DPRINTF(AHCI_D_VERBOSE, " GHC 0x%b",
		ahci_read(sc, AHCI_REG_GHC), AHCI_FMT_GHC);

	/*
	 * AHCI version.
	 */
	vers = ahci_read(sc, AHCI_REG_VS);

	/*
	 * save BIOS initialised parameters, enable staggered spin up
	 */
	bios_cap = ahci_read(sc, AHCI_REG_CAP);
	bios_cap &= AHCI_REG_CAP_SMPS | AHCI_REG_CAP_SSS;

	pi = ahci_read(sc, AHCI_REG_PI);

	/*
	 * Unconditionally reset the controller, do not conditionalize on
	 * trying to figure it if it was previously active or not.
	 *
	 * NOTE: On AE before HR.  The AHCI-1.1 spec has a note in section
	 *	 5.2.2.1 regarding this.  HR should be set to 1 only after
	 *	 AE is set to 1.  The reset sequence will clear HR when
	 *	 it completes, and will also clear AE if SAM is 0.  AE must
	 *	 then be set again.  When SAM is 1 the AE bit typically reads
	 *	 as 1 (and is read-only).
	 *
	 * NOTE: Avoid PCI[e] transaction burst by issuing dummy reads,
	 *	 otherwise the writes will only be separated by a few
	 *	 nanoseconds.
	 *
	 * NOTE BRICKS (1)
	 *
	 *	If you have a port multiplier and it does not have a device
	 *	in target 0, and it probes normally, but a later operation
	 *	mis-probes a target behind that PM, it is possible for the
	 *	port to brick such that only (a) a power cycle of the host
	 *	or (b) placing a device in target 0 will fix the problem.
	 *	Power cycling the PM has no effect (it works fine on another
	 *	host port).  This issue is unrelated to CLO.
	 */
	/*
	 * Wait for any prior reset sequence to complete
	 */
	if (ahci_wait_ne(sc, AHCI_REG_GHC,
			 AHCI_REG_GHC_HR, AHCI_REG_GHC_HR) != 0) {
		device_printf(sc->sc_dev, "Controller is stuck in reset\n");
		return (1);
	}
	ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_AE);
	ahci_os_sleep(500);
	ahci_read(sc, AHCI_REG_GHC);		/* flush */
	ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_AE | AHCI_REG_GHC_HR);
	ahci_os_sleep(500);
	ahci_read(sc, AHCI_REG_GHC);		/* flush */
	if (ahci_wait_ne(sc, AHCI_REG_GHC,
			 AHCI_REG_GHC_HR, AHCI_REG_GHC_HR) != 0) {
		device_printf(sc->sc_dev, "unable to reset controller\n");
		return (1);
	}
	if (ahci_read(sc, AHCI_REG_GHC) & AHCI_REG_GHC_AE) {
		device_printf(sc->sc_dev, "AE did not auto-clear!\n");
		ahci_write(sc, AHCI_REG_GHC, 0);
		ahci_os_sleep(500);
	}

	/*
	 * Enable ahci (global interrupts disabled)
	 *
	 * Restore saved parameters.  Avoid pci transaction burst write
	 * by issuing dummy reads.
	 */
	ahci_os_sleep(500);
	ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_AE);
	ahci_os_sleep(500);

	ahci_read(sc, AHCI_REG_GHC);		/* flush */

	bios_cap |= AHCI_REG_CAP_SSS;
	ahci_write(sc, AHCI_REG_CAP, ahci_read(sc, AHCI_REG_CAP) | bios_cap);
	ahci_write(sc, AHCI_REG_PI, pi);
	ahci_read(sc, AHCI_REG_GHC);		/* flush */

	/*
	 * Intel hocus pocus in case the BIOS has not set the chip up
	 * properly for AHCI operation.
	 */
	if (pci_get_vendor(sc->sc_dev) == PCI_VENDOR_INTEL) {
	        if ((pci_read_config(sc->sc_dev, 0x92, 2) & 0x0F) != 0x0F)
			device_printf(sc->sc_dev, "Intel hocus pocus\n");
		pci_write_config(sc->sc_dev, 0x92,
			     pci_read_config(sc->sc_dev, 0x92, 2) | 0x0F, 2);
	}

	/*
	 * This is a hack that currently does not appear to have
	 * a significant effect, but I noticed the port registers
	 * do not appear to be completely cleared after the host
	 * controller is reset.
	 *
	 * Use a temporary ap structure so we can call ahci_pwrite().
	 *
	 * We must be sure to stop the port
	 */
	ap = kmalloc(sizeof(*ap), M_DEVBUF, M_WAITOK | M_ZERO);
	ap->ap_sc = sc;
	pleft = pi;
	for (i = 0; i < AHCI_MAX_PORTS; ++i) {
		if (pleft == 0)
			break;
		if ((pi & (1 << i)) == 0)
			continue;
		if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    AHCI_PORT_REGION(i), AHCI_PORT_SIZE, &ap->ap_ioh) != 0) {
			device_printf(sc->sc_dev, "can't map port\n");
			return (1);
		}
		/*
		 * NOTE!  Setting AHCI_PREG_SCTL_DET_DISABLE on AHCI1.0 or
		 *	  AHCI1.1 can brick the chipset.  Not only brick it,
		 *	  but also crash the PC.  The bit seems unreliable
		 *	  on AHCI1.2 as well.
		 */
		ahci_port_stop(ap, 1);
		ahci_pwrite(ap, AHCI_PREG_SCTL, AHCI_PREG_SCTL_IPM_DISABLED);
		ahci_pwrite(ap, AHCI_PREG_SERR, -1);
		ahci_pwrite(ap, AHCI_PREG_IE, 0);
		ahci_write(ap->ap_sc, AHCI_REG_IS, 1 << i);
		ahci_pwrite(ap, AHCI_PREG_CMD, 0);
		ahci_pwrite(ap, AHCI_PREG_IS, -1);
		sc->sc_portmask |= (1 << i);
		pleft &= ~(1 << i);
	}
	sc->sc_numports = i;
	kfree(ap, M_DEVBUF);

	return (0);
}

/*
 * Allocate and initialize an AHCI port.
 */
int
ahci_port_alloc(struct ahci_softc *sc, u_int port)
{
	struct ahci_port	*ap;
	struct ata_port		*at;
	struct ahci_ccb		*ccb;
	u_int64_t		dva;
	u_int32_t		cmd;
	u_int32_t		data;
	struct ahci_cmd_hdr	*hdr;
	struct ahci_cmd_table	*table;
	int	rc = ENOMEM;
	int	error;
	int	i;

	ap = kmalloc(sizeof(*ap), M_DEVBUF, M_WAITOK | M_ZERO);
	ap->ap_err_scratch = kmalloc(512, M_DEVBUF, M_WAITOK | M_ZERO);

	ksnprintf(ap->ap_name, sizeof(ap->ap_name), "%s%d.%d",
		  device_get_name(sc->sc_dev),
		  device_get_unit(sc->sc_dev),
		  port);
	sc->sc_ports[port] = ap;

	/*
	 * Allocate enough so we never have to reallocate, it makes
	 * it easier.
	 *
	 * ap_pmcount will be reduced by the scan if we encounter the
	 * port multiplier port prior to target 15.
	 *
	 * kmalloc power-of-2 allocations are guaranteed not to cross
	 * a page boundary.  Make sure the identify sub-structure in the
	 * at structure does not cross a page boundary, just in case the
	 * part is AHCI-1.1 and can't handle multiple DRQ blocks.
	 */
	if (ap->ap_ata[0] == NULL) {
		int pw2;

		for (pw2 = 1; pw2 < sizeof(*at); pw2 <<= 1)
			;
		for (i = 0; i < AHCI_MAX_PMPORTS; ++i) {
			at = kmalloc(pw2, M_DEVBUF, M_INTWAIT | M_ZERO);
			ap->ap_ata[i] = at;
			at->at_ahci_port = ap;
			at->at_target = i;
			at->at_probe = ATA_PROBE_NEED_INIT;
			at->at_features |= ATA_PORT_F_RESCAN;
			ksnprintf(at->at_name, sizeof(at->at_name),
				  "%s.%d", ap->ap_name, i);
		}
	}
	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
	    AHCI_PORT_REGION(port), AHCI_PORT_SIZE, &ap->ap_ioh) != 0) {
		device_printf(sc->sc_dev,
			      "unable to create register window for port %d\n",
			      port);
		goto freeport;
	}

	ap->ap_sc = sc;
	ap->ap_num = port;
	ap->ap_probe = ATA_PROBE_NEED_INIT;
	ap->link_pwr_mgmt = AHCI_LINK_PWR_MGMT_NONE;
	ap->sysctl_tree = NULL;
	TAILQ_INIT(&ap->ap_ccb_free);
	TAILQ_INIT(&ap->ap_ccb_pending);
	lockinit(&ap->ap_ccb_lock, "ahcipo", 0, 0);

	/* Disable port interrupts */
	ahci_pwrite(ap, AHCI_PREG_IE, 0);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);

	/*
	 * Sec 10.1.2 - deinitialise port if it is already running
	 */
	cmd = ahci_pread(ap, AHCI_PREG_CMD);
	kprintf("%s: Caps %b\n", PORTNAME(ap), cmd, AHCI_PFMT_CMD);

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
		ahci_pwrite(ap, AHCI_PREG_SCTL, AHCI_PREG_SCTL_IPM_DISABLED);
	}

	/* Allocate RFIS */
	ap->ap_dmamem_rfis = ahci_dmamem_alloc(sc, sc->sc_tag_rfis);
	if (ap->ap_dmamem_rfis == NULL) {
		kprintf("%s: NORFIS\n", PORTNAME(ap));
		goto nomem;
	}

	/* Setup RFIS base address */
	ap->ap_rfis = (struct ahci_rfis *) AHCI_DMA_KVA(ap->ap_dmamem_rfis);
	dva = AHCI_DMA_DVA(ap->ap_dmamem_rfis);
	ahci_pwrite(ap, AHCI_PREG_FBU, (u_int32_t)(dva >> 32));
	ahci_pwrite(ap, AHCI_PREG_FB, (u_int32_t)dva);

	/* Clear SERR before starting FIS reception or ST or anything */
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);

	/* Enable FIS reception and activate port. */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	cmd &= ~(AHCI_PREG_CMD_CLO | AHCI_PREG_CMD_PMA);
	cmd |= AHCI_PREG_CMD_FRE | AHCI_PREG_CMD_POD | AHCI_PREG_CMD_SUD;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd | AHCI_PREG_CMD_ICC_ACTIVE);

	/* Check whether port activated.  Skip it if not. */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	if ((cmd & AHCI_PREG_CMD_FRE) == 0) {
		kprintf("%s: NOT-ACTIVATED\n", PORTNAME(ap));
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

		callout_init_mp(&ccb->ccb_timeout);
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

		ccb->ccb_xa.state = ATA_S_COMPLETE;

		/*
		 * CCB[1] is the error CCB and is not get or put.  It is
		 * also used for probing.  Numerous HBAs only load the
		 * signature from CCB[1] so it MUST be used for the second
		 * FIS.
		 */
		if (i == 1)
			ap->ap_err_ccb = ccb;
		else
			ahci_put_ccb(ccb);
	}

	/*
	 * Wait for ICC change to complete
	 */
	ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_ICC);

	/*
	 * Calculate the interrupt mask
	 */
	data = AHCI_PREG_IE_TFEE | AHCI_PREG_IE_HBFE |
	       AHCI_PREG_IE_IFE | AHCI_PREG_IE_OFE |
	       AHCI_PREG_IE_DPE | AHCI_PREG_IE_UFE |
	       AHCI_PREG_IE_PCE | AHCI_PREG_IE_PRCE |
	       AHCI_PREG_IE_DHRE | AHCI_PREG_IE_SDBE;
	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SSNTF)
		data |= AHCI_PREG_IE_IPME;
#ifdef AHCI_COALESCE
	if (sc->sc_ccc_ports & (1 << port)
		data &= ~(AHCI_PREG_IE_SDBE | AHCI_PREG_IE_DHRE);
#endif
	ap->ap_intmask = data;

	/*
	 * Start the port helper thread.  The helper thread will call
	 * ahci_port_init() so the ports can all be started in parallel.
	 * A failure by ahci_port_init() does not deallocate the port
	 * since we still want hot-plug events.
	 */
	ahci_os_start_port(ap);
	return(0);
freeport:
	ahci_port_free(sc, port);
	return (rc);
}

/*
 * [re]initialize an idle port.  No CCBs should be active.  (from port thread)
 *
 * This function is called during the initial port allocation sequence
 * and is also called on hot-plug insertion.  We take no chances and
 * use a portreset instead of a softreset.
 *
 * This function is the only way to move a failed port back to active
 * status.
 *
 * Returns 0 if a device is successfully detected.
 */
int
ahci_port_init(struct ahci_port *ap)
{
	u_int32_t cmd;

	/*
	 * Register [re]initialization
	 *
	 * Flush the TFD and SERR and make sure the port is stopped before
	 * enabling its interrupt.  We no longer cycle the port start as
	 * the port should not be started unless a device is present.
	 *
	 * XXX should we enable FIS reception? (FRE)?
	 */
	ahci_pwrite(ap, AHCI_PREG_IE, 0);
	ahci_port_stop(ap, 0);
	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SSNTF)
		ahci_pwrite(ap, AHCI_PREG_SNTF, -1);
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);

	/*
	 * If we are being harsh try to kill the port completely.  Normally
	 * we would want to hold on to some of the state the BIOS may have
	 * set, such as SUD (spin up device).
	 *
	 * AP_F_HARSH_REINIT is cleared in the hard reset state
	 */
	if (ap->ap_flags & AP_F_HARSH_REINIT) {
		ahci_pwrite(ap, AHCI_PREG_SCTL, AHCI_PREG_SCTL_IPM_DISABLED);
		ahci_pwrite(ap, AHCI_PREG_CMD, 0);

		ahci_os_sleep(1000);

		cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
		cmd &= ~(AHCI_PREG_CMD_CLO | AHCI_PREG_CMD_PMA);
		cmd |= AHCI_PREG_CMD_FRE | AHCI_PREG_CMD_POD |
		       AHCI_PREG_CMD_SUD;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd | AHCI_PREG_CMD_ICC_ACTIVE);
		cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
		if ((cmd & AHCI_PREG_CMD_FRE) == 0) {
			kprintf("%s: Warning: FRE did not come up during "
				"harsh reinitialization\n",
				PORTNAME(ap));
		}
		ahci_os_sleep(1000);
	}

	/*
	 * Clear any pending garbage and re-enable the interrupt before
	 * going to the next stage.
	 */
	ap->ap_probe = ATA_PROBE_NEED_HARD_RESET;
	ap->ap_pmcount = 0;

	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SSNTF)
		ahci_pwrite(ap, AHCI_PREG_SNTF, -1);
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	ahci_pwrite(ap, AHCI_PREG_IS, -1);

	ahci_port_interrupt_enable(ap);

	return (0);
}

/*
 * Enable or re-enable interrupts on a port.
 *
 * This routine is called from the port initialization code or from the
 * helper thread as the real interrupt may be forced to turn off certain
 * interrupt sources.
 */
void
ahci_port_interrupt_enable(struct ahci_port *ap)
{
	ahci_pwrite(ap, AHCI_PREG_IE, ap->ap_intmask);
}

/*
 * Manage the agressive link power management capability.
 */
void
ahci_port_link_pwr_mgmt(struct ahci_port *ap, int link_pwr_mgmt)
{
	u_int32_t cmd, sctl;

	if (link_pwr_mgmt == ap->link_pwr_mgmt)
		return;

	if ((ap->ap_sc->sc_cap & AHCI_REG_CAP_SALP) == 0) {
		kprintf("%s: link power management not supported.\n",
			PORTNAME(ap));
		return;
	}

	ahci_os_lock_port(ap);

	if (link_pwr_mgmt == AHCI_LINK_PWR_MGMT_AGGR &&
	    (ap->ap_sc->sc_cap & AHCI_REG_CAP_SSC)) {
		kprintf("%s: enabling aggressive link power management.\n",
			PORTNAME(ap));

		ap->link_pwr_mgmt = link_pwr_mgmt;

		ap->ap_intmask &= ~AHCI_PREG_IE_PRCE;
		ahci_port_interrupt_enable(ap);

		sctl = ahci_pread(ap, AHCI_PREG_SCTL);
		sctl &= ~(AHCI_PREG_SCTL_IPM_DISABLED);
		ahci_pwrite(ap, AHCI_PREG_SCTL, sctl);

		/*
		 * Enable device initiated link power management for
		 * directly attached devices that support it.
		 */
		if (ap->ap_type != ATA_PORT_T_PM &&
		    ap->ap_ata[0]->at_identify.satafsup & (1 << 3)) {
			if (ahci_set_feature(ap, NULL, ATA_SATAFT_DEVIPS, 1))
				kprintf("%s: Could not enable device initiated "
				    "link power management.\n",
				    PORTNAME(ap));
		}

		cmd = ahci_pread(ap, AHCI_PREG_CMD);
		cmd |= AHCI_PREG_CMD_ASP;
		cmd |= AHCI_PREG_CMD_ALPE;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	} else if (link_pwr_mgmt == AHCI_LINK_PWR_MGMT_MEDIUM &&
	           (ap->ap_sc->sc_cap & AHCI_REG_CAP_PSC)) {
		kprintf("%s: enabling medium link power management.\n",
			PORTNAME(ap));

		ap->link_pwr_mgmt = link_pwr_mgmt;

		ap->ap_intmask &= ~AHCI_PREG_IE_PRCE;
		ahci_port_interrupt_enable(ap);

		sctl = ahci_pread(ap, AHCI_PREG_SCTL);
		sctl |= AHCI_PREG_SCTL_IPM_DISABLED;
		sctl &= ~AHCI_PREG_SCTL_IPM_NOPARTIAL;
		ahci_pwrite(ap, AHCI_PREG_SCTL, sctl);

		cmd = ahci_pread(ap, AHCI_PREG_CMD);
		cmd &= ~AHCI_PREG_CMD_ASP;
		cmd |= AHCI_PREG_CMD_ALPE;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	} else if (link_pwr_mgmt == AHCI_LINK_PWR_MGMT_NONE) {
		kprintf("%s: disabling link power management.\n",
			PORTNAME(ap));

		/* Disable device initiated link power management */
		if (ap->ap_type != ATA_PORT_T_PM &&
		    ap->ap_ata[0]->at_identify.satafsup & (1 << 3))
			ahci_set_feature(ap, NULL, ATA_SATAFT_DEVIPS, 0);

		cmd = ahci_pread(ap, AHCI_PREG_CMD);
		cmd &= ~(AHCI_PREG_CMD_ALPE | AHCI_PREG_CMD_ASP);
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

		sctl = ahci_pread(ap, AHCI_PREG_SCTL);
		sctl |= AHCI_PREG_SCTL_IPM_DISABLED;
		ahci_pwrite(ap, AHCI_PREG_SCTL, sctl);

		/* let the drive come back to avoid PRCS interrupts later */
		ahci_os_unlock_port(ap);
		ahci_os_sleep(1000);
		ahci_os_lock_port(ap);

		ahci_pwrite(ap, AHCI_PREG_SERR,
			    AHCI_PREG_SERR_DIAG_N | AHCI_PREG_SERR_DIAG_W);
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_PRCS);

		ap->ap_intmask |= AHCI_PREG_IE_PRCE;
		ahci_port_interrupt_enable(ap);

		ap->link_pwr_mgmt = link_pwr_mgmt;
	} else {
		kprintf("%s: unsupported link power management state %d.\n",
			PORTNAME(ap), link_pwr_mgmt);
	}

	ahci_os_unlock_port(ap);
}

/*
 * Return current link power state.
 */
int
ahci_port_link_pwr_state(struct ahci_port *ap)
{
	uint32_t r;

	r = ahci_pread(ap, AHCI_PREG_SSTS);
	switch (r & SATA_PM_SSTS_IPM) {
	case SATA_PM_SSTS_IPM_ACTIVE:
		return 1;
	case SATA_PM_SSTS_IPM_PARTIAL:
		return 2;
	case SATA_PM_SSTS_IPM_SLUMBER:
		return 3;
	default:
		return 0;
	}
}

/*
 * Run the port / target state machine from a main context.
 *
 * The state machine for the port is always run.
 *
 * If atx is non-NULL run the state machine for a particular target.
 * If atx is NULL run the state machine for all targets.
 */
void
ahci_port_state_machine(struct ahci_port *ap, int initial)
{
	struct ata_port *at;
	u_int32_t data;
	int target;
	int didsleep;
	int loop;

	/*
	 * State machine for port.  Note that CAM is not yet associated
	 * during the initial parallel probe and the port's probe state
	 * will not get past ATA_PROBE_NEED_IDENT.
	 */
	{
		if (initial == 0 && ap->ap_probe <= ATA_PROBE_NEED_HARD_RESET) {
			kprintf("%s: Waiting 10 seconds on insertion\n",
				PORTNAME(ap));
			ahci_os_sleep(10000);
			initial = 1;
		}
		if (ap->ap_probe == ATA_PROBE_NEED_INIT)
			ahci_port_init(ap);
		if (ap->ap_probe == ATA_PROBE_NEED_HARD_RESET)
			ahci_port_reset(ap, NULL, 1);
		if (ap->ap_probe == ATA_PROBE_NEED_SOFT_RESET)
			ahci_port_reset(ap, NULL, 0);
		if (ap->ap_probe == ATA_PROBE_NEED_IDENT)
			ahci_cam_probe(ap, NULL);
	}
	if (ap->ap_type != ATA_PORT_T_PM) {
		if (ap->ap_probe == ATA_PROBE_FAILED) {
			ahci_cam_changed(ap, NULL, 0);
		} else if (ap->ap_probe >= ATA_PROBE_NEED_IDENT) {
			ahci_cam_changed(ap, NULL, 1);
		}
		return;
	}

	/*
	 * Port Multiplier state machine.
	 *
	 * Get a mask of changed targets and combine with any runnable
	 * states already present.
	 */
	for (loop = 0; ;++loop) {
		if (ahci_pm_read(ap, 15, SATA_PMREG_EINFO, &data)) {
			kprintf("%s: PM unable to read hot-plug bitmap\n",
				PORTNAME(ap));
			break;
		}

		/*
		 * Do at least one loop, then stop if no more state changes
		 * have occured.  The PM might not generate a new
		 * notification until we clear the entire bitmap.
		 */
		if (loop && data == 0)
			break;

		/*
		 * New devices showing up in the bitmap require some spin-up
		 * time before we start probing them.  Reset didsleep.  The
		 * first new device we detect will sleep before probing.
		 *
		 * This only applies to devices whos change bit is set in
		 * the data, and does not apply to the initial boot-time
		 * probe.
		 */
		didsleep = 0;

		for (target = 0; target < ap->ap_pmcount; ++target) {
			at = ap->ap_ata[target];

			/*
			 * Check the target state for targets behind the PM
			 * which have changed state.  This will adjust
			 * at_probe and set ATA_PORT_F_RESCAN
			 *
			 * We want to wait at least 10 seconds before probing
			 * a newly inserted device.  If the check status
			 * indicates a device is present and in need of a
			 * hard reset, we make sure we have slept before
			 * continuing.
			 *
			 * We also need to wait at least 1 second for the
			 * PHY state to change after insertion, if we
			 * haven't already waited the 10 seconds.
			 *
			 * NOTE: When pm_check_good finds a good port it
			 *	 typically starts us in probe state
			 *	 NEED_HARD_RESET rather than INIT.
			 */
			if (data & (1 << target)) {
				if (initial == 0 && didsleep == 0)
					ahci_os_sleep(1000);
				ahci_pm_check_good(ap, target);
				if (initial == 0 && didsleep == 0 &&
				    at->at_probe <= ATA_PROBE_NEED_HARD_RESET
				) {
					didsleep = 1;
					kprintf("%s: Waiting 10 seconds on insertion\n", PORTNAME(ap));
					ahci_os_sleep(10000);
				}
			}

			/*
			 * Report hot-plug events before the probe state
			 * really gets hot.  Only actual events are reported
			 * here to reduce spew.
			 */
			if (data & (1 << target)) {
				kprintf("%s: HOTPLUG (PM) - ", ATANAME(ap, at));
				switch(at->at_probe) {
				case ATA_PROBE_NEED_INIT:
				case ATA_PROBE_NEED_HARD_RESET:
					kprintf("Device inserted\n");
					break;
				case ATA_PROBE_FAILED:
					kprintf("Device removed\n");
					break;
				default:
					kprintf("Device probe in progress\n");
					break;
				}
			}

			/*
			 * Run through the state machine as necessary if
			 * the port is not marked failed.
			 *
			 * The state machine may stop at NEED_IDENT if
			 * CAM is not yet attached.
			 *
			 * Acquire exclusive access to the port while we
			 * are doing this.  This prevents command-completion
			 * from queueing commands for non-polled targets
			 * inbetween our probe steps.  We need to do this
			 * because the reset probes can generate severe PHY
			 * and protocol errors and soft-brick the port.
			 */
			if (at->at_probe != ATA_PROBE_FAILED &&
			    at->at_probe != ATA_PROBE_GOOD) {
				ahci_beg_exclusive_access(ap, at);
				if (at->at_probe == ATA_PROBE_NEED_INIT)
					ahci_pm_port_init(ap, at);
				if (at->at_probe == ATA_PROBE_NEED_HARD_RESET)
					ahci_port_reset(ap, at, 1);
				if (at->at_probe == ATA_PROBE_NEED_SOFT_RESET)
					ahci_port_reset(ap, at, 0);
				if (at->at_probe == ATA_PROBE_NEED_IDENT)
					ahci_cam_probe(ap, at);
				ahci_end_exclusive_access(ap, at);
			}

			/*
			 * Add or remove from CAM
			 */
			if (at->at_features & ATA_PORT_F_RESCAN) {
				at->at_features &= ~ATA_PORT_F_RESCAN;
				if (at->at_probe == ATA_PROBE_FAILED) {
					ahci_cam_changed(ap, at, 0);
				} else if (at->at_probe >= ATA_PROBE_NEED_IDENT) {
					ahci_cam_changed(ap, at, 1);
				}
			}
			data &= ~(1 << target);
		}
		if (data) {
			kprintf("%s: WARNING (PM): extra bits set in "
				"EINFO: %08x\n", PORTNAME(ap), data);
			while (target < AHCI_MAX_PMPORTS) {
				ahci_pm_check_good(ap, target);
				++target;
			}
		}
	}
}


/*
 * De-initialize and detach a port.
 */
void
ahci_port_free(struct ahci_softc *sc, u_int port)
{
	struct ahci_port	*ap = sc->sc_ports[port];
	struct ahci_ccb		*ccb;
	int i;

	/*
	 * Ensure port is disabled and its interrupts are all flushed.
	 */
	if (ap->ap_sc) {
		ahci_port_stop(ap, 1);
		ahci_os_stop_port(ap);
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
		if ((ccb = ap->ap_err_ccb) != NULL) {
			if (ccb->ccb_dmamap) {
				bus_dmamap_destroy(sc->sc_tag_data,
						   ccb->ccb_dmamap);
				ccb->ccb_dmamap = NULL;
			}
			ap->ap_err_ccb = NULL;
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
	if (ap->ap_ata) {
		for (i = 0; i < AHCI_MAX_PMPORTS; ++i) {
			if (ap->ap_ata[i]) {
				kfree(ap->ap_ata[i], M_DEVBUF);
				ap->ap_ata[i] = NULL;
			}
		}
	}
	if (ap->ap_err_scratch) {
		kfree(ap->ap_err_scratch, M_DEVBUF);
		ap->ap_err_scratch = NULL;
	}

	/* bus_space(9) says we dont free the subregions handle */

	kfree(ap, M_DEVBUF);
	sc->sc_ports[port] = NULL;
}

static
u_int32_t
ahci_pactive(struct ahci_port *ap)
{
	u_int32_t mask;

	mask = ahci_pread(ap, AHCI_PREG_CI);
	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SNCQ)
		mask |= ahci_pread(ap, AHCI_PREG_SACT);
	return(mask);
}

/*
 * Start high-level command processing on the port
 */
int
ahci_port_start(struct ahci_port *ap)
{
	u_int32_t	r, s, is, tfd;

	/*
	 * FRE must be turned on before ST.  Wait for FR to go active
	 * before turning on ST.  The spec doesn't seem to think this
	 * is necessary but waiting here avoids an on-off race in the
	 * ahci_port_stop() code.
	 */
	r = ahci_pread(ap, AHCI_PREG_CMD);
	if ((r & AHCI_PREG_CMD_FRE) == 0) {
		r |= AHCI_PREG_CMD_FRE;
		ahci_pwrite(ap, AHCI_PREG_CMD, r);
	}
	if ((ap->ap_sc->sc_flags & AHCI_F_IGN_FR) == 0) {
		if (ahci_pwait_set(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_FR)) {
			kprintf("%s: Cannot start FIS reception\n",
				PORTNAME(ap));
			return (2);
		}
	} else {
		ahci_os_sleep(10);
	}

	/*
	 * Turn on ST, wait for CR to come up.
	 */
	r |= AHCI_PREG_CMD_ST;
	ahci_pwrite(ap, AHCI_PREG_CMD, r);
	if (ahci_pwait_set_to(ap, 2000, AHCI_PREG_CMD, AHCI_PREG_CMD_CR)) {
		s = ahci_pread(ap, AHCI_PREG_SERR);
		is = ahci_pread(ap, AHCI_PREG_IS);
		tfd = ahci_pread(ap, AHCI_PREG_TFD);
		kprintf("%s: Cannot start command DMA\n"
			"NCMP=%b NSERR=%b\n"
			"NEWIS=%b\n"
			"NEWTFD=%b\n",
			PORTNAME(ap),
			r, AHCI_PFMT_CMD, s, AHCI_PFMT_SERR,
			is, AHCI_PFMT_IS,
			tfd, AHCI_PFMT_TFD_STS);
		return (1);
	}

#ifdef AHCI_COALESCE
	/*
	 * (Re-)enable coalescing on the port.
	 */
	if (ap->ap_sc->sc_ccc_ports & (1 << ap->ap_num)) {
		ap->ap_sc->sc_ccc_ports_cur |= (1 << ap->ap_num);
		ahci_write(ap->ap_sc, AHCI_REG_CCC_PORTS,
		    ap->ap_sc->sc_ccc_ports_cur);
	}
#endif

	return (0);
}

/*
 * Stop high-level command processing on a port
 *
 * WARNING!  If the port is stopped while CR is still active our saved
 *	     CI/SACT will race any commands completed by the command
 *	     processor prior to being able to stop.  Thus we never call
 *	     this function unless we intend to dispose of any remaining
 *	     active commands.  In particular, this complicates the timeout
 *	     code.
 */
int
ahci_port_stop(struct ahci_port *ap, int stop_fis_rx)
{
	u_int32_t	r;

#ifdef AHCI_COALESCE
	/*
	 * Disable coalescing on the port while it is stopped.
	 */
	if (ap->ap_sc->sc_ccc_ports & (1 << ap->ap_num)) {
		ap->ap_sc->sc_ccc_ports_cur &= ~(1 << ap->ap_num);
		ahci_write(ap->ap_sc, AHCI_REG_CCC_PORTS,
		    ap->ap_sc->sc_ccc_ports_cur);
	}
#endif

	/*
	 * Turn off ST, then wait for CR to go off.
	 */
	r = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	r &= ~AHCI_PREG_CMD_ST;
	ahci_pwrite(ap, AHCI_PREG_CMD, r);

	if (ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_CR)) {
		kprintf("%s: Port bricked, unable to stop (ST)\n",
			PORTNAME(ap));
		return (1);
	}

#if 0
	/*
	 * Turn off FRE, then wait for FR to go off.  FRE cannot
	 * be turned off until CR transitions to 0.
	 */
	if ((r & AHCI_PREG_CMD_FR) == 0) {
		kprintf("%s: FR stopped, clear FRE for next start\n",
			PORTNAME(ap));
		stop_fis_rx = 2;
	}
#endif
	if (stop_fis_rx) {
		r &= ~AHCI_PREG_CMD_FRE;
		ahci_pwrite(ap, AHCI_PREG_CMD, r);
		if (ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_FR)) {
			kprintf("%s: Port bricked, unable to stop (FRE)\n",
				PORTNAME(ap));
			return (2);
		}
	}

	return (0);
}

/*
 * AHCI command list override -> forcibly clear TFD.STS.{BSY,DRQ}
 */
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
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd | AHCI_PREG_CMD_CLO);

	/* Wait for completion */
	if (ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_CLO)) {
		kprintf("%s: CLO did not complete\n", PORTNAME(ap));
		return (1);
	}

	return (0);
}

/*
 * Reset a port.
 *
 * If hard is 0 perform a softreset of the port.
 * If hard is 1 perform a hard reset of the port.
 *
 * If at is non-NULL an indirect port via a port-multiplier is being
 * reset, otherwise a direct port is being reset.
 *
 * NOTE: Indirect ports can only be soft-reset.
 */
int
ahci_port_reset(struct ahci_port *ap, struct ata_port *at, int hard)
{
	int rc;

	if (hard) {
		if (at)
			rc = ahci_pm_hardreset(ap, at->at_target, hard);
		else
			rc = ahci_port_hardreset(ap, hard);
	} else {
		if (at)
			rc = ahci_pm_softreset(ap, at->at_target);
		else
			rc = ahci_port_softreset(ap);
	}
	return(rc);
}

/*
 * AHCI soft reset, Section 10.4.1
 *
 * (at) will be NULL when soft-resetting a directly-attached device, and
 * non-NULL when soft-resetting a device through a port multiplier.
 *
 * This function keeps port communications intact and attempts to generate
 * a reset to the connected device using device commands.
 */
int
ahci_port_softreset(struct ahci_port *ap)
{
	struct ahci_ccb		*ccb = NULL;
	struct ahci_cmd_hdr	*cmd_slot;
	u_int8_t		*fis;
	int			error;

	error = EIO;

	if (bootverbose) {
		kprintf("%s: START SOFTRESET %b\n", PORTNAME(ap),
			ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD);
	}

	DPRINTF(AHCI_D_VERBOSE, "%s: soft reset\n", PORTNAME(ap));

	crit_enter();
	ap->ap_flags |= AP_F_IN_RESET;
	ap->ap_state = AP_S_NORMAL;

	/*
	 * Remember port state in cmd (main to restore start/stop)
	 *
	 * Idle port.
	 */
	if (ahci_port_stop(ap, 0)) {
		kprintf("%s: failed to stop port, cannot softreset\n",
			PORTNAME(ap));
		goto err;
	}

	/*
	 * Request CLO if device appears hung.
	 */
	if (ahci_pread(ap, AHCI_PREG_TFD) &
		   (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		ahci_port_clo(ap);
	}

	/*
	 * This is an attempt to clear errors so a new signature will
	 * be latched.  It isn't working properly.  XXX
	 */
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);

	/* Restart port */
	if (ahci_port_start(ap)) {
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
		error = EBUSY;
		goto err;
	}

	/*
	 * Prep first D2H command with SRST feature & clear busy/reset flags
	 *
	 * It is unclear which other fields in the FIS are used.  Just zero
	 * everything.
	 *
	 * NOTE!  This CCB is used for both the first and second commands.
	 *	  The second command must use CCB slot 1 to properly load
	 *	  the signature.
	 */
	ccb = ahci_get_err_ccb(ap);
	ccb->ccb_xa.complete = ahci_dummy_done;
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_EXCLUSIVE;
	KKASSERT(ccb->ccb_slot == 1);
	ccb->ccb_xa.at = NULL;
	cmd_slot = ccb->ccb_cmd_hdr;

	fis = ccb->ccb_cmd_table->cfis;
	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[15] = ATA_FIS_CONTROL_SRST|ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_C); /* Clear busy on OK */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_R); /* Reset */

	ccb->ccb_xa.state = ATA_S_PENDING;

	if (ahci_poll(ccb, 1000, ahci_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: First FIS failed\n", PORTNAME(ap));
		goto err;
	}

	/*
	 * WARNING!	TIME SENSITIVE SPACE!	WARNING!
	 *
	 * The two FISes are supposed to be back to back.  Don't issue other
	 * commands or even delay if we can help it.
	 */

	/*
	 * Prep second D2H command to read status and complete reset sequence
	 * AHCI 10.4.1 and "Serial ATA Revision 2.6".  I can't find the ATA
	 * Rev 2.6 and it is unclear how the second FIS should be set up
	 * from the AHCI document.
	 *
	 * It is unclear which other fields in the FIS are used.  Just zero
	 * everything.
	 */
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_AUTOSENSE | ATA_F_EXCLUSIVE;

	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[15] = ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */

	ccb->ccb_xa.state = ATA_S_PENDING;
	if (ahci_poll(ccb, 1000, ahci_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: Second FIS failed\n", PORTNAME(ap));
		goto err;
	}

	if (ahci_pwait_clr(ap, AHCI_PREG_TFD,
			    AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: device didn't come ready after reset, TFD: 0x%b\n",
			PORTNAME(ap),
			ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS);
		error = EBUSY;
		goto err;
	}

	/*
	 * If the softreset is trying to clear a BSY condition after a
	 * normal portreset we assign the port type.
	 *
	 * If the softreset is being run first as part of the ccb error
	 * processing code then report if the device signature changed
	 * unexpectedly.
	 */
	ahci_os_sleep(100);
	if (ap->ap_type == ATA_PORT_T_NONE) {
		ap->ap_type = ahci_port_signature_detect(ap, NULL);
	} else {
		if (ahci_port_signature_detect(ap, NULL) != ap->ap_type) {
			kprintf("%s: device signature unexpectedly "
				"changed\n", PORTNAME(ap));
			error = EBUSY; /* XXX */
		}
	}
	error = 0;

	ahci_os_sleep(3);
err:
	if (ccb != NULL) {
		ahci_put_err_ccb(ccb);

		/*
		 * If the target is busy use CLO to clear the busy
		 * condition.  The BSY should be cleared on the next
		 * start.
		 */
		if (ahci_pread(ap, AHCI_PREG_TFD) &
		    (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
			ahci_port_clo(ap);
		}
	}

	/*
	 * If we failed to softreset make the port quiescent, otherwise
	 * make sure the port's start/stop state matches what it was on
	 * entry.
	 *
	 * Don't kill the port if the softreset is on a port multiplier
	 * target, that would kill all the targets!
	 */
	if (error) {
		ahci_port_hardstop(ap);
		/* ap_probe set to failed */
	} else {
		ap->ap_probe = ATA_PROBE_NEED_IDENT;
		ap->ap_pmcount = 1;
		ahci_port_start(ap);
	}
	ap->ap_flags &= ~AP_F_IN_RESET;
	crit_exit();

	if (bootverbose)
		kprintf("%s: END SOFTRESET\n", PORTNAME(ap));

	return (error);
}

/*
 * Issue just do the core COMRESET and basic device detection on a port.
 *
 * NOTE: Only called by ahci_port_hardreset().
 */
static int
ahci_comreset(struct ahci_port *ap, int *pmdetectp)
{
	u_int32_t cmd;
	u_int32_t r;
	int error;
	int loop;
	int retries = 0;

	/*
	 * Idle the port,
	 */
	*pmdetectp = 0;
	ahci_port_stop(ap, 0);
	ap->ap_state = AP_S_NORMAL;
	ahci_os_sleep(10);

	/*
	 * The port may have been quiescent with its SUD bit cleared, so
	 * set the SUD (spin up device).
	 *
	 * NOTE: I do not know if SUD is a hardware pin/low-level signal
	 *	 or if it is messaged.
	 */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;

	cmd |= AHCI_PREG_CMD_SUD | AHCI_PREG_CMD_POD;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	ahci_os_sleep(10);

	/*
	 * Make sure that all power management is disabled.
	 *
	 * NOTE!  AHCI_PREG_SCTL_DET_DISABLE seems to be highly unreliable
	 *	  on multiple chipsets and can brick the chipset or even
	 *	  the whole PC.  Never use it.
	 */
	ap->ap_type = ATA_PORT_T_NONE;

	r = AHCI_PREG_SCTL_IPM_DISABLED |
	    AHCI_PREG_SCTL_SPM_DISABLED;
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);

retry:
	/*
	 * Give the new power management state time to settle, then clear
	 * pending status.
	 */
	ahci_os_sleep(1000);
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);

	/*
	 * Start transmitting COMRESET.  The spec says that COMRESET must
	 * be sent for at least 1ms but in actual fact numerous devices
	 * appear to take much longer.  Delay a whole second here.
	 *
	 * In addition, SATA-3 ports can take longer to train, so even
	 * SATA-2 devices which would normally detect very quickly may
	 * take longer when plugged into a SATA-3 port.
	 */
	r |= AHCI_PREG_SCTL_DET_INIT;
	switch(AhciForceGen) {
	case 0:
		r |= AHCI_PREG_SCTL_SPD_ANY;
		break;
	case 1:
		r |= AHCI_PREG_SCTL_SPD_GEN1;
		break;
	case 2:
		r |= AHCI_PREG_SCTL_SPD_GEN2;
		break;
	case 3:
		r |= AHCI_PREG_SCTL_SPD_GEN3;
		break;
	default:
		r |= AHCI_PREG_SCTL_SPD_GEN3;
		break;
	}
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	ahci_os_sleep(1000);

	ap->ap_flags &= ~AP_F_HARSH_REINIT;

	/*
	 * Only SERR_DIAG_X needs to be cleared for TFD updates, but
	 * since we are hard-resetting the port we might as well clear
	 * the whole enchillada.  Also be sure to clear any spurious BSY
	 * prior to clearing INIT.
	 *
	 * Wait 1 whole second after clearing INIT before checking
	 * the device detection bits in an attempt to work around chipsets
	 * which do not properly mask PCS/PRCS during low level init.
	 */
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
/*	ahci_port_clo(ap);*/
	ahci_os_sleep(10);

	r &= ~AHCI_PREG_SCTL_SPD;
	r &= ~AHCI_PREG_SCTL_DET_INIT;
	r |= AHCI_PREG_SCTL_DET_NONE;
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	ahci_os_sleep(1000);

	/*
	 * Try to determine if there is a device on the port.
	 *
	 * Give the device 3/10 second to at least be detected.
	 * If we fail clear PRCS (phy detect) since we may cycled
	 * the phy and probably caused another PRCS interrupt.
	 */
	loop = 300;
	while (loop > 0) {
		r = ahci_pread(ap, AHCI_PREG_SSTS);
		if (r & AHCI_PREG_SSTS_DET)
			break;
		loop -= ahci_os_softsleep();
	}
	if (loop == 0) {
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_PRCS);
		if (bootverbose) {
			kprintf("%s: Port appears to be unplugged\n",
				PORTNAME(ap));
		}
		error = ENODEV;
		goto done;
	}

	/*
	 * There is something on the port.  Regardless of what happens
	 * after this tell the caller to try to detect a port multiplier.
	 *
	 * Give the device 3 seconds to fully negotiate.
	 */
	*pmdetectp = 1;

	if (ahci_pwait_eq(ap, 3000, AHCI_PREG_SSTS,
			  AHCI_PREG_SSTS_DET, AHCI_PREG_SSTS_DET_DEV)) {
		if (bootverbose) {
			kprintf("%s: Device may be powered down\n",
				PORTNAME(ap));
		}
		error = ENODEV;
		goto done;
	}

	/*
	 * We got something that definitely looks like a device.  Give
	 * the device time to send us its first D2H FIS.  Waiting for
	 * BSY to clear accomplishes this.
	 *
	 * NOTE: A port multiplier may or may not clear BSY here,
	 *	 depending on what is sitting in target 0 behind it.
	 *
	 * NOTE: Intel SSDs seem to have compatibility problems with Intel
	 *	 mobo's on cold boots and may leave BSY set.  A single
	 *	 retry works around the problem.  This is definitely a bug
	 *	 with the mobo and/or the SSD and does not appear to occur
	 *	 with other devices connected to the same port.
	 */
	ahci_flush_tfd(ap);
	if (ahci_pwait_clr_to(ap, 8000, AHCI_PREG_TFD,
			    AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: Device BUSY: %b\n",
			PORTNAME(ap),
			ahci_pread(ap, AHCI_PREG_TFD),
				AHCI_PFMT_TFD_STS);
		if (retries == 0) {
			kprintf("%s: Retrying\n", PORTNAME(ap));
			retries = 1;
			goto retry;
		}
		error = EBUSY;
	} else {
		error = 0;
	}

done:
	ahci_flush_tfd(ap);
	return error;
}


/*
 * AHCI port reset, Section 10.4.2
 *
 * This function does a hard reset of the port.  Note that the device
 * connected to the port could still end-up hung.
 */
int
ahci_port_hardreset(struct ahci_port *ap, int hard)
{
	u_int32_t data;
	int	error;
	int	pmdetect;

	if (bootverbose)
		kprintf("%s: START HARDRESET\n", PORTNAME(ap));
	ap->ap_flags |= AP_F_IN_RESET;

	error = ahci_comreset(ap, &pmdetect);

	/*
	 * We may be asked to perform a port multiplier check even if the
	 * comreset failed.  This typically occurs when the PM has nothing
	 * in slot 0, which can cause BSY to remain set.
	 *
	 * If the PM detection is successful it will override (error),
	 * otherwise (error) is retained.  If an error does occur it
	 * is possible that a normal device has blown up on us DUE to
	 * the PM detection code, so re-run the comreset and assume
	 * a normal device.
	 */
	if (pmdetect) {
		if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SPM) {
			error = ahci_pm_port_probe(ap, error);
			if (error) {
				error = ahci_comreset(ap, &pmdetect);
			}
		}
	}

	/*
	 * Finish up.
	 */
	ahci_os_sleep(500);

	switch(error) {
	case 0:
		/*
		 * All good, make sure the port is running and set the
		 * probe state.  Ignore the signature junk (it's unreliable)
		 * until we get to the softreset code.
		 */
		if (ahci_port_start(ap)) {
			kprintf("%s: failed to start command DMA on port, "
			        "disabling\n", PORTNAME(ap));
			error = EBUSY;
			break;
		}
		if (ap->ap_type == ATA_PORT_T_PM)
			ap->ap_probe = ATA_PROBE_GOOD;
		else
			ap->ap_probe = ATA_PROBE_NEED_SOFT_RESET;
		break;
	case ENODEV:
		/*
		 * Normal device probe failure
		 */
		data = ahci_pread(ap, AHCI_PREG_SSTS);

		switch(data & AHCI_PREG_SSTS_DET) {
		case AHCI_PREG_SSTS_DET_DEV_NE:
			kprintf("%s: Device not communicating\n",
				PORTNAME(ap));
			break;
		case AHCI_PREG_SSTS_DET_PHYOFFLINE:
			kprintf("%s: PHY offline\n",
				PORTNAME(ap));
			break;
		default:
			kprintf("%s: No device detected\n",
				PORTNAME(ap));
			break;
		}
		ahci_port_hardstop(ap);
		break;
	default:
		/*
		 * Abnormal probe (EBUSY)
		 */
		kprintf("%s: Device on port is bricked\n",
			PORTNAME(ap));
		ahci_port_hardstop(ap);
#if 0
		rc = ahci_port_reset(ap, atx, 0);
		if (rc) {
			kprintf("%s: Unable unbrick device\n",
				PORTNAME(ap));
		} else {
			kprintf("%s: Successfully unbricked\n",
				PORTNAME(ap));
		}
#endif
		break;
	}

	/*
	 * Clean up
	 */
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS);

	ap->ap_flags &= ~AP_F_IN_RESET;

	if (bootverbose)
		kprintf("%s: END HARDRESET %d\n", PORTNAME(ap), error);
	return (error);
}

/*
 * Hard-stop on hot-swap device removal.  See 10.10.1
 *
 * Place the port in a mode that will allow it to detect hot-swap insertions.
 * This is a bit imprecise because just setting-up SCTL to DET_INIT doesn't
 * seem to do the job.
 *
 * FIS reception is left enabled but command processing is disabled.
 * Cycling FIS reception (FRE) can brick ports.
 */
void
ahci_port_hardstop(struct ahci_port *ap)
{
	struct ahci_ccb *ccb;
	struct ata_port *at;
	u_int32_t r;
	u_int32_t cmd;
	int slot;
	int i;
	int serial;

	/*
	 * Stop the port.  We can't modify things like SUD if the port
	 * is running.
	 */
	ap->ap_state = AP_S_FATAL_ERROR;
	ap->ap_probe = ATA_PROBE_FAILED;
	ap->ap_type = ATA_PORT_T_NONE;
	ahci_port_stop(ap, 0);
	cmd = ahci_pread(ap, AHCI_PREG_CMD);
	cmd &= ~(AHCI_PREG_CMD_CLO | AHCI_PREG_CMD_PMA | AHCI_PREG_CMD_ICC);
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	/*
	 * Clean up AT sub-ports on SATA port.
	 */
	for (i = 0; ap->ap_ata && i < AHCI_MAX_PMPORTS; ++i) {
		at = ap->ap_ata[i];
		at->at_type = ATA_PORT_T_NONE;
		at->at_probe = ATA_PROBE_FAILED;
	}

	/*
	 * Make sure FRE is active.  There isn't anything we can do if it
	 * fails so just ignore errors.
	 */
	if ((cmd & AHCI_PREG_CMD_FRE) == 0) {
		cmd |= AHCI_PREG_CMD_FRE;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
		if ((ap->ap_sc->sc_flags & AHCI_F_IGN_FR) == 0)
			ahci_pwait_set(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_FR);
	}

	/*
	 * 10.10.1 place us in the Listen state.
	 *
	 * 10.10.3 DET must be set to 0 and found to be 0 before
	 * setting SUD to 0.
	 *
	 * Deactivating SUD only applies if the controller supports SUD, it
	 * is a bit unclear what happens w/regards to detecting hotplug
	 * if it doesn't.
	 */
	r = AHCI_PREG_SCTL_IPM_DISABLED |
	    AHCI_PREG_SCTL_SPM_DISABLED;
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	ahci_os_sleep(10);
	cmd &= ~AHCI_PREG_CMD_SUD;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	ahci_os_sleep(10);

	/*
	 * 10.10.1
	 *
	 * Transition su to the spin-up state.  HBA shall send COMRESET and
	 * begin initialization sequence (whatever that means).  Presumably
	 * this is edge-triggered.  Following the spin-up state the HBA
	 * will automatically transition to the Normal state.
	 *
	 * This only applies if the controller supports SUD.
	 * NEVER use AHCI_PREG_DET_DISABLE.
	 */
	cmd |= AHCI_PREG_CMD_POD |
	       AHCI_PREG_CMD_SUD |
	       AHCI_PREG_CMD_ICC_ACTIVE;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	ahci_os_sleep(10);

	/*
	 * Flush SERR_DIAG_X so the TFD can update.
	 */
	ahci_flush_tfd(ap);

	/*
	 * Clean out pending ccbs
	 */
restart:
	while (ap->ap_active) {
		slot = ffs(ap->ap_active) - 1;
		ap->ap_active &= ~(1 << slot);
		--ap->ap_active_cnt;
		ccb = &ap->ap_ccbs[slot];
		if (ccb->ccb_xa.flags & ATA_F_TIMEOUT_RUNNING) {
			serial = ccb->ccb_xa.serial;
			callout_stop_sync(&ccb->ccb_timeout);
			if (serial != ccb->ccb_xa.serial) {
				kprintf("%s: Warning: timeout race ccb %p\n",
					PORTNAME(ap), ccb);
				goto restart;
			}
			ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_RUNNING;
		}
		ap->ap_expired &= ~(1 << slot);
		ccb->ccb_xa.flags &= ~(ATA_F_TIMEOUT_DESIRED |
				       ATA_F_TIMEOUT_EXPIRED);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}
	while (ap->ap_sactive) {
		slot = ffs(ap->ap_sactive) - 1;
		ap->ap_sactive &= ~(1 << slot);
		ccb = &ap->ap_ccbs[slot];
		if (ccb->ccb_xa.flags & ATA_F_TIMEOUT_RUNNING) {
			serial = ccb->ccb_xa.serial;
			callout_stop_sync(&ccb->ccb_timeout);
			if (serial != ccb->ccb_xa.serial) {
				kprintf("%s: Warning: timeout race ccb %p\n",
					PORTNAME(ap), ccb);
				goto restart;
			}
			ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_RUNNING;
		}
		ap->ap_expired &= ~(1 << slot);
		ccb->ccb_xa.flags &= ~(ATA_F_TIMEOUT_DESIRED |
				       ATA_F_TIMEOUT_EXPIRED);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}
	KKASSERT(ap->ap_active_cnt == 0);

	while ((ccb = TAILQ_FIRST(&ap->ap_ccb_pending)) != NULL) {
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_DESIRED;
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}

	/*
	 * Hot-plug device detection should work at this point.  e.g. on
	 * AMD chipsets Spin-Up/Normal state is sufficient for hot-plug
	 * detection and entering RESET (continuous COMRESET by setting INIT)
	 * will actually prevent hot-plug detection from working properly.
	 *
	 * There may be cases where this will fail to work, I have some
	 * additional code to place the HBA in RESET (send continuous
	 * COMRESET) and hopefully get DIAG.X or other events when something
	 * is plugged in.  Unfortunately this isn't universal and can
	 * also prevent events from generating interrupts.
	 */

#if 0
	/*
	 * Transition us to the Reset state.  Theoretically we send a
	 * continuous stream of COMRESETs in this state.
	 */
	r |= AHCI_PREG_SCTL_DET_INIT;
	if (AhciForceGen1 & (1 << ap->ap_num)) {
		kprintf("%s: Force 1.5Gbits\n", PORTNAME(ap));
		r |= AHCI_PREG_SCTL_SPD_GEN1;
	} else {
		r |= AHCI_PREG_SCTL_SPD_ANY;
	}
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	ahci_os_sleep(10);

	/*
	 * Flush SERR_DIAG_X so the TFD can update.
	 */
	ahci_flush_tfd(ap);
#endif
	/* NOP */
}

/*
 * We can't loop on the X bit, a continuous COMINIT received will make
 * it loop forever.  Just assume one event has built up and clear X
 * so the task file descriptor can update.
 */
void
ahci_flush_tfd(struct ahci_port *ap)
{
	u_int32_t r;

	r = ahci_pread(ap, AHCI_PREG_SERR);
	if (r & AHCI_PREG_SERR_DIAG_X)
		ahci_pwrite(ap, AHCI_PREG_SERR, AHCI_PREG_SERR_DIAG_X);
}

/*
 * Figure out what type of device is connected to the port, ATAPI or
 * DISK.
 */
int
ahci_port_signature_detect(struct ahci_port *ap, struct ata_port *at)
{
	u_int32_t sig;

	sig = ahci_pread(ap, AHCI_PREG_SIG);
	if (bootverbose)
		kprintf("%s: sig %08x\n", ATANAME(ap, at), sig);
	if ((sig & 0xffff0000) == (SATA_SIGNATURE_ATAPI & 0xffff0000)) {
		return(ATA_PORT_T_ATAPI);
	} else if ((sig & 0xffff0000) ==
		 (SATA_SIGNATURE_PORT_MULTIPLIER & 0xffff0000)) {
		return(ATA_PORT_T_PM);
	} else {
		return(ATA_PORT_T_DISK);
	}
}

/*
 * Load the DMA descriptor table for a CCB's buffer.
 */
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
#if 0
	if (xa->flags & ATA_F_PIO)
		prdt->flags |= htole32(AHCI_PRDT_FLAG_INTR);
#endif

	cmd_slot->prdtl = htole16(prdt - ccb->ccb_cmd_table->prdt + 1);

	if (xa->flags & ATA_F_READ)
		bus_dmamap_sync(sc->sc_tag_data, dmap, BUS_DMASYNC_PREREAD);
	if (xa->flags & ATA_F_WRITE)
		bus_dmamap_sync(sc->sc_tag_data, dmap, BUS_DMASYNC_PREWRITE);

	return (0);
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
		if (xa->flags & ATA_F_READ) {
			bus_dmamap_sync(sc->sc_tag_data, dmap,
					BUS_DMASYNC_POSTREAD);
		}
		if (xa->flags & ATA_F_WRITE) {
			bus_dmamap_sync(sc->sc_tag_data, dmap,
					BUS_DMASYNC_POSTWRITE);
		}
		bus_dmamap_unload(sc->sc_tag_data, dmap);

		/*
		 * prdbc is only updated by hardware for non-NCQ commands.
		 */
		if (ccb->ccb_xa.flags & ATA_F_NCQ) {
			xa->resid = 0;
		} else {
			if (ccb->ccb_cmd_hdr->prdbc == 0 &&
			    ccb->ccb_xa.state == ATA_S_COMPLETE) {
				kprintf("%s: WARNING!  Unload prdbc resid "
					"was zero! tag=%d\n",
					ATANAME(ap, xa->at), ccb->ccb_slot);
			}
			xa->resid = xa->datalen -
			    le32toh(ccb->ccb_cmd_hdr->prdbc);
		}
	}
}

/*
 * Start a command and poll for completion.
 *
 * timeout is in ms and only counts once the command gets on-chip.
 *
 * Returns ATA_S_* state, compare against ATA_S_COMPLETE to determine
 * that no error occured.
 *
 * NOTE: If the caller specifies a NULL timeout function the caller is
 *	 responsible for clearing hardware state on failure, but we will
 *	 deal with removing the ccb from any pending queue.
 *
 * NOTE: NCQ should never be used with this function.
 *
 * NOTE: If the port is in a failed state and stopped we do not try
 *	 to activate the ccb.
 */
int
ahci_poll(struct ahci_ccb *ccb, int timeout,
	  void (*timeout_fn)(struct ahci_ccb *))
{
	struct ahci_port *ap = ccb->ccb_port;

	if (ccb->ccb_port->ap_state == AP_S_FATAL_ERROR) {
		ccb->ccb_xa.state = ATA_S_ERROR;
		return(ccb->ccb_xa.state);
	}
	crit_enter();
#if 0
	kprintf("%s: Start command %02x tag=%d\n",
		ATANAME(ccb->ccb_port, ccb->ccb_xa.at),
		ccb->ccb_xa.fis->command, ccb->ccb_slot);
#endif
	ahci_start(ccb);

	do {
		ahci_port_intr(ap, 1);
		switch(ccb->ccb_xa.state) {
		case ATA_S_ONCHIP:
			timeout -= ahci_os_softsleep();
			break;
		case ATA_S_PENDING:
			ahci_os_softsleep();
			ahci_check_active_timeouts(ap);
			break;
		default:
			crit_exit();
			return (ccb->ccb_xa.state);
		}
	} while (timeout > 0);

	if ((ccb->ccb_xa.flags & ATA_F_SILENT) == 0) {
		kprintf("%s: Poll timeout slot %d CMD: %b TFD: 0x%b SERR: %b\n",
			ATANAME(ap, ccb->ccb_xa.at), ccb->ccb_slot,
			ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD,
			ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS,
			ahci_pread(ap, AHCI_PREG_SERR), AHCI_PFMT_SERR);
	}

	timeout_fn(ccb);

	crit_exit();

	return(ccb->ccb_xa.state);
}

/*
 * When polling we have to check if the currently active CCB(s)
 * have timed out as the callout will be deadlocked while we
 * hold the port lock.
 */
void
ahci_check_active_timeouts(struct ahci_port *ap)
{
	struct ahci_ccb *ccb;
	u_int32_t mask;
	int tag;

	mask = ap->ap_active | ap->ap_sactive;
	while (mask) {
		tag = ffs(mask) - 1;
		mask &= ~(1 << tag);
		ccb = &ap->ap_ccbs[tag];
		if (ccb->ccb_xa.flags & ATA_F_TIMEOUT_EXPIRED) {
			ahci_ata_cmd_timeout(ccb);
		}
	}
}

static
__inline
void
ahci_start_timeout(struct ahci_ccb *ccb)
{
	if (ccb->ccb_xa.flags & ATA_F_TIMEOUT_DESIRED) {
		ccb->ccb_xa.flags |= ATA_F_TIMEOUT_RUNNING;
		callout_reset(&ccb->ccb_timeout,
			      (ccb->ccb_xa.timeout * hz + 999) / 1000,
			      ahci_ata_cmd_timeout_unserialized, ccb);
	}
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

	/*
	 * There's no point trying to optimize this, it only shaves a few
	 * nanoseconds so just queue the command and call our generic issue.
	 */
	ahci_issue_pending_commands(ap, ccb);
}

/*
 * While holding the port lock acquire exclusive access to the port.
 *
 * This is used when running the state machine to initialize and identify
 * targets over a port multiplier.  Setting exclusive access prevents
 * ahci_port_intr() from activating any requests sitting on the pending
 * queue.
 */
void
ahci_beg_exclusive_access(struct ahci_port *ap, struct ata_port *at)
{
	KKASSERT((ap->ap_flags & AP_F_EXCLUSIVE_ACCESS) == 0);
	ap->ap_flags |= AP_F_EXCLUSIVE_ACCESS;
	while (ap->ap_active || ap->ap_sactive) {
		ahci_port_intr(ap, 1);
		ahci_os_softsleep();
	}
}

void
ahci_end_exclusive_access(struct ahci_port *ap, struct ata_port *at)
{
	KKASSERT((ap->ap_flags & AP_F_EXCLUSIVE_ACCESS) != 0);
	ap->ap_flags &= ~AP_F_EXCLUSIVE_ACCESS;
	ahci_issue_pending_commands(ap, NULL);
}

/*
 * If ccb is not NULL enqueue and/or issue it.
 *
 * If ccb is NULL issue whatever we can from the queue.  However, nothing
 * new is issued if the exclusive access flag is set or expired ccb's are
 * present.
 *
 * If existing commands are still active (ap_active/ap_sactive) we can only
 * issue matching new commands.
 */
void
ahci_issue_pending_commands(struct ahci_port *ap, struct ahci_ccb *ccb)
{
	u_int32_t		mask;
	int			limit;

	/*
	 * Enqueue the ccb.
	 *
	 * If just running the queue and in exclusive access mode we
	 * just return.  Also in this case if there are any expired ccb's
	 * we want to clear the queue so the port can be safely stopped.
	 */
	if (ccb) {
		TAILQ_INSERT_TAIL(&ap->ap_ccb_pending, ccb, ccb_entry);
	} else if ((ap->ap_flags & AP_F_EXCLUSIVE_ACCESS) || ap->ap_expired) {
		return;
	}

	/*
	 * Pull the next ccb off the queue and run it if possible.
	 *
	 * The error CCB supercedes all normal queue operations and
	 * implies exclusive access while the error CCB is active.
	 */
	if (ccb != ap->ap_err_ccb) {
		if ((ccb = TAILQ_FIRST(&ap->ap_ccb_pending)) == NULL)
			return;
		if (ap->ap_flags & AP_F_ERR_CCB_RESERVED) {
			kprintf("DELAY CCB slot %d\n", ccb->ccb_slot);
			return;
		}
	}

	/*
	 * Handle exclusivity requirements.
	 *
	 * ATA_F_EXCLUSIVE is used when we want to be the only command
	 * running.
	 *
	 * ATA_F_AUTOSENSE is used when we want the D2H rfis loaded
	 * back into the ccb on a normal (non-errored) command completion.
	 * For example, for PM requests to target 15.  Because the AHCI
	 * spec does not stop the command processor and has only one rfis
	 * area (for non-FBSS anyway), AUTOSENSE currently implies EXCLUSIVE.
	 * Otherwise multiple completions can destroy the rfis data before
	 * we have a chance to copy it.
	 */
	if (ap->ap_active & ~ap->ap_expired) {
		/*
		 * There may be multiple ccb's already running,
		 * if any are running and ap_run_flags sets
		 * one of these flags then we know only one is
		 * running.
		 *
		 * XXX Current AUTOSENSE code forces exclusivity
		 *     to simplify the code.
		 */
		if (ap->ap_run_flags &
		    (ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE)) {
			return;
		}

		if (ccb->ccb_xa.flags &
		    (ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE)) {
			return;
		}
	}

	if (ccb->ccb_xa.flags & ATA_F_NCQ) {
		/*
		 * The next command is a NCQ command and can be issued as
		 * long as currently active commands are not standard.
		 */
		if (ap->ap_active) {
			KKASSERT(ap->ap_active_cnt > 0);
			return;
		}
		KKASSERT(ap->ap_active_cnt == 0);

		mask = 0;
		do {
			TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
			KKASSERT((mask & (1 << ccb->ccb_slot)) == 0);
			mask |= 1 << ccb->ccb_slot;
			KKASSERT(ccb->ccb_xa.state == ATA_S_PENDING);
			KKASSERT(ccb == &ap->ap_ccbs[ccb->ccb_slot]);
			ccb->ccb_xa.state = ATA_S_ONCHIP;
			ahci_start_timeout(ccb);
			ap->ap_run_flags = ccb->ccb_xa.flags;
			ccb = TAILQ_FIRST(&ap->ap_ccb_pending);
		} while (ccb && (ccb->ccb_xa.flags & ATA_F_NCQ) &&
			 (ap->ap_run_flags &
			     (ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE)) == 0);

		KKASSERT(((ap->ap_active | ap->ap_sactive) & mask) == 0);

		ap->ap_sactive |= mask;
		ahci_pwrite(ap, AHCI_PREG_SACT, mask);
		ahci_pwrite(ap, AHCI_PREG_CI, mask);
	} else {
		/*
		 * The next command is a standard command and can be issued
		 * as long as currently active commands are not NCQ.
		 *
		 * We limit ourself to 1 command if we have a port multiplier,
		 * (at least without FBSS support), otherwise timeouts on
		 * one port can race completions on other ports (see
		 * ahci_ata_cmd_timeout() for more information).
		 *
		 * If not on a port multiplier generally allow up to 4
		 * standard commands to be enqueued.  Remember that the
		 * command processor will still process them sequentially.
		 */
		if (ap->ap_sactive)
			return;
		if (ap->ap_type == ATA_PORT_T_PM)
			limit = 1;
		else if (ap->ap_sc->sc_ncmds > 4)
			limit = 4;
		else
			limit = 2;

		while (ap->ap_active_cnt < limit && ccb &&
		       (ccb->ccb_xa.flags & ATA_F_NCQ) == 0) {
			TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
			KKASSERT(((ap->ap_active | ap->ap_sactive) &
				  (1 << ccb->ccb_slot)) == 0);
			ap->ap_active |= 1 << ccb->ccb_slot;
			ap->ap_active_cnt++;
			ap->ap_run_flags = ccb->ccb_xa.flags;
			ccb->ccb_xa.state = ATA_S_ONCHIP;
			ahci_start_timeout(ccb);
			ahci_pwrite(ap, AHCI_PREG_CI, 1 << ccb->ccb_slot);
			if ((ap->ap_run_flags &
			    (ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE)) == 0) {
				break;
			}
			ccb = TAILQ_FIRST(&ap->ap_ccb_pending);
			if (ccb && (ccb->ccb_xa.flags &
				    (ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE))) {
				break;
			}
		}
	}
}

void
ahci_intr(void *arg)
{
	struct ahci_softc	*sc = arg;
	struct ahci_port	*ap;
	u_int32_t		is;
	u_int32_t		ack;
	int			port;

	/*
	 * Check if the master enable is up, and whether any interrupts are
	 * pending.
	 */
	if ((sc->sc_flags & AHCI_F_INT_GOOD) == 0)
		return;
	is = ahci_read(sc, AHCI_REG_IS);
	if (is == 0 || is == 0xffffffff) {
		return;
	}
	is &= sc->sc_portmask;

#ifdef AHCI_COALESCE
	/* Check coalescing interrupt first */
	if (is & sc->sc_ccc_mask) {
		DPRINTF(AHCI_D_INTR, "%s: command coalescing interrupt\n",
		    DEVNAME(sc));
		is &= ~sc->sc_ccc_mask;
		is |= sc->sc_ccc_ports_cur;
	}
#endif

	/*
	 * Process interrupts for each port in a non-blocking fashion.
	 *
	 * The global IS bit is supposed to be forced on if any unmasked
	 * port interrupt is pending, even if we clear it.
	 *
	 * However it would appear that it is simply latched on some parts,
	 * which means we have to clear it BEFORE processing the status bits
	 * to avoid races.
	 */
	ahci_write(sc, AHCI_REG_IS, is);
	for (ack = 0; is; is &= ~(1 << port)) {
		port = ffs(is) - 1;
		ack |= 1 << port;

		ap = sc->sc_ports[port];
		if (ap == NULL)
			continue;

		if (ahci_os_lock_port_nb(ap) == 0) {
			ahci_port_intr(ap, 0);
			ahci_os_unlock_port(ap);
		} else {
			ahci_pwrite(ap, AHCI_PREG_IE, 0);
			ahci_os_signal_port_thread(ap, AP_SIGF_PORTINT);
		}
	}
}

/*
 * Core called from helper thread.
 */
void
ahci_port_thread_core(struct ahci_port *ap, int mask)
{
	/*
	 * Process any expired timedouts.
	 */
	ahci_os_lock_port(ap);
	if (mask & AP_SIGF_TIMEOUT) {
		ahci_check_active_timeouts(ap);
	}

	/*
	 * Process port interrupts which require a higher level of
	 * intervention.
	 */
	if (mask & AP_SIGF_PORTINT) {
		ahci_port_intr(ap, 1);
		ahci_port_interrupt_enable(ap);
	} else if (ap->ap_probe != ATA_PROBE_FAILED) {
		ahci_port_intr(ap, 1);
		ahci_port_interrupt_enable(ap);
	}
	ahci_os_unlock_port(ap);
}

/*
 * Core per-port interrupt handler.
 *
 * If blockable is 0 we cannot call ahci_os_sleep() at all and we can only
 * deal with normal command completions which do not require blocking.
 */
void
ahci_port_intr(struct ahci_port *ap, int blockable)
{
	struct ahci_softc	*sc = ap->ap_sc;
	u_int32_t		is, ci_saved, ci_masked;
	int			slot;
	int			stopped = 0;
	struct ahci_ccb		*ccb = NULL;
	struct ata_port		*ccb_at = NULL;
	volatile u_int32_t	*active;
	const u_int32_t		blockable_mask = AHCI_PREG_IS_TFES |
						 AHCI_PREG_IS_IFS |
						 AHCI_PREG_IS_PCS |
						 AHCI_PREG_IS_PRCS |
						 AHCI_PREG_IS_HBFS |
						 AHCI_PREG_IS_OFS |
						 AHCI_PREG_IS_UFS;

	enum { NEED_NOTHING, NEED_REINIT, NEED_RESTART,
	       NEED_HOTPLUG_INSERT, NEED_HOTPLUG_REMOVE } need = NEED_NOTHING;

	/*
	 * All basic command completions are always processed.
	 */
	is = ahci_pread(ap, AHCI_PREG_IS);
	if (is & AHCI_PREG_IS_DPS)
		ahci_pwrite(ap, AHCI_PREG_IS, is & AHCI_PREG_IS_DPS);

	/*
	 * If we can't block then we can't handle these here.  Disable
	 * the interrupts in question so we don't live-lock, the helper
	 * thread will re-enable them.
	 *
	 * If the port is in a completely failed state we do not want
	 * to drop through to failed-command-processing if blockable is 0,
	 * just let the thread deal with it all.
	 *
	 * Otherwise we fall through and still handle DHRS and any commands
	 * which completed normally.  Even if we are errored we haven't
	 * stopped the port yet so CI/SACT are still good.
	 */
	if (blockable == 0) {
		if (ap->ap_state == AP_S_FATAL_ERROR) {
			ahci_pwrite(ap, AHCI_PREG_IE, 0);
			ahci_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			return;
		}
		if (is & blockable_mask) {
			ahci_pwrite(ap, AHCI_PREG_IE, 0);
			ahci_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			return;
		}
	}

	/*
	 * Either NCQ or non-NCQ commands will be active, never both.
	 */
	if (ap->ap_sactive) {
		KKASSERT(ap->ap_active == 0);
		KKASSERT(ap->ap_active_cnt == 0);
		ci_saved = ahci_pread(ap, AHCI_PREG_SACT);
		active = &ap->ap_sactive;
	} else {
		ci_saved = ahci_pread(ap, AHCI_PREG_CI);
		active = &ap->ap_active;
	}
	KKASSERT(!(ap->ap_sactive && ap->ap_active));
	KKASSERT((ci_saved & (ap->ap_sactive | ap->ap_active)) == ci_saved);
#if 0
	kprintf("CHECK act=%08x/%08x sact=%08x/%08x\n",
		ap->ap_active, ahci_pread(ap, AHCI_PREG_CI),
		ap->ap_sactive, ahci_pread(ap, AHCI_PREG_SACT));
#endif

	/*
	 * Ignore AHCI_PREG_IS_PRCS when link power management is on
	 */
	if (ap->link_pwr_mgmt != AHCI_LINK_PWR_MGMT_NONE) {
		is &= ~AHCI_PREG_IS_PRCS;
		ahci_pwrite(ap, AHCI_PREG_SERR,
			    AHCI_PREG_SERR_DIAG_N | AHCI_PREG_SERR_DIAG_W);
	}

	/*
	 * Command failed (blockable).
	 *
	 * See AHCI 1.1 spec 6.2.2.1 and 6.2.2.2.
	 *
	 * This stops command processing.
	 */
	if (is & AHCI_PREG_IS_TFES) {
		u_int32_t tfd, serr;
		int	err_slot;

process_error:
		tfd = ahci_pread(ap, AHCI_PREG_TFD);
		serr = ahci_pread(ap, AHCI_PREG_SERR);

		/*
		 * Load the error slot and restart command processing.
		 * CLO if we need to.  The error slot may not be valid.
		 * MUST BE DONE BEFORE CLEARING ST!
		 *
		 * Cycle ST.
		 *
		 * It is unclear but we may have to clear SERR to reenable
		 * error processing.
		 */
		err_slot = AHCI_PREG_CMD_CCS(ahci_pread(ap, AHCI_PREG_CMD));
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_TFES |
					      AHCI_PREG_IS_PSS |
					      AHCI_PREG_IS_DHRS |
					      AHCI_PREG_IS_SDBS);
		is &= ~(AHCI_PREG_IS_TFES | AHCI_PREG_IS_PSS |
			AHCI_PREG_IS_DHRS | AHCI_PREG_IS_SDBS);
		ahci_pwrite(ap, AHCI_PREG_SERR, serr);
		ahci_port_stop(ap, 0);
		ahci_os_hardsleep(10);
		if (tfd & (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
			kprintf("%s: Issuing CLO\n", PORTNAME(ap));
			ahci_port_clo(ap);
		}

		/*
		 * We are now stopped and need a restart.  If we have to
		 * process a NCQ error we will temporarily start and then
		 * stop the port again, so this condition holds.
		 */
		stopped = 1;
		need = NEED_RESTART;

		/*
		 * ATAPI errors are fairly common from probing, just
		 * report disk errors or if bootverbose is on.
		 */
		if (bootverbose || ap->ap_type != ATA_PORT_T_ATAPI) {
			kprintf("%s: TFES slot %d ci_saved = %08x\n",
				PORTNAME(ap), err_slot, ci_saved);
		}

		/*
		 * If we got an error on an error CCB just complete it
		 * with an error.  ci_saved has the mask to restart
		 * (the err_ccb will be removed from it by finish_error).
		 */
		if (ap->ap_flags & AP_F_ERR_CCB_RESERVED) {
			err_slot = ap->ap_err_ccb->ccb_slot;
			goto finish_error;
		}

		/*
		 * If NCQ commands were active get the error slot from
		 * the log page.  NCQ is not supported for PM's so this
		 * is a direct-attached target.
		 *
		 * Otherwise if no commands were active we have a problem.
		 *
		 * Otherwise if the error slot is bad we have a problem.
		 *
		 * Otherwise process the error for the slot.
		 */
		if (ap->ap_sactive) {
			ahci_port_start(ap);
			err_slot = ahci_port_read_ncq_error(ap, 0);
			ahci_port_stop(ap, 0);
		} else if (ap->ap_active == 0) {
			kprintf("%s: TFES with no commands pending\n",
				PORTNAME(ap));
			err_slot = -1;
		} else if (err_slot < 0 || err_slot >= ap->ap_sc->sc_ncmds) {
			kprintf("%s: bad error slot %d\n",
				PORTNAME(ap), err_slot);
			err_slot = -1;
		} else {
			ccb = &ap->ap_ccbs[err_slot];

			/*
			 * Validate the errored ccb.  Note that ccb_at can
			 * be NULL for direct-attached ccb's.
			 *
			 * Copy received taskfile data from the RFIS.
			 */
			if (ccb->ccb_xa.state == ATA_S_ONCHIP) {
				ccb_at = ccb->ccb_xa.at;
				memcpy(&ccb->ccb_xa.rfis, ap->ap_rfis->rfis,
				       sizeof(struct ata_fis_d2h));
				if (bootverbose) {
					kprintf("%s: Copying rfis slot %d\n",
						ATANAME(ap, ccb_at), err_slot);
				}
			} else {
				kprintf("%s: Cannot copy rfis, CCB slot "
					"%d is not on-chip (state=%d)\n",
					ATANAME(ap, ccb->ccb_xa.at),
					err_slot, ccb->ccb_xa.state);
				err_slot = -1;
			}
		}

		/*
		 * If we could not determine the errored slot then
		 * reset the port.
		 */
		if (err_slot < 0) {
			kprintf("%s: TFES: Unable to determine errored slot\n",
				PORTNAME(ap));
			if (ap->ap_flags & AP_F_IN_RESET)
				goto fatal;
			goto failall;
		}

		/*
		 * Finish error on slot.  We will restart ci_saved
		 * commands except the errored slot which we generate
		 * a failure for.
		 */
finish_error:
		ccb = &ap->ap_ccbs[err_slot];
		ci_saved &= ~(1 << err_slot);
		KKASSERT(ccb->ccb_xa.state == ATA_S_ONCHIP);
		ccb->ccb_xa.state = ATA_S_ERROR;
	} else if (is & AHCI_PREG_IS_DHRS) {
		/*
		 * Command posted D2H register FIS to the rfis (non-blocking).
		 *
		 * A normal completion with an error may set DHRS instead
		 * of TFES.  The CCS bits are only valid if ERR was set.
		 * If ERR is set command processing was probably stopped.
		 *
		 * If ERR was not set we can only copy-back data for
		 * exclusive-mode commands because otherwise we won't know
		 * which tag the rfis belonged to.
		 *
		 * err_slot must be read from the CCS before any other port
		 * action, such as stopping the port.
		 *
		 * WARNING!	This is not well documented in the AHCI spec.
		 *		It can be found in the state machine tables
		 *		but not in the explanations.
		 */
		u_int32_t tfd;
		u_int32_t cmd;
		int err_slot;

		tfd = ahci_pread(ap, AHCI_PREG_TFD);
		cmd = ahci_pread(ap, AHCI_PREG_CMD);

		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_DHRS);
		if ((tfd & AHCI_PREG_TFD_STS_ERR) &&
		    (cmd & AHCI_PREG_CMD_CR) == 0) {
			err_slot = AHCI_PREG_CMD_CCS(
						ahci_pread(ap, AHCI_PREG_CMD));
			ccb = &ap->ap_ccbs[err_slot];
			kprintf("%s: DHRS tfd=%b err_slot=%d cmd=%02x\n",
				PORTNAME(ap),
				tfd, AHCI_PFMT_TFD_STS,
				err_slot, ccb->ccb_xa.fis->command);
			goto process_error;
		}
		/*
		 * NO ELSE... copy back is in the normal command completion
		 * code and only if no error occured and ATA_F_AUTOSENSE
		 * was set.
		 */
	}

	/*
	 * Device notification to us (non-blocking)
	 *
	 * NOTE!  On some parts notification bits can cause an IPMS
	 *	  interrupt instead of a SDBS interrupt.
	 *
	 * NOTE!  On some parts (e.g. VBOX, probably intel ICHx),
	 *	  SDBS notifies us of the completion of a NCQ command
	 *	  and DBS does not.
	 */
	if (is & (AHCI_PREG_IS_SDBS | AHCI_PREG_IS_IPMS)) {
		u_int32_t data;

		ahci_pwrite(ap, AHCI_PREG_IS,
				AHCI_PREG_IS_SDBS | AHCI_PREG_IS_IPMS);
		if (sc->sc_cap & AHCI_REG_CAP_SSNTF) {
			data = ahci_pread(ap, AHCI_PREG_SNTF);
			if (data) {
				ahci_pwrite(ap, AHCI_PREG_IS,
						AHCI_PREG_IS_SDBS);
				kprintf("%s: NOTIFY %08x\n",
					PORTNAME(ap), data);
				ahci_pwrite(ap, AHCI_PREG_SERR,
						AHCI_PREG_SERR_DIAG_N);
				ahci_pwrite(ap, AHCI_PREG_SNTF, data);
				ahci_cam_changed(ap, NULL, -1);
			}
		}
		is &= ~(AHCI_PREG_IS_SDBS | AHCI_PREG_IS_IPMS);
	}

	/*
	 * Spurious IFS errors (blockable) - when AP_F_IGNORE_IFS is set.
	 *
	 * Spurious IFS errors can occur while we are doing a reset
	 * sequence through a PM, probably due to an unexpected FIS
	 * being received during the PM target reset sequence.  Chipsets
	 * are supposed to mask these events but some do not.
	 *
	 * Try to recover from the condition.
	 */
	if ((is & AHCI_PREG_IS_IFS) && (ap->ap_flags & AP_F_IGNORE_IFS)) {
		u_int32_t serr = ahci_pread(ap, AHCI_PREG_SERR);
		if ((ap->ap_flags & AP_F_IFS_IGNORED) == 0) {
			kprintf("%s: IFS during PM probe (ignored) "
				"IS=%b, SERR=%b\n",
				PORTNAME(ap),
				is, AHCI_PFMT_IS,
				serr, AHCI_PFMT_SERR);
			ap->ap_flags |= AP_F_IFS_IGNORED;
		}

		/*
		 * Try to clear the error condition.  The IFS error killed
		 * the port so stop it so we can restart it.
		 */
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);
		ahci_pwrite(ap, AHCI_PREG_SERR, -1);
		is &= ~AHCI_PREG_IS_IFS;
		need = NEED_RESTART;
		goto failall;
	}

	/*
	 * Port change (hot-plug) (blockable).
	 *
	 * A PRCS interrupt can occur:
	 *	(1) On hot-unplug / normal-unplug (phy lost)
	 *	(2) Sometimes on hot-plug too.
	 *
	 * A PCS interrupt can occur in a number of situations:
	 *	(1) On hot-plug once communication is established
	 *	(2) On hot-unplug sometimes.
	 *	(3) For chipsets with badly written firmware it can occur
	 *	    during INIT/RESET sequences due to the device reset.
	 *	(4) For chipsets with badly written firmware it can occur
	 *	    when it thinks an unsolicited COMRESET is received
	 *	    during a INIT/RESET sequence, even though we actually
	 *	    did request it.
	 *
	 * XXX We can then check the CPS (Cold Presence State) bit, if
	 * supported, to determine if a device is plugged in or not and do
	 * the right thing.
	 *
	 * PCS interrupts are cleared by clearing DIAG_X.  If this occurs
	 * command processing is automatically stopped (CR goes inactive)
	 * and the port must be stopped and restarted.
	 *
	 * WARNING: AMD parts (e.g. 880G chipset, probably others) can
	 *	    generate PCS on initialization even when device is
	 *	    already connected up.  It is unclear why this happens.
	 *	    Depending on the state of the device detect this can
	 *	    cause us to go into harsh reinit or hot-plug insertion
	 *	    mode.
	 *
	 * WARNING: PCS errors can be repetitive (e.g. unsolicited COMRESET
	 *	    continues to flow in from the device), we must clear the
	 *	    interrupt in all cases and enforce a delay to prevent
	 *	    a livelock and give the port time to settle down.
	 *	    Only print something if we aren't in INIT/HARD-RESET.
	 */
	if (is & (AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS)) {
		ahci_pwrite(ap, AHCI_PREG_IS,
			    is & (AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS));
		/*
		 * Try to clear the error.  Because of the repetitiveness
		 * of this interrupt avoid any harsh action if the port is
		 * already in the init or hard-reset probe state.
		 */
		ahci_pwrite(ap, AHCI_PREG_SERR, -1);
		/* (AHCI_PREG_SERR_DIAG_N | AHCI_PREG_SERR_DIAG_X) */

		/*
		 * Ignore PCS/PRCS errors during probes (but still clear the
		 * interrupt to avoid a livelock).  The AMD 880/890/SB850
		 * chipsets do not mask PCS/PRCS internally during reset
		 * sequences.
		 */
		if (ap->ap_flags & AP_F_IN_RESET)
			goto skip_pcs;

		if (ap->ap_probe == ATA_PROBE_NEED_INIT ||
		    ap->ap_probe == ATA_PROBE_NEED_HARD_RESET) {
			is &= ~(AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS);
			need = NEED_NOTHING;
			ahci_os_sleep(1000);
			goto failall;
		}
		kprintf("%s: Transient Errors: %b (%d)\n",
			PORTNAME(ap), is, AHCI_PFMT_IS, ap->ap_probe);
		is &= ~(AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS);
		ahci_os_sleep(200);

		/*
		 * Stop the port and figure out what to do next.
		 */
		ahci_port_stop(ap, 0);
		stopped = 1;

		switch (ahci_pread(ap, AHCI_PREG_SSTS) & AHCI_PREG_SSTS_DET) {
		case AHCI_PREG_SSTS_DET_DEV:
			/*
			 * Device detect
			 */
			if (ap->ap_probe == ATA_PROBE_FAILED) {
				need = NEED_HOTPLUG_INSERT;
				goto fatal;
			}
			need = NEED_RESTART;
			break;
		case AHCI_PREG_SSTS_DET_DEV_NE:
			/*
			 * Device not communicating.  AMD parts seem to
			 * like to throw this error on initialization
			 * for no reason that I can fathom.
			 */
			kprintf("%s: Device present but not communicating, "
				"attempting port restart\n",
				PORTNAME(ap));
			need = NEED_REINIT;
			goto fatal;
		default:
			if (ap->ap_probe != ATA_PROBE_FAILED) {
				need = NEED_HOTPLUG_REMOVE;
				goto fatal;
			}
			need = NEED_RESTART;
			break;
		}
skip_pcs:
		;
	}

	/*
	 * Check for remaining errors - they are fatal. (blockable)
	 */
	if (is & (AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS | AHCI_PREG_IS_IFS |
		  AHCI_PREG_IS_OFS | AHCI_PREG_IS_UFS)) {
		u_int32_t serr;

		ahci_pwrite(ap, AHCI_PREG_IS,
			    is & (AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS |
				  AHCI_PREG_IS_IFS | AHCI_PREG_IS_OFS |
				  AHCI_PREG_IS_UFS));
		serr = ahci_pread(ap, AHCI_PREG_SERR);
		kprintf("%s: Unrecoverable errors (IS: %b, SERR: %b), "
			"disabling port.\n",
			PORTNAME(ap),
			is, AHCI_PFMT_IS,
			serr, AHCI_PFMT_SERR
		);
		is &= ~(AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS |
			AHCI_PREG_IS_IFS | AHCI_PREG_IS_OFS |
		        AHCI_PREG_IS_UFS);

		/*
		 * Fail all commands but then what?  For now try to
		 * reinitialize the port.
		 */
		need = NEED_REINIT;
		goto fatal;
	}

	/*
	 * Fail all outstanding commands if we know the port won't recover.
	 *
	 * We may have a ccb_at if the failed command is known and was
	 * being sent to a device over a port multiplier (PM).  In this
	 * case if the port itself has not completely failed we fail just
	 * the commands related to that target.
	 *
	 * ci_saved contains the mask of active commands as of when the
	 * error occured, prior to any port stops.
	 */
	if (ap->ap_state == AP_S_FATAL_ERROR) {
fatal:
		ap->ap_state = AP_S_FATAL_ERROR;
failall:
		ahci_port_stop(ap, 0);
		stopped = 1;

		/*
		 * Error all the active slots not already errored.
		 */
		ci_masked = ci_saved & *active & ~ap->ap_expired;
		if (ci_masked) {
			kprintf("%s: Failing all commands: %08x\n",
				PORTNAME(ap), ci_masked);
		}

		while (ci_masked) {
			slot = ffs(ci_masked) - 1;
			ccb = &ap->ap_ccbs[slot];
			ccb->ccb_xa.state = ATA_S_TIMEOUT;
			ap->ap_expired |= 1 << slot;
			ci_saved &= ~(1 << slot);
			ci_masked &= ~(1 << slot);
		}

		/*
		 * Clear bits in ci_saved (cause completions to be run)
		 * for all slots which are not active.
		 */
		ci_saved &= ~*active;

		/*
		 * Don't restart the port if our problems were deemed fatal.
		 *
		 * Also acknowlege all fatal interrupt sources to prevent
		 * a livelock.
		 */
		if (ap->ap_state == AP_S_FATAL_ERROR) {
			if (need == NEED_RESTART)
				need = NEED_NOTHING;
			ahci_pwrite(ap, AHCI_PREG_IS,
				    AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS |
				    AHCI_PREG_IS_IFS | AHCI_PREG_IS_OFS |
				    AHCI_PREG_IS_UFS);
		}
	}

	/*
	 * If we are stopped the AHCI chipset is supposed to have cleared
	 * CI and SACT.  Did it?  If it didn't we try very hard to clear
	 * the fields otherwise we may end up completing CCBs which are
	 * actually still active.
	 *
	 * IFS errors on (at least) AMD chipsets create this confusion.
	 */
	if (stopped) {
		u_int32_t mask;
		if ((mask = ahci_pactive(ap)) != 0) {
			kprintf("%s: chipset failed to clear "
				"active cmds %08x\n",
				PORTNAME(ap), mask);
			ahci_port_start(ap);
			ahci_port_stop(ap, 0);
			if ((mask = ahci_pactive(ap)) != 0) {
				kprintf("%s: unable to prod the chip into "
					"clearing active cmds %08x\n",
					PORTNAME(ap), mask);
				/* what do we do now? */
			}
		}
	}

	/*
	 * CCB completion (non blocking).
	 *
	 * CCB completion is detected by noticing its slot's bit in CI has
	 * changed to zero some time after we activated it.
	 * If we are polling, we may only be interested in particular slot(s).
	 *
	 * Any active bits not saved are completed within the restrictions
	 * imposed by the caller.
	 */
	ci_masked = ~ci_saved & *active;
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
		if (active == &ap->ap_active) {
			KKASSERT(ap->ap_active_cnt > 0);
			--ap->ap_active_cnt;
		}

		/*
		 * Complete the ccb.  If the ccb was marked expired it
		 * was probably already removed from the command processor,
		 * so don't take the clear ci_saved bit as meaning the
		 * command actually succeeded, it didn't.
		 */
		if (ap->ap_expired & (1 << ccb->ccb_slot)) {
			ap->ap_expired &= ~(1 << ccb->ccb_slot);
			ccb->ccb_xa.state = ATA_S_TIMEOUT;
			ccb->ccb_done(ccb);
			ccb->ccb_xa.complete(&ccb->ccb_xa);
		} else {
			if (ccb->ccb_xa.state == ATA_S_ONCHIP) {
				ccb->ccb_xa.state = ATA_S_COMPLETE;
				if (ccb->ccb_xa.flags & ATA_F_AUTOSENSE) {
					memcpy(&ccb->ccb_xa.rfis,
					    ap->ap_rfis->rfis,
					    sizeof(struct ata_fis_d2h));
					if (ccb->ccb_xa.state == ATA_S_TIMEOUT)
						ccb->ccb_xa.state = ATA_S_ERROR;
				}
			}
			ccb->ccb_done(ccb);
		}
	}

	/*
	 * Cleanup.  Will not be set if non-blocking.
	 */
	switch(need) {
	case NEED_NOTHING:
		/*
		 * If operating normally and not stopped the interrupt was
		 * probably just a normal completion and we may be able to
		 * issue more commands.
		 */
		if (stopped == 0 && ap->ap_state != AP_S_FATAL_ERROR)
			ahci_issue_pending_commands(ap, NULL);
		break;
	case NEED_RESTART:
		/*
		 * A recoverable error occured and we can restart outstanding
		 * commands on the port.
		 */
		ci_saved &= ~ap->ap_expired;
		if (ci_saved) {
			kprintf("%s: Restart %08x\n", PORTNAME(ap), ci_saved);
			ahci_issue_saved_commands(ap, ci_saved);
		}

		/*
		 * Potentially issue new commands if not in a failed
		 * state.
		 */
		if (ap->ap_state != AP_S_FATAL_ERROR) {
			ahci_port_start(ap);
			ahci_issue_pending_commands(ap, NULL);
		}
		break;
	case NEED_REINIT:
		/*
		 * Something horrible happened to the port and we
		 * need to reinitialize it.
		 */
		kprintf("%s: REINIT - Attempting to reinitialize the port "
			"after it had a horrible accident\n",
			PORTNAME(ap));
		ap->ap_flags |= AP_F_IN_RESET;
		ap->ap_flags |= AP_F_HARSH_REINIT;
		ap->ap_probe = ATA_PROBE_NEED_INIT;
		ahci_cam_changed(ap, NULL, -1);
		break;
	case NEED_HOTPLUG_INSERT:
		/*
		 * A hot-plug insertion event has occured and all
		 * outstanding commands have already been revoked.
		 *
		 * Don't recurse if this occurs while we are
		 * resetting the port.
		 */
		if ((ap->ap_flags & AP_F_IN_RESET) == 0) {
			kprintf("%s: HOTPLUG - Device inserted\n",
				PORTNAME(ap));
			ap->ap_probe = ATA_PROBE_NEED_INIT;
			ahci_cam_changed(ap, NULL, -1);
		}
		break;
	case NEED_HOTPLUG_REMOVE:
		/*
		 * A hot-plug removal event has occured and all
		 * outstanding commands have already been revoked.
		 *
		 * Don't recurse if this occurs while we are
		 * resetting the port.
		 */
		if ((ap->ap_flags & AP_F_IN_RESET) == 0) {
			kprintf("%s: HOTPLUG - Device removed\n",
				PORTNAME(ap));
			ahci_port_hardstop(ap);
			/* ap_probe set to failed */
			ahci_cam_changed(ap, NULL, -1);
		}
		break;
	default:
		break;
	}
}

struct ahci_ccb *
ahci_get_ccb(struct ahci_port *ap)
{
	struct ahci_ccb			*ccb;

	lockmgr(&ap->ap_ccb_lock, LK_EXCLUSIVE);
	ccb = TAILQ_FIRST(&ap->ap_ccb_free);
	if (ccb != NULL) {
		KKASSERT((ap->ap_sactive & (1 << ccb->ccb_slot)) == 0);
		KKASSERT(ccb->ccb_xa.state == ATA_S_PUT);
		TAILQ_REMOVE(&ap->ap_ccb_free, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_SETUP;
		ccb->ccb_xa.flags = 0;
		ccb->ccb_xa.at = NULL;
	}
	lockmgr(&ap->ap_ccb_lock, LK_RELEASE);

	return (ccb);
}

void
ahci_put_ccb(struct ahci_ccb *ccb)
{
	struct ahci_port		*ap = ccb->ccb_port;

	KKASSERT(ccb->ccb_xa.state != ATA_S_PUT);
	KKASSERT((ap->ap_sactive & (1 << ccb->ccb_slot)) == 0);
	lockmgr(&ap->ap_ccb_lock, LK_EXCLUSIVE);
	ccb->ccb_xa.state = ATA_S_PUT;
	++ccb->ccb_xa.serial;
	TAILQ_INSERT_TAIL(&ap->ap_ccb_free, ccb, ccb_entry);
	lockmgr(&ap->ap_ccb_lock, LK_RELEASE);
}

struct ahci_ccb *
ahci_get_err_ccb(struct ahci_port *ap)
{
	struct ahci_ccb *err_ccb;
	u_int32_t sact;
	u_int32_t ci;

	/* No commands may be active on the chip. */

	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SNCQ) {
		sact = ahci_pread(ap, AHCI_PREG_SACT);
		if (sact != 0) {
			kprintf("%s: ahci_get_err_ccb but SACT %08x != 0?\n",
				PORTNAME(ap), sact);
		}
	}
	ci = ahci_pread(ap, AHCI_PREG_CI);
	if (ci) {
		kprintf("%s: ahci_get_err_ccb: ci not 0 (%08x)\n",
			ap->ap_name, ci);
	}
	KKASSERT(ci == 0);
	KKASSERT((ap->ap_flags & AP_F_ERR_CCB_RESERVED) == 0);
	ap->ap_flags |= AP_F_ERR_CCB_RESERVED;

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
	err_ccb = ap->ap_err_ccb;
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
	u_int32_t ci;

	KKASSERT((ap->ap_flags & AP_F_ERR_CCB_RESERVED) != 0);

	/*
	 * No commands may be active on the chip
	 */
	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SNCQ) {
		sact = ahci_pread(ap, AHCI_PREG_SACT);
		if (sact) {
			panic("ahci_port_err_ccb(%d) but SACT %08x != 0",
			      ccb->ccb_slot, sact);
		}
	}
	ci = ahci_pread(ap, AHCI_PREG_CI);
	if (ci) {
		panic("ahci_put_err_ccb(%d) but CI %08x != 0 "
		      "(act=%08x sact=%08x)\n",
		      ccb->ccb_slot, ci,
		      ap->ap_active, ap->ap_sactive);
	}

	KKASSERT(ccb == ap->ap_err_ccb);

	/* Restore outstanding command state */
	ap->ap_sactive = ap->ap_err_saved_sactive;
	ap->ap_active_cnt = ap->ap_err_saved_active_cnt;
	ap->ap_active = ap->ap_err_saved_active;

	ap->ap_flags &= ~AP_F_ERR_CCB_RESERVED;
}

/*
 * Read log page to get NCQ error.
 *
 * NOTE: NCQ not currently supported on port multipliers. XXX
 */
int
ahci_port_read_ncq_error(struct ahci_port *ap, int target)
{
	struct ata_log_page_10h	*log;
	struct ahci_ccb		*ccb;
	struct ahci_ccb		*ccb2;
	struct ahci_cmd_hdr	*cmd_slot;
	struct ata_fis_h2d	*fis;
	int			err_slot;

	if (bootverbose) {
		kprintf("%s: READ LOG PAGE target %d\n", PORTNAME(ap),
			target);
	}

	/*
	 * Prep error CCB for READ LOG EXT, page 10h, 1 sector.
	 *
	 * Getting err_ccb clears active/sactive/active_cnt, putting
	 * it back restores the fields.
	 */
	ccb = ahci_get_err_ccb(ap);
	ccb->ccb_xa.flags = ATA_F_READ | ATA_F_POLL;
	ccb->ccb_xa.data = ap->ap_err_scratch;
	ccb->ccb_xa.datalen = 512;
	ccb->ccb_xa.complete = ahci_dummy_done;
	ccb->ccb_xa.at = ap->ap_ata[target];

	fis = (struct ata_fis_h2d *)ccb->ccb_cmd_table->cfis;
	bzero(fis, sizeof(*fis));
	fis->type = ATA_FIS_TYPE_H2D;
	fis->flags = ATA_H2D_FLAGS_CMD | target;
	fis->command = ATA_C_READ_LOG_EXT;
	fis->lba_low = 0x10;		/* queued error log page (10h) */
	fis->sector_count = 1;		/* number of sectors (1) */
	fis->sector_count_exp = 0;
	fis->lba_mid = 0;		/* starting offset */
	fis->lba_mid_exp = 0;
	fis->device = 0;

	cmd_slot = ccb->ccb_cmd_hdr;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */

	if (ahci_load_prdt(ccb) != 0) {
		err_slot = -1;
		goto err;
	}

	ccb->ccb_xa.state = ATA_S_PENDING;
	if (ahci_poll(ccb, 1000, ahci_quick_timeout) != ATA_S_COMPLETE) {
		err_slot = -1;
		ahci_unload_prdt(ccb);
		goto err;
	}
	ahci_unload_prdt(ccb);

	/*
	 * Success, extract failed register set and tags from the scratch
	 * space.
	 */
	log = (struct ata_log_page_10h *)ap->ap_err_scratch;
	if (log->err_regs.type & ATA_LOG_10H_TYPE_NOTQUEUED) {
		/* Not queued bit was set - wasn't an NCQ error? */
		kprintf("%s: read NCQ error page, but not an NCQ error?\n",
			PORTNAME(ap));
		err_slot = -1;
	} else {
		/* Copy back the log record as a D2H register FIS. */
		err_slot = log->err_regs.type & ATA_LOG_10H_TYPE_TAG_MASK;

		ccb2 = &ap->ap_ccbs[err_slot];
		if (ccb2->ccb_xa.state == ATA_S_ONCHIP) {
			kprintf("%s: read NCQ error page slot=%d\n",
				ATANAME(ap, ccb2->ccb_xa.at),
				err_slot);
			memcpy(&ccb2->ccb_xa.rfis, &log->err_regs,
				sizeof(struct ata_fis_d2h));
			ccb2->ccb_xa.rfis.type = ATA_FIS_TYPE_D2H;
			ccb2->ccb_xa.rfis.flags = 0;
		} else {
			kprintf("%s: read NCQ error page slot=%d, "
				"slot does not match any cmds\n",
				ATANAME(ccb2->ccb_port, ccb2->ccb_xa.at),
				err_slot);
			err_slot = -1;
		}
	}
err:
	ahci_put_err_ccb(ccb);
	kprintf("%s: DONE log page target %d err_slot=%d\n",
		PORTNAME(ap), target, err_slot);
	return (err_slot);
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

/*
 * Wait up to (timeout) milliseconds for the masked port register to
 * match the target.
 *
 * Timeout is in milliseconds.
 */
int
ahci_pwait_eq(struct ahci_port *ap, int timeout,
	      bus_size_t r, u_int32_t mask, u_int32_t target)
{
	int	t;

	/*
	 * Loop hard up to 100uS
	 */
	for (t = 0; t < 100; ++t) {
		if ((ahci_pread(ap, r) & mask) == target)
			return (0);
		ahci_os_hardsleep(1);	/* us */
	}

	do {
		timeout -= ahci_os_softsleep();
		if ((ahci_pread(ap, r) & mask) == target)
			return (0);
	} while (timeout > 0);
	return (1);
}

int
ahci_wait_ne(struct ahci_softc *sc, bus_size_t r, u_int32_t mask,
	     u_int32_t target)
{
	int	t;

	/*
	 * Loop hard up to 100uS
	 */
	for (t = 0; t < 100; ++t) {
		if ((ahci_read(sc, r) & mask) != target)
			return (0);
		ahci_os_hardsleep(1);	/* us */
	}

	/*
	 * And one millisecond the slow way
	 */
	t = 1000;
	do {
		t -= ahci_os_softsleep();
		if ((ahci_read(sc, r) & mask) != target)
			return (0);
	} while (t > 0);

	return (1);
}


/*
 * Acquire an ata transfer.
 *
 * Pass a NULL at for direct-attached transfers, and a non-NULL at for
 * targets that go through the port multiplier.
 */
struct ata_xfer *
ahci_ata_get_xfer(struct ahci_port *ap, struct ata_port *at)
{
	struct ahci_ccb		*ccb;

	ccb = ahci_get_ccb(ap);
	if (ccb == NULL) {
		DPRINTF(AHCI_D_XFER, "%s: ahci_ata_get_xfer: NULL ccb\n",
		    PORTNAME(ap));
		return (NULL);
	}

	DPRINTF(AHCI_D_XFER, "%s: ahci_ata_get_xfer got slot %d\n",
	    PORTNAME(ap), ccb->ccb_slot);

	bzero(ccb->ccb_xa.fis, sizeof(*ccb->ccb_xa.fis));
	ccb->ccb_xa.at = at;
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
	if (ccb->ccb_xa.at) {
		cmd_slot->flags |= htole16(ccb->ccb_xa.at->at_target <<
					   AHCI_CMD_LIST_FLAG_PMP_SHIFT);
	}

	if (xa->flags & ATA_F_WRITE)
		cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_W);

	if (xa->flags & ATA_F_PACKET)
		cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_A);

	if (ahci_load_prdt(ccb) != 0)
		goto failcmd;

	xa->state = ATA_S_PENDING;

	if (xa->flags & ATA_F_POLL)
		return (ahci_poll(ccb, xa->timeout, ahci_ata_cmd_timeout));

	crit_enter();
	KKASSERT((xa->flags & ATA_F_TIMEOUT_EXPIRED) == 0);
	xa->flags |= ATA_F_TIMEOUT_DESIRED;
	ahci_start(ccb);
	crit_exit();
	return (xa->state);

failcmd:
	crit_enter();
	xa->state = ATA_S_ERROR;
	xa->complete(xa);
	crit_exit();
	return (ATA_S_ERROR);
}

void
ahci_ata_cmd_done(struct ahci_ccb *ccb)
{
	struct ata_xfer	*xa = &ccb->ccb_xa;
	int serial;

	/*
	 * NOTE: Callout does not lock port and may race us modifying
	 *	 the flags, so make sure its stopped.
	 *
	 *	 A callout race can clean up the ccb.  A change in the
	 *	 serial number should catch this condition.
	 */
	if (xa->flags & ATA_F_TIMEOUT_RUNNING) {
		serial = ccb->ccb_xa.serial;
		callout_stop_sync(&ccb->ccb_timeout);
		if (serial != ccb->ccb_xa.serial) {
			kprintf("%s: Warning: timeout race ccb %p\n",
				PORTNAME(ccb->ccb_port), ccb);
			return;
		}
		xa->flags &= ~ATA_F_TIMEOUT_RUNNING;
	}
	xa->flags &= ~(ATA_F_TIMEOUT_DESIRED | ATA_F_TIMEOUT_EXPIRED);
	ccb->ccb_port->ap_expired &= ~(1 << ccb->ccb_slot);

	KKASSERT(xa->state != ATA_S_ONCHIP && xa->state != ATA_S_PUT);
	ahci_unload_prdt(ccb);

	if (xa->state != ATA_S_TIMEOUT)
		xa->complete(xa);
}

/*
 * Timeout from callout, MPSAFE - nothing can mess with the CCB's flags
 * while the callout is runing.
 *
 * We can't safely get the port lock here or delay, we could block
 * the callout thread.
 */
static void
ahci_ata_cmd_timeout_unserialized(void *arg)
{
	struct ahci_ccb		*ccb = arg;
	struct ahci_port	*ap = ccb->ccb_port;

	KKASSERT(ccb->ccb_xa.flags & ATA_F_TIMEOUT_RUNNING);
	ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_RUNNING;
	ccb->ccb_xa.flags |= ATA_F_TIMEOUT_EXPIRED;
	ahci_os_signal_port_thread(ap, AP_SIGF_TIMEOUT);
}

/*
 * Timeout code, typically called when the port command processor is running.
 *
 * We have to be very very careful here.  We cannot stop the port unless
 * CR is already clear or the only active commands remaining are timed-out
 * ones.  Otherwise stopping the port will race the command processor and
 * we can lose events.  While we can theoretically just restart everything
 * that could result in a double-issue which will not work for ATAPI commands.
 */
void
ahci_ata_cmd_timeout(struct ahci_ccb *ccb)
{
	struct ata_xfer		*xa = &ccb->ccb_xa;
	struct ahci_port	*ap = ccb->ccb_port;
	struct ata_port		*at;
	u_int32_t		ci_saved;
	u_int32_t		mask;
	int			slot;

	at = ccb->ccb_xa.at;

	kprintf("%s: CMD TIMEOUT state=%d slot=%d\n"
		"\tglb-status 0x%08x\n"
		"\tcmd-reg 0x%b\n"
		"\tport_status 0x%b\n"
		"\tsactive=%08x active=%08x expired=%08x\n"
		"\t   sact=%08x     ci=%08x\n"
		"\t    STS=%b\n",
		ATANAME(ap, at),
		ccb->ccb_xa.state, ccb->ccb_slot,
		ahci_read(ap->ap_sc, AHCI_REG_IS),
		ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD,
		ahci_pread(ap, AHCI_PREG_IS), AHCI_PFMT_IS,
		ap->ap_sactive, ap->ap_active, ap->ap_expired,
		ahci_pread(ap, AHCI_PREG_SACT),
		ahci_pread(ap, AHCI_PREG_CI),
		ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS
	);


	/*
	 * NOTE: Timeout will not be running if the command was polled.
	 *	 If we got here at least one of these flags should be set.
	 */
	KKASSERT(xa->flags & (ATA_F_POLL | ATA_F_TIMEOUT_DESIRED |
			      ATA_F_TIMEOUT_RUNNING));
	xa->flags &= ~(ATA_F_TIMEOUT_RUNNING | ATA_F_TIMEOUT_EXPIRED);

	if (ccb->ccb_xa.state == ATA_S_PENDING) {
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_done(ccb);
		xa->complete(xa);
		ahci_issue_pending_commands(ap, NULL);
		return;
	}
	if (ccb->ccb_xa.state != ATA_S_ONCHIP) {
		kprintf("%s: Unexpected state during timeout: %d\n",
			ATANAME(ap, at), ccb->ccb_xa.state);
		return;
	}

	/*
	 * Ok, we can only get this command off the chip if CR is inactive
	 * or if the only commands running on the chip are all expired.
	 * Otherwise we have to wait until the port is in a safe state.
	 *
	 * Do not set state here, it will cause polls to return when the
	 * ccb is not yet off the chip.
	 */
	ap->ap_expired |= 1 << ccb->ccb_slot;

	if ((ahci_pread(ap, AHCI_PREG_CMD) & AHCI_PREG_CMD_CR) &&
	    (ap->ap_active | ap->ap_sactive) != ap->ap_expired) {
		/*
		 * If using FBSS or NCQ we can't safely stop the port
		 * right now.
		 */
		kprintf("%s: Deferred timeout until its safe, slot %d\n",
			ATANAME(ap, at), ccb->ccb_slot);
		return;
	}

	/*
	 * We can safely stop the port and process all expired ccb's,
	 * which will include our current ccb.
	 */
	ci_saved = (ap->ap_sactive) ? ahci_pread(ap, AHCI_PREG_SACT) :
				      ahci_pread(ap, AHCI_PREG_CI);
	ahci_port_stop(ap, 0);

	while (ap->ap_expired) {
		slot = ffs(ap->ap_expired) - 1;
		ap->ap_expired &= ~(1 << slot);
		ci_saved &= ~(1 << slot);
		ccb = &ap->ap_ccbs[slot];
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		if (ccb->ccb_xa.flags & ATA_F_NCQ) {
			KKASSERT(ap->ap_sactive & (1 << slot));
			ap->ap_sactive &= ~(1 << slot);
		} else {
			KKASSERT(ap->ap_active & (1 << slot));
			ap->ap_active &= ~(1 << slot);
			--ap->ap_active_cnt;
		}
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}
	/* ccb invalid now */

	/*
	 * We can safely CLO the port to clear any BSY/DRQ, a case which
	 * can occur with port multipliers.  This will unbrick the port
	 * and allow commands to other targets behind the PM continue.
	 * (FBSS).
	 *
	 * Finally, once the port has been restarted we can issue any
	 * previously saved pending commands, and run the port interrupt
	 * code to handle any completions which may have occured when
	 * we saved CI.
	 */
	if (ahci_pread(ap, AHCI_PREG_TFD) &
		   (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: Warning, issuing CLO after timeout\n",
			ATANAME(ap, at));
		ahci_port_clo(ap);
	}
	ahci_port_start(ap);

	/*
	 * We absolutely must make sure the chipset cleared activity on
	 * all slots.  This sometimes might not happen due to races with
	 * a chipset interrupt which stops the port before we can manage
	 * to.  For some reason some chipsets don't clear the active
	 * commands when we turn off CMD_ST after the chip has stopped
	 * operations itself.
	 */
	if (ahci_pactive(ap) != 0) {
		ahci_port_stop(ap, 0);
		ahci_port_start(ap);
		if ((mask = ahci_pactive(ap)) != 0) {
			kprintf("%s: quick-timeout: chipset failed "
				"to clear active cmds %08x\n",
				PORTNAME(ap), mask);
		}
	}
	ahci_issue_saved_commands(ap, ci_saved & ~ap->ap_expired);
	ahci_issue_pending_commands(ap, NULL);
	ahci_port_intr(ap, 0);
}

/*
 * Issue a previously saved set of commands
 */
void
ahci_issue_saved_commands(struct ahci_port *ap, u_int32_t ci_saved)
{
	if (ci_saved) {
		KKASSERT(!((ap->ap_active & ci_saved) &&
			   (ap->ap_sactive & ci_saved)));
		KKASSERT((ci_saved & ap->ap_expired) == 0);
		if (ap->ap_sactive & ci_saved)
			ahci_pwrite(ap, AHCI_PREG_SACT, ci_saved);
		ahci_pwrite(ap, AHCI_PREG_CI, ci_saved);
	}
}

/*
 * Used by the softreset, pmprobe, and read_ncq_error only, in very
 * specialized, controlled circumstances.
 *
 * Only one command may be pending.
 */
void
ahci_quick_timeout(struct ahci_ccb *ccb)
{
	struct ahci_port *ap = ccb->ccb_port;
	u_int32_t mask;

	switch (ccb->ccb_xa.state) {
	case ATA_S_PENDING:
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		break;
	case ATA_S_ONCHIP:
		/*
		 * We have to clear the command on-chip.
		 */
		KKASSERT(ap->ap_active == (1 << ccb->ccb_slot) &&
			 ap->ap_sactive == 0);
		ahci_port_stop(ap, 0);
		ahci_port_start(ap);
		if (ahci_pactive(ap) != 0) {
			ahci_port_stop(ap, 0);
			ahci_port_start(ap);
			if ((mask = ahci_pactive(ap)) != 0) {
				kprintf("%s: quick-timeout: chipset failed "
					"to clear active cmds %08x\n",
					PORTNAME(ap), mask);
			}
		}

		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ap->ap_active &= ~(1 << ccb->ccb_slot);
		KKASSERT(ap->ap_active_cnt > 0);
		--ap->ap_active_cnt;
		break;
	default:
		panic("%s: ahci_quick_timeout: ccb in bad state %d",
		      ATANAME(ap, ccb->ccb_xa.at), ccb->ccb_xa.state);
	}
}

static void
ahci_dummy_done(struct ata_xfer *xa)
{
}

static void
ahci_empty_done(struct ahci_ccb *ccb)
{
}

int
ahci_set_feature(struct ahci_port *ap, struct ata_port *atx,
		 int feature, int enable)
{
	struct ata_port *at;
	struct ata_xfer *xa;
	int error;

	at = atx ? atx : ap->ap_ata[0];

	xa = ahci_ata_get_xfer(ap, atx);

	xa->fis->type = ATA_FIS_TYPE_H2D;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;
	xa->fis->command = ATA_C_SET_FEATURES;
	xa->fis->features = enable ? ATA_C_SATA_FEATURE_ENA :
	                             ATA_C_SATA_FEATURE_DIS;
	xa->fis->sector_count = feature;
	xa->fis->control = ATA_FIS_CONTROL_4BIT;

	xa->complete = ahci_dummy_done;
	xa->datalen = 0;
	xa->flags = ATA_F_POLL;
	xa->timeout = 1000;

	if (ahci_ata_cmd(xa) == ATA_S_COMPLETE)
		error = 0;
	else
		error = EIO;
	ahci_ata_put_xfer(xa);
	return(error);
}
