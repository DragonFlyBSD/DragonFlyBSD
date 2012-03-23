/*
 * (MPSAFE)
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
 *
 * $OpenBSD: sili.c,v 1.147 2009/02/16 21:19:07 miod Exp $
 */

#include "sili.h"

void	sili_port_interrupt_enable(struct sili_port *ap);
void	sili_port_interrupt_redisable(struct sili_port *ap);
void	sili_port_interrupt_reenable(struct sili_port *ap);

int	sili_load_prb(struct sili_ccb *);
void	sili_unload_prb(struct sili_ccb *);
static void sili_load_prb_callback(void *info, bus_dma_segment_t *segs,
				    int nsegs, int error);
void	sili_start(struct sili_ccb *);
static void	sili_port_reinit(struct sili_port *ap);
int	sili_port_softreset(struct sili_port *ap);
int	sili_port_hardreset(struct sili_port *ap);
void	sili_port_hardstop(struct sili_port *ap);
void	sili_port_listen(struct sili_port *ap);

static void sili_ata_cmd_timeout_unserialized(void *);
static int sili_core_timeout(struct sili_ccb *ccb, int really_error);
void	sili_check_active_timeouts(struct sili_port *ap);

void	sili_issue_pending_commands(struct sili_port *ap, struct sili_ccb *ccb);

void	sili_port_read_ncq_error(struct sili_port *, int);

struct sili_dmamem *sili_dmamem_alloc(struct sili_softc *, bus_dma_tag_t tag);
void	sili_dmamem_free(struct sili_softc *, struct sili_dmamem *);
static void sili_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error);

static void sili_dummy_done(struct ata_xfer *xa);
static void sili_empty_done(struct sili_ccb *ccb);
static void sili_ata_cmd_done(struct sili_ccb *ccb);

/*
 * Initialize the global SILI hardware.  This code does not set up any of
 * its ports.
 */
int
sili_init(struct sili_softc *sc)
{
	DPRINTF(SILI_D_VERBOSE, " GHC 0x%b",
		sili_read(sc, SILI_REG_GHC), SILI_FMT_GHC);

	/*
	 * Reset the entire chip.  This also resets all ports.
	 *
	 * The spec doesn't say anything about how long we have to
	 * wait, so wait 10ms.
	 */
	sili_write(sc, SILI_REG_GCTL, SILI_REG_GCTL_GRESET);
	sili_os_sleep(10);
	sili_write(sc, SILI_REG_GCTL, 0);
	sili_os_sleep(10);

	return (0);
}

/*
 * Allocate and initialize an SILI port.
 */
int
sili_port_alloc(struct sili_softc *sc, u_int port)
{
	struct sili_port	*ap;
	struct ata_port		*at;
	struct sili_prb		*prb;
	struct sili_ccb		*ccb;
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
	 */
	if (ap->ap_ata == NULL) {
		ap->ap_ata = kmalloc(sizeof(*ap->ap_ata) * SILI_MAX_PMPORTS,
				     M_DEVBUF, M_INTWAIT | M_ZERO);
		for (i = 0; i < SILI_MAX_PMPORTS; ++i) {
			at = &ap->ap_ata[i];
			at->at_sili_port = ap;
			at->at_target = i;
			at->at_probe = ATA_PROBE_NEED_INIT;
			at->at_features |= ATA_PORT_F_RESCAN;
			ksnprintf(at->at_name, sizeof(at->at_name),
				  "%s.%d", ap->ap_name, i);
		}
	}
	if (bus_space_subregion(sc->sc_piot, sc->sc_pioh,
				SILI_PORT_REGION(port), SILI_PORT_SIZE,
				&ap->ap_ioh) != 0) {
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
	lockinit(&ap->ap_ccb_lock, "silipo", 0, 0);

	/* Disable port interrupts */
	sili_pwrite(ap, SILI_PREG_INT_DISABLE, SILI_PREG_INT_MASK);

	/*
	 * Reset the port.  This is similar to a Device Reset but far
	 * more invasive.  We use Device Reset in our hardreset function.
	 * This function also does the same OOB initialization sequence
	 * that Device Reset does.
	 *
	 * NOTE: SILI_PREG_STATUS_READY will not be asserted unless and until
	 * 	 a device is connected to the port, so we can't use it to
	 *	 verify that the port exists.
	 */
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_RESET);
	if (sili_pread(ap, SILI_PREG_STATUS) & SILI_PREG_STATUS_READY) {
		device_printf(sc->sc_dev,
			      "Port %d will not go into reset\n", port);
		goto freeport;
	}
	sili_os_sleep(10);
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_RESET);

	/*
	 * Allocate the SGE Table
	 */
	ap->ap_dmamem_prbs = sili_dmamem_alloc(sc, sc->sc_tag_prbs);
	if (ap->ap_dmamem_prbs == NULL) {
		kprintf("%s: NOSGET\n", PORTNAME(ap));
		goto freeport;
	}

	/*
	 * Set up the SGE table base address
	 */
	ap->ap_prbs = (struct sili_prb *)SILI_DMA_KVA(ap->ap_dmamem_prbs);

	/*
	 * Allocate a CCB for each command slot
	 */
	ap->ap_ccbs = kmalloc(sizeof(struct sili_ccb) * sc->sc_ncmds, M_DEVBUF,
			      M_WAITOK | M_ZERO);
	if (ap->ap_ccbs == NULL) {
		device_printf(sc->sc_dev,
			      "unable to allocate command list for port %d\n",
			      port);
		goto freeport;
	}

	/*
	 * Most structures are in the port BAR.  Assign convenient
	 * pointers in the CCBs
	 */
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

		/*
		 * WARNING!!!  Access to the rfis is only allowed under very
		 *	       carefully controlled circumstances because it
		 *	       is located in the LRAM and reading from the
		 *	       LRAM has hardware issues which can blow the
		 *	       port up.  I kid you not (from Linux, and
		 *	       verified by testing here).
		 */
		callout_init(&ccb->ccb_timeout);
		ccb->ccb_slot = i;
		ccb->ccb_port = ap;
		ccb->ccb_prb = &ap->ap_prbs[i];
		ccb->ccb_prb_paddr = SILI_DMA_DVA(ap->ap_dmamem_prbs) +
				     sizeof(*ccb->ccb_prb) * i;
		ccb->ccb_xa.fis = &ccb->ccb_prb->prb_h2d;
		prb = bus_space_kva(ap->ap_sc->sc_iot, ap->ap_ioh,
				    SILI_PREG_LRAM_SLOT(i));
		ccb->ccb_prb_lram = prb;
		/*
		 * Point our rfis to host-memory instead of the LRAM PRB.
		 * It will be copied back if ATA_F_AUTOSENSE is set.  The
		 * LRAM PRB is buggy.
		 */
		/*ccb->ccb_xa.rfis = &prb->prb_d2h;*/
		ccb->ccb_xa.rfis = (void *)ccb->ccb_xa.fis;

		ccb->ccb_xa.packetcmd = prb_packet(ccb->ccb_prb);
		ccb->ccb_xa.tag = i;

		ccb->ccb_xa.state = ATA_S_COMPLETE;

		/*
		 * Reserve CCB[1] as the error CCB.  It doesn't matter
		 * which one we use for the Sili controllers.
		 */
		if (i == 1)
			ap->ap_err_ccb = ccb;
		else
			sili_put_ccb(ccb);
	}
	/*
	 * Do not call sili_port_init() here, the helper thread will
	 * call it for the parallel probe
	 */
	sili_os_start_port(ap);
	return(0);
freeport:
	sili_port_free(sc, port);
	return (rc);
}

/*
 * This is called once by the low level attach (from the helper thread)
 * to get the port state machine rolling, and typically only called again
 * on a hot-plug insertion event.
 *
 * This is called for PM attachments and hot-plug insertion events, and
 * typically not called again until after an unplug/replug sequence.
 *
 * Returns 0 if a device is successfully detected.
 */
int
sili_port_init(struct sili_port *ap)
{
	/*
	 * Do a very hard reset of the port
	 */
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_RESET);
	sili_os_sleep(10);
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_RESET);

	/*
	 * Register initialization
	 */
	sili_pwrite(ap, SILI_PREG_FIFO_CTL,
		    SILI_PREG_FIFO_CTL_ENCODE(1024, 1024));
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_32BITDMA |
					   SILI_PREG_CTL_PMA);
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_NOAUTOCC);
	if (ap->ap_sc->sc_flags & SILI_F_SSNTF)
		sili_pwrite(ap, SILI_PREG_SNTF, -1);
	ap->ap_probe = ATA_PROBE_NEED_HARD_RESET;
	ap->ap_pmcount = 0;
	sili_port_interrupt_enable(ap);
	return (0);
}

/*
 * Handle an errored port.  This routine is called when the only
 * commands left on the queue are expired, meaning we can safely
 * go through a port init to clear its state.
 *
 * We complete the expired CCBs and then restart the queue.
 */
