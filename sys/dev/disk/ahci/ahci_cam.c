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
 * $DragonFly$
 */
/*
 * Implement each SATA port as its own SCSI bus on CAM.  This way we can
 * implement future port multiplier features as individual devices on the
 * bus.
 *
 * Much of the cdb<->xa conversion code was taken from OpenBSD, the rest
 * was written natively for DragonFly.
 */

#include "ahci.h"

const char *ScsiTypeArray[32] = {
	"DIRECT",
	"SEQUENTIAL",
	"PRINTER",
	"PROCESSOR",
	"WORM",
	"CDROM",
	"SCANNER",
	"OPTICAL",
	"CHANGER",
	"COMM",
	"ASC0",
	"ASC1",
	"STORARRAY",
	"ENCLOSURE",
	"RBC",
	"OCRW",
	"0x10",
	"OSD",
	"ADC",
	"0x13",
	"0x14",
	"0x15",
	"0x16",
	"0x17",
	"0x18",
	"0x19",
	"0x1A",
	"0x1B",
	"0x1C",
	"0x1D",
	"0x1E",
	"NODEVICE"
};

static void ahci_xpt_action(struct cam_sim *sim, union ccb *ccb);
static void ahci_xpt_poll(struct cam_sim *sim);
static void ahci_xpt_scsi_disk_io(struct cam_sim *sim, union ccb *ccb);
static void ahci_xpt_scsi_atapi_io(struct cam_sim *sim, union ccb *ccb);

static void ahci_ata_complete_disk_rw(struct ata_xfer *xa);
static void ahci_ata_complete_disk_synchronize_cache(struct ata_xfer *xa);
static void ahci_atapi_complete_cmd(struct ata_xfer *xa);
static void ahci_ata_dummy_sense(struct scsi_sense_data *sense_data);
static void ahci_ata_atapi_sense(struct ata_fis_d2h *rfis,
		     struct scsi_sense_data *sense_data);

static int ahci_cam_probe(struct ahci_port *ap);
static int ahci_cam_probe_disk(struct ahci_port *ap);
static int ahci_cam_probe_atapi(struct ahci_port *ap);
static void ahci_ata_dummy_done(struct ata_xfer *xa);
static void ata_fix_identify(struct ata_identify *id);
static void ahci_cam_rescan(struct ahci_port *ap);

