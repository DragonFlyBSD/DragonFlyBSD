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
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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
 * $OpenBSD: atascsi.c,v 1.64 2009/02/16 21:19:06 miod Exp $
 */
/*
 * Implement each SATA port as its own SCSI bus on CAM.  This way we can
 * implement future port multiplier features as individual devices on the
 * bus.
 *
 * Much of the cdb<->xa conversion code was taken from OpenBSD, the rest
 * was written natively for DragonFly.
 *
 * NOTE-1: I was temporarily unlocking the port while making the CCB
 *	   callback, to reduce the chance of a deadlock and to improve
 *	   performance by allowing new commands to be queued.
 *
 *	   However, this also creates an opening where another AHCI
 *	   interrupt can come in and execute the ahci_port_intr()
 *	   function, creating a huge mess in the sequencing of the
 *	   chipset.
 *
 *	   So for now we don't do this. XXX
 */

#include "ahci.h"

static void ahci_xpt_action(struct cam_sim *sim, union ccb *ccb);
static void ahci_xpt_poll(struct cam_sim *sim);
static void ahci_xpt_scsi_disk_io(struct ahci_port *ap,
			struct ata_port *at, union ccb *ccb);
static void ahci_xpt_scsi_atapi_io(struct ahci_port *ap,
			struct ata_port *at, union ccb *ccb);
static void ahci_xpt_page_inquiry(struct ahci_port *ap,
			struct ata_port *at, union ccb *ccb);

static void ahci_ata_complete_disk_rw(struct ata_xfer *xa);
static void ahci_ata_complete_disk_synchronize_cache(struct ata_xfer *xa);
static void ahci_atapi_complete_cmd(struct ata_xfer *xa);
static void ahci_ata_dummy_sense(struct scsi_sense_data *sense_data);
static void ahci_ata_atapi_sense(struct ata_fis_d2h *rfis,
		     struct scsi_sense_data *sense_data);

static int ahci_cam_probe_disk(struct ahci_port *ap, struct ata_port *at);
static int ahci_cam_probe_atapi(struct ahci_port *ap, struct ata_port *at);
static int ahci_set_xfer(struct ahci_port *ap, struct ata_port *atx);
static void ahci_ata_dummy_done(struct ata_xfer *xa);
static void ata_fix_identify(struct ata_identify *id);
static void ahci_cam_rescan(struct ahci_port *ap);
static void ahci_strip_string(const char **basep, int *lenp);

int
ahci_cam_attach(struct ahci_port *ap)
{
	struct cam_devq *devq;
	struct cam_sim *sim;
	int error;
	int unit;

	/*
	 * We want at least one ccb to be available for error processing
	 * so don't let CAM use more then ncmds - 1.
	 */
	unit = device_get_unit(ap->ap_sc->sc_dev);
	if (ap->ap_sc->sc_ncmds > 1)
		devq = cam_simq_alloc(ap->ap_sc->sc_ncmds - 1);
	else
		devq = cam_simq_alloc(ap->ap_sc->sc_ncmds);
	if (devq == NULL) {
		return (ENOMEM);
	}

	/*
	 * Give the devq enough room to run with 32 max_dev_transactions,
	 * but set the overall max tags to 1 until NCQ is negotiated.
	 */
	sim = cam_sim_alloc(ahci_xpt_action, ahci_xpt_poll, "ahci",
			   (void *)ap, unit, &ap->ap_sim_lock,
			   32, 1, devq);
	cam_simq_release(devq);
	if (sim == NULL) {
		return (ENOMEM);
	}
	ap->ap_sim = sim;
	ahci_os_unlock_port(ap);
	lockmgr(&ap->ap_sim_lock, LK_EXCLUSIVE);
	error = xpt_bus_register(ap->ap_sim, ap->ap_num);
	lockmgr(&ap->ap_sim_lock, LK_RELEASE);
	ahci_os_lock_port(ap);
	if (error != CAM_SUCCESS) {
		ahci_cam_detach(ap);
		return (EINVAL);
	}
	ap->ap_flags |= AP_F_BUS_REGISTERED;

	if (ap->ap_probe == ATA_PROBE_NEED_IDENT)
		error = ahci_cam_probe(ap, NULL);
	else
		error = 0;
	if (error) {
		ahci_cam_detach(ap);
		return (EIO);
	}
	ap->ap_flags |= AP_F_CAM_ATTACHED;

	return(0);
}

/*
 * The state of the port has changed.
 *
 * If atx is NULL the physical port has changed state.
 * If atx is non-NULL a particular target behind a PM has changed state.
 *
 * If found is -1 the target state must be queued to a non-interrupt context.
 * (only works with at == NULL).
 *
 * If found is 0 the target was removed.
 * If found is 1 the target was inserted.
 */
void
ahci_cam_changed(struct ahci_port *ap, struct ata_port *atx, int found)
{
	struct cam_path *tmppath;
	int status;
	int target;

	target = atx ? atx->at_target : CAM_TARGET_WILDCARD;

	if (ap->ap_sim == NULL)
		return;
	if (found == CAM_TARGET_WILDCARD) {
		status = xpt_create_path(&tmppath, NULL,
					 cam_sim_path(ap->ap_sim),
					 target, CAM_LUN_WILDCARD);
		if (status != CAM_REQ_CMP)
			return;
		ahci_cam_rescan(ap);
	} else {
		status = xpt_create_path(&tmppath, NULL,
					 cam_sim_path(ap->ap_sim),
					 target,
					 CAM_LUN_WILDCARD);
		if (status != CAM_REQ_CMP)
			return;
#if 0
		/*
		 * This confuses CAM
		 */
		if (found)
			xpt_async(AC_FOUND_DEVICE, tmppath, NULL);
		else
			xpt_async(AC_LOST_DEVICE, tmppath, NULL);
#endif
	}
	xpt_free_path(tmppath);
}

void
ahci_cam_detach(struct ahci_port *ap)
{
	int error __debugvar;

	if ((ap->ap_flags & AP_F_CAM_ATTACHED) == 0)
		return;
	lockmgr(&ap->ap_sim_lock, LK_EXCLUSIVE);
	if (ap->ap_sim) {
		xpt_freeze_simq(ap->ap_sim, 1);
	}
	if (ap->ap_flags & AP_F_BUS_REGISTERED) {
		error = xpt_bus_deregister(cam_sim_path(ap->ap_sim));
		KKASSERT(error == CAM_REQ_CMP);
		ap->ap_flags &= ~AP_F_BUS_REGISTERED;
	}
	if (ap->ap_sim) {
		cam_sim_free(ap->ap_sim);
		ap->ap_sim = NULL;
	}
	lockmgr(&ap->ap_sim_lock, LK_RELEASE);
	ap->ap_flags &= ~AP_F_CAM_ATTACHED;
}

/*
 * Once the AHCI port has been attached we need to probe for a device or
 * devices on the port and setup various options.
 *
 * If at is NULL we are probing the direct-attached device on the port,
 * which may or may not be a port multiplier.
 */
