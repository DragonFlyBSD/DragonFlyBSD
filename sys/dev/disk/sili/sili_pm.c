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

#include "sili.h"

static void sili_pm_dummy_done(struct ata_xfer *xa);
static void sili_pm_empty_done(struct sili_ccb *ccb);

/*
 * This is called for PM attachments and hot-plug insertion events, and
 * typically not called again until after an unplug/replug sequence.
 *
 * We just fall through to the hard-reset code, we don't need to
 * set up any initial conditions.
 */
int
sili_pm_port_init(struct sili_port *ap, struct ata_port *at)
{
	at->at_probe = ATA_PROBE_NEED_HARD_RESET;
	return (0);
}

/*
 * This is called from the port hardreset code.
 */
int
sili_pm_port_probe(struct sili_port *ap, int orig_error)
{
	struct ata_port *at;
	int error;
	int i;

	/*
	 * Clean up the port state machine
	 */
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_PMA);
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_INIT);
	if (sili_pwait_clr_to(ap, 5000, SILI_PREG_STATUS, SILI_PREG_CTL_INIT)) {
		kprintf("%s: PM probe: unable to init port\n",
			PORTNAME(ap));
		return (EBUSY);
	}
	if (sili_pwait_set(ap, SILI_PREG_STATUS, SILI_PREG_STATUS_READY)) {
		kprintf("%s: PM probe: port will not come ready\n",
			PORTNAME(ap));
		return (EBUSY);
	}

	/*
	 * Issue a soft-reset of target 15
	 */
	ap->ap_state = AP_S_NORMAL;
	sili_pwrite(ap, SILI_PREG_SERR, -1);
	error = sili_pm_softreset(ap, 15);

	if (error == 0)
		error = sili_pm_identify(ap);

	/*
	 * Finalize.  If the softreset failed.  Re-init the port
	 * state machine again so the normal non-PM softreset does
	 * not bog down.
	 */
	if (error == 0) {
		for (i = 0; i < SILI_MAX_PMPORTS; ++i) {
			at = &ap->ap_ata[i];
			at->at_probe = ATA_PROBE_NEED_INIT;
			at->at_features |= ATA_PORT_F_RESCAN;
			at->at_features &= ~ATA_PORT_F_READLOG;
		}
		ap->ap_type = ATA_PORT_T_PM;
		return (0);
	}

	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_PMA);
	sili_port_init(ap);
	if (orig_error == 0) {
		if (sili_pwait_set_to(ap, 5000, SILI_PREG_STATUS,
				      SILI_PREG_STATUS_READY)) {
			kprintf("%s: PM probe: port will not come ready\n",
				PORTNAME(ap));
			orig_error = EBUSY;
		}
	}
	return (orig_error);

#if 0
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_RESUME);
	sili_pwrite(ap, SILI_PREG_CTL_CLR, SILI_PREG_CTL_PMA);
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_INIT);
	if (sili_pwait_clr_to(ap, 5000, SILI_PREG_STATUS, SILI_PREG_CTL_INIT)) {
		kprintf("%s: PM probe: unable to init port\n",
			PORTNAME(ap));
		orig_error = EBUSY;
	}
	if (sili_pwait_set(ap, SILI_PREG_STATUS, SILI_PREG_STATUS_READY)) {
		kprintf("%s: PM probe: port will not come ready\n",
			PORTNAME(ap));
		orig_error = EBUSY;
	}
	kprintf("ORIG ERROR %d\n", orig_error);
	if (orig_error)
		return (orig_error);

	/*
	 * If we originally detected a device redo the device reset to
	 * try to clear the mess.
	 */
	sili_pwrite(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_DEVRESET);
	if (sili_pwait_clr(ap, SILI_PREG_CTL_SET, SILI_PREG_CTL_DEVRESET)) {
		kprintf("%s: PM probe: unable to reset\n", PORTNAME(ap));
		orig_error = EBUSY;
	}
	if (sili_pwait_set(ap, SILI_PREG_STATUS, SILI_PREG_STATUS_READY)) {
		kprintf("%s: PM probe: port will not come ready\n",
			PORTNAME(ap));
		orig_error = EBUSY;
	}
	return (orig_error);