int
ahci_cam_attach(struct ahci_port *ap)
{
	struct cam_devq *devq;
	struct cam_sim *sim;
	int error;
	int unit;

	unit = device_get_unit(ap->ap_sc->sc_dev);
	devq = cam_simq_alloc(ap->ap_sc->sc_ncmds);
	if (devq == NULL) {
		return (ENOMEM);
	}
	sim = cam_sim_alloc(ahci_xpt_action, ahci_xpt_poll, "ahci",
			   (void *)ap, unit, &sim_mplock, 1, 1, devq);
	cam_simq_release(devq);
	if (sim == NULL) {
		return (ENOMEM);
	}
	ap->ap_sim = sim;
	error = xpt_bus_register(ap->ap_sim, ap->ap_num);
	if (error != CAM_SUCCESS) {
		ahci_cam_detach(ap);
		return (EINVAL);
	}
	ap->ap_flags |= AP_F_BUS_REGISTERED;
	error = xpt_create_path(&ap->ap_path, NULL, cam_sim_path(sim),
				CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (error != CAM_REQ_CMP) {
		ahci_cam_detach(ap);
		return (ENOMEM);
	}

	error = ahci_cam_probe(ap);
	if (error) {
		ahci_cam_detach(ap);
		return (EIO);
	}
	ap->ap_flags |= AP_F_CAM_ATTACHED;

	ahci_cam_rescan(ap);

	return(0);
}

void
ahci_cam_changed(struct ahci_port *ap, int found)
{
	struct cam_path *tmppath;

	if (ap->ap_sim == NULL)
		return;
	if (xpt_create_path(&tmppath, NULL, cam_sim_path(ap->ap_sim),
			    0, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		return;
	}
	if (found) {
		ahci_cam_probe(ap);
		/*
		 * XXX calling AC_FOUND_DEVICE with inquiry data is
		 *     basically a NOP.  For now just tell CAM to
		 *     rescan the bus.
		 */
		xpt_async(AC_FOUND_DEVICE, tmppath, NULL);
		ahci_cam_rescan(ap);
	} else {
		xpt_async(AC_LOST_DEVICE, tmppath, NULL);
	}
	xpt_free_path(tmppath);
}

void
ahci_cam_detach(struct ahci_port *ap)
{
	int error;

	if ((ap->ap_flags & AP_F_CAM_ATTACHED) == 0)
		return;
	get_mplock();
	if (ap->ap_sim) {
		xpt_freeze_simq(ap->ap_sim, 1);
	}
	if (ap->ap_path) {
		xpt_free_path(ap->ap_path);
		ap->ap_path = NULL;
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
	rel_mplock();
	ap->ap_flags &= ~AP_F_CAM_ATTACHED;
}

/*
 * Once the AHCI port has been attched we need to probe for a device or
 * devices on the port and setup various options.
 */
static int
ahci_cam_probe(struct ahci_port *ap)
{
	struct ata_xfer	*xa;
	u_int64_t	capacity;
	u_int64_t	capacity_bytes;
	int		model_len;
	int		status;
	int		error;
	int		devncqdepth;
	int		i;
	const char	*wcstr;
	const char	*rastr;
	const char	*scstr;
	const char	*type;

	if (ap->ap_ata.ap_type == ATA_PORT_T_NONE)
		return (EIO);

	/*
	 * Issue identify, saving the result
	 */
	xa = ahci_ata_get_xfer(ap);
	xa->complete = ahci_ata_dummy_done;
	xa->data = &ap->ap_ata.ap_identify;
	xa->datalen = sizeof(ap->ap_ata.ap_identify);
	xa->fis->flags = ATA_H2D_FLAGS_CMD;
	if (ap->ap_ata.ap_type == ATA_PORT_T_ATAPI) {
		xa->fis->command = ATA_C_ATAPI_IDENTIFY;
		type = "ATAPI";
	} else {
		xa->fis->command = ATA_C_IDENTIFY;
		type = "DISK";
	}
	xa->fis->features = 0;
	xa->fis->device = 0;
	xa->flags = ATA_F_READ | ATA_F_PIO | ATA_F_POLL;
	xa->timeout = hz;

	status = ahci_ata_cmd(xa);
	if (status != ATA_COMPLETE) {
		kprintf("%s: Detected %s device but unable to IDENTIFY\n",
			PORTNAME(ap), type);
		ahci_ata_put_xfer(xa);
		return(EIO);
	}
	if (xa->state != ATA_S_COMPLETE) {
		kprintf("%s: Detected %s device but unable to IDENTIFY "
			" xa->state=%d\n",
			PORTNAME(ap), type, xa->state);
		ahci_ata_put_xfer(xa);
		return(EIO);
	}
	ahci_ata_put_xfer(xa);

	ata_fix_identify(&ap->ap_ata.ap_identify);

	/*
	 * Read capacity using SATA probe info.
	 */
	if (le16toh(ap->ap_ata.ap_identify.cmdset83) & 0x0400) {
		/* LBA48 feature set supported */
		capacity = 0;
		for (i = 3; i >= 0; --i) {
			capacity <<= 16;
			capacity +=
			    le16toh(ap->ap_ata.ap_identify.addrsecxt[i]);
		}
	} else {
		capacity = le16toh(ap->ap_ata.ap_identify.addrsec[1]);
		capacity <<= 16;
		capacity += le16toh(ap->ap_ata.ap_identify.addrsec[0]);
	}
	ap->ap_ata.ap_capacity = capacity;
	ap->ap_ata.ap_features |= ATA_PORT_F_PROBED;

	capacity_bytes = capacity * 512;

	/*
	 * Negotiate NCQ, throw away any ata_xfer's beyond the negotiated
	 * number of slots and limit the number of CAM ccb's to one less
	 * so we always have a slot available for recovery.
	 *
	 * NCQ is not used if ap_ncqdepth is 1 or the host controller does
	 * not support it, and in that case the driver can handle extra
	 * ccb's.
	 */
	if ((ap->ap_sc->sc_cap & AHCI_REG_CAP_SNCQ) &&
	    (le16toh(ap->ap_ata.ap_identify.satacap) & (1 << 8))) {
		ap->ap_ata.ap_ncqdepth = (le16toh(ap->ap_ata.ap_identify.qdepth) & 0x1F) + 1;
		devncqdepth = ap->ap_ata.ap_ncqdepth;
		if (ap->ap_ata.ap_ncqdepth > ap->ap_sc->sc_ncmds)
			ap->ap_ata.ap_ncqdepth = ap->ap_sc->sc_ncmds;
		for (i = 0; i < ap->ap_sc->sc_ncmds; ++i) {
			xa = ahci_ata_get_xfer(ap);
			if (xa->tag < ap->ap_ata.ap_ncqdepth) {
				xa->state = ATA_S_COMPLETE;
				ahci_ata_put_xfer(xa);
			}
		}
		if (ap->ap_ata.ap_ncqdepth > 1 &&
		    ap->ap_ata.ap_ncqdepth >= ap->ap_sc->sc_ncmds) {
			cam_devq_resize(ap->ap_sim->devq,
					ap->ap_ata.ap_ncqdepth - 1);
		}
	} else {
		devncqdepth = 0;
	}

	/*
	 * Make the model string a bit more presentable
	 */
	for (model_len = 40; model_len; --model_len) {
		if (ap->ap_ata.ap_identify.model[model_len-1] == ' ')
			continue;
		if (ap->ap_ata.ap_identify.model[model_len-1] == 0)
			continue;
		break;
	}

	/*
	 * Generate informatiive strings.
	 *
	 * NOTE: We do not automatically set write caching, lookahead,
	 *	 or the security state for ATAPI devices.
	 */
	if (ap->ap_ata.ap_identify.cmdset82 & ATA_IDENTIFY_WRITECACHE) {
		if (ap->ap_ata.ap_identify.features85 & ATA_IDENTIFY_WRITECACHE)
			wcstr = "enabled";
		else if (ap->ap_ata.ap_type == ATA_PORT_T_ATAPI)
			wcstr = "disabled";
		else
			wcstr = "enabling";
	} else {
		    wcstr = "notsupp";
	}

	if (ap->ap_ata.ap_identify.cmdset82 & ATA_IDENTIFY_LOOKAHEAD) {
		if (ap->ap_ata.ap_identify.features85 & ATA_IDENTIFY_LOOKAHEAD)
			rastr = "enabled";
		else if (ap->ap_ata.ap_type == ATA_PORT_T_ATAPI)
			rastr = "disabled";
		else
			rastr = "enabling";
	} else {
		    rastr = "notsupp";
	}

	if (ap->ap_ata.ap_identify.cmdset82 & ATA_IDENTIFY_SECURITY) {
		if (ap->ap_ata.ap_identify.securestatus & ATA_SECURE_FROZEN)
			scstr = "frozen";
		else if (ap->ap_ata.ap_type == ATA_PORT_T_ATAPI)
			scstr = "unfrozen";
		else
			scstr = "freezing";
	} else {
		    scstr = "notsupp";
	}

	kprintf("%s: Found %s \"%*.*s %8.8s\" serial=\"%20.20s\"\n"
		"%s: tags=%d/%d satacaps=%04x satafeat=%04x "
		"capacity=%lld.%02dMB\n"
		"%s: f85=%04x f86=%04x f87=%04x WC=%s RA=%s SEC=%s\n",
		PORTNAME(ap),
		type,
		model_len, model_len,
		ap->ap_ata.ap_identify.model,
		ap->ap_ata.ap_identify.firmware,
		ap->ap_ata.ap_identify.serial,

		PORTNAME(ap),
		devncqdepth, ap->ap_sc->sc_ncmds,
		ap->ap_ata.ap_identify.satacap,
		ap->ap_ata.ap_identify.satafsup,
		(long long)capacity_bytes / (1024 * 1024),
		(int)(capacity_bytes % (1024 * 1024)) * 100 / (1024 * 1024),

		PORTNAME(ap),
		ap->ap_ata.ap_identify.features85,
		ap->ap_ata.ap_identify.features86,
		ap->ap_ata.ap_identify.features87,
		wcstr,
		rastr,
		scstr
	);

	/*
	 * Additional type-specific probing
	 */
	switch(ap->ap_ata.ap_type) {
	case ATA_PORT_T_DISK:
		error = ahci_cam_probe_disk(ap);
		break;
	default:
		error = ahci_cam_probe_atapi(ap);
		break;
	}
	return (0);
}

/*
 * DISK-specific probe after initial ident
 */
static int
ahci_cam_probe_disk(struct ahci_port *ap)
{
	struct ata_xfer	*xa;
	int status;

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
	if ((ap->ap_ata.ap_identify.cmdset82 & ATA_IDENTIFY_WRITECACHE) &&
	    (ap->ap_ata.ap_identify.features85 & ATA_IDENTIFY_WRITECACHE) == 0) {
		xa = ahci_ata_get_xfer(ap);
		xa->complete = ahci_ata_dummy_done;
		xa->fis->command = ATA_C_SET_FEATURES;
		/*xa->fis->features = ATA_SF_WRITECACHE_EN;*/
		xa->fis->features = ATA_SF_LOOKAHEAD_EN;
		xa->fis->flags = ATA_H2D_FLAGS_CMD;
		xa->fis->device = 0;
		xa->flags = ATA_F_READ | ATA_F_PIO | ATA_F_POLL;
		xa->timeout = hz;
		xa->datalen = 0;
		status = ahci_ata_cmd(xa);
		if (status == ATA_COMPLETE)
			ap->ap_ata.ap_features |= ATA_PORT_F_WCACHE;
		ahci_ata_put_xfer(xa);
	}

	/*
	 * Enable readahead if supported
	 */
	if ((ap->ap_ata.ap_identify.cmdset82 & ATA_IDENTIFY_LOOKAHEAD) &&
	    (ap->ap_ata.ap_identify.features85 & ATA_IDENTIFY_LOOKAHEAD) == 0) {
		xa = ahci_ata_get_xfer(ap);
		xa->complete = ahci_ata_dummy_done;
		xa->fis->command = ATA_C_SET_FEATURES;
		xa->fis->features = ATA_SF_LOOKAHEAD_EN;
		xa->fis->flags = ATA_H2D_FLAGS_CMD;
		xa->fis->device = 0;
		xa->flags = ATA_F_READ | ATA_F_PIO | ATA_F_POLL;
		xa->timeout = hz;
		xa->datalen = 0;
		status = ahci_ata_cmd(xa);
		if (status == ATA_COMPLETE)
			ap->ap_ata.ap_features |= ATA_PORT_F_RAHEAD;
		ahci_ata_put_xfer(xa);
	}

	/*
	 * FREEZE LOCK the device so malicious users can't lock it on us.
	 * As there is no harm in issuing this to devices that don't
	 * support the security feature set we just send it, and don't bother
	 * checking if the device sends a command abort to tell us it doesn't
	 * support it
	 */
	if ((ap->ap_ata.ap_identify.cmdset82 & ATA_IDENTIFY_SECURITY) &&
	    (ap->ap_ata.ap_identify.securestatus & ATA_SECURE_FROZEN) == 0) {
		xa = ahci_ata_get_xfer(ap);
		xa->complete = ahci_ata_dummy_done;
		xa->fis->command = ATA_C_SEC_FREEZE_LOCK;
		xa->fis->flags = ATA_H2D_FLAGS_CMD;
		xa->flags = ATA_F_READ | ATA_F_PIO | ATA_F_POLL;
		xa->timeout = hz;
		xa->datalen = 0;
		status = ahci_ata_cmd(xa);
		if (status == ATA_COMPLETE)
			ap->ap_ata.ap_features |= ATA_PORT_F_FRZLCK;
		ahci_ata_put_xfer(xa);
	}

	return (0);
}

/*
 * ATAPI-specific probe after initial ident
 */
static int
ahci_cam_probe_atapi(struct ahci_port *ap)
{
	return(0);
}

#if 0
	/*
	 * Keep this old code around for a little bit, it is another way
	 * to probe an ATAPI device by using a ATAPI (SCSI) INQUIRY
	 */
	struct ata_xfer	*xa;
	int		status;
	int		devncqdepth;
	struct scsi_inquiry_data *inq_data;
	struct scsi_inquiry *inq_cmd;

	inq_data = kmalloc(sizeof(*inq_data), M_TEMP, M_WAITOK | M_ZERO);

	/*
	 * Issue identify, saving the result
	 */
	xa = ahci_ata_get_xfer(ap);
	xa->complete = ahci_ata_dummy_done;
	xa->data = inq_data;
	xa->datalen = sizeof(*inq_data);
	xa->flags = ATA_F_READ | ATA_F_PACKET | ATA_F_PIO | ATA_F_POLL;
	xa->timeout = hz;

	xa->fis->flags = ATA_H2D_FLAGS_CMD;
	xa->fis->command = ATA_C_PACKET;
	xa->fis->device = 0;
	xa->fis->sector_count = xa->tag << 3;
	xa->fis->features = ATA_H2D_FEATURES_DMA |
		    ((xa->flags & ATA_F_WRITE) ?
		    ATA_H2D_FEATURES_DIR_WRITE : ATA_H2D_FEATURES_DIR_READ);
	xa->fis->lba_mid = 0x00;
	xa->fis->lba_high = 0x20;

	inq_cmd = (void *)xa->packetcmd;
	inq_cmd->opcode = INQUIRY;
	inq_cmd->length = SHORT_INQUIRY_LENGTH;

	status = ahci_ata_cmd(xa);
	if (status != ATA_COMPLETE) {
		kprintf("%s: Detected ATAPI device but unable to INQUIRY\n",
			PORTNAME(ap));
		ahci_ata_put_xfer(xa);
		kfree(inq_data, M_TEMP);
		return(EIO);
	}
	if (xa->state != ATA_S_COMPLETE) {
		kprintf("%s: Detected ATAPI device but unable to INQUIRY "
			" xa->state=%d\n",
			PORTNAME(ap), xa->state);
		ahci_ata_put_xfer(xa);
		kfree(inq_data, M_TEMP);
		return(EIO);
	}
	ahci_ata_put_xfer(xa);

	ap->ap_ata.ap_features |= ATA_PORT_F_PROBED;

	/*
	 * XXX Negotiate NCQ with ATAPI?  How do we do this?
	 */

	devncqdepth = 0;

	kprintf("%s: Found ATAPI %s \"%8.8s %16.16s\" rev=\"%4.4s\"\n"
		"%s: tags=%d/%d\n",
		PORTNAME(ap),
		ScsiTypeArray[SID_TYPE(inq_data)],
		inq_data->vendor,
		inq_data->product,
		inq_data->revision,

		PORTNAME(ap),
		devncqdepth, ap->ap_sc->sc_ncmds
	);
	kfree(inq_data, M_TEMP);
#endif

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
 * Initiate a bus scan.
 *
 * An asynchronous bus scan is used to avoid reentrancy issues
 */
static void
ahci_cam_rescan_callback(struct cam_periph *periph, union ccb *ccb)
{
	kfree(ccb, M_TEMP);
}

static void
ahci_cam_rescan(struct ahci_port *ap)
{
	struct cam_path *path;
	union ccb *ccb;
	int status;

	ccb = kmalloc(sizeof(*ccb), M_TEMP, M_WAITOK | M_ZERO);
	status = xpt_create_path(&path, xpt_periph, cam_sim_path(ap->ap_sim),
				 CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP)
		return;

	xpt_setup_ccb(&ccb->ccb_h, path, 5);	/* 5 = low priority */
	ccb->ccb_h.func_code = XPT_SCAN_BUS | XPT_FC_QUEUED;
	ccb->ccb_h.cbfcnp = ahci_cam_rescan_callback;
	ccb->crcn.flags = CAM_FLAG_NONE;
	xpt_action(ccb);

	/* scan is now underway */
}

/*
 * Action function - dispatch command
 */
static
void
ahci_xpt_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ahci_port *ap;
	struct ccb_hdr *ccbh;
	int unit;

	/* XXX lock */
	ap = cam_sim_softc(sim);
	KKASSERT(ap != NULL);
	ccbh = &ccb->ccb_h;
	unit = cam_sim_unit(sim);

	/*
	 * Non-zero target and lun ids will be used for future
	 * port multiplication(?).  A target wildcard indicates only
	 * the general bus is being probed.
	 *
	 * XXX What do we do with a LUN wildcard?
	 */
	if (ccbh->target_id != CAM_TARGET_WILDCARD) {
		if (ap->ap_ata.ap_type == ATA_PORT_T_NONE) {
			ccbh->status = CAM_REQ_INVALID;
			xpt_done(ccb);
			return;
		}
		if (ccbh->target_id) {
			ccbh->status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}
		if (ccbh->target_lun != CAM_LUN_WILDCARD && ccbh->target_lun) {
			ccbh->status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}
	}

	/*
	 * Switch on the meta XPT command
	 */
	switch(ccbh->func_code) {
	case XPT_PATH_INQ:
		ccb->cpi.version_num = 1;
		ccb->cpi.hba_inquiry = 0;
		ccb->cpi.target_sprt = 0;
		ccb->cpi.hba_misc = 0;
		ccb->cpi.hba_eng_cnt = 0;
		bzero(ccb->cpi.vuhba_flags, sizeof(ccb->cpi.vuhba_flags));
		ccb->cpi.max_target = 7;
		ccb->cpi.max_lun = 0;
		ccb->cpi.async_flags = 0;
		ccb->cpi.hpath_id = 0;
		ccb->cpi.initiator_id = 7;
		ccb->cpi.unit_number = cam_sim_unit(sim);
		ccb->cpi.bus_id = cam_sim_bus(sim);
		ccb->cpi.base_transfer_speed = 150000;
		ccb->cpi.transport = XPORT_AHCI;
		ccb->cpi.transport_version = 1;
		ccb->cpi.protocol = PROTO_SCSI;
		ccb->cpi.protocol_version = SCSI_REV_2;

		/*
		 * Non-zero target and lun ids will be used for future
		 * port multiplication(?).  A target wildcard indicates only
		 * the general bus is being probed.
		 *
		 * XXX What do we do with a LUN wildcard?
		 */
		if (ccbh->target_id != CAM_TARGET_WILDCARD) {
			switch(ahci_pread(ap, AHCI_PREG_SSTS) &
			       AHCI_PREG_SSTS_SPD) {
			case AHCI_PREG_SSTS_SPD_GEN1:
				ccb->cpi.base_transfer_speed = 150000;
				break;
			case AHCI_PREG_SSTS_SPD_GEN2:
				ccb->cpi.base_transfer_speed = 300000;
				break;
			default:
				/* unknown */
				ccb->cpi.base_transfer_speed = 1000;
				break;
			}
			/* XXX check attached, set base xfer speed */
		}
		ccbh->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	case XPT_RESET_DEV:
		lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
		ahci_port_softreset(ap);
		lwkt_serialize_exit(&ap->ap_sc->sc_serializer);

		ccbh->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	case XPT_RESET_BUS:
		lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
		ahci_port_portreset(ap);
		ahci_port_softreset(ap);
		lwkt_serialize_exit(&ap->ap_sc->sc_serializer);

		xpt_async(AC_BUS_RESET, ap->ap_path, NULL);

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
		ccb->cts.transport = XPORT_AHCI;
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
		switch(ap->ap_ata.ap_type) {
		case ATA_PORT_T_DISK:
			ahci_xpt_scsi_disk_io(sim, ccb);
			break;
		case ATA_PORT_T_ATAPI:
			ahci_xpt_scsi_atapi_io(sim, ccb);
			break;
		default:
			ccbh->status = CAM_REQ_INVALID;
			xpt_done(ccb);
			break;
		}
		break;
	default:
		kprintf("xpt_unknown\n");
		ccbh->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	}
}

/*
 * Poll function (unused?)
 */
static
void
ahci_xpt_poll(struct cam_sim *sim)
{
	/*struct ahci_port *ap = cam_sim_softc(sim);*/

	kprintf("ahci_xpt_poll\n");
	/* XXX lock */
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
ahci_xpt_scsi_disk_io(struct cam_sim *sim, union ccb *ccb)
{
	struct ahci_port *ap;
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	struct ata_xfer *xa;
	struct ata_fis_h2d *fis;
	scsi_cdb_t cdb;
	union scsi_data *rdata;
	int rdata_len;
	u_int64_t capacity;
	u_int64_t lba;
	u_int32_t count;

	ap = cam_sim_softc(sim);
	ccbh = &ccb->csio.ccb_h;
	csio = &ccb->csio;
	xa = ahci_ata_get_xfer(ap);
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
			switch(cdb->inquiry.page_code) {
			case SVPD_SUPPORTED_PAGE_LIST:
				/* XXX atascsi_disk_vpd_supported */
			case SVPD_UNIT_SERIAL_NUMBER:
				/* XXX atascsi_disk_vpd_serial */
			case SVPD_UNIT_DEVID:
				/* XXX atascsi_disk_vpd_ident */
			default:
				ccbh->status = CAM_FUNC_NOTAVAIL;
				break;
			}
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
			bcopy(ap->ap_ata.ap_identify.model,
			      rdata->inquiry_data.product,
			      sizeof(rdata->inquiry_data.product));
			bcopy(ap->ap_ata.ap_identify.firmware,
			      rdata->inquiry_data.revision,
			      sizeof(rdata->inquiry_data.revision));
			ccbh->status = CAM_REQ_CMP;
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

		capacity = ap->ap_ata.ap_capacity;

		bzero(rdata, rdata_len);
		if (cdb->generic.opcode == READ_CAPACITY) {
			rdata_len = sizeof(rdata->read_capacity_data);
			if (capacity > 0xFFFFFFFFU)
				capacity = 0xFFFFFFFFU;
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
		xa->datalen = 0;
		xa->flags = ATA_F_READ;
		xa->complete = ahci_ata_complete_disk_synchronize_cache;
		if (xa->timeout < 45 * hz)
			xa->timeout = 45 * hz;
		fis->flags = ATA_H2D_FLAGS_CMD;
		fis->command = ATA_C_FLUSH_CACHE;
		fis->device = 0;
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
	case ATA_PASS_16:
		/*
		 * XXX implement pass-through
		 */
		ccbh->status = CAM_FUNC_NOTAVAIL;
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

		if (ap->ap_ata.ap_ncqdepth > 1 &&
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
		} else if (count > 0x100 || lba > 0xFFFFFFFFU) {
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
		xa->timeout = ccbh->timeout * hz / 1000;
		if (ccbh->flags & CAM_POLLED)
			xa->flags |= ATA_F_POLL;
		break;
	}

	/*
	 * If the request is still in progress the xa and FIS have
	 * been set up and must be dispatched.  Otherwise the request
	 * is complete.
	 */
	if (ccbh->status == CAM_REQ_INPROG) {
		KKASSERT(xa->complete != NULL);
		xa->atascsi_private = ccb;
		ccb->ccb_h.sim_priv.entries[0].ptr = ap;
		lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
		ahci_ata_cmd(xa);
		lwkt_serialize_exit(&ap->ap_sc->sc_serializer);
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
ahci_xpt_scsi_atapi_io(struct cam_sim *sim, union ccb *ccb)
{
	struct ahci_port *ap;
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	struct ata_xfer *xa;
	struct ata_fis_h2d *fis;
	scsi_cdb_t cdbs;
	scsi_cdb_t cdbd;
	int flags;

	ap = cam_sim_softc(sim);
	ccbh = &ccb->csio.ccb_h;
	csio = &ccb->csio;

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
	 * The command has to fit in the packet command buffer.
	 */
	if (csio->cdb_len < 6 || csio->cdb_len > 16) {
		ccbh->status = CAM_CCB_LEN_ERR;
		xpt_done(ccb);
		return;
	}

	/*
	 * Initialize the XA and FIS.
	 */
	xa = ahci_ata_get_xfer(ap);
	fis = xa->fis;

	xa->flags = flags;
	xa->data = csio->data_ptr;
	xa->datalen = csio->dxfer_len;
	xa->timeout = ccbh->timeout * hz / 1000;
	if (ccbh->flags & CAM_POLLED)
		xa->flags |= ATA_F_POLL;

	fis->flags = ATA_H2D_FLAGS_CMD;
	fis->command = ATA_C_PACKET;
	fis->device = 0;
	fis->sector_count = xa->tag << 3;
	fis->features = ATA_H2D_FEATURES_DMA |
		    ((xa->flags & ATA_F_WRITE) ?
		    ATA_H2D_FEATURES_DIR_WRITE : ATA_H2D_FEATURES_DIR_READ);
	fis->lba_mid = 0x00;
	fis->lba_high = 0x20;

	/*
	 * Copy the cdb to the packetcmd buffer in the FIS using a
	 * convenient pointer in the xa.
	 */
	cdbs = (void *)((ccbh->flags & CAM_CDB_POINTER) ?
			csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes);
	bcopy(cdbs, xa->packetcmd, csio->cdb_len);

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
	case INQUIRY:
		/*
		 * Some ATAPI devices can't handle SI_EVPD being set
		 * for a basic inquiry (page_code == 0).
		 *
		 * Some ATAPI devices can't handle long inquiry lengths,
		 * don't ask me why.  Truncate the inquiry length.
		 */
		if ((cdbd->inquiry.byte2 & SI_EVPD) &&
		    cdbd->inquiry.page_code == 0) {
			cdbd->inquiry.byte2 &= ~SI_EVPD;
		}
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
	ahci_ata_cmd(xa);
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
		kprintf("%s: synchronize_cache: error\n", PORTNAME(ap));
		ccbh->status = CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_dummy_sense(&ccb->csio.sense_data);
		break;
	case ATA_S_TIMEOUT:
		kprintf("%s: synchronize_cache: timeout\n", PORTNAME(ap));
		ccbh->status = CAM_CMD_TIMEOUT;
		break;
	default:
		kprintf("%s: synchronize_cache: unknown state %d\n",
			PORTNAME(ap), xa->state);
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}
	ahci_ata_put_xfer(xa);
	lwkt_serialize_exit(&ap->ap_sc->sc_serializer);
	xpt_done(ccb);
	lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
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
		kprintf("%s: disk_rw: error\n", PORTNAME(ap));
		ccbh->status = CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_dummy_sense(&ccb->csio.sense_data);
		break;
	case ATA_S_TIMEOUT:
		kprintf("%s: disk_rw: timeout\n", PORTNAME(ap));
		ccbh->status = CAM_CMD_TIMEOUT;
		break;
	default:
		kprintf("%s: disk_rw: unknown state %d\n",
			PORTNAME(ap), xa->state);
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}
	ccb->csio.resid = xa->resid;
	ahci_ata_put_xfer(xa);
	lwkt_serialize_exit(&ap->ap_sc->sc_serializer);
	xpt_done(ccb);
	lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
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
		kprintf("%s: cmd %d: error\n",
			PORTNAME(ap), cdb->generic.opcode);
		ccbh->status = CAM_SCSI_STATUS_ERROR;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ahci_ata_atapi_sense(&xa->rfis, &ccb->csio.sense_data);
		break;
	case ATA_S_TIMEOUT:
		kprintf("%s: cmd %d: timeout\n",
			PORTNAME(ap), cdb->generic.opcode);
		ccbh->status = CAM_CMD_TIMEOUT;
		break;
	default:
		kprintf("%s: cmd %d: unknown state %d\n",
			PORTNAME(ap), cdb->generic.opcode, xa->state);
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}
	ccb->csio.resid = xa->resid;
	ahci_ata_put_xfer(xa);
	lwkt_serialize_exit(&ap->ap_sc->sc_serializer);
	xpt_done(ccb);
	lwkt_serialize_enter(&ap->ap_sc->sc_serializer);
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