static
void
sili_port_reinit(struct sili_port *ap)
{
	struct sili_ccb	*ccb;
	struct ata_port *at;
	int slot;
	int target;
	u_int32_t data;

	if (bootverbose || 1) {
		kprintf("%s: reiniting port after error reent=%d "
			"expired=%08x\n",
			PORTNAME(ap),
			(ap->ap_flags & AP_F_REINIT_ACTIVE),
			ap->ap_expired);
	}

	/*
	 * Clear port resume, clear bits 16:13 in the port device status
	 * register.  This is from the data sheet.
	 *
	 * Data sheet does not specify a delay but it seems prudent.
	 */
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_RESUME);
	sili_os_sleep(10);
	for (target = 0; target < SILI_MAX_PMPORTS; ++target) {
		data = sili_pread(ap, SILI_PREG_PM_STATUS(target));
		data &= ~(SILI_PREG_PM_STATUS_SERVICE |
			  SILI_PREG_PM_STATUS_LEGACY |
			  SILI_PREG_PM_STATUS_NATIVE |
			  SILI_PREG_PM_STATUS_VBSY);
		sili_pwrite(ap, SILI_PREG_PM_STATUS(target), data);
		sili_pwrite(ap, SILI_PREG_PM_QACTIVE(target), 0);
	}

	/*
	 * Issue a Port Initialize and wait for it to clear.  This flushes
	 * commands but does not reset the port.  Then wait for port ready.
	 */
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_INIT);
	if (sili_pwait_clr_to(ap, 5000, SILI_PREG_STATUS, SILI_PREG_CTL_INIT)) {
		kprintf("%s: Unable to reinit, port failed\n",
			PORTNAME(ap));
	}
	if (sili_pwait_set(ap, SILI_PREG_STATUS, SILI_PREG_STATUS_READY)) {
		kprintf("%s: Unable to reinit, port will not come ready\n",
			PORTNAME(ap));
	}

	/*
	 * If reentrant, stop here.  Otherwise the state for the original
	 * ahci_port_reinit() will get ripped out from under it.
	 */
	if (ap->ap_flags & AP_F_REINIT_ACTIVE)
		return;
	ap->ap_flags |= AP_F_REINIT_ACTIVE;

	/*
	 * Read the LOG ERROR page for targets that returned a specific
	 * D2H FIS with ERR set.
	 *
	 * Don't bother if we are already using the error CCB.
	 */
	if ((ap->ap_flags & AP_F_ERR_CCB_RESERVED) == 0) {
		for (target = 0; target < SILI_MAX_PMPORTS; ++target) {
			at = &ap->ap_ata[target];
			if (at->at_features & ATA_PORT_F_READLOG) {
				at->at_features &= ~ATA_PORT_F_READLOG;
				sili_port_read_ncq_error(ap, target);
			}
		}
	}

	/*
	 * Finally clean out the expired commands, we've probed the error
	 * status (or hopefully probed the error status).  Well, ok,
	 * we probably didn't XXX.
	 */
	while (ap->ap_expired) {
		slot = ffs(ap->ap_expired) - 1;
		ap->ap_expired &= ~(1 << slot);
		KKASSERT(ap->ap_active & (1 << slot));
		ap->ap_active &= ~(1 << slot);
		--ap->ap_active_cnt;
		ccb = &ap->ap_ccbs[slot];
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}
	ap->ap_flags &= ~AP_F_REINIT_ACTIVE;

	/*
	 * Wow.  All done.  We can get the port moving again.
	 */
	if (ap->ap_probe == ATA_PROBE_FAILED) {
		kprintf("%s: reinit failed, port is dead\n", PORTNAME(ap));
		while ((ccb = TAILQ_FIRST(&ap->ap_ccb_pending)) != NULL) {
			TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
			ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_DESIRED;
			ccb->ccb_xa.state = ATA_S_TIMEOUT;
			ccb->ccb_done(ccb);
			ccb->ccb_xa.complete(&ccb->ccb_xa);
		}
	} else {
		sili_issue_pending_commands(ap, NULL);
	}
}

/*
 * Enable or re-enable interrupts on a port.
 *
 * This routine is called from the port initialization code or from the
 * helper thread as the real interrupt may be forced to turn off certain
 * interrupt sources.
 */
void
sili_port_interrupt_enable(struct sili_port *ap)
{
	u_int32_t data;

	data =	SILI_PREG_INT_CCOMPLETE | SILI_PREG_INT_CERROR |
		SILI_PREG_INT_PHYRDYCHG | SILI_PREG_INT_DEVEXCHG |
		SILI_PREG_INT_DECODE | SILI_PREG_INT_CRC |
		SILI_PREG_INT_HANDSHK | SILI_PREG_INT_PMCHANGE;
	if (ap->ap_sc->sc_flags & SILI_F_SSNTF)
		data |= SILI_PREG_INT_SDB;
	sili_pwrite(ap, SILI_PREG_INT_ENABLE, data);
}

void
sili_port_interrupt_redisable(struct sili_port *ap)
{
	u_int32_t data;

	data = sili_read(ap->ap_sc, SILI_REG_GCTL);
	data &= SILI_REG_GINT_PORTMASK;
	data &= ~(1 << ap->ap_num);
	sili_write(ap->ap_sc, SILI_REG_GCTL, data);
}

void
sili_port_interrupt_reenable(struct sili_port *ap)
{
	u_int32_t data;

	data = sili_read(ap->ap_sc, SILI_REG_GCTL);
	data &= SILI_REG_GINT_PORTMASK;
	data |= (1 << ap->ap_num);
	sili_write(ap->ap_sc, SILI_REG_GCTL, data);
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
sili_port_state_machine(struct sili_port *ap, int initial)
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
			kprintf("%s: Waiting 7 seconds on insertion\n",
				PORTNAME(ap));
			sili_os_sleep(7000);
			initial = 1;
		}
		if (ap->ap_probe == ATA_PROBE_NEED_INIT)
			sili_port_init(ap);
		if (ap->ap_probe == ATA_PROBE_NEED_HARD_RESET)
			sili_port_reset(ap, NULL, 1);
		if (ap->ap_probe == ATA_PROBE_NEED_SOFT_RESET)
			sili_port_reset(ap, NULL, 0);
		if (ap->ap_probe == ATA_PROBE_NEED_IDENT)
			sili_cam_probe(ap, NULL);
	}
	if (ap->ap_type != ATA_PORT_T_PM) {
		if (ap->ap_probe == ATA_PROBE_FAILED) {
			sili_cam_changed(ap, NULL, 0);
		} else if (ap->ap_probe >= ATA_PROBE_NEED_IDENT) {
			sili_cam_changed(ap, NULL, 1);
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
		if (sili_pm_read(ap, 15, SATA_PMREG_EINFO, &data)) {
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
					sili_os_sleep(1000);
				sili_pm_check_good(ap, target);
				if (initial == 0 && didsleep == 0 &&
				    at->at_probe <= ATA_PROBE_NEED_HARD_RESET
				) {
					didsleep = 1;
					kprintf("%s: Waiting 10 seconds on insertion\n", PORTNAME(ap));
					sili_os_sleep(10000);
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
				if (at->at_probe == ATA_PROBE_NEED_INIT)
					sili_pm_port_init(ap, at);
				if (at->at_probe == ATA_PROBE_NEED_HARD_RESET)
					sili_port_reset(ap, at, 1);
				if (at->at_probe == ATA_PROBE_NEED_SOFT_RESET)
					sili_port_reset(ap, at, 0);
				if (at->at_probe == ATA_PROBE_NEED_IDENT)
					sili_cam_probe(ap, at);
			}

			/*
			 * Add or remove from CAM
			 */
			if (at->at_features & ATA_PORT_F_RESCAN) {
				at->at_features &= ~ATA_PORT_F_RESCAN;
				if (at->at_probe == ATA_PROBE_FAILED) {
					sili_cam_changed(ap, at, 0);
				} else if (at->at_probe >= ATA_PROBE_NEED_IDENT) {
					sili_cam_changed(ap, at, 1);
				}
			}
			data &= ~(1 << target);
		}
		if (data) {
			kprintf("%s: WARNING (PM): extra bits set in "
				"EINFO: %08x\n", PORTNAME(ap), data);
			while (target < SILI_MAX_PMPORTS) {
				sili_pm_check_good(ap, target);
				++target;
			}
		}
	}
}

/*
 * De-initialize and detach a port.
 */