int
ahci_cam_probe(struct ahci_port *ap, struct ata_port *atx)
{
	struct ata_port	*at;
	struct ata_xfer	*xa;
	u_int64_t	capacity;
	u_int64_t	capacity_bytes;
	int		model_len;
	int		firmware_len;
	int		serial_len;
	int		error;
	int		devncqdepth;
	int		i;
	const char	*model_id;
	const char	*firmware_id;
	const char	*serial_id;
	const char	*wcstr;
	const char	*rastr;
	const char	*scstr;
	const char	*type;

	error = EIO;

	/*
	 * Delayed CAM attachment for initial probe, sim may be NULL
	 */
	if (ap->ap_sim == NULL)
		return(0);

	/*
	 * A NULL atx indicates a probe of the directly connected device.
	 * A non-NULL atx indicates a device connected via a port multiplier.
	 * We need to preserve atx for calls to ahci_ata_get_xfer().
	 *
	 * at is always non-NULL.  For directly connected devices we supply
	 * an (at) pointing to target 0.
	 */
	if (atx == NULL) {
		at = ap->ap_ata[0];	/* direct attached - device 0 */
		if (ap->ap_type == ATA_PORT_T_PM) {
			kprintf("%s: Found Port Multiplier\n",
				ATANAME(ap, atx));
			return (0);
		}
		at->at_type = ap->ap_type;
	} else {
		at = atx;
		if (atx->at_type == ATA_PORT_T_PM) {
			kprintf("%s: Bogus device, reducing port count to %d\n",
				ATANAME(ap, atx), atx->at_target);
			if (ap->ap_pmcount > atx->at_target)
				ap->ap_pmcount = atx->at_target;
			goto err;
		}
	}
	if (ap->ap_type == ATA_PORT_T_NONE)
		goto err;
	if (at->at_type == ATA_PORT_T_NONE)
		goto err;

	/*
	 * Issue identify, saving the result
	 */
	xa = ahci_ata_get_xfer(ap, atx);
	xa->complete = ahci_ata_dummy_done;
	xa->data = &at->at_identify;
	xa->datalen = sizeof(at->at_identify);
	xa->flags = ATA_F_READ | ATA_F_PIO | ATA_F_POLL;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;

	switch(at->at_type) {
	case ATA_PORT_T_DISK:
		xa->fis->command = ATA_C_IDENTIFY;
		type = "DISK";
		break;
	case ATA_PORT_T_ATAPI:
		xa->fis->command = ATA_C_ATAPI_IDENTIFY;
		xa->flags |= ATA_F_AUTOSENSE;
		type = "ATAPI";
		break;
	default:
		xa->fis->command = ATA_C_ATAPI_IDENTIFY;
		type = "UNKNOWN(ATAPI?)";
		break;
	}
	xa->fis->features = 0;
	xa->fis->device = 0;
	xa->timeout = 1000;

	if (ahci_ata_cmd(xa) != ATA_S_COMPLETE) {
		kprintf("%s: Detected %s device but unable to IDENTIFY\n",
			ATANAME(ap, atx), type);
		ahci_ata_put_xfer(xa);
		goto err;
	}
	ahci_ata_put_xfer(xa);

	ata_fix_identify(&at->at_identify);

	/*
	 * Read capacity using SATA probe info.
	 */
	if (le16toh(at->at_identify.cmdset83) & 0x0400) {
		/* LBA48 feature set supported */
		capacity = 0;
		for (i = 3; i >= 0; --i) {
			capacity <<= 16;
			capacity +=
			    le16toh(at->at_identify.addrsecxt[i]);
		}
	} else {
		capacity = le16toh(at->at_identify.addrsec[1]);
		capacity <<= 16;
		capacity += le16toh(at->at_identify.addrsec[0]);
	}
	if (capacity == 0)
		capacity = 1024 * 1024 / 512;
	at->at_capacity = capacity;
	if (atx == NULL)
		ap->ap_probe = ATA_PROBE_GOOD;

	capacity_bytes = capacity * 512;

	/*
	 * Negotiate NCQ, throw away any ata_xfer's beyond the negotiated
	 * number of slots and limit the number of CAM ccb's to one less
	 * so we always have a slot available for recovery.
	 *
	 * NCQ is not used if ap_ncqdepth is 1 or the host controller does
	 * not support it, and in that case the driver can handle extra
	 * ccb's.
	 *
	 * NCQ is currently used only with direct-attached disks.  It is
	 * not used with port multipliers or direct-attached ATAPI devices.
	 *
	 * Remember at least one extra CCB needs to be reserved for the
	 * error ccb.
	 */
	if ((ap->ap_sc->sc_cap & AHCI_REG_CAP_SNCQ) &&
	    ap->ap_type == ATA_PORT_T_DISK &&
	    (le16toh(at->at_identify.satacap) & (1 << 8))) {
		at->at_ncqdepth = (le16toh(at->at_identify.qdepth) & 0x1F) + 1;
		devncqdepth = at->at_ncqdepth;
		if (at->at_ncqdepth > ap->ap_sc->sc_ncmds)
			at->at_ncqdepth = ap->ap_sc->sc_ncmds;
		if (at->at_ncqdepth > 1) {
			for (i = 0; i < ap->ap_sc->sc_ncmds; ++i) {
				xa = ahci_ata_get_xfer(ap, atx);
				if (xa->tag < at->at_ncqdepth) {
					xa->state = ATA_S_COMPLETE;
					ahci_ata_put_xfer(xa);
				}
			}
			if (at->at_ncqdepth >= ap->ap_sc->sc_ncmds) {
				cam_sim_set_max_tags(ap->ap_sim,
						     at->at_ncqdepth - 1);
			}
		}
	} else {
		devncqdepth = 0;
	}

	model_len = sizeof(at->at_identify.model);
	model_id = at->at_identify.model;
	ahci_strip_string(&model_id, &model_len);

	firmware_len = sizeof(at->at_identify.firmware);
	firmware_id = at->at_identify.firmware;
	ahci_strip_string(&firmware_id, &firmware_len);

	serial_len = sizeof(at->at_identify.serial);
	serial_id = at->at_identify.serial;
	ahci_strip_string(&serial_id, &serial_len);

	/*
	 * Generate informatiive strings.
	 *
	 * NOTE: We do not automatically set write caching, lookahead,
	 *	 or the security state for ATAPI devices.
	 */
	if (at->at_identify.cmdset82 & ATA_IDENTIFY_WRITECACHE) {
		if (at->at_identify.features85 & ATA_IDENTIFY_WRITECACHE)
			wcstr = "enabled";
		else if (at->at_type == ATA_PORT_T_ATAPI)
			wcstr = "disabled";
		else
			wcstr = "enabling";
	} else {
		    wcstr = "notsupp";
	}

	if (at->at_identify.cmdset82 & ATA_IDENTIFY_LOOKAHEAD) {
		if (at->at_identify.features85 & ATA_IDENTIFY_LOOKAHEAD)
			rastr = "enabled";
		else if (at->at_type == ATA_PORT_T_ATAPI)
			rastr = "disabled";
		else
			rastr = "enabling";
	} else {
		    rastr = "notsupp";
	}

	if (at->at_identify.cmdset82 & ATA_IDENTIFY_SECURITY) {
		if (at->at_identify.securestatus & ATA_SECURE_FROZEN)
			scstr = "frozen";
		else if (at->at_type == ATA_PORT_T_ATAPI)
			scstr = "unfrozen";
		else if (AhciNoFeatures & (1 << ap->ap_num))
			scstr = "<disabled>";
		else
			scstr = "freezing";
	} else {
		    scstr = "notsupp";
	}

	kprintf("%s: Found %s \"%*.*s %*.*s\" serial=\"%*.*s\"\n"
		"%s: tags=%d/%d satacap=%04x satacap2=%04x satafea=%04x NCQ=%s "
		"capacity=%lld.%02dMB\n",

		ATANAME(ap, atx),
		type,
		model_len, model_len, model_id,
		firmware_len, firmware_len, firmware_id,
		serial_len, serial_len, serial_id,

		ATANAME(ap, atx),
		devncqdepth, ap->ap_sc->sc_ncmds,
		at->at_identify.satacap,
		at->at_identify.satacap2,
		at->at_identify.satafsup,
		(at->at_ncqdepth > 1 ? "YES" : "NO"),
		(long long)capacity_bytes / (1024 * 1024),
		(int)(capacity_bytes % (1024 * 1024)) * 100 / (1024 * 1024)
	);
	kprintf("%s: f85=%04x f86=%04x f87=%04x WC=%s RA=%s SEC=%s\n",
		ATANAME(ap, atx),
		at->at_identify.features85,
		at->at_identify.features86,
		at->at_identify.features87,
		wcstr,
		rastr,
		scstr
	);

	/*
	 * Additional type-specific probing
	 */
	switch(at->at_type) {
	case ATA_PORT_T_DISK:
		error = ahci_cam_probe_disk(ap, atx);
		break;
	case ATA_PORT_T_ATAPI:
		error = ahci_cam_probe_atapi(ap, atx);
		break;
	default:
		error = EIO;
		break;
	}
err:
	if (error) {
		at->at_probe = ATA_PROBE_FAILED;
		if (atx == NULL)
			ap->ap_probe = at->at_probe;
	} else {
		at->at_probe = ATA_PROBE_GOOD;
		if (atx == NULL)
			ap->ap_probe = at->at_probe;
	}
	return (error);
}

