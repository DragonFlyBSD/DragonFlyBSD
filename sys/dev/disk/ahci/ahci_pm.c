/*
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
static void ahci_pm_empty_done(struct ahci_ccb *ccb);

/*
 * Identify the port multiplier
 */
int
ahci_pm_identify(struct ahci_port *ap)
{
	u_int32_t chipid;
	u_int32_t rev;
	u_int32_t nports;

	kprintf("%s: pm_identify\n", PORTNAME(ap));

	ap->ap_pmcount = 0;
	ap->ap_probe = ATA_PROBE_FAILED;
	if (ahci_pm_read(ap, 15, 0, &chipid))
		return(EIO);
	if (ahci_pm_read(ap, 15, 1, &rev))
		return(EIO);
	if (ahci_pm_read(ap, 15, 2, &nports))
		return(EIO);
	ap->ap_probe = ATA_PROBE_GOOD;
	kprintf("%s: port multiplier chip=%08x rev=%08x nports=%d\n",
		PORTNAME(ap), chipid, rev, nports);
	ap->ap_pmcount = nports;
	return (0);
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

	kprintf("%s.%d: ahci_pm_hardreset\n", PORTNAME(ap), target);

	at = &ap->ap_ata[target];

	/*
	 * Turn off power management and kill the phy on the target
	 * if requested.  Hold state for 10ms.
	 */
	data = AHCI_PREG_SCTL_IPM_DISABLED;
	if (hard == 2)
		data |= AHCI_PREG_SCTL_DET_DISABLE;
	if (ahci_pm_write(ap, target, AHCI_PMREG_SCTL, data))
		return(EIO);
	DELAY(10000);

	/*
	 * Start transmitting COMRESET.  COMRESET must be sent for at
	 * least 1ms.
	 */
	at->at_probe = ATA_PROBE_FAILED;
	at->at_type = ATA_PORT_T_NONE;
	data = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_INIT;
	if (AhciForceGen1 & (1 << ap->ap_num)) {
		kprintf("%s.%d: Force 1.5GBits\n", PORTNAME(ap), target);
		data |= AHCI_PREG_SCTL_SPD_GEN1;
	} else {
		data |= AHCI_PREG_SCTL_SPD_ANY;
	}
	if (ahci_pm_write(ap, target, AHCI_PMREG_SCTL, data))
		return(EIO);
	DELAY(1000);

	/*
	 * Flush any status, then clear DET to initiate negotiation.
	 */
	ahci_pm_write(ap, target, AHCI_PMREG_SERR, -1);
	data = AHCI_PREG_SCTL_IPM_DISABLED | AHCI_PREG_SCTL_DET_NONE;
	if (ahci_pm_write(ap, target, AHCI_PMREG_SCTL, data))
		return(EIO);

	/*
	 * Try to determine if there is a device on the port.
	 *
	 * Give the device 3/10 second to at least be detected.
	 * If we fail clear any pending status since we may have
	 * cycled the phy and probably caused another PRCS interrupt.
	 */
	for (loop = 30; loop; --loop) {
		if (ahci_pm_read(ap, target, AHCI_PMREG_SSTS, &data))
			return(EIO);
		if (data & AHCI_PREG_SSTS_DET)
			break;
	}
	if (loop == 0) {
		kprintf("%s.%d: Port appears to be unplugged\n",
			PORTNAME(ap), target);
		return (ENODEV);
	}

	/*
	 * There is something on the port.  Give the device 3 seconds
	 * to fully negotiate.
	 */
	for (loop = 300; loop; --loop) {
		if (ahci_pm_read(ap, target, AHCI_PMREG_SSTS, &data))
			return(EIO);
		if ((data & AHCI_PREG_SSTS_DET) == AHCI_PREG_SSTS_DET_DEV)
			break;
		DELAY(10000);
	}
	if (loop == 0) {
		kprintf("%s: Device may be powered down\n",
			PORTNAME(ap));
		return (ENODEV);
	}

	/*
	 * Device detected!  Wait for it to become ready? XXX
	 */
	ahci_pm_write(ap, target, AHCI_PMREG_SERR, -1);
	kprintf("%s.%d: Device detected data=%08x\n",
		PORTNAME(ap), target, data);
	at->at_probe = ATA_PROBE_NEED_SOFT_RESET;
	return (0);
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
	struct ahci_ccb		*ccb = NULL;
	struct ahci_cmd_hdr	*cmd_slot;
	u_int8_t		*fis;
	int			rc, count;
	u_int32_t		data;

	rc = EIO;
	at = &ap->ap_ata[target];

	DPRINTF(AHCI_D_VERBOSE, "%s: soft reset\n", PORTNAME(ap));

	kprintf("%s: ahci_pm_softreset\n", ATANAME(ap, at));
	count = 2;

retry:
	/*
	 * Try to clear the phy so we get a good signature, otherwise
	 * the PM may not latch a new signature.
	 *
	 * NOTE: This cannot be safely done between the first and second
	 *	 softreset FISs.  It's now or never.
	 */
#if 1
	if (ahci_pm_phy_status(ap, target, &data)) {
		kprintf("%s: Cannot clear phy status\n",
			ATANAME(ap ,at));
	}
#endif

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
	ccb = ahci_get_ccb(ap);
	ccb->ccb_done = ahci_pm_empty_done;
	ccb->ccb_xa.flags = ATA_F_READ | ATA_F_POLL;
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
	ccb->ccb_xa.flags = 0;

	if (ahci_poll(ccb, hz, ahci_ata_cmd_timeout) != 0 ||
	    ccb->ccb_xa.state != ATA_S_COMPLETE) {
		kprintf("%s: First FIS failed\n", ATANAME(ap, at));
		if (--count) {
			fis[15] = 0;
			ahci_put_ccb(ccb);
			goto retry;
		}
		goto err;
	}

	/*
	 * The device may muff the PHY up.
	 */
	DELAY(100000);	/* XXX 3000 */

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
	ccb->ccb_xa.flags = 0;

	if (ahci_poll(ccb, hz, ahci_ata_cmd_timeout) != 0 ||
	    ccb->ccb_xa.state != ATA_S_COMPLETE) {
		kprintf("%s: Second FIS failed\n", ATANAME(ap, at));
		if (--count) {
			fis[15] = 0;
			ahci_put_ccb(ccb);
			goto retry;
		}
		goto err;
	}

	DELAY(10000);

	/*
	 * Do it again, even if we think we got a good result
	 */
	if (--count) {
		fis[15] = 0;
		ahci_put_ccb(ccb);
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
			rc = EBUSY; /* XXX */
		}
	}
	rc = 0;

	DELAY(3000);
