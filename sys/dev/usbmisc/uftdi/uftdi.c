/*
 * $NetBSD: uftdi.c,v 1.13 2002/09/23 05:51:23 simonb Exp $
 * $FreeBSD: src/sys/dev/usb/uftdi.c,v 1.37 2007/06/22 05:53:05 imp Exp $
 */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * FTDI FT8U100AX serial adapter driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <sys/file.h>

#include <sys/select.h>

#include <sys/sysctl.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbhid.h>

#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#include "../ucom/ucomvar.h"

#include "uftdireg.h"

#ifdef USB_DEBUG
static int uftdidebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, uftdi, CTLFLAG_RW, 0, "USB uftdi");
SYSCTL_INT(_hw_usb_uftdi, OID_AUTO, debug, CTLFLAG_RW,
	   &uftdidebug, 0, "uftdi debug level");
#define DPRINTF(x)      do { \
				if (uftdidebug) \
					kprintf x; \
			} while (0)

#define DPRINTFN(n, x)  do { \
				if (uftdidebug > (n)) \
					kprintf x; \
			} while (0)

#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UFTDI_CONFIG_INDEX	0
#define UFTDI_IFACE_INDEX	0

/*
 * These are the maximum number of bytes transferred per frame.
 * The output buffer size cannot be increased due to the size encoding.
 */
#define UFTDIIBUFSIZE 64
#define UFTDIOBUFSIZE 64

struct uftdi_softc {
	struct ucom_softc	sc_ucom;
	usbd_interface_handle	sc_iface;	/* interface */
	enum uftdi_type		sc_type;
	u_int			sc_hdrlen;
	u_char			sc_msr;
	u_char			sc_lsr;
	u_int			last_lcr;
};

static void	uftdi_get_status(void *, int portno, u_char *lsr, u_char *msr);
static void	uftdi_set(void *, int, int, int);
static int	uftdi_param(void *, int, struct termios *);
static int	uftdi_open(void *sc, int portno);
static void	uftdi_read(void *sc, int portno, u_char **ptr,u_int32_t *count);
static void	uftdi_write(void *sc, int portno, u_char *to, u_char *from,
			    u_int32_t *count);
static void	uftdi_break(void *sc, int portno, int onoff);
static int	uftdi_8u232am_getrate(speed_t speed, int *rate);

struct ucom_callback uftdi_callback = {
	uftdi_get_status,
	uftdi_set,
	uftdi_param,
	NULL,
	uftdi_open,
	NULL,
	uftdi_read,
	uftdi_write,
};

