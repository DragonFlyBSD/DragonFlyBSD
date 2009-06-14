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

int	ahci_port_start(struct ahci_port *ap);
int	ahci_port_stop(struct ahci_port *ap, int stop_fis_rx);
int	ahci_port_clo(struct ahci_port *ap);
void	ahci_port_interrupt_enable(struct ahci_port *ap);

int	ahci_load_prdt(struct ahci_ccb *);
void	ahci_unload_prdt(struct ahci_ccb *);
static void ahci_load_prdt_callback(void *info, bus_dma_segment_t *segs,
				    int nsegs, int error);
void	ahci_start(struct ahci_ccb *);
int	ahci_port_softreset(struct ahci_port *ap);
int	ahci_port_pmprobe(struct ahci_port *ap);
int	ahci_port_hardreset(struct ahci_port *ap, int hard);
void	ahci_port_hardstop(struct ahci_port *ap);
void	ahci_flush_tfd(struct ahci_port *ap);

static void ahci_ata_cmd_timeout_unserialized(void *);
void	ahci_quick_timeout(struct ahci_ccb *ccb);
void	ahci_check_active_timeouts(struct ahci_port *ap);

void	ahci_beg_exclusive_access(struct ahci_port *ap, struct ata_port *at);
void	ahci_end_exclusive_access(struct ahci_port *ap, struct ata_port *at);
void	ahci_issue_pending_commands(struct ahci_port *ap, struct ahci_ccb *ccb);
void	ahci_issue_saved_commands(struct ahci_port *ap, u_int32_t mask);

int	ahci_port_read_ncq_error(struct ahci_port *, int *);

struct ahci_dmamem *ahci_dmamem_alloc(struct ahci_softc *, bus_dma_tag_t tag);
void	ahci_dmamem_free(struct ahci_softc *, struct ahci_dmamem *);
static void ahci_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error);

void	ahci_empty_done(struct ahci_ccb *ccb);
void	ahci_ata_cmd_done(struct ahci_ccb *ccb);

/* Wait for all bits in _b to be cleared */
#define ahci_pwait_clr(_ap, _r, _b) \
	ahci_pwait_eq((_ap), AHCI_PWAIT_TIMEOUT, (_r), (_b), 0)
#define ahci_pwait_clr_to(_ap, _to,  _r, _b) \
	ahci_pwait_eq((_ap), _to, (_r), (_b), 0)

/* Wait for all bits in _b to be set */
#define ahci_pwait_set(_ap, _r, _b) \
	ahci_pwait_eq((_ap), AHCI_PWAIT_TIMEOUT, (_r), (_b), (_b))
#define ahci_pwait_set_to(_ap, _to, _r, _b) \
	ahci_pwait_eq((_ap), _to, (_r), (_b), (_b))

#define AHCI_PWAIT_TIMEOUT	1000

/*
 * Initialize the global AHCI hardware.  This code does not set up any of
 * its ports.
 */
int
ahci_init(struct ahci_softc *sc)
{
	u_int32_t	cap, pi;
	int		i;
	struct ahci_port *ap;

	DPRINTF(AHCI_D_VERBOSE, " GHC 0x%b",
		ahci_read(sc, AHCI_REG_GHC), AHCI_FMT_GHC);

	/* save BIOS initialised parameters, enable staggered spin up */
	cap = ahci_read(sc, AHCI_REG_CAP);
	cap &= AHCI_REG_CAP_SMPS;
	cap |= AHCI_REG_CAP_SSS;
	pi = ahci_read(sc, AHCI_REG_PI);

#if 1
	/*
	 * This is a hack that currently does not appear to have
	 * a significant effect, but I noticed the port registers
	 * do not appear to be completely cleared after the host
	 * controller is reset.
	 */
	ap = kmalloc(sizeof(*ap), M_DEVBUF, M_WAITOK | M_ZERO);
	ap->ap_sc = sc;
	for (i = 0; i < AHCI_MAX_PMPORTS; ++i) {
		if ((pi & (1 << i)) == 0)
			continue;
		if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    AHCI_PORT_REGION(i), AHCI_PORT_SIZE, &ap->ap_ioh) != 0) {
			device_printf(sc->sc_dev, "can't map port\n");
			return (1);
		}
		ahci_pwrite(ap, AHCI_PREG_SCTL, AHCI_PREG_SCTL_IPM_DISABLED |
						AHCI_PREG_SCTL_DET_DISABLE);
		ahci_pwrite(ap, AHCI_PREG_SERR, -1);
		ahci_pwrite(ap, AHCI_PREG_IE, 0);
		ahci_pwrite(ap, AHCI_PREG_CMD, 0);
		ahci_pwrite(ap, AHCI_PREG_IS, 0);
	}
	kfree(ap, M_DEVBUF);