/*
 * DISK-specific probe after initial ident
 */
static int
ahci_cam_probe_disk(struct ahci_port *ap, struct ata_port *atx)
{
	struct ata_port *at;
	struct ata_xfer	*xa;

	at = atx ? atx : ap->ap_ata[0];

	/*
	 * Set dummy xfer mode
	 */
	ahci_set_xfer(ap, atx);

	/*
	 * Enable write cache if supported
	 *
	 * NOTE: "WD My Book" external disk devices have a very poor
	 *	 daughter board between the the ESATA and the HD.  Sending
	 *	 any ATA_C_SET_FEATURES commands will break the hardware port
	 *	 with a fatal protocol error.  However, this device also
	 *	 indicates that WRITECACHE is already on and READAHEAD is
	 *	 not supported so we avoid the issue.
	 */
	if ((at->at_identify.cmdset82 & ATA_IDENTIFY_WRITECACHE) &&
	    (at->at_identify.features85 & ATA_IDENTIFY_WRITECACHE) == 0) {
		xa = ahci_ata_get_xfer(ap, atx);
		xa->complete = ahci_ata_dummy_done;
		xa->fis->command = ATA_C_SET_FEATURES;
		xa->fis->features = ATA_SF_WRITECACHE_EN;
		/* xa->fis->features = ATA_SF_LOOKAHEAD_EN; */
		xa->fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;
		xa->fis->device = 0;
		xa->flags = ATA_F_PIO | ATA_F_POLL;
		xa->timeout = 1000;
		xa->datalen = 0;
		if (ahci_ata_cmd(xa) == ATA_S_COMPLETE)
			at->at_features |= ATA_PORT_F_WCACHE;
		else
			kprintf("%s: Unable to enable write-caching\n",
				ATANAME(ap, atx));
		ahci_ata_put_xfer(xa);
	}

	/*
	 * Enable readahead if supported
	 */
	if ((at->at_identify.cmdset82 & ATA_IDENTIFY_LOOKAHEAD) &&
	    (at->at_identify.features85 & ATA_IDENTIFY_LOOKAHEAD) == 0) {
		xa = ahci_ata_get_xfer(ap, atx);
		xa->complete = ahci_ata_dummy_done;
		xa->fis->command = ATA_C_SET_FEATURES;
		xa->fis->features = ATA_SF_LOOKAHEAD_EN;
		xa->fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;
		xa->fis->device = 0;
		xa->flags = ATA_F_PIO | ATA_F_POLL;
		xa->timeout = 1000;
		xa->datalen = 0;
		if (ahci_ata_cmd(xa) == ATA_S_COMPLETE)
			at->at_features |= ATA_PORT_F_RAHEAD;
		else
			kprintf("%s: Unable to enable read-ahead\n",
				ATANAME(ap, atx));
		ahci_ata_put_xfer(xa);
	}

	/*
	 * FREEZE LOCK the device so malicious users can't lock it on us.
	 * As there is no harm in issuing this to devices that don't
	 * support the security feature set we just send it, and don't bother
	 * checking if the device sends a command abort to tell us it doesn't
	 * support it
	 */
	if ((at->at_identify.cmdset82 & ATA_IDENTIFY_SECURITY) &&
	    (at->at_identify.securestatus & ATA_SECURE_FROZEN) == 0 &&
	    (AhciNoFeatures & (1 << ap->ap_num)) == 0) {
		xa = ahci_ata_get_xfer(ap, atx);
		xa->complete = ahci_ata_dummy_done;
		xa->fis->command = ATA_C_SEC_FREEZE_LOCK;
		xa->fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;
		xa->flags = ATA_F_PIO | ATA_F_POLL;
		xa->timeout = 1000;
		xa->datalen = 0;
		if (ahci_ata_cmd(xa) == ATA_S_COMPLETE)
			at->at_features |= ATA_PORT_F_FRZLCK;
		else
			kprintf("%s: Unable to set security freeze\n",
				ATANAME(ap, atx));
		ahci_ata_put_xfer(xa);
	}

	return (0);
}

/*
 * ATAPI-specific probe after initial ident
 */
static int
ahci_cam_probe_atapi(struct ahci_port *ap, struct ata_port *atx)
{
	ahci_set_xfer(ap, atx);
	return(0);
}

/*
 * Setting the transfer mode is irrelevant for the SATA transport
 * but some (atapi) devices seem to need it anyway.  In addition
 * if we are running through a SATA->PATA converter for some reason
 * beyond my comprehension we might have to set the mode.
 *
 * We only support DMA modes for SATA attached devices, so don't bother
 * with legacy modes.
 */
static int
ahci_set_xfer(struct ahci_port *ap, struct ata_port *atx)
{
	struct ata_port *at;
	struct ata_xfer	*xa;
	u_int16_t mode;
	u_int16_t mask;

	at = atx ? atx : ap->ap_ata[0];

	/*
	 * Figure out the supported UDMA mode.  Ignore other legacy modes.
	 */
	mask = le16toh(at->at_identify.ultradma);
	if ((mask & 0xFF) == 0 || mask == 0xFFFF)
		return(0);
	mask &= 0xFF;
	mode = 0x4F;
	while ((mask & 0x8000) == 0) {
		mask <<= 1;
		--mode;
	}

	/*
	 * SATA atapi devices often still report a dma mode, even though
	 * it is irrelevant for SATA transport.  It is also possible that
	 * we are running through a SATA->PATA converter and seeing the
	 * PATA dma mode.
	 *
	 * In this case the device may require a (dummy) SETXFER to be
	 * sent before it will work properly.
	 */
	xa = ahci_ata_get_xfer(ap, atx);
	xa->complete = ahci_ata_dummy_done;
	xa->fis->command = ATA_C_SET_FEATURES;
	xa->fis->features = ATA_SF_SETXFER;
	xa->fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;
	xa->fis->sector_count = mode;
	xa->flags = ATA_F_PIO | ATA_F_POLL;
	xa->timeout = 1000;
	xa->datalen = 0;
	if (ahci_ata_cmd(xa) != ATA_S_COMPLETE) {
		kprintf("%s: Unable to set dummy xfer mode \n",
			ATANAME(ap, atx));
	} else if (bootverbose) {
		kprintf("%s: Set dummy xfer mode to %02x\n",
			ATANAME(ap, atx), mode);
	}
	ahci_ata_put_xfer(xa);
	return(0);
}

/*
 * Fix byte ordering so buffers can be accessed as
 * strings.
 */
static void
ata_fix_identify(struct ata_identify *id)
{
	u_int16_t	*swap;
	int		i;

	swap = (u_int16_t *)id->serial;
	for (i = 0; i < sizeof(id->serial) / sizeof(u_int16_t); i++)
		swap[i] = bswap16(swap[i]);

	swap = (u_int16_t *)id->firmware;
	for (i = 0; i < sizeof(id->firmware) / sizeof(u_int16_t); i++)
		swap[i] = bswap16(swap[i]);

	swap = (u_int16_t *)id->model;
	for (i = 0; i < sizeof(id->model) / sizeof(u_int16_t); i++)
		swap[i] = bswap16(swap[i]);
}

/*
 * Dummy done callback for xa.
 */
static void
ahci_ata_dummy_done(struct ata_xfer *xa)
{
}

/*
 * Use an engineering request to initiate a target scan for devices
 * behind a port multiplier.
 *
 * An asynchronous bus scan is used to avoid reentrancy issues.
 */
