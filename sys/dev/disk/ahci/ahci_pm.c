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
 */

#include "ahci.h"

static void ahci_pm_dummy_done(struct ata_xfer *xa);

int
ahci_pm_port_init(struct ahci_port *ap, struct ata_port *at)
{
        at->at_probe = ATA_PROBE_NEED_HARD_RESET;
	return (0);
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
ahci_pm_port_probe(struct ahci_port *ap, int orig_error)
{
	struct ahci_cmd_hdr *cmd_slot;
	struct ata_port	*at;
	struct ahci_ccb	*ccb = NULL;
	u_int8_t	*fis = NULL;
	int		error;
	u_int32_t	cmd;
	u_int32_t	fbs;
	int		count;
	int		i;

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
	 * Try to enable FIS-Based switching.  We have to probe PREG_FBS
	 *
	 * XXX I'm still trying to find an AHCI chipset that actually
	 *     supports FBSS so I can finish implementing support, so
	 *     this doesn't do a whole lot right now.
	 */
	if (ap->ap_sc->sc_cap & AHCI_REG_CAP_FBSS) {
		const char *str1 = "";
		const char *str2 = "";
		const char *str3 = "";

		if (cmd & AHCI_PREG_CMD_FBSCP) {
			str1 = "HW indicates support";
		} else if (ap->ap_sc->sc_flags & AHCI_F_FORCE_FBSS) {
			cmd |= AHCI_PREG_CMD_FBSCP;	/* Force on */
			str1 = "HW indicates no-support, force";
		} else {
			str1 = "HW indicates no-support";
		}
		if (cmd & AHCI_PREG_CMD_FBSCP) {
			fbs = ahci_pread(ap, AHCI_PREG_FBS);
			ahci_pwrite(ap, AHCI_PREG_FBS, fbs | AHCI_PREG_FBS_EN);
			fbs = ahci_pread(ap, AHCI_PREG_FBS);
			if (fbs & AHCI_PREG_FBS_EN) {
				str2 = ", enable succeeded";
				str3 = ", (driver support not yet implemented)";
			} else {
				str2 = ", enable failed";
			}
			ahci_pwrite(ap, AHCI_PREG_FBS, fbs & ~AHCI_PREG_FBS_EN);
		}
		kprintf("%s: Port multiplier: FIS-Based Sw: %s%s%s\n",
			PORTNAME(ap), str1, str2, str3);
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
		error = EIO;
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
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_SILENT;
	ccb->ccb_xa.complete = ahci_pm_dummy_done;
	ccb->ccb_xa.at = ap->ap_ata[15];
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

	/*
	 * The only way one can determine if a port multiplier is on the
	 * port is to probe target 15, and of course this will fail if
	 * there is no port multiplier.
	 *
	 * The probing has to be done whether or not a device is probed on
	 * target 0, because when a PM is attached target 0 represents
	 * slot #0 behind the PM.
	 *
	 * Port multipliers are expected to answer more quickly than normal
	 * devices, use a shorter timeout than normal.
	 *
	 * If there is no PM here this command can still succeed due to
	 * the _C_
	 */
	if (ahci_poll(ccb, 500, ahci_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: PMPROBE(1) No Port Multiplier was found.\n",
			PORTNAME(ap));
		if (--count) {
			ahci_put_err_ccb(ccb);
			goto retry;
		}
		error = EBUSY;
		goto err;
	}

	if (ahci_pwait_clr(ap, AHCI_PREG_TFD,
			       AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
		kprintf("%s: PMPROBE Busy after first FIS\n", PORTNAME(ap));
	}

	/*
	 * The device may have muffed up the PHY when it reset.
	 */
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
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_SILENT;

	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[1] = 0x0F;
	fis[15] = ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_PMP); /* port 0xF */

	ccb->ccb_xa.state = ATA_S_PENDING;

	/*
	 * The only way one can determine if a port multiplier is on the
	 * port is to probe target 15, and of course this will fail if
	 * there is no port multiplier.
	 *
	 * The probing has to be done whether or not a device is probed on
	 * target 0, because when a PM is attached target 0 represents
	 * slot #0 behind the PM.
	 */
	if (ahci_poll(ccb, 5000, ahci_quick_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: PMPROBE(2) No Port Multiplier was found.\n",
			PORTNAME(ap));
		if (--count) {
			ahci_put_err_ccb(ccb);
			goto retry;
		}
		error = EBUSY;
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
		ap->ap_ata[15]->at_probe = ATA_PROBE_GOOD;
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
			at = ap->ap_ata[i];
			at->at_probe = ATA_PROBE_NEED_INIT;
			at->at_features |= ATA_PORT_F_RESCAN;
		}
		ap->ap_type = ATA_PORT_T_PM;
		return (0);
	}

	/*
	 * If we failed turn off PMA, otherwise identify the port multiplier.
	 * CAM will iterate the devices.
	 */
	ahci_port_stop(ap, 0);
	ahci_port_clo(ap);
	cmd = ahci_pread(ap, AHCI_PREG_CMD) & ~AHCI_PREG_CMD_ICC;
	cmd &= ~AHCI_PREG_CMD_PMA;
	ahci_pwrite(ap, AHCI_PREG_CMD, cmd);
	if (orig_error == 0) {
		if (ahci_pwait_clr(ap, AHCI_PREG_TFD,
			    AHCI_PREG_TFD_STS_BSY | AHCI_PREG_TFD_STS_DRQ)) {
			kprintf("%s: PM probe: port will not come ready\n",
				PORTNAME(ap));
			orig_error = EBUSY;
			ahci_port_init(ap);
		}
	}
	return(orig_error);
}

/*
 * Identify the port multiplier
 */
int
ahci_pm_identify(struct ahci_port *ap)
{
	u_int32_t chipid;
	u_int32_t rev;
	u_int32_t nports;
	u_int32_t data1;
	u_int32_t data2;
	int	  has_dummy_port;

	ap->ap_probe = ATA_PROBE_FAILED;
	if (ahci_pm_read(ap, 15, 0, &chipid))
		goto err;
	if (ahci_pm_read(ap, 15, 1, &rev))
		goto err;
	if (ahci_pm_read(ap, 15, 2, &nports))
		goto err;
	nports &= 0x0000000F;	/* only the low 4 bits */
	ap->ap_probe = ATA_PROBE_GOOD;

	/*
	 * Ignore fake port on PMs which have it.  We can probe it but the
	 * softreset will probably fail.
	 */
	switch(chipid) {
	case 0x37261095:
		has_dummy_port = 1;
		break;
	default:
		has_dummy_port = 0;
		break;
	}
	if (has_dummy_port) {
		if (nports > 1)
			--nports;
	}

	kprintf("%s: Port multiplier: chip=%08x rev=0x%b nports=%d\n",
		PORTNAME(ap),
		chipid,
		rev, SATA_PFMT_PM_REV,
		nports);
	if (has_dummy_port) {
		kprintf("%s: Port multiplier: Ignoring dummy port #%d\n",
		PORTNAME(ap), nports);
	}
	ap->ap_pmcount = nports;

	if (ahci_pm_read(ap, 15, SATA_PMREG_FEA, &data1)) {
		kprintf("%s: Port multiplier: Warning, "
			"cannot read feature register\n", PORTNAME(ap));
	} else {
		kprintf("%s: Port multiplier features: 0x%b\n",
			PORTNAME(ap),
			data1,
			SATA_PFMT_PM_FEA);
	}
	if (ahci_pm_read(ap, 15, SATA_PMREG_FEAEN, &data2) == 0) {
		kprintf("%s: Port multiplier defaults: 0x%b\n",
			PORTNAME(ap),
			data2,
			SATA_PFMT_PM_FEA);
	}

	/*
	 * Turn on async notification if we support and the PM supports it.
	 * This allows the PM to forward async notification events to us and
	 * it will also generate an event for target 15 for hot-plug events
	 * (or is supposed to anyway).
	 */
	if ((ap->ap_sc->sc_cap & AHCI_REG_CAP_SSNTF) &&
	    (data1 & SATA_PMFEA_ASYNCNOTIFY)) {
		u_int32_t serr_bits = AHCI_PREG_SERR_DIAG_N |
				      AHCI_PREG_SERR_DIAG_X;
		data2 |= SATA_PMFEA_ASYNCNOTIFY;
		if (ahci_pm_write(ap, 15, SATA_PMREG_FEAEN, data2)) {
			kprintf("%s: Port multiplier: AsyncNotify cannot be "
				"enabled\n", PORTNAME(ap));
		} else if (ahci_pm_write(ap, 15, SATA_PMREG_EEENA, serr_bits)) {
			kprintf("%s: Port mulltiplier: AsyncNotify unable "
				"to enable error info bits\n", PORTNAME(ap));
		} else {
			kprintf("%s: Port multiplier: AsyncNotify enabled\n",
				PORTNAME(ap));
		}
	}

	return (0);
err:
	kprintf("%s: Port multiplier cannot be identified\n", PORTNAME(ap));
	return (EIO);
}

/*
 * Do a COMRESET sequence on the target behind a port multiplier.
 *
 * If hard is 2 we also cycle the phy on the target.
 *
 * This must be done prior to any softreset or probe attempts on
 * targets behind the port multiplier.
 *
 * Returns 0 on success or an error.
 */
int
ahci_pm_hardreset(struct ahci_port *ap, int target, int hard)
{
	struct ata_port *at;
	u_int32_t data;
	int loop;
	int error = EIO;

	at = ap->ap_ata[target];

	/*
	 * Turn off power management and kill the phy on the target
	 * if requested.  Hold state for 10ms.
	 */
	data = AHCI_PREG_SCTL_IPM_DISABLED;
	if (hard == 2)
		data |= AHCI_PREG_SCTL_DET_DISABLE;
	if (ahci_pm_write(ap, target, SATA_PMREG_SERR, -1))
		goto err;
	if (ahci_pm_write(ap, target, SATA_PMREG_SCTL, data))
		goto err;
	ahci_os_sleep(10);

	/*
	 * Start transmitting COMRESET.  COMRESET must be sent for at
	 * least 1ms.
	 */
	at->at_probe = ATA_PROBE_FAILED;
	at->at_type = ATA_PORT_T_NONE;
	data = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_INIT;
	switch(AhciForceGen) {
	case 0:
		data |= AHCI_PREG_SCTL_SPD_ANY;
		break;
	case 1:
		data |= AHCI_PREG_SCTL_SPD_GEN1;
		break;
	case 2:
		data |= AHCI_PREG_SCTL_SPD_GEN2;
		break;
	case 3:
		data |= AHCI_PREG_SCTL_SPD_GEN3;
		break;
	default:
		data |= AHCI_PREG_SCTL_SPD_GEN3;
		break;
	}
	if (ahci_pm_write(ap, target, SATA_PMREG_SCTL, data))
		goto err;

	/*
	 * It takes about 100ms for the DET logic to settle down,
	 * from trial and error testing.  If this is too short
	 * the softreset code will fail.
	 */
	ahci_os_sleep(100);

	if (ahci_pm_phy_status(ap, target, &data)) {
		kprintf("%s: (A)Cannot clear phy status\n",
			ATANAME(ap ,at));
	}

	/*
	 * Flush any status, then clear DET to initiate negotiation.
	 */
	ahci_pm_write(ap, target, SATA_PMREG_SERR, -1);
	data = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_NONE;
	if (ahci_pm_write(ap, target, SATA_PMREG_SCTL, data))
		goto err;

	/*
	 * Try to determine if there is a device on the port.
	 *
	 * Give the device 3/10 second to at least be detected.
	 * If we fail clear any pending status since we may have
	 * cycled the phy and probably caused another PRCS interrupt.
	 */
	for (loop = 3; loop; --loop) {
		if (ahci_pm_read(ap, target, SATA_PMREG_SSTS, &data))
			goto err;
		if (data & AHCI_PREG_SSTS_DET)
			break;
		ahci_os_sleep(100);
	}
	if (loop == 0) {
		kprintf("%s.%d: Port appears to be unplugged\n",
			PORTNAME(ap), target);
		error = ENODEV;
		goto err;
	}

	/*
	 * There is something on the port.  Give the device 3 seconds
	 * to fully negotiate.
	 */
	for (loop = 30; loop; --loop) {
		if (ahci_pm_read(ap, target, SATA_PMREG_SSTS, &data))
			goto err;
		if ((data & AHCI_PREG_SSTS_DET) == AHCI_PREG_SSTS_DET_DEV)
			break;
		ahci_os_sleep(100);
	}

	/*
	 * Device not detected
	 */
	if (loop == 0) {
		kprintf("%s: Device may be powered down\n",
			PORTNAME(ap));
		error = ENODEV;
		goto err;
	}

	/*
	 * Device detected
	 */
	kprintf("%s.%d: Device detected data=%08x\n",
		PORTNAME(ap), target, data);
	/*
	 * Clear SERR on the target so we get a new NOTIFY event if a hot-plug
	 * or hot-unplug occurs.  Clear any spurious IFS that may have
	 * occured during the probe.
	 *
	 * WARNING!  100ms seems to work in most cases but
	 */
	ahci_os_sleep(100);
	ahci_pm_write(ap, target, SATA_PMREG_SERR, -1);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);

	error = 0;
err:
	at->at_probe = error ? ATA_PROBE_FAILED : ATA_PROBE_NEED_SOFT_RESET;
	return (error);
}