#endif

	/*
	 * Unconditionally reset the controller, do not conditionalize on
	 * trying to figure it if it was previously active or not.
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
	ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_HR);
	if (ahci_wait_ne(sc, AHCI_REG_GHC,
			 AHCI_REG_GHC_HR, AHCI_REG_GHC_HR) != 0) {
		device_printf(sc->sc_dev,
			      "unable to reset controller\n");
		return (1);
	}
	ahci_os_sleep(100);

	/* enable ahci (global interrupts disabled) */
	ahci_write(sc, AHCI_REG_GHC, AHCI_REG_GHC_AE);

	/* restore parameters */
	ahci_write(sc, AHCI_REG_CAP, cap);
	ahci_write(sc, AHCI_REG_PI, pi);

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
	struct ahci_cmd_hdr	*hdr;
	struct ahci_cmd_table	*table;
	int	rc = ENOMEM;
	int	error;
	int	i;

	ap = kmalloc(sizeof(*ap), M_DEVBUF, M_WAITOK | M_ZERO);

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
	 */
	if (ap->ap_ata == NULL) {
		ap->ap_ata = kmalloc(sizeof(*ap->ap_ata) * AHCI_MAX_PMPORTS,
				     M_DEVBUF, M_INTWAIT | M_ZERO);
		for (i = 0; i < AHCI_MAX_PMPORTS; ++i) {
			at = &ap->ap_ata[i];
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

	/* Wait for ICC change to complete */
	ahci_pwait_clr(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_ICC);

	/*
	 * Start the port.  The helper thread will call ahci_port_init()
	 * so the ports can all be started in parallel.  A failure by
	 * ahci_port_init() does not deallocate the port since we still
	 * want hot-plug events.
	 */
	ahci_os_start_port(ap);
	return(0);
freeport:
	ahci_port_free(sc, port);
	return (rc);
}

/*
 * [re]initialize an idle port.  No CCBs should be active.
 *
 * If at is NULL we are initializing a directly connected port, otherwise
 * we are indirectly initializing a port multiplier port.
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
ahci_port_init(struct ahci_port *ap, struct ata_port *atx)
{
	u_int32_t data;
	int rc;

	/*
	 * Clear all notification bits
	 */
	if (atx == NULL && (ap->ap_sc->sc_cap & AHCI_REG_CAP_SSNTF))
		ahci_pwrite(ap, AHCI_PREG_SNTF, -1);

	/*
	 * Hard-reset the port.  If a device is detected but it is busy
	 * we try a second time, this time cycling the phy as well.
	 *
	 * XXX note: hard reset mode 2 (cycling the PHY) is not reliable.
	 */
	if (atx)
		atx->at_probe = ATA_PROBE_NEED_HARD_RESET;
	else
		ap->ap_probe = ATA_PROBE_NEED_HARD_RESET;

	rc = ahci_port_reset(ap, atx, 1);
#if 0
	rc = ahci_port_reset(ap, atx, 1);
	if (rc == EBUSY) {
		rc = ahci_port_reset(ap, atx, 2);
	}
#endif

	switch (rc) {
	case ENODEV:
		/*
		 * We had problems talking to the device on the port.
		 */
		if (atx) {
			ahci_pm_read(ap, atx->at_target,
				     AHCI_PMREG_SSTS, &data);
		} else {
			data = ahci_pread(ap, AHCI_PREG_SSTS);
		}

		switch(data & AHCI_PREG_SSTS_DET) {
		case AHCI_PREG_SSTS_DET_DEV_NE:
			kprintf("%s: Device not communicating\n",
				ATANAME(ap, atx));
			break;
		case AHCI_PREG_SSTS_DET_PHYOFFLINE:
			kprintf("%s: PHY offline\n",
				ATANAME(ap, atx));
			break;
		default:
			kprintf("%s: No device detected\n",
				ATANAME(ap, atx));
			break;
		}
		break;

	case EBUSY:
		/*
		 * The device on the port is still telling us its busy,
		 * which means that it is not properly handling a SATA
		 * port COMRESET.
		 *
		 * It may be possible to softreset the device using CLO
		 * and a device reset command.
		 */
		if (atx) {
			kprintf("%s: Device on port is bricked, giving up\n",
				ATANAME(ap, atx));
		} else {
			kprintf("%s: Device on port is bricked, "
				"trying softreset\n", PORTNAME(ap));

			rc = ahci_port_reset(ap, atx, 0);
			if (rc) {
				kprintf("%s: Unable unbrick device\n",
					PORTNAME(ap));
			} else {
				kprintf("%s: Successfully unbricked\n",
					PORTNAME(ap));
			}
		}
		break;

	default:
		break;
	}

	/*
	 * Command transfers can only be enabled if a device was successfully
	 * detected.
	 *
	 * Allocate or deallocate the ap_ata array here too.
	 */
	if (atx == NULL) {
		switch(ap->ap_type) {
		case ATA_PORT_T_NONE:
			ap->ap_pmcount = 0;
			break;
		case ATA_PORT_T_PM:
			/* already set */
			break;
		default:
			ap->ap_pmcount = 1;
			break;
		}
	}

	/*
	 * Start the port if we succeeded.
	 *
	 * There's nothing to start for devices behind a port multiplier.
	 */
	if (rc == 0 && atx == NULL) {
		if (ahci_port_start(ap)) {
			kprintf("%s: failed to start command DMA on port, "
			        "disabling\n", PORTNAME(ap));
			rc = ENXIO;	/* couldn't start port */
		}
	}

	/*
	 * Flush interrupts on the port. XXX
	 *
	 * Enable interrupts on the port whether a device is sitting on
	 * it or not, to handle hot-plug events.
	 */
	if (atx == NULL) {
		ahci_pwrite(ap, AHCI_PREG_IS, ahci_pread(ap, AHCI_PREG_IS));
		ahci_write(ap->ap_sc, AHCI_REG_IS, 1 << ap->ap_num);

		ahci_port_interrupt_enable(ap);
	}
	return(rc);
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
	u_int32_t data;

	data = AHCI_PREG_IE_TFEE | AHCI_PREG_IE_HBFE |
	       AHCI_PREG_IE_IFE | AHCI_PREG_IE_OFE |
	       AHCI_PREG_IE_DPE | AHCI_PREG_IE_UFE |
	       AHCI_PREG_IE_PCE | AHCI_PREG_IE_PRCE |
	       AHCI_PREG_IE_DHRE;
	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_SSNTF)
		data |= AHCI_PREG_IE_SDBE;
#ifdef AHCI_COALESCE
	if (sc->sc_ccc_ports & (1 << port)
		data &= ~(AHCI_PREG_IE_SDBE | AHCI_PREG_IE_DHRE);
#endif
	ahci_pwrite(ap, AHCI_PREG_IE, data);
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
	if (ap->ap_type == ATA_PORT_T_NONE) {
		if (initial == 0 && ap->ap_probe <= ATA_PROBE_NEED_HARD_RESET) {
			kprintf("%s: Waiting 10 seconds on insertion\n",
				PORTNAME(ap));
			ahci_os_sleep(10000);
			initial = 1;
		}
		if (ap->ap_probe == ATA_PROBE_NEED_INIT)
			ahci_port_init(ap, NULL);
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
		if (ahci_pm_read(ap, 15, AHCI_PMREG_EINFO, &data)) {
			kprintf("%s: PM unable to read hot-plug bitmap\n",
				PORTNAME(ap));
			break;
		}
		data &= (1 << ap->ap_pmcount) - 1;

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
			at = &ap->ap_ata[target];

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
					ahci_port_init(ap, at);
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
		}
	}
}


/*
 * De-initialize and detach a port.
 */
void
ahci_port_free(struct ahci_softc *sc, u_int port)
{
	struct ahci_port		*ap = sc->sc_ports[port];
	struct ahci_ccb			*ccb;

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
		kfree(ap->ap_ata, M_DEVBUF);
		ap->ap_ata = NULL;
	}

	/* bus_space(9) says we dont free the subregions handle */

	kfree(ap, M_DEVBUF);
	sc->sc_ports[port] = NULL;
}

/*
 * Start high-level command processing on the port
 */