static const struct usb_devno uftdi_devs[] = {
	/* FTDI chips defaults */
	{ USB_DEVICE(0x0403, 0x0232) }, /* FT232 serial converter */
	{ USB_DEVICE(0x0403, 0x6001) }, /* FT232 serial converter */
	{ USB_DEVICE(0x0403, 0x6006) }, /* FT232 serial converter */
	{ USB_DEVICE(0x0403, 0x6007) }, /* FT232 serial converter */
	{ USB_DEVICE(0x0403, 0x6008) }, /* FT232 serial converter */
	{ USB_DEVICE(0x0403, 0x6009) }, /* FT232 serial converter */
	{ USB_DEVICE(0x0403, 0x6010) }, /* FT2232 dual port serial converter */
	{ USB_DEVICE(0x0403, 0x8372) }, /* FTDI 8U100AX USB hub controller */

	/* RR-CirKits products */
	{ USB_DEVICE(0x0403, 0xc7d0) }, /* LocoBuffer USB */

	/* DMX4ALL products */
	{ USB_DEVICE(0x0403, 0xc850) }, /* DMX interface */

	/* ASK products */
	{ USB_DEVICE(0x0403, 0xc990) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc991) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc992) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc993) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc994) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc995) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc996) }, /* RDR 4X7 series card reader */
	{ USB_DEVICE(0x0403, 0xc997) }, /* RDR 4X7 series card reader */

	/* MJS products */
	{ USB_DEVICE(0x0403, 0xca81) }, /* Sirius To PC Interface */

	/* Starting Point Systems products */
	{ USB_DEVICE(0x0403, 0xcaa0) }, /* µChameleon */

	/* Tactrix products */
	{ USB_DEVICE(0x0403, 0xcc48) }, /* OpenPort 1.3 Mitsubishi */
	{ USB_DEVICE(0x0403, 0xcc49) }, /* OpenPort 1.3 Subaru */
	{ USB_DEVICE(0x0403, 0xcc4a) }, /* OpenPort 1.3 Universal */

	/* Plus GSM products */
	{ USB_DEVICE(0x0403, 0xd070) }, /* Plus GSM iPlus */

	/* Xsens Technologies BV products */
	{ USB_DEVICE(0x0403, 0xd388) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd389) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd38a) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd38b) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd38c) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd38d) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd38e) }, /* Serial interface */
	{ USB_DEVICE(0x0403, 0xd38f) }, /* Serial interface */

	/* Eurami Group products */
	{ USB_DEVICE(0x0403, 0xd678) }, /* Gamma Scout Online */

	/* Westrex International products */
	{ USB_DEVICE(0x0403, 0xdc00) }, /* Model 777 */
	{ USB_DEVICE(0x0403, 0xdc01) }, /* Model 8900F */

	/* ACG Identification GmbH products */
	{ USB_DEVICE(0x0403, 0xdd20) }, /* HF Dual ISO Reader (RFID) */

	/* Artemis products */
	{ USB_DEVICE(0x0403, 0xdf28) }, /* CCD camera */

	/* ATIK Instruments products */
	{ USB_DEVICE(0x0403, 0xdf30) }, /* ATK-16 Grayscale Camera */
	{ USB_DEVICE(0x0403, 0xdf31) }, /* ATK-16HR Grayscale Camera */
	{ USB_DEVICE(0x0403, 0xdf32) }, /* ATK-16C Colour Camera */
	{ USB_DEVICE(0x0403, 0xdf33) }, /* ATK-16HRC Colour Camera */

	/* Yost Engineering, Inc. products */
	{ USB_DEVICE(0x0403, 0xe050) }, /* ServoCenter3.1 USB */

	/* EVER Sp. products */
	{ USB_DEVICE(0x0403, 0xe520) }, /* Eco Pro UPS */

	/* Active Robots products */
	{ USB_DEVICE(0x0403, 0xe548) }, /* Active Robots comms board */

	/* Pyramid Computer GmbH products */
	{ USB_DEVICE(0x0403, 0xe6c8) }, /* Pyramid Appliance Display */

	/* Gude Analog- und Digitalsysteme GmbH products */
	{ USB_DEVICE(0x0403, 0xe808) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe809) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe80a) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe80b) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe80c) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe80d) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe80e) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe80f) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe888) }, /* Expert ISDN Control USB */
	{ USB_DEVICE(0x0403, 0xe889) }, /* USB-RS232 OptoBridge */
	{ USB_DEVICE(0x0403, 0xe88a) }, /* Expert mouseCLOCK USB II */
	{ USB_DEVICE(0x0403, 0xe88b) }, /* Precision Clock MSF USB */
	{ USB_DEVICE(0x0403, 0xe88c) }, /* Expert mouseCLOCK USB II HBG */
	{ USB_DEVICE(0x0403, 0xe88d) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe88e) }, /* USB to serial */
	{ USB_DEVICE(0x0403, 0xe88f) }, /* USB to serial */

	/* Eclo, Lda. products */
	{ USB_DEVICE(0x0403, 0xea90) }, /* COM to 1-Wire USB adaptor */

	/* Coastal ChipWorks products */
	{ USB_DEVICE(0x0403, 0xebe0) }, /* TNC-X USB to packet-radio adapter */

	/* Teratronik products */
	{ USB_DEVICE(0x0403, 0xec88) }, /* Teratronik VCP device */

	/* MaxStream products */
	{ USB_DEVICE(0x0403, 0xee18) }, /* PKG-U RF modem */

	/* microHAM products */
	{ USB_DEVICE(0x0403, 0xeee8) }, /* USB-KW interface */
	{ USB_DEVICE(0x0403, 0xeee9) }, /* USB-YS interface */
	{ USB_DEVICE(0x0403, 0xeeea) }, /* USB-Y6 interface */
	{ USB_DEVICE(0x0403, 0xeeeb) }, /* USB-Y8 interface */
	{ USB_DEVICE(0x0403, 0xeeec) }, /* USB-IC interface */
	{ USB_DEVICE(0x0403, 0xeeed) }, /* USB-DB9 interface */
	{ USB_DEVICE(0x0403, 0xeeee) }, /* USB-RS232 interface */
	{ USB_DEVICE(0x0403, 0xeeef) }, /* USB-Y9 interface */

	/* ELV products */
	{ USB_DEVICE(0x0403, 0xf06e) }, /* ALC 8500 Expert */
	{ USB_DEVICE(0x0403, 0xf06f) }, /* FHZ 1000 PC */

	/* Perle Systems products */
	{ USB_DEVICE(0x0403, 0xf0c0) }, /* UltraPort USB */

	/* ACT Solutions products */
	{ USB_DEVICE(0x0403, 0xf2d0) }, /* HomePro ZWave */

	/* 4n-galaxy.de products */
	{ USB_DEVICE(0x0403, 0xf3c0) }, /* Galaxy USB to serial */
	{ USB_DEVICE(0x0403, 0xf3c1) }, /* Galaxy USB to serial */

	/* Linx Technologies products */
	{ USB_DEVICE(0x0403, 0xf448) }, /* Linx SDM-USB-QS-S */
	{ USB_DEVICE(0x0403, 0xf449) }, /* Linx Master Development 2.0 */
	{ USB_DEVICE(0x0403, 0xf44a) }, /* Linx USB to serial */
	{ USB_DEVICE(0x0403, 0xf44b) }, /* Linx USB to serial */
	{ USB_DEVICE(0x0403, 0xf44c) }, /* Linx USB to serial */

	/* Suunto Oy products */
	{ USB_DEVICE(0x0403, 0xf680) }, /* Suunto Sports instrument */

	/* USB-UIRT */
	{ USB_DEVICE(0x0403, 0xf850) }, /* USB-UIRT */

	/* CCS Inc. products */
	{ USB_DEVICE(0x0403, 0xf9d0) }, /* ICD-U20 */
	{ USB_DEVICE(0x0403, 0xf9d1) }, /* ICD-U40 */
	{ USB_DEVICE(0x0403, 0xf9d2) }, /* MACH-X */

	/* Matrix Orbital LCD displays */
	{ USB_DEVICE(0x0403, 0xfa00) }, /* USB Serial */
	{ USB_DEVICE(0x0403, 0xfa01) }, /* MX2 or MX3 LCD */
	{ USB_DEVICE(0x0403, 0xfa02) }, /* MX4 or MX5 LCD */
	{ USB_DEVICE(0x0403, 0xfa03) }, /* LK202-24 LCD */
	{ USB_DEVICE(0x0403, 0xfa04) }, /* LK204-24 LCD */
	{ USB_DEVICE(0x0403, 0xfa05) }, /* USB Serial */
	{ USB_DEVICE(0x0403, 0xfa06) }, /* USB Serial */

	/* Home Electronics products */
	{ USB_DEVICE(0x0403, 0xfa78) }, /* Tira-1 */

	/* PCDJ products */
	{ USB_DEVICE(0x0403, 0xfa88) }, /* DAC-2 */

	/* Inside.fr products */
	{ USB_DEVICE(0x0403, 0xfad0) }, /* Accesso contactless reader */

	/* Thorlabs GmbH products */
	{ USB_DEVICE(0x0403, 0xfaf0) }, /* Motors controller */

	/* ELV products */
	{ USB_DEVICE(0x0403, 0xfb58) }, /* UR 100 */
	{ USB_DEVICE(0x0403, 0xfb5a) }, /* UM 100 */
	{ USB_DEVICE(0x0403, 0xfb5b) }, /* UO 100 */

	/* Crystalfontz products */
	{ USB_DEVICE(0x0403, 0xfc08) }, /* CFA-632 LCD */
	{ USB_DEVICE(0x0403, 0xfc09) }, /* CFA-634 LCD */
	{ USB_DEVICE(0x0403, 0xfc0a) }, /* CFA-547 LCD */
	{ USB_DEVICE(0x0403, 0xfc0b) }, /* CFA-633 LCD */
	{ USB_DEVICE(0x0403, 0xfc0c) }, /* CFA-631 LCD */
	{ USB_DEVICE(0x0403, 0xfc0d) }, /* CFA-635 LCD */
	{ USB_DEVICE(0x0403, 0xfc0e) }, /* CFA-640 LCD */
	{ USB_DEVICE(0x0403, 0xfc0f) }, /* CFA-642 LCD */

	/* IRTrans GmbH products */
	{ USB_DEVICE(0x0403, 0xfc60) }, /* Irtrans device */

	/* Sony Ericsson products */
	{ USB_DEVICE(0x0403, 0xfc82) }, /* DSS-20 SyncStation */

	/* RM Michaelides Software & Elektronik GmbH products */
	{ USB_DEVICE(0x0403, 0xfd60) }, /* CANview USB */

	/* Video Networks Limited / Homechoice products */
	{ USB_DEVICE(0x0403, 0xfe38) }, /* Homechoice broadband modem */

	/* AlphaMicro Components products */
	{ USB_DEVICE(0x0403, 0xff00) }, /* AMC-232USB01 */

	/* Thought Technology Ltd. products */
	{ USB_DEVICE(0x0403, 0xff20) }, /* TT-USB */

	/* IBS elektronik products */
	{ USB_DEVICE(0x0403, 0xff38) }, /* US485 interface */
	{ USB_DEVICE(0x0403, 0xff39) }, /* PIC-Programmer */
	{ USB_DEVICE(0x0403, 0xff3a) }, /* PCMCIA SRAM-cards reader */
	{ USB_DEVICE(0x0403, 0xff3b) }, /* Particel counter PK1 */
	{ USB_DEVICE(0x0403, 0xff3c) }, /* RS232 - Monitor */
	{ USB_DEVICE(0x0403, 0xff3d) }, /* APP 70 dust monitoring */
	{ USB_DEVICE(0x0403, 0xff3e) }, /* PEDO-Modem */
	{ USB_DEVICE(0x0403, 0xff3f) }, /* Future device */

	/* Lawicel products */
	{ USB_DEVICE(0x0403, 0xffa8) }, /* CANUSB device */

	/* Melco, Inc products */
	{ USB_DEVICE(0x0411, 0x00b3) }, /* PC-OP-RS1 RemoteStation */

	/* B&B Electronics products */
	{ USB_DEVICE(0x0856, 0xac01) }, /* USOTL4 */
	{ USB_DEVICE(0x0856, 0xac02) }, /* USTL4 */
	{ USB_DEVICE(0x0856, 0xac03) }, /* USO9ML2 */
	{ USB_DEVICE(0x0856, 0xac11) }, /* USOPTL4 */
	{ USB_DEVICE(0x0856, 0xac12) }, /* USPTL4 */
	{ USB_DEVICE(0x0856, 0xac16) }, /* USO9ML2DR-2 */
	{ USB_DEVICE(0x0856, 0xac17) }, /* USO9ML2DR */
	{ USB_DEVICE(0x0856, 0xac18) }, /* USOPTL4DR-2 */
	{ USB_DEVICE(0x0856, 0xac19) }, /* USOPTL4DR */
	{ USB_DEVICE(0x0856, 0xac25) }, /* 485USB9F-2W */
	{ USB_DEVICE(0x0856, 0xac26) }, /* 485USB9F-4W */
	{ USB_DEVICE(0x0856, 0xac27) }, /* 232USB9M */

	/* Interpid Control Systems products */
	{ USB_DEVICE(0x093c, 0x0601) }, /* ValueCAN */
	{ USB_DEVICE(0x093c, 0x0701) }, /* NeoVI Blue */
	
	/* ID TECH products */
	{ USB_DEVICE(0x0acd, 0x0300) }, /* USB to serial adapter */

	/* Omnidirectional Control Technology products */
	{ USB_DEVICE(0x0b39, 0x0421) }, /* USB to serial */

	/* Icom, Inc. products */
	{ USB_DEVICE(0x0c26, 0x0004) }, /* ID-1 */
	{ USB_DEVICE(0x0c26, 0x0009) }, /* ID-RP2C service 1 */
	{ USB_DEVICE(0x0c26, 0x000a) }, /* ID-RP2C service 2 */
	{ USB_DEVICE(0x0c26, 0x000b) }, /* ID-RP2D */
	{ USB_DEVICE(0x0c26, 0x000c) }, /* ID-RP2V service T */
	{ USB_DEVICE(0x0c26, 0x000d) }, /* ID-RP2V service R */
	{ USB_DEVICE(0x0c26, 0x0011) }, /* ID-RP4000V service R */
	{ USB_DEVICE(0x0c26, 0x0011) }, /* ID-RP4000V service T */
	{ USB_DEVICE(0x0c26, 0x0012) }, /* ID-RP2000V service T */
	{ USB_DEVICE(0x0c26, 0x0013) }, /* ID-RP2000V service R */

	/* Sealevel products */
	{ USB_DEVICE(0x0c52, 0X2811) }, /* SeaLINK+8/232 (2801) Port 1 */
	{ USB_DEVICE(0x0c52, 0X2812) }, /* SeaLINK+8/485 (2802) Port 1 */
	{ USB_DEVICE(0x0c52, 0X2813) }, /* SeaLINK+8 (2803) Port 1 */
	{ USB_DEVICE(0x0c52, 0X2821) }, /* SeaLINK+8/232 (2801) Port 2 */
	{ USB_DEVICE(0x0c52, 0X2822) }, /* SeaLINK+8/485 (2802) Port 2 */
	{ USB_DEVICE(0x0c52, 0X2823) }, /* SeaLINK+8 (2803) Port 2 */
	{ USB_DEVICE(0x0c52, 0X2831) }, /* SeaLINK+8/232 (2801) Port 3 */
	{ USB_DEVICE(0x0c52, 0X2832) }, /* SeaLINK+8/485 (2802) Port 3 */
	{ USB_DEVICE(0x0c52, 0X2833) }, /* SeaLINK+8 (2803) Port 3 */
	{ USB_DEVICE(0x0c52, 0X2841) }, /* SeaLINK+8/232 (2801) Port 4 */
	{ USB_DEVICE(0x0c52, 0X2842) }, /* SeaLINK+8/485 (2802) Port 4 */
	{ USB_DEVICE(0x0c52, 0X2843) }, /* SeaLINK+8 (2803) Port 4 */
	{ USB_DEVICE(0x0c52, 0X2851) }, /* SeaLINK+8/232 (2801) Port 5 */
	{ USB_DEVICE(0x0c52, 0X2852) }, /* SeaLINK+8/485 (2802) Port 5 */
	{ USB_DEVICE(0x0c52, 0X2853) }, /* SeaLINK+8 (2803) Port 5 */
	{ USB_DEVICE(0x0c52, 0X2861) }, /* SeaLINK+8/232 (2801) Port 6 */
	{ USB_DEVICE(0x0c52, 0X2862) }, /* SeaLINK+8/485 (2802) Port 6 */
	{ USB_DEVICE(0x0c52, 0X2863) }, /* SeaLINK+8 (2803) Port 6 */
	{ USB_DEVICE(0x0c52, 0X2871) }, /* SeaLINK+8/232 (2801) Port 7 */
	{ USB_DEVICE(0x0c52, 0X2872) }, /* SeaLINK+8/485 (2802) Port 7 */
	{ USB_DEVICE(0x0c52, 0X2873) }, /* SeaLINK+8 (2803) Port 7 */
	{ USB_DEVICE(0x0c52, 0X2881) }, /* SeaLINK+8/232 (2801) Port 8 */
	{ USB_DEVICE(0x0c52, 0X2882) }, /* SeaLINK+8/485 (2802) Port 8 */
	{ USB_DEVICE(0x0c52, 0X2883) }, /* SeaLINK+8 (2803) Port 8 */
	{ USB_DEVICE(0x0c52, 0x2101) }, /* SeaLINK+232 (2101/2105) */
	{ USB_DEVICE(0x0c52, 0x2102) }, /* SeaLINK+485 (2102) */
	{ USB_DEVICE(0x0c52, 0x2103) }, /* SeaLINK+232I (2103) */
	{ USB_DEVICE(0x0c52, 0x2104) }, /* SeaLINK+485I (2104) */
	{ USB_DEVICE(0x0c52, 0x2211) }, /* SeaPORT+2/232 (2201) Port 1 */
	{ USB_DEVICE(0x0c52, 0x2212) }, /* SeaPORT+2/485 (2202) Port 1 */
	{ USB_DEVICE(0x0c52, 0x2213) }, /* SeaPORT+2 (2203) Port 1 */
	{ USB_DEVICE(0x0c52, 0x2221) }, /* SeaPORT+2/232 (2201) Port 2 */
	{ USB_DEVICE(0x0c52, 0x2222) }, /* SeaPORT+2/485 (2202) Port 2 */
	{ USB_DEVICE(0x0c52, 0x2223) }, /* SeaPORT+2 (2203) Port 2 */
	{ USB_DEVICE(0x0c52, 0x2411) }, /* SeaPORT+4/232 (2401) Port 1 */
	{ USB_DEVICE(0x0c52, 0x2412) }, /* SeaPORT+4/485 (2402) Port 1 */
	{ USB_DEVICE(0x0c52, 0x2413) }, /* SeaPORT+4 (2403) Port 1 */
	{ USB_DEVICE(0x0c52, 0x2421) }, /* SeaPORT+4/232 (2401) Port 2 */
	{ USB_DEVICE(0x0c52, 0x2422) }, /* SeaPORT+4/485 (2402) Port 2 */
	{ USB_DEVICE(0x0c52, 0x2423) }, /* SeaPORT+4 (2403) Port 2 */
	{ USB_DEVICE(0x0c52, 0x2431) }, /* SeaPORT+4/232 (2401) Port 3 */
	{ USB_DEVICE(0x0c52, 0x2432) }, /* SeaPORT+4/485 (2402) Port 3 */
	{ USB_DEVICE(0x0c52, 0x2433) }, /* SeaPORT+4 (2403) Port 3 */
	{ USB_DEVICE(0x0c52, 0x2441) }, /* SeaPORT+4/232 (2401) Port 4 */
	{ USB_DEVICE(0x0c52, 0x2442) }, /* SeaPORT+4/485 (2402) Port 4 */
	{ USB_DEVICE(0x0c52, 0x2443) }, /* SeaPORT+4 (2403) Port 4 */
	{ USB_DEVICE(0x0c52, 0x9020) }, /* SeaLINK+422 (2106) */

	/* Posiflex Technologies products */
	{ USB_DEVICE(0x0d3a, 0x0300) }, /* PP7000 series printer */
	{ USB_DEVICE(0x0d3a, 0x0400) }, /* PP7000 series printer */

	/* Kobil Systems products */
	{ USB_DEVICE(0x0d46, 0x2020) }, /* Konverter for B1 */
	{ USB_DEVICE(0x0d46, 0x2021) }, /* Konverter for KAAN */

	/* Falcom Wireless Communications products */
	{ USB_DEVICE(0x0f94, 0x0001) }, /* Twist USB GPRS modem */
	{ USB_DEVICE(0x0f94, 0x0005) }, /* Samba USB GPRS modem */

	/* Thurlby Thandar Instruments products */
	{ USB_DEVICE(0x103e, 0x03e8) }, /* QL355P power supply */

	/* InterBiometrics products */
	{ USB_DEVICE(0x1209, 0x1002) }, /* IO Board */
	{ USB_DEVICE(0x1209, 0x1006) }, /* Mini IO Board */

	/* Testo AG products */
	{ USB_DEVICE(0x128d, 0x0001) }, /* 175/177 USB interface */
	{ USB_DEVICE(0x128d, 0x0002) }, /* 330 USB interface */
	{ USB_DEVICE(0x128d, 0x0003) }, /* 435/635/735 USB interface */
	{ USB_DEVICE(0x128d, 0x0004) }, /* 845 USB interface */
	{ USB_DEVICE(0x128d, 0x0005) }, /* Service adapter */
	{ USB_DEVICE(0x128d, 0x0006) }, /* 580 USB interface */
	{ USB_DEVICE(0x128d, 0x0007) }, /* 174 USB interface */
	{ USB_DEVICE(0x128d, 0x0009) }, /* 556/560 USB interface */
	{ USB_DEVICE(0x128d, 0x000a) }, /* USB adapter */
	{ USB_DEVICE(0x128d, 0xf001) }, /* USB to serial converter */

	/* Mobility products */
	{ USB_DEVICE(0x1342, 0x0202) }, /* EasiDock 200 serial port */

	/* Papouch s.r.o. products */
	{ USB_DEVICE(0x5050, 0x0100) }, /* SB485 USB-485/422 Converter */
	{ USB_DEVICE(0x5050, 0x0101) }, /* AP485 USB-RS485 Converter */
	{ USB_DEVICE(0x5050, 0x0102) }, /* SB422 USB-RS422 Converter */
	{ USB_DEVICE(0x5050, 0x0103) }, /* SB485 USB-485/422 Converter */
	{ USB_DEVICE(0x5050, 0x0104) }, /* AP485 USB-RS485 Converter */
	{ USB_DEVICE(0x5050, 0x0105) }, /* SB422 USB-RS422 Converter */
	{ USB_DEVICE(0x5050, 0x0106) }, /* SB485S USB-485/422 Converter */
	{ USB_DEVICE(0x5050, 0x0107) }, /* SB485C USB-485/422 Converter */
	{ USB_DEVICE(0x5050, 0x0200) }, /* USB Device */
	{ USB_DEVICE(0x5050, 0x0300) }, /* LEC USB Converter */
	{ USB_DEVICE(0x5050, 0x0301) }, /* SB232 USB-RS232 Converter */
	{ USB_DEVICE(0x5050, 0x0400) }, /* TMU Thermometer */
	{ USB_DEVICE(0x5050, 0x0500) }, /* IRAmp Duplex */
	{ USB_DEVICE(0x5050, 0x0700) }, /* DRAK5 */
	{ USB_DEVICE(0x5050, 0x0800) }, /* QUIDO USB 8/8 */
	{ USB_DEVICE(0x5050, 0x0900) }, /* QUIDO USB 4/4 */
	{ USB_DEVICE(0x5050, 0x0A00) }, /* QUIDO USB 2/2 */
	{ USB_DEVICE(0x5050, 0x0B00) }, /* QUIDO USB 10/1 */
	{ USB_DEVICE(0x5050, 0x0C00) }, /* QUIDO USB 30/3 */
	{ USB_DEVICE(0x5050, 0x0D00) }, /* QUIDO USB 60(100)/3 */
	{ USB_DEVICE(0x5050, 0x0E00) }, /* QUIDO USB 2/16 */
	{ USB_DEVICE(0x5050, 0x0F00) }, /* QUIDO USB 3/32 */
	{ USB_DEVICE(0x5050, 0x1000) }, /* DRAK6 USB */
	{ USB_DEVICE(0x5050, 0x8000) }, /* UPS-USB Stavovy Adapter */
	{ USB_DEVICE(0x5050, 0x8001) }, /* MU Controller */
	{ USB_DEVICE(0x5050, 0x8002) }, /* SimuKey */
	{ USB_DEVICE(0x5050, 0x8003) }, /* AD4USB */
	{ USB_DEVICE(0x5050, 0x8004) }, /* GOLIATH MUX */
	{ USB_DEVICE(0x5050, 0x8005) }, /* GOLIATH MSR */

	/* Evolution Robotics, Inc. products */
	{ USB_DEVICE(0xdeee, 0x0300) }, /* ER1 Control Module */
	{ USB_DEVICE(0xdeee, 0x0302) }, /* RCM4 interface */
	{ USB_DEVICE(0xdeee, 0x0303) }, /* RCM4 interface */
};