/*
 * AHCI soft reset through port multiplier.
 *
 * This function keeps port communications intact and attempts to generate
 * a reset to the connected device using device commands.  Unlike
 * hard-port operations we can't do fancy stop/starts or stuff like
 * that without messing up other commands that might be running or
 * queued.
 */
int
ahci_pm_softreset(struct ahci_port *ap, int target)
{
	struct ata_port		*at;
	struct ahci_ccb		*ccb;
	struct ahci_cmd_hdr	*cmd_slot;
	u_int8_t		*fis;
	int			count;
	int			error;
	u_int32_t		data;

	error = EIO;
	at = ap->ap_ata[target];

	DPRINTF(AHCI_D_VERBOSE, "%s: soft reset\n", PORTNAME(ap));

	count = 2;
retry:
	/*
	 * Try to clear the phy so we get a good signature, otherwise
	 * the PM may not latch a new signature.
	 *
	 * NOTE: This cannot be safely done between the first and second
	 *	 softreset FISs.  It's now or never.
	 */
	if (ahci_pm_phy_status(ap, target, &data)) {
		kprintf("%s: (B)Cannot clear phy status\n",
			ATANAME(ap ,at));
	}
	ahci_pm_write(ap, target, SATA_PMREG_SERR, -1);

	/*
	 * Prep first D2H command with SRST feature & clear busy/reset flags
	 *
	 * It is unclear which other fields in the FIS are used.  Just zero
	 * everything.
	 *
	 * When soft-resetting a port behind a multiplier at will be
	 * non-NULL, assigning it to the ccb prevents the port interrupt
	 * from hard-resetting the port if a problem crops up.
	 */
	ccb = ahci_get_err_ccb(ap);
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE;
	ccb->ccb_xa.complete = ahci_pm_dummy_done;
	ccb->ccb_xa.at = at;

	fis = ccb->ccb_cmd_table->cfis;
	bzero(fis, sizeof(ccb->ccb_cmd_table->cfis));
	fis[0] = ATA_FIS_TYPE_H2D;
	fis[1] = at->at_target;
	fis[15] = ATA_FIS_CONTROL_SRST|ATA_FIS_CONTROL_4BIT;

	cmd_slot = ccb->ccb_cmd_hdr;
	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_C); /* Clear busy on OK */
	cmd_slot->flags |= htole16(AHCI_CMD_LIST_FLAG_R); /* Reset */
	cmd_slot->flags |= htole16(at->at_target <<
				   AHCI_CMD_LIST_FLAG_PMP_SHIFT);

	ccb->ccb_xa.state = ATA_S_PENDING;

	/*
	 * This soft reset of the AP target can cause a stream of IFS
	 * errors to occur.  Setting AP_F_IGNORE_IFS prevents the port
	 * from being hard reset (because its the target behind the
	 * port that isn't happy).
	 *
	 * The act of sending the soft reset can cause the target to
	 * blow the port up and generate IFS errors.
	 */
	ap->ap_flags |= AP_F_IGNORE_IFS;
	ap->ap_flags &= ~AP_F_IFS_IGNORED;

	if (ahci_poll(ccb, 1000, ahci_ata_cmd_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: Soft-reset through PM failed, %s\n",
			ATANAME(ap, at),
			(count > 1 ? "retrying" : "giving up"));
		ahci_put_err_ccb(ccb);
		if (--count) {
			if (ap->ap_flags & AP_F_IFS_IGNORED)
				ahci_os_sleep(5000);
			else
				ahci_os_sleep(1000);
			ahci_pwrite(ap, AHCI_PREG_SERR, -1);
			ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);
			goto retry;
		}
		goto err;
	}

	/*
	 * WARNING!  SENSITIVE TIME PERIOD!  WARNING!
	 *
	 * The first and second FISes are supposed to be back-to-back,
	 * I think the idea is to get the second sent and then after
	 * the device resets it will send a signature.  Do not delay
	 * here and most definitely do not issue any commands to other
	 * targets!
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
	fis[1] = at->at_target;
	fis[15] = ATA_FIS_CONTROL_4BIT;

	cmd_slot->prdtl = 0;
	cmd_slot->flags = htole16(5);	/* FIS length: 5 DWORDS */
	cmd_slot->flags |= htole16(at->at_target <<
				   AHCI_CMD_LIST_FLAG_PMP_SHIFT);

	ccb->ccb_xa.state = ATA_S_PENDING;
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE;

	ap->ap_flags &= ~AP_F_IFS_IGNORED;

	if (ahci_poll(ccb, 1000, ahci_ata_cmd_timeout) != ATA_S_COMPLETE) {
		kprintf("%s: Soft-reset(2) through PM failed, %s\n",
			ATANAME(ap, at),
			(count > 1 ? "retrying" : "giving up"));
		if (--count) {
			ahci_os_sleep(1000);
			ahci_put_err_ccb(ccb);
			ahci_pwrite(ap, AHCI_PREG_SERR, -1);
			ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);
			goto retry;
		}
		goto err;
	}

	ahci_put_err_ccb(ccb);
	ahci_os_sleep(100);
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);

	ahci_pm_write(ap, target, SATA_PMREG_SERR, -1);
	if (ahci_pm_phy_status(ap, target, &data)) {
		kprintf("%s: (C)Cannot clear phy status\n",
			ATANAME(ap ,at));
	}
	ahci_pm_write(ap, target, SATA_PMREG_SERR, -1);

	/*
	 * Do it again, even if we think we got a good result
	 */
	if (--count) {
		fis[15] = 0;
		goto retry;
	}

	/*
	 * If the softreset is trying to clear a BSY condition after a
	 * normal portreset we assign the port type.
	 *
	 * If the softreset is being run first as part of the ccb error
	 * processing code then report if the device signature changed
	 * unexpectedly.
	 */
	if (at->at_type == ATA_PORT_T_NONE) {
		at->at_type = ahci_port_signature_detect(ap, at);
	} else {
		if (ahci_port_signature_detect(ap, at) != at->at_type) {
			kprintf("%s: device signature unexpectedly "
				"changed\n", ATANAME(ap, at));
			error = EBUSY; /* XXX */
		}
	}
	error = 0;

	/*
	 * Who knows what kind of mess occured.  We have exclusive access
	 * to the port so try to clean up potential problems.
	 */