int
ahci_port_start(struct ahci_port *ap)
{
	u_int32_t	r, oldr, s, olds, is, oldis, tfd, oldtfd;

	/*
	 * FRE must be turned on before ST.  Wait for FR to go active
	 * before turning on ST.  The spec doesn't seem to think this
	 * is necessary but waiting here avoids an on-off race in the
	 * ahci_port_stop() code.
	 */
	 /* XXX REMOVE ME */
	olds = ahci_pread(ap, AHCI_PREG_SERR);
	oldis= ahci_pread(ap, AHCI_PREG_IS);
	oldtfd = ahci_pread(ap, AHCI_PREG_TFD);
	oldr = r = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
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
	}

	/*
	 * Turn on ST, wait for CR to come up.
	 */
	r |= AHCI_PREG_CMD_ST;
	ahci_pwrite(ap, AHCI_PREG_CMD, r);
	if (ahci_pwait_set(ap, AHCI_PREG_CMD, AHCI_PREG_CMD_CR)) {
		s = ahci_pread(ap, AHCI_PREG_SERR);
		is = ahci_pread(ap, AHCI_PREG_IS);
		tfd = ahci_pread(ap, AHCI_PREG_TFD);
		kprintf("%s: Cannot start command DMA\n"
			"OCMD=%b OSERR=%b\n"
			"NCMP=%b NSERR=%b\n"
			"OLDIS=%b\nNEWIS=%b\n"
			"OLDTFD=%b\nNEWTFD=%b\n",
			PORTNAME(ap),
			oldr, AHCI_PFMT_CMD, olds, AHCI_PFMT_SERR,
			r, AHCI_PFMT_CMD, s, AHCI_PFMT_SERR,
			oldis, AHCI_PFMT_IS, is, AHCI_PFMT_IS,
			oldtfd, AHCI_PFMT_TFD_STS, tfd, AHCI_PFMT_TFD_STS);
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
	u_int32_t			r;

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

/*
 * Reset a port.
 *
 * If hard is 0 perform a softreset of the port.
 * If hard is 1 perform a hard reset of the port.
 * If hard is 2 perform a hard reset of the port and cycle the phy.
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

	kprintf("%s: START SOFTRESET %b\n", PORTNAME(ap),
		ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD);

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
	ccb->ccb_xa.flags = 0;
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
	 * Give the device 3ms before sending the second FIS.
	 *
	 * It is unclear which other fields in the FIS are used.  Just zero
	 * everything.
	 */
	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[15] = ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */

	ccb->ccb_xa.state = ATA_S_PENDING;
	ccb->ccb_xa.flags = 0;
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
	ahci_os_sleep(10);

	/*
	 * If the softreset is trying to clear a BSY condition after a
	 * normal portreset we assign the port type.
	 *
	 * If the softreset is being run first as part of the ccb error
	 * processing code then report if the device signature changed
	 * unexpectedly.
	 */
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
		ahci_port_start(ap);
	}
	ap->ap_flags &= ~AP_F_IN_RESET;
	crit_exit();

	kprintf("%s: END SOFTRESET\n", PORTNAME(ap));

	return (error);
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
	u_int32_t cmd, r;
	int	error;
	int	loop;
	int	type;

	DPRINTF(AHCI_D_VERBOSE, "%s: port reset\n", PORTNAME(ap));

	ap->ap_flags |= AP_F_IN_RESET;

	/*
	 * Idle the port,
	 */
	ahci_port_stop(ap, 0);
	ap->ap_state = AP_S_NORMAL;
	error = 0;

	/*
	 * The port may have been quiescent with its SUD bit cleared, so
	 * set the SUD (spin up device).
	 */
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	cmd |= AHCI_PREG_CMD_SUD;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);

	/*
	 * Perform device detection.  Cycle the PHY off, wait 10ms.
	 * This simulates the SATA cable being physically unplugged.
	 *
	 * NOTE: hard reset mode 2 (cycling the PHY) is not reliable
	 *       and not currently used.
	 */
	ap->ap_type = ATA_PORT_T_NONE;

	r = AHCI_PREG_SCTL_IPM_DISABLED;
	if (hard == 2)
		r |= AHCI_PREG_SCTL_DET_DISABLE;
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	ahci_os_sleep(10);

	/*
	 * Start transmitting COMRESET.  COMRESET must be sent for at
	 * least 1ms.
	 */
	r = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_INIT;
	if (AhciForceGen1 & (1 << ap->ap_num)) {
		kprintf("%s: Force 1.5Gbits\n", PORTNAME(ap));
		r |= AHCI_PREG_SCTL_SPD_GEN1;
	} else {
		r |= AHCI_PREG_SCTL_SPD_ANY;
	}
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);

	/*
	 * Through trial and error it seems to take around 100ms
	 * for the detect logic to settle down.  If this is too
	 * short the softreset code will fail.
	 */
	ahci_os_sleep(100);

	/*
	 * Only SERR_DIAG_X needs to be cleared for TFD updates, but
	 * since we are hard-resetting the port we might as well clear
	 * the whole enchillada
	 */
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	r &= ~AHCI_PREG_SCTL_DET_INIT;
	r |= AHCI_PREG_SCTL_DET_NONE;
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);

	/*
	 * Try to determine if there is a device on the port.
	 *
	 * Give the device 3/10 second to at least be detected.
	 * If we fail clear PRCS (phy detect) since we may cycled
	 * the phy and probably caused another PRCS interrupt.
	 */
	for (loop = 30; loop; --loop) {
		r = ahci_pread(ap, AHCI_PREG_SSTS);
		if (r & AHCI_PREG_SSTS_DET)
			break;
		ahci_os_sleep(10);
	}
	if (loop == 0) {
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_PRCS);
		kprintf("%s: Port appears to be unplugged\n",
			PORTNAME(ap));
		error = ENODEV;
	}

	/*
	 * There is something on the port.  Give the device 3 seconds
	 * to fully negotiate.
	 */
	if (error == 0 &&
	    ahci_pwait_eq(ap, 3000, AHCI_PREG_SSTS,
			  AHCI_PREG_SSTS_DET, AHCI_PREG_SSTS_DET_DEV)) {
		kprintf("%s: Device may be powered down\n",
			PORTNAME(ap));
		error = ENODEV;
	}

	/*
	 * Wait for the device to become ready.
	 *
	 * This can take more then a second, give it 3 seconds.  If we
	 * succeed give the device another 3ms after that.
	 *
	 * NOTE: Port multipliers can do two things here.  First they can
	 *	 return device-ready if a device is on target 0 and also
	 *	 return the signature for that device.  If there is no
	 *	 device on target 0 then BSY/DRQ is never cleared and
	 *	 it never comes ready.
	 */
	if (error == 0 &&
	    ahci_pwait_clr_to(ap, 3000, AHCI_PREG_TFD,
			    AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		/*
		 * The device is bricked or its a port multiplier and will
		 * not unbusy until we do the pmprobe CLO softreset sequence.
		 */
		error = ahci_port_pmprobe(ap);
		if (error) {
			kprintf("%s: Device will not come ready 0x%b\n",
				PORTNAME(ap),
				ahci_pread(ap, AHCI_PREG_TFD),
				AHCI_PFMT_TFD_STS);
		} else {
			ap->ap_type = ATA_PORT_T_PM;
		}
	} else if (error == 0) {
		/*
		 * We generally will not get a port multiplier signature in
		 * this case even if this is a port multiplier, because of
		 * Intel's stupidity.  We almost certainly got target 0
		 * behind the PM, if there is a PM.
		 *
		 * Save the signature and probe for a PM.  If we do not
		 * find a PM then use the saved signature and return
		 * success.
		 */
		type = ahci_port_signature_detect(ap, NULL);
		error = ahci_port_pmprobe(ap);
		if (error) {
			ap->ap_type = type;
			error = 0;
		} else {
			ap->ap_type = ATA_PORT_T_PM;
			kprintf("%s: Port multiplier detected\n",
				PORTNAME(ap));
		}
	}

	/*
	 * hard-stop the port if we failed.  This will set ap_probe
	 * to FAILED.
	 */
	ap->ap_flags &= ~AP_F_IN_RESET;
	if (error) {
		ahci_port_hardstop(ap);
		/* ap_probe set to failed */
	} else {
		if (ap->ap_type == ATA_PORT_T_PM)
			ap->ap_probe = ATA_PROBE_GOOD;
		else
			ap->ap_probe = ATA_PROBE_NEED_SOFT_RESET;
	}
	return (error);
}