#endif
}

/*
 * Identify the port multiplier
 */
int
sili_pm_identify(struct sili_port *ap)
{
	u_int32_t chipid;
	u_int32_t rev;
	u_int32_t nports;
	u_int32_t data1;
	u_int32_t data2;
	int	  has_dummy_port;

	ap->ap_probe = ATA_PROBE_FAILED;
	if (sili_pm_read(ap, 15, 0, &chipid))
		goto err;
	if (sili_pm_read(ap, 15, 1, &rev))
		goto err;
	if (sili_pm_read(ap, 15, 2, &nports))
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

	if (sili_pm_read(ap, 15, SATA_PMREG_FEA, &data1)) {
		kprintf("%s: Port multiplier: Warning, "
			"cannot read feature register\n", PORTNAME(ap));
	} else {
		kprintf("%s: Port multiplier features: 0x%b\n",
			PORTNAME(ap),
			data1,
			SATA_PFMT_PM_FEA);
	}
	if (sili_pm_read(ap, 15, SATA_PMREG_FEAEN, &data2) == 0) {
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
	if ((ap->ap_sc->sc_flags & SILI_F_SSNTF) &&
	    (data1 & SATA_PMFEA_ASYNCNOTIFY)) {
		u_int32_t serr_bits = SATA_PM_SERR_DIAG_N |
				      SATA_PM_SERR_DIAG_X;
		data2 |= SATA_PMFEA_ASYNCNOTIFY;
		if (sili_pm_write(ap, 15, SATA_PMREG_FEAEN, data2)) {
			kprintf("%s: Port multiplier: AsyncNotify cannot be "
				"enabled\n", PORTNAME(ap));
		} else if (sili_pm_write(ap, 15, SATA_PMREG_EEENA, serr_bits)) {
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
sili_pm_hardreset(struct sili_port *ap, int target, int hard)
{
	struct ata_port *at;
	u_int32_t data;
	int loop;
	int error = EIO;

	at = &ap->ap_ata[target];

	/*
	 * Ensure that no other commands are pending.  Our HW reset of
	 * the PM target can skewer the port overall!
	 */
	sili_exclusive_access(ap);

	/*
	 * Turn off power management and kill the phy on the target
	 * if requested.  Hold state for 10ms.
	 */
	data = SATA_PM_SCTL_IPM_DISABLED;
#if 0
	if (hard == 2)
		data |= SATA_PM_SCTL_DET_DISABLE;
#endif
	if (sili_pm_write(ap, target, SATA_PMREG_SERR, -1))
		goto err;
	if (sili_pm_write(ap, target, SATA_PMREG_SCTL, data))
		goto err;
	sili_os_sleep(10);

	/*
	 * Start transmitting COMRESET.  COMRESET must be sent for at
	 * least 1ms.
	 *
	 * It takes about 100ms for the DET logic to settle down,
	 * from trial and error testing.  If this is too short
	 * the softreset code will fail.
	 *
	 * It is very important to allow the logic to settle before
	 * we issue any additional commands or the target will interfere
	 * with our PM commands.
	 */
	at->at_probe = ATA_PROBE_FAILED;
	at->at_type = ATA_PORT_T_NONE;
	data = SATA_PM_SCTL_IPM_DISABLED | SATA_PM_SCTL_DET_INIT;
	if (SiliForceGen1 & (1 << ap->ap_num)) {
		kprintf("%s.%d: Force 1.5GBits\n", PORTNAME(ap), target);
		data |= SATA_PM_SCTL_SPD_GEN1;
	} else {
		data |= SATA_PM_SCTL_SPD_ANY;
	}
	if (sili_pm_write(ap, target, SATA_PMREG_SCTL, data))
		goto err;
	sili_os_sleep(100);

	if (sili_pm_phy_status(ap, target, &data)) {
		kprintf("%s: (A)Cannot clear phy status\n",
			ATANAME(ap ,at));
	}

	/*
	 * Flush any status, then clear DET to initiate negotiation.
	 *
	 * It is very important to allow the negotiation to settle before
	 * we issue any additional commands or the target will interfere
	 * with our PM commands.
	 */
	sili_pm_write(ap, target, SATA_PMREG_SERR, -1);
	data = SATA_PM_SCTL_IPM_DISABLED | SATA_PM_SCTL_DET_NONE;
	if (sili_pm_write(ap, target, SATA_PMREG_SCTL, data))
		goto err;
	sili_os_sleep(100);

	/*
	 * Try to determine if there is a device on the port.
	 *
	 * Give the device 3/10 second to at least be detected.
	 * If we fail clear any pending status since we may have
	 * cycled the phy and probably caused another PRCS interrupt.
	 */
	for (loop = 3; loop; --loop) {
		if (sili_pm_read(ap, target, SATA_PMREG_SSTS, &data))
			goto err;
		if (data & SATA_PM_SSTS_DET)
			break;
		sili_os_sleep(100);
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
		if (sili_pm_read(ap, target, SATA_PMREG_SSTS, &data))
			goto err;
		if ((data & SATA_PM_SSTS_DET) == SATA_PM_SSTS_DET_DEV)
			break;
		sili_os_sleep(100);
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
	 * Device detected.
	 *
	 * Wait 200ms to give the device time to send its first D2H FIS.
	 * If we do not wait long enough our softreset sequence can collide
	 * with the end of the device's reset sequence and brick the port.
	 * Some devices may need longer and we handle those cases in the
	 * pm softreset code.
	 *
	 * XXX Looks like we have to wait a lot longer.  If the Sili chip's
	 *     softreset fails due to a collision with the D2H FIS or the
	 *     unbusying it bricks the port.
	 *
	 * XXX how do we poll that particular target's BSY status via the
	 *     PM?
	 */
	kprintf("%s.%d: PM Device detected ssts=%08x\n",
		PORTNAME(ap), target, data);
	sili_os_sleep(5000);

	error = 0;
err:
	at->at_probe = error ? ATA_PROBE_FAILED : ATA_PROBE_NEED_SOFT_RESET;
	return (error);
}

/*
 * SILI soft reset through port multiplier.
 *
 * This function generates a soft reset through the port multiplier,
 * keeping port communications intact.
 *
 * The SII chip will do the whole mess for us.  However, the command
 * can brick the port if the target is still busy from the previous
 * COMRESET.
 */
int
sili_pm_softreset(struct sili_port *ap, int target)
{
	struct ata_port		*at;
	struct sili_ccb		*ccb;
	struct sili_prb		*prb;
	int			error;
	u_int32_t		data;
	u_int32_t		sig;
	int			timeout;

	error = EIO;
	at = &ap->ap_ata[target];

	kprintf("%s: PM softreset\n", ATANAME(ap, at));

	/*
	 * Prep the special soft-reset SII command.
	 */
	ccb = sili_get_err_ccb(ap);
	ccb->ccb_done = sili_pm_empty_done;
	ccb->ccb_xa.flags = ATA_F_POLL | ATA_F_EXCLUSIVE | ATA_F_AUTOSENSE;
	ccb->ccb_xa.complete = sili_pm_dummy_done;
	ccb->ccb_xa.at = at;

	prb = ccb->ccb_prb;
	bzero(&prb->prb_h2d, sizeof(prb->prb_h2d));
	prb->prb_h2d.flags = at->at_target;
	prb->prb_control = SILI_PRB_CTRL_SOFTRESET;
	prb->prb_override = 0;
	prb->prb_xfer_count = 0;

	ccb->ccb_xa.state = ATA_S_PENDING;

	timeout = (target == 15) ? 1000 : 8000;

	/*
	 * NOTE: Must use sili_quick_timeout() because we hold the err_ccb
	 */
	if (sili_poll(ccb, timeout, sili_quick_timeout) != ATA_S_COMPLETE) {
		if (target != 15) {
			kprintf("%s: (PM) Softreset FIS failed\n",
				ATANAME(ap, at));
		}
		sili_put_err_ccb(ccb);
		goto err;
	}

	sig = (prb->prb_d2h.lba_high << 24) |
	      (prb->prb_d2h.lba_mid << 16) |
	      (prb->prb_d2h.lba_low << 8) |
	      (prb->prb_d2h.sector_count);
	kprintf("%s: PM SOFTRESET SIGNATURE %08x\n", ATANAME(ap, at), sig);

	sili_put_err_ccb(ccb);

	/*
	 * Clear the phy status of the target so we can get a new event.
	 *
	 * Target 15 is the PM itself and these registers have
	 * different meanings.
	 */
	if (target != 15) {
		if (sili_pm_phy_status(ap, target, &data)) {
			kprintf("%s: (C)Cannot clear phy status\n",
				ATANAME(ap ,at));
		}
		sili_pm_write(ap, target, SATA_PMREG_SERR, -1);
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
		at->at_type = sili_port_signature(ap, at, sig);
	} else {
		if (sili_port_signature(ap, at, sig) != at->at_type) {
			kprintf("%s: device signature unexpectedly "
				"changed\n", ATANAME(ap, at));
			error = EBUSY; /* XXX */
		}
	}
	error = 0;
err:
	/*
	 * Clear error status so we can detect removal.
	 *
	 * Target 15 is the PM itself and these registers have
	 * different meanings.
	 */
	kprintf("%s: PM softreset done error %d\n", ATANAME(ap, at), error);
	if (error == 0 && target != 15) {
		if (sili_pm_write(ap, target, SATA_PMREG_SERR, -1)) {
			kprintf("%s: sili_pm_softreset unable to clear SERR\n",
				ATANAME(ap, at));
			ap->ap_flags &= ~AP_F_IGNORE_IFS;
		}
	}

	at->at_probe = error ? ATA_PROBE_FAILED : ATA_PROBE_NEED_IDENT;
	return (error);
}


/*
 * Return the phy status for a target behind a port multiplier and
 * reset SATA_PMREG_SERR.
 *
 * Returned bits follow SILI_PREG_SSTS bits.  The SILI_PREG_SSTS_SPD
 * bits can be used to determine the link speed and will be 0 if there
 * is no link.
 *
 * 0 is returned if any communications error occurs.
 */
int
sili_pm_phy_status(struct sili_port *ap, int target, u_int32_t *datap)
{
	int error;

	error = sili_pm_read(ap, target, SATA_PMREG_SSTS, datap);
	if (error == 0)
		error = sili_pm_write(ap, target, SATA_PMREG_SERR, -1);
	if (error)
		*datap = 0;
	return(error);
}

int
sili_pm_set_feature(struct sili_port *ap, int feature, int enable)
{
	struct ata_xfer	*xa;
	int error;

	xa = sili_ata_get_xfer(ap, &ap->ap_ata[15]);

	xa->fis->type = ATA_FIS_TYPE_H2D;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | 15;
	xa->fis->command = enable ? ATA_C_SATA_FEATURE_ENA :
				    ATA_C_SATA_FEATURE_DIS;
	xa->fis->sector_count = feature;
	xa->fis->control = ATA_FIS_CONTROL_4BIT;

	xa->complete = sili_pm_dummy_done;
	xa->datalen = 0;
	xa->flags = ATA_F_POLL | ATA_F_EXCLUSIVE;
	xa->timeout = 1000;

	if (sili_ata_cmd(xa) == ATA_S_COMPLETE)
		error = 0;
	else
		error = EIO;
	sili_ata_put_xfer(xa);
	return(error);
}

/*
 * Check that a target is still good.
 */
void
sili_pm_check_good(struct sili_port *ap, int target)
{
	struct ata_port *at;
	u_int32_t data;

	/*
	 * It looks like we might have to read the EINFO register
	 * to allow the PM to generate a new event.
	 */
	if (sili_pm_read(ap, 15, SATA_PMREG_EINFO, &data)) {
		kprintf("%s: Port multiplier EINFO could not be read\n",
			PORTNAME(ap));
	}

	if (sili_pm_write(ap, target, SATA_PMREG_SERR, -1)) {
		kprintf("%s: Port multiplier: SERR could not be cleared\n",
			PORTNAME(ap));
	}

	if (target == CAM_TARGET_WILDCARD || target >= ap->ap_pmcount)
		return;
	at = &ap->ap_ata[target];

	/*
	 * If the device needs an init or hard reset also make sure the
	 * PHY is turned on.
	 */
	if (at->at_probe <= ATA_PROBE_NEED_HARD_RESET) {
		/*kprintf("%s DOHARD\n", ATANAME(ap, at));*/
		sili_pm_hardreset(ap, target, 1);
	}

	/*
	 * Read the detect status
	 */
	if (sili_pm_read(ap, target, SATA_PMREG_SSTS, &data)) {
		kprintf("%s: Unable to access PM SSTS register target %d\n",
			PORTNAME(ap), target);
		return;
	}
	if ((data & SATA_PM_SSTS_DET) != SATA_PM_SSTS_DET_DEV) {
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
sili_pm_read(struct sili_port *ap, int target, int which, u_int32_t *datap)
{
	struct ata_xfer	*xa;
	int error;

	xa = sili_ata_get_xfer(ap, &ap->ap_ata[15]);

	xa->fis->type = ATA_FIS_TYPE_H2D;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | 15;
	xa->fis->command = ATA_C_READ_PM;
	xa->fis->features = which;
	xa->fis->device = target | ATA_H2D_DEVICE_LBA;
	xa->fis->control = ATA_FIS_CONTROL_4BIT;

	xa->complete = sili_pm_dummy_done;
	xa->datalen = 0;
	xa->flags = ATA_F_POLL | ATA_F_AUTOSENSE;
	xa->timeout = 1000;

	if (sili_ata_cmd(xa) == ATA_S_COMPLETE) {
		*datap = xa->rfis->sector_count | (xa->rfis->lba_low << 8) |
		       (xa->rfis->lba_mid << 16) | (xa->rfis->lba_high << 24);
		error = 0;
	} else {
		kprintf("%s.%d pm_read SCA[%d] failed\n",
			PORTNAME(ap), target, which);
		*datap = 0;
		error = EIO;
	}
	sili_ata_put_xfer(xa);
	return (error);
}

/*
 * Write a PM register
 */
int
sili_pm_write(struct sili_port *ap, int target, int which, u_int32_t data)
{
	struct ata_xfer	*xa;
	int error;

	xa = sili_ata_get_xfer(ap, &ap->ap_ata[15]);

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

	xa->complete = sili_pm_dummy_done;
	xa->datalen = 0;
	xa->flags = ATA_F_POLL | ATA_F_EXCLUSIVE;
	xa->timeout = 1000;

	if (sili_ata_cmd(xa) == ATA_S_COMPLETE)
		error = 0;
	else
		error = EIO;
	sili_ata_put_xfer(xa);
	return(error);
}

/*
 * Dummy done callback for xa.
 */
static void
sili_pm_dummy_done(struct ata_xfer *xa)
{
}

static void
sili_pm_empty_done(struct sili_ccb *ccb)
{
}