err:
	ahci_os_sleep(100);

	/*
	 * Clear error status so we can detect removal.
	 */
	if (ahci_pm_write(ap, target, SATA_PMREG_SERR, -1)) {
		kprintf("%s: ahci_pm_softreset unable to clear SERR\n",
			ATANAME(ap, at));
	}
	ahci_pwrite(ap, AHCI_PREG_SERR, -1);
	ahci_pwrite(ap, AHCI_PREG_IS, AHCI_PREG_IS_IFS);
	ap->ap_flags &= ~(AP_F_IGNORE_IFS | AP_F_IFS_IGNORED);

	at->at_probe = error ? ATA_PROBE_FAILED : ATA_PROBE_NEED_IDENT;
	return (error);
}


/*
 * Return the phy status for a target behind a port multiplier and
 * reset SATA_PMREG_SERR.
 *
 * Returned bits follow AHCI_PREG_SSTS bits.  The AHCI_PREG_SSTS_SPD
 * bits can be used to determine the link speed and will be 0 if there
 * is no link.
 *
 * 0 is returned if any communications error occurs.
 */
int
ahci_pm_phy_status(struct ahci_port *ap, int target, u_int32_t *datap)
{
	int error;

	error = ahci_pm_read(ap, target, SATA_PMREG_SSTS, datap);
	if (error == 0)
		error = ahci_pm_write(ap, target, SATA_PMREG_SERR, -1);
	if (error)
		*datap = 0;
	return(error);
}