/*
 * AHCI port multiplier probe.  This routine is run by the hardreset code
 * if it gets past the device detect, whether or not BSY is found to be
 * stuck.
 *
 * We MUST use CLO to properly probe whether the port multiplier exists
 * or not.
 *
 * Return 0 on success, non-zero on failure.
 */
int
ahci_port_pmprobe(struct ahci_port *ap)
{
	struct ahci_cmd_hdr *cmd_slot;
	struct ata_port	*at;
	struct ahci_ccb	*ccb = NULL;
	u_int8_t	*fis = NULL;
	int		error = EIO;
	u_int32_t	cmd;
	int		count;
	int		i;

	/*
	 * If we don't support port multipliers don't try to detect one.
	 */
	if ((ap->ap_sc->sc_cap & AHCI_REG_CAP_SPM) == 0)
		return (ENODEV);

	count = 2;
retry:
	/*
	 * This code is only called from hardreset, which does not
	 * high level command processing.  The port should be stopped.
	 *
	 * Set PMA mode while the port is stopped.
	 *
	 * NOTE: On retry the port might be running, stopped, or failed.
	 */
	ahci_port_stop(ap, 0);
	ap->ap_state = AP_S_NORMAL;
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	if ((cmd & AHCI_PREG_CMD_PMA) == 0) {
		cmd |= AHCI_PREG_CMD_PMA;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	}

	/*
	 * Flush any errors and request CLO unconditionally, then start
	 * the port.
	 */
	ahci_flush_tfd(ap);
	ahci_port_clo(ap);
	if (ahci_port_start(ap)) {
		kprintf("%s: PMPROBE failed to start port, cannot softreset\n",
		        PORTNAME(ap));
		goto err;
	}

	/*
	 * Check whether CLO worked
	 */
	if (ahci_pwait_clr(ap, AHCI_PREG_TFD,
			       AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: PMPROBE CLO %s, need port reset\n",
			PORTNAME(ap),
			(ahci_read(ap->ap_sc, AHCI_REG_CAP) & AHCI_REG_CAP_SCLO)
			? "failed" : "unsupported");
		error = EBUSY;
		goto err;
	}

	/*
	 * Use the error CCB for all commands
	 *
	 * NOTE!  This CCB is used for both the first and second commands.
	 *	  The second command must use CCB slot 1 to properly load
	 *	  the signature.
	 */
	ccb = ahci_get_err_ccb(ap);
	cmd_slot = ccb->ccb_cmd_hdr;
	KKASSERT(ccb->ccb_slot == 1);

	/*
	 * Prep the first H2D command with SRST feature & clear busy/reset
	 * flags.
	 */

	fis = ccb->ccb_cmd_table->cfis;
	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[1] = 0x0F;			/* Target 15 */
	fis[15] = ATA_FIS_CONTROL_SRST | ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_C); /* Clear busy on OK */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_R); /* Reset */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_PMP); /* port 0xF */

	ccb->ccb_xa.state = ATA_S_PENDING;
	ccb->ccb_xa.flags = 0;

	if (ahci_poll(ccb, 1000, ahci_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: PMPROBE First FIS failed\n", PORTNAME(ap));
		if (--count) {
			ahci_put_err_ccb(ccb);
			goto retry;
		}
		goto err;
	}
	if (ahci_pwait_clr(ap, AHCI_PREG_TFD,
			       AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: PMPROBE Busy after first FIS\n", PORTNAME(ap));
	}

	/*
	 * The device may have muffed up the PHY when it reset.
	 */
	ahci_os_sleep(100);
	ahci_flush_tfd(ap);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	/* ahci_pm_phy_status(ap, 15, &cmd); */

	/*
	 * Prep second D2H command to read status and complete reset sequence
	 * AHCI 10.4.1 and "Serial ATA Revision 2.6".  I can't find the ATA
	 * Rev 2.6 and it is unclear how the second FIS should be set up
	 * from the AHCI document.
	 *
	 * Give the device 3ms before sending the second FIS.
	 *
	 * It is unclear which other fields in the FIS are used.  Just zero
	 * everything.
	 */
	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[1] = 0x0F;
	fis[15] = ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_PMP); /* port 0xF */

	ccb->ccb_xa.state = ATA_S_PENDING;
	ccb->ccb_xa.flags = 0;

	if (ahci_poll(ccb, 5000, ahci_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: PMPROBE Second FIS failed\n", PORTNAME(ap));
		if (--count) {
			ahci_put_err_ccb(ccb);
			goto retry;
		}
		goto err;
	}

	/*
	 * What? We succeeded?  Yup, but for some reason the signature
	 * is still latched from the original detect (that saw target 0
	 * behind the PM), and I don't know how to clear the condition
	 * other then by retrying the whole reset sequence.
	 */
	if (--count) {
		fis[15] = 0;
		ahci_put_err_ccb(ccb);
		goto retry;
	}

	/*
	 * Get the signature.  The caller sets the ap fields.
	 */
	if (ahci_port_signature_detect(ap, NULL) == ATA_PORT_T_PM) {
		ap->ap_ata[15].at_probe = ATA_PROBE_GOOD;
		error = 0;
	} else {
		error = EBUSY;
	}

	/*
	 * Fall through / clean up the CCB and perform error processing.
	 */
err:
	if (ccb != NULL)
		ahci_put_err_ccb(ccb);

	if (error == 0 && ahci_pm_identify(ap)) {
		kprintf("%s: PM - cannot identify port multiplier\n",
			PORTNAME(ap));
		error = EBUSY;
	}

	/*
	 * If we probed the PM reset the state for the targets behind
	 * it so they get probed by the state machine.
	 */
	if (error == 0) {
		for (i = 0; i < AHCI_MAX_PMPORTS; ++i) {
			at = &ap->ap_ata[i];
			at->at_probe = ATA_PROBE_NEED_INIT;
			at->at_features |= ATA_PORT_F_RESCAN;
		}
	}

	/*
	 * If we failed turn off PMA, otherwise identify the port multiplier.
	 * CAM will iterate the devices.
	 */
	if (error) {
		ahci_port_stop(ap, 0);
		cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
		cmd &= ~AHCI_PREG_CMD_PMA;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	}
	ahci_port_stop(ap, 0);

	return(error);
}

/*
 * Hard-stop on hot-swap device removal.  See 10.10.1
 *
 * Place the port in a mode that will allow it to detect hot-swap insertions.
 * This is a bit imprecise because just setting-up SCTL to DET_INIT doesn't
 * seem to do the job.
 */
void
ahci_port_hardstop(struct ahci_port *ap)
{
	struct ata_port *at;
	u_int32_t r;
	u_int32_t cmd;
	int i;

	/*
	 * Stop the port.  We can't modify things like SUD if the port
	 * is running.
	 */
	ap->ap_state = AP_S_FATAL_ERROR;
	ap->ap_probe = ATA_PROBE_FAILED;
	ap->ap_type = ATA_PORT_T_NONE;
	ahci_port_stop(ap, 0);
	cmd = ahci_pread(ap, AHCI_PREG_CMD);

	/*
	 * Clean up AT sub-ports on SATA port.
	 */
	for (i = 0; ap->ap_ata && i < AHCI_MAX_PMPORTS; ++i) {
		at = &ap->ap_ata[i];
		at->at_type = ATA_PORT_T_NONE;
		at->at_probe = ATA_PROBE_FAILED;
	}

	/*
	 * Turn off port-multiplier control bit
	 */
	if (cmd & AHCI_PREG_CMD_PMA) {
		cmd &= ~AHCI_PREG_CMD_PMA;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
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
	 * 10.10.3 DET must be set to 0 before setting SUD to 0.
	 * 10.10.1 place us in the Listen state.
	 *
	 * Deactivating SUD only applies if the controller supports SUD.
	 */
	ahci_pwrite(ap, AHCI_PREG_SCTL, AHCI_PREG_SCTL_IPM_DISABLED);
	ahci_os_sleep(1);
	if (cmd & AHCI_PREG_CMD_SUD) {
		cmd &= ~AHCI_PREG_CMD_SUD;
		ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	}
	ahci_os_sleep(1);

	/*
	 * Transition su to the spin-up state.  HVA shall send COMRESET and
	 * begin initialization sequence (whatever that means).
	 *
	 * This only applies if the controller supports SUD.
	 */
	cmd |= AHCI_PREG_CMD_SUD;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	ahci_os_sleep(1);

	/*
	 * Transition us to the Reset state.  Theoretically we send a
	 * continuous stream of COMRESETs in this state.
	 */
	r = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_INIT;
	if (AhciForceGen1 & (1 << ap->ap_num)) {
		kprintf("%s: Force 1.5Gbits\n", PORTNAME(ap));
		r |= AHCI_PREG_SCTL_SPD_GEN1;
	} else {
		r |= AHCI_PREG_SCTL_SPD_ANY;
	}
	ahci_pwrite(ap, AHCI_PREG_SCTL, r);
	ahci_os_sleep(1);

	/*
	 * Flush SERR_DIAG_X so the TFD can update.
	 */
	ahci_flush_tfd(ap);

	/*
	 * Leave us in COMRESET (both SUD and INIT active), the HBA should
	 * hopefully send us a DIAG_X-related interrupt if it receives
	 * a COMINIT, and if not that then at least a Phy transition
	 * interrupt.
	 *
	 * If we transition INIT from 1->0 to begin the initalization
	 * sequence it is unclear if that sequence will remain active
	 * until the next device insertion.
	 *
	 * If we go back to the listen state it is unclear if the
	 * device will actually send us a COMINIT, since we aren't
	 * sending any COMRESET's
	 */
	/* NOP */
}

/*
 * Multiple events may have built up in the TFD.  The spec is not very
 * clear on this but it does seem to serialize events so clearing DIAG_X
 * just once might not do the job during a reset sequence.
 *
 * XXX this probably isn't right.
 */
void
ahci_flush_tfd(struct ahci_port *ap)
{
	u_int32_t r;

	r = ahci_pread(ap, AHCI_PREG_SERR);
	while (r & AHCI_PREG_SERR_DIAG_X) {
		ahci_pwrite(ap, AHCI_PREG_SERR, AHCI_PREG_SERR_DIAG_X);
		ahci_os_sleep(1);
		r = ahci_pread(ap, AHCI_PREG_SERR);
	}
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

	kprintf("%s: Poll timeout slot %d CMD: %b TFD: 0x%b SERR: %b\n",
		ATANAME(ap, ccb->ccb_xa.at), ccb->ccb_slot,
		ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD,
		ahci_pread(ap, AHCI_PREG_TFD), AHCI_PFMT_TFD_STS,
		ahci_pread(ap, AHCI_PREG_SERR), AHCI_PFMT_SERR);

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
	 */
	if ((ccb = TAILQ_FIRST(&ap->ap_ccb_pending)) == NULL)
		return;

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
			ahci_start_timeout(ccb);
			mask |= 1 << ccb->ccb_slot;
			ccb->ccb_xa.state = ATA_S_ONCHIP;
			ccb = TAILQ_FIRST(&ap->ap_ccb_pending);
		} while (ccb && (ccb->ccb_xa.flags & ATA_F_NCQ));

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
			ahci_start_timeout(ccb);
			ap->ap_active |= 1 << ccb->ccb_slot;
			ap->ap_active_cnt++;
			ccb->ccb_xa.state = ATA_S_ONCHIP;
			ahci_pwrite(ap, AHCI_PREG_CI, 1 << ccb->ccb_slot);
			ccb = TAILQ_FIRST(&ap->ap_ccb_pending);
		}
	}
}