static void
ahci_cam_rescan_callback(struct cam_periph *periph, union ccb *ccb)
{
	struct ahci_port *ap = ccb->ccb_h.sim_priv.entries[0].ptr;

	if (ccb->ccb_h.func_code == XPT_SCAN_BUS) {
		ap->ap_flags &= ~AP_F_SCAN_RUNNING;
		if (ap->ap_flags & AP_F_SCAN_REQUESTED) {
			ap->ap_flags &= ~AP_F_SCAN_REQUESTED;
			ahci_cam_rescan(ap);
		}
		ap->ap_flags |= AP_F_SCAN_COMPLETED;
		wakeup(&ap->ap_flags);
	}
	xpt_free_ccb(ccb);
}

static void
ahci_cam_rescan(struct ahci_port *ap)
{
	struct cam_path *path;
	union ccb *ccb;
	int status;
	int i;

	if (ap->ap_flags & AP_F_SCAN_RUNNING) {
		ap->ap_flags |= AP_F_SCAN_REQUESTED;
		return;
	}
	ap->ap_flags |= AP_F_SCAN_RUNNING;
	for (i = 0; i < AHCI_MAX_PMPORTS; ++i) {
		ap->ap_ata[i]->at_features |= ATA_PORT_F_RESCAN;
	}

	status = xpt_create_path(&path, xpt_periph, cam_sim_path(ap->ap_sim),
				 CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP)
		return;

	ccb = xpt_alloc_ccb();
	xpt_setup_ccb(&ccb->ccb_h, path, 5);	/* 5 = low priority */
	ccb->ccb_h.func_code = XPT_ENG_EXEC;
	ccb->ccb_h.cbfcnp = ahci_cam_rescan_callback;
	ccb->ccb_h.sim_priv.entries[0].ptr = ap;
	ccb->crcn.flags = CAM_FLAG_NONE;
	xpt_action_async(ccb);
}

static void
ahci_xpt_rescan(struct ahci_port *ap)
{
	struct cam_path *path;
	union ccb *ccb;
	int status;

	status = xpt_create_path(&path, xpt_periph, cam_sim_path(ap->ap_sim),
				 CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP)
		return;

	ccb = xpt_alloc_ccb();
	xpt_setup_ccb(&ccb->ccb_h, path, 5);	/* 5 = low priority */
	ccb->ccb_h.func_code = XPT_SCAN_BUS;
	ccb->ccb_h.cbfcnp = ahci_cam_rescan_callback;
	ccb->ccb_h.sim_priv.entries[0].ptr = ap;
	ccb->crcn.flags = CAM_FLAG_NONE;
	xpt_action_async(ccb);
}

/*
 * Action function - dispatch command
 */
static
void
ahci_xpt_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ahci_port *ap;
	struct ata_port	 *at, *atx;
	struct ccb_hdr *ccbh;

	/* XXX lock */
	ap = cam_sim_softc(sim);
	atx = NULL;
	KKASSERT(ap != NULL);
	ccbh = &ccb->ccb_h;

	/*
	 * Early failure checks.  These checks do not apply to XPT_PATH_INQ,
	 * otherwise the bus rescan will not remove the dead devices when
	 * unplugging a PM.
	 *
	 * For non-wildcards we have one target (0) and one lun (0),
	 * unless we have a port multiplier.
	 *
	 * A wildcard target indicates only the general bus is being
	 * probed.
	 *
	 * Calculate at and atx.  at is always non-NULL.  atx is only
	 * NULL for direct-attached devices.  It will be non-NULL for
	 * devices behind a port multiplier.
	 *
	 * XXX What do we do with a LUN wildcard?
	 */
	if (ccbh->target_id != CAM_TARGET_WILDCARD &&
	    ccbh->func_code != XPT_PATH_INQ) {
		if (ap->ap_type == ATA_PORT_T_NONE) {
			ccbh->status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}
		if (ccbh->target_id < 0 || ccbh->target_id >= ap->ap_pmcount) {
			ccbh->status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}
		at = ap->ap_ata[ccbh->target_id];
		if (ap->ap_type == ATA_PORT_T_PM)
			atx = at;

		if (ccbh->target_lun != CAM_LUN_WILDCARD && ccbh->target_lun) {
			ccbh->status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}
	} else {
		at = ap->ap_ata[0];
	}

	/*
	 * Switch on the meta XPT command
	 */
	switch(ccbh->func_code) {
	case XPT_ENG_EXEC:
		/*
		 * This routine is called after a port multiplier has been
		 * probed.
		 */
		ccbh->status = CAM_REQ_CMP;
		ahci_os_lock_port(ap);
		ahci_port_state_machine(ap, 0);
		ahci_os_unlock_port(ap);
		xpt_done(ccb);
		ahci_xpt_rescan(ap);
		break;
	case XPT_PATH_INQ:
		/*
		 * This command always succeeds, otherwise the bus scan
		 * will not detach dead devices.
		 */
		ccb->cpi.version_num = 1;
		ccb->cpi.hba_inquiry = 0;
		ccb->cpi.target_sprt = 0;
		ccb->cpi.hba_misc = PIM_SEQSCAN;
		ccb->cpi.hba_eng_cnt = 0;
		bzero(ccb->cpi.vuhba_flags, sizeof(ccb->cpi.vuhba_flags));
		ccb->cpi.max_target = AHCI_MAX_PMPORTS - 1;
		ccb->cpi.max_lun = 0;
		ccb->cpi.async_flags = 0;
		ccb->cpi.hpath_id = 0;
		ccb->cpi.initiator_id = AHCI_MAX_PMPORTS - 1;
		ccb->cpi.unit_number = cam_sim_unit(sim);
		ccb->cpi.bus_id = cam_sim_bus(sim);
		ccb->cpi.base_transfer_speed = 150000;
		ccb->cpi.transport = XPORT_SATA;
		ccb->cpi.transport_version = 1;
		ccb->cpi.protocol = PROTO_SCSI;
		ccb->cpi.protocol_version = SCSI_REV_2;
		ccb->cpi.maxio = AHCI_MAXPHYS;

		ccbh->status = CAM_REQ_CMP;
		if (ccbh->target_id == CAM_TARGET_WILDCARD) {
			ahci_os_lock_port(ap);
			ahci_port_state_machine(ap, 0);
			ahci_os_unlock_port(ap);
		} else {
			switch(ahci_pread(ap, AHCI_PREG_SSTS) &
			       AHCI_PREG_SSTS_SPD) {
			case AHCI_PREG_SSTS_SPD_GEN1:
				ccb->cpi.base_transfer_speed = 150000;
				break;
			case AHCI_PREG_SSTS_SPD_GEN2:
				ccb->cpi.base_transfer_speed = 300000;
				break;
			case AHCI_PREG_SSTS_SPD_GEN3:
				ccb->cpi.base_transfer_speed = 600000;
				break;
			default:
				/* unknown */
				ccb->cpi.base_transfer_speed = 1000;
				break;
			}
#if 0
			if (ap->ap_type == ATA_PORT_T_NONE)
				ccbh->status = CAM_DEV_NOT_THERE;
#endif
		}
		xpt_done(ccb);
		break;
	case XPT_RESET_DEV:
		ahci_os_lock_port(ap);
		if (ap->ap_type == ATA_PORT_T_NONE) {
			ccbh->status = CAM_DEV_NOT_THERE;
		} else {
			ahci_port_reset(ap, atx, 0);
			ccbh->status = CAM_REQ_CMP;
		}
		ahci_os_unlock_port(ap);
		xpt_done(ccb);
		break;
	case XPT_RESET_BUS:
		ahci_os_lock_port(ap);
		ahci_port_reset(ap, NULL, 1);
		ahci_os_unlock_port(ap);
		ccbh->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	case XPT_SET_TRAN_SETTINGS:
		ccbh->status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		break;
	case XPT_GET_TRAN_SETTINGS:
		ccb->cts.protocol = PROTO_SCSI;
		ccb->cts.protocol_version = SCSI_REV_2;
		ccb->cts.transport = XPORT_SATA;
		ccb->cts.transport_version = XPORT_VERSION_UNSPECIFIED;
		ccb->cts.proto_specific.valid = 0;
		ccb->cts.xport_specific.valid = 0;
		ccbh->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, 1);
		xpt_done(ccb);
		break;
	case XPT_SCSI_IO:
		/*
		 * Our parallel startup code might have only probed through
		 * to the IDENT, so do the last step if necessary.
		 */
		if (at->at_probe == ATA_PROBE_NEED_IDENT)
			ahci_cam_probe(ap, atx);
		if (at->at_probe != ATA_PROBE_GOOD) {
			ccbh->status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			break;
		}
		switch(at->at_type) {
		case ATA_PORT_T_DISK:
			ahci_xpt_scsi_disk_io(ap, atx, ccb);
			break;
		case ATA_PORT_T_ATAPI:
			ahci_xpt_scsi_atapi_io(ap, atx, ccb);
			break;
		default:
			ccbh->status = CAM_REQ_INVALID;
			xpt_done(ccb);
			break;
		}
		break;
	case XPT_TRIM:
	{
		scsi_cdb_t cdb;
		struct ccb_scsiio *csio;
		csio = &ccb->csio;
		cdb = (void *)((ccbh->flags & CAM_CDB_POINTER) ?
		    csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes);
		cdb->generic.opcode = TRIM;
		ahci_xpt_scsi_disk_io(ap, atx, ccb);
		break;
	}
	default:
		ccbh->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	}
}