/*
 * Check that a target is still good.
 */
void
ahci_pm_check_good(struct ahci_port *ap, int target)
{
	struct ata_port *at;
	u_int32_t data;

	/*
	 * It looks like we might have to read the EINFO register
	 * to allow the PM to generate a new event.
	 */
	if (ahci_pm_read(ap, 15, SATA_PMREG_EINFO, &data)) {
		kprintf("%s: Port multiplier EINFO could not be read\n",
			PORTNAME(ap));
	}

	if (ahci_pm_write(ap, target, SATA_PMREG_SERR, -1)) {
		kprintf("%s: Port multiplier: SERR could not be cleared\n",
			PORTNAME(ap));
	}

	if (target == CAM_TARGET_WILDCARD || target >= ap->ap_pmcount)
		return;
	at = ap->ap_ata[target];

	/*
	 * If the device needs an init or hard reset also make sure the
	 * PHY is turned on.
	 */
	if (at->at_probe <= ATA_PROBE_NEED_HARD_RESET) {
		/*kprintf("%s DOHARD\n", ATANAME(ap, at));*/
		ahci_pm_hardreset(ap, target, 1);
	}

	/*
	 * Read the detect status
	 */
	if (ahci_pm_read(ap, target, SATA_PMREG_SSTS, &data)) {
		kprintf("%s: Unable to access PM SSTS register target %d\n",
			PORTNAME(ap), target);
		return;
	}
	if ((data & AHCI_PREG_SSTS_DET) != AHCI_PREG_SSTS_DET_DEV) {
		/*kprintf("%s: DETECT %08x\n", ATANAME(ap, at), data);*/
		if (at->at_probe != ATA_PROBE_FAILED) {
			at->at_probe = ATA_PROBE_FAILED;
			at->at_type = ATA_PORT_T_NONE;
			at->at_features |= ATA_PORT_F_RESCAN;
			kprintf("%s: HOTPLUG (PM) - Device removed\n",
				ATANAME(ap, at));
		}
	} else {
		if (at->at_probe == ATA_PROBE_FAILED) {
			at->at_probe = ATA_PROBE_NEED_HARD_RESET;
			at->at_features |= ATA_PORT_F_RESCAN;
			kprintf("%s: HOTPLUG (PM) - Device inserted\n",
				ATANAME(ap, at));
		}
	}
}