void
ahci_intr(void *arg)
{
	struct ahci_softc	*sc = arg;
	struct ahci_port	*ap;
	u_int32_t		is, ack = 0;
	int			port;

	/*
	 * Check if the master enable is up, and whether any interrupts are
	 * pending.
	 */
	if ((sc->sc_flags & AHCI_F_INT_GOOD) == 0)
		return;
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

	/*
	 * Process interrupts for each port in a non-blocking fashion.
	 */
	while (is) {
		port = ffs(is) - 1;
		ap = sc->sc_ports[port];
		if (ap) {
			if (ahci_os_lock_port_nb(ap) == 0) {
				ahci_port_intr(ap, 0);
				ahci_os_unlock_port(ap);
			} else {
				ahci_pwrite(ap, AHCI_PREG_IE, 0);
				ahci_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			}
		}
		is &= ~(1 << port);
	}

	/* Finally, acknowledge global interrupt */
	ahci_write(sc, AHCI_REG_IS, ack);
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
		ahci_os_unlock_port(ap);
	} else {
		ahci_os_unlock_port(ap);
	}
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
	struct ahci_ccb		*ccb = NULL;
	struct ata_port		*ccb_at = NULL;
	volatile u_int32_t	*active;
#ifdef DIAGNOSTIC
	u_int32_t		tmp;