/*
 * Poll function.
 *
 * Generally this function gets called heavily when interrupts might be
 * non-operational, during a halt/reboot or panic.
 */
static
void
ahci_xpt_poll(struct cam_sim *sim)
{
	struct ahci_port *ap;

	ap = cam_sim_softc(sim);
	crit_enter();
	ahci_os_lock_port(ap);
	ahci_port_intr(ap, 1);
	ahci_os_unlock_port(ap);
	crit_exit();
}

/*
 * Convert the SCSI command in ccb to an ata_xfer command in xa
 * for ATA_PORT_T_DISK operations.  Set the completion function
 * to convert the response back, then dispatch to the OpenBSD AHCI
 * layer.
 *
 * AHCI DISK commands only support a limited command set, and we
 * fake additional commands to make it play nice with the CAM subsystem.
 */
static
void
ahci_xpt_scsi_disk_io(struct ahci_port *ap, struct ata_port *atx,
		      union ccb *ccb)
{
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	struct ata_xfer *xa;
	struct ata_port	*at;
	struct ata_fis_h2d *fis;
	struct ata_pass_12 *atp12;
	struct ata_pass_16 *atp16;
	scsi_cdb_t cdb;
	union scsi_data *rdata;
	int rdata_len;
	u_int64_t capacity;
	u_int64_t lba;
	u_int32_t count;

	ccbh = &ccb->csio.ccb_h;
	csio = &ccb->csio;
	at = atx ? atx : ap->ap_ata[0];

	/*
	 * XXX not passing NULL at for direct attach!
	 */
	xa = ahci_ata_get_xfer(ap, atx);
	rdata = (void *)csio->data_ptr;
	rdata_len = csio->dxfer_len;

	/*
	 * Build the FIS or process the csio to completion.
	 */
	cdb = (void *)((ccbh->flags & CAM_CDB_POINTER) ?
			csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes);