void
sili_port_free(struct sili_softc *sc, u_int port)
{
	struct sili_port		*ap = sc->sc_ports[port];
	struct sili_ccb			*ccb;

	/*
	 * Ensure port is disabled and its interrupts are all flushed.
	 */
	if (ap->ap_sc) {
		sili_os_stop_port(ap);
		sili_pwrite(ap, SILI_PREG_INT_DISABLE, SILI_PREG_INT_MASK);
		sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_RESET);
		sili_write(ap->ap_sc, SILI_REG_GCTL,
			sili_read(ap->ap_sc, SILI_REG_GCTL) &
			~SILI_REG_GINT_PORTST(ap->ap_num));
	}

	if (ap->ap_ccbs) {
		while ((ccb = sili_get_ccb(ap)) != NULL) {
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

	if (ap->ap_dmamem_prbs) {
		sili_dmamem_free(sc, ap->ap_dmamem_prbs);
		ap->ap_dmamem_prbs = NULL;
	}
	if (ap->ap_ata) {
		kfree(ap->ap_ata, M_DEVBUF);
		ap->ap_ata = NULL;
	}
	if (ap->ap_err_scratch) {
		kfree(ap->ap_err_scratch, M_DEVBUF);
		ap->ap_err_scratch = NULL;
	}

	/* bus_space(9) says we dont free the subregions handle */

	kfree(ap, M_DEVBUF);
	sc->sc_ports[port] = NULL;
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
sili_port_reset(struct sili_port *ap, struct ata_port *at, int hard)
{
	int rc;

	if (hard) {
		if (at)
			rc = sili_pm_hardreset(ap, at->at_target, hard);
		else
			rc = sili_port_hardreset(ap);
	} else {
		if (at)
			rc = sili_pm_softreset(ap, at->at_target);
		else
			rc = sili_port_softreset(ap);
	}
	return(rc);
}

/*
 * SILI soft reset, Section 10.4.1
 *
 * (at) will be NULL when soft-resetting a directly-attached device, and
 * non-NULL when soft-resetting a device through a port multiplier.
 *
 * This function keeps port communications intact and attempts to generate
 * a reset to the connected device using device commands.
 */
int
sili_port_softreset(struct sili_port *ap)
{
	struct sili_ccb		*ccb = NULL;
	struct sili_prb		*prb;
	int			error;
	u_int32_t		sig;

	error = EIO;

	if (bootverbose)
		kprintf("%s: START SOFTRESET\n", PORTNAME(ap));

	crit_enter();
	ap->ap_state = AP_S_NORMAL;

	/*
	 * Prep the special soft-reset SII command.
	 */
	ccb = sili_get_err_ccb(ap);
	ccb->ccb_done = sili_empty_done;
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_AUTOSENSE | ATA_F_EXCLUSIVE;
	ccb->ccb_xa.complete = sili_dummy_done;
	ccb->ccb_xa.at = NULL;

	prb = ccb->ccb_prb;
	bzero(&prb->prb_h2d, sizeof(prb->prb_h2d));
	prb->prb_h2d.flags = 0;
	prb->prb_control = SILI_PRB_CTRL_SOFTRESET;
	prb->prb_override = 0;
	prb->prb_xfer_count = 0;

	ccb->ccb_xa.state = ATA_S_PENDING;

	/*
	 * NOTE: Must use sili_quick_timeout() because we hold the err_ccb
	 */
	if (sili_poll(ccb, 8000, sili_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: First FIS failed\n", PORTNAME(ap));
		goto err;
	}

	sig = (prb->prb_d2h.lba_high << 24) |
	      (prb->prb_d2h.lba_mid << 16) |
	      (prb->prb_d2h.lba_low << 8) |
	      (prb->prb_d2h.sector_count);
	if (bootverbose)
		kprintf("%s: SOFTRESET SIGNATURE %08x\n", PORTNAME(ap), sig);

	/*
	 * If the softreset is trying to clear a BSY condition after a
	 * normal portreset we assign the port type.
	 *
	 * If the softreset is being run first as part of the ccb error
	 * processing code then report if the device signature changed
	 * unexpectedly.
	 */
	if (ap->ap_type == ATA_PORT_T_NONE) {
		ap->ap_type = sili_port_signature(ap, NULL, sig);
	} else {
		if (sili_port_signature(ap, NULL, sig) != ap->ap_type) {
			kprintf("%s: device signature unexpectedly "
				"changed\n", PORTNAME(ap));
			error = EBUSY; /* XXX */
		}
	}
	error = 0;
err:
	if (ccb != NULL) {
		sili_put_err_ccb(ccb);
	}

	/*
	 * If we failed to softreset make the port quiescent, otherwise
	 * make sure the port's start/stop state matches what it was on
	 * entry.
	 *
	 * Don't kill the port if the softreset is on a port multiplier
	 * target, that would kill all the targets!
	 */
	if (bootverbose) {
		kprintf("%s: END SOFTRESET %d prob=%d state=%d\n",
			PORTNAME(ap), error, ap->ap_probe, ap->ap_state);
	}
	if (error) {
		sili_port_hardstop(ap);
		/* ap_probe set to failed */
	} else {
		ap->ap_probe = ATA_PROBE_NEED_IDENT;
		ap->ap_pmcount = 1;
	}
	crit_exit();

	sili_pwrite(ap, SILI_PREG_SERR, -1);
	if (bootverbose)
		kprintf("%s: END SOFTRESET\n", PORTNAME(ap));

	return (error);
}

/*
 * This function does a hard reset of the port.  Note that the device
 * connected to the port could still end-up hung.  Phy detection is
 * used to short-cut longer operations.
 */
int
sili_port_hardreset(struct sili_port *ap)
{
	u_int32_t data;
	int	error;
	int	loop;

	if (bootverbose)
		kprintf("%s: START HARDRESET\n", PORTNAME(ap));

	ap->ap_state = AP_S_NORMAL;

	/*
	 * Set SCTL up for any speed restrictions before issuing the
	 * device reset.   This may also take us out of an INIT state
	 * (if we were previously in a continuous reset state from
	 * sili_port_listen()).
	 */
	data = SILI_PREG_SCTL_SPM_NONE |
	       SILI_PREG_SCTL_IPM_NONE |
	       SILI_PREG_SCTL_SPD_NONE |
	       SILI_PREG_SCTL_DET_NONE;
	if (SiliForceGen1 & (1 << ap->ap_num)) {
		data &= ~SILI_PREG_SCTL_SPD_NONE;
		data |= SILI_PREG_SCTL_SPD_GEN1;
	}
	sili_pwrite(ap, SILI_PREG_SCTL, data);

	/*
	 * The transition from a continuous COMRESET state from
	 * sili_port_listen() back to device detect can take a
	 * few seconds.  It's quite non-deterministic.  Most of
	 * the time it takes far less.  Use a polling loop to
	 * wait.
	 */
	loop = 4000;
	while (loop > 0) {
		data = sili_pread(ap, SILI_PREG_SSTS);
		if (data & SILI_PREG_SSTS_DET)
			break;
		loop -= sili_os_softsleep();
	}
	sili_os_sleep(100);

	/*
	 * Issue Device Reset, give the phy a little time to settle down.
	 *
	 * NOTE:  Unlike Port Reset, the port ready signal will not
	 *	  go active unless a device is established to be on
	 *	  the port.
	 */
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_PMA);
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_RESUME);
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_DEVRESET);
	if (sili_pwait_clr(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_DEVRESET)) {
		kprintf("%s: hardreset failed to clear\n", PORTNAME(ap));
	}
	sili_os_sleep(20);

	/*
	 * Try to determine if there is a device on the port.
	 *
	 * Give the device 3/10 second to at least be detected.
	 */
	loop = 300;
	while (loop > 0) {
		data = sili_pread(ap, SILI_PREG_SSTS);
		if (data & SILI_PREG_SSTS_DET)
			break;
		loop -= sili_os_softsleep();
	}
	if (loop <= 0) {
		if (bootverbose) {
			kprintf("%s: Port appears to be unplugged\n",
				PORTNAME(ap));
		}
		error = ENODEV;
		goto done;
	}

	/*
	 * There is something on the port.  Give the device 3 seconds
	 * to detect.
	 */
	if (sili_pwait_eq(ap, 3000, SILI_PREG_SSTS,
			  SILI_PREG_SSTS_DET, SILI_PREG_SSTS_DET_DEV)) {
		if (bootverbose) {
			kprintf("%s: Device may be powered down\n",
				PORTNAME(ap));
		}
		error = ENODEV;
		goto pmdetect;
	}

	/*
	 * We got something that definitely looks like a device.  Give
	 * the device time to send us its first D2H FIS.
	 *
	 * This effectively waits for BSY to clear.
	 */
	if (sili_pwait_set_to(ap, 3000, SILI_PREG_STATUS,
			      SILI_PREG_STATUS_READY)) {
		error = EBUSY;
	} else {
		error = 0;
	}

pmdetect:
	/*
	 * Do the PM port probe regardless of how things turned out above.
	 *
	 * If the PM port probe fails it will return the original error
	 * from above.
	 */
	if (ap->ap_sc->sc_flags & SILI_F_SPM) {
		error = sili_pm_port_probe(ap, error);
	}

done:
	/*
	 * Finish up
	 */
	switch(error) {
	case 0:
		if (ap->ap_type == ATA_PORT_T_PM)
			ap->ap_probe = ATA_PROBE_GOOD;
		else
			ap->ap_probe = ATA_PROBE_NEED_SOFT_RESET;
		break;
	case ENODEV:
		/*
		 * No device detected.
		 */
		data = sili_pread(ap, SILI_PREG_SSTS);

		switch(data & SATA_PM_SSTS_DET) {
		case SILI_PREG_SSTS_DET_DEV_NE:
			kprintf("%s: Device not communicating\n",
				PORTNAME(ap));
			break;
		case SILI_PREG_SSTS_DET_OFFLINE:
			kprintf("%s: PHY offline\n",
				PORTNAME(ap));
			break;
		default:
			kprintf("%s: No device detected\n",
				PORTNAME(ap));
			break;
		}
		sili_port_hardstop(ap);
		break;
	default:
		/*
		 * (EBUSY)
		 */
		kprintf("%s: Device on port is bricked\n",
			PORTNAME(ap));
		sili_port_hardstop(ap);
		break;
	}
	sili_pwrite(ap, SILI_PREG_SERR, -1);

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
 */
void
sili_port_hardstop(struct sili_port *ap)
{
	struct sili_ccb *ccb;
	struct ata_port *at;
	int i;
	int slot;
	int serial;

	ap->ap_state = AP_S_FATAL_ERROR;
	ap->ap_probe = ATA_PROBE_FAILED;
	ap->ap_type = ATA_PORT_T_NONE;

	/*
	 * Clean up AT sub-ports on SATA port.
	 */
	for (i = 0; ap->ap_ata && i < SILI_MAX_PMPORTS; ++i) {
		at = &ap->ap_ata[i];
		at->at_type = ATA_PORT_T_NONE;
		at->at_probe = ATA_PROBE_FAILED;
		at->at_features &= ~ATA_PORT_F_READLOG;
	}

	/*
	 * Kill the port.  Don't bother waiting for it to transition
	 * back up.
	 */
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_RESET);
	if (sili_pread(ap, SILI_PREG_STATUS) & SILI_PREG_STATUS_READY) {
		kprintf("%s: Port will not go into reset\n",
			PORTNAME(ap));
	}
	sili_os_sleep(10);
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_RESET);

	/*
	 * Turn off port-multiplier control bit
	 */
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_PMA);

	/*
	 * Clean up the command list.
	 */