#endif
	const u_int32_t		blockable_mask = AHCI_PREG_IS_TFES |
						 AHCI_PREG_IS_IFS |
						 AHCI_PREG_IS_PCS |
						 AHCI_PREG_IS_PRCS |
						 AHCI_PREG_IS_HBFS |
						 AHCI_PREG_IS_OFS |
						 AHCI_PREG_IS_UFS;

	enum { NEED_NOTHING, NEED_RESTART, NEED_HOTPLUG_INSERT,
	       NEED_HOTPLUG_REMOVE } need = NEED_NOTHING;

	is = ahci_pread(ap, AHCI_PREG_IS);

	/*
	 * All basic command completions are always processed.
	 */
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
			ahci_pwrite(ap, AHCI_PREG_IE,
				    ahci_pread(ap, AHCI_PREG_IE) & ~is);
			ahci_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			return;
		}
		if (is & blockable_mask) {
			ahci_pwrite(ap, AHCI_PREG_IE,
			    ahci_pread(ap, AHCI_PREG_IE) & ~blockable_mask);
			is &= ~blockable_mask;
			ahci_os_signal_port_thread(ap, AP_SIGF_PORTINT);
		}
	}

#if 0
	kprintf("%s: INTERRUPT %b\n", PORTNAME(ap),
		is, AHCI_PFMT_IS);