static int
uftdi_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	if (usb_lookup(uftdi_devs, uaa->vendor, uaa->product) != NULL)
		return (UMATCH_VENDOR_IFACESUBCLASS);

	return (UMATCH_NONE);
}

static int
uftdi_attach(device_t self)
{
	struct uftdi_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usbd_device_handle dev = uaa->device;
	usbd_interface_handle iface;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;
	struct ucom_softc *ucom = &sc->sc_ucom;
	DPRINTFN(10,("\nuftdi_attach: sc=%p\n", sc));

	ucom->sc_dev = self;
	ucom->sc_udev = dev;

	iface = uaa->iface;
	id = usbd_get_interface_descriptor(iface);
	ucom->sc_iface = iface;

	if (uaa->release < 0x0200) {
		sc->sc_type = UFTDI_TYPE_SIO;
		sc->sc_hdrlen = 1;
	} else {
		sc->sc_type = UFTDI_TYPE_8U232AM;
		sc->sc_hdrlen = 0;
	}

	ucom->sc_bulkin_no = ucom->sc_bulkout_no = -1;

	for (i = 0; i < id->bNumEndpoints; i++) {
		int addr, dir, attr;
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			device_printf(ucom->sc_dev,
			    "could not read endpoint descriptor\n");
			goto bad;
		}

		addr = ed->bEndpointAddress;
		dir = UE_GET_DIR(ed->bEndpointAddress);
		attr = ed->bmAttributes & UE_XFERTYPE;
		if (dir == UE_DIR_IN && attr == UE_BULK)
			ucom->sc_bulkin_no = addr;
		else if (dir == UE_DIR_OUT && attr == UE_BULK)
			ucom->sc_bulkout_no = addr;
		else {
			device_printf(ucom->sc_dev, "unexpected endpoint\n");
			goto bad;
		}
	}
	if (ucom->sc_bulkin_no == -1) {
		device_printf(ucom->sc_dev, "Could not find data bulk in\n");
		goto bad;
	}
	if (ucom->sc_bulkout_no == -1) {
		device_printf(ucom->sc_dev, "Could not find data bulk out\n");
		goto bad;
	}
	ucom->sc_parent  = sc;
	ucom->sc_portno = FTDI_PIT_SIOA + id->bInterfaceNumber;
	/* bulkin, bulkout set above */

  	ucom->sc_ibufsize = UFTDIIBUFSIZE;
	ucom->sc_obufsize = UFTDIOBUFSIZE - sc->sc_hdrlen;
	ucom->sc_ibufsizepad = UFTDIIBUFSIZE;
	ucom->sc_opkthdrlen = sc->sc_hdrlen;


	ucom->sc_callback = &uftdi_callback;