err:
	/*
	 * Clean up the CCB.  If the command failed it already went through
	 * the standard timeout handling and should no longer be active.
	 */
	if (ccb) {
		KKASSERT((ap->ap_active & (1 << ccb->ccb_slot)) == 0);
		fis[15] = 0;
		ahci_put_ccb(ccb);
	}

	/*
	 * If we failed to softreset make the port quiescent, otherwise
	 * make sure the port's start/stop state matches what it was on
	 * entry.
	 *
	 * Don't kill the port if the softreset is on a port multiplier
	 * target, that would kill all the targets!
	 */
	if (rc)
		at->at_probe = ATA_PROBE_FAILED;
	else
		at->at_probe = ATA_PROBE_NEED_IDENT;

	kprintf("%s: ahci_pm_softreset done %d\n", ATANAME(ap, at), rc);

	return (rc);
}


/*
 * Return the phy status for a target behind a port multiplier and
 * reset AHCI_PMREG_SERR.
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

	error = ahci_pm_read(ap, target, AHCI_PMREG_SSTS, datap);
	if (error == 0)
		error = ahci_pm_write(ap, target, AHCI_PMREG_SERR, -1);
	if (error)
		*datap = 0;
	return(error);
}

/*
 * Read a PM register
 */
int
ahci_pm_read(struct ahci_port *ap, int target, int which, u_int32_t *datap)
{
	struct ata_xfer	*xa;
	int status;
	int error;

	xa = ahci_ata_get_xfer(ap, &ap->ap_ata[15]);

	bzero(xa->fis, sizeof(*xa->fis));
	xa->fis->type = ATA_FIS_TYPE_H2D;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | 15;
	xa->fis->command = ATA_C_READ_PM;
	xa->fis->features = which;
	xa->fis->device = target | ATA_H2D_DEVICE_LBA;
	xa->fis->control = ATA_FIS_CONTROL_4BIT;

	xa->complete = ahci_pm_dummy_done;
	xa->datalen = 0;
	xa->flags = ATA_F_READ | ATA_F_POLL;
	xa->timeout = hz;

	status = ahci_ata_cmd(xa);
	if (status == ATA_COMPLETE && xa->state == ATA_S_COMPLETE) {
		*datap = xa->rfis.sector_count | (xa->rfis.lba_low << 8) |
		       (xa->rfis.lba_mid << 16) | (xa->rfis.lba_high << 24);
		error = 0;
	} else {
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
	int status;
	int error;

	xa = ahci_ata_get_xfer(ap, &ap->ap_ata[15]);

	bzero(xa->fis, sizeof(*xa->fis));
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
	xa->flags = ATA_F_READ | ATA_F_POLL;
	xa->timeout = hz;

	status = ahci_ata_cmd(xa);
	error = (status == ATA_COMPLETE && xa->state == ATA_S_COMPLETE) ?
		0 : EIO;
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

static void
ahci_pm_empty_done(struct ahci_ccb *ccb)
{
        ccb->ccb_xa.state = ATA_S_COMPLETE;
}