	switch(cdb->generic.opcode) {
	case REQUEST_SENSE:
		/*
		 * Auto-sense everything, so explicit sense requests
		 * return no-sense.
		 */
		ccbh->status = CAM_SCSI_STATUS_ERROR;
		break;
	case INQUIRY:
		/*
		 * Inquiry supported features
		 *
		 * [opcode, byte2, page_code, length, control]
		 */
		if (cdb->inquiry.byte2 & SI_EVPD) {
			ahci_xpt_page_inquiry(ap, at, ccb);
		} else {
			bzero(rdata, rdata_len);
			if (rdata_len < SHORT_INQUIRY_LENGTH) {
				ccbh->status = CAM_CCB_LEN_ERR;
				break;
			}
			if (rdata_len > sizeof(rdata->inquiry_data))
				rdata_len = sizeof(rdata->inquiry_data);
			rdata->inquiry_data.device = T_DIRECT;
			rdata->inquiry_data.version = SCSI_REV_SPC2;
			rdata->inquiry_data.response_format = 2;
			rdata->inquiry_data.additional_length = 32;
			bcopy("SATA    ", rdata->inquiry_data.vendor, 8);
			bcopy(at->at_identify.model,
			      rdata->inquiry_data.product,
			      sizeof(rdata->inquiry_data.product));
			bcopy(at->at_identify.firmware,
			      rdata->inquiry_data.revision,
			      sizeof(rdata->inquiry_data.revision));
			ccbh->status = CAM_REQ_CMP;
		}
		
		/*
		 * Use the vendor specific area to set the TRIM status
		 * for scsi_da
		 */
		if (at->at_identify.support_dsm) {
			rdata->inquiry_data.vendor_specific1[0] =
			    at->at_identify.support_dsm &ATA_SUPPORT_DSM_TRIM;
			rdata->inquiry_data.vendor_specific1[1] = 
			    at->at_identify.max_dsm_blocks;
		}
		break;
	case READ_CAPACITY_16:
		if (cdb->read_capacity_16.service_action != SRC16_SERVICE_ACTION) {
			ccbh->status = CAM_REQ_INVALID;
			break;
		}
		if (rdata_len < sizeof(rdata->read_capacity_data_16)) {
			ccbh->status = CAM_CCB_LEN_ERR;
			break;
		}
		/* fall through */
	case READ_CAPACITY:
		if (rdata_len < sizeof(rdata->read_capacity_data)) {
			ccbh->status = CAM_CCB_LEN_ERR;
			break;
		}

		capacity = at->at_capacity;

		bzero(rdata, rdata_len);
		if (cdb->generic.opcode == READ_CAPACITY) {
			rdata_len = sizeof(rdata->read_capacity_data);
			if (capacity > 0xFFFFFFFFU) {
				/*
				 * Set capacity to 0 so maxsector winds up
				 * being 0xffffffff in CAM in order to trigger
				 * DA_STATE_PROBE2.
				 */
				capacity = 0;
			}
			bzero(&rdata->read_capacity_data, rdata_len);
			scsi_ulto4b((u_int32_t)capacity - 1,
				    rdata->read_capacity_data.addr);
			scsi_ulto4b(512, rdata->read_capacity_data.length);
		} else {
			rdata_len = sizeof(rdata->read_capacity_data_16);
			bzero(&rdata->read_capacity_data_16, rdata_len);
			scsi_u64to8b(capacity - 1,
				     rdata->read_capacity_data_16.addr);
			scsi_ulto4b(512, rdata->read_capacity_data_16.length);
		}
		ccbh->status = CAM_REQ_CMP;
		break;
	case SYNCHRONIZE_CACHE:
		/*
		 * Synchronize cache.  Specification says this can take
		 * greater then 30 seconds so give it at least 45.
		 */
		fis = xa->fis;
		fis->flags = ATA_H2D_FLAGS_CMD;
		fis->command = ATA_C_FLUSH_CACHE;
		fis->device = 0;
		if (xa->timeout < 45000)
			xa->timeout = 45000;
		xa->datalen = 0;
		xa->flags = 0;
		xa->complete = ahci_ata_complete_disk_synchronize_cache;
		break;
	case TRIM:
		fis = xa->fis;
		fis->command = ATA_C_DATA_SET_MANAGEMENT;
		fis->features = (u_int8_t)ATA_SF_DSM_TRIM;
		fis->features_exp = (u_int8_t)(ATA_SF_DSM_TRIM>> 8);

		xa->flags = ATA_F_WRITE;
		fis->flags = ATA_H2D_FLAGS_CMD;

		xa->data = csio->data_ptr;
		xa->datalen = csio->dxfer_len;
		xa->timeout = ccbh->timeout*50;	/* milliseconds */

		fis->sector_count =(u_int8_t)(xa->datalen/512);
		fis->sector_count_exp =(u_int8_t)((xa->datalen/512)>>8);

		lba = 0;
		fis->lba_low = (u_int8_t)lba;
		fis->lba_mid = (u_int8_t)(lba >> 8);
		fis->lba_high = (u_int8_t)(lba >> 16);
		fis->lba_low_exp = (u_int8_t)(lba >> 24);
		fis->lba_mid_exp = (u_int8_t)(lba >> 32);
		fis->lba_high_exp = (u_int8_t)(lba >> 40);
		   
		fis->device = ATA_H2D_DEVICE_LBA;
		xa->data = csio->data_ptr;

		xa->complete = ahci_ata_complete_disk_rw;
		ccbh->status = CAM_REQ_INPROG;
		break;
	case TEST_UNIT_READY:
	case START_STOP_UNIT:
	case PREVENT_ALLOW:
		/*
		 * Just silently return success
		 */
		ccbh->status = CAM_REQ_CMP;
		rdata_len = 0;
		break;
	case ATA_PASS_12:
		atp12 = &cdb->ata_pass_12;
		fis = xa->fis;
		/*
		 * Figure out the flags to be used, depending on the direction of the
		 * CAM request.
		 */
		switch (ccbh->flags & CAM_DIR_MASK) {
		case CAM_DIR_IN:
			xa->flags = ATA_F_READ;
			break;
		case CAM_DIR_OUT:
			xa->flags = ATA_F_WRITE;
			break;
		default:
			xa->flags = 0;
		}
		xa->flags |= ATA_F_POLL | ATA_F_EXCLUSIVE;
		xa->data = csio->data_ptr;
		xa->datalen = csio->dxfer_len;
		xa->complete = ahci_ata_complete_disk_rw;
		xa->timeout = ccbh->timeout;

		/*
		 * Populate the fis from the information we received through CAM
		 * ATA passthrough.
		 */
		fis->flags = ATA_H2D_FLAGS_CMD;	/* maybe also atp12->flags ? */
		fis->features = atp12->features;
		fis->sector_count = atp12->sector_count;
		fis->lba_low = atp12->lba_low;
		fis->lba_mid = atp12->lba_mid;
		fis->lba_high = atp12->lba_high;
		fis->device = atp12->device;	/* maybe always 0? */
		fis->command = atp12->command;
		fis->control = atp12->control;

		/*
		 * Mark as in progress so it is sent to the device.
		 */
		ccbh->status = CAM_REQ_INPROG;
		break;
	case ATA_PASS_16:
		atp16 = &cdb->ata_pass_16;
		fis = xa->fis;
		/*
		 * Figure out the flags to be used, depending on the direction of the
		 * CAM request.
		 */
		switch (ccbh->flags & CAM_DIR_MASK) {
		case CAM_DIR_IN:
			xa->flags = ATA_F_READ;
			break;
		case CAM_DIR_OUT:
			xa->flags = ATA_F_WRITE;
			break;
		default:
			xa->flags = 0;
		}
		xa->flags |= ATA_F_POLL | ATA_F_EXCLUSIVE;
		xa->data = csio->data_ptr;
		xa->datalen = csio->dxfer_len;
		xa->complete = ahci_ata_complete_disk_rw;
		xa->timeout = ccbh->timeout;

		/*
		 * Populate the fis from the information we received through CAM
		 * ATA passthrough.
		 */
		fis->flags = ATA_H2D_FLAGS_CMD;	/* maybe also atp16->flags ? */
		fis->features = atp16->features;
		fis->features_exp = atp16->features_ext;
		fis->sector_count = atp16->sector_count;
		fis->sector_count_exp = atp16->sector_count_ext;
		fis->lba_low = atp16->lba_low;
		fis->lba_low_exp = atp16->lba_low_ext;
		fis->lba_mid = atp16->lba_mid;
		fis->lba_mid_exp = atp16->lba_mid_ext;
		fis->lba_high = atp16->lba_high;
		fis->lba_mid_exp = atp16->lba_mid_ext;
		fis->device = atp16->device;	/* maybe always 0? */
		fis->command = atp16->command;

		/*
		 * Mark as in progress so it is sent to the device.
		 */
		ccbh->status = CAM_REQ_INPROG;
		break;
	default:
		switch(cdb->generic.opcode) {
		case READ_6:
			lba = scsi_3btoul(cdb->rw_6.addr) & 0x1FFFFF;
			count = cdb->rw_6.length ? cdb->rw_6.length : 0x100;
			xa->flags = ATA_F_READ;
			break;
		case READ_10:
			lba = scsi_4btoul(cdb->rw_10.addr);
			count = scsi_2btoul(cdb->rw_10.length);
			xa->flags = ATA_F_READ;
			break;
		case READ_12:
			lba = scsi_4btoul(cdb->rw_12.addr);
			count = scsi_4btoul(cdb->rw_12.length);
			xa->flags = ATA_F_READ;
			break;
		case READ_16:
			lba = scsi_8btou64(cdb->rw_16.addr);
			count = scsi_4btoul(cdb->rw_16.length);
			xa->flags = ATA_F_READ;
			break;
		case WRITE_6:
			lba = scsi_3btoul(cdb->rw_6.addr) & 0x1FFFFF;
			count = cdb->rw_6.length ? cdb->rw_6.length : 0x100;
			xa->flags = ATA_F_WRITE;
			break;
		case WRITE_10:
			lba = scsi_4btoul(cdb->rw_10.addr);
			count = scsi_2btoul(cdb->rw_10.length);
			xa->flags = ATA_F_WRITE;
			break;
		case WRITE_12:
			lba = scsi_4btoul(cdb->rw_12.addr);
			count = scsi_4btoul(cdb->rw_12.length);
			xa->flags = ATA_F_WRITE;
			break;
		case WRITE_16:
			lba = scsi_8btou64(cdb->rw_16.addr);
			count = scsi_4btoul(cdb->rw_16.length);
			xa->flags = ATA_F_WRITE;
			break;
		default:
			ccbh->status = CAM_REQ_INVALID;
			break;
		}
		if (ccbh->status != CAM_REQ_INPROG)
			break;

		fis = xa->fis;
		fis->flags = ATA_H2D_FLAGS_CMD;
		fis->lba_low = (u_int8_t)lba;
		fis->lba_mid = (u_int8_t)(lba >> 8);
		fis->lba_high = (u_int8_t)(lba >> 16);
		fis->device = ATA_H2D_DEVICE_LBA;

		/*
		 * NCQ only for direct-attached disks, do not currently
		 * try to use NCQ with port multipliers.
		 */
		if (at->at_ncqdepth > 1 &&
		    ap->ap_type == ATA_PORT_T_DISK &&
		    (ap->ap_sc->sc_cap & AHCI_REG_CAP_SNCQ) &&
		    (ccbh->flags & CAM_POLLED) == 0) {
			/*
			 * Use NCQ - always uses 48 bit addressing
			 */
			xa->flags |= ATA_F_NCQ;
			fis->command = (xa->flags & ATA_F_WRITE) ?
					ATA_C_WRITE_FPDMA : ATA_C_READ_FPDMA;
			fis->lba_low_exp = (u_int8_t)(lba >> 24);
			fis->lba_mid_exp = (u_int8_t)(lba >> 32);
			fis->lba_high_exp = (u_int8_t)(lba >> 40);
			fis->sector_count = xa->tag << 3;
			fis->features = (u_int8_t)count;
			fis->features_exp = (u_int8_t)(count >> 8);
		} else if (count > 0x100 || lba > 0x0FFFFFFFU) {
			/*
			 * Use LBA48
			 */
			fis->command = (xa->flags & ATA_F_WRITE) ?
					ATA_C_WRITEDMA_EXT : ATA_C_READDMA_EXT;
			fis->lba_low_exp = (u_int8_t)(lba >> 24);
			fis->lba_mid_exp = (u_int8_t)(lba >> 32);
			fis->lba_high_exp = (u_int8_t)(lba >> 40);
			fis->sector_count = (u_int8_t)count;
			fis->sector_count_exp = (u_int8_t)(count >> 8);
		} else {
			/*
			 * Use LBA
			 *
			 * NOTE: 256 sectors is supported, stored as 0.
			 */
			fis->command = (xa->flags & ATA_F_WRITE) ?
					ATA_C_WRITEDMA : ATA_C_READDMA;
			fis->device |= (u_int8_t)(lba >> 24) & 0x0F;
			fis->sector_count = (u_int8_t)count;
		}

		xa->data = csio->data_ptr;
		xa->datalen = csio->dxfer_len;
		xa->complete = ahci_ata_complete_disk_rw;
		xa->timeout = ccbh->timeout;	/* milliseconds */
#if 0
		if (xa->timeout > 10000)	/* XXX - debug */
			xa->timeout = 10000;
#endif
		if (ccbh->flags & CAM_POLLED)
			xa->flags |= ATA_F_POLL;
		break;
	}

