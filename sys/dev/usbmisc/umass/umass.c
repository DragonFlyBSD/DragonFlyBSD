/*-
 * Copyright (c) 1999 MAEKAWA Masahide <bishop@rr.iij4u.or.jp>,
 *		      Nick Hibma <n_hibma@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $NetBSD: umass.c,v 1.28 2000/04/02 23:46:53 augustss Exp $
 * $FreeBSD: src/sys/dev/usb/umass.c,v 1.96 2003/12/19 12:19:11 sanpei Exp $
 */

/*
 * Universal Serial Bus Mass Storage Class specs:
 * http://www.usb.org/developers/data/devclass/usbmassover_11.pdf
 * http://www.usb.org/developers/data/devclass/usbmassbulk_10.pdf
 * http://www.usb.org/developers/data/devclass/usbmass-cbi10.pdf
 * http://www.usb.org/developers/data/devclass/usbmass-ufi10.pdf
 */

/*
 * Ported to NetBSD by Lennart Augustsson <augustss@netbsd.org>.
 * Parts of the code written my Jason R. Thorpe <thorpej@shagadelic.org>.
 */

/*
 * The driver handles 3 Wire Protocols
 * - Command/Bulk/Interrupt (CBI)
 * - Command/Bulk/Interrupt with Command Completion Interrupt (CBI with CCI)
 * - Mass Storage Bulk-Only (BBB)
 *   (BBB refers Bulk/Bulk/Bulk for Command/Data/Status phases)
 *
 * Over these wire protocols it handles the following command protocols
 * - SCSI
 * - UFI (floppy command set)
 * - 8070i (ATAPI)
 *
 * UFI and 8070i (ATAPI) are transformed versions of the SCSI command set. The
 * sc->transform method is used to convert the commands into the appropriate
 * format (if at all necessary). For example, UFI requires all commands to be
 * 12 bytes in length amongst other things.
 *
 * The source code below is marked and can be split into a number of pieces
 * (in this order):
 *
 * - probe/attach/detach
 * - generic transfer routines
 * - BBB
 * - CBI
 * - CBI_I (in addition to functions from CBI)
 * - CAM (Common Access Method)
 * - SCSI
 * - UFI
 * - 8070i (ATAPI)
 *
 * The protocols are implemented using a state machine, for the transfers as
 * well as for the resets. The state machine is contained in umass_*_state.
 * The state machine is started through either umass_*_transfer or
 * umass_*_reset.
 *
 * The reason for doing this is a) CAM performs a lot better this way and b) it
 * avoids using tsleep from interrupt context (for example after a failed
 * transfer).
 */

/*
 * The SCSI related part of this driver has been derived from the
 * dev/ppbus/vpo.c driver, by Nicolas Souchu (nsouch@freebsd.org).
 *
 * The CAM layer uses so called actions which are messages sent to the host
 * adapter for completion. The actions come in through umass_cam_action. The
 * appropriate block of routines is called depending on the transport protocol
 * in use. When the transfer has finished, these routines call
 * umass_cam_cb again to complete the CAM command.
 */

/*
 * XXX Currently CBI with CCI is not supported because it bombs the system
 *     when the device is detached (low frequency interrupts are detached
 *     too late.
 */
#undef CBI_I

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#include <bus/cam/cam.h>
#include <bus/cam/cam_ccb.h>
#include <bus/cam/cam_sim.h>
#include <bus/cam/cam_xpt_sim.h>
#include <bus/cam/scsi/scsi_all.h>
#include <bus/cam/scsi/scsi_da.h>
#include <bus/cam/scsi/scsi_cd.h>
#include <bus/cam/scsi/scsi_ch.h>

#include <bus/cam/cam_periph.h>

#ifdef USB_DEBUG
#define DIF(m, x)	if (umassdebug & (m)) do { x ; } while (0)
#define	DPRINTF(m, x)	if (umassdebug & (m)) kprintf x
#define UDMASS_GEN	0x00010000	/* general */
#define UDMASS_SCSI	0x00020000	/* scsi */
#define UDMASS_UFI	0x00040000	/* ufi command set */
#define UDMASS_ATAPI	0x00080000	/* 8070i command set */
#define UDMASS_CMD	(UDMASS_SCSI|UDMASS_UFI|UDMASS_ATAPI)
#define UDMASS_USB	0x00100000	/* USB general */
#define UDMASS_BBB	0x00200000	/* Bulk-Only transfers */
#define UDMASS_CBI	0x00400000	/* CBI transfers */
#define UDMASS_WIRE	(UDMASS_BBB|UDMASS_CBI)
#define UDMASS_ALL	0xffff0000	/* all of the above */
int umassdebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, umass, CTLFLAG_RW, 0, "USB umass");
SYSCTL_INT(_hw_usb_umass, OID_AUTO, debug, CTLFLAG_RW,
	   &umassdebug, 0, "umass debug level");
#else
#define DIF(m, x)	/* nop */
#define	DPRINTF(m, x)	/* nop */
#endif


/* Generic definitions */

/* Direction for umass_*_transfer */
#define DIR_NONE	0
#define DIR_IN		1
#define DIR_OUT		2

/* device name */
#define DEVNAME		"umass"
#define DEVNAME_SIM	"umass-sim"

#define UMASS_MAX_TRANSFER_SIZE		65536
/* Approximate maximum transfer speeds (assumes 33% overhead). */
#define UMASS_FULL_TRANSFER_SPEED	1000
#define UMASS_HIGH_TRANSFER_SPEED	40000
#define UMASS_FLOPPY_TRANSFER_SPEED	20

#define UMASS_TIMEOUT			5000 /* msecs */

/* CAM specific definitions */

#define UMASS_SCSIID_MAX	1	/* maximum number of drives expected */
#define UMASS_SCSIID_HOST	UMASS_SCSIID_MAX

#define MS_TO_TICKS(ms) ((ms) * hz / 1000)


/* Bulk-Only features */

#define UR_BBB_RESET		0xff		/* Bulk-Only reset */
#define UR_BBB_GET_MAX_LUN	0xfe		/* Get maximum lun */

/* Command Block Wrapper */
typedef struct {
	uDWord		dCBWSignature;
#	define CBWSIGNATURE	0x43425355
	uDWord		dCBWTag;
	uDWord		dCBWDataTransferLength;
	uByte		bCBWFlags;
#	define CBWFLAGS_OUT	0x00
#	define CBWFLAGS_IN	0x80
	uByte		bCBWLUN;
	uByte		bCDBLength;
#	define CBWCDBLENGTH	16
	uByte		CBWCDB[CBWCDBLENGTH];
} umass_bbb_cbw_t;
#define	UMASS_BBB_CBW_SIZE	31

/* Command Status Wrapper */
typedef struct {
	uDWord		dCSWSignature;
#	define CSWSIGNATURE	0x53425355
#	define CSWSIGNATURE_OLYMPUS_C1	0x55425355
	uDWord		dCSWTag;
	uDWord		dCSWDataResidue;
	uByte		bCSWStatus;
#	define CSWSTATUS_GOOD	0x0
#	define CSWSTATUS_FAILED	0x1
#	define CSWSTATUS_PHASE	0x2
} umass_bbb_csw_t;
#define	UMASS_BBB_CSW_SIZE	13

/* CBI features */

#define UR_CBI_ADSC	0x00

typedef unsigned char umass_cbi_cbl_t[16];	/* Command block */

typedef union {
	struct {
		unsigned char	type;
		#define IDB_TYPE_CCI		0x00
		unsigned char	value;
		#define IDB_VALUE_PASS		0x00
		#define IDB_VALUE_FAIL		0x01
		#define IDB_VALUE_PHASE		0x02
		#define IDB_VALUE_PERSISTENT	0x03
		#define IDB_VALUE_STATUS_MASK	0x03
	} common;

	struct {
		unsigned char	asc;
		unsigned char	ascq;
	} ufi;
} umass_cbi_sbl_t;



struct umass_softc;		/* see below */

typedef void (*transfer_cb_f)	(struct umass_softc *sc, void *priv,
				int residue, int status);
#define STATUS_CMD_OK		0	/* everything ok */
#define STATUS_CMD_UNKNOWN	1	/* will have to fetch sense */
#define STATUS_CMD_FAILED	2	/* transfer was ok, command failed */
#define STATUS_WIRE_FAILED	3	/* couldn't even get command across */

typedef void (*wire_reset_f)	(struct umass_softc *sc, int status);
typedef void (*wire_transfer_f)	(struct umass_softc *sc, int lun,
				void *cmd, int cmdlen, void *data, int datalen,
				int dir, u_int timeout, transfer_cb_f cb, void *priv);
typedef void (*wire_state_f)	(usbd_xfer_handle xfer,
				usbd_private_handle priv, usbd_status err);

typedef int (*command_transform_f)	(struct umass_softc *sc,
				unsigned char *cmd, int cmdlen,
				unsigned char **rcmd, int *rcmdlen);


struct umass_devdescr_t {
	u_int32_t	vendor;
	u_int32_t	product;
	u_int32_t	release;
#	define WILDCARD_ID	0xffffffff
#	define EOT_ID		0xfffffffe

	/* wire and command protocol */
	u_int16_t	proto;
#	define UMASS_PROTO_BBB		0x0001	/* USB wire protocol */
#	define UMASS_PROTO_CBI		0x0002
#	define UMASS_PROTO_CBI_I	0x0004
#	define UMASS_PROTO_WIRE		0x00ff	/* USB wire protocol mask */
#	define UMASS_PROTO_SCSI		0x0100	/* command protocol */
#	define UMASS_PROTO_ATAPI	0x0200
#	define UMASS_PROTO_UFI		0x0400
#	define UMASS_PROTO_RBC		0x0800
#	define UMASS_PROTO_COMMAND	0xff00	/* command protocol mask */

	/* Device specific quirks */
	u_int16_t	quirks;
#	define NO_QUIRKS		0x0000
	/* The drive does not support Test Unit Ready. Convert to Start Unit
	 */
#	define NO_TEST_UNIT_READY	0x0001
	/* The drive does not reset the Unit Attention state after REQUEST
	 * SENSE has been sent. The INQUIRY command does not reset the UA
	 * either, and so CAM runs in circles trying to retrieve the initial
	 * INQUIRY data.
	 */
#	define RS_NO_CLEAR_UA		0x0002
	/* The drive does not support START STOP.  */
#	define NO_START_STOP		0x0004
	/* Don't ask for full inquiry data (255b).  */
#	define FORCE_SHORT_INQUIRY	0x0008
	/* Needs to be initialised the Shuttle way */
#	define SHUTTLE_INIT		0x0010
	/* Drive needs to be switched to alternate iface 1 */
#	define ALT_IFACE_1		0x0020
	/* Drive does not do 1Mb/s, but just floppy speeds (20kb/s) */
#	define FLOPPY_SPEED		0x0040
	/* The device can't count and gets the residue of transfers wrong */
#	define IGNORE_RESIDUE		0x0080
	/* No GetMaxLun call */
#	define NO_GETMAXLUN		0x0100
	/* The device uses a weird CSWSIGNATURE. */
#	define WRONG_CSWSIG		0x0200
	/* Device cannot handle INQUIRY so fake a generic response */
#	define NO_INQUIRY		0x0400
	/* Device cannot handle INQUIRY EVPD, return CHECK CONDITION */
#	define NO_INQUIRY_EVPD		0x0800
#	define READ_CAPACITY_OFFBY1	0x2000
};