restart:
	while (ap->ap_active) {
		slot = ffs(ap->ap_active) - 1;
		ap->ap_active &= ~(1 << slot);
		ap->ap_expired &= ~(1 << slot);
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
		ccb->ccb_xa.flags &= ~(ATA_F_TIMEOUT_DESIRED |
				       ATA_F_TIMEOUT_EXPIRED);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}
	while ((ccb = TAILQ_FIRST(&ap->ap_ccb_pending)) != NULL) {
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_DESIRED;
		ccb->ccb_done(ccb);
		ccb->ccb_xa.complete(&ccb->ccb_xa);
	}
	KKASSERT(ap->ap_active_cnt == 0);

	/*
	 * Put the port into a listen mode, we want to get insertion/removal
	 * events.
	 */
	sili_port_listen(ap);
}

/*
 * Place port into a listen mode for hotplug events only.  The port has
 * already been reset and the command processor may not be ready due
 * to the lack of a device.
 */
void
sili_port_listen(struct sili_port *ap)
{
	u_int32_t data;

#if 1
	data = SILI_PREG_SCTL_SPM_NONE |
	       SILI_PREG_SCTL_IPM_NONE |
	       SILI_PREG_SCTL_SPD_NONE |
	       SILI_PREG_SCTL_DET_INIT;
	if (SiliForceGen1 & (1 << ap->ap_num)) {
		data &= ~SILI_PREG_SCTL_SPD_NONE;
		data |= SILI_PREG_SCTL_SPD_GEN1;
	}
#endif
	sili_os_sleep(20);
	sili_pwrite(ap, SILI_PREG_SERR, -1);
	sili_pwrite(ap, SILI_PREG_INT_ENABLE, SILI_PREG_INT_PHYRDYCHG |
					      SILI_PREG_INT_DEVEXCHG);
}

/*
 * Figure out what type of device is connected to the port, ATAPI or
 * DISK.
 */