#if 0
	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, ucom->sc_udev,
	  ucom->sc_dev);
#endif
	DPRINTF(("uftdi: in=0x%x out=0x%x\n", ucom->sc_bulkin_no, ucom->sc_bulkout_no));
	ucom_attach(&sc->sc_ucom);
	return 0;

bad:
	DPRINTF(("uftdi_attach: ATTACH ERROR\n"));
	ucom->sc_dying = 1;
	return ENXIO;
}
#if 0
int
uftdi_activate(device_t self, enum devact act)
{
	struct uftdi_softc *sc = (struct uftdi_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_ACTIVATE:
		return (EOPNOTSUPP);

	case DVACT_DEACTIVATE:
		if (sc->sc_subdev != NULL)
			rv = config_deactivate(sc->sc_subdev);
		sc->sc_ucom.sc_dying = 1;
		break;
	}
	return (rv);
}
#endif

static int
uftdi_detach(device_t self)
{
	struct uftdi_softc *sc = device_get_softc(self);

	int rv = 0;

	DPRINTF(("uftdi_detach: sc=%p\n", sc));
	sc->sc_ucom.sc_dying = 1;
	rv = ucom_detach(&sc->sc_ucom);

	return rv;
}

static int
uftdi_open(void *vsc, int portno)
{
	struct uftdi_softc *sc = vsc;
	struct ucom_softc *ucom = &sc->sc_ucom;
	usb_device_request_t req;
	usbd_status err;
	struct termios t;

	DPRINTF(("uftdi_open: sc=%p\n", sc));

	if (ucom->sc_dying)
		return (EIO);

	/* Perform a full reset on the device */
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_RESET;
	USETW(req.wValue, FTDI_SIO_RESET_SIO);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(ucom->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	/* Set 9600 baud, 2 stop bits, no parity, 8 bits */
	t.c_ospeed = 9600;
	t.c_cflag = CSTOPB | CS8;
	(void)uftdi_param(sc, portno, &t);

	/* Turn on RTS/CTS flow control */
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_SET_FLOW_CTRL;
	USETW(req.wValue, 0);
	USETW2(req.wIndex, FTDI_SIO_RTS_CTS_HS, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(ucom->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	return (0);
}

static void
uftdi_read(void *vsc, int portno, u_char **ptr, u_int32_t *count)
{
	struct uftdi_softc *sc = vsc;
	u_char msr, lsr;

	DPRINTFN(15,("uftdi_read: sc=%p, port=%d count=%d\n", sc, portno,
		     *count));

	msr = FTDI_GET_MSR(*ptr);
	lsr = FTDI_GET_LSR(*ptr);

#ifdef USB_DEBUG
	if (*count != 2)
		DPRINTFN(10,("uftdi_read: sc=%p, port=%d count=%d data[0]="
			    "0x%02x\n", sc, portno, *count, (*ptr)[2]));
#endif

	if (sc->sc_msr != msr ||
	    (sc->sc_lsr & FTDI_LSR_MASK) != (lsr & FTDI_LSR_MASK)) {
		DPRINTF(("uftdi_read: status change msr=0x%02x(0x%02x) "
			 "lsr=0x%02x(0x%02x)\n", msr, sc->sc_msr,
			 lsr, sc->sc_lsr));
		sc->sc_msr = msr;
		sc->sc_lsr = lsr;
		ucom_status_change(&sc->sc_ucom);
	}

	/* Pick up status and adjust data part. */
	*ptr += 2;
	*count -= 2;
}

static void
uftdi_write(void *vsc, int portno, u_char *to, u_char *from, u_int32_t *count)
{
	struct uftdi_softc *sc = vsc;

	DPRINTFN(10,("uftdi_write: sc=%p, port=%d count=%u data[0]=0x%02x\n",
		     vsc, portno, *count, from[0]));

	/* Make length tag and copy data */
	if (sc->sc_hdrlen > 0)
		*to = FTDI_OUT_TAG(*count, portno);

	memcpy(to + sc->sc_hdrlen, from, *count);
	*count += sc->sc_hdrlen;
}

static void
uftdi_set(void *vsc, int portno, int reg, int onoff)
{
	struct uftdi_softc *sc = vsc;
	struct ucom_softc *ucom = vsc;
	usb_device_request_t req;
	int ctl;

	DPRINTF(("uftdi_set: sc=%p, port=%d reg=%d onoff=%d\n", vsc, portno,
		 reg, onoff));

	switch (reg) {
	case UCOM_SET_DTR:
		ctl = onoff ? FTDI_SIO_SET_DTR_HIGH : FTDI_SIO_SET_DTR_LOW;
		break;
	case UCOM_SET_RTS:
		ctl = onoff ? FTDI_SIO_SET_RTS_HIGH : FTDI_SIO_SET_RTS_LOW;
		break;
	case UCOM_SET_BREAK:
		uftdi_break(sc, portno, onoff);
		return;
	default:
		return;
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_MODEM_CTRL;
	USETW(req.wValue, ctl);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	DPRINTFN(2,("uftdi_set: reqtype=0x%02x req=0x%02x value=0x%04x "
		    "index=0x%04x len=%d\n", req.bmRequestType, req.bRequest,
		    UGETW(req.wValue), UGETW(req.wIndex), UGETW(req.wLength)));
	(void)usbd_do_request(ucom->sc_udev, &req, NULL);
}

static int
uftdi_param(void *vsc, int portno, struct termios *t)
{
	struct uftdi_softc *sc = vsc;
	struct ucom_softc *ucom = &sc->sc_ucom;
	usb_device_request_t req;
	usbd_status err;
	int rate=0, data, flow;

	DPRINTF(("uftdi_param: sc=%p\n", sc));

	if (ucom->sc_dying)
		return (EIO);

	switch (sc->sc_type) {
	case UFTDI_TYPE_SIO:
		switch (t->c_ospeed) {
		case 300: rate = ftdi_sio_b300; break;
		case 600: rate = ftdi_sio_b600; break;
		case 1200: rate = ftdi_sio_b1200; break;
		case 2400: rate = ftdi_sio_b2400; break;
		case 4800: rate = ftdi_sio_b4800; break;
		case 9600: rate = ftdi_sio_b9600; break;
		case 19200: rate = ftdi_sio_b19200; break;
		case 38400: rate = ftdi_sio_b38400; break;
		case 57600: rate = ftdi_sio_b57600; break;
		case 115200: rate = ftdi_sio_b115200; break;
		default:
			return (EINVAL);
		}
		break;

	case UFTDI_TYPE_8U232AM:
		if (uftdi_8u232am_getrate(t->c_ospeed, &rate) == -1)
			return (EINVAL);
		break;
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_SET_BAUD_RATE;
	USETW(req.wValue, rate);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	DPRINTFN(2,("uftdi_param: reqtype=0x%02x req=0x%02x value=0x%04x "
		    "index=0x%04x len=%d\n", req.bmRequestType, req.bRequest,
		    UGETW(req.wValue), UGETW(req.wIndex), UGETW(req.wLength)));
	err = usbd_do_request(ucom->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	if (ISSET(t->c_cflag, CSTOPB))
		data = FTDI_SIO_SET_DATA_STOP_BITS_2;
	else
		data = FTDI_SIO_SET_DATA_STOP_BITS_1;
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= FTDI_SIO_SET_DATA_PARITY_ODD;
		else
			data |= FTDI_SIO_SET_DATA_PARITY_EVEN;
	} else
		data |= FTDI_SIO_SET_DATA_PARITY_NONE;
	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data |= FTDI_SIO_SET_DATA_BITS(5);
		break;
	case CS6:
		data |= FTDI_SIO_SET_DATA_BITS(6);
		break;
	case CS7:
		data |= FTDI_SIO_SET_DATA_BITS(7);
		break;
	case CS8:
		data |= FTDI_SIO_SET_DATA_BITS(8);
		break;
	}
	sc->last_lcr = data;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_SET_DATA;
	USETW(req.wValue, data);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	DPRINTFN(2,("uftdi_param: reqtype=0x%02x req=0x%02x value=0x%04x "
		    "index=0x%04x len=%d\n", req.bmRequestType, req.bRequest,
		    UGETW(req.wValue), UGETW(req.wIndex), UGETW(req.wLength)));
	err = usbd_do_request(ucom->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	if (ISSET(t->c_cflag, CRTSCTS)) {
		flow = FTDI_SIO_RTS_CTS_HS;
		USETW(req.wValue, 0);
	} else if (ISSET(t->c_iflag, IXON|IXOFF)) {
		flow = FTDI_SIO_XON_XOFF_HS;
		USETW2(req.wValue, t->c_cc[VSTOP], t->c_cc[VSTART]);
	} else {
		flow = FTDI_SIO_DISABLE_FLOW_CTRL;
		USETW(req.wValue, 0);
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_SET_FLOW_CTRL;
	USETW2(req.wIndex, flow, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(ucom->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	return (0);
}

void
uftdi_get_status(void *vsc, int portno, u_char *lsr, u_char *msr)
{
	struct uftdi_softc *sc = vsc;

	DPRINTF(("uftdi_status: msr=0x%02x lsr=0x%02x\n",
		 sc->sc_msr, sc->sc_lsr));

	if (msr != NULL)
		*msr = sc->sc_msr;
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
}

void
uftdi_break(void *vsc, int portno, int onoff)
{
	struct uftdi_softc *sc = vsc;
	struct ucom_softc *ucom = vsc;

	usb_device_request_t req;
	int data;

	DPRINTF(("uftdi_break: sc=%p, port=%d onoff=%d\n", vsc, portno,
		  onoff));

	if (onoff) {
		data = sc->last_lcr | FTDI_SIO_SET_BREAK;
	} else {
		data = sc->last_lcr;
	}

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FTDI_SIO_SET_DATA;
	USETW(req.wValue, data);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	(void)usbd_do_request(ucom->sc_udev, &req, NULL);
}

static int
uftdi_8u232am_getrate(speed_t speed, int *rate)
{
	/* Table of the nearest even powers-of-2 for values 0..15. */
	static const unsigned char roundoff[16] = {
		0, 2, 2, 4,  4,  4,  8,  8,
		8, 8, 8, 8, 16, 16, 16, 16,
	};

	unsigned int d, freq;
	int result;

	if (speed <= 0)
		return (-1);

	/* Special cases for 2M and 3M. */
	if (speed >= 3000000 * 100 / 103 &&
	    speed <= 3000000 * 100 / 97) {
		result = 0;
		goto done;
	}
	if (speed >= 2000000 * 100 / 103 &&
	    speed <= 2000000 * 100 / 97) {
		result = 1;
		goto done;
	}

	d = (FTDI_8U232AM_FREQ << 4) / speed;
	d = (d & ~15) + roundoff[d & 15];

	if (d < FTDI_8U232AM_MIN_DIV)
		d = FTDI_8U232AM_MIN_DIV;
	else if (d > FTDI_8U232AM_MAX_DIV)
		d = FTDI_8U232AM_MAX_DIV;

	/* 
	 * Calculate the frequency needed for d to exactly divide down
	 * to our target speed, and check that the actual frequency is
	 * within 3% of this.
	 */
	freq = speed * d;
	if (freq < (quad_t)(FTDI_8U232AM_FREQ << 4) * 100 / 103 ||
	    freq > (quad_t)(FTDI_8U232AM_FREQ << 4) * 100 / 97)
		return (-1);

	/* 
	 * Pack the divisor into the resultant value.  The lower
	 * 14-bits hold the integral part, while the upper 2 bits
	 * encode the fractional component: either 0, 0.5, 0.25, or
	 * 0.125.
	 */
	result = d >> 4;
	if (d & 8)
		result |= 0x4000;
	else if (d & 4)
		result |= 0x8000;
	else if (d & 2)
		result |= 0xc000;

done:
	*rate = result;
	return (0);
}
static device_method_t uftdi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, uftdi_match),
	DEVMETHOD(device_attach, uftdi_attach),
	DEVMETHOD(device_detach, uftdi_detach),

	{ 0, 0 }
};

static driver_t uftdi_driver = {
	"ucom",
	uftdi_methods,
	sizeof (struct uftdi_softc)
};

DRIVER_MODULE(uftdi, uhub, uftdi_driver, ucom_devclass, usbd_driver_load, NULL);
MODULE_DEPEND(uftdi, usb, 1, 1, 1);
MODULE_DEPEND(uftdi, ucom,UCOM_MINVER, UCOM_PREFVER, UCOM_MAXVER);