	/*
	 * If the request is still in progress the xa and FIS have
	 * been set up (except for the PM target), and must be dispatched.
	 * Otherwise the request was completed.
	 */
	if (ccbh->status == CAM_REQ_INPROG) {
		KKASSERT(xa->complete != NULL);
		xa->atascsi_private = ccb;
		ccb->ccb_h.sim_priv.entries[0].ptr = ap;
		ahci_os_lock_port(ap);
		xa->fis->flags |= at->at_target;
		ahci_ata_cmd(xa);
		ahci_os_unlock_port(ap);
	} else {
		ahci_ata_put_xfer(xa);
		xpt_done(ccb);
	}
}

/*
 * Convert the SCSI command in ccb to an ata_xfer command in xa
 * for ATA_PORT_T_ATAPI operations.  Set the completion function
 * to convert the response back, then dispatch to the OpenBSD AHCI
 * layer.
 */
static
void
ahci_xpt_scsi_atapi_io(struct ahci_port *ap, struct ata_port *atx,
			union ccb *ccb)
{
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	struct ata_xfer *xa;
	struct ata_fis_h2d *fis;
	scsi_cdb_t cdbs;
	scsi_cdb_t cdbd;
	int flags;
	struct ata_port	*at;

	ccbh = &ccb->csio.ccb_h;
	csio = &ccb->csio;
	at = atx ? atx : ap->ap_ata[0];

	switch (ccbh->flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		flags = ATA_F_PACKET | ATA_F_READ;
		break;
	case CAM_DIR_OUT:
		flags = ATA_F_PACKET | ATA_F_WRITE;
		break;
	case CAM_DIR_NONE:
		flags = ATA_F_PACKET;
		break;
	default:
		ccbh->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		return;
		/* NOT REACHED */
	}

	/*
	 * Special handling to get the rfis back into host memory while
	 * still allowing the chip to run commands in parallel to
	 * ATAPI devices behind a PM.
	 */
	flags |= ATA_F_AUTOSENSE;

	/*
	 * The command has to fit in the packet command buffer.
	 */
	if (csio->cdb_len < 6 || csio->cdb_len > 16) {
		ccbh->status = CAM_CCB_LEN_ERR;
		xpt_done(ccb);
		return;
	}

	/*
	 * Initialize the XA and FIS.  It is unclear how much of
	 * this has to mimic the equivalent ATA command.
	 *
	 * XXX not passing NULL at for direct attach!
	 */
	xa = ahci_ata_get_xfer(ap, atx);
	fis = xa->fis;

	fis->flags = ATA_H2D_FLAGS_CMD | at->at_target;
	fis->command = ATA_C_PACKET;
	fis->device = ATA_H2D_DEVICE_LBA;
	fis->sector_count = xa->tag << 3;
	if (flags & (ATA_F_READ | ATA_F_WRITE)) {
		if (flags & ATA_F_WRITE) {
			fis->features = ATA_H2D_FEATURES_DMA |
				       ATA_H2D_FEATURES_DIR_WRITE;
		} else {
			fis->features = ATA_H2D_FEATURES_DMA |
				       ATA_H2D_FEATURES_DIR_READ;
		}
	} else {
		fis->lba_mid = 0;
		fis->lba_high = 0;
	}
	fis->control = ATA_FIS_CONTROL_4BIT;

	xa->flags = flags;
	xa->data = csio->data_ptr;
	xa->datalen = csio->dxfer_len;
	xa->timeout = ccbh->timeout;	/* milliseconds */

	if (ccbh->flags & CAM_POLLED)
		xa->flags |= ATA_F_POLL;

	/*
	 * Copy the cdb to the packetcmd buffer in the FIS using a
	 * convenient pointer in the xa.
	 *
	 * Zero-out any trailing bytes in case the ATAPI device cares.
	 */
	cdbs = (void *)((ccbh->flags & CAM_CDB_POINTER) ?
			csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes);
	bcopy(cdbs, xa->packetcmd, csio->cdb_len);
	if (csio->cdb_len < 16)
		bzero(xa->packetcmd + csio->cdb_len, 16 - csio->cdb_len);

#if 0
	kprintf("opcode %d cdb_len %d dxfer_len %d\n",
		cdbs->generic.opcode,
		csio->cdb_len, csio->dxfer_len);
#endif

	/*
	 * Some ATAPI commands do not actually follow the SCSI standard.
	 */
	cdbd = (void *)xa->packetcmd;

	switch(cdbd->generic.opcode) {
	case REQUEST_SENSE:
		/*
		 * Force SENSE requests to the ATAPI sense length.
		 *
		 * It is unclear if this is needed or not.
		 */
		if (cdbd->sense.length == SSD_FULL_SIZE) {
			if (bootverbose) {
				kprintf("%s: Shortening sense request\n",
					PORTNAME(ap));
			}
			cdbd->sense.length = offsetof(struct scsi_sense_data,
						      extra_bytes[0]);
		}
		break;
	case INQUIRY:
		/*
		 * Some ATAPI devices can't handle long inquiry lengths,
		 * don't ask me why.  Truncate the inquiry length.
		 */
		if (cdbd->inquiry.page_code == 0 &&
		    cdbd->inquiry.length > SHORT_INQUIRY_LENGTH) {
			cdbd->inquiry.length = SHORT_INQUIRY_LENGTH;
		}
		break;
	case READ_6:
	case WRITE_6:
		/*
		 * Convert *_6 to *_10 commands.  Most ATAPI devices
		 * cannot handle the SCSI READ_6 and WRITE_6 commands.
		 */
		cdbd->rw_10.opcode |= 0x20;
		cdbd->rw_10.byte2 = 0;
		cdbd->rw_10.addr[0] = cdbs->rw_6.addr[0] & 0x1F;
		cdbd->rw_10.addr[1] = cdbs->rw_6.addr[1];
		cdbd->rw_10.addr[2] = cdbs->rw_6.addr[2];
		cdbd->rw_10.addr[3] = 0;
		cdbd->rw_10.reserved = 0;
		cdbd->rw_10.length[0] = 0;
		cdbd->rw_10.length[1] = cdbs->rw_6.length;
		cdbd->rw_10.control = cdbs->rw_6.control;
		break;
	default:
		break;
	}

	/*
	 * And dispatch
	 */
	xa->complete = ahci_atapi_complete_cmd;
	xa->atascsi_private = ccb;
	ccb->ccb_h.sim_priv.entries[0].ptr = ap;
	ahci_os_lock_port(ap);
	ahci_ata_cmd(xa);
	ahci_os_unlock_port(ap);
}

/*
 * Simulate page inquiries for disk attachments.
 */