int
sili_port_signature(struct sili_port *ap, struct ata_port *at, u_int32_t sig)
{
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
 *
 * NOTE: ATA_F_PIO is auto-selected by sili part.
 */
int
sili_load_prb(struct sili_ccb *ccb)
{
	struct sili_port		*ap = ccb->ccb_port;
	struct sili_softc		*sc = ap->ap_sc;
	struct ata_xfer			*xa = &ccb->ccb_xa;
	struct sili_prb			*prb = ccb->ccb_prb;
	struct sili_sge			*sge;
	bus_dmamap_t			dmap = ccb->ccb_dmamap;
	int				error;

	/*
	 * Set up the PRB.  The PRB contains 2 SGE's (1 if it is an ATAPI
	 * command).  The SGE must be set up to link to the rest of our
	 * SGE array, in blocks of four SGEs (a SGE table) starting at
	 */
	prb->prb_xfer_count = 0;
	prb->prb_control = 0;
	prb->prb_override = 0;
	sge = (ccb->ccb_xa.flags & ATA_F_PACKET) ?
		&prb->prb_sge_packet : &prb->prb_sge_normal;
	if (xa->datalen == 0) {
		sge->sge_flags = SILI_SGE_FLAGS_TRM | SILI_SGE_FLAGS_DRD;
		sge->sge_count = 0;
		return (0);
	}

	if (ccb->ccb_xa.flags & ATA_F_READ)
		prb->prb_control |= SILI_PRB_CTRL_READ;
	if (ccb->ccb_xa.flags & ATA_F_WRITE)
		prb->prb_control |= SILI_PRB_CTRL_WRITE;
	sge->sge_flags = SILI_SGE_FLAGS_LNK;
	sge->sge_count = 0;
	sge->sge_paddr = ccb->ccb_prb_paddr +
			 offsetof(struct sili_prb, prb_sge[0]);

	/*
	 * Load our sge array.
	 */
	error = bus_dmamap_load(sc->sc_tag_data, dmap,
				xa->data, xa->datalen,
				sili_load_prb_callback,
				ccb,
				((xa->flags & ATA_F_NOWAIT) ?
				    BUS_DMA_NOWAIT : BUS_DMA_WAITOK));
	if (error != 0) {
		kprintf("%s: error %d loading dmamap\n", PORTNAME(ap), error);
		return (1);
	}

	bus_dmamap_sync(sc->sc_tag_data, dmap,
			(xa->flags & ATA_F_READ) ?
			    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Callback from BUSDMA system to load the segment list.
 *
 * The scatter/gather table is loaded by the sili chip in blocks of
 * four SGE's.  If a continuance is required the last entry in each
 * block must point to the next block.
 */
static
void
sili_load_prb_callback(void *info, bus_dma_segment_t *segs, int nsegs,
			int error)
{
	struct sili_ccb *ccb = info;
	struct sili_sge *sge;
	int sgi;

	KKASSERT(nsegs <= SILI_MAX_SGET);

	sgi = 0;
	sge = &ccb->ccb_prb->prb_sge[0];
	while (nsegs) {
		if ((sgi & 3) == 3) {
			sge->sge_paddr = htole64(ccb->ccb_prb_paddr +
						 offsetof(struct sili_prb,
							prb_sge[sgi + 1]));
			sge->sge_count = 0;
			sge->sge_flags = SILI_SGE_FLAGS_LNK;
		} else {
			sge->sge_paddr = htole64(segs->ds_addr);
			sge->sge_count = htole32(segs->ds_len);
			sge->sge_flags = 0;
			--nsegs;
			++segs;
		}
		++sge;
		++sgi;
	}
	--sge;
	sge->sge_flags |= SILI_SGE_FLAGS_TRM;
}

void
sili_unload_prb(struct sili_ccb *ccb)
{
	struct sili_port		*ap = ccb->ccb_port;
	struct sili_softc		*sc = ap->ap_sc;
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
				    le32toh(ccb->ccb_prb->prb_xfer_count);
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
sili_poll(struct sili_ccb *ccb, int timeout,
	  void (*timeout_fn)(struct sili_ccb *))
{
	struct sili_port *ap = ccb->ccb_port;

	if (ccb->ccb_port->ap_state == AP_S_FATAL_ERROR) {
		ccb->ccb_xa.state = ATA_S_ERROR;
		return(ccb->ccb_xa.state);
	}

	KKASSERT((ap->ap_expired & (1 << ccb->ccb_slot)) == 0);
	sili_start(ccb);

	do {
		sili_port_intr(ap, 1);
		switch(ccb->ccb_xa.state) {
		case ATA_S_ONCHIP:
			timeout -= sili_os_softsleep();
			break;
		case ATA_S_PENDING:
			/*
			 * The packet can get stuck on the pending queue
			 * if the port refuses to come ready.  XXX
			 */
#if 0
			if (xxx AP_F_EXCLUSIVE_ACCESS)
				timeout -= sili_os_softsleep();
			else
#endif
				sili_os_softsleep();
			sili_check_active_timeouts(ap);
			break;
		default:
			return (ccb->ccb_xa.state);
		}
	} while (timeout > 0);

	/*
	 * Don't spew if this is a probe during hard reset
	 */
	if (ap->ap_probe != ATA_PROBE_NEED_HARD_RESET) {
		kprintf("%s: Poll timeout slot %d\n",
			ATANAME(ap, ccb->ccb_xa.at),
			ccb->ccb_slot);
	}

	timeout_fn(ccb);

	return(ccb->ccb_xa.state);
}

/*
 * When polling we have to check if the currently active CCB(s)
 * have timed out as the callout will be deadlocked while we
 * hold the port lock.
 */
void
sili_check_active_timeouts(struct sili_port *ap)
{
	struct sili_ccb *ccb;
	u_int32_t mask;
	int tag;

	mask = ap->ap_active;
	while (mask) {
		tag = ffs(mask) - 1;
		mask &= ~(1 << tag);
		ccb = &ap->ap_ccbs[tag];
		if (ccb->ccb_xa.flags & ATA_F_TIMEOUT_EXPIRED) {
			sili_core_timeout(ccb, 0);
		}
	}
}

static
__inline
void
sili_start_timeout(struct sili_ccb *ccb)
{
	if (ccb->ccb_xa.flags & ATA_F_TIMEOUT_DESIRED) {
		ccb->ccb_xa.flags |= ATA_F_TIMEOUT_RUNNING;
		callout_reset(&ccb->ccb_timeout,
			      (ccb->ccb_xa.timeout * hz + 999) / 1000,
			      sili_ata_cmd_timeout_unserialized, ccb);
	}
}

void
sili_start(struct sili_ccb *ccb)
{
	struct sili_port		*ap = ccb->ccb_port;
#if 0
	struct sili_softc		*sc = ap->ap_sc;
#endif

	KKASSERT(ccb->ccb_xa.state == ATA_S_PENDING);

	/*
	 * Sync our SGE table and PRB
	 */
	bus_dmamap_sync(ap->ap_dmamem_prbs->adm_tag,
			ap->ap_dmamem_prbs->adm_map,
			BUS_DMASYNC_PREWRITE);

	/*
	 * XXX dmamap for PRB XXX  BUS_DMASYNC_PREWRITE
	 */

	/*
	 * Controller will update shared memory!
	 * XXX bus_dmamap_sync ... BUS_DMASYNC_PREREAD ...
	 */
	/* Prepare RFIS area for write by controller */

	/*
	 * There's no point trying to optimize this, it only shaves a few
	 * nanoseconds so just queue the command and call our generic issue.
	 */
	sili_issue_pending_commands(ap, ccb);
}

/*
 * Wait for all commands to complete processing.  We hold the lock so no
 * new commands will be queued.
 */
void
sili_exclusive_access(struct sili_port *ap)
{
	while (ap->ap_active) {
		sili_port_intr(ap, 1);
		sili_os_softsleep();
	}
}

/*
 * If ccb is not NULL enqueue and/or issue it.
 *
 * If ccb is NULL issue whatever we can from the queue.  However, nothing
 * new is issued if the exclusive access flag is set or expired ccb's are
 * present.
 *
 * If existing commands are still active (ap_active) we can only
 * issue matching new commands.
 */
void
sili_issue_pending_commands(struct sili_port *ap, struct sili_ccb *ccb)
{
	/*
	 * Enqueue the ccb.
	 *
	 * If just running the queue and in exclusive access mode we
	 * just return.  Also in this case if there are any expired ccb's
	 * we want to clear the queue so the port can be safely stopped.
	 *
	 * XXX sili chip - expiration needs to be per-target if PM supports
	 *	FBSS?
	 */
	if (ccb) {
		TAILQ_INSERT_TAIL(&ap->ap_ccb_pending, ccb, ccb_entry);
	} else if (ap->ap_expired) {
		return;
	}

	/*
	 * Pull the next ccb off the queue and run it if possible.
	 * If the port is not ready to accept commands enable the
	 * ready interrupt instead of starting a new command.
	 *
	 * XXX limit ncqdepth for attached devices behind PM
	 */
	while ((ccb = TAILQ_FIRST(&ap->ap_ccb_pending)) != NULL) {
		/*
		 * Port may be wedged.
		 */
		if ((sili_pread(ap, SILI_PREG_STATUS) &
		    SILI_PREG_STATUS_READY) == 0) {
			kprintf("%s: slot %d NOT READY\n",
				ATANAME(ap, ccb->ccb_xa.at), ccb->ccb_slot);
			sili_pwrite(ap, SILI_PREG_INT_ENABLE,
				    SILI_PREG_INT_READY);
			break;
		}

		/*
		 * Handle exclusivity requirements.  ATA_F_EXCLUSIVE is used
		 * when we may have to access the rfis which is stored in
		 * the LRAM PRB.  Unfortunately reading the LRAM PRB is
		 * highly problematic, so requests (like PM requests) which
		 * need to access the rfis use exclusive mode and then
		 * access the copy made by the port interrupt code back in
		 * host memory.
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
				break;
			}

			/*
			 * If the ccb we want to run is exclusive and ccb's
			 * are still active on the port, we can't queue it
			 * yet.
			 *
			 * XXX Current AUTOSENSE code forces exclusivity
			 *     to simplify the code.
			 */
			if (ccb->ccb_xa.flags &
			    (ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE)) {
				break;
			}
		}

		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_ONCHIP;
		ap->ap_active |= 1 << ccb->ccb_slot;
		ap->ap_active_cnt++;
		ap->ap_run_flags = ccb->ccb_xa.flags;

		/*
		 * We can't use the CMD_FIFO method because it requires us
		 * building the PRB in the LRAM, and the LRAM is buggy.  So
		 * we use host memory for the PRB.
		 */
		sili_pwrite(ap, SILI_PREG_CMDACT(ccb->ccb_slot),
			    (u_int32_t)ccb->ccb_prb_paddr);
		sili_pwrite(ap, SILI_PREG_CMDACT(ccb->ccb_slot) + 4,
			    (u_int32_t)(ccb->ccb_prb_paddr >> 32));
		/* sili_pwrite(ap, SILI_PREG_CMD_FIFO, ccb->ccb_slot); */
		sili_start_timeout(ccb);
	}
}

void
sili_intr(void *arg)
{
	struct sili_softc	*sc = arg;
	struct sili_port	*ap;
	u_int32_t		gint;
	int			port;

	/*
	 * Check if the master enable is up, and whether any interrupts are
	 * pending.
	 *
	 * Clear the ints we got.
	 */
	if ((sc->sc_flags & SILI_F_INT_GOOD) == 0)
		return;
	gint = sili_read(sc, SILI_REG_GINT);
	if (gint == 0 || gint == 0xffffffff)
		return;
	sili_write(sc, SILI_REG_GINT, gint);

	/*
	 * Process interrupts for each port in a non-blocking fashion.
	 */
	while (gint & SILI_REG_GINT_PORTMASK) {
		port = ffs(gint) - 1;
		ap = sc->sc_ports[port];
		if (ap) {
			if (sili_os_lock_port_nb(ap) == 0) {
				sili_port_intr(ap, 0);
				sili_os_unlock_port(ap);
			} else {
				sili_port_interrupt_redisable(ap);
				sili_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			}
		}
		gint &= ~(1 << port);
	}
}

/*
 * Core called from helper thread.
 */
void
sili_port_thread_core(struct sili_port *ap, int mask)
{
	/*
	 * Process any expired timedouts.
	 */
	sili_os_lock_port(ap);
	if (mask & AP_SIGF_TIMEOUT) {
		sili_check_active_timeouts(ap);
	}

	/*
	 * Process port interrupts which require a higher level of
	 * intervention.
	 */
	if (mask & AP_SIGF_PORTINT) {
		sili_port_intr(ap, 1);
		sili_port_interrupt_reenable(ap);
	}
	sili_os_unlock_port(ap);
}

/*
 * Core per-port interrupt handler.
 *
 * If blockable is 0 we cannot call sili_os_sleep() at all and we can only
 * deal with normal command completions which do not require blocking.
 */
void
sili_port_intr(struct sili_port *ap, int blockable)
{
	struct sili_softc	*sc = ap->ap_sc;
	u_int32_t		is;
	int			slot;
	struct sili_ccb		*ccb = NULL;
	struct ata_port		*ccb_at = NULL;
	u_int32_t		active;
	u_int32_t		finished;
	const u_int32_t		blockable_mask = SILI_PREG_IST_PHYRDYCHG |
						 SILI_PREG_IST_DEVEXCHG |
						 SILI_PREG_IST_CERROR |
						 SILI_PREG_IST_DECODE |
						 SILI_PREG_IST_CRC |
						 SILI_PREG_IST_HANDSHK;
	const u_int32_t		fatal_mask     = SILI_PREG_IST_PHYRDYCHG |
						 SILI_PREG_IST_DEVEXCHG |
						 SILI_PREG_IST_DECODE |
						 SILI_PREG_IST_CRC |
						 SILI_PREG_IST_HANDSHK;

	enum { NEED_NOTHING, NEED_HOTPLUG_INSERT,
	       NEED_HOTPLUG_REMOVE } need = NEED_NOTHING;

	/*
	 * NOTE: CCOMPLETE was automatically cleared when we read INT_STATUS.
	 */
	is = sili_pread(ap, SILI_PREG_INT_STATUS);
	is &= SILI_PREG_IST_MASK;
	if (is & SILI_PREG_IST_CCOMPLETE)
		sili_pwrite(ap, SILI_PREG_INT_STATUS, SILI_PREG_IST_CCOMPLETE);

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
			sili_port_interrupt_redisable(ap);
			sili_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			/*is &= ~blockable_mask;*/
			return;
		}
		if (is & blockable_mask) {
			sili_port_interrupt_redisable(ap);
			sili_os_signal_port_thread(ap, AP_SIGF_PORTINT);
			/*is &= ~blockable_mask;*/
			return;
		}
	}

	if (is & SILI_PREG_IST_CERROR) {
		/*
		 * Command failed (blockable).
		 *
		 * This stops command processing.  We can extract the PM
		 * target from the PMP field in SILI_PREG_CONTEXT.  The
		 * tag is not necessarily valid so don't use that.
		 *
		 * We must then expire all CCB's for that target and resume
		 * processing if any other targets have active commands.
		 * Particular error codes can be recovered by reading the LOG
		 * page.
		 *
		 * The expire handling code will do the rest, which is
		 * basically to reset the port once the only active
		 * commands remaining are all expired.
		 */
		u_int32_t error;
		int	  target;
		int	  resume = 1;

		target = (sili_pread(ap, SILI_PREG_CONTEXT) >>
			  SILI_PREG_CONTEXT_PMPORT_SHIFT) &
			  SILI_PREG_CONTEXT_PMPORT_MASK;
		sili_pwrite(ap, SILI_PREG_INT_STATUS, SILI_PREG_IST_CERROR);
		active = ap->ap_active & ~ap->ap_expired;
		error = sili_pread(ap, SILI_PREG_CERROR);
		kprintf("%s.%d target error %d active=%08x hactive=%08x "
			"SERR=%b\n",
			PORTNAME(ap), target, error,
			active, sili_pread(ap, SILI_PREG_SLOTST),
			sili_pread(ap, SILI_PREG_SERR), SILI_PFMT_SERR);

		while (active) {
			slot = ffs(active) - 1;
			ccb = &ap->ap_ccbs[slot];
			if ((ccb_at = ccb->ccb_xa.at) == NULL)
				ccb_at = &ap->ap_ata[0];
			if (target == ccb_at->at_target) {
				if ((ccb->ccb_xa.flags & ATA_F_NCQ) &&
				    (error == SILI_PREG_CERROR_DEVICE ||
				     error == SILI_PREG_CERROR_SDBERROR)) {
					ccb_at->at_features |= ATA_PORT_F_READLOG;
				}
				if (sili_core_timeout(ccb, 1) == 0)
					resume = 0;
			}
			active &= ~(1 << slot);
		}

		/*
		 * Resume will be 0 if the timeout reinited and restarted
		 * the port.  Otherwise we resume the port to allow other
		 * commands to complete.
		 */
		if (resume)
			sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_RESUME);
	}

	/*
	 * Device notification to us (non-blocking)
	 *
	 * This is interrupt status SILIPREG_IST_SDB
	 *
	 * NOTE!  On some parts notification bits can get set without
	 *	  generating an interrupt.  It is unclear whether this is
	 *	  a bug in the PM (sending a DTOH device setbits with 'N' set
	 *	  and 'I' not set), or a bug in the host controller.
	 *
	 *	  It only seems to occur under load.
	 */
	if (sc->sc_flags & SILI_F_SSNTF) {
		u_int32_t data;
		const char *xstr;

		data = sili_pread(ap, SILI_PREG_SNTF);
		if (is & SILI_PREG_IST_SDB) {
			sili_pwrite(ap, SILI_PREG_INT_STATUS,
				    SILI_PREG_IST_SDB);
			is &= ~SILI_PREG_IST_SDB;
			xstr = " (no SDBS!)";
		} else {
			xstr = "";
		}
		if (data) {
			kprintf("%s: NOTIFY %08x%s\n",
				PORTNAME(ap), data, xstr);
			sili_pwrite(ap, SILI_PREG_SNTF, data);
			sili_cam_changed(ap, NULL, -1);
		}
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
	if (is & (SILI_PREG_IST_PHYRDYCHG | SILI_PREG_IST_DEVEXCHG)) {
		/* XXX */
		sili_pwrite(ap, SILI_PREG_SERR,
			(SILI_PREG_SERR_DIAG_N | SILI_PREG_SERR_DIAG_X));
		sili_pwrite(ap, SILI_PREG_INT_STATUS,
		    is & (SILI_PREG_IST_PHYRDYCHG | SILI_PREG_IST_DEVEXCHG));

		is &= ~(SILI_PREG_IST_PHYRDYCHG | SILI_PREG_IST_DEVEXCHG);
		kprintf("%s: Port change\n", PORTNAME(ap));

		switch (sili_pread(ap, SILI_PREG_SSTS) & SILI_PREG_SSTS_DET) {
		case SILI_PREG_SSTS_DET_DEV:
			if (ap->ap_type == ATA_PORT_T_NONE &&
			    ap->ap_probe == ATA_PROBE_FAILED) {
				need = NEED_HOTPLUG_INSERT;
				goto fatal;
			}
			break;
		default:
			kprintf("%s: Device lost\n", PORTNAME(ap));
			if (ap->ap_type != ATA_PORT_T_NONE) {
				need = NEED_HOTPLUG_REMOVE;
				goto fatal;
			}
			break;
		}
	}

	/*
	 * Check for remaining errors - they are fatal. (blockable)
	 */
	if (is & fatal_mask) {
		u_int32_t serr;

		sili_pwrite(ap, SILI_PREG_INT_STATUS, is & fatal_mask);

		serr = sili_pread(ap, SILI_PREG_SERR);
		kprintf("%s: Unrecoverable errors (IS: %b, SERR: %b), "
			"disabling port.\n",
			PORTNAME(ap),
			is, SILI_PFMT_INT_STATUS,
			serr, SILI_PFMT_SERR
		);
		is &= ~fatal_mask;
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
	if (ap->ap_state == AP_S_FATAL_ERROR &&
	    (ap->ap_active & ~ap->ap_expired)) {
		kprintf("%s: Fatal port error, expiring %08x\n",
			PORTNAME(ap), ap->ap_active & ~ap->ap_expired);
fatal:
		ap->ap_state = AP_S_FATAL_ERROR;

		/*
		 * Error all the active slots.  If running across a PM
		 * try to error out just the slots related to the target.
		 */
		active = ap->ap_active & ~ap->ap_expired;

		while (active) {
			slot = ffs(active) - 1;
			active &= ~(1 << slot);
			ccb = &ap->ap_ccbs[slot];
			sili_core_timeout(ccb, 1);
		}
	}

	/*
	 * CCB completion (non blocking).
	 *
	 * CCB completion is detected by noticing the slot bit in
	 * the port slot status register has cleared while the bit
	 * is still set in our ap_active variable.
	 *
	 * When completing expired events we must remember to reinit
	 * the port once everything is clear.
	 *
	 * Due to a single-level recursion when reading the log page,
	 * it is possible for the slot to already have been cleared
	 * for some expired tags, do not include expired tags in
	 * the list.
	 */
	active = ap->ap_active & ~sili_pread(ap, SILI_PREG_SLOTST);
	active &= ~ap->ap_expired;

	finished = active;
	while (active) {
		slot = ffs(active) - 1;
		ccb = &ap->ap_ccbs[slot];

		DPRINTF(SILI_D_INTR, "%s: slot %d is complete%s\n",
		    PORTNAME(ap), slot, ccb->ccb_xa.state == ATA_S_ERROR ?
		    " (error)" : "");

		active &= ~(1 << slot);

		/*
		 * XXX sync POSTREAD for return data?
		 */
		ap->ap_active &= ~(1 << ccb->ccb_slot);
		--ap->ap_active_cnt;

		/*
		 * Complete the ccb.  If the ccb was marked expired it
		 * may or may not have been cleared from the port,
		 * make sure we mark it as having timed out.
		 *
		 * In a normal completion if AUTOSENSE is set we copy
		 * the PRB LRAM rfis back to the rfis in host-memory.
		 *
		 * XXX Currently AUTOSENSE also forces exclusivity so we
		 *     can safely work around a hardware bug when reading
		 *     the LRAM.
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
					memcpy(ccb->ccb_xa.rfis,
					       &ccb->ccb_prb_lram->prb_d2h,
					       sizeof(ccb->ccb_prb_lram->prb_d2h));
					if (ccb->ccb_xa.state == ATA_S_TIMEOUT)
						ccb->ccb_xa.state = ATA_S_ERROR;
				}
			}
			ccb->ccb_done(ccb);
		}
	}
	if (is & SILI_PREG_IST_READY) {
		is &= ~SILI_PREG_IST_READY;
		sili_pwrite(ap, SILI_PREG_INT_DISABLE, SILI_PREG_INT_READY);
		sili_pwrite(ap, SILI_PREG_INT_STATUS, SILI_PREG_IST_READY);
	}

	/*
	 * If we had expired commands and were waiting for
	 * remaining commands to complete, and they have now
	 * completed, we can reinit the port.
	 *
	 * This will also clean out the expired commands.
	 * The timeout code also calls sili_port_reinit() if
	 * the only commands remaining after a timeout are all
	 * now expired commands.
	 *
	 * Otherwise just reissue.
	 */
	if (ap->ap_expired && ap->ap_active == ap->ap_expired) {
		if (finished)
			sili_port_reinit(ap);
	} else {
		sili_issue_pending_commands(ap, NULL);
	}

	/*
	 * Cleanup.  Will not be set if non-blocking.
	 */
	switch(need) {
	case NEED_HOTPLUG_INSERT:
		/*
		 * A hot-plug insertion event has occured and all
		 * outstanding commands have already been revoked.
		 *
		 * Don't recurse if this occurs while we are
		 * resetting the port.
		 *
		 * Place the port in a continuous COMRESET state
		 * until the INIT code gets to it.
		 */
		kprintf("%s: HOTPLUG - Device inserted\n",
			PORTNAME(ap));
		ap->ap_probe = ATA_PROBE_NEED_INIT;
		sili_cam_changed(ap, NULL, -1);
		break;
	case NEED_HOTPLUG_REMOVE:
		/*
		 * A hot-plug removal event has occured and all
		 * outstanding commands have already been revoked.
		 *
		 * Don't recurse if this occurs while we are
		 * resetting the port.
		 */
		kprintf("%s: HOTPLUG - Device removed\n",
			PORTNAME(ap));
		sili_port_hardstop(ap);
		/* ap_probe set to failed */
		sili_cam_changed(ap, NULL, -1);
		break;
	default:
		break;
	}
}

struct sili_ccb *
sili_get_ccb(struct sili_port *ap)
{
	struct sili_ccb			*ccb;

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
sili_put_ccb(struct sili_ccb *ccb)
{
	struct sili_port		*ap = ccb->ccb_port;

	lockmgr(&ap->ap_ccb_lock, LK_EXCLUSIVE);
	ccb->ccb_xa.state = ATA_S_PUT;
	++ccb->ccb_xa.serial;
	TAILQ_INSERT_TAIL(&ap->ap_ccb_free, ccb, ccb_entry);
	lockmgr(&ap->ap_ccb_lock, LK_RELEASE);
}

struct sili_ccb *
sili_get_err_ccb(struct sili_port *ap)
{
	struct sili_ccb *err_ccb;

	KKASSERT((ap->ap_flags & AP_F_ERR_CCB_RESERVED) == 0);
	ap->ap_flags |= AP_F_ERR_CCB_RESERVED;

	/*
	 * Grab a CCB to use for error recovery.  This should never fail, as
	 * we ask atascsi to reserve one for us at init time.
	 */
	err_ccb = ap->ap_err_ccb;
	KKASSERT(err_ccb != NULL);
	err_ccb->ccb_xa.flags = 0;
	err_ccb->ccb_done = sili_empty_done;

	return err_ccb;
}

void
sili_put_err_ccb(struct sili_ccb *ccb)
{
	struct sili_port *ap = ccb->ccb_port;

	KKASSERT((ap->ap_flags & AP_F_ERR_CCB_RESERVED) != 0);

	KKASSERT(ccb == ap->ap_err_ccb);

	ap->ap_flags &= ~AP_F_ERR_CCB_RESERVED;
}

/*
 * Read log page to get NCQ error.
 *
 * Return 0 on success
 */
void
sili_port_read_ncq_error(struct sili_port *ap, int target)
{
	struct sili_ccb		*ccb;
	struct ata_fis_h2d	*fis;
	int			status;

	DPRINTF(SILI_D_VERBOSE, "%s: read log page\n", PORTNAME(ap));

	/* Prep error CCB for READ LOG EXT, page 10h, 1 sector. */
	ccb = sili_get_err_ccb(ap);
	ccb->ccb_done = sili_empty_done;
	ccb->ccb_xa.flags = ATA_F_NOWAIT | ATA_F_READ | ATA_F_POLL;
	ccb->ccb_xa.data = ap->ap_err_scratch;
	ccb->ccb_xa.datalen = 512;
	ccb->ccb_xa.complete = sili_dummy_done;
	ccb->ccb_xa.at = &ap->ap_ata[target];
	fis = &ccb->ccb_prb->prb_h2d;
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

	/*
	 * NOTE: Must use sili_quick_timeout() because we hold the err_ccb
	 */
	if (sili_load_prb(ccb) != 0) {
		status = ATA_S_ERROR;
	} else {
		ccb->ccb_xa.state = ATA_S_PENDING;
		status = sili_poll(ccb, 1000, sili_quick_timeout);
	}

	/*
	 * Just spew if it fails, there isn't much we can do at this point.
	 */
	if (status != ATA_S_COMPLETE) {
		kprintf("%s: log page read failed, slot %d was still active.\n",
			ATANAME(ap, ccb->ccb_xa.at), ccb->ccb_slot);
	}

	/* Done with the error CCB now. */
	sili_unload_prb(ccb);
	sili_put_err_ccb(ccb);

	/* Extract failed register set and tags from the scratch space. */
	if (status == ATA_S_COMPLETE) {
		struct ata_log_page_10h		*log;
		int				err_slot;

		log = (struct ata_log_page_10h *)ap->ap_err_scratch;
		if (log->err_regs.type & ATA_LOG_10H_TYPE_NOTQUEUED) {
			/*
			 * Not queued bit was set - wasn't an NCQ error?
			 *
			 * XXX This bit seems to be set a lot even for NCQ
			 *     errors?
			 */
		} else {
			/*
			 * Copy back the log record as a D2H register FIS.
			 */
			err_slot = log->err_regs.type &
				   ATA_LOG_10H_TYPE_TAG_MASK;
			ccb = &ap->ap_ccbs[err_slot];
			if (ap->ap_expired & (1 << ccb->ccb_slot)) {
				kprintf("%s: read NCQ error page slot=%d\n",
					ATANAME(ap, ccb->ccb_xa.at), err_slot
				);
				memcpy(&ccb->ccb_prb->prb_d2h, &log->err_regs,
					sizeof(struct ata_fis_d2h));
				ccb->ccb_prb->prb_d2h.type = ATA_FIS_TYPE_D2H;
				ccb->ccb_prb->prb_d2h.flags = 0;
				if (ccb->ccb_xa.state == ATA_S_TIMEOUT)
					ccb->ccb_xa.state = ATA_S_ERROR;
			} else {
				kprintf("%s: read NCQ error page slot=%d, "
					"slot does not match any cmds\n",
					ATANAME(ccb->ccb_port, ccb->ccb_xa.at),
					err_slot
				);
			}
		}
	}
}

/*
 * Allocate memory for various structures DMAd by hardware.  The maximum
 * number of segments for these tags is 1 so the DMA memory will have a
 * single physical base address.
 */
struct sili_dmamem *
sili_dmamem_alloc(struct sili_softc *sc, bus_dma_tag_t tag)
{
	struct sili_dmamem *adm;
	int	error;

	adm = kmalloc(sizeof(*adm), M_DEVBUF, M_INTWAIT | M_ZERO);

	error = bus_dmamem_alloc(tag, (void **)&adm->adm_kva,
				 BUS_DMA_ZERO, &adm->adm_map);
	if (error == 0) {
		adm->adm_tag = tag;
		error = bus_dmamap_load(tag, adm->adm_map,
					adm->adm_kva,
					bus_dma_tag_getmaxsize(tag),
					sili_dmamem_saveseg, &adm->adm_busaddr,
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
sili_dmamem_saveseg(void *info, bus_dma_segment_t *segs, int nsegs, int error)
{
	KKASSERT(error == 0);
	KKASSERT(nsegs == 1);
	*(bus_addr_t *)info = segs->ds_addr;
}


void
sili_dmamem_free(struct sili_softc *sc, struct sili_dmamem *adm)
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
sili_read(struct sili_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
			  BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, r));
}

void
sili_write(struct sili_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
			  BUS_SPACE_BARRIER_WRITE);
}

u_int32_t
sili_pread(struct sili_port *ap, bus_size_t r)
{
	bus_space_barrier(ap->ap_sc->sc_iot, ap->ap_ioh, r, 4,
			  BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(ap->ap_sc->sc_iot, ap->ap_ioh, r));
}

void
sili_pwrite(struct sili_port *ap, bus_size_t r, u_int32_t v)
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
sili_pwait_eq(struct sili_port *ap, int timeout,
	      bus_size_t r, u_int32_t mask, u_int32_t target)
{
	int	t;

	/*
	 * Loop hard up to 100uS
	 */
	for (t = 0; t < 100; ++t) {
		if ((sili_pread(ap, r) & mask) == target)
			return (0);
		sili_os_hardsleep(1);	/* us */
	}

	do {
		timeout -= sili_os_softsleep();
		if ((sili_pread(ap, r) & mask) == target)
			return (0);
	} while (timeout > 0);
	return (1);
}

int
sili_wait_ne(struct sili_softc *sc, bus_size_t r, u_int32_t mask,
	     u_int32_t target)
{
	int	t;

	/*
	 * Loop hard up to 100uS
	 */
	for (t = 0; t < 100; ++t) {
		if ((sili_read(sc, r) & mask) != target)
			return (0);
		sili_os_hardsleep(1);	/* us */
	}

	/*
	 * And one millisecond the slow way
	 */
	t = 1000;
	do {
		t -= sili_os_softsleep();
		if ((sili_read(sc, r) & mask) != target)
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
sili_ata_get_xfer(struct sili_port *ap, struct ata_port *at)
{
	struct sili_ccb		*ccb;

	ccb = sili_get_ccb(ap);
	if (ccb == NULL) {
		DPRINTF(SILI_D_XFER, "%s: sili_ata_get_xfer: NULL ccb\n",
		    PORTNAME(ap));
		return (NULL);
	}

	DPRINTF(SILI_D_XFER, "%s: sili_ata_get_xfer got slot %d\n",
	    PORTNAME(ap), ccb->ccb_slot);

	bzero(ccb->ccb_xa.fis, sizeof(*ccb->ccb_xa.fis));
	ccb->ccb_xa.at = at;
	ccb->ccb_xa.fis->type = ATA_FIS_TYPE_H2D;

	return (&ccb->ccb_xa);
}

void
sili_ata_put_xfer(struct ata_xfer *xa)
{
	struct sili_ccb			*ccb = (struct sili_ccb *)xa;

	DPRINTF(SILI_D_XFER, "sili_ata_put_xfer slot %d\n", ccb->ccb_slot);

	sili_put_ccb(ccb);
}

int
sili_ata_cmd(struct ata_xfer *xa)
{
	struct sili_ccb			*ccb = (struct sili_ccb *)xa;

	KKASSERT(xa->state == ATA_S_SETUP);

	if (ccb->ccb_port->ap_state == AP_S_FATAL_ERROR)
		goto failcmd;
#if 0
	kprintf("%s: started std command %b ccb %d ccb_at %p %d\n",
		ATANAME(ccb->ccb_port, ccb->ccb_xa.at),
		sili_pread(ccb->ccb_port, SILI_PREG_CMD), SILI_PFMT_CMD,
		ccb->ccb_slot,
		ccb->ccb_xa.at,
		ccb->ccb_xa.at ? ccb->ccb_xa.at->at_target : -1);
#endif

	ccb->ccb_done = sili_ata_cmd_done;

	if (sili_load_prb(ccb) != 0)
		goto failcmd;

	xa->state = ATA_S_PENDING;

	if (xa->flags & ATA_F_POLL)
		return (sili_poll(ccb, xa->timeout, sili_ata_cmd_timeout));

	crit_enter();
	KKASSERT((xa->flags & ATA_F_TIMEOUT_EXPIRED) == 0);
	xa->flags |= ATA_F_TIMEOUT_DESIRED;
	sili_start(ccb);
	crit_exit();
	return (xa->state);

failcmd:
	crit_enter();
	xa->state = ATA_S_ERROR;
	xa->complete(xa);
	crit_exit();
	return (ATA_S_ERROR);
}

static void
sili_ata_cmd_done(struct sili_ccb *ccb)
{
	struct ata_xfer			*xa = &ccb->ccb_xa;
	int serial;

	/*
	 * NOTE: callout does not lock port and may race us modifying
	 * the flags, so make sure its stopped.
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

	KKASSERT(xa->state != ATA_S_ONCHIP);
	sili_unload_prb(ccb);

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
sili_ata_cmd_timeout_unserialized(void *arg)
{
	struct sili_ccb		*ccb = arg;
	struct sili_port	*ap = ccb->ccb_port;

	ccb->ccb_xa.flags &= ~ATA_F_TIMEOUT_RUNNING;
	ccb->ccb_xa.flags |= ATA_F_TIMEOUT_EXPIRED;
	sili_os_signal_port_thread(ap, AP_SIGF_TIMEOUT);
}

void
sili_ata_cmd_timeout(struct sili_ccb *ccb)
{
	sili_core_timeout(ccb, 0);
}

/*
 * Timeout code, typically called when the port command processor is running.
 *
 * Returns 0 if all timeout processing completed, non-zero if it is still
 * in progress.
 */
static
int
sili_core_timeout(struct sili_ccb *ccb, int really_error)
{
	struct ata_xfer		*xa = &ccb->ccb_xa;
	struct sili_port	*ap = ccb->ccb_port;
	struct ata_port		*at;

	at = ccb->ccb_xa.at;

	kprintf("%s: CMD %s state=%d slot=%d\n"
		"\t active=%08x\n"
		"\texpired=%08x\n"
		"\thactive=%08x\n",
		ATANAME(ap, at),
		(really_error ? "ERROR" : "TIMEOUT"),
		ccb->ccb_xa.state, ccb->ccb_slot,
		ap->ap_active,
		ap->ap_expired,
		sili_pread(ap, SILI_PREG_SLOTST)
	);

	/*
	 * NOTE: Timeout will not be running if the command was polled.
	 *	 If we got here at least one of these flags should be set.
	 *
	 *	 However, it might be running if we are called from the
	 *	 interrupt error handling code.
	 */
	KKASSERT(xa->flags & (ATA_F_POLL | ATA_F_TIMEOUT_DESIRED |
			      ATA_F_TIMEOUT_RUNNING));
	if (xa->flags & ATA_F_TIMEOUT_RUNNING) {
		callout_stop(&ccb->ccb_timeout);
		xa->flags &= ~ATA_F_TIMEOUT_RUNNING;
	}
	xa->flags &= ~ATA_F_TIMEOUT_EXPIRED;

	if (ccb->ccb_xa.state == ATA_S_PENDING) {
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ccb->ccb_done(ccb);
		xa->complete(xa);
		sili_issue_pending_commands(ap, NULL);
		return(1);
	}
	if (ccb->ccb_xa.state != ATA_S_ONCHIP) {
		kprintf("%s: Unexpected state during timeout: %d\n",
			ATANAME(ap, at), ccb->ccb_xa.state);
		return(1);
	}

	/*
	 * We can't process timeouts while other commands are running.
	 */
	ap->ap_expired |= 1 << ccb->ccb_slot;

	if (ap->ap_active != ap->ap_expired) {
		kprintf("%s: Deferred timeout until its safe, slot %d\n",
			ATANAME(ap, at), ccb->ccb_slot);
		return(1);
	}

	/*
	 * We have to issue a Port reinit.  We don't read an error log
	 * page for timeouts.  Reiniting the port will clear all pending
	 * commands.
	 */
	sili_port_reinit(ap);
	return(0);
}

/*
 * Used by the softreset, pm_port_probe, and read_ncq_error only, in very
 * specialized, controlled circumstances.
 */
void
sili_quick_timeout(struct sili_ccb *ccb)
{
	struct sili_port *ap = ccb->ccb_port;

	switch (ccb->ccb_xa.state) {
	case ATA_S_PENDING:
		TAILQ_REMOVE(&ap->ap_ccb_pending, ccb, ccb_entry);
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		break;
	case ATA_S_ONCHIP:
		KKASSERT((ap->ap_active & ~ap->ap_expired) ==
			 (1 << ccb->ccb_slot));
		ccb->ccb_xa.state = ATA_S_TIMEOUT;
		ap->ap_active &= ~(1 << ccb->ccb_slot);
		KKASSERT(ap->ap_active_cnt > 0);
		--ap->ap_active_cnt;
		sili_port_reinit(ap);
		break;
	default:
		panic("%s: sili_quick_timeout: ccb in bad state %d",
		      ATANAME(ap, ccb->ccb_xa.at), ccb->ccb_xa.state);
	}
}

static void
sili_dummy_done(struct ata_xfer *xa)
{
}

static void
sili_empty_done(struct sili_ccb *ccb)
{
}