#endif

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

	if (is & AHCI_PREG_IS_TFES) {
		/*
		 * Command failed (blockable).
		 *
		 * See AHCI 1.1 spec 6.2.2.1 and 6.2.2.2.
		 *
		 * This stops command processing.
		 */
		u_int32_t tfd, serr;
		int	err_slot;

		tfd = ahci_pread(ap, AHCI_PREG_TFD);
		serr = ahci_pread(ap, AHCI_PREG_SERR);

		/*
		 * If no NCQ commands are active the error slot is easily
		 * determined, otherwise we have to extract the error
		 * from the log page.
		 */
		if (ap->ap_sactive == 0) {
			err_slot = AHCI_PREG_CMD_CCS(
					ahci_pread(ap, AHCI_PREG_CMD));
			ccb = &ap->ap_ccbs[err_slot];
			ccb_at = ccb->ccb_xa.at;	/* can be NULL */

			/* Preserve received taskfile data from the RFIS. */
			memcpy(&ccb->ccb_xa.rfis, ap->ap_rfis->rfis,
			       sizeof(struct ata_fis_d2h));
		} else {
			err_slot = -1;
		}

		DPRINTF(AHCI_D_VERBOSE, "%s: errd slot %d, TFD: %b, SERR: %b\n",
			PORTNAME(ap), err_slot,
			tfd, AHCI_PFMT_TFD_STS,
			serr, AHCI_PFMT_SERR);

		/* Stopping the port clears CI and SACT */
		ahci_port_stop(ap, 0);
		need = NEED_RESTART;

		/*
		 * Clear SERR (primarily DIAG_X) to enable capturing of the
		 * next error.
		 */
		ahci_pwrite(ap, AHCI_PREG_SERR, serr);

		/* Acknowledge the interrupts we can recover from. */
		ahci_pwrite(ap, AHCI_PREG_IS,
			    is & (AHCI_PREG_IS_TFES | AHCI_PREG_IS_IFS));
		is &= ~(AHCI_PREG_IS_TFES | AHCI_PREG_IS_IFS);

		/* If device hasn't cleared its busy status, try to idle it. */
		if (tfd & (AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
			kprintf("%s: Attempting to idle device\n",
				ATANAME(ap, ccb_at));
			if (ap->ap_flags & AP_F_IN_RESET)
				goto fatal;
			/*
			 * XXX how do we unbrick a PM target (ccb_at != NULL).
			 *
			 * For now fail the target and use CLO to clear the
			 * busy condition and make the ahci port usable for
			 * the remaining devices.
			 */
			if (ccb_at) {
				ccb_at->at_probe = ATA_PROBE_FAILED;
				ahci_port_clo(ap);
			} else if (ahci_port_reset(ap, ccb_at, 0)) {
				kprintf("%s: Unable to idle device, port "
					"bricked on us\n",
					PORTNAME(ap));
				goto fatal;
			}

			/* Had to reset device, can't gather extended info. */
		} else if (ap->ap_sactive) {
			/*
			 * Recover the NCQ error from log page 10h.
			 *
			 * XXX NCQ currently not supported with port
			 *     multiplier.
			 */
			ahci_port_read_ncq_error(ap, &err_slot);
			kprintf("recover from NCQ error err_slot %d\n",
				err_slot);
			if (err_slot < 0)
				goto failall;

			DPRINTF(AHCI_D_VERBOSE, "%s: NCQ errored slot %d\n",
				PORTNAME(ap), err_slot);

			ccb = &ap->ap_ccbs[err_slot];
		} else {
			/*
			 * Non-NCQ error.  We could gather extended info from
			 * the log but for now just fall through.
			 */
			/* */
		}

		/*
		 * If we couldn't determine the errored slot, reset the port
		 * and fail all the active slots.
		 */
		if (err_slot == -1) {
			if (ap->ap_flags & AP_F_IN_RESET)
				goto fatal;
			/*
			 * XXX how do we unbrick a PM target (ccb_at != NULL).
			 *
			 * For now fail the target and use CLO to clear the
			 * busy condition and make the ahci port usable for
			 * the remaining devices.
			 */
			if (ccb_at) {
				ccb_at->at_probe = ATA_PROBE_FAILED;
				ahci_port_clo(ap);
			} else if (ahci_port_reset(ap, ccb_at, 0)) {
				kprintf("%s: Unable to idle device after "
					"NCQ error, port bricked on us\n",
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
	} else if (is & AHCI_PREG_IS_DHRS) {
		/*
		 * Command posted D2H register FIS to the rfis (non-blocking).
		 *
		 * Command posted D2H register FIS to the rfis.  This
		 * does NOT stop command processing and it is unclear
		 * how we are supposed to deal with it other then using
		 * only a queue of 1.
		 *
		 * We must copy the port rfis to the ccb and restart
		 * command processing.  ahci_pm_read() does not function
		 * without this support.
		 */
		int	err_slot;

		if (ap->ap_sactive == 0) {
			err_slot = AHCI_PREG_CMD_CCS(
					ahci_pread(ap, AHCI_PREG_CMD));
			ccb = &ap->ap_ccbs[err_slot];
			ccb_at = ccb->ccb_xa.at;	/* can be NULL */

			memcpy(&ccb->ccb_xa.rfis, ap->ap_rfis->rfis,
			       sizeof(struct ata_fis_d2h));
		} else {
			kprintf("%s: Unexpected DHRS posted while "
				"NCQ running\n", PORTNAME(ap));
			err_slot = -1;
		}
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_DHRS);
		is &= ~AHCI_PREG_IS_DHRS;
	}

	/*
	 * Device notification to us (non-blocking)
	 *
	 * NOTE!  On some parts notification bits can get set without
	 *	  generating an interrupt.  It is unclear whether this is
	 *	  a bug in the PM (sending a DTOH device setbits with 'N' set
	 *	  and 'I' not set), or a bug in the host controller.
	 *
	 *	  It only seems to occur under load.
	 */
	if (/*(is & AHCI_PREG_IS_SDBS) &&*/ (sc->sc_cap & AHCI_REG_CAP_SSNTF)) {
		u_int32_t data;
		const char *xstr;

		data = ahci_pread(ap, AHCI_PREG_SNTF);
		if (is & AHCI_PREG_IS_SDBS) {
			ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_SDBS);
			is &= ~AHCI_PREG_IS_SDBS;
			xstr = " (no SDBS!)";
		} else {
			xstr = "";
		}
		if (data) {
			ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_SDBS);

			kprintf("%s: NOTIFY %08x%s\n",
				PORTNAME(ap), data, xstr);
			ahci_pwrite(ap, AHCI_PREG_SERR, AHCI_PREG_SERR_DIAG_N);
			ahci_pwrite(ap, AHCI_PREG_SNTF, data);
			ahci_cam_changed(ap, NULL, -1);
		}
	}

	/*
	 * Spurious IFS errors (blockable).
	 *
	 * Spurious IFS errors can occur while we are doing a reset
	 * sequence through a PM.  Try to recover if we are being asked
	 * to ignore IFS errors during these periods.
	 */
	if ((is & AHCI_PREG_IS_IFS) && (ap->ap_flags & AP_F_IGNORE_IFS)) {
		u_int32_t serr = ahci_pread(ap, AHCI_PREG_SERR);
		if ((ap->ap_flags & AP_F_IFS_IGNORED) == 0) {
			kprintf("%s: Ignoring IFS (XXX) (IS: %b, SERR: %b)\n",
				PORTNAME(ap),
				is, AHCI_PFMT_IS,
				serr, AHCI_PFMT_SERR);
			ap->ap_flags |= AP_F_IFS_IGNORED;
		}
		ap->ap_flags |= AP_F_IFS_OCCURED;
		ahci_pwrite(ap, AHCI_PREG_SERR, -1);
		ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);
		is &= ~AHCI_PREG_IS_IFS;
		ahci_port_stop(ap, 0);
		ahci_port_start(ap);
		need = NEED_RESTART;
	}

	/*
	 * Port change (hot-plug) (blockable).
	 *
	 * A PCS interrupt will occur on hot-plug once communication is
	 * established.
	 *
	 * A PRCS interrupt will occur on hot-unplug (and possibly also
	 * on hot-plug).
	 *
	 * XXX We can then check the CPS (Cold Presence State) bit, if
	 * supported, to determine if a device is plugged in or not and do
	 * the right thing.
	 *
	 * WARNING:  A PCS interrupt is cleared by clearing DIAG_X, and
	 *	     can also occur if an unsolicited COMINIT is received.
	 *	     If this occurs command processing is automatically
	 *	     stopped (CR goes inactive) and the port must be stopped
	 *	     and restarted.
	 */
	if (is & (AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS)) {
		ahci_pwrite(ap, AHCI_PREG_IS,
			    is & (AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS));
		is &= ~(AHCI_PREG_IS_PCS | AHCI_PREG_IS_PRCS);
		ahci_pwrite(ap, AHCI_PREG_SERR,
			(AHCI_PREG_SERR_DIAG_N | AHCI_PREG_SERR_DIAG_X));
		ahci_port_stop(ap, 0);
		switch (ahci_pread(ap, AHCI_PREG_SSTS) & AHCI_PREG_SSTS_DET) {
		case AHCI_PREG_SSTS_DET_DEV:
			if (ap->ap_type == ATA_PORT_T_NONE) {
				need = NEED_HOTPLUG_INSERT;
				goto fatal;
			}
			need = NEED_RESTART;
			break;
		default:
			if (ap->ap_type != ATA_PORT_T_NONE) {
				need = NEED_HOTPLUG_REMOVE;
				goto fatal;
			}
			need = NEED_RESTART;
			break;
		}
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
		/* XXX try recovery first */
		goto fatal;
	}

	/*
	 * Fail all outstanding commands if we know the port won't recover.
	 *
	 * We may have a ccb_at if the failed command is known and was
	 * being sent to a device over a port multiplier (PM).  In this
	 * case if the port itself has not completely failed we fail just
	 * the commands related to that target.
	 */
	if (ap->ap_state == AP_S_FATAL_ERROR) {
fatal:
		ap->ap_state = AP_S_FATAL_ERROR;
failall:

		/* Stopping the port clears CI/SACT */
		ahci_port_stop(ap, 0);

		/*
		 * Error all the active slots.  If running across a PM
		 * try to error out just the slots related to the target.
		 */
		ci_masked = ci_saved & *active;
		while (ci_masked) {
			slot = ffs(ci_masked) - 1;
			ccb = &ap->ap_ccbs[slot];
			if (ccb_at == ccb->ccb_xa.at ||
			    ap->ap_state == AP_S_FATAL_ERROR) {
				ci_masked &= ~(1 << slot);
				ccb->ccb_xa.state = ATA_S_ERROR;
			}
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
			if (need == NEED_RESTART)
				need = NEED_NOTHING;
			ahci_pwrite(ap, AHCI_PREG_IS,
				    AHCI_PREG_IS_TFES | AHCI_PREG_IS_HBFS |
				    AHCI_PREG_IS_IFS | AHCI_PREG_IS_OFS |
				    AHCI_PREG_IS_UFS);
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
			ccb->ccb_xa.state = ATA_S_TIMEOUT;
			ccb->ccb_done(ccb);
			ccb->ccb_xa.complete(&ccb->ccb_xa);
		} else {
			if (ccb->ccb_xa.state == ATA_S_ONCHIP)
				ccb->ccb_xa.state = ATA_S_COMPLETE;
			ccb->ccb_done(ccb);
		}
		ahci_issue_pending_commands(ap, NULL);
	}

	/*
	 * Cleanup.  Will not be set if non-blocking.
	 */
	switch(need) {
	case NEED_RESTART:
		/*
		 * A recoverable error occured and we can restart outstanding
		 * commands on the port.
		 */
		ahci_port_start(ap);

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
			ahci_issue_saved_commands(ap, ci_saved);
		}
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
		KKASSERT(ccb->ccb_xa.state == ATA_S_PUT);
		TAILQ_REMOVE(&ap->ap_ccb_free, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_SETUP;
		ccb->ccb_xa.at = NULL;
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
	KKASSERT((ap->ap_flags & AP_F_ERR_CCB_RESERVED) == 0);
	ap->ap_flags |= AP_F_ERR_CCB_RESERVED;

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

#ifdef DIAGNOSTIC
	KKASSERT(ap->ap_err_busy);
#endif
	KKASSERT((ap->ap_flags & AP_F_ERR_CCB_RESERVED) != 0);

	/*
	 * No commands may be active on the chip
	 */
	sact = ahci_pread(ap, AHCI_PREG_SACT);
	if (sact) {
		panic("ahci_port_err_ccb(%d) but SACT %08x != 0\n",
		      ccb->ccb_slot, sact);
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

#ifdef DIAGNOSTIC
	ap->ap_err_busy = 0;
#endif
	ap->ap_flags &= ~AP_F_ERR_CCB_RESERVED;
}

/*
 * Read log page to get NCQ error.
 *
 * NOTE: NCQ not currently supported on port multipliers. XXX
 */
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
	ahci_port_start(ap);

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
	if (ahci_poll(ccb, 1000, ahci_quick_timeout) != 0)
		goto err;

	rc = 0;
err:
	/* Abort our command, if it failed, by stopping command DMA. */
	if (rc) {
		kprintf("%s: log page read failed, slot %d was still active.\n",
			PORTNAME(ap), ccb->ccb_slot);
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
#if 0
	kprintf("%s: started std command %b ccb %d ccb_at %p %d\n",
		ATANAME(ccb->ccb_port, ccb->ccb_xa.at),
		ahci_pread(ccb->ccb_port, AHCI_PREG_CMD), AHCI_PFMT_CMD,
		ccb->ccb_slot,
		ccb->ccb_xa.at,
		ccb->ccb_xa.at ? ccb->ccb_xa.at->at_target : -1);
#endif

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
	struct ata_xfer			*xa = &ccb->ccb_xa;

	/*
	 * NOTE: callout does not lock port and may race us modifying
	 * the flags, so make sure its stopped.
	 */
	if (xa->flags & ATA_F_TIMEOUT_RUNNING) {
		callout_stop(&ccb->ccb_timeout);
		xa->flags &= ~ATA_F_TIMEOUT_RUNNING;
	}
	xa->flags &= ~(ATA_F_TIMEOUT_DESIRED | ATA_F_TIMEOUT_EXPIRED);

	KKASSERT(xa->state != ATA_S_ONCHIP);
	ahci_unload_prdt(ccb);

#ifdef DIAGNOSTIC
	else if (xa->state != ATA_S_ERROR && xa->state != ATA_S_TIMEOUT)
		kprintf("%s: invalid ata_xfer state %02x in ahci_ata_cmd_done, "
			"slot %d\n",
			PORTNAME(ccb->ccb_port), xa->state, ccb->ccb_slot);
#endif
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
	int			ci_saved;
	int			slot;

	at = ccb->ccb_xa.at;

	kprintf("%s: CMD TIMEOUT state=%d slot=%d\n"
		"\tcmd-reg 0x%b\n"
		"\tsactive=%08x active=%08x expired=%08x\n"
		"\t   sact=%08x     ci=%08x\n",
		ATANAME(ap, at),
		ccb->ccb_xa.state, ccb->ccb_slot,
		ahci_pread(ap, AHCI_PREG_CMD), AHCI_PFMT_CMD,
		ap->ap_sactive, ap->ap_active, ap->ap_expired,
		ahci_pread(ap, AHCI_PREG_SACT),
		ahci_pread(ap, AHCI_PREG_CI));

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

	switch (ccb->ccb_xa.state) {
	case ATA_S_PENDING:
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		break;
	case ATA_S_ONCHIP:
		KKASSERT(ap->ap_active == (1 << ccb->ccb_slot) &&
			 ap->ap_sactive == 0);
		ahci_port_stop(ap, 0);
		ahci_port_start(ap);

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

void
ahci_empty_done(struct ahci_ccb *ccb)
{
}