static
void
ahci_xpt_page_inquiry(struct ahci_port *ap, struct ata_port *at, union ccb *ccb)
{
	union {
		struct scsi_vpd_supported_page_list	list;
		struct scsi_vpd_unit_serial_number	serno;
		struct scsi_vpd_unit_devid		devid;
		char					buf[256];
	} *page;
	scsi_cdb_t cdb;
	int i;
	int j;
	int len;

	page = kmalloc(sizeof(*page), M_DEVBUF, M_WAITOK | M_ZERO);

	cdb = (void *)((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
			ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes);

	switch(cdb->inquiry.page_code) {
	case SVPD_SUPPORTED_PAGE_LIST:
		i = 0;
		page->list.device = T_DIRECT;
		page->list.page_code = SVPD_SUPPORTED_PAGE_LIST;
		page->list.list[i++] = SVPD_SUPPORTED_PAGE_LIST;
		page->list.list[i++] = SVPD_UNIT_SERIAL_NUMBER;
		page->list.list[i++] = SVPD_UNIT_DEVID;
		page->list.length = i;
		len = offsetof(struct scsi_vpd_supported_page_list, list[3]);
		break;
	case SVPD_UNIT_SERIAL_NUMBER:
		i = 0;
		j = sizeof(at->at_identify.serial);
		for (i = 0; i < j && at->at_identify.serial[i] == ' '; ++i)
			;
		while (j > i && at->at_identify.serial[j-1] == ' ')
			--j;
		page->serno.device = T_DIRECT;
		page->serno.page_code = SVPD_UNIT_SERIAL_NUMBER;
		page->serno.length = j - i;
		bcopy(at->at_identify.serial + i,
		      page->serno.serial_num, j - i);
		len = offsetof(struct scsi_vpd_unit_serial_number,
			       serial_num[j-i]);
		break;
	case SVPD_UNIT_DEVID:
		/* fall through for now */
	default:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		len = 0;
		break;
	}
	if (ccb->ccb_h.status == CAM_REQ_INPROG) {
		if (len <= ccb->csio.dxfer_len) {
			ccb->ccb_h.status = CAM_REQ_CMP;
			bzero(ccb->csio.data_ptr, ccb->csio.dxfer_len);
			bcopy(page, ccb->csio.data_ptr, len);
			ccb->csio.resid = ccb->csio.dxfer_len - len;
		} else {
			ccb->ccb_h.status = CAM_CCB_LEN_ERR;
		}
	}
	kfree(page, M_DEVBUF);
}

/*
 * Completion function for ATA_PORT_T_DISK cache synchronization.
 */
static
void
ahci_ata_complete_disk_synchronize_cache(struct ata_xfer *xa)
{
	union ccb *ccb = xa->atascsi_private;
	struct ccb_hdr *ccbh = &ccb->ccb_h;
	struct ahci_port *ap = ccb->ccb_h.sim_priv.entries[0].ptr;

	switch(xa->state) {
	case ATA_S_COMPLETE:
		ccbh->status = CAM_REQ_CMP;
		ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case ATA_S_ERROR:
		kprintf("%s: synchronize_cache: error\n",
			ATANAME(ap, xa->at));
		ccbh->status = CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_dummy_sense(&ccb->csio.sense_data);
		break;
	case ATA_S_TIMEOUT:
		kprintf("%s: synchronize_cache: timeout\n",
			ATANAME(ap, xa->at));
		ccbh->status = CAM_CMD_TIMEOUT;
		break;
	default:
		kprintf("%s: synchronize_cache: unknown state %d\n",
			ATANAME(ap, xa->at), xa->state);
		panic("%s: Unknown state", ATANAME(ap, xa->at));
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}
	ahci_ata_put_xfer(xa);
	/*ahci_os_unlock_port(ap); ILLEGAL SEE NOTE-1 AT TOP */
	xpt_done(ccb);
	/*ahci_os_lock_port(ap);*/
}

/*
 * Completion function for ATA_PORT_T_DISK I/O
 */
static
void
ahci_ata_complete_disk_rw(struct ata_xfer *xa)
{
	union ccb *ccb = xa->atascsi_private;
	struct ccb_hdr *ccbh = &ccb->ccb_h;
	struct ahci_port *ap = ccb->ccb_h.sim_priv.entries[0].ptr;

	switch(xa->state) {
	case ATA_S_COMPLETE:
		ccbh->status = CAM_REQ_CMP;
		ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case ATA_S_ERROR:
		kprintf("%s: disk_rw: error\n", ATANAME(ap, xa->at));
		ccbh->status = CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_dummy_sense(&ccb->csio.sense_data);
		break;
	case ATA_S_TIMEOUT:
		kprintf("%s: disk_rw: timeout\n", ATANAME(ap, xa->at));
		ccbh->status = CAM_CMD_TIMEOUT;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_dummy_sense(&ccb->csio.sense_data);
		break;
	default:
		kprintf("%s: disk_rw: unknown state %d\n",
			ATANAME(ap, xa->at), xa->state);
		panic("%s: Unknown state", ATANAME(ap, xa->at));
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}
	ccb->csio.resid = xa->resid;
	ahci_ata_put_xfer(xa);
	/*ahci_os_unlock_port(ap); ILLEGAL SEE NOTE-1 AT TOP */
	xpt_done(ccb);
	/*ahci_os_lock_port(ap);*/
}

/*
 * Completion function for ATA_PORT_T_ATAPI I/O
 *
 * Sense data is returned in the rfis.
 */
static
void
ahci_atapi_complete_cmd(struct ata_xfer *xa)
{
	union ccb *ccb = xa->atascsi_private;
	struct ccb_hdr *ccbh = &ccb->ccb_h;
	struct ahci_port *ap = ccb->ccb_h.sim_priv.entries[0].ptr;
	scsi_cdb_t cdb;

	cdb = (void *)((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
			ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes);

	switch(xa->state) {
	case ATA_S_COMPLETE:
		ccbh->status = CAM_REQ_CMP;
		ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case ATA_S_ERROR:
		ccbh->status = CAM_SCSI_STATUS_ERROR;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_atapi_sense(&xa->rfis, &ccb->csio.sense_data);
		break;
	case ATA_S_TIMEOUT:
		kprintf("%s: cmd %d: timeout\n",
			PORTNAME(ap), cdb->generic.opcode);
		ccbh->status = CAM_CMD_TIMEOUT;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_dummy_sense(&ccb->csio.sense_data);
		break;
	default:
		kprintf("%s: cmd %d: unknown state %d\n",
			PORTNAME(ap), cdb->generic.opcode, xa->state);
		panic("%s: Unknown state", PORTNAME(ap));
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}
	ccb->csio.resid = xa->resid;
	xa->atascsi_private = NULL;
	ahci_ata_put_xfer(xa);
	/*ahci_os_unlock_port(ap); ILLEGAL SEE NOTE-1 AT TOP */
	xpt_done(ccb);
	/*ahci_os_lock_port(ap);*/
}

/*
 * Construct dummy sense data for errors on DISKs
 */
static
void
ahci_ata_dummy_sense(struct scsi_sense_data *sense_data)
{
	sense_data->error_code = SSD_ERRCODE_VALID | SSD_CURRENT_ERROR;
	sense_data->segment = 0;
	sense_data->flags = SSD_KEY_MEDIUM_ERROR;
	sense_data->info[0] = 0;
	sense_data->info[1] = 0;
	sense_data->info[2] = 0;
	sense_data->info[3] = 0;
	sense_data->extra_len = 0;
}

/*
 * Construct atapi sense data for errors on ATAPI
 *
 * The ATAPI sense data is stored in the passed rfis and must be converted
 * to SCSI sense data.
 */
static
void
ahci_ata_atapi_sense(struct ata_fis_d2h *rfis,
		     struct scsi_sense_data *sense_data)
{
	sense_data->error_code = SSD_ERRCODE_VALID | SSD_CURRENT_ERROR;
	sense_data->segment = 0;
	sense_data->flags = (rfis->error & 0xF0) >> 4;
	if (rfis->error & 0x04)
		sense_data->flags |= SSD_KEY_ILLEGAL_REQUEST;
	if (rfis->error & 0x02)
		sense_data->flags |= SSD_EOM;
	if (rfis->error & 0x01)
		sense_data->flags |= SSD_ILI;
	sense_data->info[0] = 0;
	sense_data->info[1] = 0;
	sense_data->info[2] = 0;
	sense_data->info[3] = 0;
	sense_data->extra_len = 0;
}

static
void
ahci_strip_string(const char **basep, int *lenp)
{
	const char *base = *basep;
	int len = *lenp;

	while (len && (*base == 0 || *base == ' ')) {
		--len;
		++base;
	}
	while (len && (base[len-1] == 0 || base[len-1] == ' '))
		--len;
	*basep = base;
	*lenp = len;
}