static struct umass_devdescr_t umass_devdescrs[] = {
	/* Addonics Cable 205  */
	{ .vendor = 0x0bf6, .product = 0xa001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Addonics USB 2.0 Flash */
	{ .vendor = 0x09df, .product = 0x1300, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* Addonics Attache 256MB USB */
	{ .vendor = 0x09df, .product = 0x1400, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* Addonics USB 2.0 Flash */
	{ .vendor = 0x09df, .product = 0x1420, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* AIPTEK PocketCAM 3Mega  */
	{ .vendor = 0x08ca, .product = 0x2011, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* All Asahi Optical products */
	{ .vendor = 0x0a17, .product = WILDCARD_ID, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = RS_NO_CLEAR_UA
	},
	/* Belkin USB to SCSI */
	{ .vendor = 0x050d, .product = 0x0115, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* CASIO QV DigiCam  */
	{ .vendor = 0x07cf, .product = 0x1001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_INQUIRY
	},
	/* CCYU EasyDisk ED1064  */
	{ .vendor = 0x1065, .product = 0x2136, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Century Century USB Disk */
	{ .vendor = 0x07f7, .product = 0x011e, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Desknote UCR-61S2B   */
	{ .vendor = 0x1019, .product = 0x0c55, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* DMI CF/SM Reader/Writer  */
	{ .vendor = 0x0c0b, .product = 0xa109, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_GETMAXLUN
	},
	/* Epson Stylus Photo 875DC */
	{ .vendor = 0x03f8, .product = 0x0601, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_INQUIRY
	},
	/* Epson Stylus Photo 895 */
	{ .vendor = 0x03f8, .product = 0x0602, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_GETMAXLUN
	},
	/* Feiya 5-in-1 Card Reader */
	{ .vendor = 0x090c, .product = 0x1132, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Freecom DVD drive  */
	{ .vendor = 0x07ab, .product = 0xfc01, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* Fujiphoto mass storage products */
	{ .vendor = 0x04cb, .product = 0x0100, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = RS_NO_CLEAR_UA
	},
	/* Genesys GL641USB USB-IDE Bridge */
	{ .vendor = 0x05e3, .product = 0x0701, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Genesys GL641USB USB-IDE Bridge */
	{ .vendor = 0x05e3, .product = 0x0702, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Genesys GL641USB CompactFlash Card */
	{ .vendor = 0x05e3, .product = 0x0700, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Genesys GL641USB 6-in-1 Card */
	{ .vendor = 0x05e3, .product = 0x0760, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = WRONG_CSWSIG
	},
	/* Hagiwara FlashGate SmartMedia Card */
	{ .vendor = 0x0693, .product = 0x0002, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Hitachi DVDCAM USB HS Interface */
	{ .vendor = 0x04a4, .product = 0x001e, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = NO_INQUIRY
	},
	/* Hitachi DVD-CAM DZ-MV100A Camcorder */
	{ .vendor = 0x04a4, .product = 0x0004, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_GETMAXLUN
	},
	/* Hewlett CD-Writer+ CD-4e  */
	{ .vendor = 0x03f0, .product = 0x0307, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI,
	  .quirks = NO_QUIRKS
	},
	/* Hewlett CD-Writer Plus 8200e */
	{ .vendor = 0x03f0, .product = 0x0207, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = NO_TEST_UNIT_READY | NO_START_STOP
	},
	/* Imagination DBX1 DSP core */
	{ .vendor = 0x149a, .product = 0x2107, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = WRONG_CSWSIG
	},
	/* In-System ATAPI Adapter  */
	{ .vendor = 0x05ab, .product = 0x0031, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* In-System USB Storage Adapter */
	{ .vendor = 0x05ab, .product = 0x5701, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* In-System USB cable */
	{ .vendor = 0x05ab, .product = 0x081a, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_TEST_UNIT_READY | NO_START_STOP | ALT_IFACE_1
	},
	/* I-O DVD Multi-plus unit */
	{ .vendor = 0x04bb, .product = 0x0204, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* I-O DVD Multi-plus unit */
	{ .vendor = 0x04bb, .product = 0x0206, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Iomega Zip 100 */
	{ .vendor = 0x059b, .product = 0x0001, .release = WILDCARD_ID,
	  /* XXX This is not correct as there are Zip drives that use ATAPI. */
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_TEST_UNIT_READY
	},
	/* Kyocera Finecam L3  */
	{ .vendor = 0x0482, .product = 0x0105, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Kyocera Finecam S3x  */
	{ .vendor = 0x0482, .product = 0x0100, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_INQUIRY
	},
	/* Kyocera Finecam S4  */
	{ .vendor = 0x0482, .product = 0x0101, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_INQUIRY
	},
	/* Kyocera Finecam S5  */
	{ .vendor = 0x0482, .product = 0x0103, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* LaCie Hard Disk  */
	{ .vendor = 0x059f, .product = 0xa601, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Lexar USB CF Reader */
	{ .vendor = 0x05dc, .product = 0xb002, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Lexar jumpSHOT CompactFlash Reader */
	{ .vendor = 0x05dc, .product = 0x0001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* Logitech DVD Multi-plus unit */
	{ .vendor = 0x046d, .product = 0x0033, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* Logitech DVD Multi-plus unit LDR-H443U2 */
	{ .vendor = 0x0789, .product = 0x00b3, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Melco USB-IDE Bridge: DUB-PxxG */
	{ .vendor = 0x0411, .product = 0x001c, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Microtech USB CameraMate */
	{ .vendor = 0x07af, .product = 0x0006, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_TEST_UNIT_READY | NO_START_STOP
	},
	/* Microtech USB-SCSI-DB25   */
	{ .vendor = 0x07af, .product = 0x0004, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Microtech USB-SCSI-HD50   */
	{ .vendor = 0x07af, .product = 0x0005, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Minolta Dimage E223  */
	{ .vendor = 0x0686, .product = 0x4017, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* Minolta Dimage F300  */
	{ .vendor = 0x0686, .product = 0x4011, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Mitsumi CD-R/RW Drive  */
	{ .vendor = 0x03ee, .product = 0x0000, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Mitsumi USB FDD  */
	{ .vendor = 0x03ee, .product = 0x6901, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_GETMAXLUN
	},
	/* Motorola E398 Mobile Phone */
	{ .vendor = 0x22b8, .product = 0x4810, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_INQUIRY_EVPD | NO_GETMAXLUN
	},
	/* M-Systems DiskOnKey */
	{ .vendor = 0x08ec, .product = 0x0010, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE | NO_GETMAXLUN | RS_NO_CLEAR_UA
	},
	/* M-Systems DiskOnKey */
	{ .vendor = 0x08ec, .product = 0x0011, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Myson USB-IDE   */
	{ .vendor = 0x04cf, .product = 0x8818, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY | IGNORE_RESIDUE
	},
	/* Neodio 8-in-1 Multi-format Flash */
	{ .vendor = 0x0aec, .product = 0x3260, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY
	},
	/* Netac USB-CF-Card   */
	{ .vendor = 0x0dd8, .product = 0x1060, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Netac OnlyDisk   */
	{ .vendor = 0x0dd8, .product = 0x0003, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* NetChip USB Clik! 40 */
	{ .vendor = 0x0525, .product = 0xa140, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI,
	  .quirks = NO_INQUIRY
	},
	/* Olympus C-1 Digital Camera */
	{ .vendor = 0x07b4, .product = 0x0102, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = WRONG_CSWSIG
	},
	/* Olympus C-700 Ultra Zoom */
	{ .vendor = 0x07b4, .product = 0x0105, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_GETMAXLUN
	},
	/* OnSpec SIIG/Datafab Memory Stick+CF */
	{ .vendor = 0x07c4, .product = 0xa001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* OnSpec USB to CF */
	{ .vendor = 0x07c4, .product = 0xa109, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* OnSpec PNY/Datafab CF+SM Reader */
	{ .vendor = 0x07c4, .product = 0xa005, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* OnSpec Simple Tech/Datafab CF+SM */
	{ .vendor = 0x07c4, .product = 0xa006, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* OnSpec MDCFE-B USB CF */
	{ .vendor = 0x07c4, .product = 0xa000, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* OnSpec MDSM-B reader  */
	{ .vendor = 0x07c4, .product = 0xa103, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_INQUIRY
	},
	/* OnSpec Datafab-based Reader  */
	{ .vendor = 0x07c4, .product = 0xa003, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* OnSpec FlashLink UCF-100 CompactFlash */
	{ .vendor = 0x07c4, .product = 0xa400, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY | NO_GETMAXLUN
	},
	/* OnSpec ImageMate SDDR55  */
	{ .vendor = 0x55aa, .product = 0xa103, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_GETMAXLUN
	},
	/* Panasonic CD-R Drive KXL-840AN */
	{ .vendor = 0x04da, .product = 0x0d01, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_BBB,
	  .quirks = NO_GETMAXLUN
	},
	/* Panasonic CD-R Drive KXL-CB20AN */
	{ .vendor = 0x04da, .product = 0x0d0a, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Panasonic DVD-ROM & CD-R/RW */
	{ .vendor = 0x04da, .product = 0x0d0e, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Panasonic LS-120 Camera  */
	{ .vendor = 0x04da, .product = 0x0901, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_UFI,
	  .quirks = NO_QUIRKS
	},
	/* Plextor PlexWriter 40/12/40U  */
	{ .vendor = 0x093b, .product = 0x0011, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_TEST_UNIT_READY
	},
	/* PNY USB 2.0 Flash */
	{ .vendor = 0x154b, .product = 0x0010, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE | NO_START_STOP
	},
	/* Pen USB 2.0 Flash Drive */
	{ .vendor = 0x0d7d, .product = 0x1300, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* Samsung YP-U2 MP3 Player */
	{ .vendor = 0x04e8, .product = 0x5050, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = SHUTTLE_INIT | NO_GETMAXLUN
	},
	/* Samsung Digimax 410  */
	{ .vendor = 0x0839, .product = 0x000a, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* SanDisk ImageMate SDDR-05a  */
	{ .vendor = 0x0781, .product = 0x0001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = READ_CAPACITY_OFFBY1 | NO_GETMAXLUN
	},
	/* SanDisk ImageMate SDDR-09  */
	{ .vendor = 0x0781, .product = 0x0200, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = READ_CAPACITY_OFFBY1 | NO_GETMAXLUN
	},
	/* SanDisk ImageMate SDDR-12  */
	{ .vendor = 0x0781, .product = 0x0100, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = READ_CAPACITY_OFFBY1 | NO_GETMAXLUN
	},
	/* SanDisk ImageMate SDDR-31  */
	{ .vendor = 0x0781, .product = 0x0002, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = READ_CAPACITY_OFFBY1
	},
	/* SanDisk Cruzer Mini 256MB */
	{ .vendor = 0x0781, .product = 0x7104, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* SanDisk Cruzer Micro 128MB */
	{ .vendor = 0x0781, .product = 0x7112, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* SanDisk Cruzer Micro 256MB */
	{ .vendor = 0x0781, .product = 0x7113, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* ScanLogic SL11R IDE Adapter */
	{ .vendor = 0x04ce, .product = 0x0002, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = NO_QUIRKS
	},
	/* Shuttle CD-RW Device  */
	{ .vendor = 0x04e6, .product = 0x0101, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Shuttle eUSB CompactFlash Adapter */
	{ .vendor = 0x04e6, .product = 0x000a, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Shuttle E-USB Bridge  */
	{ .vendor = 0x04e6, .product = 0x0001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = NO_TEST_UNIT_READY | NO_START_STOP | SHUTTLE_INIT
	},
	/* Shuttle eUSB ATA/ATAPI Adapter */
	{ .vendor = 0x04e6, .product = 0x0009, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Shuttle eUSB SmartMedia / */
	{ .vendor = 0x04e6, .product = 0x0005, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* Shuttle eUSCSI Bridge  */
	{ .vendor = 0x04e6, .product = 0x0002, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Shuttle Sony Hifd  */
	{ .vendor = 0x04e6, .product = 0x0007, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_GETMAXLUN
	},
	/* Shuttle ImageMate SDDR09  */
	{ .vendor = 0x04e6, .product = 0x0003, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_GETMAXLUN
	},
	/* Shuttle eUSB MultiMediaCard Adapter */
	{ .vendor = 0x04e6, .product = 0x0006, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_GETMAXLUN
	},
	/* Sigmatel i-Bead 100 MP3 */
	{ .vendor = 0x066f, .product = 0x8008, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = SHUTTLE_INIT
	},
	/* SIIG WINTERREADER Reader  */
	{ .vendor = 0x07cc, .product = 0x0330, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* Skanhex MD 7425 Camera */
	{ .vendor = 0x0d96, .product = 0x410a, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Skanhex SX 520z Camera */
	{ .vendor = 0x0d96, .product = 0x5200, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Sony Clie v4.0 */
	{ .vendor = 0x054c, .product = 0x006d, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Sony DSC cameras */
	{ .vendor = 0x054c, .product = 0x0010, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	{ .vendor = 0x054c, .product = 0x02f7, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Sony Handycam   */
	{ .vendor = 0x054c, .product = 0x002e, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Sony Memorystick MSC-U03  */
	{ .vendor = 0x054c, .product = 0x0069, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_UFI | UMASS_PROTO_CBI,
	  .quirks = NO_GETMAXLUN
	},
	/* Sony Memorystick NW-MS7  */
	{ .vendor = 0x054c, .product = 0x0025, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_GETMAXLUN
	},
	/* Sony PEG N760c Memorystick */
	{ .vendor = 0x054c, .product = 0x0058, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Sony Memorystick MSAC-US1  */
	{ .vendor = 0x054c, .product = 0x002d, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_GETMAXLUN
	},
	/* Sony MSC memory stick */
	{ .vendor = 0x054c, .product = 0x0032, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Sony Portable USB Harddrive */
	{ .vendor = 0x054c, .product = 0x002b, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Taugagreining CameraMate (DPCM_USB)  */
	{ .vendor = 0x0436, .product = 0x0005, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* TEAC FD-05PUB floppy  */
	{ .vendor = 0x0644, .product = 0x0000, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_UFI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Trek IBM USB Memory */
	{ .vendor = 0x0a16, .product = 0x8888, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Trek ThumbDrive_8MB   */
	{ .vendor = 0x0a16, .product = 0x9988, .release = WILDCARD_ID,
          .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_BBB,
	  .quirks = IGNORE_RESIDUE
	},
	/* Trumpion Comotron C3310 MP3 */
	{ .vendor = 0x090a, .product = 0x1100, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_UFI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	/* Trumpion MP3 player  */
	{ .vendor = 0x090a, .product = 0x1200, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_RBC,
	  .quirks = NO_QUIRKS
	},
	/* Trumpion T33520 USB Flash */
	{ .vendor = 0x090a, .product = 0x1001, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI,
	  .quirks = NO_QUIRKS
	},
	/* TwinMOS Memory Disk IV */
	{ .vendor = 0x126f, .product = 0x1325, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_QUIRKS
	},
	/* Vivitar Vivicam 35Xx  */
	{ .vendor = 0x0636, .product = 0x0003, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Western Firewire USB Combo */
	{ .vendor = 0x1058, .product = 0x0200, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Western External USB HHD My Passport */
	{ .vendor = 0x1058, .product = 0x0704, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Western External HDD  */
	{ .vendor = 0x1058, .product = 0x0400, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Western MyBook External HDD */
	{ .vendor = 0x1058, .product = 0x0901, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY_EVPD
	},
	/* WinMaxGroup USB Flash Disk */
	{ .vendor = 0x0ed1, .product = 0x6660, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = NO_INQUIRY
	},
	/* Yano METALWEAR-HDD   */
	{ .vendor = 0x094f, .product = 0x05fc, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_BBB,
	  .quirks = FORCE_SHORT_INQUIRY | NO_START_STOP | IGNORE_RESIDUE
	},
	/* Yano U640MO-03 */
	{ .vendor = 0x094f, .product = 0x0101, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI_I,
	  .quirks = FORCE_SHORT_INQUIRY
	},
	/* Y-E Flashbuster-U   */
	{ .vendor = 0x057b, .product = 0x0000, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_SCSI | UMASS_PROTO_CBI,
	  .quirks = NO_GETMAXLUN
	},
	/* Zoran Digital Camera EX-20 */
	{ .vendor = 0x0595, .product = 0x4343, .release = WILDCARD_ID,
	  .proto  = UMASS_PROTO_ATAPI | UMASS_PROTO_CBI,
	  .quirks = NO_QUIRKS
	},
	{ .vendor = EOT_ID, .product = EOT_ID, .release = EOT_ID,
	  .proto  = 0, .quirks = 0 }
};


/* the per device structure */
struct umass_softc {
	device_t		sc_dev;		/* base device */
	usbd_device_handle	sc_udev;	/* USB device */

	struct cam_sim		*umass_sim;	/* SCSI Interface Module */

	unsigned char		flags;		/* various device flags */
#	define UMASS_FLAGS_GONE		0x01	/* devices is no more */

	u_int16_t		proto;		/* wire and cmd protocol */
	u_int16_t		quirks;		/* they got it almost right */

	usbd_interface_handle	iface;		/* Mass Storage interface */
	int			ifaceno;	/* MS iface number */

	u_int8_t		bulkin;		/* bulk-in Endpoint Address */
	u_int8_t		bulkout;	/* bulk-out Endpoint Address */
	u_int8_t		intrin;		/* intr-in Endp. (CBI) */
	usbd_pipe_handle	bulkin_pipe;
	usbd_pipe_handle	bulkout_pipe;
	usbd_pipe_handle	intrin_pipe;

	/* Reset the device in a wire protocol specific way */
	wire_reset_f		reset;

	/* The start of a wire transfer. It prepares the whole transfer (cmd,
	 * data, and status stage) and initiates it. It is up to the state
	 * machine (below) to handle the various stages and errors in these
	 */
	wire_transfer_f		transfer;

	/* The state machine, handling the various states during a transfer */
	wire_state_f		state;

	/* The command transform function is used to conver the SCSI commands
	 * into their derivatives, like UFI, ATAPI, and friends.
	 */
	command_transform_f	transform;	/* command transform */

	/* Bulk specific variables for transfers in progress */
	umass_bbb_cbw_t		cbw;	/* command block wrapper */
	umass_bbb_csw_t		csw;	/* command status wrapper*/
	/* CBI specific variables for transfers in progress */
	umass_cbi_cbl_t		cbl;	/* command block */
	umass_cbi_sbl_t		sbl;	/* status block */

	/* generic variables for transfers in progress */
	/* ctrl transfer requests */
	usb_device_request_t	request;

	/* xfer handles
	 * Most of our operations are initiated from interrupt context, so
	 * we need to avoid using the one that is in use. We want to avoid
	 * allocating them in the interrupt context as well.
	 */
	/* indices into array below */
#	define XFER_BBB_CBW		0	/* Bulk-Only */
#	define XFER_BBB_DATA		1
#	define XFER_BBB_DCLEAR		2
#	define XFER_BBB_CSW1		3
#	define XFER_BBB_CSW2		4
#	define XFER_BBB_SCLEAR		5
#	define XFER_BBB_RESET1		6
#	define XFER_BBB_RESET2		7
#	define XFER_BBB_RESET3		8

#	define XFER_CBI_CB		0	/* CBI */
#	define XFER_CBI_DATA		1
#	define XFER_CBI_STATUS		2
#	define XFER_CBI_DCLEAR		3
#	define XFER_CBI_SCLEAR		4
#	define XFER_CBI_RESET1		5
#	define XFER_CBI_RESET2		6
#	define XFER_CBI_RESET3		7

#	define XFER_NR			9	/* maximum number */

	usbd_xfer_handle	transfer_xfer[XFER_NR];	/* for ctrl xfers */

	int			transfer_dir;		/* data direction */
	void			*transfer_data;		/* data buffer */
	int			transfer_datalen;	/* (maximum) length */
	int			transfer_actlen;	/* actual length */
	transfer_cb_f		transfer_cb;		/* callback */
	void			*transfer_priv;		/* for callback */
	int			transfer_status;

	int			transfer_state;
#	define TSTATE_ATTACH			0	/* in attach */
#	define TSTATE_IDLE			1
#	define TSTATE_BBB_COMMAND		2	/* CBW transfer */
#	define TSTATE_BBB_DATA			3	/* Data transfer */
#	define TSTATE_BBB_DCLEAR		4	/* clear endpt stall */
#	define TSTATE_BBB_STATUS1		5	/* clear endpt stall */
#	define TSTATE_BBB_SCLEAR		6	/* clear endpt stall */
#	define TSTATE_BBB_STATUS2		7	/* CSW transfer */
#	define TSTATE_BBB_RESET1		8	/* reset command */
#	define TSTATE_BBB_RESET2		9	/* in clear stall */
#	define TSTATE_BBB_RESET3		10	/* out clear stall */
#	define TSTATE_CBI_COMMAND		11	/* command transfer */
#	define TSTATE_CBI_DATA			12	/* data transfer */
#	define TSTATE_CBI_STATUS		13	/* status transfer */
#	define TSTATE_CBI_DCLEAR		14	/* clear ep stall */
#	define TSTATE_CBI_SCLEAR		15	/* clear ep stall */
#	define TSTATE_CBI_RESET1		16	/* reset command */
#	define TSTATE_CBI_RESET2		17	/* in clear stall */
#	define TSTATE_CBI_RESET3		18	/* out clear stall */
#	define TSTATE_STATES			19	/* # of states above */


	/* SCSI/CAM specific variables */
	unsigned char 		cam_scsi_command[CAM_MAX_CDBLEN];
	unsigned char 		cam_scsi_command2[CAM_MAX_CDBLEN];
	struct scsi_sense	cam_scsi_sense;
	struct scsi_sense	cam_scsi_test_unit_ready;
	int			timeout;		/* in msecs */

	int			maxlun;			/* maximum LUN number */
	struct callout		rescan_timeout;
};

#ifdef USB_DEBUG
char *states[TSTATE_STATES+1] = {
	/* should be kept in sync with the list at transfer_state */
	"Attach",
	"Idle",
	"BBB CBW",
	"BBB Data",
	"BBB Data bulk-in/-out clear stall",
	"BBB CSW, 1st attempt",
	"BBB CSW bulk-in clear stall",
	"BBB CSW, 2nd attempt",
	"BBB Reset",
	"BBB bulk-in clear stall",
	"BBB bulk-out clear stall",
	"CBI Command",
	"CBI Data",
	"CBI Status",
	"CBI Data bulk-in/-out clear stall",
	"CBI Status intr-in clear stall",
	"CBI Reset",
	"CBI bulk-in clear stall",
	"CBI bulk-out clear stall",
	NULL
};
#endif

/* If device cannot return valid inquiry data, fake it */
static uint8_t fake_inq_data[SHORT_INQUIRY_LENGTH] = {
	0, /*removable*/ 0x80, SCSI_REV_2, SCSI_REV_2,
	/*additional_length*/ 31, 0, 0, 0
};

/* USB device probe/attach/detach functions */
static device_probe_t umass_match;
static device_attach_t umass_attach;
static device_detach_t umass_detach;

static devclass_t umass_devclass;

static kobj_method_t umass_methods[] = {
	DEVMETHOD(device_probe, umass_match),
	DEVMETHOD(device_attach, umass_attach),
	DEVMETHOD(device_detach, umass_detach),
	{0,0},
	{0,0}
};

static driver_t umass_driver = {
	"umass",
	umass_methods,
	sizeof(struct umass_softc)
};

MODULE_DEPEND(umass, usb, 1, 1, 1);

static int umass_match_proto	(struct umass_softc *sc,
				usbd_interface_handle iface,
				usbd_device_handle udev);

/* quirk functions */
static void umass_init_shuttle	(struct umass_softc *sc);

/* generic transfer functions */
static usbd_status umass_setup_transfer	(struct umass_softc *sc,
				usbd_pipe_handle pipe,
				void *buffer, int buflen, int flags,
				usbd_xfer_handle xfer);
static usbd_status umass_setup_ctrl_transfer	(struct umass_softc *sc,
				usbd_device_handle udev,
				usb_device_request_t *req,
				void *buffer, int buflen, int flags,
				usbd_xfer_handle xfer);
static void umass_clear_endpoint_stall	(struct umass_softc *sc,
				u_int8_t endpt, usbd_pipe_handle pipe,
				int state, usbd_xfer_handle xfer);
static void umass_reset		(struct umass_softc *sc,
				transfer_cb_f cb, void *priv);

/* Bulk-Only related functions */
static void umass_bbb_reset	(struct umass_softc *sc, int status);
static void umass_bbb_transfer	(struct umass_softc *sc, int lun,
				void *cmd, int cmdlen,
		    		void *data, int datalen, int dir, u_int timeout,
				transfer_cb_f cb, void *priv);
static void umass_bbb_state	(usbd_xfer_handle xfer,
				usbd_private_handle priv,
				usbd_status err);
static int umass_bbb_get_max_lun
				(struct umass_softc *sc);

/* CBI related functions */
static int umass_cbi_adsc	(struct umass_softc *sc,
				char *buffer, int buflen,
				usbd_xfer_handle xfer);
static void umass_cbi_reset	(struct umass_softc *sc, int status);
static void umass_cbi_transfer	(struct umass_softc *sc, int lun,
				void *cmd, int cmdlen,
		    		void *data, int datalen, int dir, u_int timeout,
				transfer_cb_f cb, void *priv);
static void umass_cbi_state	(usbd_xfer_handle xfer,
				usbd_private_handle priv, usbd_status err);

/* CAM related functions */
static void umass_cam_action	(struct cam_sim *sim, union ccb *ccb);
static void umass_cam_poll	(struct cam_sim *sim);

static void umass_cam_cb	(struct umass_softc *sc, void *priv,
				int residue, int status);
static void umass_cam_sense_cb	(struct umass_softc *sc, void *priv,
				int residue, int status);
static void umass_cam_quirk_cb	(struct umass_softc *sc, void *priv,
				int residue, int status);

static void umass_cam_rescan_callback
				(struct cam_periph *periph,union ccb *ccb);
static void umass_cam_rescan	(void *addr);

static int umass_cam_attach_sim	(struct umass_softc *sc);
static int umass_cam_attach	(struct umass_softc *sc);
static int umass_cam_detach_sim	(struct umass_softc *sc);


/* SCSI specific functions */
static int umass_scsi_transform	(struct umass_softc *sc,
				unsigned char *cmd, int cmdlen,
		     		unsigned char **rcmd, int *rcmdlen);

/* UFI specific functions */
#define UFI_COMMAND_LENGTH	12	/* UFI commands are always 12 bytes */
static int umass_ufi_transform	(struct umass_softc *sc,
				unsigned char *cmd, int cmdlen,
		    		unsigned char **rcmd, int *rcmdlen);

/* ATAPI (8070i) specific functions */
#define ATAPI_COMMAND_LENGTH	12	/* ATAPI commands are always 12 bytes */
static int umass_atapi_transform	(struct umass_softc *sc,
				unsigned char *cmd, int cmdlen,
		     		unsigned char **rcmd, int *rcmdlen);

/* RBC specific functions */
static int umass_rbc_transform	(struct umass_softc *sc,
				unsigned char *cmd, int cmdlen,
		     		unsigned char **rcmd, int *rcmdlen);

#ifdef USB_DEBUG
/* General debugging functions */
static void umass_bbb_dump_cbw	(struct umass_softc *sc, umass_bbb_cbw_t *cbw);
static void umass_bbb_dump_csw	(struct umass_softc *sc, umass_bbb_csw_t *csw);
static void umass_cbi_dump_cmd	(struct umass_softc *sc, void *cmd, int cmdlen);
static void umass_dump_buffer	(struct umass_softc *sc, u_int8_t *buffer,
				int buflen, int printlen);
#endif

MODULE_DEPEND(umass, cam, 1,1,1);

/*
 * USB device probe/attach/detach
 */

/*
 * Match the device we are seeing with the devices supported. Fill in the
 * description in the softc accordingly. This function is called from both
 * probe and attach.
 */

static int
umass_match_proto(struct umass_softc *sc, usbd_interface_handle iface,
		  usbd_device_handle udev)
{
	usb_device_descriptor_t *dd;
	usb_interface_descriptor_t *id;
	int i;
	int found = 0;

	sc->sc_udev = udev;
	sc->proto = 0;
	sc->quirks = 0;

	dd = usbd_get_device_descriptor(udev);

	/* An entry specifically for Y-E Data devices as they don't fit in the
	 * device description table.
	 */
	if (UGETW(dd->idVendor) == 0x057b && UGETW(dd->idProduct) == 0x0000) {

		/* Revisions < 1.28 do not handle the inerrupt endpoint
		 * very well.
		 */
		if (UGETW(dd->bcdDevice) < 0x128) {
			sc->proto = UMASS_PROTO_UFI | UMASS_PROTO_CBI;
		} else {
			sc->proto = UMASS_PROTO_UFI | UMASS_PROTO_CBI_I;
		}

		/*
		 * Revisions < 1.28 do not have the TEST UNIT READY command
		 * Revisions == 1.28 have a broken TEST UNIT READY
		 */
		if (UGETW(dd->bcdDevice) <= 0x128)
			sc->quirks |= NO_TEST_UNIT_READY;

		sc->quirks |= RS_NO_CLEAR_UA | FLOPPY_SPEED;
		return(UMATCH_VENDOR_PRODUCT);
	}

	/* Check the list of supported devices for a match. While looking,
	 * check for wildcarded and fully matched. First match wins.
	 */
	for (i = 0; umass_devdescrs[i].vendor != EOT_ID && !found; i++) {
		if (umass_devdescrs[i].vendor == WILDCARD_ID &&
		    umass_devdescrs[i].product == WILDCARD_ID &&
		    umass_devdescrs[i].release == WILDCARD_ID) {
			kprintf("umass: ignoring invalid wildcard quirk\n");
			continue;
		}
		if ((umass_devdescrs[i].vendor == UGETW(dd->idVendor) ||
		     umass_devdescrs[i].vendor == WILDCARD_ID)
		 && (umass_devdescrs[i].product == UGETW(dd->idProduct) ||
		     umass_devdescrs[i].product == WILDCARD_ID)) {
			if (umass_devdescrs[i].release == WILDCARD_ID) {
				sc->proto = umass_devdescrs[i].proto;
				sc->quirks = umass_devdescrs[i].quirks;
				return (UMATCH_VENDOR_PRODUCT);
			} else if (umass_devdescrs[i].release ==
			    UGETW(dd->bcdDevice)) {
				sc->proto = umass_devdescrs[i].proto;
				sc->quirks = umass_devdescrs[i].quirks;
				return (UMATCH_VENDOR_PRODUCT_REV);
			} /* else RID does not match */
		}
	}

	/* Check for a standards compliant device */

	id = usbd_get_interface_descriptor(iface);
	if (id == NULL || id->bInterfaceClass != UICLASS_MASS)
		return(UMATCH_NONE);
	
	switch (id->bInterfaceSubClass) {
	case UISUBCLASS_SCSI:
		sc->proto |= UMASS_PROTO_SCSI;
		break;
	case UISUBCLASS_UFI:
		sc->proto |= UMASS_PROTO_UFI;
		break;
	case UISUBCLASS_RBC:
		sc->proto |= UMASS_PROTO_RBC;
		break;
	case UISUBCLASS_SFF8020I:
	case UISUBCLASS_SFF8070I:
		sc->proto |= UMASS_PROTO_ATAPI;
		break;
	default:
		DPRINTF(UDMASS_GEN, ("%s: Unsupported command protocol %d\n",
			device_get_nameunit(sc->sc_dev), id->bInterfaceSubClass));
		return(UMATCH_NONE);
	}

	switch (id->bInterfaceProtocol) {
	case UIPROTO_MASS_CBI:
		sc->proto |= UMASS_PROTO_CBI;
		break;
	case UIPROTO_MASS_CBI_I:
		sc->proto |= UMASS_PROTO_CBI_I;
		break;
	case UIPROTO_MASS_BBB_OLD:
	case UIPROTO_MASS_BBB:
		sc->proto |= UMASS_PROTO_BBB;
		break;
	default:
		DPRINTF(UDMASS_GEN, ("%s: Unsupported wire protocol %d\n",
			device_get_nameunit(sc->sc_dev), id->bInterfaceProtocol));
		return(UMATCH_NONE);
	}

	return(UMATCH_DEVCLASS_DEVSUBCLASS_DEVPROTO);
}

static int
umass_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct umass_softc *sc = device_get_softc(self);

	sc->sc_dev = self;

	if (uaa->iface == NULL)
		return(UMATCH_NONE);

	return(umass_match_proto(sc, uaa->iface, uaa->device));
}

static int
umass_attach(device_t self)
{
	struct umass_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;
	int err;

	/*
	 * the softc struct is bzero-ed in device_set_driver. We can safely
	 * call umass_detach without specifically initialising the struct.
	 */

	sc->sc_dev = self;

	sc->iface = uaa->iface;
	sc->ifaceno = uaa->ifaceno;

	/* initialise the proto and drive values in the umass_softc (again) */
	(void) umass_match_proto(sc, sc->iface, uaa->device);

	id = usbd_get_interface_descriptor(sc->iface);
#ifdef USB_DEBUG
	kprintf("%s: ", device_get_nameunit(sc->sc_dev));
	switch (sc->proto&UMASS_PROTO_COMMAND) {
	case UMASS_PROTO_SCSI:
		kprintf("SCSI");
		break;
	case UMASS_PROTO_ATAPI:
		kprintf("8070i (ATAPI)");
		break;
	case UMASS_PROTO_UFI:
		kprintf("UFI");
		break;
	case UMASS_PROTO_RBC:
		kprintf("RBC");
		break;
	default:
		kprintf("(unknown 0x%02x)", sc->proto&UMASS_PROTO_COMMAND);
		break;
	}
	kprintf(" over ");
	switch (sc->proto&UMASS_PROTO_WIRE) {
	case UMASS_PROTO_BBB:
		kprintf("Bulk-Only");
		break;
	case UMASS_PROTO_CBI:			/* uses Comand/Bulk pipes */
		kprintf("CBI");
		break;
	case UMASS_PROTO_CBI_I:		/* uses Comand/Bulk/Interrupt pipes */
		kprintf("CBI with CCI");
#ifndef CBI_I
		kprintf(" (using CBI)");
#endif
		break;
	default:
		kprintf("(unknown 0x%02x)", sc->proto&UMASS_PROTO_WIRE);
	}
	kprintf("; quirks = 0x%04x\n", sc->quirks);
#endif

#ifndef CBI_I
	if (sc->proto & UMASS_PROTO_CBI_I) {
		/* See beginning of file for comment on the use of CBI with CCI */
		sc->proto = (sc->proto & ~UMASS_PROTO_CBI_I) | UMASS_PROTO_CBI;
	}
#endif

	if (sc->quirks & ALT_IFACE_1) {
		err = usbd_set_interface(0, 1);
		if (err) {
			DPRINTF(UDMASS_USB, ("%s: could not switch to "
				"Alt Interface %d\n",
				device_get_nameunit(sc->sc_dev), 1));
			umass_detach(self);
			return ENXIO;
		}
	}

	/*
	 * In addition to the Control endpoint the following endpoints
	 * are required:
	 * a) bulk-in endpoint.
	 * b) bulk-out endpoint.
	 * and for Control/Bulk/Interrupt with CCI (CBI_I)
	 * c) intr-in
	 *
	 * The endpoint addresses are not fixed, so we have to read them
	 * from the device descriptors of the current interface.
	 */
	for (i = 0 ; i < id->bNumEndpoints ; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->iface, i);
		if (!ed) {
			kprintf("%s: could not read endpoint descriptor\n",
			       device_get_nameunit(sc->sc_dev));
			return ENXIO;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN
		    && (ed->bmAttributes & UE_XFERTYPE) == UE_BULK) {
			sc->bulkin = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT
		    && (ed->bmAttributes & UE_XFERTYPE) == UE_BULK) {
			sc->bulkout = ed->bEndpointAddress;
		} else if (sc->proto & UMASS_PROTO_CBI_I
		    && UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN
		    && (ed->bmAttributes & UE_XFERTYPE) == UE_INTERRUPT) {
			sc->intrin = ed->bEndpointAddress;
#ifdef USB_DEBUG
			if (UGETW(ed->wMaxPacketSize) > 2) {
				DPRINTF(UDMASS_CBI, ("%s: intr size is %d\n",
					device_get_nameunit(sc->sc_dev),
					UGETW(ed->wMaxPacketSize)));
			}
#endif
		}
	}

	/* check whether we found all the endpoints we need */
	if (!sc->bulkin || !sc->bulkout
	    || (sc->proto & UMASS_PROTO_CBI_I && !sc->intrin) ) {
	    	DPRINTF(UDMASS_USB, ("%s: endpoint not found %d/%d/%d\n",
			device_get_nameunit(sc->sc_dev),
			sc->bulkin, sc->bulkout, sc->intrin));
		umass_detach(self);
		return ENXIO;
	}

	/* Open the bulk-in and -out pipe */
	err = usbd_open_pipe(sc->iface, sc->bulkout,
				USBD_EXCLUSIVE_USE, &sc->bulkout_pipe);
	if (err) {
		DPRINTF(UDMASS_USB, ("%s: cannot open %d-out pipe (bulk)\n",
			device_get_nameunit(sc->sc_dev), sc->bulkout));
		umass_detach(self);
		return ENXIO;
	}
	err = usbd_open_pipe(sc->iface, sc->bulkin,
				USBD_EXCLUSIVE_USE, &sc->bulkin_pipe);
	if (err) {
		DPRINTF(UDMASS_USB, ("%s: could not open %d-in pipe (bulk)\n",
			device_get_nameunit(sc->sc_dev), sc->bulkin));
		umass_detach(self);
		return ENXIO;
	}
	/* Open the intr-in pipe if the protocol is CBI with CCI.
	 * Note: early versions of the Zip drive do have an interrupt pipe, but
	 * this pipe is unused
	 *
	 * We do not open the interrupt pipe as an interrupt pipe, but as a
	 * normal bulk endpoint. We send an IN transfer down the wire at the
	 * appropriate time, because we know exactly when to expect data on
	 * that endpoint. This saves bandwidth, but more important, makes the
	 * code for handling the data on that endpoint simpler. No data
	 * arriving concurently.
	 */
	if (sc->proto & UMASS_PROTO_CBI_I) {
		err = usbd_open_pipe(sc->iface, sc->intrin,
				USBD_EXCLUSIVE_USE, &sc->intrin_pipe);
		if (err) {
			DPRINTF(UDMASS_USB, ("%s: couldn't open %d-in (intr)\n",
				device_get_nameunit(sc->sc_dev), sc->intrin));
			umass_detach(self);
			return ENXIO;
		}
	}

	/* initialisation of generic part */
	sc->transfer_state = TSTATE_ATTACH;

	/* request a sufficient number of xfer handles */
	for (i = 0; i < XFER_NR; i++) {
		sc->transfer_xfer[i] = usbd_alloc_xfer(uaa->device);
		if (!sc->transfer_xfer[i]) {
			DPRINTF(UDMASS_USB, ("%s: Out of memory\n",
				device_get_nameunit(sc->sc_dev)));
			umass_detach(self);
			return ENXIO;
		}
	}

	/*
	 * Preallocate buffers to avoid auto-allocation from an interrupt
	 * handler.
	 */
	usbd_alloc_buffer(sc->transfer_xfer[XFER_BBB_DATA],
			  MAXPHYS);
	usbd_alloc_buffer(sc->transfer_xfer[XFER_BBB_CBW],
			  UMASS_BBB_CBW_SIZE);
	usbd_alloc_buffer(sc->transfer_xfer[XFER_BBB_CSW1],
			  UMASS_BBB_CSW_SIZE);
	usbd_alloc_buffer(sc->transfer_xfer[XFER_BBB_CSW2],
			  UMASS_BBB_CSW_SIZE);
	usbd_alloc_buffer(sc->transfer_xfer[XFER_CBI_DATA],
			  MAXPHYS);

	/* Initialise the wire protocol specific methods */
	if (sc->proto & UMASS_PROTO_BBB) {
		sc->reset = umass_bbb_reset;
		sc->transfer = umass_bbb_transfer;
		sc->state = umass_bbb_state;
	} else if (sc->proto & (UMASS_PROTO_CBI|UMASS_PROTO_CBI_I)) {
		sc->reset = umass_cbi_reset;
		sc->transfer = umass_cbi_transfer;
		sc->state = umass_cbi_state;
#ifdef USB_DEBUG
	} else {
		panic("%s:%d: Unknown proto 0x%02x",
		      __FILE__, __LINE__, sc->proto);
#endif
	}

	if (sc->proto & UMASS_PROTO_SCSI)
		sc->transform = umass_scsi_transform;
	else if (sc->proto & UMASS_PROTO_UFI)
		sc->transform = umass_ufi_transform;
	else if (sc->proto & UMASS_PROTO_ATAPI)
		sc->transform = umass_atapi_transform;
	else if (sc->proto & UMASS_PROTO_RBC)
		sc->transform = umass_rbc_transform;
#ifdef USB_DEBUG
	else
		panic("No transformation defined for command proto 0x%02x",
		      sc->proto & UMASS_PROTO_COMMAND);
#endif

	/* From here onwards the device can be used. */

	if (sc->quirks & SHUTTLE_INIT)
		umass_init_shuttle(sc);

	/* Get the maximum LUN supported by the device.
	 */
	if (((sc->proto & UMASS_PROTO_WIRE) == UMASS_PROTO_BBB) &&
	    !(sc->quirks & NO_GETMAXLUN))
		sc->maxlun = umass_bbb_get_max_lun(sc);
	else
		sc->maxlun = 0;

	if ((sc->proto & UMASS_PROTO_SCSI) ||
	    (sc->proto & UMASS_PROTO_ATAPI) ||
	    (sc->proto & UMASS_PROTO_UFI) ||
	    (sc->proto & UMASS_PROTO_RBC)) {
		/* Prepare the SCSI command block */
		sc->cam_scsi_sense.opcode = REQUEST_SENSE;
		sc->cam_scsi_test_unit_ready.opcode = TEST_UNIT_READY;

		/* register the SIM */
		err = umass_cam_attach_sim(sc);
		if (err) {
			umass_detach(self);
			return ENXIO;
		}
		/* scan the new sim */
		err = umass_cam_attach(sc);
		if (err) {
			umass_cam_detach_sim(sc);
			umass_detach(self);
			return ENXIO;
		}
	} else {
		panic("%s:%d: Unknown proto 0x%02x",
		      __FILE__, __LINE__, sc->proto);
	}

	sc->transfer_state = TSTATE_IDLE;
	DPRINTF(UDMASS_GEN, ("%s: Attach finished\n", device_get_nameunit(sc->sc_dev)));

	return 0;
}

static int
umass_detach(device_t self)
{
	struct umass_softc *sc = device_get_softc(self);
	int err = 0;
	int i;
	int to;

	DPRINTF(UDMASS_USB, ("%s: detached\n", device_get_nameunit(sc->sc_dev)));

	/*
	 * Set UMASS_FLAGS_GONE to prevent any new transfers from being
	 * queued, and abort any transfers in progress to ensure that
	 * pending requests (e.g. from CAM's bus scan) are terminated.
	 */
	sc->flags |= UMASS_FLAGS_GONE;

	if (sc->bulkout_pipe)
		usbd_abort_pipe(sc->bulkout_pipe);
	if (sc->bulkin_pipe)
		usbd_abort_pipe(sc->bulkin_pipe);
	if (sc->intrin_pipe)
		usbd_abort_pipe(sc->intrin_pipe);

	/*
	 * Wait until we go idle to make sure that all of our xfer requests
	 * have finished.  We could be in the middle of a BBB reset (which
	 * would not be effected by the pipe aborts above).
	 */
	to = hz;
	while (sc->transfer_state != TSTATE_IDLE) {
		kprintf("%s: state %d waiting for idle\n",
		    device_get_nameunit(sc->sc_dev), sc->transfer_state);
		tsleep(sc, 0, "umassidl", to);
		if (to >= hz * 10) {
			kprintf("%s: state %d giving up!\n",
			    device_get_nameunit(sc->sc_dev), sc->transfer_state);
			break;
		}
		to += hz;
	}

	if ((sc->proto & UMASS_PROTO_SCSI) ||
	    (sc->proto & UMASS_PROTO_ATAPI) ||
	    (sc->proto & UMASS_PROTO_UFI) ||
	    (sc->proto & UMASS_PROTO_RBC)) {
		/* detach the SCSI host controller (SIM) */
		err = umass_cam_detach_sim(sc);
	}

	for (i = 0; i < XFER_NR; i++) {
		if (sc->transfer_xfer[i])
			usbd_free_xfer(sc->transfer_xfer[i]);
	}

	/* remove all the pipes */
	if (sc->bulkout_pipe)
		usbd_close_pipe(sc->bulkout_pipe);
	if (sc->bulkin_pipe)
		usbd_close_pipe(sc->bulkin_pipe);
	if (sc->intrin_pipe)
		usbd_close_pipe(sc->intrin_pipe);

	return(err);
}

static void
umass_init_shuttle(struct umass_softc *sc)
{
	usb_device_request_t req;
	u_char status[2];

	/* The Linux driver does this, but no one can tell us what the
	 * command does.
	 */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = 1;	/* XXX unknown command */
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->ifaceno);
	USETW(req.wLength, sizeof status);
	(void) usbd_do_request(sc->sc_udev, &req, &status);

	DPRINTF(UDMASS_GEN, ("%s: Shuttle init returned 0x%02x%02x\n",
		device_get_nameunit(sc->sc_dev), status[0], status[1]));
}

 /*
 * Generic functions to handle transfers
 */

static usbd_status
umass_setup_transfer(struct umass_softc *sc, usbd_pipe_handle pipe,
			void *buffer, int buflen, int flags,
			usbd_xfer_handle xfer)
{
	usbd_status err;

	/* Initialiase a USB transfer and then schedule it */

	(void) usbd_setup_xfer(xfer, pipe, (void *) sc, buffer, buflen, flags,
			sc->timeout, sc->state);

	err = usbd_transfer(xfer);
	if (err && err != USBD_IN_PROGRESS) {
		DPRINTF(UDMASS_BBB, ("%s: failed to setup transfer, %s\n",
			device_get_nameunit(sc->sc_dev), usbd_errstr(err)));
		return(err);
	}

	return (USBD_NORMAL_COMPLETION);
}


static usbd_status
umass_setup_ctrl_transfer(struct umass_softc *sc, usbd_device_handle udev,
	 usb_device_request_t *req,
	 void *buffer, int buflen, int flags,
	 usbd_xfer_handle xfer)
{
	usbd_status err;

	/* Initialiase a USB control transfer and then schedule it */

	(void) usbd_setup_default_xfer(xfer, udev, (void *) sc,
			UMASS_TIMEOUT, req, buffer, buflen, flags, sc->state);

	err = usbd_transfer(xfer);
	if (err && err != USBD_IN_PROGRESS) {
		DPRINTF(UDMASS_BBB, ("%s: failed to setup ctrl transfer, %s\n",
			 device_get_nameunit(sc->sc_dev), usbd_errstr(err)));

		/* do not reset, as this would make us loop */
		return(err);
	}

	return (USBD_NORMAL_COMPLETION);
}

static void
umass_clear_endpoint_stall(struct umass_softc *sc,
				u_int8_t endpt, usbd_pipe_handle pipe,
				int state, usbd_xfer_handle xfer)
{
	usbd_device_handle udev;

	DPRINTF(UDMASS_BBB, ("%s: Clear endpoint 0x%02x stall\n",
		device_get_nameunit(sc->sc_dev), endpt));

	usbd_interface2device_handle(sc->iface, &udev);

	sc->transfer_state = state;

	usbd_clear_endpoint_toggle(pipe);

	sc->request.bmRequestType = UT_WRITE_ENDPOINT;
	sc->request.bRequest = UR_CLEAR_FEATURE;
	USETW(sc->request.wValue, UF_ENDPOINT_HALT);
	USETW(sc->request.wIndex, endpt);
	USETW(sc->request.wLength, 0);
	umass_setup_ctrl_transfer(sc, udev, &sc->request, NULL, 0, 0, xfer);
}

static void
umass_reset(struct umass_softc *sc, transfer_cb_f cb, void *priv)
{
	sc->transfer_cb = cb;
	sc->transfer_priv = priv;

	/* The reset is a forced reset, so no error (yet) */
	sc->reset(sc, STATUS_CMD_OK);
}

/*
 * Bulk protocol specific functions
 */

static void
umass_bbb_reset(struct umass_softc *sc, int status)
{
	usbd_device_handle udev;

	KASSERT(sc->proto & UMASS_PROTO_BBB,
		("%s: umass_bbb_reset: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));

	/*
	 * Reset recovery (5.3.4 in Universal Serial Bus Mass Storage Class)
	 *
	 * For Reset Recovery the host shall issue in the following order:
	 * a) a Bulk-Only Mass Storage Reset
	 * b) a Clear Feature HALT to the Bulk-In endpoint
	 * c) a Clear Feature HALT to the Bulk-Out endpoint
	 *
	 * This is done in 3 steps, states:
	 * TSTATE_BBB_RESET1
	 * TSTATE_BBB_RESET2
	 * TSTATE_BBB_RESET3
	 *
	 * If the reset doesn't succeed, the device should be port reset.
	 */

	DPRINTF(UDMASS_BBB, ("%s: Bulk Reset\n",
		device_get_nameunit(sc->sc_dev)));

	sc->transfer_state = TSTATE_BBB_RESET1;
	sc->transfer_status = status;

	usbd_interface2device_handle(sc->iface, &udev);

	/* reset is a class specific interface write */
	sc->request.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	sc->request.bRequest = UR_BBB_RESET;
	USETW(sc->request.wValue, 0);
	USETW(sc->request.wIndex, sc->ifaceno);
	USETW(sc->request.wLength, 0);
	umass_setup_ctrl_transfer(sc, udev, &sc->request, NULL, 0, 0,
				  sc->transfer_xfer[XFER_BBB_RESET1]);
}

static void
umass_bbb_transfer(struct umass_softc *sc, int lun, void *cmd, int cmdlen,
		    void *data, int datalen, int dir, u_int timeout,
		    transfer_cb_f cb, void *priv)
{
	KASSERT(sc->proto & UMASS_PROTO_BBB,
		("%s: umass_bbb_transfer: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));
	/* Be a little generous. */
	sc->timeout = timeout + UMASS_TIMEOUT;

	/*
	 * Do a Bulk-Only transfer with cmdlen bytes from cmd, possibly
	 * a data phase of datalen bytes from/to the device and finally a
	 * csw read phase.
	 * If the data direction was inbound a maximum of datalen bytes
	 * is stored in the buffer pointed to by data.
	 *
	 * umass_bbb_transfer initialises the transfer and lets the state
	 * machine in umass_bbb_state handle the completion. It uses the
	 * following states:
	 * TSTATE_BBB_COMMAND
	 *   -> TSTATE_BBB_DATA
	 *   -> TSTATE_BBB_STATUS
	 *   -> TSTATE_BBB_STATUS2
	 *   -> TSTATE_BBB_IDLE
	 *
	 * An error in any of those states will invoke
	 * umass_bbb_reset.
	 */

	/* check the given arguments */
	KASSERT(datalen == 0 || data != NULL,
		("%s: datalen > 0, but no buffer",device_get_nameunit(sc->sc_dev)));
	KASSERT(cmdlen <= CBWCDBLENGTH,
		("%s: cmdlen exceeds CDB length in CBW (%d > %d)",
			device_get_nameunit(sc->sc_dev), cmdlen, CBWCDBLENGTH));
	KASSERT(dir == DIR_NONE || datalen > 0,
		("%s: datalen == 0 while direction is not NONE",
			device_get_nameunit(sc->sc_dev)));
	KASSERT(datalen == 0 || dir != DIR_NONE,
		("%s: direction is NONE while datalen is not zero",
			device_get_nameunit(sc->sc_dev)));
	KASSERT(sizeof(umass_bbb_cbw_t) == UMASS_BBB_CBW_SIZE,
		("%s: CBW struct does not have the right size (%ld vs. %d)",
			device_get_nameunit(sc->sc_dev),
			(long)sizeof(umass_bbb_cbw_t), UMASS_BBB_CBW_SIZE));
	KASSERT(sizeof(umass_bbb_csw_t) == UMASS_BBB_CSW_SIZE,
		("%s: CSW struct does not have the right size (%ld vs. %d)",
			device_get_nameunit(sc->sc_dev),
			(long)sizeof(umass_bbb_csw_t), UMASS_BBB_CSW_SIZE));

	/*
	 * Determine the direction of the data transfer and the length.
	 *
	 * dCBWDataTransferLength (datalen) :
	 *   This field indicates the number of bytes of data that the host
	 *   intends to transfer on the IN or OUT Bulk endpoint(as indicated by
	 *   the Direction bit) during the execution of this command. If this
	 *   field is set to 0, the device will expect that no data will be
	 *   transferred IN or OUT during this command, regardless of the value
	 *   of the Direction bit defined in dCBWFlags.
	 *
	 * dCBWFlags (dir) :
	 *   The bits of the Flags field are defined as follows:
	 *     Bits 0-6  reserved
	 *     Bit  7    Direction - this bit shall be ignored if the
	 *                           dCBWDataTransferLength field is zero.
	 *               0 = data Out from host to device
	 *               1 = data In from device to host
	 */

	/* Fill in the Command Block Wrapper
	 * We fill in all the fields, so there is no need to bzero it first.
	 */
	USETDW(sc->cbw.dCBWSignature, CBWSIGNATURE);
	/* We don't care about the initial value, as long as the values are unique */
	USETDW(sc->cbw.dCBWTag, UGETDW(sc->cbw.dCBWTag) + 1);
	USETDW(sc->cbw.dCBWDataTransferLength, datalen);
	/* DIR_NONE is treated as DIR_OUT (0x00) */
	sc->cbw.bCBWFlags = (dir == DIR_IN? CBWFLAGS_IN:CBWFLAGS_OUT);
	sc->cbw.bCBWLUN = lun;
	sc->cbw.bCDBLength = cmdlen;
	bcopy(cmd, sc->cbw.CBWCDB, cmdlen);

	DIF(UDMASS_BBB, umass_bbb_dump_cbw(sc, &sc->cbw));

	/* store the details for the data transfer phase */
	sc->transfer_dir = dir;
	sc->transfer_data = data;
	sc->transfer_datalen = datalen;
	sc->transfer_actlen = 0;
	sc->transfer_cb = cb;
	sc->transfer_priv = priv;
	sc->transfer_status = STATUS_CMD_OK;

	/* move from idle to the command state */
	sc->transfer_state = TSTATE_BBB_COMMAND;

	/* Send the CBW from host to device via bulk-out endpoint. */
	if (umass_setup_transfer(sc, sc->bulkout_pipe,
			&sc->cbw, UMASS_BBB_CBW_SIZE, 0,
			sc->transfer_xfer[XFER_BBB_CBW])) {
		umass_bbb_reset(sc, STATUS_WIRE_FAILED);
	}
}


static void
umass_bbb_state(usbd_xfer_handle xfer, usbd_private_handle priv,
		usbd_status err)
{
	struct umass_softc *sc = (struct umass_softc *) priv;
	usbd_xfer_handle next_xfer;

	KASSERT(sc->proto & UMASS_PROTO_BBB,
		("%s: umass_bbb_state: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));

	/*
	 * State handling for BBB transfers.
	 *
	 * The subroutine is rather long. It steps through the states given in
	 * Annex A of the Bulk-Only specification.
	 * Each state first does the error handling of the previous transfer
	 * and then prepares the next transfer.
	 * Each transfer is done asynchroneously so after the request/transfer
	 * has been submitted you will find a 'return;'.
	 */

	DPRINTF(UDMASS_BBB, ("%s: Handling BBB state %d (%s), xfer=%p, %s\n",
		device_get_nameunit(sc->sc_dev), sc->transfer_state,
		states[sc->transfer_state], xfer, usbd_errstr(err)));

	switch (sc->transfer_state) {

	/***** Bulk Transfer *****/
	case TSTATE_BBB_COMMAND:
		/* Command transport phase, error handling */
		if (err) {
			DPRINTF(UDMASS_BBB, ("%s: failed to send CBW\n",
				device_get_nameunit(sc->sc_dev)));
			/* If the device detects that the CBW is invalid, then
			 * the device may STALL both bulk endpoints and require
			 * a Bulk-Reset
			 */
			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;
		}

		/* Data transport phase, setup transfer */
		sc->transfer_state = TSTATE_BBB_DATA;
		if (sc->transfer_dir == DIR_IN) {
			if (umass_setup_transfer(sc, sc->bulkin_pipe,
					sc->transfer_data, sc->transfer_datalen,
					USBD_SHORT_XFER_OK,
					sc->transfer_xfer[XFER_BBB_DATA]))
				umass_bbb_reset(sc, STATUS_WIRE_FAILED);

			return;
		} else if (sc->transfer_dir == DIR_OUT) {
			if (umass_setup_transfer(sc, sc->bulkout_pipe,
					sc->transfer_data, sc->transfer_datalen,
					0,	/* fixed length transfer */
					sc->transfer_xfer[XFER_BBB_DATA]))
				umass_bbb_reset(sc, STATUS_WIRE_FAILED);

			return;
		} else {
			DPRINTF(UDMASS_BBB, ("%s: no data phase\n",
				device_get_nameunit(sc->sc_dev)));
		}

		/* FALLTHROUGH if no data phase, err == 0 */
	case TSTATE_BBB_DATA:
		/* Command transport phase, error handling (ignored if no data
		 * phase (fallthrough from previous state)) */
		if (sc->transfer_dir != DIR_NONE) {
			/* retrieve the length of the transfer that was done */
			usbd_get_xfer_status(xfer, NULL, NULL,
						&sc->transfer_actlen, NULL);

			if (err) {
				DPRINTF(UDMASS_BBB, ("%s: Data-%s %db failed, "
					"%s\n", device_get_nameunit(sc->sc_dev),
					(sc->transfer_dir == DIR_IN?"in":"out"),
					sc->transfer_datalen,usbd_errstr(err)));

				if (err == USBD_STALLED) {
					umass_clear_endpoint_stall(sc,
					  (sc->transfer_dir == DIR_IN?
					    sc->bulkin:sc->bulkout),
					  (sc->transfer_dir == DIR_IN?
					    sc->bulkin_pipe:sc->bulkout_pipe),
					  TSTATE_BBB_DCLEAR,
					  sc->transfer_xfer[XFER_BBB_DCLEAR]);
					return;
				} else {
					/* Unless the error is a pipe stall the
					 * error is fatal.
					 */
					umass_bbb_reset(sc,STATUS_WIRE_FAILED);
					return;
				}
			}
		}

		DIF(UDMASS_BBB, if (sc->transfer_dir == DIR_IN)
					umass_dump_buffer(sc, sc->transfer_data,
						sc->transfer_datalen, 48));



		/* FALLTHROUGH, err == 0 (no data phase or successfull) */
	case TSTATE_BBB_DCLEAR:	/* stall clear after data phase */
	case TSTATE_BBB_SCLEAR:	/* stall clear after status phase */
		/* Reading of CSW after bulk stall condition in data phase
		 * (TSTATE_BBB_DATA2) or bulk-in stall condition after
		 * reading CSW (TSTATE_BBB_SCLEAR).
		 * In the case of no data phase or successfull data phase,
		 * err == 0 and the following if block is passed.
		 */
		if (err) {	/* should not occur */
			/* try the transfer below, even if clear stall failed */
			DPRINTF(UDMASS_BBB, ("%s: bulk-%s stall clear failed"
				", %s\n", device_get_nameunit(sc->sc_dev),
				(sc->transfer_dir == DIR_IN? "in":"out"),
				usbd_errstr(err)));
			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;
		}

		/* Status transport phase, setup transfer */
		if (sc->transfer_state == TSTATE_BBB_COMMAND ||
		    sc->transfer_state == TSTATE_BBB_DATA ||
		    sc->transfer_state == TSTATE_BBB_DCLEAR) {
		    	/* After no data phase, successfull data phase and
			 * after clearing bulk-in/-out stall condition
			 */
			sc->transfer_state = TSTATE_BBB_STATUS1;
			next_xfer = sc->transfer_xfer[XFER_BBB_CSW1];
		} else {
			/* After first attempt of fetching CSW */
			sc->transfer_state = TSTATE_BBB_STATUS2;
			next_xfer = sc->transfer_xfer[XFER_BBB_CSW2];
		}

		/* Read the Command Status Wrapper via bulk-in endpoint. */
		if (umass_setup_transfer(sc, sc->bulkin_pipe,
				&sc->csw, UMASS_BBB_CSW_SIZE, 0,
				next_xfer)) {
			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;
		}

		return;
	case TSTATE_BBB_STATUS1:	/* first attempt */
	case TSTATE_BBB_STATUS2:	/* second attempt */
		/* Status transfer, error handling */
		{
		int Residue;
		if (err) {
			DPRINTF(UDMASS_BBB, ("%s: Failed to read CSW, %s%s\n",
				device_get_nameunit(sc->sc_dev), usbd_errstr(err),
				(sc->transfer_state == TSTATE_BBB_STATUS1?
					", retrying":"")));

			/* If this was the first attempt at fetching the CSW
			 * retry it, otherwise fail.
			 */
			if (sc->transfer_state == TSTATE_BBB_STATUS1) {
				umass_clear_endpoint_stall(sc,
					    sc->bulkin, sc->bulkin_pipe,
					    TSTATE_BBB_SCLEAR,
					    sc->transfer_xfer[XFER_BBB_SCLEAR]);
				return;
			} else {
				umass_bbb_reset(sc, STATUS_WIRE_FAILED);
				return;
			}
		}

		DIF(UDMASS_BBB, umass_bbb_dump_csw(sc, &sc->csw));

		/* Translate weird command-status signatures. */
		if ((sc->quirks & WRONG_CSWSIG) &&
		    UGETDW(sc->csw.dCSWSignature) == CSWSIGNATURE_OLYMPUS_C1)
			USETDW(sc->csw.dCSWSignature, CSWSIGNATURE);

		Residue = UGETDW(sc->csw.dCSWDataResidue);
		if (Residue == 0 &&
		    sc->transfer_datalen - sc->transfer_actlen != 0)
			Residue = sc->transfer_datalen - sc->transfer_actlen;

		/* Check CSW and handle any error */
		if (UGETDW(sc->csw.dCSWSignature) != CSWSIGNATURE) {
			/* Invalid CSW: Wrong signature or wrong tag might
			 * indicate that the device is confused -> reset it.
			 */
			kprintf("%s: Invalid CSW: sig 0x%08x should be 0x%08x\n",
				device_get_nameunit(sc->sc_dev),
				UGETDW(sc->csw.dCSWSignature),
				CSWSIGNATURE);

			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;
		} else if (UGETDW(sc->csw.dCSWTag)
				!= UGETDW(sc->cbw.dCBWTag)) {
			kprintf("%s: Invalid CSW: tag %d should be %d\n",
				device_get_nameunit(sc->sc_dev),
				UGETDW(sc->csw.dCSWTag),
				UGETDW(sc->cbw.dCBWTag));

			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;

		/* CSW is valid here */
		} else if (sc->csw.bCSWStatus > CSWSTATUS_PHASE) {
			kprintf("%s: Invalid CSW: status %d > %d\n",
				device_get_nameunit(sc->sc_dev),
				sc->csw.bCSWStatus,
				CSWSTATUS_PHASE);

			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;
		} else if (sc->csw.bCSWStatus == CSWSTATUS_PHASE) {
			kprintf("%s: Phase Error, residue = %d\n",
				device_get_nameunit(sc->sc_dev), Residue);

			umass_bbb_reset(sc, STATUS_WIRE_FAILED);
			return;

		} else if (sc->transfer_actlen > sc->transfer_datalen) {
			/* Buffer overrun! Don't let this go by unnoticed */
			panic("%s: transferred %db instead of %db",
				device_get_nameunit(sc->sc_dev),
				sc->transfer_actlen, sc->transfer_datalen);

		} else if (sc->csw.bCSWStatus == CSWSTATUS_FAILED) {
			DPRINTF(UDMASS_BBB, ("%s: Command Failed, res = %d\n",
				device_get_nameunit(sc->sc_dev), Residue));

			/* SCSI command failed but transfer was succesful */
			sc->transfer_state = TSTATE_IDLE;
			sc->transfer_cb(sc, sc->transfer_priv, Residue,
					STATUS_CMD_FAILED);
			return;

		} else {	/* success */
			sc->transfer_state = TSTATE_IDLE;
			sc->transfer_cb(sc, sc->transfer_priv, Residue,
					STATUS_CMD_OK);

			return;
		}
		}

	/***** Bulk Reset *****/
	case TSTATE_BBB_RESET1:
		if (err)
			kprintf("%s: BBB reset failed, %s\n",
				device_get_nameunit(sc->sc_dev), usbd_errstr(err));

		umass_clear_endpoint_stall(sc,
			sc->bulkin, sc->bulkin_pipe, TSTATE_BBB_RESET2,
			sc->transfer_xfer[XFER_BBB_RESET2]);

		return;
	case TSTATE_BBB_RESET2:
		if (err)	/* should not occur */
			kprintf("%s: BBB bulk-in clear stall failed, %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(err));
			/* no error recovery, otherwise we end up in a loop */

		umass_clear_endpoint_stall(sc,
			sc->bulkout, sc->bulkout_pipe, TSTATE_BBB_RESET3,
			sc->transfer_xfer[XFER_BBB_RESET3]);

		return;
	case TSTATE_BBB_RESET3:
		if (err)	/* should not occur */
			kprintf("%s: BBB bulk-out clear stall failed, %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(err));
			/* no error recovery, otherwise we end up in a loop */

		sc->transfer_state = TSTATE_IDLE;
		if (sc->transfer_priv) {
			sc->transfer_cb(sc, sc->transfer_priv,
					sc->transfer_datalen,
					sc->transfer_status);
		}

		return;

	/***** Default *****/
	default:
		panic("%s: Unknown state %d",
		      device_get_nameunit(sc->sc_dev), sc->transfer_state);
	}
}

static int
umass_bbb_get_max_lun(struct umass_softc *sc)
{
	usbd_device_handle udev;
	usb_device_request_t req;
	usbd_status err;
	usb_interface_descriptor_t *id;
	int maxlun = 0;
	u_int8_t buf = 0;

	usbd_interface2device_handle(sc->iface, &udev);
	id = usbd_get_interface_descriptor(sc->iface);

	/* The Get Max Lun command is a class-specific request. */
	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UR_BBB_GET_MAX_LUN;
	USETW(req.wValue, 0);
	USETW(req.wIndex, id->bInterfaceNumber);
	USETW(req.wLength, 1);

	err = usbd_do_request(udev, &req, &buf);
	switch (err) {
	case USBD_NORMAL_COMPLETION:
		maxlun = buf;
		DPRINTF(UDMASS_BBB, ("%s: Max Lun is %d\n",
		    device_get_nameunit(sc->sc_dev), maxlun));
		break;
	case USBD_STALLED:
	case USBD_SHORT_XFER:
	default:
		/* Device doesn't support Get Max Lun request. */
		kprintf("%s: Get Max Lun not supported (%s)\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(err));
		/* XXX Should we port_reset the device? */
		break;
	}

	return(maxlun);
}

/*
 * Command/Bulk/Interrupt (CBI) specific functions
 */

static int
umass_cbi_adsc(struct umass_softc *sc, char *buffer, int buflen,
	       usbd_xfer_handle xfer)
{
	usbd_device_handle udev;

	KASSERT(sc->proto & (UMASS_PROTO_CBI|UMASS_PROTO_CBI_I),
		("%s: umass_cbi_adsc: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));

	usbd_interface2device_handle(sc->iface, &udev);

	sc->request.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	sc->request.bRequest = UR_CBI_ADSC;
	USETW(sc->request.wValue, 0);
	USETW(sc->request.wIndex, sc->ifaceno);
	USETW(sc->request.wLength, buflen);
	return umass_setup_ctrl_transfer(sc, udev, &sc->request, buffer,
					 buflen, 0, xfer);
}


static void
umass_cbi_reset(struct umass_softc *sc, int status)
{
	int i;
#	define SEND_DIAGNOSTIC_CMDLEN	12

	KASSERT(sc->proto & (UMASS_PROTO_CBI|UMASS_PROTO_CBI_I),
		("%s: umass_cbi_reset: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));

	/*
	 * Command Block Reset Protocol
	 *
	 * First send a reset request to the device. Then clear
	 * any possibly stalled bulk endpoints.

	 * This is done in 3 steps, states:
	 * TSTATE_CBI_RESET1
	 * TSTATE_CBI_RESET2
	 * TSTATE_CBI_RESET3
	 *
	 * If the reset doesn't succeed, the device should be port reset.
	 */

	DPRINTF(UDMASS_CBI, ("%s: CBI Reset\n",
		device_get_nameunit(sc->sc_dev)));

	KASSERT(sizeof(sc->cbl) >= SEND_DIAGNOSTIC_CMDLEN,
		("%s: CBL struct is too small (%ld < %d)",
			device_get_nameunit(sc->sc_dev),
			(long)sizeof(sc->cbl), SEND_DIAGNOSTIC_CMDLEN));

	sc->transfer_state = TSTATE_CBI_RESET1;
	sc->transfer_status = status;

	/* The 0x1d code is the SEND DIAGNOSTIC command. To distingiush between
	 * the two the last 10 bytes of the cbl is filled with 0xff (section
	 * 2.2 of the CBI spec).
	 */
	sc->cbl[0] = 0x1d;	/* Command Block Reset */
	sc->cbl[1] = 0x04;
	for (i = 2; i < SEND_DIAGNOSTIC_CMDLEN; i++)
		sc->cbl[i] = 0xff;

	umass_cbi_adsc(sc, sc->cbl, SEND_DIAGNOSTIC_CMDLEN,
		       sc->transfer_xfer[XFER_CBI_RESET1]);
	/* XXX if the command fails we should reset the port on the bub */
}

static void
umass_cbi_transfer(struct umass_softc *sc, int lun,
		void *cmd, int cmdlen, void *data, int datalen, int dir,
		u_int timeout, transfer_cb_f cb, void *priv)
{
	KASSERT(sc->proto & (UMASS_PROTO_CBI|UMASS_PROTO_CBI_I),
		("%s: umass_cbi_transfer: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));
	/* Be a little generous. */
	sc->timeout = timeout + UMASS_TIMEOUT;

	/*
	 * Do a CBI transfer with cmdlen bytes from cmd, possibly
	 * a data phase of datalen bytes from/to the device and finally a
	 * csw read phase.
	 * If the data direction was inbound a maximum of datalen bytes
	 * is stored in the buffer pointed to by data.
	 *
	 * umass_cbi_transfer initialises the transfer and lets the state
	 * machine in umass_cbi_state handle the completion. It uses the
	 * following states:
	 * TSTATE_CBI_COMMAND
	 *   -> XXX fill in
	 *
	 * An error in any of those states will invoke
	 * umass_cbi_reset.
	 */

	/* check the given arguments */
	KASSERT(datalen == 0 || data != NULL,
		("%s: datalen > 0, but no buffer",device_get_nameunit(sc->sc_dev)));
	KASSERT(datalen == 0 || dir != DIR_NONE,
		("%s: direction is NONE while datalen is not zero",
			device_get_nameunit(sc->sc_dev)));

	/* store the details for the data transfer phase */
	sc->transfer_dir = dir;
	sc->transfer_data = data;
	sc->transfer_datalen = datalen;
	sc->transfer_actlen = 0;
	sc->transfer_cb = cb;
	sc->transfer_priv = priv;
	sc->transfer_status = STATUS_CMD_OK;

	/* move from idle to the command state */
	sc->transfer_state = TSTATE_CBI_COMMAND;

	DIF(UDMASS_CBI, umass_cbi_dump_cmd(sc, cmd, cmdlen));

	/* Send the Command Block from host to device via control endpoint. */
	if (umass_cbi_adsc(sc, cmd, cmdlen, sc->transfer_xfer[XFER_CBI_CB]))
		umass_cbi_reset(sc, STATUS_WIRE_FAILED);
}

static void
umass_cbi_state(usbd_xfer_handle xfer, usbd_private_handle priv,
		usbd_status err)
{
	struct umass_softc *sc = (struct umass_softc *) priv;

	KASSERT(sc->proto & (UMASS_PROTO_CBI|UMASS_PROTO_CBI_I),
		("%s: umass_cbi_state: wrong sc->proto 0x%02x",
			device_get_nameunit(sc->sc_dev), sc->proto));

	/*
	 * State handling for CBI transfers.
	 */

	DPRINTF(UDMASS_CBI, ("%s: Handling CBI state %d (%s), xfer=%p, %s\n",
		device_get_nameunit(sc->sc_dev), sc->transfer_state,
		states[sc->transfer_state], xfer, usbd_errstr(err)));

	switch (sc->transfer_state) {

	/***** CBI Transfer *****/
	case TSTATE_CBI_COMMAND:
		if (err == USBD_STALLED) {
			DPRINTF(UDMASS_CBI, ("%s: Command Transport failed\n",
				device_get_nameunit(sc->sc_dev)));
			/* Status transport by control pipe (section 2.3.2.1).
			 * The command contained in the command block failed.
			 *
			 * The control pipe has already been unstalled by the
			 * USB stack.
			 * Section 2.4.3.1.1 states that the bulk in endpoints
			 * should not be stalled at this point.
			 */

			sc->transfer_state = TSTATE_IDLE;
			sc->transfer_cb(sc, sc->transfer_priv,
					sc->transfer_datalen,
					STATUS_CMD_FAILED);

			return;
		} else if (err) {
			DPRINTF(UDMASS_CBI, ("%s: failed to send ADSC\n",
				device_get_nameunit(sc->sc_dev)));
			umass_cbi_reset(sc, STATUS_WIRE_FAILED);

			return;
		}

		sc->transfer_state = TSTATE_CBI_DATA;
		if (sc->transfer_dir == DIR_IN) {
			if (umass_setup_transfer(sc, sc->bulkin_pipe,
					sc->transfer_data, sc->transfer_datalen,
					USBD_SHORT_XFER_OK,
					sc->transfer_xfer[XFER_CBI_DATA]))
				umass_cbi_reset(sc, STATUS_WIRE_FAILED);

		} else if (sc->transfer_dir == DIR_OUT) {
			if (umass_setup_transfer(sc, sc->bulkout_pipe,
					sc->transfer_data, sc->transfer_datalen,
					0,	/* fixed length transfer */
					sc->transfer_xfer[XFER_CBI_DATA]))
				umass_cbi_reset(sc, STATUS_WIRE_FAILED);

		} else if (sc->proto & UMASS_PROTO_CBI_I) {
			DPRINTF(UDMASS_CBI, ("%s: no data phase\n",
				device_get_nameunit(sc->sc_dev)));
			sc->transfer_state = TSTATE_CBI_STATUS;
			if (umass_setup_transfer(sc, sc->intrin_pipe,
					&sc->sbl, sizeof(sc->sbl),
					0,	/* fixed length transfer */
					sc->transfer_xfer[XFER_CBI_STATUS])){
				umass_cbi_reset(sc, STATUS_WIRE_FAILED);
			}
		} else {
			DPRINTF(UDMASS_CBI, ("%s: no data phase\n",
				device_get_nameunit(sc->sc_dev)));
			/* No command completion interrupt. Request
			 * sense data.
			 */
			sc->transfer_state = TSTATE_IDLE;
			sc->transfer_cb(sc, sc->transfer_priv,
			       0, STATUS_CMD_UNKNOWN);
		}

		return;

	case TSTATE_CBI_DATA:
		/* retrieve the length of the transfer that was done */
		usbd_get_xfer_status(xfer,NULL,NULL,&sc->transfer_actlen,NULL);

		if (err) {
			DPRINTF(UDMASS_CBI, ("%s: Data-%s %db failed, "
				"%s\n", device_get_nameunit(sc->sc_dev),
				(sc->transfer_dir == DIR_IN?"in":"out"),
				sc->transfer_datalen,usbd_errstr(err)));

			if (err == USBD_STALLED) {
				umass_clear_endpoint_stall(sc,
					sc->bulkin, sc->bulkin_pipe,
					TSTATE_CBI_DCLEAR,
					sc->transfer_xfer[XFER_CBI_DCLEAR]);
			} else {
				umass_cbi_reset(sc, STATUS_WIRE_FAILED);
			}
			return;
		}

		DIF(UDMASS_CBI, if (sc->transfer_dir == DIR_IN)
					umass_dump_buffer(sc, sc->transfer_data,
						sc->transfer_actlen, 48));

		if (sc->proto & UMASS_PROTO_CBI_I) {
			sc->transfer_state = TSTATE_CBI_STATUS;
			if (umass_setup_transfer(sc, sc->intrin_pipe,
				    &sc->sbl, sizeof(sc->sbl),
				    0,	/* fixed length transfer */
				    sc->transfer_xfer[XFER_CBI_STATUS])){
				umass_cbi_reset(sc, STATUS_WIRE_FAILED);
			}
		} else {
			/* No command completion interrupt. Request
			 * sense to get status of command.
			 */
			sc->transfer_state = TSTATE_IDLE;
			sc->transfer_cb(sc, sc->transfer_priv,
				sc->transfer_datalen - sc->transfer_actlen,
				STATUS_CMD_UNKNOWN);
		}
		return;

	case TSTATE_CBI_STATUS:
		if (err) {
			DPRINTF(UDMASS_CBI, ("%s: Status Transport failed\n",
				device_get_nameunit(sc->sc_dev)));
			/* Status transport by interrupt pipe (section 2.3.2.2).
			 */

			if (err == USBD_STALLED) {
				umass_clear_endpoint_stall(sc,
					sc->intrin, sc->intrin_pipe,
					TSTATE_CBI_SCLEAR,
					sc->transfer_xfer[XFER_CBI_SCLEAR]);
			} else {
				umass_cbi_reset(sc, STATUS_WIRE_FAILED);
			}
			return;
		}

		/* Dissect the information in the buffer */

		if (sc->proto & UMASS_PROTO_UFI) {
			int status;

			/* Section 3.4.3.1.3 specifies that the UFI command
			 * protocol returns an ASC and ASCQ in the interrupt
			 * data block.
			 */

			DPRINTF(UDMASS_CBI, ("%s: UFI CCI, ASC = 0x%02x, "
				"ASCQ = 0x%02x\n",
				device_get_nameunit(sc->sc_dev),
				sc->sbl.ufi.asc, sc->sbl.ufi.ascq));

			if (sc->sbl.ufi.asc == 0 && sc->sbl.ufi.ascq == 0)
				status = STATUS_CMD_OK;
			else
				status = STATUS_CMD_FAILED;

			sc->transfer_state = TSTATE_IDLE;
			sc->transfer_cb(sc, sc->transfer_priv,
				sc->transfer_datalen - sc->transfer_actlen,
				status);
		} else {
			/* Command Interrupt Data Block */
			DPRINTF(UDMASS_CBI, ("%s: type=0x%02x, value=0x%02x\n",
				device_get_nameunit(sc->sc_dev),
				sc->sbl.common.type, sc->sbl.common.value));

			if (sc->sbl.common.type == IDB_TYPE_CCI) {
				int err;

				if ((sc->sbl.common.value&IDB_VALUE_STATUS_MASK)
							== IDB_VALUE_PASS) {
					err = STATUS_CMD_OK;
				} else if ((sc->sbl.common.value & IDB_VALUE_STATUS_MASK)
							== IDB_VALUE_FAIL ||
					   (sc->sbl.common.value & IDB_VALUE_STATUS_MASK)
						== IDB_VALUE_PERSISTENT) {
					err = STATUS_CMD_FAILED;
				} else {
					err = STATUS_WIRE_FAILED;
				}

				sc->transfer_state = TSTATE_IDLE;
				sc->transfer_cb(sc, sc->transfer_priv,
				       sc->transfer_datalen-sc->transfer_actlen,
				       err);
			}
		}
		return;

	case TSTATE_CBI_DCLEAR:
		if (err) {	/* should not occur */
			kprintf("%s: CBI bulk-in/out stall clear failed, %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(err));
			umass_cbi_reset(sc, STATUS_WIRE_FAILED);
		}

		sc->transfer_state = TSTATE_IDLE;
		sc->transfer_cb(sc, sc->transfer_priv,
				sc->transfer_datalen,
				STATUS_CMD_FAILED);
		return;

	case TSTATE_CBI_SCLEAR:
		if (err)	/* should not occur */
			kprintf("%s: CBI intr-in stall clear failed, %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(err));

		/* Something really bad is going on. Reset the device */
		umass_cbi_reset(sc, STATUS_CMD_FAILED);
		return;

	/***** CBI Reset *****/
	case TSTATE_CBI_RESET1:
		if (err)
			kprintf("%s: CBI reset failed, %s\n",
				device_get_nameunit(sc->sc_dev), usbd_errstr(err));

		umass_clear_endpoint_stall(sc,
			sc->bulkin, sc->bulkin_pipe, TSTATE_CBI_RESET2,
			sc->transfer_xfer[XFER_CBI_RESET2]);

		return;
	case TSTATE_CBI_RESET2:
		if (err)	/* should not occur */
			kprintf("%s: CBI bulk-in stall clear failed, %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(err));
			/* no error recovery, otherwise we end up in a loop */

		umass_clear_endpoint_stall(sc,
			sc->bulkout, sc->bulkout_pipe, TSTATE_CBI_RESET3,
			sc->transfer_xfer[XFER_CBI_RESET3]);

		return;
	case TSTATE_CBI_RESET3:
		if (err)	/* should not occur */
			kprintf("%s: CBI bulk-out stall clear failed, %s\n",
			       device_get_nameunit(sc->sc_dev), usbd_errstr(err));
			/* no error recovery, otherwise we end up in a loop */

		sc->transfer_state = TSTATE_IDLE;
		if (sc->transfer_priv) {
			sc->transfer_cb(sc, sc->transfer_priv,
					sc->transfer_datalen,
					sc->transfer_status);
		}

		return;


	/***** Default *****/
	default:
		panic("%s: Unknown state %d",
		      device_get_nameunit(sc->sc_dev), sc->transfer_state);
	}
}




/*
 * CAM specific functions (used by SCSI, UFI, 8070i (ATAPI))
 */

static int
umass_cam_attach_sim(struct umass_softc *sc)
{
	struct cam_devq	*devq;		/* Per device Queue */

	/* A HBA is attached to the CAM layer.
	 *
	 * The CAM layer will then after a while start probing for
	 * devices on the bus. The number of SIMs is limited to one.
	 */

	callout_init(&sc->rescan_timeout);
	devq = cam_simq_alloc(1 /*maximum openings*/);
	if (devq == NULL)
		return(ENOMEM);

	sc->umass_sim = cam_sim_alloc(umass_cam_action, umass_cam_poll,
				DEVNAME_SIM,
				sc /*priv*/,
				device_get_unit(sc->sc_dev) /*unit number*/,
				&sim_mplock,
				1 /*maximum device openings*/,
				0 /*maximum tagged device openings*/,
				devq);
	cam_simq_release(devq);
	if (sc->umass_sim == NULL)
		return(ENOMEM);

	/*
	 * If we could not register the bus we must immediately free the
	 * sim so we do not attempt to deregister a bus later on that we
	 * had not registered.
	 */
	if (xpt_bus_register(sc->umass_sim, device_get_unit(sc->sc_dev)) !=
	    CAM_SUCCESS) {
		cam_sim_free(sc->umass_sim);
		sc->umass_sim = NULL;
		return(ENOMEM);
	}

	return(0);
}

static void
umass_cam_rescan_callback(struct cam_periph *periph, union ccb *ccb)
{
#ifdef USB_DEBUG
	if (ccb->ccb_h.status != CAM_REQ_CMP) {
		DPRINTF(UDMASS_SCSI, ("%s:%d Rescan failed, 0x%04x\n",
			periph->periph_name, periph->unit_number,
			ccb->ccb_h.status));
	} else {
		DPRINTF(UDMASS_SCSI, ("%s%d: Rescan succeeded\n",
			periph->periph_name, periph->unit_number));
	}
#endif

	xpt_free_path(ccb->ccb_h.path);
	kfree(ccb, M_USBDEV);
}

/*
 * Rescan the SCSI bus to detect newly added devices.  We use
 * an async rescan to avoid reentrancy issues.
 */
static void
umass_cam_rescan(void *addr)
{
	struct umass_softc *sc = (struct umass_softc *) addr;
	struct cam_path *path;
	union ccb *ccb;

	ccb = kmalloc(sizeof(union ccb), M_USBDEV, M_INTWAIT|M_ZERO);

	DPRINTF(UDMASS_SCSI, ("scbus%d: scanning for %s:%d:%d:%d\n",
		cam_sim_path(sc->umass_sim),
		device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
		device_get_unit(sc->sc_dev), CAM_LUN_WILDCARD));

	if (xpt_create_path(&path, xpt_periph, cam_sim_path(sc->umass_sim),
			    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD)
	    != CAM_REQ_CMP) {
		kfree(ccb, M_USBDEV);
 		return;
	}

	xpt_setup_ccb(&ccb->ccb_h, path, 5/*priority (low)*/);
	ccb->ccb_h.func_code = XPT_SCAN_BUS;
	ccb->ccb_h.cbfcnp = umass_cam_rescan_callback;
	ccb->crcn.flags = CAM_FLAG_NONE;
	xpt_action_async(ccb);

	/* The scan is in progress now. */
}

static int
umass_cam_attach(struct umass_softc *sc)
{
#ifndef USB_DEBUG
	if (bootverbose)
#endif
		kprintf("%s:%d:%d:%d: Attached to scbus%d\n",
			device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
			device_get_unit(sc->sc_dev), CAM_LUN_WILDCARD,
			cam_sim_path(sc->umass_sim));

	if (!cold) {
		/* 
		 * Notify CAM of the new device after a 0.2 second delay. Any
		 * failure is benign, as the user can still do it by hand
		 * (camcontrol rescan <busno>). Only do this if we are not
		 * booting, because CAM does a scan after booting has
		 * completed, when interrupts have been enabled.
		 */
		callout_reset(&sc->rescan_timeout, MS_TO_TICKS(200),
				umass_cam_rescan, sc);
	}

	return(0);	/* always succesfull */
}

/* umass_cam_detach
 *	detach from the CAM layer
 */

static int
umass_cam_detach_sim(struct umass_softc *sc)
{
	callout_stop(&sc->rescan_timeout);
	if (sc->umass_sim) {
		xpt_bus_deregister(cam_sim_path(sc->umass_sim));
		cam_sim_free(sc->umass_sim);

		sc->umass_sim = NULL;
	}

	return(0);
}

/* umass_cam_action
 * 	CAM requests for action come through here
 */

static void
umass_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct umass_softc *sc = (struct umass_softc *)sim->softc;

	/* The softc is still there, but marked as going away. umass_cam_detach
	 * has not yet notified CAM of the lost device however.
	 */
	if (sc && (sc->flags & UMASS_FLAGS_GONE)) {
		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:func_code 0x%04x: "
			"Invalid target (gone)\n",
			device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
			ccb->ccb_h.func_code));
		ccb->ccb_h.status = CAM_TID_INVALID;
		xpt_done(ccb);
		return;
	}

	/* Verify, depending on the operation to perform, that we either got a
	 * valid sc, because an existing target was referenced, or otherwise
	 * the SIM is addressed.
	 *
	 * This avoids bombing out at a kprintf and does give the CAM layer some
	 * sensible feedback on errors.
	 */
	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
	case XPT_RESET_DEV:
	case XPT_GET_TRAN_SETTINGS:
	case XPT_SET_TRAN_SETTINGS:
	case XPT_CALC_GEOMETRY:
		/* the opcodes requiring a target. These should never occur. */
		if (sc == NULL) {
			kprintf("%s:%d:%d:%d:func_code 0x%04x: "
				"Invalid target (target needed)\n",
				DEVNAME_SIM, 0, ccb->ccb_h.target_id,
				ccb->ccb_h.target_lun, ccb->ccb_h.func_code);

			ccb->ccb_h.status = CAM_TID_INVALID;
			xpt_done(ccb);
			return;
		}
		break;
	case XPT_PATH_INQ:
	case XPT_NOOP:
		/* The opcodes sometimes aimed at a target (sc is valid),
		 * sometimes aimed at the SIM (sc is invalid and target is
		 * CAM_TARGET_WILDCARD)
		 */
		if (sc == NULL && ccb->ccb_h.target_id != CAM_TARGET_WILDCARD) {
			DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:func_code 0x%04x: "
				"Invalid target (no wildcard)\n",
				DEVNAME_SIM, 0, ccb->ccb_h.target_id, 
				ccb->ccb_h.target_lun, ccb->ccb_h.func_code));

			ccb->ccb_h.status = CAM_TID_INVALID;
			xpt_done(ccb);
			return;
		}
		break;
	default:
		/* XXX Hm, we should check the input parameters */
		break;
	}

	/* Perform the requested action */
	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
	{
		struct ccb_scsiio *csio = &ccb->csio;	/* deref union */
		int dir;
		unsigned char *cmd;
		int cmdlen;
		unsigned char *rcmd;
		int rcmdlen;

		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:XPT_SCSI_IO: "
			"cmd: 0x%02x, flags: 0x%02x, "
			"%db cmd/%db data/%db sense\n",
			device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
			csio->cdb_io.cdb_bytes[0],
			ccb->ccb_h.flags & CAM_DIR_MASK,
			csio->cdb_len, csio->dxfer_len,
			csio->sense_len));

		/* clear the end of the buffer to make sure we don't send out
		 * garbage.
		 */
		DIF(UDMASS_SCSI, if ((ccb->ccb_h.flags & CAM_DIR_MASK)
				     == CAM_DIR_OUT)
					umass_dump_buffer(sc, csio->data_ptr,
						csio->dxfer_len, 48));

		if (sc->transfer_state != TSTATE_IDLE) {
			DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:XPT_SCSI_IO: "
				"I/O in progress, deferring (state %d, %s)\n",
				device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
				ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
				sc->transfer_state,states[sc->transfer_state]));
			ccb->ccb_h.status = CAM_SCSI_BUSY;
			xpt_done(ccb);
			return;
		}

		switch(ccb->ccb_h.flags&CAM_DIR_MASK) {
		case CAM_DIR_IN:
			dir = DIR_IN;
			break;
		case CAM_DIR_OUT:
			dir = DIR_OUT;
			break;
		default:
			dir = DIR_NONE;
		}

		ccb->ccb_h.status = CAM_REQ_INPROG | CAM_SIM_QUEUED;


		if (csio->ccb_h.flags & CAM_CDB_POINTER) {
			cmd = (unsigned char *) csio->cdb_io.cdb_ptr;
		} else {
			cmd = (unsigned char *) &csio->cdb_io.cdb_bytes;
		}
		cmdlen = csio->cdb_len;
		rcmd = (unsigned char *) &sc->cam_scsi_command;
		rcmdlen = sizeof(sc->cam_scsi_command);

		/* sc->transform will convert the command to the command
		 * (format) needed by the specific command set and return
		 * the converted command in a buffer pointed to be rcmd.
		 * We pass in a buffer, but if the command does not
		 * have to be transformed it returns a ptr to the original
		 * buffer (see umass_scsi_transform).
		 */

		if (sc->transform(sc, cmd, cmdlen, &rcmd, &rcmdlen)) {
			/*
			 * Handle EVPD inquiry for broken devices first
			 * NO_INQUIRY also implies NO_INQUIRY_EVPD
			 */
			if ((sc->quirks & (NO_INQUIRY_EVPD | NO_INQUIRY)) &&
			    rcmd[0] == INQUIRY && (rcmd[1] & SI_EVPD)) {
				struct scsi_sense_data *sense;

				sense = &ccb->csio.sense_data;
				bzero(sense, sizeof(*sense));
				sense->error_code = SSD_CURRENT_ERROR;
				sense->flags = SSD_KEY_ILLEGAL_REQUEST;
				sense->add_sense_code = 0x24;
				sense->extra_len = 10;
 				ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
				ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR |
				    CAM_AUTOSNS_VALID;
				xpt_done(ccb);
				return;
			}
			/* Return fake inquiry data for broken devices */
			if ((sc->quirks & NO_INQUIRY) && rcmd[0] == INQUIRY) {
				struct ccb_scsiio *csio = &ccb->csio;

				memcpy(csio->data_ptr, &fake_inq_data,
				    sizeof(fake_inq_data));
				csio->scsi_status = SCSI_STATUS_OK;
				ccb->ccb_h.status = CAM_REQ_CMP;
				xpt_done(ccb);
				return;
			}
			if ((sc->quirks & FORCE_SHORT_INQUIRY) &&
			    rcmd[0] == INQUIRY) {
				csio->dxfer_len = SHORT_INQUIRY_LENGTH;
			}
			sc->transfer(sc, ccb->ccb_h.target_lun, rcmd, rcmdlen,
				     csio->data_ptr,
				     csio->dxfer_len, dir, ccb->ccb_h.timeout,
				     umass_cam_cb, (void *) ccb);
		} else {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
		}

		break;
	}
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:XPT_PATH_INQ:.\n",
			(sc == NULL ? DEVNAME_SIM : device_get_nameunit(sc->sc_dev)),
			(sc == NULL ? 0 : cam_sim_path(sc->umass_sim)),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun));

		/* host specific information */
		cpi->version_num = 1;
		cpi->hba_inquiry = 0;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NO_6_BYTE;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = UMASS_SCSIID_MAX;	/* one target */
		cpi->initiator_id = UMASS_SCSIID_HOST;
		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "USB SCSI", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);

		if (sc == NULL) {
			cpi->base_transfer_speed = 0;
			cpi->max_lun = 0;
		} else {
			cpi->bus_id = device_get_unit(sc->sc_dev);

			if (sc->quirks & FLOPPY_SPEED) {
				cpi->base_transfer_speed =
				    UMASS_FLOPPY_TRANSFER_SPEED;
			} else if (usbd_get_speed(sc->sc_udev) ==
			    USB_SPEED_HIGH) {
				cpi->base_transfer_speed =
				    UMASS_HIGH_TRANSFER_SPEED;
			} else {
				cpi->base_transfer_speed =
				    UMASS_FULL_TRANSFER_SPEED;
			}
			cpi->max_lun = sc->maxlun;
		}

		cpi->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	case XPT_RESET_DEV:
	{
		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:XPT_RESET_DEV:.\n",
			device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun));

		ccb->ccb_h.status = CAM_REQ_INPROG;
		umass_reset(sc, umass_cam_cb, ccb);
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings *cts = &ccb->cts;
		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_2;
		cts->transport = XPORT_USB;
		cts->transport_version = XPORT_VERSION_UNSPECIFIED;
		cts->xport_specific.valid = 0;

		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
	{
		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:XPT_SET_TRAN_SETTINGS:.\n",
			device_get_nameunit(sc->sc_dev), cam_sim_path(sc->umass_sim),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun));

		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		break;
	}
	case XPT_CALC_GEOMETRY:
	{
		cam_calc_geometry(&ccb->ccg, /*extended*/1);
		xpt_done(ccb);
		break;
	}
	case XPT_NOOP:
	{
		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:XPT_NOOP:.\n",
			(sc == NULL ? DEVNAME_SIM : device_get_nameunit(sc->sc_dev)),
			(sc == NULL ? 0 : cam_sim_path(sc->umass_sim)),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun));

		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	default:
		DPRINTF(UDMASS_SCSI, ("%s:%d:%d:%d:func_code 0x%04x: "
			"Not implemented\n",
			(sc == NULL ? DEVNAME_SIM : device_get_nameunit(sc->sc_dev)),
			(sc == NULL ? 0 : cam_sim_path(sc->umass_sim)),
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
			ccb->ccb_h.func_code));

		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		break;
	}
}

static void
umass_cam_poll(struct cam_sim *sim)
{
	struct umass_softc *sc = (struct umass_softc *) sim->softc;

	KKASSERT(sc != NULL);

	DPRINTF(UDMASS_SCSI, ("%s: CAM poll\n",
		device_get_nameunit(sc->sc_dev)));

	usbd_set_polling(sc->sc_udev, 1);
	usbd_dopoll(sc->iface);
	usbd_set_polling(sc->sc_udev, 0);
}


/* umass_cam_cb
 *	finalise a completed CAM command
 */

static void
umass_cam_cb(struct umass_softc *sc, void *priv, int residue, int status)
{
	union ccb *ccb = (union ccb *) priv;
	struct ccb_scsiio *csio = &ccb->csio;		/* deref union */

	csio->resid = residue;

	switch (status) {
	case STATUS_CMD_OK:
		ccb->ccb_h.status = CAM_REQ_CMP;
		if ((sc->quirks & READ_CAPACITY_OFFBY1) &&
		    (ccb->ccb_h.func_code == XPT_SCSI_IO) &&
		    (csio->cdb_io.cdb_bytes[0] == READ_CAPACITY)) {
			struct scsi_read_capacity_data *rcap;
			uint32_t maxsector;

			rcap = (struct scsi_read_capacity_data *)csio->data_ptr;
			maxsector = scsi_4btoul(rcap->addr) - 1; 
			scsi_ulto4b(maxsector, rcap->addr);
		}
		xpt_done(ccb);
		break;

	case STATUS_CMD_UNKNOWN:
	case STATUS_CMD_FAILED:
		switch (ccb->ccb_h.func_code) {
		case XPT_SCSI_IO:
		{
			unsigned char *rcmd;
			int rcmdlen;

			/* fetch sense data */
			/* the rest of the command was filled in at attach */
			sc->cam_scsi_sense.length = csio->sense_len;

			DPRINTF(UDMASS_SCSI,("%s: Fetching %db sense data\n",
				device_get_nameunit(sc->sc_dev), csio->sense_len));

			rcmd = (unsigned char *) &sc->cam_scsi_command;
			rcmdlen = sizeof(sc->cam_scsi_command);

			if (sc->transform(sc,
				    (unsigned char *) &sc->cam_scsi_sense,
				    sizeof(sc->cam_scsi_sense),
				    &rcmd, &rcmdlen)) {
				if ((sc->quirks & FORCE_SHORT_INQUIRY) && (rcmd[0] == INQUIRY)) {
					csio->sense_len = SHORT_INQUIRY_LENGTH;
				}
				sc->transfer(sc, ccb->ccb_h.target_lun,
					     rcmd, rcmdlen,
					     &csio->sense_data,
					     csio->sense_len, DIR_IN, ccb->ccb_h.timeout,
					     umass_cam_sense_cb, (void *) ccb);
			} else {
				panic("transform(REQUEST_SENSE) failed");
			}
			break;
		}
		case XPT_RESET_DEV: /* Reset failed */
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			xpt_done(ccb);
			break;
		default:
			panic("umass_cam_cb called for func_code %d",
			      ccb->ccb_h.func_code);
		}
		break;

	case STATUS_WIRE_FAILED:
		/* the wire protocol failed and will have recovered
		 * (hopefully).  We return an error to CAM and let CAM retry
		 * the command if necessary.
		 */
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
		xpt_done(ccb);
		break;
	default:
		panic("%s: Unknown status %d in umass_cam_cb",
			device_get_nameunit(sc->sc_dev), status);
	}
}

/* Finalise a completed autosense operation
 */
static void
umass_cam_sense_cb(struct umass_softc *sc, void *priv, int residue, int status)
{
	union ccb *ccb = (union ccb *) priv;
	struct ccb_scsiio *csio = &ccb->csio;		/* deref union */
	unsigned char *rcmd;
	int rcmdlen;

	switch (status) {
	case STATUS_CMD_OK:
	case STATUS_CMD_UNKNOWN:
	case STATUS_CMD_FAILED:
		/* Getting sense data always succeeds (apart from wire
		 * failures).
		 */
		if ((sc->quirks & RS_NO_CLEAR_UA)
		    && csio->cdb_io.cdb_bytes[0] == INQUIRY
		    && (csio->sense_data.flags & SSD_KEY)
		    				== SSD_KEY_UNIT_ATTENTION) {
			/* Ignore unit attention errors in the case where
			 * the Unit Attention state is not cleared on
			 * REQUEST SENSE. They will appear again at the next
			 * command.
			 */
			ccb->ccb_h.status = CAM_REQ_CMP;
		} else if ((csio->sense_data.flags & SSD_KEY)
						== SSD_KEY_NO_SENSE) {
			/* No problem after all (in the case of CBI without
			 * CCI)
			 */
			ccb->ccb_h.status = CAM_REQ_CMP;
		} else if ((sc->quirks & RS_NO_CLEAR_UA) &&
			   (csio->cdb_io.cdb_bytes[0] == READ_CAPACITY) &&
			   ((csio->sense_data.flags & SSD_KEY)
			    == SSD_KEY_UNIT_ATTENTION)) {
			/*
			 * Some devices do not clear the unit attention error
			 * on request sense. We insert a test unit ready
			 * command to make sure we clear the unit attention
			 * condition, then allow the retry to proceed as
			 * usual.
			 */

			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR
					    | CAM_AUTOSNS_VALID;
			csio->scsi_status = SCSI_STATUS_CHECK_COND;

#if 0
			DELAY(300000);
#endif

			DPRINTF(UDMASS_SCSI,("%s: Doing a sneaky"
					     "TEST_UNIT_READY\n",
				device_get_nameunit(sc->sc_dev)));

			/* the rest of the command was filled in at attach */

			rcmd = (unsigned char *) &sc->cam_scsi_command2;
			rcmdlen = sizeof(sc->cam_scsi_command2);

			if (sc->transform(sc,
					(unsigned char *)
					&sc->cam_scsi_test_unit_ready,
					sizeof(sc->cam_scsi_test_unit_ready),
					&rcmd, &rcmdlen)) {
				sc->transfer(sc, ccb->ccb_h.target_lun,
					     rcmd, rcmdlen,
					     NULL, 0, DIR_NONE, ccb->ccb_h.timeout,
					     umass_cam_quirk_cb, (void *) ccb);
			} else {
				panic("transform(TEST_UNIT_READY) failed");
			}
			break;
		} else {
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR
					    | CAM_AUTOSNS_VALID;
			csio->scsi_status = SCSI_STATUS_CHECK_COND;
		}
		xpt_done(ccb);
		break;

	default:
		DPRINTF(UDMASS_SCSI, ("%s: Autosense failed, status %d\n",
			device_get_nameunit(sc->sc_dev), status));
		ccb->ccb_h.status = CAM_AUTOSENSE_FAIL;
		xpt_done(ccb);
	}
}

/*
 * This completion code just handles the fact that we sent a test-unit-ready
 * after having previously failed a READ CAPACITY with CHECK_COND.  Even
 * though this command succeeded, we have to tell CAM to retry.
 */
static void
umass_cam_quirk_cb(struct umass_softc *sc, void *priv, int residue, int status)
{
	union ccb *ccb = (union ccb *) priv;

	DPRINTF(UDMASS_SCSI, ("%s: Test unit ready returned status %d\n",
	device_get_nameunit(sc->sc_dev), status));
#if 0
	ccb->ccb_h.status = CAM_REQ_CMP;
#endif
	ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR
			    | CAM_AUTOSNS_VALID;
	ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
	xpt_done(ccb);
}

static int
umass_driver_load(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_UNLOAD:
	case MOD_LOAD:
	default:
		return(usbd_driver_load(mod, what, arg));
	}
}

/*
 * SCSI specific functions
 */

static int
umass_scsi_transform(struct umass_softc *sc, unsigned char *cmd, int cmdlen,
		     unsigned char **rcmd, int *rcmdlen)
{
	switch (cmd[0]) {
	case TEST_UNIT_READY:
		if (sc->quirks & NO_TEST_UNIT_READY) {
			KASSERT(*rcmdlen >= sizeof(struct scsi_start_stop_unit),
				("rcmdlen = %d < %ld, buffer too small",
				 *rcmdlen,
				 (long)sizeof(struct scsi_start_stop_unit)));
			DPRINTF(UDMASS_SCSI, ("%s: Converted TEST_UNIT_READY "
				"to START_UNIT\n", device_get_nameunit(sc->sc_dev)));
			memset(*rcmd, 0, *rcmdlen);
			(*rcmd)[0] = START_STOP_UNIT;
			(*rcmd)[4] = SSS_START;
			return 1;
		}
		/* fallthrough */
	case INQUIRY:
		/* some drives wedge when asked for full inquiry information. */
		if (sc->quirks & FORCE_SHORT_INQUIRY) {
			memcpy(*rcmd, cmd, cmdlen);
			*rcmdlen = cmdlen;
			(*rcmd)[4] = SHORT_INQUIRY_LENGTH;
			return 1;
		}
		/* fallthrough */
	default:
		*rcmd = cmd;		/* We don't need to copy it */
		*rcmdlen = cmdlen;
	}

	return 1;
}
/* RBC specific functions */
static int
umass_rbc_transform(struct umass_softc *sc, unsigned char *cmd, int cmdlen,
		     unsigned char **rcmd, int *rcmdlen)
{
	switch (cmd[0]) {
	/* these commands are defined in RBC: */
	case READ_10:
	case READ_CAPACITY:
	case START_STOP_UNIT:
	case SYNCHRONIZE_CACHE:
	case WRITE_10:
	case 0x2f: /* VERIFY_10 is absent from scsi_all.h??? */
	case INQUIRY:
	case MODE_SELECT_10:
	case MODE_SENSE_10:
	case TEST_UNIT_READY:
	case WRITE_BUFFER:
	 /* The following commands are not listed in my copy of the RBC specs.
	  * CAM however seems to want those, and at least the Sony DSC device
	  * appears to support those as well */
	case REQUEST_SENSE:
	case PREVENT_ALLOW:
		*rcmd = cmd;		/* We don't need to copy it */
		*rcmdlen = cmdlen;
		return 1;
	/* All other commands are not legal in RBC */
	default:
		kprintf("%s: Unsupported RBC command 0x%02x",
			device_get_nameunit(sc->sc_dev), cmd[0]);
		kprintf("\n");
		return 0;	/* failure */
	}
}

/*
 * UFI specific functions
 */
static int
umass_ufi_transform(struct umass_softc *sc, unsigned char *cmd, int cmdlen,
		    unsigned char **rcmd, int *rcmdlen)
{
	/* A UFI command is always 12 bytes in length */
	KASSERT(*rcmdlen >= UFI_COMMAND_LENGTH,
		("rcmdlen = %d < %d, buffer too small",
		 *rcmdlen, UFI_COMMAND_LENGTH));

	*rcmdlen = UFI_COMMAND_LENGTH;
	memset(*rcmd, 0, UFI_COMMAND_LENGTH);

	switch (cmd[0]) {
	/* Commands of which the format has been verified. They should work.
	 * Copy the command into the (zeroed out) destination buffer.
	 */
	case TEST_UNIT_READY:
		if (sc->quirks &  NO_TEST_UNIT_READY) {
			/* Some devices do not support this command.
			 * Start Stop Unit should give the same results
			 */
			DPRINTF(UDMASS_UFI, ("%s: Converted TEST_UNIT_READY "
				"to START_UNIT\n", device_get_nameunit(sc->sc_dev)));
			(*rcmd)[0] = START_STOP_UNIT;
			(*rcmd)[4] = SSS_START;
		} else {
			memcpy(*rcmd, cmd, cmdlen);
		}
		return 1;

	case REZERO_UNIT:
	case REQUEST_SENSE:
	case INQUIRY:
	case START_STOP_UNIT:
	case SEND_DIAGNOSTIC:
	case PREVENT_ALLOW:
	case READ_CAPACITY:
	case READ_10:
	case WRITE_10:
	case POSITION_TO_ELEMENT:	/* SEEK_10 */
	case MODE_SELECT_10:
	case MODE_SENSE_10:
	case READ_12:
	case WRITE_12:
		memcpy(*rcmd, cmd, cmdlen);
		return 1;

	/* Other UFI commands: FORMAT_UNIT, READ_FORMAT_CAPACITY,
	 * VERIFY, WRITE_AND_VERIFY.
	 * These should be checked whether they somehow can be made to fit.
	 */

	default:
		kprintf("%s: Unsupported UFI command 0x%02x\n",
			device_get_nameunit(sc->sc_dev), cmd[0]);
		return 0;	/* failure */
	}
}

/*
 * 8070i (ATAPI) specific functions
 */
static int
umass_atapi_transform(struct umass_softc *sc, unsigned char *cmd, int cmdlen,
		      unsigned char **rcmd, int *rcmdlen)
{
	/* An ATAPI command is always 12 bytes in length. */
	KASSERT(*rcmdlen >= ATAPI_COMMAND_LENGTH,
		("rcmdlen = %d < %d, buffer too small",
		 *rcmdlen, ATAPI_COMMAND_LENGTH));

	*rcmdlen = ATAPI_COMMAND_LENGTH;
	memset(*rcmd, 0, ATAPI_COMMAND_LENGTH);

	switch (cmd[0]) {
	/* Commands of which the format has been verified. They should work.
	 * Copy the command into the (zeroed out) destination buffer.
	 */
	case INQUIRY:
		memcpy(*rcmd, cmd, cmdlen);
		/* some drives wedge when asked for full inquiry information. */
		if (sc->quirks & FORCE_SHORT_INQUIRY)
			(*rcmd)[4] = SHORT_INQUIRY_LENGTH;
		return 1;

	case TEST_UNIT_READY:
		if (sc->quirks & NO_TEST_UNIT_READY) {
			KASSERT(*rcmdlen >= sizeof(struct scsi_start_stop_unit),
				("rcmdlen = %d < %ld, buffer too small",
				 *rcmdlen,
				 (long)sizeof(struct scsi_start_stop_unit)));
			DPRINTF(UDMASS_SCSI, ("%s: Converted TEST_UNIT_READY "
				"to START_UNIT\n", device_get_nameunit(sc->sc_dev)));
			memset(*rcmd, 0, *rcmdlen);
			(*rcmd)[0] = START_STOP_UNIT;
			(*rcmd)[4] = SSS_START;
			return 1;
		}
		/* fallthrough */
	default:
		/*
		 * All commands are passed through, very likely it will just work
		 * regardless whether we know these commands or not.
		 */
		memcpy(*rcmd, cmd, cmdlen);
		return 1;
	}
}


/* (even the comment is missing) */

DRIVER_MODULE(umass, uhub, umass_driver, umass_devclass, umass_driver_load, NULL);



#ifdef USB_DEBUG
static void
umass_bbb_dump_cbw(struct umass_softc *sc, umass_bbb_cbw_t *cbw)
{
	int clen = cbw->bCDBLength;
	int dlen = UGETDW(cbw->dCBWDataTransferLength);
	u_int8_t *c = cbw->CBWCDB;
	int tag = UGETDW(cbw->dCBWTag);
	int flags = cbw->bCBWFlags;

	DPRINTF(UDMASS_BBB, ("%s: CBW %d: cmd = %db "
		"(0x%02x%02x%02x%02x%02x%02x%s), "
		"data = %db, dir = %s\n",
		device_get_nameunit(sc->sc_dev), tag, clen,
		c[0], c[1], c[2], c[3], c[4], c[5], (clen > 6? "...":""),
		dlen, (flags == CBWFLAGS_IN? "in":
		       (flags == CBWFLAGS_OUT? "out":"<invalid>"))));
}

static void
umass_bbb_dump_csw(struct umass_softc *sc, umass_bbb_csw_t *csw)
{
	int sig = UGETDW(csw->dCSWSignature);
	int tag = UGETW(csw->dCSWTag);
	int res = UGETDW(csw->dCSWDataResidue);
	int status = csw->bCSWStatus;

	DPRINTF(UDMASS_BBB, ("%s: CSW %d: sig = 0x%08x (%s), tag = %d, "
		"res = %d, status = 0x%02x (%s)\n", device_get_nameunit(sc->sc_dev),
		tag, sig, (sig == CSWSIGNATURE?  "valid":"invalid"),
		tag, res,
		status, (status == CSWSTATUS_GOOD? "good":
			 (status == CSWSTATUS_FAILED? "failed":
			  (status == CSWSTATUS_PHASE? "phase":"<invalid>")))));
}

static void
umass_cbi_dump_cmd(struct umass_softc *sc, void *cmd, int cmdlen)
{
	u_int8_t *c = cmd;
	int dir = sc->transfer_dir;

	DPRINTF(UDMASS_BBB, ("%s: cmd = %db "
		"(0x%02x%02x%02x%02x%02x%02x%s), "
		"data = %db, dir = %s\n",
		device_get_nameunit(sc->sc_dev), cmdlen,
		c[0], c[1], c[2], c[3], c[4], c[5], (cmdlen > 6? "...":""),
		sc->transfer_datalen,
		(dir == DIR_IN? "in":
		 (dir == DIR_OUT? "out":
		  (dir == DIR_NONE? "no data phase": "<invalid>")))));
}

static void
umass_dump_buffer(struct umass_softc *sc, u_int8_t *buffer, int buflen,
		  int printlen)
{
	int i, j;
	char s1[40];
	char s2[40];
	char s3[5];

	s1[0] = '\0';
	s3[0] = '\0';

	ksprintf(s2, " buffer=%p, buflen=%d", buffer, buflen);
	for (i = 0; i < buflen && i < printlen; i++) {
		j = i % 16;
		if (j == 0 && i != 0) {
			DPRINTF(UDMASS_GEN, ("%s: 0x %s%s\n",
				device_get_nameunit(sc->sc_dev), s1, s2));
			s2[0] = '\0';
		}
		ksprintf(&s1[j*2], "%02x", buffer[i] & 0xff);
	}
	if (buflen > printlen)
		ksprintf(s3, " ...");
	DPRINTF(UDMASS_GEN, ("%s: 0x %s%s%s\n",
		device_get_nameunit(sc->sc_dev), s1, s2, s3));
}
#endif