/*
 * Read a PM register
 */
int
ahci_pm_read(struct ahci_port *ap, int target, int which, u_int32_t *datap)
{
	struct ata_xfer	*xa;
	int error;

	xa = ahci_ata_get_xfer(ap, ap->ap_ata[15]);

	xa->fis->type = ATA_FIS_TYPE_H2D;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | 15;
	xa->fis->command = ATA_C_READ_PM;
	xa->fis->features = which;
	xa->fis->device = target | ATA_H2D_DEVICE_LBA;
	xa->fis->control = ATA_FIS_CONTROL_4BIT;

	xa->complete = ahci_pm_dummy_done;
	xa->datalen = 0;
	xa->flags = ATA_F_POLL | ATA_F_AUTOSENSE;
	xa->timeout = 1000;

	if (ahci_ata_cmd(xa) == ATA_S_COMPLETE) {
		*datap = xa->rfis.sector_count | (xa->rfis.lba_low << 8) |
		       (xa->rfis.lba_mid << 16) | (xa->rfis.lba_high << 24);
		error = 0;
	} else {
		kprintf("%s.%d pm_read SCA[%d] failed\n",
			PORTNAME(ap), target, which);
		*datap = 0;
		error = EIO;
	}
	ahci_ata_put_xfer(xa);
	return (error);
}

/*
 * Write a PM register
 */
int
ahci_pm_write(struct ahci_port *ap, int target, int which, u_int32_t data)
{
	struct ata_xfer	*xa;
	int error;

	xa = ahci_ata_get_xfer(ap, ap->ap_ata[15]);

	xa->fis->type = ATA_FIS_TYPE_H2D;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | 15;
	xa->fis->command = ATA_C_WRITE_PM;
	xa->fis->features = which;
	xa->fis->device = target | ATA_H2D_DEVICE_LBA;
	xa->fis->sector_count = (u_int8_t)data;
	xa->fis->lba_low = (u_int8_t)(data >> 8);
	xa->fis->lba_mid = (u_int8_t)(data >> 16);
	xa->fis->lba_high = (u_int8_t)(data >> 24);
	xa->fis->control = ATA_FIS_CONTROL_4BIT;

	xa->complete = ahci_pm_dummy_done;
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

/*
 * Dummy done callback for xa.
 */
static void
ahci_pm_dummy_done(struct ata_xfer *xa)
{
}
